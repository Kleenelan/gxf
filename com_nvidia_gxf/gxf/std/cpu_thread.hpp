/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#ifndef NVIDIA_GXF_STD_CPU_THREAD_HPP
#define NVIDIA_GXF_STD_CPU_THREAD_HPP

#include <cstdint>
#include <vector>

#include "gxf/core/component.hpp"
#include "gxf/std/resources.hpp"

namespace nvidia {
namespace gxf {

// Real-time scheduling policies supported by POSIX and Linux kernel
// (backported from GXF 5.1 for holoscan-sdk compatibility).
enum class SchedulingPolicy : int32_t {
  kFirstInFirstOut = 1,  // SCHED_FIFO supported by POSIX and Linux kernel
  kRoundRobin = 2,  // SCHED_RR supported by POSIX and Linux kernel
  kDeadline = 6  // SCHED_DEADLINE supported by Linux kernel
};

// Custom parameter parser for SchedulingPolicy
template <>
struct ParameterParser<SchedulingPolicy> {
  static Expected<SchedulingPolicy> Parse(gxf_context_t context, gxf_uid_t component_uid,
                                         const char* key, const YAML::Node& node,
                                         const std::string& prefix) {
    const std::string value = node.as<std::string>();
    if (strcmp(value.c_str(), "SCHED_FIFO") == 0) {
      return SchedulingPolicy::kFirstInFirstOut;
    }
    if (strcmp(value.c_str(), "SCHED_RR") == 0) {
      return SchedulingPolicy::kRoundRobin;
    }
    if (strcmp(value.c_str(), "SCHED_DEADLINE") == 0) {
      return SchedulingPolicy::kDeadline;
    }
    GXF_LOG_ERROR("Invalid scheduling policy: %s", value.c_str());
    return Unexpected{GXF_ARGUMENT_OUT_OF_RANGE};
  }
};

// Custom parameter wrapper for SchedulingPolicy
template<>
struct ParameterWrapper<SchedulingPolicy> {
  static Expected<YAML::Node> Wrap(gxf_context_t context, const SchedulingPolicy& value) {
    YAML::Node node(YAML::NodeType::Scalar);
    switch (value) {
      case SchedulingPolicy::kFirstInFirstOut: {
        node = std::string("SCHED_FIFO");
        break;
      }
      case SchedulingPolicy::kRoundRobin: {
        node = std::string("SCHED_RR");
        break;
      }
      case SchedulingPolicy::kDeadline: {
        node = std::string("SCHED_DEADLINE");
        break;
      }
      default:
        GXF_LOG_ERROR("Invalid scheduling policy: %d", static_cast<int32_t>(value));
        return Unexpected{GXF_PARAMETER_OUT_OF_RANGE};
    }
    return node;
  }
};

class CPUThread : public Component {
 public:
  gxf_result_t registerInterface(Registrar* registrar) override;

  bool pinned() const {
    return pin_entity_;
  }

  // CPU cores to pin the worker thread to (empty means no core pinning).
  std::vector<uint32_t> pinCores() const {
    auto maybe = pin_cores_.try_get();
    return maybe ? maybe.value() : std::vector<uint32_t>{};
  }

  // Real-time scheduling policy. Returns a default value if not configured.
  SchedulingPolicy schedPolicy() const {
    auto maybe = sched_policy_.try_get();
    return maybe ? maybe.value() : SchedulingPolicy::kFirstInFirstOut;
  }

  // Thread priority for SCHED_FIFO/SCHED_RR. Returns 0 if not configured.
  uint32_t schedPriority() const {
    auto maybe = sched_priority_.try_get();
    return maybe ? maybe.value() : 0u;
  }

  // SCHED_DEADLINE runtime in nanoseconds. Returns 0 if not configured.
  uint64_t schedRuntime() const {
    auto maybe = sched_runtime_.try_get();
    return maybe ? maybe.value() : 0ull;
  }

  // SCHED_DEADLINE deadline in nanoseconds. Returns 0 if not configured.
  uint64_t schedDeadline() const {
    auto maybe = sched_deadline_.try_get();
    return maybe ? maybe.value() : 0ull;
  }

  // SCHED_DEADLINE period in nanoseconds. Returns 0 if not configured.
  uint64_t schedPeriod() const {
    auto maybe = sched_period_.try_get();
    return maybe ? maybe.value() : 0ull;
  }

  // Returns true if a real-time scheduling policy is configured.
  bool isRealtime() const {
    auto maybe = sched_policy_.try_get();
    if (!maybe) { return false; }
    const auto policy = maybe.value();
    return policy == SchedulingPolicy::kFirstInFirstOut ||
           policy == SchedulingPolicy::kRoundRobin ||
           policy == SchedulingPolicy::kDeadline;
  }

 private:
  // Keep track of whether or not the component should be pinned to a worker thread
  Parameter<bool> pin_entity_;

  // GXF 5.1+ parameters: accepted, parsed and applied when a worker thread is
  // created for the entity which owns this CPUThread component.
  Parameter<std::vector<uint32_t>> pin_cores_;
  Parameter<SchedulingPolicy> sched_policy_;
  Parameter<uint32_t> sched_priority_;
  Parameter<uint64_t> sched_runtime_;
  Parameter<uint64_t> sched_deadline_;
  Parameter<uint64_t> sched_period_;
};

// Applies the CPUThread configuration identified by |cpu_thread_cid| to the
// calling thread. Affinity is applied first, followed by the scheduling policy.
// Scheduling failures due to missing privileges are logged as warnings and do
// not stop execution.
gxf_result_t ApplyCPUThreadConfiguration(gxf_context_t context, gxf_uid_t cpu_thread_cid);

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_CPU_THREAD_HPP
