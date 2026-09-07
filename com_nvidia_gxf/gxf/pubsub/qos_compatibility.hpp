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
#ifndef NVIDIA_GXF_PUBSUB_QOS_COMPATIBILITY_HPP_
#define NVIDIA_GXF_PUBSUB_QOS_COMPATIBILITY_HPP_

#include <string>

#include "gxf/pubsub/qos_profile.hpp"

namespace nvidia {
namespace gxf {

/// @brief Result of QoS compatibility check between publisher and subscriber
enum class QoSCompatibility {
  /// Publisher and subscriber QoS are fully compatible - communication will work as expected
  kCompatible,

  /// QoS is technically compatible but may have degraded behavior
  /// (e.g., subscriber may miss messages due to depth mismatch)
  kWarning,

  /// QoS is incompatible - communication will not work correctly
  /// (e.g., subscriber requests RELIABLE but publisher offers only BEST_EFFORT)
  kIncompatible
};

/// @brief Convert QoSCompatibility to string
/// @param compatibility The compatibility result to convert
/// @return String representation
const char* to_string(QoSCompatibility compatibility);

/// @brief Check if publisher and subscriber QoS profiles are compatible
///
/// DDS enforces QoS compatibility natively in the middleware. These checks
/// are primarily useful for backends without built-in QoS enforcement
/// (Zenoh, gRPC+UCX) so the framework can catch mismatches early.
///
/// Follows DDS "offered vs requested" semantics:
///
/// **Reliability Rules:**
/// - Subscriber requesting RELIABLE requires publisher offering RELIABLE
/// - Subscriber requesting BEST_EFFORT can match either publisher kind
/// - Publisher offering RELIABLE + Subscriber requesting BEST_EFFORT = Warning
///   (subscriber may drop messages it could have received reliably)
///
/// **Durability Rules:**
/// - Subscriber requesting TRANSIENT_LOCAL requires publisher offering TRANSIENT_LOCAL
/// - Subscriber requesting VOLATILE can match either publisher kind
/// - Publisher offering TRANSIENT_LOCAL + Subscriber requesting VOLATILE = Warning
///   (late-joining subscriber won't receive cached historical messages)
///
/// **History Depth Rules:**
/// - If publisher depth > subscriber depth = Warning (subscriber may miss messages)
/// - Keep-all policies are treated as "infinite" depth
///
/// **Deadline Rules:**
/// - If subscriber deadline is stricter (smaller) than publisher deadline = Warning
///   (subscriber expects faster publication rate than publisher promises)
///
/// @param publisher_qos QoS profile offered by the publisher
/// @param subscriber_qos QoS profile requested by the subscriber
/// @param reason Optional output parameter for human-readable explanation
/// @return QoSCompatibility result
QoSCompatibility checkQoSCompatibility(
    const QoSProfile& publisher_qos,
    const QoSProfile& subscriber_qos,
    std::string* reason = nullptr);

/// @brief Structure containing detailed compatibility check results
struct QoSCompatibilityResult {
  /// Overall compatibility result
  QoSCompatibility compatibility = QoSCompatibility::kCompatible;

  /// Human-readable reason for the result
  std::string reason;

  /// Individual check results (for debugging/logging)
  QoSCompatibility reliability_result = QoSCompatibility::kCompatible;
  QoSCompatibility durability_result = QoSCompatibility::kCompatible;
  QoSCompatibility history_result = QoSCompatibility::kCompatible;
  QoSCompatibility deadline_result = QoSCompatibility::kCompatible;

  /// Returns true if compatible (possibly with warnings)
  bool is_compatible() const {
    return compatibility != QoSCompatibility::kIncompatible;
  }

  /// Returns true if fully compatible (no warnings)
  bool is_fully_compatible() const {
    return compatibility == QoSCompatibility::kCompatible;
  }
};

/// @brief Detailed QoS compatibility check with per-field results
///
/// Same logic as checkQoSCompatibility, but returns detailed results
/// for each QoS dimension for debugging and logging purposes.
///
/// @param publisher_qos QoS profile offered by the publisher
/// @param subscriber_qos QoS profile requested by the subscriber
/// @return Detailed compatibility result
QoSCompatibilityResult checkQoSCompatibilityDetailed(
    const QoSProfile& publisher_qos,
    const QoSProfile& subscriber_qos);

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_QOS_COMPATIBILITY_HPP_
