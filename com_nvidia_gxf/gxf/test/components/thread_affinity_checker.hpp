/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef NVIDIA_GXF_TEST_COMPONENTS_THREAD_AFFINITY_CHECKER_HPP_
#define NVIDIA_GXF_TEST_COMPONENTS_THREAD_AFFINITY_CHECKER_HPP_

#include <cstdint>
#include <vector>

#include "gxf/std/codelet.hpp"

namespace nvidia {
namespace gxf {
namespace test {

// Codelet used to verify that the worker thread which executes it has the
// expected CPU affinity. It is intended to be used together with a CPUThread
// component that pins the entity to specific cores.
class ThreadAffinityChecker : public Codelet {
 public:
  gxf_result_t registerInterface(Registrar* registrar) override;
  gxf_result_t start() override { return GXF_SUCCESS; }
  gxf_result_t tick() override;
  gxf_result_t stop() override { return GXF_SUCCESS; }

 private:
  Parameter<std::vector<uint32_t>> expected_cores_;
};

}  // namespace test
}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_TEST_COMPONENTS_THREAD_AFFINITY_CHECKER_HPP_
