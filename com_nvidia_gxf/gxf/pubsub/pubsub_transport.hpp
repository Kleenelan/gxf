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
#ifndef PUBSUB_ITRANSPORT_HPP
#define PUBSUB_ITRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gxf/core/expected.hpp"
#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/gid.hpp"

namespace nvidia {
namespace gxf {

// Forward declaration -- full definition in pubsub_native_buffer.hpp (no CUDA dependency).
struct NativeDescriptorPayload;

/// @brief Identifies the payload encoding carried in a message.
enum class PayloadMode : uint8_t {
  kSerializedBytes        = 0,  ///< existing byte / staging path (default)
  kNativeHandleDescriptor = 1,  ///< descriptor-only; GPU data flows via IPC
};

/// @brief Routing scope hint for the transport layer.
enum class TransportScope : uint8_t {
  kLocalOnly     = 0,  ///< never route off-host
  kRemoteAllowed = 1,  ///< normal routing (default)
  kDualPath      = 2,  ///< local subs get IPC descriptor, remote get bytes
};

/// @brief Transport model indicating how a backend routes messages
///
/// Determines how PubSubContext dispatches messages in send_message():
/// - **kEndpointAddressed**: The transport requires one send() call per
///   destination subscriber. Used by connection-oriented backends (UCX).
/// - **kTopicBased**: The transport handles fan-out internally — a single
///   send() call delivers to all subscribers on the topic. Used by
///   topic-based backends (DDS, Zenoh).
///
/// The default is kEndpointAddressed, preserving existing per-subscriber
/// send behavior for backends that do not override transport_model().
enum class TransportModel : int32_t {
  kEndpointAddressed = 0,  ///< UCX-like: must send once per destination
  kTopicBased = 1          ///< DDS/Zenoh-like: single write fans out automatically
};

/// @brief Metadata associated with each transported message
///
/// Contains information about message ordering and timing.
/// Backends may extend this with additional fields as needed.
struct MessageMetadata {
  /// Sequence number for ordering (per source endpoint)
  uint64_t sequence_number = 0;

  /// Timestamp when the message was sent (nanoseconds since epoch)
  uint64_t source_timestamp_ns = 0;

  /// Priority hint (0 = default, higher = more urgent)
  /// Backend may ignore if not supported
  uint32_t priority = 0;

  /// GID of the original publisher (for routing on receive side)
  Gid publisher_gid;

  /// GID of the intended destination endpoint (when known)
  ///
  /// - For endpoint-addressed transports (UCX-like), this should be set so the receiver
  ///   can route precisely without topic fan-out.
  /// - For topic/broadcast transports (DDS-like), this may be null/omitted; receivers
  ///   may fan-out locally based on matches and topic/type compatibility.
  Gid destination_gid;

  /// Topic name for topic-based transports (DDS, Zenoh)
  ///
  /// For topic-based transports, this allows direct routing by topic name
  /// without needing to look up the publisher_gid to determine the topic.
  /// - For endpoint-addressed transports (UCX-like), this may be ignored.
  /// - For topic-based transports (DDS, Zenoh), this enables direct key
  ///   expression / DataWriter lookup without an extra GID→topic mapping step.
  std::string topic_name;

  /// Express / low-latency mode flag (default: false)
  ///
  /// When true, hints the transport to bypass message batching for this
  /// message, reducing latency at the cost of higher per-message overhead.
  /// Useful for low-frequency, latency-sensitive control messages as
  /// opposed to high-throughput sensor data.
  ///
  /// Backend mapping:
  /// - Zenoh: Maps to the express flag, bypassing message batching
  /// - DDS: Can map to DDS latency budget QoS (or be ignored)
  /// - UCX: Typically ignored
  /// - Backends that do not support express mode may ignore this field
  bool express = false;

  /// Payload encoding for this message (byte-serialized or native descriptor)
  PayloadMode payload_mode{PayloadMode::kSerializedBytes};

  /// Routing scope hint (local-only, remote-allowed, or dual-path)
  TransportScope transport_scope{TransportScope::kRemoteAllowed};

  /// Wire format version of the native descriptor blob (0 = not applicable)
  uint8_t descriptor_format_version{0};

  /// Native protocol identifier for descriptor payloads (empty = not applicable)
  std::string protocol_name;

  /// Default constructor
  MessageMetadata() = default;

  /// Convenience constructor
  MessageMetadata(uint64_t seq, uint64_t timestamp)
      : sequence_number(seq), source_timestamp_ns(timestamp) {}
};

/// @brief Abstract transport interface for the pub/sub data plane
///
/// This is the key abstraction that makes transport backends interchangeable.
/// The transport layer is responsible for:
/// - Establishing connections between endpoints
/// - Sending and receiving serialized message payloads
/// - Managing connection lifecycle
///
/// Possible Concrete Implementations:
/// - InMemoryTransport: In-process delivery with optional fault injection
/// - UcxTransport: High-performance UCX-based transport
/// - DdsTransport: DDS data writer/reader wrapper
/// - ZenohTransport: Zenoh pub/sub with SHM zero-copy support
///
/// Thread Safety: Implementations must be thread-safe. The receive callback
/// may be invoked from any thread, so callback implementations must handle
/// concurrent invocation appropriately.
///
/// Lifecycle:
/// 1. Create instance
/// 2. Set callbacks (before initialize)
/// 3. Call initialize()
/// 4. Use create_publisher_endpoint/create_subscriber_endpoint (topic-based)
///    or connect_to (connection-oriented) for endpoint setup
/// 5. Use send for data transfer
/// 6. Use remove_publisher_endpoint/remove_subscriber_endpoint (topic-based)
///    or disconnect_from (connection-oriented) for endpoint teardown
/// 7. Call shutdown()
///
/// Transport Models (see TransportModel enum):
/// The interface supports two transport models. Override transport_model()
/// to declare which model the backend uses:
/// - **kEndpointAddressed** (UCX): Uses connect_to()/disconnect_from() for
///   explicit endpoint connections. Topic-based lifecycle methods are no-ops.
///   PubSubContext sends once per matched subscriber.
/// - **kTopicBased** (DDS, Zenoh): Uses create_publisher_endpoint()/
///   create_subscriber_endpoint() to create per-topic transport endpoints.
///   connect_to()/disconnect_from() may be no-ops. PubSubContext sends
///   once per publisher; the transport handles fan-out internally.
///
/// @note Payloads are raw bytes - serialization is handled separately by
/// PubSubEntitySerializer. This separation allows different serialization
/// strategies without changing the transport.
class PubSubTransport {
 public:
  virtual ~PubSubTransport() = default;

  //----------------------------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------------------------

  /// @brief Initialize the transport backend
  ///
  /// Must be called before any other methods (except setters).
  /// Allocates resources, starts background threads, etc.
  ///
  /// @return Success or error code
  virtual Expected<void> initialize() = 0;

  /// @brief Shutdown the transport backend
  ///
  /// Releases all connections, resources, and background threads.
  ///
  /// **Shutdown Contract** (implementors must guarantee):
  /// 1. After shutdown() returns, **no more** ReceiveCallback,
  ///    ConnectionEstablishedCallback, or ConnectionLostCallback
  ///    invocations will occur.
  /// 2. Any callbacks that are **currently executing** at the time
  ///    shutdown() is called will **complete** before shutdown() returns.
  ///    (i.e., shutdown blocks until in-flight callbacks finish.)
  /// 3. Calling send() after shutdown() returns an error
  ///    (GXF_CONTRACT_INVALID_SEQUENCE or backend-specific code).
  /// 4. Safe to call multiple times (idempotent). A second call is a
  ///    no-op and returns Success.
  /// 5. If remove_publisher_endpoint() / remove_subscriber_endpoint()
  ///    have not been called for all endpoints, shutdown() must still
  ///    succeed — it should clean up remaining endpoints internally.
  ///
  /// **Shutdown Ordering** (enforced by PubSubContext::deinitialize()):
  ///   a. Callbacks on both discovery and transport are cleared (set to nullptr).
  ///   b. Discovery endpoints are removed, then discovery_->shutdown().
  ///   c. Transport endpoints are removed, then transport_->shutdown().
  /// Transport shutdown always happens **after** discovery shutdown.
  ///
  /// @return Success or error code
  virtual Expected<void> shutdown() = 0;

  /// @brief Check if the transport is initialized
  /// @return true if initialized and ready for use
  virtual bool is_initialized() const = 0;

  //----------------------------------------------------------------------------
  // Transport Model
  //----------------------------------------------------------------------------

  /// @brief Query the transport model for send dispatch optimization
  ///
  /// Returns the transport's routing model, which determines how
  /// PubSubContext::send_message() dispatches messages:
  ///
  /// - kEndpointAddressed (default): send() is called once per matched
  ///   subscriber with destination_gid set. Used by connection-oriented
  ///   backends like UCX.
  /// - kTopicBased: send() is called once using publisher_gid; the transport
  ///   handles fan-out to all subscribers internally. Used by DDS and Zenoh.
  ///
  /// The return value must be stable for the transport's lifetime.
  virtual TransportModel transport_model() const {
    return TransportModel::kEndpointAddressed;
  }

  //----------------------------------------------------------------------------
  // Backend Capabilities (Optional)
  //----------------------------------------------------------------------------

  /// @brief Whether the transport handles topic-to-subscriber matching natively
  ///
  /// When true, the transport (and/or its discovery partner) natively matches
  /// publishers to subscribers by topic name and QoS — the framework's
  /// TopicRegistry matching is redundant.
  ///
  /// - DDS: true — SEDP handles topic matching + QoS compatibility at the
  ///   RTPS protocol level.
  /// - Zenoh: true — key expression matching is native to Zenoh.
  /// - UCX: false (default) — the framework's TopicRegistry provides matching.
  virtual bool native_topic_matching() const { return false; }

  /// @brief Whether the transport enforces QoS policies at the protocol level
  ///
  /// When true, QoS incompatibility is detected and enforced by the transport
  /// protocol itself (e.g., DDS RTPS reliability/durability negotiation).
  /// When false, the framework's TopicRegistry checkQoSCompatibility() is
  /// the sole enforcement point.
  ///
  /// - DDS: true — RTPS-level reliability, durability, and deadline
  ///   enforcement between DataWriters and DataReaders.
  /// - Zenoh: false — Zenoh does not enforce QoS compatibility between
  ///   publishers and subscribers; the framework must check.
  /// - UCX: false (default) — no native QoS.
  virtual bool native_qos_enforcement() const { return false; }

  /// @brief Whether the transport supports multicast delivery
  ///
  /// - DDS: true — RTPS supports multicast for discovery and data.
  /// - Zenoh: true — scouting uses multicast.
  /// - UCX: false (default) — point-to-point only.
  virtual bool supports_multicast() const { return false; }

  /// @brief Whether the transport requires explicit connect_to() calls
  ///
  /// When true, PubSubContext must call connect_to() for each discovered
  /// subscriber before sending. When false, the transport handles endpoint
  /// setup internally (via create_publisher_endpoint() / create_subscriber_endpoint()).
  ///
  /// - UCX: true (default) — requires explicit endpoint connections.
  /// - DDS: false — DataWriters/DataReaders handle connections internally.
  /// - Zenoh: false — publishers/subscribers handle routing internally.
  virtual bool requires_explicit_connections() const { return true; }

  /// @brief Whether the transport can deliver native buffer descriptors
  ///
  /// When true, the transport supports the send_native_descriptor() path
  /// for delivering lightweight IPC descriptors (e.g. CUDA IPC handles)
  /// instead of fully serialized byte payloads.
  ///
  /// - DDS/Zenoh (with native buffer extension): true
  /// - UCX/Loopback: false (default)
  virtual bool supports_native_buffers() const { return false; }

  /// @brief Whether the transport can simultaneously deliver native descriptors
  /// to local-eligible subscribers and byte payloads to remote/ineligible
  /// subscribers on the same logical topic.
  virtual bool supports_mixed_local_remote_fanout() const { return false; }

  /// @brief Whether the given native buffer profile name is supported
  ///
  /// @param profile Profile identifier, e.g. "cuda_ipc_same_gpu_v1"
  /// @return true if this transport can handle the named profile
  virtual bool supports_native_profile(const std::string& /*profile*/) const {
    return false;
  }

  /// @brief Send a native buffer descriptor to subscribers on a topic
  ///
  /// Optional native descriptor send path. Backends that support
  /// native buffers override this to deliver lightweight IPC descriptors
  /// instead of full byte payloads.
  ///
  /// @param topic_name Topic to publish on
  /// @param descriptor The native descriptor payload
  /// @param metadata Message metadata (payload_mode should be kNativeHandleDescriptor)
  /// @return Success or GXF_NOT_IMPLEMENTED if not supported
  virtual Expected<void> send_native_descriptor(
      const std::string& /*topic_name*/,
      const NativeDescriptorPayload& /*descriptor*/,
      const MessageMetadata& /*metadata*/) {
    return Unexpected{GXF_NOT_IMPLEMENTED};
  }

  //----------------------------------------------------------------------------
  // Connection Management
  //----------------------------------------------------------------------------

  /// @brief Establish a connection to a remote endpoint
  ///
  /// Must be called before sending data to the endpoint. For connection-
  /// oriented transports, this establishes the actual connection. For
  /// connectionless transports, this may just record the endpoint info.
  ///
  /// Safe to call multiple times for the same endpoint (updates info).
  ///
  /// @param remote_endpoint Information about the remote endpoint
  /// @return Success or error code (e.g., connection failed)
  virtual Expected<void> connect_to(const EndpointInfo& remote_endpoint) = 0;

  /// @brief Disconnect from a remote endpoint
  ///
  /// Closes the connection and releases associated resources.
  /// After disconnection, send() to this endpoint will fail.
  /// Safe to call even if never connected (no-op).
  ///
  /// @param remote_gid GID of the remote endpoint to disconnect from
  /// @return Success or error code
  virtual Expected<void> disconnect_from(const Gid& remote_gid) = 0;

  /// @brief Check if connected to a remote endpoint
  ///
  /// @param remote_gid GID of the remote endpoint
  /// @return true if connected
  virtual bool is_connected_to(const Gid& remote_gid) const = 0;

  //----------------------------------------------------------------------------
  // Topic-Based Endpoint Lifecycle (Optional)
  //----------------------------------------------------------------------------

  /// @brief Create a transport-level publisher endpoint for a topic
  ///
  /// Called by PubSubContext when a publisher is registered. For topic-based
  /// transports (DDS, Zenoh), this creates the underlying transport endpoint
  /// (e.g., DDS DataWriter, Zenoh publisher) for the given topic.
  ///
  /// For connection-oriented transports (UCX), this is typically a no-op —
  /// endpoints are established via connect_to() when subscribers are discovered.
  ///
  /// @param topic_name Topic to publish on
  /// @param publisher_gid GID assigned to this publisher
  /// @param qos Quality of service settings for this publisher
  /// @return Success or error code
  virtual Expected<void> create_publisher_endpoint(
      const std::string& topic_name,
      const Gid& publisher_gid,
      const QoSProfile& qos = QoSProfile{}) {
    (void)topic_name; (void)publisher_gid; (void)qos;
    return Success;  // No-op for connection-oriented transports
  }

  /// @brief Create a transport-level subscriber endpoint for a topic
  ///
  /// Called by PubSubContext when a subscriber is registered. For topic-based
  /// transports (DDS, Zenoh), this creates the underlying transport endpoint
  /// (e.g., DDS DataReader, Zenoh subscriber) for the given topic.
  ///
  /// For connection-oriented transports (UCX), this is typically a no-op.
  ///
  /// @param topic_name Topic to subscribe to
  /// @param subscriber_gid GID assigned to this subscriber
  /// @param qos Quality of service settings for this subscriber
  /// @return Success or error code
  virtual Expected<void> create_subscriber_endpoint(
      const std::string& topic_name,
      const Gid& subscriber_gid,
      const QoSProfile& qos = QoSProfile{}) {
    (void)topic_name; (void)subscriber_gid; (void)qos;
    return Success;  // No-op for connection-oriented transports
  }

  /// @brief Remove a transport-level publisher endpoint
  ///
  /// Called by PubSubContext when a publisher is unregistered. For topic-based
  /// transports, this removes the underlying transport endpoint (e.g., deletes
  /// the DDS DataWriter, undeclares the Zenoh publisher).
  ///
  /// For connection-oriented transports, this is typically a no-op.
  ///
  /// @param publisher_gid GID of the publisher to remove
  /// @return Success or error code
  virtual Expected<void> remove_publisher_endpoint(const Gid& publisher_gid) {
    (void)publisher_gid;
    return Success;
  }

  /// @brief Remove a transport-level subscriber endpoint
  ///
  /// Called by PubSubContext when a subscriber is unregistered. For topic-based
  /// transports, this removes the underlying transport endpoint.
  ///
  /// For connection-oriented transports, this is typically a no-op.
  ///
  /// @param subscriber_gid GID of the subscriber to remove
  /// @return Success or error code
  virtual Expected<void> remove_subscriber_endpoint(const Gid& subscriber_gid) {
    (void)subscriber_gid;
    return Success;
  }

  //----------------------------------------------------------------------------
  // Data Plane - Send
  //----------------------------------------------------------------------------

  /// @brief Send a message to a remote endpoint
  ///
  /// The message is sent asynchronously - this method returns immediately
  /// after queueing the message. Use get_send_queue_size() to monitor
  /// backpressure.
  ///
  /// @param destination_gid GID of the destination endpoint
  /// @param payload Serialized message data
  /// @param metadata Message metadata (sequence number, timestamp, etc.)
  /// @return Success or error code (e.g., not connected, queue full)
  virtual Expected<void> send(
      const Gid& destination_gid,
      const std::vector<uint8_t>& payload,
      const MessageMetadata& metadata) = 0;

  /// @brief Move-semantic variant of send(). Avoids copying the payload
  /// if the transport can take ownership. Default delegates to the
  /// const-ref overload.
  virtual Expected<void> send(
      const Gid& destination_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata) {
    // Default: delegate to const ref version
    return send(destination_gid, payload, metadata);
  }

  //----------------------------------------------------------------------------
  // Data Plane - Send by Topic (Optional)
  //----------------------------------------------------------------------------

  /// @brief Send a message to a topic (for topic-based transports)
  ///
  /// Preferred overload for topic-based transports (DDS, Zenoh) where
  /// routing is by topic name rather than endpoint GID. Avoids the
  /// GID->topic lookup step that the endpoint-addressed send() requires.
  ///
  /// Default implementation delegates to the endpoint-addressed send()
  /// using metadata.publisher_gid as the destination, so connection-oriented
  /// transports need not override this.
  ///
  /// @note C++ name hiding: derived classes that override the GID-based
  ///   send() must add `using PubSubTransport::send;` to keep the
  ///   topic-based overloads visible.
  ///
  /// @param topic_name Topic to publish on
  /// @param payload Serialized message data
  /// @param metadata Message metadata (must include publisher_gid)
  /// @return Success or error code
  virtual Expected<void> send(
      const std::string& topic_name,
      const std::vector<uint8_t>& payload,
      const MessageMetadata& metadata) {
    // Default: delegate to endpoint-addressed send via publisher_gid
    return send(metadata.publisher_gid, payload, metadata);
  }

  /// @brief Move-semantic variant of the topic-based send(). Default
  /// delegates to the const-ref topic-based overload.
  virtual Expected<void> send(
      const std::string& topic_name,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata) {
    // Default: delegate to const ref topic-based version
    return send(topic_name, payload, metadata);
  }

  //----------------------------------------------------------------------------
  // Data Plane - Async Send (Optional)
  //----------------------------------------------------------------------------

  /// @brief Callback invoked when an asynchronous send completes
  ///
  /// @param result Success or error code from the send operation
  using SendCompletionCallback = std::function<void(Expected<void> result)>;

  /// @brief Asynchronous send with completion notification (GID-based)
  ///
  /// Queues a message for delivery and returns immediately. The completion
  /// callback is invoked when the transport has finished processing the
  /// message (successfully sent, or failed).
  ///
  /// The default implementation delegates to the synchronous send() and
  /// invokes the callback inline. Backends with native async support
  /// (e.g., UCX amSend()) should override for true non-blocking behavior.
  ///
  /// Ownership: the payload is moved into the transport — the caller must
  /// not access it after this call returns.
  ///
  /// @param destination_gid GID of the destination endpoint
  /// @param payload Serialized message data (moved into transport)
  /// @param metadata Message metadata
  /// @param on_complete Callback invoked when the send completes (may be
  ///                    invoked from any thread). May be nullptr if the
  ///                    caller does not need completion notification.
  virtual void send_async(
      const Gid& destination_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata,
      SendCompletionCallback on_complete) {
    auto result = send(destination_gid, std::move(payload), metadata);
    if (on_complete) on_complete(std::move(result));
  }

  /// @brief Topic-based variant of send_async() for backends that route
  /// by topic name (DDS, Zenoh). Default delegates to the synchronous
  /// topic-based send().
  virtual void send_async(
      const std::string& topic_name,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata,
      SendCompletionCallback on_complete) {
    auto result = send(topic_name, std::move(payload), metadata);
    if (on_complete) on_complete(std::move(result));
  }

  //----------------------------------------------------------------------------
  // Data Plane - Receive (Push-based)
  //----------------------------------------------------------------------------

  /// @brief Callback invoked when a message is received
  ///
  /// Parameters:
  /// - source_gid: GID of the sender
  /// - payload: Received message data (moved for efficiency)
  /// - metadata: Message metadata from sender
  ///
  /// @note The callback may be invoked from any thread. Implementations
  /// should minimize work in the callback to avoid blocking the receive path.
  ///
  /// @warning **Reentrancy**: For intra-process communication, some backends
  /// (notably DDS) may invoke ReceiveCallback **synchronously from within
  /// send()**. This happens because the DDS middleware detects that the
  /// destination subscriber is in the same process and delivers the message
  /// immediately during the DataWriter::write() call.
  ///
  /// Consequence: callback implementations **must not** hold locks that
  /// send() also acquires, or a deadlock will occur. PubSubContext handles
  /// this by releasing its mutex before calling transport->send() and by
  /// using only lock-free data structures in the receive path.
  ///
  /// Backend-specific reentrancy notes:
  /// - **DDS**: High risk — intra-process delivery via on_data_available()
  ///   is synchronous from within DataWriter::write().
  /// - **Zenoh**: Moderate risk — subscriber callbacks fire from Zenoh's
  ///   internal thread pool, but intra-process delivery timing varies.
  /// - **UCX**: Low risk — callbacks fire from explicit worker.progress()
  ///   calls, giving the application control over callback dispatch.
  using ReceiveCallback = std::function<void(
      const Gid& source_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata)>;

  /// @brief Set callback for received messages
  ///
  /// Should be set before initialize() to avoid missing early messages.
  /// Only one callback can be set - subsequent calls replace the previous.
  ///
  /// @param callback Function to call when a message is received
  virtual void set_on_receive(ReceiveCallback callback) = 0;

  //----------------------------------------------------------------------------
  // Connection Events (Optional)
  //----------------------------------------------------------------------------

  /// @brief Callback invoked when a connection is established
  using ConnectionEstablishedCallback = std::function<void(const Gid& remote_gid)>;

  /// @brief Callback invoked when a connection is lost unexpectedly
  ///
  /// @note May be invoked from any thread (e.g., a transport health-check
  /// thread). After shutdown() returns, this callback will not be invoked.
  using ConnectionLostCallback = std::function<void(const Gid& remote_gid)>;

  /// @brief Set callback for connection established events
  /// @param callback Function to call when connection is ready
  virtual void set_on_connection_established(ConnectionEstablishedCallback callback) = 0;

  /// @brief Set callback for connection lost events (unexpected disconnection)
  /// @param callback Function to call when connection is lost
  virtual void set_on_connection_lost(ConnectionLostCallback callback) = 0;

  //----------------------------------------------------------------------------
  // Metrics and Diagnostics
  //----------------------------------------------------------------------------

  /// @brief Get number of messages waiting to be sent
  ///
  /// Useful for backpressure monitoring. High values indicate the
  /// transport is congested.
  ///
  /// @return Number of queued outbound messages
  virtual size_t get_send_queue_size() const = 0;

  /// @brief Get number of received messages waiting to be processed
  ///
  /// Useful for monitoring receive path congestion.
  ///
  /// @return Number of queued inbound messages
  virtual size_t get_receive_queue_size() const = 0;

  /// @brief Get number of active connections
  /// @return Number of connected remote endpoints
  virtual size_t get_connection_count() const = 0;
};

/// @brief Shared pointer type for PubSubTransport
using PubSubTransportPtr = std::shared_ptr<PubSubTransport>;

}  // namespace gxf
}  // namespace nvidia

#endif /* PUBSUB_ITRANSPORT_HPP */
