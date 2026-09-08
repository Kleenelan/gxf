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
#include "gxf/test/components/thread_affinity_checker.hpp"

#include <pthread.h>
#include <sched.h>

#include "gxf/core/gxf.h"

namespace nvidia {
namespace gxf {
namespace test {

gxf_result_t ThreadAffinityChecker::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->parameter(
      expected_cores_, "expected_cores", "Expected Cores",
      "CPU cores the executing thread is expected to be pinned to.",
      Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  return ToResultCode(result);
}

gxf_result_t ThreadAffinityChecker::tick() {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  const int ret = pthread_getaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
  if (ret != 0) {
    GXF_LOG_ERROR("pthread_getaffinity_np failed: %d", ret);
    return GXF_FAILURE;
  }

  // If no expected cores are configured we only verify that the call succeeds.
  auto maybe_expected = expected_cores_.try_get();
  if (maybe_expected) {
    const auto& expected = maybe_expected.value();
    for (uint32_t core : expected) {
      if (!CPU_ISSET(core, &cpuset)) {
        GXF_LOG_ERROR("Expected core %u is not set in current thread affinity", core);
        return GXF_FAILURE;
      }
    }
    GXF_LOG_INFO("ThreadAffinityChecker: affinity verified for cores %u..%u",
                 expected.front(), expected.back());
  }

  return GXF_SUCCESS;
}

}  // namespace test
}  // namespace gxf
}  // namespace nvidia
