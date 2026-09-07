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
#ifndef NVIDIA_GXF_PUBSUB_PUBSUB_RECEIVER_HPP_
#define NVIDIA_GXF_PUBSUB_PUBSUB_RECEIVER_HPP_

#include <condition_variable>
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
#include "gxf/std/receiver.hpp"

namespace nvidia {
namespace gxf {

// Forward declaration (avoid circular dependency with pubsub_context.hpp)
class PubSubContext;

/// @brief Backend-agnostic subscriber component
///
/// PubSubReceiver is a GXF Receiver that subscribes to messages on a topic.
/// It uses PubSubContext to abstract away backend details (gRPC, DDS, UCX, etc.).
///
/// Features:
/// - Topic-based subscription
/// - QoS profile support
/// - Matched publisher tracking
/// - Double-buffered queue for received entities
/// - Blocking wait for messages
///
/// Usage:
/// 1. Create PubSubContext and inject backends
/// 2. Create PubSubReceiver with topic_name and context reference
/// 3. PubSubContext routes incoming messages to this receiver
/// 4. Call sync() to move entities from backstage to main stage
/// 5. Call receive() to get the next entity
///
/// Thread Safety: All public methods are thread-safe.
class PubSubReceiver : public Receiver {
 public:
  using queue_t = ::gxf::staging_queue::StagingQueue<Entity>;

  PubSubReceiver() = default;
  ~PubSubReceiver() override = default;

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

  /// @brief Push an entity to the backstage (used by PubSubContext)
  gxf_result_t push_abi(gxf_uid_t other) override;

  /// @brief Peek at entity at given index in the main stage
  gxf_result_t peek_abi(gxf_uid_t* uid, int32_t index) override;

  /// @brief Get the maximum capacity of the queue
  size_t capacity_abi() override;

  /// @brief Get the current number of entities in the main stage
  size_t size_abi() override;

  //----------------------------------------------------------------------------
  // Receiver Interface
  //----------------------------------------------------------------------------

  /// @brief Receive the next entity (same as pop)
  gxf_result_t receive_abi(gxf_uid_t* uid) override;

  /// @brief Get the number of entities in the backstage
  size_t back_size_abi() override;

  /// @brief Return a coherent snapshot of both stage sizes under a single lock acquisition
  StageSizeSnapshot stage_sizes_abi() override;

  /// @brief Peek at entity in the backstage
  gxf_result_t peek_back_abi(gxf_uid_t* uid, int32_t index) override;

  /// @brief Move entities from backstage to main stage
  gxf_result_t sync_abi() override;

  /// @brief Wait for new entities to arrive (blocking)
  gxf_result_t wait_abi() override;

  //----------------------------------------------------------------------------
  // Pub/Sub Specific
  //----------------------------------------------------------------------------

  /// @brief Get the unique identifier for this subscriber
  const SubscriberGid& gid() const { return gid_; }

  /// @brief Get the topic name
  const std::string& topic_name() const;

  /// @brief Get the QoS profile
  const QoSProfile& qos() const;

  /// @brief Get the number of matched publishers
  size_t matched_publisher_count() const;

  /// @brief Check if there are any matched publishers
  bool has_matched_publishers() const;

  /// @brief Get the GIDs of all currently matched publishers
  /// @return Snapshot vector of publisher GIDs (taken under lock)
  std::vector<PublisherGid> matched_publisher_gids() const;

  /// @brief Check if this receiver is registered with a PubSubContext
  bool is_registered() const { return registered_; }

  /// @brief Inject the PubSubContext pointer (called from addRoutes)
  ///
  /// Called by PubSubContext::addRoutes() when it discovers this receiver
  /// in an entity.  Only stores the pointer — actual registration is deferred
  /// to initialize(), which runs after the context entity is fully activated.
  void set_context(PubSubContext* ctx) { network_context_ = ctx; }

  /// @brief Register this receiver with a PubSubContext
  ///
  /// Can be called explicitly (e.g., from tests) to register with a context
  /// that is already fully initialized.  In the normal GXF lifecycle,
  /// registration happens automatically in initialize().
  ///
  /// @param ctx The PubSubContext to register with (must not be null)
  /// @return GXF_SUCCESS on success, or an error code
  gxf_result_t register_with_context(PubSubContext* ctx);

  /// @brief Called by PubSubContext when a publisher matches
  void on_publisher_matched(const PublisherInfo& info);

  /// @brief Called by PubSubContext when a publisher unmatches
  void on_publisher_unmatched(const PublisherGid& gid);

  /// @brief Push a received entity to the backstage (called by PubSubContext)
  /// Thread-safe, notifies waiting threads.
  void push_received_entity(Entity entity);

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

  // Subscriber state
  SubscriberGid gid_;
  bool registered_{false};

  // Matched publishers
  std::set<PublisherGid> matched_publishers_;
  mutable std::mutex matched_mutex_;

  // Entity queue (double-buffered staging queue)
  std::unique_ptr<queue_t> queue_;

  // Condition variable + mutex for blocking wait and safe shutdown.
  //
  // IMPORTANT: `StagingQueue` is internally thread-safe (it locks inside each method), but that is
  // NOT sufficient here because:
  // 1) We also synchronize access to the `queue_` POINTER itself. `deinitialize()` resets `queue_`
  //    to nullptr to unblock `wait_abi()`. Without an external lock, another thread could observe
  //    a non-null `queue_` and call into it while `queue_` is being reset/destroyed
  //    (data race / potential use-after-free).
  // 2) `wait_abi()` uses a condition-variable predicate that reads `queue_` and may query its
  //    sizes. The predicate check and the updates + notify in `push_received_entity()`/
  //    `deinitialize()` must be protected by the SAME mutex to avoid missed wakeups.
  //
  // PERFORMANCE NOTE: All queue operations (pop_abi, push_abi, peek_abi, size_abi, sync_abi,
  // push_received_entity) acquire this mutex, which introduces lock contention between the
  // transport receive thread and the codelet execution thread. In practice, this overhead is
  // expected to be small relative to serialization/deserialization and network I/O costs.
  // If profiling reveals contention as a bottleneck, consider lock-free queue designs or
  // finer-grained locking strategies.
  std::condition_variable wait_cv_;
  std::mutex wait_mutex_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_PUBSUB_RECEIVER_HPP_
