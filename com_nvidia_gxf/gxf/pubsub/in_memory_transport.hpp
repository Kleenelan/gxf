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
#ifndef NVIDIA_GXF_PUBSUB_IN_MEMORY_TRANSPORT_HPP_
#define NVIDIA_GXF_PUBSUB_IN_MEMORY_TRANSPORT_HPP_

#include <mutex>
#include <unordered_map>
#include <vector>

#include "gxf/pubsub/pubsub_transport.hpp"

namespace nvidia {
namespace gxf {

/// @brief In-memory `PubSubTransport` implementation for single-process use
///
/// Delivers messages within the same process via direct callback dispatch,
/// with no serialization, IPC, or network overhead. Suitable for:
/// - Single-process pub/sub pipeline testing
/// - CI environments without external middleware dependencies
/// - Performance baseline measurements (isolates matching/routing overhead)
///
/// Transport model: `kEndpointAddressed` — PubSubContext sends one call per
/// matched subscriber. Use `register_subscriber_endpoint()` to bind each
/// subscriber GID to its receive callback before sending.
///
/// Delivery model: synchronous by default — `send()` invokes the subscriber
/// callback in the caller's thread before returning. This makes tests
/// deterministic. There is no receive queue; `get_receive_queue_size()` is
/// always 0.
///
/// Fault Injection (optional):
/// Deterministic patterns (not probabilistic) for reproducible test scenarios:
/// - `set_drop_pattern({false, true})` drops every other message
/// - `set_reorder_pattern({false, true})` delays every other message
/// Delayed messages are held in a buffer until `flush_delayed_messages()` is
/// called explicitly. Non-delayed sends do not auto-flush the buffer.
///
/// Thread Safety: all public methods are thread-safe.
///
/// Callback Semantics:
/// - Receive callbacks are invoked synchronously in the calling thread.
/// - Callbacks are invoked **outside** the transport's internal mutex to
///   prevent lock inversion between the send path and any callback that
///   itself calls transport methods.
///
/// Usage:
/// @code
/// auto transport = std::make_shared<InMemoryTransport>();
/// transport->initialize();
///
/// Gid subscriber_gid = Gid::generate();
/// transport->register_subscriber_endpoint(subscriber_gid,
///     [](const Gid& src, auto&& payload, const MessageMetadata& meta) {
///       // Handle received message
///     });
///
/// transport->send(subscriber_gid, {1, 2, 3}, MessageMetadata{});
/// @endcode
class InMemoryTransport : public PubSubTransport {
 public:
  InMemoryTransport() = default;
  ~InMemoryTransport() override = default;

  // Non-copyable, non-movable (contains mutex)
  InMemoryTransport(const InMemoryTransport&) = delete;
  InMemoryTransport& operator=(const InMemoryTransport&) = delete;
  InMemoryTransport(InMemoryTransport&&) = delete;
  InMemoryTransport& operator=(InMemoryTransport&&) = delete;

  //----------------------------------------------------------------------------
  // PubSubTransport Lifecycle
  //----------------------------------------------------------------------------

  Expected<void> initialize() override;
  Expected<void> shutdown() override;
  bool is_initialized() const override;

  //----------------------------------------------------------------------------
  // PubSubTransport Transport Model
  //----------------------------------------------------------------------------

  /// @brief Returns kEndpointAddressed. PubSubContext sends one call per
  /// matched subscriber GID.
  TransportModel transport_model() const override {
    return TransportModel::kEndpointAddressed;
  }

  /// @brief Returns false. Matching is delegated to the framework's
  /// TopicRegistry via PubSubContext.
  bool native_topic_matching() const override { return false; }

  /// @brief Returns false. QoS enforcement is delegated to the framework's
  /// checkQoSCompatibility() in PubSubContext.
  bool native_qos_enforcement() const override { return false; }

  /// @brief Returns false. Single-process; no network multicast needed.
  bool supports_multicast() const override { return false; }

  /// @brief Returns true. PubSubContext will call connect_to() for each matched
  /// subscriber before sending. connect_to() records the connection and fires
  /// the established callback; no actual network handshake is performed.
  bool requires_explicit_connections() const override { return true; }

  //----------------------------------------------------------------------------
  // PubSubTransport Connection Management
  //----------------------------------------------------------------------------

  Expected<void> connect_to(const EndpointInfo& remote_endpoint) override;
  Expected<void> disconnect_from(const Gid& remote_gid) override;
  bool is_connected_to(const Gid& remote_gid) const override;

  //----------------------------------------------------------------------------
  // PubSubTransport Data Plane
  //----------------------------------------------------------------------------

  // Bring base class topic-based send() overloads into scope (avoid C++ name hiding)
  using PubSubTransport::send;

  Expected<void> send(
      const Gid& destination_gid,
      const std::vector<uint8_t>& payload,
      const MessageMetadata& metadata) override;

  Expected<void> send(
      const Gid& destination_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata) override;

  /// Set the global dispatch callback — used by PubSubContext to receive all
  /// messages routed through the context. Prefer `register_subscriber_endpoint()`
  /// for tests that own routing directly.
  void set_on_receive(ReceiveCallback callback) override;

  //----------------------------------------------------------------------------
  // PubSubTransport Connection Events
  //----------------------------------------------------------------------------

  void set_on_connection_established(ConnectionEstablishedCallback callback) override;
  void set_on_connection_lost(ConnectionLostCallback callback) override;

  //----------------------------------------------------------------------------
  // PubSubTransport Metrics
  //----------------------------------------------------------------------------

  size_t get_send_queue_size() const override;
  size_t get_receive_queue_size() const override;
  size_t get_connection_count() const override;

  //----------------------------------------------------------------------------
  // M:N Routing
  //----------------------------------------------------------------------------

  /// @brief Register a per-subscriber receive callback
  ///
  /// Binds a subscriber GID to the callback that delivers messages directly
  /// to that subscriber, bypassing the global dispatch path. Use this in tests
  /// or driver code that owns routing without going through PubSubContext.
  ///
  /// Delivery priority in `send()`:
  /// 1. Per-subscriber callback (registered here) — preferred
  /// 2. Global dispatch callback (set via `set_on_receive()`) — PubSubContext path
  /// 3. Neither set -> GXF_LOG_WARNING + message dropped
  ///
  /// @param subscriber_gid  GID of the subscriber endpoint
  /// @param callback        Callback to invoke when a message arrives for this GID
  void register_subscriber_endpoint(const Gid& subscriber_gid, ReceiveCallback callback);

  /// @brief Unregister a per-subscriber receive callback
  ///
  /// After this call, messages sent to `subscriber_gid` fall back to the
  /// global callback (or are silently dropped if no global callback is set).
  ///
  /// @param subscriber_gid  GID of the subscriber endpoint to unregister
  void unregister_subscriber_endpoint(const Gid& subscriber_gid);

  //----------------------------------------------------------------------------
  // Source Identity
  //----------------------------------------------------------------------------

  /// @brief Override the source GID stamped onto outgoing messages
  ///
  /// By default a random GID is generated during `initialize()`. Call this
  /// to pin the source GID to a specific value (e.g. the publisher's GID as
  /// assigned by PubSubContext).
  ///
  /// @param gid  Source GID for outgoing messages
  void set_source_gid(const Gid& gid);

  /// @brief Get the current source GID
  Gid get_source_gid() const;

  //----------------------------------------------------------------------------
  // Fault Injection — Deterministic Patterns
  //----------------------------------------------------------------------------

  /// @brief Set a deterministic drop pattern
  ///
  /// The pattern is cycled for each send call:
  /// - `{false, true}` drops every other message
  /// - `{false, false, true}` drops every third message
  ///
  /// Dropped messages are silently discarded (send() returns Success).
  ///
  /// @param pattern  Cycle of drop flags (true = drop)
  void set_drop_pattern(std::vector<bool> pattern);

  /// @brief Set a deterministic reorder pattern
  ///
  /// The pattern is cycled for each send call:
  /// - `{false, true}` delays every other message
  ///
  /// Delayed messages are buffered until `flush_delayed_messages()` is called.
  /// Non-delayed sends do not auto-flush the buffer.
  ///
  /// @param pattern Cycle of delay flags (true = delay)
  void set_reorder_pattern(std::vector<bool> pattern);

  /// @brief Clear all active fault injection patterns and reset indices
  void clear_fault_injection();

  /// @brief Flush all buffered (delayed) messages immediately
  ///
  /// Delivers all messages that were held by the reorder pattern.
  /// Callbacks are invoked outside the internal mutex in buffer order.
  void flush_delayed_messages();

  //----------------------------------------------------------------------------
  // Statistics
  //----------------------------------------------------------------------------

  /// @brief Total messages silently dropped by the drop pattern
  size_t get_dropped_count() const;

  /// @brief Total messages successfully delivered to a receive callback
  size_t get_delivered_count() const;

  /// @brief Messages currently held in the reorder delay buffer
  size_t get_delayed_count() const;

  /// @brief Reset dropped and delivered counters to zero
  void reset_statistics();

 private:
  struct DelayedMessage {
    Gid source_gid;
    Gid destination_gid;
    std::vector<uint8_t> payload;
    MessageMetadata metadata;
  };

  /// Deliver one message: select callback, increment counter, invoke outside mutex.
  void deliver_message(
      const Gid& source_gid,
      const Gid& destination_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata);

  mutable std::mutex mutex_;
  bool initialized_ = false;

  // Source identity stamped on outgoing messages
  Gid source_gid_;

  // connect_to() / disconnect_from() endpoint cache
  std::unordered_map<Gid, EndpointInfo> connections_;

  // Global receive callback (used when no per-subscriber callback is registered)
  ReceiveCallback default_receive_callback_;

  // Per-subscriber routing: destination GID -> callback
  std::unordered_map<Gid, ReceiveCallback> subscriber_callbacks_;

  // Connection event callbacks
  ConnectionEstablishedCallback on_connection_established_;
  ConnectionLostCallback on_connection_lost_;

  // Fault injection state
  std::vector<bool> drop_pattern_;
  std::vector<bool> reorder_pattern_;
  size_t drop_index_ = 0;
  size_t reorder_index_ = 0;

  // Buffered delayed messages (reorder pattern)
  std::vector<DelayedMessage> delayed_messages_;

  // Statistics
  size_t dropped_count_ = 0;
  size_t delivered_count_ = 0;
};

}  // namespace gxf
}  // namespace nvidia

#endif /* NVIDIA_GXF_PUBSUB_IN_MEMORY_TRANSPORT_HPP_ */
