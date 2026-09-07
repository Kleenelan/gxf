/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

// GXF 5.7.1 pub/sub compatibility stub (built on the GXF 4.1 tree).
// QoSProfile is a pure value type; presets, comparisons and fluent setters are
// implemented for real. from_yaml() is a stub (discovery payload exchange is
// not functional in this build) and returns GXF_NOT_IMPLEMENTED.

#include "gxf/pubsub/qos_profile.hpp"

#include <algorithm>
#include <cctype>

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

const char* to_string(ReliabilityPolicy policy) {
  switch (policy) {
    case ReliabilityPolicy::kBestEffort: return "best_effort";
    case ReliabilityPolicy::kReliable: return "reliable";
  }
  return "unknown";
}

const char* to_string(DurabilityPolicy policy) {
  switch (policy) {
    case DurabilityPolicy::kVolatile: return "volatile";
    case DurabilityPolicy::kTransientLocal: return "transient_local";
  }
  return "unknown";
}

const char* to_string(HistoryPolicy policy) {
  switch (policy) {
    case HistoryPolicy::kKeepLast: return "keep_last";
    case HistoryPolicy::kKeepAll: return "keep_all";
  }
  return "unknown";
}

const char* to_string(CongestionControlPolicy policy) {
  switch (policy) {
    case CongestionControlPolicy::kDrop: return "drop";
    case CongestionControlPolicy::kBlock: return "block";
  }
  return "unknown";
}

bool QoSProfile::operator==(const QoSProfile& other) const {
  return reliability == other.reliability && durability == other.durability &&
         history == other.history && depth == other.depth &&
         deadline_ns == other.deadline_ns && lifespan_ns == other.lifespan_ns &&
         priority == other.priority && congestion_control == other.congestion_control &&
         max_blocking_time_ns == other.max_blocking_time_ns;
}

bool QoSProfile::operator!=(const QoSProfile& other) const { return !(*this == other); }

QoSProfile QoSProfile::Default() { return QoSProfile{}; }

QoSProfile QoSProfile::SensorData() {
  QoSProfile qos;
  qos.depth = 1;
  return qos;
}

QoSProfile QoSProfile::Reliable() {
  QoSProfile qos;
  qos.reliability = ReliabilityPolicy::kReliable;
  return qos;
}

QoSProfile QoSProfile::TransientLocal() {
  QoSProfile qos;
  qos.durability = DurabilityPolicy::kTransientLocal;
  return qos;
}

QoSProfile QoSProfile::ControlMessage() {
  QoSProfile qos;
  qos.reliability = ReliabilityPolicy::kReliable;
  qos.durability = DurabilityPolicy::kTransientLocal;
  qos.history = HistoryPolicy::kKeepAll;
  qos.depth = 100;
  return qos;
}

QoSProfile QoSProfile::TensorData() {
  QoSProfile qos;
  qos.reliability = ReliabilityPolicy::kReliable;
  qos.depth = 3;
  qos.deadline_ns = 30'000'000'000ULL;  // 30 s
  return qos;
}

QoSProfile QoSProfile::VideoStream() {
  QoSProfile qos;
  qos.depth = 1;
  return qos;
}

QoSProfile QoSProfile::BulkTransfer() {
  QoSProfile qos;
  qos.reliability = ReliabilityPolicy::kReliable;
  qos.depth = 1;
  qos.deadline_ns = 60'000'000'000ULL;  // 60 s
  return qos;
}

Expected<QoSProfile> QoSProfile::from_name(const std::string& name) {
  std::string lowered = name;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lowered == "default") return Default();
  if (lowered == "sensor_data") return SensorData();
  if (lowered == "reliable") return Reliable();
  if (lowered == "transient_local") return TransientLocal();
  if (lowered == "control_message") return ControlMessage();
  if (lowered == "tensor_data") return TensorData();
  if (lowered == "video_stream") return VideoStream();
  if (lowered == "bulk_transfer") return BulkTransfer();
  return Unexpected{GXF_ARGUMENT_INVALID};
}

std::vector<std::string> QoSProfile::preset_names() {
  return {"default",       "sensor_data", "reliable",     "transient_local",
          "control_message", "tensor_data", "video_stream", "bulk_transfer"};
}

QoSProfile& QoSProfile::set_reliability(ReliabilityPolicy policy) {
  reliability = policy;
  return *this;
}

QoSProfile& QoSProfile::set_durability(DurabilityPolicy policy) {
  durability = policy;
  return *this;
}

QoSProfile& QoSProfile::set_history(HistoryPolicy policy, size_t depth_value) {
  history = policy;
  depth = depth_value;
  return *this;
}

QoSProfile& QoSProfile::set_deadline(uint64_t deadline_value_ns) {
  deadline_ns = deadline_value_ns;
  return *this;
}

QoSProfile& QoSProfile::set_lifespan(uint64_t lifespan_value_ns) {
  lifespan_ns = lifespan_value_ns;
  return *this;
}

QoSProfile& QoSProfile::set_priority(uint32_t priority_value) {
  priority = priority_value;
  return *this;
}

QoSProfile& QoSProfile::set_congestion_control(CongestionControlPolicy policy) {
  congestion_control = policy;
  return *this;
}

QoSProfile& QoSProfile::set_max_blocking_time(uint64_t time_ns) {
  max_blocking_time_ns = time_ns;
  return *this;
}

std::string QoSProfile::to_yaml() const {
  // Hand-built YAML (no yaml-cpp dependency in this TU).
  std::string out;
  out += "reliability: ";
  out += to_string(reliability);
  out += "\ndurability: ";
  out += to_string(durability);
  out += "\nhistory: ";
  out += to_string(history);
  out += "\ndepth: " + std::to_string(depth);
  out += "\ndeadline_ns: " + std::to_string(deadline_ns);
  out += "\nlifespan_ns: " + std::to_string(lifespan_ns);
  out += "\npriority: " + std::to_string(priority);
  out += "\ncongestion_control: ";
  out += to_string(congestion_control);
  out += "\nmax_blocking_time_ns: " + std::to_string(max_blocking_time_ns);
  out += "\n";
  return out;
}

Expected<QoSProfile> QoSProfile::from_yaml(const std::string& yaml) {
  (void)yaml;
  GXF_LOG_ERROR(
      "QoSProfile::from_yaml is a compatibility stub in this build; "
      "pub/sub discovery payload exchange is not functional");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

}  // namespace gxf
}  // namespace nvidia
