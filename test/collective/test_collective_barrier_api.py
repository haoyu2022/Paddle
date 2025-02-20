#   Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import unittest

from legacy_test.test_collective_api_base import TestDistBase

import paddle

paddle.enable_static()


class TestCollectiveBarrierAPI(TestDistBase):
    def _setup_config(self):
        pass

    def test_barrier_nccl(self):
        self.check_with_place("collective_barrier_api.py", "barrier", "nccl")

    def test_barrier_nccl_with_new_comm(self):
        self.check_with_place(
            "collective_barrier_api.py",
            "barrier",
            "nccl",
            need_envs={"FLAGS_dynamic_static_unified_comm": "true"},
        )

    def test_barrier_gloo(self):
        self.check_with_place(
            "collective_barrier_api.py",
            "barrier",
            "gloo",
            "5",
            need_envs={"FLAGS_dynamic_static_unified_comm": "false"},
        )


if __name__ == '__main__':
    unittest.main()
