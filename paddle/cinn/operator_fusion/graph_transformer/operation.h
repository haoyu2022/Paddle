// Copyright (c) 2024 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "paddle/cinn/operator_fusion/pattern_fuser.h"
#include "paddle/cinn/operator_fusion/pattern_graph.h"

namespace cinn::fusion {

// Operation

struct MergeTrivialPatternOperation {
  void operator()(PatternGraph* graph, PatternNodePtr upstream) {
    PADDLE_ENFORCE_GE(upstream->downstream().size(),
                      1,
                      phi::errors::PreconditionNotMet(
                          "The trivial pattern wait for sinking should has "
                          "at least 1 downstream , but got %d.",
                          upstream->downstream().size()));

    std::vector<PatternNodePtr> fusion_candidate = upstream->downstream();
    upstream->ClearDownstream();

    for (const auto& downstream : fusion_candidate) {
      bool can_fuse =
          std::holds_alternative<ReducePattern>(downstream->stmt_pattern()) ||
          std::holds_alternative<TrivialPattern>(downstream->stmt_pattern()) ||
          std::holds_alternative<ReduceTreePattern>(
              downstream->stmt_pattern()) ||
          std::holds_alternative<ReduceTreePlusTrivialPattern>(
              downstream->stmt_pattern()) ||
          std::holds_alternative<AnchorPattern>(downstream->stmt_pattern());

      if (can_fuse) {
        auto merged_node = graph->MergeNode(upstream, downstream, MergePattern);
        graph->RemoveNode(downstream);
        VLOG(4) << "Spliting trivial pattern: \nupstream "
                << upstream->DebugStr() << "\ndownstream "
                << downstream->DebugStr() << "\nmerged "
                << merged_node->DebugStr();
        merged_node->AppendInstr(std::make_shared<TrivialInlineInstr>(
            upstream->id(), downstream->id(), merged_node->id()));
      } else {
        upstream->AddNodeToDownstream(downstream);
      }
    }
    if (upstream->downstream().empty()) {
      graph->RemoveNode(upstream);
    }
  }
};

struct MergeReduceTreeOperation {
  PatternNodePtr operator()(PatternGraph* graph, PatternNodePtr node) {
    PADDLE_ENFORCE_EQ(
        node->downstream().size(),
        1,
        phi::errors::PreconditionNotMet(
            "The downstream of the ReduceTree node should be 1, but got %d.",
            node->downstream().size()));
    auto downstream = node->downstream().at(0);
    auto merged_node = graph->MergeNode(node, downstream, MergePattern);
    graph->RemoveNode(downstream);
    graph->RemoveNode(node);
    VLOG(4) << "MergeReduceTreeOperation: \nupstream " << node->DebugStr()
            << "\ndownstream " << downstream->DebugStr() << "\nmerged "
            << merged_node->DebugStr();
    merged_node->UpdateTracker();
    return merged_node;
  }
};

struct MergeReduceTreeAndTrivialOperation {
  PatternNodePtr operator()(PatternGraph* graph, PatternNodePtr node) {
    PADDLE_ENFORCE_EQ(
        node->downstream().size(),
        1,
        phi::errors::PreconditionNotMet(
            "The downstream of the ReduceTree node should be 1, but got %d.",
            node->downstream().size()));
    auto downstream = node->downstream().at(0);
    auto fake_reduce_iter_idx = graph->policy_manager()
                                    .template GetPolicy<RelativeJudgePolicy>()
                                    ->GetFakeReduceIterIdx(node, downstream);
    const auto merge_pattern_fn = [&fake_reduce_iter_idx](
                                      const StmtPattern& first,
                                      const StmtPattern& secend) {
      auto rt_pattern =
          std::get<ReduceTreePlusTrivialPattern>(MergePattern(first, secend));
      rt_pattern.fake_reduce_iter_idx = fake_reduce_iter_idx;
      return rt_pattern;
    };
    PatternNodePtr merged_node =
        graph->MergeNode(node, downstream, merge_pattern_fn);
    graph->RemoveNode(downstream);
    graph->RemoveNode(node);
    VLOG(4) << "MergeReduceTreeAndTrivialOperation: \nupstream "
            << node->DebugStr() << "\ndownstream " << downstream->DebugStr()
            << "\nmerged " << merged_node->DebugStr();
    merged_node->UpdateTracker();
    return merged_node;
  }
};

struct LiftReduceToReduceTreeOperation {
  PatternNodePtr operator()(PatternGraph* graph, PatternNodePtr node) {
    auto origin_name = node->id();
    const auto& reduce_pattern = std::get<ReducePattern>(node->stmt_pattern());
    node->set_stmt_pattern(ReduceTreePattern(
        {},
        reduce_pattern,
        std::make_shared<FusionTracker>(reduce_pattern.tracker_)));
    VLOG(4) << "Make CopyInstr: " << origin_name << " -> " << node->id();
    node->AppendInstr(std::make_shared<CopyInstr>(origin_name, node->id()));
    return node;
  }
};

struct LiftToHorizontalFusionPatternOperation {
  PatternNodePtr operator()(PatternGraph* graph, PatternNodePtr node) {
    auto origin_name = node->id();
    node->set_stmt_pattern(HorizontalFusionPattern(
        {typename HorizontalFusionPattern::PaddingStmtPattern(
            node->stmt_pattern(), {})},
        std::make_shared<FusionTracker>(
            GetFusionTracker(node->stmt_pattern()))));
    VLOG(4) << "Make CopyInstr: " << origin_name << " -> " << node->id();
    node->AppendInstr(std::make_shared<CopyInstr>(origin_name, node->id()));
    return node;
  }
};

struct LiftToAnchorPatternOperation {
  PatternNodePtr operator()(PatternGraph* graph, PatternNodePtr node) {
    auto origin_name = node->id();
    std::vector<pir::Operation*> ops = GetOpsInPattern(node->stmt_pattern());
    // TODO(@wuzhanfei) move sink_op into pattern (currently, part of pattern
    // type has sink and the others not) then, update logic here
    PADDLE_ENFORCE_EQ(
        node->sink_op()->num_results(),
        1,
        phi::errors::PreconditionNotMet(
            "Op with multi output value can not lift to AnchorPattern"));
    pir::Value anchor = node->sink_op()->result(0);
    node->set_stmt_pattern(AnchorPattern(
        ops,
        anchor,
        AnchorState(InitExprPromise(node->stmt_pattern(), anchor)),
        std::make_shared<FusionTracker>(
            GetFusionTracker(node->stmt_pattern()))));
    node->AppendInstr(std::make_shared<CopyInstr>(origin_name, node->id()));
    VLOG(4) << "LiftToAnchorPatternOperation: remain tracker: "
            << GetFusionTracker(node->stmt_pattern())->DebugStr();
    return node;
  }
};

struct FuseUpstreamAnchorOperation {
  PatternNodePtr operator()(PatternGraph* graph,
                            const PatternNodePtr& upstream,
                            const PatternNodePtr& downstream) {
    auto optional_transform_route =
        graph->policy_manager()
            .template GetPolicy<AnchorSearchPolicy>()
            ->FindUpstreamAnchorTransformRoute(upstream, downstream);
    PADDLE_ENFORCE_NE(
        optional_transform_route,
        std::nullopt,
        phi::errors::PreconditionNotMet("Transform Route Not Found"));

    auto transform_route = optional_transform_route.value();

    const auto merge_pattern_fn = [transform_route](
                                      const StmtPattern& source,
                                      const StmtPattern& destination) {
      auto new_anchor_pattern =
          std::get<AnchorPattern>(MergePattern(source, destination));
      auto transformed_anchor_state = ApplyAnchorTransformRoute(
          GetAnchorState(std::get<AnchorPattern>(destination)),
          transform_route);
      new_anchor_pattern.anchor_state.update(
          GetAnchorState(std::get<AnchorPattern>(source)));
      new_anchor_pattern.anchor_state.update(transformed_anchor_state);
      return new_anchor_pattern;
    };

    auto merged_node = graph->MergeNode(upstream, downstream, merge_pattern_fn);
    graph->RemoveNode(upstream);
    graph->RemoveNode(downstream);
    merged_node->UpdateTracker();
    return merged_node;
  }
};

struct FuseDownstreamAnchorOperation {
  PatternNodePtr operator()(PatternGraph* graph,
                            const PatternNodePtr& upstream,
                            const PatternNodePtr& downstream) {
    auto optional_transform_route =
        graph->policy_manager()
            .template GetPolicy<AnchorSearchPolicy>()
            ->FindDownstreamAnchorTransformRoute(upstream, downstream);

    PADDLE_ENFORCE_NE(
        optional_transform_route,
        std::nullopt,
        phi::errors::PreconditionNotMet("Transform Route Not Found"));

    auto transform_route = optional_transform_route.value();

    const auto merge_pattern_fn = [transform_route](
                                      const StmtPattern& destination,
                                      const StmtPattern& source) {
      auto new_anchor_pattern =
          std::get<AnchorPattern>(MergePattern(source, destination));
      auto transformed_anchor_state = ApplyAnchorTransformRoute(
          GetAnchorState(std::get<AnchorPattern>(destination)),
          transform_route);
      new_anchor_pattern.anchor_state.update(
          GetAnchorState(std::get<AnchorPattern>(source)));
      new_anchor_pattern.anchor_state.update(transformed_anchor_state);
      return new_anchor_pattern;
    };

    auto merged_node = graph->MergeNode(upstream, downstream, merge_pattern_fn);
    graph->RemoveNode(upstream);
    graph->RemoveNode(downstream);
    merged_node->UpdateTracker();
    return merged_node;
  }
};

struct SplitRecomputeOperation {
  void operator()(PatternGraph* graph, PatternNodePtr upstream) {
    auto origin_name = upstream->id();
    VLOG(4) << "SplitRecomputeOperation: upstream tracker is: "
            << GetFusionTracker(upstream->stmt_pattern())->DebugStr();
    upstream->set_stmt_pattern(RecoverAnchorPatternToTrivial(
        std::get<AnchorPattern>(upstream->stmt_pattern())));
    VLOG(4) << "Make CopyInstr: " << origin_name << " -> " << upstream->id();
    upstream->AppendInstr(
        std::make_shared<CopyInstr>(origin_name, upstream->id()));
    VLOG(4) << "After SplitRecomputeOperation: upstream tracker is: "
            << GetFusionTracker(upstream->stmt_pattern())->DebugStr();
    MergeTrivialPatternOperation()(graph, upstream);
  }
};

struct HorizontalFusionOperation {
  PatternNodePtr operator()(PatternGraph* graph,
                            const PatternNodePtr& i,
                            const PatternNodePtr& j) {
    VLOG(4) << "Start HorizontalFusionOperation";
    PADDLE_ENFORCE_EQ(
        GetPatternName(i->stmt_pattern()),
        HorizontalFusionPattern::name(),
        phi::errors::PreconditionNotMet(
            "The pattern of the first node should be HorizontalFusionPattern, "
            "but got %s.",
            GetPatternName(i->stmt_pattern())));
    PADDLE_ENFORCE_EQ(
        GetPatternName(j->stmt_pattern()),
        HorizontalFusionPattern::name(),
        phi::errors::PreconditionNotMet(
            "The pattern of the second node should be HorizontalFusionPattern, "
            "but got %s.",
            GetPatternName(j->stmt_pattern())));
    auto merged_node = graph->MergeNode(i, j, MergePattern);
    VLOG(4) << "MergeHorizontalPattern: \ni " << i->DebugStr() << "\nj "
            << j->DebugStr() << "\nmerged " << merged_node->DebugStr();
    graph->RemoveNode(i);
    graph->RemoveNode(j);
    merged_node->UpdateTracker();
    return merged_node;
  }
};

}  // namespace cinn::fusion
