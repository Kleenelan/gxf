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
//
// In-memory discovery backend. In this build endpoint discovery is not
// functional: lifecycle methods succeed, announce operations fail with
// GXF_NOT_IMPLEMENTED, queries report an empty discovery domain and callbacks
// are never invoked.

#include "gxf/pubsub/in_memory_discovery.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

Expected<void> InMemoryDiscovery::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  GXF_LOG_WARNING(
      "InMemoryDiscovery is a compatibility stub in this build: pub/sub endpoint "
      "discovery is not functional");
  initialized_ = true;
  return Success;
}

Expected<void> InMemoryDiscovery::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  return Success;
}

bool InMemoryDiscovery::is_initialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_;
}

Expected<void> InMemoryDiscovery::announce_publisher(const PublisherInfo& info) {
  (void)info;
  GXF_LOG_ERROR(
      "InMemoryDiscovery::announce_publisher is a compatibility stub in this build; "
      "pub/sub discovery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<void> InMemoryDiscovery::announce_subscriber(const SubscriberInfo& info) {
  (void)info;
  GXF_LOG_ERROR(
      "InMemoryDiscovery::announce_subscriber is a compatibility stub in this build; "
      "pub/sub discovery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<void> InMemoryDiscovery::remove_publisher(const PublisherGid& gid) {
  (void)gid;
  // Safe no-op: nothing is ever announced in this stub.
  return Success;
}

Expected<void> InMemoryDiscovery::remove_subscriber(const SubscriberGid& gid) {
  (void)gid;
  // Safe no-op: nothing is ever announced in this stub.
  return Success;
}

Expected<std::vector<PublisherInfo>> InMemoryDiscovery::query_publishers(
    const std::string& topic_name) {
  (void)topic_name;
  // Empty-domain semantics.
  return std::vector<PublisherInfo>{};
}

Expected<std::vector<SubscriberInfo>> InMemoryDiscovery::query_subscribers(
    const std::string& topic_name) {
  (void)topic_name;
  // Empty-domain semantics.
  return std::vector<SubscriberInfo>{};
}

Expected<std::vector<std::string>> InMemoryDiscovery::get_all_topics() {
  // Empty-domain semantics.
  return std::vector<std::string>{};
}

void InMemoryDiscovery::set_on_publisher_discovered(PublisherDiscoveredCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_publisher_discovered_ = std::move(callback);
}

void InMemoryDiscovery::set_on_subscriber_discovered(SubscriberDiscoveredCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_subscriber_discovered_ = std::move(callback);
}

void InMemoryDiscovery::set_on_publisher_lost(PublisherLostCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_publisher_lost_ = std::move(callback);
}

void InMemoryDiscovery::set_on_subscriber_lost(SubscriberLostCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_subscriber_lost_ = std::move(callback);
}

size_t InMemoryDiscovery::publisher_count() const { return 0; }

size_t InMemoryDiscovery::subscriber_count() const { return 0; }

void InMemoryDiscovery::clear() {
  // No-op: the discovery domain is always empty in this stub.
}

}  // namespace gxf
}  // namespace nvidia
