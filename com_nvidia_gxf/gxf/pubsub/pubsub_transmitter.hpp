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
#ifndef NVIDIA_GXF_PUBSUB_PUBSUB_TRANSMITTER_HPP_
#define NVIDIA_GXF_PUBSUB_PUBSUB_TRANSMITTER_HPP_

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "gxf/core/parameter.hpp"
#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/gid.hpp"
#include "gxf/pubsub/qos_profile.hpp"
#include "gxf/std/gems/staging_queue/staging_queue.hpp"
#include "gxf/std/transmitter.hpp"

namespace nvidia {
namespace gxf {

// Forward declaration (avoid circular dependency with pubsub_context.hpp)
class PubSubContext;

/// @brief Backend-agnostic publisher component
///
/// PubSubTransmitter is a GXF Transmitter that publishes messages to a topic.
/// It uses PubSubContext to abstract away backend details (gRPC, DDS, UCX, etc.).
///
/// Features:
/// - Topic-based publishing
/// - QoS profile support
/// - Matched subscriber tracking
/// - Double-buffered queue for entity staging
///
/// Usage:
/// 1. Create PubSubContext and inject backends
/// 2. Create PubSubTransmitter with topic_name and context reference
/// 3. Push entities to the transmitter
/// 4. Call sync() to move entities to backstage
/// 5. Call publish() or let the scheduler call publish_abi() to send
///
/// Thread Safety: All public methods are thread-safe.
class PubSubTransmitter : public Transmitter {
 public:
  using queue_t = ::gxf::staging_queue::StagingQueue<Entity>;

  PubSubTransmitter() = default;
  ~PubSubTransmitter() override = default;

  //----------------------------------------------------------------------------
  // GXF Component Interface
  //----------------------------------------------------------------------------

  gxf_result_t registerInterface(Registrar* registrar) override;
  gxf_result_t initialize() override;
  gxf_result_t deinitialize() override;

  //----------------------------------------------------------------------------
  // Queue Interface (from Queue base class)
  //----------------------------------------------------------------------------

  /// @brief Pop the oldest entity from the main stage
  gxf_result_t pop_abi(gxf_uid_t* uid) override;

  /// @brief Push an entity to the backstage
  gxf_result_t push_abi(gxf_uid_t other) override;

  /// @brief Peek at entity at given index in the main stage
  gxf_result_t peek_abi(gxf_uid_t* uid, int32_t index) override;

  /// @brief Get the maximum capacity of the queue
  size_t capacity_abi() override;

  /// @brief Get the current number of entities in the main stage
  size_t size_abi() override;

  //----------------------------------------------------------------------------
  // Transmitter Interface
  //----------------------------------------------------------------------------

  /// @brief Publish an entity (send to matched subscribers via PubSubContext)
  gxf_result_t publish_abi(gxf_uid_t uid) override;

  /// @brief Get the number of entities in the backstage
  size_t back_size_abi() override;

  /// @brief Move entities from backstage to main stage
  gxf_result_t sync_abi() override;

  //----------------------------------------------------------------------------
  // Pub/Sub Specific
  //----------------------------------------------------------------------------

  /// @brief Get the unique identifier for this publisher
  const PublisherGid& gid() const { return gid_; }

  /// @brief Get the topic name
  const std::string& topic_name() const;

  /// @brief Get the QoS profile
  const QoSProfile& qos() const;

  /// @brief Get the number of matched subscribers
  size_t matched_subscriber_count() const;

  /// @brief Check if there are any matched subscribers
  bool has_matched_subscribers() const;

  /// @brief Get the GIDs of all currently matched subscribers
  /// @return Snapshot vector of subscriber GIDs (taken under lock)
  std::vector<SubscriberGid> matched_subscriber_gids() const;

  /// @brief Check if this transmitter is registered with a PubSubContext
  bool is_registered() const { return registered_; }

  /// @brief Inject the PubSubContext pointer (called from addRoutes)
  ///
  /// Called by PubSubContext::addRoutes() when it discovers this transmitter
  /// in an entity.  Only stores the pointer — actual registration is deferred
  /// to initialize(), which runs after the context entity is fully activated.
  void set_context(PubSubContext* ctx) { network_context_ = ctx; }

  /// @brief Register this transmitter with a PubSubContext
  ///
  /// Can be called explicitly (e.g., from tests) to register with a context
  /// that is already fully initialized.  In the normal GXF lifecycle,
  /// registration happens automatically in initialize().
  ///
  /// @param ctx The PubSubContext to register with (must not be null)
  /// @return GXF_SUCCESS on success, or an error code
  gxf_result_t register_with_context(PubSubContext* ctx);

  /// @brief Called by PubSubContext when a subscriber matches
  void on_subscriber_matched(const SubscriberInfo& info);

  /// @brief Called by PubSubContext when a subscriber unmatches
  void on_subscriber_unmatched(const SubscriberGid& gid);

  //----------------------------------------------------------------------------
  // Programmatic Configuration
  //----------------------------------------------------------------------------

  /// @brief Set the QoS profile programmatically
  ///
  /// QoS is not yet exposed as a GXF parameter (it requires struct serialization).
  /// Use this method to configure QoS before initialize().
  void set_qos(const QoSProfile& qos);

 private:
  // Parameters (for GXF configuration)
  Parameter<std::string> topic_name_param_;
  Parameter<uint64_t> capacity_;
  Parameter<uint64_t> policy_;

  // Cached/direct configuration (for programmatic use)
  std::string topic_name_;
  QoSProfile qos_{QoSProfile::Default()};
  PubSubContext* network_context_{nullptr};

  // Publisher state
  PublisherGid gid_;
  bool registered_{false};

  // Matched subscribers
  std::set<SubscriberGid> matched_subscribers_;
  mutable std::mutex matched_mutex_;

  // Entity queue (double-buffered staging queue)
  std::unique_ptr<queue_t> queue_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_PUBSUB_TRANSMITTER_HPP_
