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

#include "paddle/fluid/pir/dialect/distributed/ir/dist_api.h"
#include <vector>
#include "paddle/fluid/pir/dialect/distributed/ir/dist_attribute.h"
#include "paddle/fluid/pir/dialect/distributed/ir/dist_op.h"
#include "paddle/fluid/pir/dialect/operator/ir/api_builder.h"
#include "paddle/phi/common/data_type.h"
#include "paddle/phi/common/place.h"
#include "paddle/phi/common/reduce_type.h"
#include "paddle/pir/include/core/builder.h"
#include "paddle/pir/include/core/operation_utils.h"
#include "paddle/pir/include/core/value.h"
#include "paddle/utils/flat_hash_map.h"

namespace paddle::dialect {

pir::Value shard_tensor(
    const pir::Value& x,
    const phi::distributed::ProcessMesh& process_mesh,
    const std::vector<int64_t>& dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& partial_status) {
  pir::IrContext* ctx = pir::IrContext::Instance();
  // support amp for shard_tensor in the future
  pir::AttributeMap attribute_map = {
      {"tensor_dist_attr",
       TensorDistAttribute::get(
           ctx, process_mesh, dims_mapping, partial_status)}};

  auto shard_tensor_op =
      ApiBuilder::Instance().GetBuilder()->Build<ShardTensorOp>(x,
                                                                attribute_map);
  return shard_tensor_op.out();
}

pir::Value reshard(
    const pir::Value& x,
    const phi::distributed::ProcessMesh& process_mesh,
    const std::vector<int64_t>& dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& partial_status) {
  pir::IrContext* ctx = pir::IrContext::Instance();
  TensorDistAttribute tensor_dist_attr =
      TensorDistAttribute::get(ctx, process_mesh, dims_mapping, partial_status);
  return reshard(x, tensor_dist_attr);
}

pir::Value reshard(const pir::Value& x,
                   const TensorDistAttribute& tensor_dist_attr) {
  auto reshard_op = ApiBuilder::Instance().GetBuilder()->Build<ReshardOp>(
      x, tensor_dist_attr);
  return reshard_op.result(0);
}

std::vector<pir::Value> local_tensors_from_dist(
    const pir::Value& input,
    const std::vector<phi::distributed::ProcessMesh>& local_mesh_list,
    const std::vector<int64_t>& local_dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& local_partial_status,
    const phi::distributed::ProcessMesh& global_mesh,
    const std::vector<int64_t>& global_dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& global_partial_status) {
  pir::IrContext* ctx = pir::IrContext::Instance();
  std::vector<TensorDistAttribute> local_dist_attrs;
  for (const phi::distributed::ProcessMesh& mesh : local_mesh_list) {
    local_dist_attrs.emplace_back(TensorDistAttribute::get(
        ctx, mesh, local_dims_mapping, local_partial_status));
  }
  TensorDistAttribute global_dist_attr = TensorDistAttribute::get(
      ctx, global_mesh, global_dims_mapping, global_partial_status);

  auto op = ApiBuilder::Instance().GetBuilder()->Build<LocalTensorsFromDistOp>(
      input, local_dist_attrs, global_dist_attr);
  return op.results();
}

pir::Value dist_tensor_from_locals(
    const std::vector<pir::Value>& inputs,
    const std::vector<phi::distributed::ProcessMesh>& local_mesh_list,
    const std::vector<int64_t>& local_dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& local_partial_status,
    const phi::distributed::ProcessMesh& global_mesh,
    const std::vector<int64_t>& global_dims_mapping,
    const flat_hash_map<int64_t, phi::ReduceType>& global_partial_status,
    const std::vector<int64_t>& global_shape) {
  pir::IrContext* ctx = pir::IrContext::Instance();

  std::vector<TensorDistAttribute> local_dist_attrs;
  for (const phi::distributed::ProcessMesh& mesh : local_mesh_list) {
    local_dist_attrs.emplace_back(TensorDistAttribute::get(
        ctx, mesh, local_dims_mapping, local_partial_status));
  }

  TensorDistAttribute global_dist_attr = TensorDistAttribute::get(
      ctx, global_mesh, global_dims_mapping, global_partial_status);

  phi::DDim global_ddim = phi::make_ddim(global_shape);

  auto op = ApiBuilder::Instance().GetBuilder()->Build<DistTensorFromLocalsOp>(
      inputs, local_dist_attrs, global_dist_attr, global_ddim);
  return op.result(0);
}

}  // namespace paddle::dialect
