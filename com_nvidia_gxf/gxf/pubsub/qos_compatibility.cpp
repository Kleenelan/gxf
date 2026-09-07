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
// QoS compatibility checking is pure metadata logic; implemented for real
// following the DDS "offered vs requested" semantics documented in the header.

#include "gxf/pubsub/qos_compatibility.hpp"

namespace nvidia {
namespace gxf {

const char* to_string(QoSCompatibility compatibility) {
  switch (compatibility) {
    case QoSCompatibility::kCompatible: return "compatible";
    case QoSCompatibility::kWarning: return "warning";
    case QoSCompatibility::kIncompatible: return "incompatible";
  }
  return "unknown";
}

namespace {

QoSCompatibility check_reliability(const QoSProfile& pub, const QoSProfile& sub,
                                   std::string* reason) {
  if (sub.reliability == ReliabilityPolicy::kReliable &&
      pub.reliability == ReliabilityPolicy::kBestEffort) {
    if (reason) {
      *reason = "subscriber requests RELIABLE but publisher offers only BEST_EFFORT";
    }
    return QoSCompatibility::kIncompatible;
  }
  if (pub.reliability == ReliabilityPolicy::kReliable &&
      sub.reliability == ReliabilityPolicy::kBestEffort) {
    if (reason) {
      *reason = "publisher offers RELIABLE but subscriber requests BEST_EFFORT "
                "(subscriber may drop messages it could have received reliably)";
    }
    return QoSCompatibility::kWarning;
  }
  return QoSCompatibility::kCompatible;
}

QoSCompatibility check_durability(const QoSProfile& pub, const QoSProfile& sub,
                                  std::string* reason) {
  if (sub.durability == DurabilityPolicy::kTransientLocal &&
      pub.durability == DurabilityPolicy::kVolatile) {
    if (reason) {
      *reason = "subscriber requests TRANSIENT_LOCAL but publisher offers only VOLATILE";
    }
    return QoSCompatibility::kIncompatible;
  }
  if (pub.durability == DurabilityPolicy::kTransientLocal &&
      sub.durability == DurabilityPolicy::kVolatile) {
    if (reason) {
      *reason = "publisher offers TRANSIENT_LOCAL but subscriber requests VOLATILE "
                "(late-joining subscriber won't receive cached historical messages)";
    }
    return QoSCompatibility::kWarning;
  }
  return QoSCompatibility::kCompatible;
}

QoSCompatibility check_history(const QoSProfile& pub, const QoSProfile& sub,
                               std::string* reason) {
  // Keep-all policies are treated as "infinite" depth.
  const bool pub_keep_all = (pub.history == HistoryPolicy::kKeepAll);
  const bool sub_keep_all = (sub.history == HistoryPolicy::kKeepAll);
  if (sub_keep_all && !pub_keep_all) {
    if (reason) {
      *reason = "subscriber requests KEEP_ALL but publisher offers KEEP_LAST";
    }
    return QoSCompatibility::kWarning;
  }
  if (!pub_keep_all && !sub_keep_all && pub.depth > sub.depth) {
    if (reason) {
      *reason = "publisher history depth exceeds subscriber depth "
                "(subscriber may miss messages)";
    }
    return QoSCompatibility::kWarning;
  }
  return QoSCompatibility::kCompatible;
}

QoSCompatibility check_deadline(const QoSProfile& pub, const QoSProfile& sub,
                                std::string* reason) {
  // 0 = no deadline. A stricter (smaller, non-zero) subscriber deadline than
  // the publisher's is a warning.
  if (sub.deadline_ns != 0 && (pub.deadline_ns == 0 || pub.deadline_ns > sub.deadline_ns)) {
    if (reason) {
      *reason = "subscriber deadline is stricter than publisher deadline "
                "(subscriber expects faster publication rate than publisher promises)";
    }
    return QoSCompatibility::kWarning;
  }
  return QoSCompatibility::kCompatible;
}

QoSCompatibility combine(std::initializer_list<QoSCompatibility> results) {
  QoSCompatibility worst = QoSCompatibility::kCompatible;
  for (const auto result : results) {
    if (result == QoSCompatibility::kIncompatible) { return QoSCompatibility::kIncompatible; }
    if (result == QoSCompatibility::kWarning) { worst = QoSCompatibility::kWarning; }
  }
  return worst;
}

}  // namespace

QoSCompatibilityResult checkQoSCompatibilityDetailed(const QoSProfile& publisher_qos,
                                                     const QoSProfile& subscriber_qos) {
  QoSCompatibilityResult result;
  std::string reliability_reason;
  std::string durability_reason;
  std::string history_reason;
  std::string deadline_reason;

  result.reliability_result =
      check_reliability(publisher_qos, subscriber_qos, &reliability_reason);
  result.durability_result =
      check_durability(publisher_qos, subscriber_qos, &durability_reason);
  result.history_result = check_history(publisher_qos, subscriber_qos, &history_reason);
  result.deadline_result = check_deadline(publisher_qos, subscriber_qos, &deadline_reason);

  result.compatibility = combine({result.reliability_result, result.durability_result,
                                  result.history_result, result.deadline_result});

  for (const auto* reason :
       {&reliability_reason, &durability_reason, &history_reason, &deadline_reason}) {
    if (!reason->empty()) {
      if (!result.reason.empty()) { result.reason += "; "; }
      result.reason += *reason;
    }
  }
  return result;
}

QoSCompatibility checkQoSCompatibility(const QoSProfile& publisher_qos,
                                       const QoSProfile& subscriber_qos, std::string* reason) {
  const auto detailed = checkQoSCompatibilityDetailed(publisher_qos, subscriber_qos);
  if (reason) { *reason = detailed.reason; }
  return detailed.compatibility;
}

}  // namespace gxf
}  // namespace nvidia
