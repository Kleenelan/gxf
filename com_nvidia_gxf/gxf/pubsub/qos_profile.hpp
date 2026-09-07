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
#ifndef NVIDIA_GXF_PUBSUB_QOS_PROFILE_HPP_
#define NVIDIA_GXF_PUBSUB_QOS_PROFILE_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "gxf/core/expected.hpp"

namespace nvidia {
namespace gxf {

/// @brief Reliability policy for pub/sub communication
///
/// Determines whether message delivery is guaranteed.
enum class ReliabilityPolicy : int32_t {
  /// Best-effort delivery: messages may be lost under load
  kBestEffort = 0,
  /// Reliable delivery: messages are guaranteed to arrive (with retransmission)
  kReliable = 1
};

/// @brief Durability policy for pub/sub communication
///
/// Determines whether late-joining subscribers receive historical messages.
enum class DurabilityPolicy : int32_t {
  /// Volatile: late joiners only receive messages published after subscription
  kVolatile = 0,
  /// Transient-local: late joiners receive cached historical messages
  kTransientLocal = 1
};

/// @brief History policy for pub/sub communication
///
/// Determines how many messages are kept in the history buffer.
enum class HistoryPolicy : int32_t {
  /// Keep last N messages (based on depth)
  kKeepLast = 0,
  /// Keep all messages (unbounded, use with caution)
  kKeepAll = 1
};

/// @brief Congestion control policy for pub/sub communication
///
/// Determines publisher behavior when the transport is congested
/// (e.g., send buffer full or subscriber not keeping up).
enum class CongestionControlPolicy : int32_t {
  /// Drop messages under congestion (default, non-blocking)
  kDrop = 0,
  /// Block the publisher until congestion clears (backpressure)
  kBlock = 1
};

/// @brief Convert ReliabilityPolicy to string
/// @param policy The policy to convert
/// @return String representation
const char* to_string(ReliabilityPolicy policy);

/// @brief Convert DurabilityPolicy to string
/// @param policy The policy to convert
/// @return String representation
const char* to_string(DurabilityPolicy policy);

/// @brief Convert HistoryPolicy to string
/// @param policy The policy to convert
/// @return String representation
const char* to_string(HistoryPolicy policy);

/// @brief Convert CongestionControlPolicy to string
/// @param policy The policy to convert
/// @return String representation
const char* to_string(CongestionControlPolicy policy);

/// @brief Quality of Service profile for pub/sub communication
///
/// Simple data structure containing all QoS settings. Used for compatibility
/// checking and passing QoS configuration between components.
struct QoSProfile {
  /// Reliability policy (default: best-effort)
  ReliabilityPolicy reliability = ReliabilityPolicy::kBestEffort;

  /// Durability policy (default: volatile)
  DurabilityPolicy durability = DurabilityPolicy::kVolatile;

  /// History policy (default: keep-last)
  HistoryPolicy history = HistoryPolicy::kKeepLast;

  /// History depth (number of messages to keep, used with kKeepLast)
  size_t depth = 10;

  /// Deadline in nanoseconds (0 = no deadline)
  /// Maximum time between message publications before considered stale
  uint64_t deadline_ns = 0;

  /// Lifespan in nanoseconds (0 = infinite)
  /// Maximum age of a message before it expires
  uint64_t lifespan_ns = 0;

  /// Priority level for message scheduling (default: 5)
  ///
  /// Higher values indicate higher priority. The valid range and
  /// interpretation are backend-specific:
  /// - Zenoh: Maps to z_priority_t (1-7, where 1=lowest, 7=highest)
  /// - DDS: Can map to DDS transport priority QoS
  /// - UCX: Can map to traffic class or service level
  /// - Backends that do not support priority may ignore this field
  ///
  /// Range validation, if any, is the responsibility of the backend.
  uint32_t priority = 5;

  /// Congestion control policy (default: drop)
  ///
  /// Controls publisher behavior when the transport is congested:
  /// - kDrop: messages are silently dropped (non-blocking, default)
  /// - kBlock: publisher blocks until congestion clears (backpressure)
  ///
  /// Backend mapping:
  /// - Zenoh: Maps directly to z_congestion_control_t (DROP / BLOCK)
  /// - DDS: Can map to resource limits QoS or flow controller
  /// - UCX: Can map to send queue behavior
  /// - Backends that do not support congestion control may ignore this field
  CongestionControlPolicy congestion_control = CongestionControlPolicy::kDrop;

  /// Maximum time a reliable send can block, in nanoseconds (default: 100 ms)
  ///
  /// Controls how long a reliable send operation may block before giving up.
  /// Special values:
  /// - 0 = non-blocking (fail immediately if buffers are full)
  /// - UINT64_MAX = block indefinitely
  ///
  /// Backend mapping:
  /// - DDS: Maps to RELIABLE_RELIABILITY_QOS max_blocking_time. Critical
  ///   for real-time pipelines where blocking indefinitely is unacceptable.
  /// - Zenoh: Not directly supported (uses explicit BLOCK/DROP via
  ///   congestion_control). Can be enforced at framework level with a
  ///   timed wait wrapper.
  /// - UCX: Typically non-blocking; can be enforced at framework level.
  /// - Backends that do not support max blocking time may ignore this field.
  uint64_t max_blocking_time_ns = 100'000'000;  // 100 ms

  /// Default constructor with best-effort, volatile, keep-last(10) settings
  QoSProfile() = default;

  /// Equality comparison
  bool operator==(const QoSProfile& other) const;

  /// Inequality comparison
  bool operator!=(const QoSProfile& other) const;

  /// @brief Create a default QoS profile
  /// Best-effort, volatile, keep-last with depth 10
  static QoSProfile Default();

  /// @brief Create a QoS profile suitable for sensor data
  /// Best-effort, volatile, keep-last with depth 1 (latest only)
  static QoSProfile SensorData();

  /// @brief Create a reliable QoS profile
  /// Reliable, volatile, keep-last with depth 10
  static QoSProfile Reliable();

  /// @brief Create a QoS profile with transient-local durability
  /// Best-effort, transient-local, keep-last with depth 10
  static QoSProfile TransientLocal();

  //--- Application-domain named presets ---

  /// @brief Create a QoS profile for small control/configuration messages
  /// Reliable, transient-local, keep-all with depth 100
  static QoSProfile ControlMessage();

  /// @brief Create a QoS profile for large tensor payloads (10–100 MB)
  /// Reliable, volatile, keep-last with depth 3, 30 s deadline
  static QoSProfile TensorData();

  /// @brief Create a QoS profile for high-frequency video (>30 fps)
  /// Best-effort, volatile, keep-last with depth 1
  static QoSProfile VideoStream();

  /// @brief Create a QoS profile for large one-shot bulk transfers
  /// Reliable, volatile, keep-last with depth 1, 60 s deadline
  static QoSProfile BulkTransfer();

  //--- Named preset lookup ---

  /// @brief Look up a QoS preset by name (case-insensitive)
  ///
  /// Recognized names (case-insensitive):
  /// "default", "sensor_data", "reliable", "transient_local",
  /// "control_message", "tensor_data", "video_stream", "bulk_transfer"
  ///
  /// @param name Preset name
  /// @return The corresponding QoSProfile, or GXF_ARGUMENT_INVALID if unknown
  static Expected<QoSProfile> from_name(const std::string& name);

  /// @brief Get the list of all recognized preset names
  /// @return Vector of lowercase preset name strings
  static std::vector<std::string> preset_names();

  // Fluent API for building QoS profiles

  /// @brief Set reliability policy
  QoSProfile& set_reliability(ReliabilityPolicy policy);

  /// @brief Set durability policy
  QoSProfile& set_durability(DurabilityPolicy policy);

  /// @brief Set history policy and depth
  QoSProfile& set_history(HistoryPolicy policy, size_t depth = 10);

  /// @brief Set deadline in nanoseconds
  QoSProfile& set_deadline(uint64_t deadline_ns);

  /// @brief Set lifespan in nanoseconds
  QoSProfile& set_lifespan(uint64_t lifespan_ns);

  /// @brief Set priority level
  QoSProfile& set_priority(uint32_t priority);

  /// @brief Set congestion control policy
  QoSProfile& set_congestion_control(CongestionControlPolicy policy);

  /// @brief Set maximum blocking time in nanoseconds
  QoSProfile& set_max_blocking_time(uint64_t time_ns);

  /// @brief Serialize to YAML string
  std::string to_yaml() const;

  /// @brief Parse from YAML string
  static Expected<QoSProfile> from_yaml(const std::string& yaml);
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_QOS_PROFILE_HPP_
