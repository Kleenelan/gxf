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

#include "gxf/core/component.hpp"
#include "gxf/std/resources.hpp"

namespace nvidia {
namespace gxf {

// Real-time scheduling policies supported by POSIX and Linux kernel
// (backported from GXF 5.1 for holoscan-sdk compatibility; the scheduling
// parameters themselves are not implemented by this 4.1-based runtime)
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

 private:
  // Keep track of whether or not the component should be pinned to a worker thread
  Parameter<bool> pin_entity_;

  // GXF 5.1 compatibility parameters: accepted in registerInterface() so that
  // graphs configuring them load successfully, but intentionally not applied
  // (no core pinning / real-time scheduling in this 4.1-based runtime).
  Parameter<std::vector<uint32_t>> pin_cores_;
  Parameter<SchedulingPolicy> sched_policy_;
  Parameter<uint32_t> sched_priority_;
  Parameter<uint64_t> sched_runtime_;
  Parameter<uint64_t> sched_deadline_;
  Parameter<uint64_t> sched_period_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_CPU_THREAD_HPP
