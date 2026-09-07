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
// The TopicRegistry is the pub/sub "matching engine". Pub/sub matching is not
// functional in this build, so registration is a no-op and all queries report
// an empty registry. Callback setters store the callback but it is never
// invoked (no matches are ever computed).

#include "gxf/pubsub/topic_registry.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

void TopicRegistry::register_publisher(const PublisherInfo& info) {
  (void)info;
  GXF_LOG_WARNING(
      "TopicRegistry::register_publisher is a compatibility stub; endpoint is not tracked "
      "and no matches are computed");
}

void TopicRegistry::register_subscriber(const SubscriberInfo& info) {
  (void)info;
  GXF_LOG_WARNING(
      "TopicRegistry::register_subscriber is a compatibility stub; endpoint is not tracked "
      "and no matches are computed");
}

void TopicRegistry::unregister_publisher(const PublisherGid& gid) {
  (void)gid;
  // No-op: nothing is ever registered in this stub.
}

void TopicRegistry::unregister_subscriber(const SubscriberGid& gid) {
  (void)gid;
  // No-op: nothing is ever registered in this stub.
}

std::vector<PublisherInfo> TopicRegistry::get_publishers(const std::string& topic) const {
  (void)topic;
  return {};
}

std::vector<SubscriberInfo> TopicRegistry::get_subscribers(const std::string& topic) const {
  (void)topic;
  return {};
}

Expected<PublisherInfo> TopicRegistry::get_publisher(const PublisherGid& gid) const {
  (void)gid;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

Expected<SubscriberInfo> TopicRegistry::get_subscriber(const SubscriberGid& gid) const {
  (void)gid;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

std::vector<std::string> TopicRegistry::get_all_topics() const { return {}; }

std::vector<TopicInfo> TopicRegistry::get_topic_info_list() const { return {}; }

Expected<TopicInfo> TopicRegistry::get_topic_info(const std::string& topic) const {
  (void)topic;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

TopicRegistry::MatchResult TopicRegistry::compute_matches_for_publisher(
    const PublisherGid& pub_gid) const {
  (void)pub_gid;
  return MatchResult{};
}

TopicRegistry::MatchResult TopicRegistry::compute_matches_for_subscriber(
    const SubscriberGid& sub_gid) const {
  (void)sub_gid;
  return MatchResult{};
}

void TopicRegistry::set_on_match(MatchCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_match_ = std::move(callback);
}

void TopicRegistry::set_on_unmatch(UnmatchCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_unmatch_ = std::move(callback);
}

size_t TopicRegistry::publisher_count() const { return 0; }

size_t TopicRegistry::subscriber_count() const { return 0; }

size_t TopicRegistry::topic_count() const { return 0; }

void TopicRegistry::clear() {
  // No-op: the registry is always empty in this stub.
}

std::vector<std::string> TopicRegistry::get_all_topics_nolock() const { return {}; }

Expected<TopicInfo> TopicRegistry::get_topic_info_nolock(const std::string& topic) const {
  (void)topic;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

QoSCompatibility TopicRegistry::check_endpoint_compatibility(
    const PublisherInfo& pub, const SubscriberInfo& sub, std::string* reason) const {
  // Pure QoS/type metadata check; this helper is never reached by the stub
  // registration path but is implemented for real for completeness.
  if (pub.type_hash != sub.type_hash) {
    if (reason) { *reason = "type hash mismatch"; }
    return QoSCompatibility::kIncompatible;
  }
  return checkQoSCompatibility(pub.qos, sub.qos, reason);
}

}  // namespace gxf
}  // namespace nvidia
