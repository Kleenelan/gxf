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
#ifndef NVIDIA_GXF_PUBSUB_IN_MEMORY_DISCOVERY_HPP_
#define NVIDIA_GXF_PUBSUB_IN_MEMORY_DISCOVERY_HPP_

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gxf/pubsub/pubsub_discovery.hpp"

namespace nvidia {
namespace gxf {

/// @brief In-memory `PubSubDiscovery` implementation for single-process use
///
/// Performs all endpoint tracking in the process heap with no network calls.
/// Suitable for:
/// - Single-process pub/sub pipeline testing
/// - CI environments without external middleware dependencies
/// - Deterministic unit testing: callbacks fire synchronously in the caller's
///   thread, making test assertions trivially race-free
///
/// Discovery model: `kDecentralizedPassive` — `announce_publisher()` and
/// `announce_subscriber()` are purely local bookkeeping. No network operations
/// are performed. Callbacks fire synchronously outside the internal mutex.
///
/// Thread Safety: all public methods are thread-safe.
///
/// Usage:
/// @code
/// auto discovery = std::make_shared<InMemoryDiscovery>();
/// discovery->set_on_subscriber_discovered([](const SubscriberInfo& info) {
///   // Handle new subscriber
/// });
/// discovery->initialize();
/// discovery->announce_publisher(pub_info);  // fires callback synchronously
/// @endcode
class InMemoryDiscovery : public PubSubDiscovery {
 public:
  InMemoryDiscovery() = default;
  ~InMemoryDiscovery() override = default;

  // Non-copyable, non-movable (contains mutex)
  InMemoryDiscovery(const InMemoryDiscovery&) = delete;
  InMemoryDiscovery& operator=(const InMemoryDiscovery&) = delete;
  InMemoryDiscovery(InMemoryDiscovery&&) = delete;
  InMemoryDiscovery& operator=(InMemoryDiscovery&&) = delete;

  //----------------------------------------------------------------------------
  // PubSubDiscovery Lifecycle
  //----------------------------------------------------------------------------

  Expected<void> initialize() override;
  Expected<void> shutdown() override;
  bool is_initialized() const override;

  //----------------------------------------------------------------------------
  // PubSubDiscovery Registration
  //----------------------------------------------------------------------------

  Expected<void> announce_publisher(const PublisherInfo& info) override;
  Expected<void> announce_subscriber(const SubscriberInfo& info) override;

  //----------------------------------------------------------------------------
  // PubSubDiscovery Deregistration
  //----------------------------------------------------------------------------

  Expected<void> remove_publisher(const PublisherGid& gid) override;
  Expected<void> remove_subscriber(const SubscriberGid& gid) override;

  //----------------------------------------------------------------------------
  // PubSubDiscovery Query
  //----------------------------------------------------------------------------

  Expected<std::vector<PublisherInfo>> query_publishers(
      const std::string& topic_name) override;
  Expected<std::vector<SubscriberInfo>> query_subscribers(
      const std::string& topic_name) override;
  Expected<std::vector<std::string>> get_all_topics() override;

  //----------------------------------------------------------------------------
  // PubSubDiscovery Callbacks
  //----------------------------------------------------------------------------

  void set_on_publisher_discovered(PublisherDiscoveredCallback callback) override;
  void set_on_subscriber_discovered(SubscriberDiscoveredCallback callback) override;
  void set_on_publisher_lost(PublisherLostCallback callback) override;
  void set_on_subscriber_lost(SubscriberLostCallback callback) override;

  //----------------------------------------------------------------------------
  // Diagnostics
  //----------------------------------------------------------------------------

  /// @brief Total number of currently registered publishers
  size_t publisher_count() const;

  /// @brief Total number of currently registered subscribers
  size_t subscriber_count() const;

  /// @brief Clear all registered endpoints without triggering callbacks
  ///
  /// Useful for resetting state between test cases. Does not affect the
  /// initialized state or registered callbacks.
  void clear();

 private:
  mutable std::mutex mutex_;
  bool initialized_ = false;

  // GID -> EndpointInfo
  std::unordered_map<Gid, PublisherInfo> publishers_;
  std::unordered_map<Gid, SubscriberInfo> subscribers_;

  // Callbacks (invoked outside the mutex)
  PublisherDiscoveredCallback on_publisher_discovered_;
  SubscriberDiscoveredCallback on_subscriber_discovered_;
  PublisherLostCallback on_publisher_lost_;
  SubscriberLostCallback on_subscriber_lost_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_IN_MEMORY_DISCOVERY_HPP_
