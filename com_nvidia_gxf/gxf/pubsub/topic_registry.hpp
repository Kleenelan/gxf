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
#ifndef NVIDIA_GXF_PUBSUB_TOPIC_REGISTRY_HPP_
#define NVIDIA_GXF_PUBSUB_TOPIC_REGISTRY_HPP_

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gxf/core/expected.hpp"
#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/gid.hpp"
#include "gxf/pubsub/qos_compatibility.hpp"

namespace nvidia {
namespace gxf {

/// @brief Thread-safe registry for tracking publishers and subscribers
///
/// This is the "matching engine" that computes which endpoints should connect.
/// Completely backend-agnostic - operates on EndpointInfo structs.
///
/// The registry tracks:
/// - Publishers and subscribers by topic
/// - Type compatibility between endpoints
/// - QoS compatibility between endpoints
///
/// When endpoints are registered, the registry computes matches and invokes
/// callbacks to notify the system of new connections.
///
/// Thread Safety: All public methods are thread-safe.
class TopicRegistry {
 public:
  /// @brief Result of computing matches for a publisher
  struct MatchResult {
    /// Subscribers that are compatible (including those with warnings)
    std::vector<SubscriberInfo> compatible_subscribers;

    /// Subscribers that are incompatible (will not receive messages)
    std::vector<SubscriberInfo> incompatible_subscribers;

    /// Human-readable reasons for compatibility/incompatibility decisions
    std::vector<std::string> reasons;
  };

  /// @brief Callback invoked when a publisher-subscriber match is established
  using MatchCallback = std::function<void(const PublisherGid&, const SubscriberGid&)>;

  /// @brief Callback invoked when a publisher-subscriber match is broken
  using UnmatchCallback = std::function<void(const PublisherGid&, const SubscriberGid&)>;

  /// Default constructor
  TopicRegistry() = default;

  /// Destructor
  ~TopicRegistry() = default;

  // Non-copyable, non-movable (contains mutex)
  TopicRegistry(const TopicRegistry&) = delete;
  TopicRegistry& operator=(const TopicRegistry&) = delete;
  TopicRegistry(TopicRegistry&&) = delete;
  TopicRegistry& operator=(TopicRegistry&&) = delete;

  //----------------------------------------------------------------------------
  // Registration
  //----------------------------------------------------------------------------

  /// @brief Register a publisher endpoint
  ///
  /// If subscribers on the same topic exist, computes matches and invokes
  /// the on_match callback for each compatible subscriber.
  ///
  /// @param info Publisher information
  void register_publisher(const PublisherInfo& info);

  /// @brief Register a subscriber endpoint
  ///
  /// If publishers on the same topic exist, computes matches and invokes
  /// the on_match callback for each compatible publisher.
  ///
  /// @param info Subscriber information
  void register_subscriber(const SubscriberInfo& info);

  //----------------------------------------------------------------------------
  // Unregistration
  //----------------------------------------------------------------------------

  /// @brief Unregister a publisher endpoint
  ///
  /// Invokes on_unmatch callback for each subscriber that was matched.
  /// Safe to call even if the publisher was never registered (no-op).
  ///
  /// @param gid Publisher GID to unregister
  void unregister_publisher(const PublisherGid& gid);

  /// @brief Unregister a subscriber endpoint
  ///
  /// Invokes on_unmatch callback for each publisher that was matched.
  /// Safe to call even if the subscriber was never registered (no-op).
  ///
  /// @param gid Subscriber GID to unregister
  void unregister_subscriber(const SubscriberGid& gid);

  //----------------------------------------------------------------------------
  // Endpoint Queries
  //----------------------------------------------------------------------------

  /// @brief Get all publishers on a topic
  /// @param topic Topic name
  /// @return List of publisher info (empty if topic doesn't exist)
  std::vector<PublisherInfo> get_publishers(const std::string& topic) const;

  /// @brief Get all subscribers on a topic
  /// @param topic Topic name
  /// @return List of subscriber info (empty if topic doesn't exist)
  std::vector<SubscriberInfo> get_subscribers(const std::string& topic) const;

  /// @brief Get publisher info by GID
  /// @param gid Publisher GID
  /// @return Publisher info, or error if not found
  Expected<PublisherInfo> get_publisher(const PublisherGid& gid) const;

  /// @brief Get subscriber info by GID
  /// @param gid Subscriber GID
  /// @return Subscriber info, or error if not found
  Expected<SubscriberInfo> get_subscriber(const SubscriberGid& gid) const;

  //----------------------------------------------------------------------------
  // Topic Queries
  //----------------------------------------------------------------------------

  /// @brief Get list of all topics with at least one endpoint
  /// @return List of topic names
  std::vector<std::string> get_all_topics() const;

  /// @brief Get summary info for all topics
  /// @return List of topic info (one per topic)
  std::vector<TopicInfo> get_topic_info_list() const;

  /// @brief Get summary info for a specific topic
  /// @param topic Topic name
  /// @return Topic info, or error if topic doesn't exist
  Expected<TopicInfo> get_topic_info(const std::string& topic) const;

  //----------------------------------------------------------------------------
  // Match Computation
  //----------------------------------------------------------------------------

  /// @brief Compute matches for a publisher
  ///
  /// Returns all subscribers on the same topic, categorized by compatibility.
  /// Does NOT invoke callbacks - use for inspection only.
  ///
  /// @param pub_gid Publisher GID
  /// @return Match result with compatible/incompatible lists
  MatchResult compute_matches_for_publisher(const PublisherGid& pub_gid) const;

  /// @brief Compute matches for a subscriber
  ///
  /// Returns all publishers on the same topic, categorized by compatibility.
  /// Does NOT invoke callbacks - use for inspection only.
  ///
  /// @param sub_gid Subscriber GID
  /// @return Match result with compatible/incompatible publishers
  MatchResult compute_matches_for_subscriber(const SubscriberGid& sub_gid) const;

  //----------------------------------------------------------------------------
  // Callbacks
  //----------------------------------------------------------------------------

  /// @brief Set callback for match events
  ///
  /// Called when a new compatible publisher-subscriber pair is discovered.
  /// The callback is invoked outside the registry lock, so it's safe to call
  /// other methods from the callback.
  ///
  /// @param callback Function to call on match (pub_gid, sub_gid)
  void set_on_match(MatchCallback callback);

  /// @brief Set callback for unmatch events
  ///
  /// Called when a previously matched publisher-subscriber pair is broken
  /// (due to unregistration). The callback is invoked outside the registry lock.
  ///
  /// @param callback Function to call on unmatch (pub_gid, sub_gid)
  void set_on_unmatch(UnmatchCallback callback);

  //----------------------------------------------------------------------------
  // Statistics
  //----------------------------------------------------------------------------

  /// @brief Get total number of registered publishers
  size_t publisher_count() const;

  /// @brief Get total number of registered subscribers
  size_t subscriber_count() const;

  /// @brief Get total number of topics
  size_t topic_count() const;

  /// @brief Clear all registered endpoints and reset state
  void clear();

 private:
  // Collect all unique topic names (caller must hold mutex_)
  std::vector<std::string> get_all_topics_nolock() const;

  // Build TopicInfo for a single topic (caller must hold mutex_)
  // Returns nullopt if topic has no publishers or subscribers.
  Expected<TopicInfo> get_topic_info_nolock(const std::string& topic) const;

  // Check if publisher and subscriber are compatible (type + QoS)
  // Returns compatibility result and fills reason string
  QoSCompatibility check_endpoint_compatibility(
      const PublisherInfo& pub,
      const SubscriberInfo& sub,
      std::string* reason) const;

  mutable std::mutex mutex_;

  // GID -> EndpointInfo
  std::unordered_map<Gid, PublisherInfo> publishers_;
  std::unordered_map<Gid, SubscriberInfo> subscribers_;

  // Topic -> set of GIDs
  std::unordered_map<std::string, std::unordered_set<Gid>> topic_publishers_;
  std::unordered_map<std::string, std::unordered_set<Gid>> topic_subscribers_;

  // Track active matches (pub_gid -> set of sub_gids)
  std::unordered_map<Gid, std::unordered_set<Gid>> active_matches_;

  // Callbacks
  MatchCallback on_match_;
  UnmatchCallback on_unmatch_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_TOPIC_REGISTRY_HPP_
