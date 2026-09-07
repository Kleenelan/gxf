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
#ifndef NVIDIA_GXF_PUBSUB_PUBSUB_CONTEXT_HPP_
#define NVIDIA_GXF_PUBSUB_PUBSUB_CONTEXT_HPP_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gxf/core/component.hpp"
#include "gxf/core/entity.hpp"
#include "gxf/core/handle.hpp"
#include "gxf/core/parameter.hpp"
#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/gid.hpp"
#include "gxf/pubsub/pubsub_discovery.hpp"
#include "gxf/pubsub/pubsub_entity_serializer.hpp"
#include "gxf/pubsub/pubsub_transport.hpp"
#include "gxf/pubsub/qos_profile.hpp"
#include "gxf/pubsub/topic_registry.hpp"
#include "gxf/std/clock.hpp"
#include "gxf/std/network_context.hpp"
#include "gxf/std/receiver.hpp"
#include "gxf/std/transmitter.hpp"

namespace nvidia {
namespace gxf {

/// Controls when the native-buffer (e.g. CUDA IPC) path is used.
enum class NativeBufferPolicy {
  kDisabled,   ///< Always use byte/staging path.
  kPreferred,  ///< Use native path where eligible; fall back silently (default).
  kRequired,   ///< Use native path; fail loudly if not eligible.
};

// Forward declarations
class PubSubTransmitter;
class PubSubReceiver;

/// @brief Aggregated backend capabilities for PubSubContext
///
/// Combines the capabilities reported by the transport and discovery backends
/// into a single struct for convenient querying. PubSubContext builds this
/// by calling the virtual capability methods on its backends.
///
/// All fields have defaults corresponding to the most conservative backend
/// (UCX-like: endpoint-addressed, centralized, no native matching, requires
/// explicit connections). Topic-based backends (DDS, Zenoh) override specific
/// capabilities via their virtual methods.
///
/// Example:
/// @code
/// auto caps = context->backend_capabilities();
/// if (caps.transport_model == TransportModel::kTopicBased) {
///   // Single send — transport handles fan-out
/// }
/// if (caps.native_qos_enforcement) {
///   // Skip framework-level QoS compatibility checking
/// }
/// @endcode
struct BackendCapabilities {
  /// Transport routing model (from PubSubTransport::transport_model())
  TransportModel transport_model = TransportModel::kEndpointAddressed;

  /// Discovery communication model (from PubSubDiscovery::discovery_model())
  DiscoveryModel discovery_model = DiscoveryModel::kDecentralizedPassive;

  /// DDS/Zenoh: true — native topic-to-subscriber matching
  bool native_topic_matching = false;

  /// DDS: true — RTPS-level QoS enforcement; Zenoh/UCX: false
  bool native_qos_enforcement = false;

  /// DDS/Zenoh: true — supports multicast delivery
  bool supports_multicast = false;

  /// Zenoh: true — key expression wildcards (e.g., "sensor/**")
  bool supports_wildcard_subscription = false;

  /// UCX: true — must call connect_to(); DDS/Zenoh: false
  bool requires_explicit_connections = true;

  /// Whether the transport can deliver native buffer (e.g. CUDA IPC) descriptors
  bool supports_native_buffers = false;

  /// Whether the transport can deliver native descriptors to local subscribers
  /// and byte payloads to remote subscribers simultaneously
  bool supports_mixed_local_remote_fanout = false;
};

/// @brief Context that wires discovery, transport, and serialization together
///
/// PubSubContext is a NetworkContext that manages the pub/sub infrastructure.
/// It coordinates:
/// - Discovery: Finding remote publishers/subscribers
/// - Transport: Sending/receiving serialized messages
/// - Serialization: Converting entities to/from bytes
/// - Matching: Computing which endpoints should connect
///
/// This class is backend-agnostic - it uses abstract interfaces (PubSubDiscovery,
/// PubSubTransport, PubSubEntitySerializer) that can be implemented by different backends
/// (gRPC, DDS, UCX, in-memory for testing, etc.).
///
/// Usage:
/// 1. Create PubSubContext component
/// 2. Inject backend implementations via set_discovery/set_transport/set_serializer
/// 3. PubSubTransmitters and PubSubReceivers reference this context
/// 4. Context manages discovery announcements and message routing
///
/// Thread Safety: All public methods are thread-safe.
class PubSubContext : public NetworkContext {
 public:
  PubSubContext() = default;
  ~PubSubContext() override;

  //----------------------------------------------------------------------------
  // GXF Component Interface
  //----------------------------------------------------------------------------

  gxf_result_t registerInterface(Registrar* registrar) override;
  gxf_result_t initialize() override;
  gxf_result_t deinitialize() override;

  //----------------------------------------------------------------------------
  // NetworkContext Interface
  //----------------------------------------------------------------------------

  gxf_result_t init_context() override;
  Expected<void> addRoutes(const Entity& entity) override;
  Expected<void> removeRoutes(const Entity& entity) override;
  bool are_connections_ready() const override;

  //----------------------------------------------------------------------------
  // Backend Injection
  //----------------------------------------------------------------------------

  /// @brief Set the discovery backend
  /// @param discovery Discovery implementation (ownership shared)
  void set_discovery(PubSubDiscoveryPtr discovery);

  /// @brief Set the transport backend
  /// @param transport Transport implementation (ownership shared)
  void set_transport(PubSubTransportPtr transport);

  /// @brief Set the entity serializer
  /// @param serializer Serializer implementation (ownership shared)
  void set_serializer(PubSubEntitySerializerPtr serializer);

  /// @brief Set the allocator for GPU staging during serialization/deserialization
  ///
  /// When set, this allocator is forwarded to serialize() and deserialize()
  /// to provide pinned host memory for D2H/H2D GPU tensor staging.
  /// If null (default), serializers that require an allocator must manage
  /// their own internally.
  ///
  /// @pre Must be called before init_context(). The allocator is immutable
  ///      after initialization to avoid locking overhead on the hot path.
  ///
  /// @param allocator Allocator for staging buffers (e.g., pinned host memory).
  ///                  May be a null handle if no GPU staging is needed.
  void set_allocator(Handle<Allocator> allocator);

  /// @brief Get the discovery backend
  PubSubDiscoveryPtr discovery() const;

  /// @brief Get the transport backend
  PubSubTransportPtr transport() const;

  /// @brief Get the serializer
  PubSubEntitySerializerPtr serializer() const;

  /// @brief Set the clock used for timestamps (for programmatic configuration)
  ///
  /// When set, PubSubContext uses this clock for creation_timestamp_ns and
  /// source_timestamp_ns instead of the raw steady_clock fallback.
  /// Typical choices:
  ///  - RealtimeClock (default params): time since clock init (~app start)
  ///  - RealtimeClock (use_time_since_epoch=true): wall-clock epoch time
  ///  - ManualClock: deterministic timestamps for testing
  ///
  /// @pre Must be called before init_context() (same as other setters).
  void set_clock(Handle<Clock> clock);

  /// @brief Get the clock
  Handle<Clock> clock() const;

  /// @brief Get the allocator for GPU staging
  Handle<Allocator> allocator() const;

  //----------------------------------------------------------------------------
  // Publisher/Subscriber Registration
  //----------------------------------------------------------------------------

  /// @brief Register a local publisher
  ///
  /// Announces the publisher to the discovery service and sets up
  /// transport connections to matching subscribers.
  ///
  /// @param topic_name Topic to publish on
  /// @param type_name Human-readable type name
  /// @param type_hash Type hash for compatibility checking
  /// @param qos Quality of service settings
  /// @param transmitter Handle to the PubSubTransmitter component
  /// @param device_id Device ordinal for this endpoint's native buffer
  ///        capability (e.g. GPU device ID for CUDA IPC), or -1 (default)
  ///        to use the node-wide default.
  ///        Passed to discovery_->local_native_capability(device_id).
  /// @return Publisher GID, or error
  Expected<PublisherGid> register_publisher(const std::string& topic_name,
                                            const std::string& type_name,
                                            const std::string& type_hash, const QoSProfile& qos,
                                            Handle<Transmitter> transmitter,
                                            int device_id = -1);

  /// @brief Register a local subscriber
  ///
  /// Announces the subscriber to the discovery service and sets up
  /// transport connections from matching publishers.
  ///
  /// @param topic_name Topic to subscribe to
  /// @param type_name Human-readable type name
  /// @param type_hash Type hash for compatibility checking
  /// @param qos Quality of service settings
  /// @param receiver Handle to the PubSubReceiver component
  /// @param device_id Device ordinal for this endpoint's native buffer
  ///        capability (e.g. GPU device ID for CUDA IPC), or -1 (default)
  ///        to use the node-wide default.
  ///        Passed to discovery_->local_native_capability(device_id).
  /// @return Subscriber GID, or error
  Expected<SubscriberGid> register_subscriber(const std::string& topic_name,
                                              const std::string& type_name,
                                              const std::string& type_hash, const QoSProfile& qos,
                                              Handle<Receiver> receiver,
                                              int device_id = -1);

  /// @brief Unregister a local publisher
  /// @param gid Publisher GID to unregister
  /// @return Success or error
  Expected<void> unregister_publisher(const PublisherGid& gid);

  /// @brief Unregister a local subscriber
  /// @param gid Subscriber GID to unregister
  /// @return Success or error
  Expected<void> unregister_subscriber(const SubscriberGid& gid);

  //----------------------------------------------------------------------------
  // Message Sending
  //----------------------------------------------------------------------------

  /// @brief Send a message from a publisher to all matched subscribers
  ///
  /// Serializes the entity and sends it via the transport to all
  /// subscribers that matched with the given publisher.
  ///
  /// @param pub_gid Publisher GID
  /// @param entity Entity to send
  /// @return Success or error
  Expected<void> send_message(const PublisherGid& pub_gid, Entity entity);

  //----------------------------------------------------------------------------
  // Introspection
  //----------------------------------------------------------------------------

  /// @brief Get the GIDs of all locally registered publishers
  /// @return Vector of publisher GIDs (snapshot under lock)
  std::vector<Gid> registered_publisher_gids() const;

  /// @brief Get the GIDs of all locally registered subscribers
  /// @return Vector of subscriber GIDs (snapshot under lock)
  std::vector<Gid> registered_subscriber_gids() const;

  /// @brief Look up the transmitter Handle for a registered publisher
  /// @param gid Publisher GID
  /// @return Handle to the Transmitter, or GXF_ENTITY_NOT_FOUND
  Expected<Handle<Transmitter>> get_publisher_transmitter(const Gid& gid) const;

  /// @brief Look up the receiver Handle for a registered subscriber
  /// @param gid Subscriber GID
  /// @return Handle to the Receiver, or GXF_ENTITY_NOT_FOUND
  Expected<Handle<Receiver>> get_subscriber_receiver(const Gid& gid) const;

  /// @brief Query the combined capabilities of the transport and discovery backends
  ///
  /// Returns a snapshot of capabilities aggregated from the current transport
  /// and discovery backends. If a backend is not set, its fields use the
  /// struct defaults (most conservative: endpoint-addressed, centralized,
  /// no native matching).
  ///
  /// Useful for callers that need to adapt behavior based on what the
  /// backend supports (e.g., skip TopicRegistry for backends with native
  /// topic matching and QoS enforcement).
  ///
  /// @return BackendCapabilities struct with current backend capabilities
  BackendCapabilities backend_capabilities() const;

  /// @brief Get information about all known topics
  std::vector<TopicInfo> get_topics() const;

  /// @brief Get number of publishers on a topic
  size_t get_publisher_count(const std::string& topic) const;

  /// @brief Get number of subscribers on a topic
  size_t get_subscriber_count(const std::string& topic) const;

  //----------------------------------------------------------------------------
  // Configuration
  //----------------------------------------------------------------------------

  /// @brief Get the node name for this context
  /// @note "Node" in PubSubContext corresponds to a Holoscan fragment
  /// @note Returns by value for thread-safety
  std::string node_name() const;

  /// @brief Set the node name (for programmatic configuration)
  /// @note "Node" in PubSubContext corresponds to a Holoscan fragment
  void set_node_name(const std::string& name);

  /// @brief Get the native buffer policy
  NativeBufferPolicy native_buffer_policy() const;

  /// @brief Set the native buffer policy (for programmatic configuration)
  void set_native_buffer_policy(NativeBufferPolicy policy);

 private:
  // Discovery callback handlers
  void on_publisher_discovered(const PublisherInfo& info);
  void on_subscriber_discovered(const SubscriberInfo& info);
  void on_publisher_lost(const PublisherGid& gid);
  void on_subscriber_lost(const SubscriberGid& gid);

  // Transport callback handler
  void on_message_received(
      const Gid& source_gid,
      std::vector<uint8_t>&& payload,
      const MessageMetadata& metadata);

  // Connection event handlers
  void on_connection_established(const Gid& remote_gid);
  void on_connection_lost(const Gid& remote_gid);

  // Match event handlers (from local registry)
  void on_local_match(const PublisherGid& pub_gid, const SubscriberGid& sub_gid);
  void on_local_unmatch(const PublisherGid& pub_gid, const SubscriberGid& sub_gid);

  // Helper to get current timestamp
  uint64_t get_timestamp_ns() const;

  // Helper to get node_name when mutex_ is already held (avoids recursive lock)
  std::string node_name_locked() const;

  // Parameters (for GXF configuration)
  Parameter<std::string> node_name_param_;
  Parameter<Handle<Clock>> clock_param_;
  Parameter<std::string> native_buffer_policy_param_;

  // Cached/direct configuration (for programmatic use)
  std::string node_name_;
  Handle<Clock> clock_;
  NativeBufferPolicy native_buffer_policy_{NativeBufferPolicy::kPreferred};

  // Backend interfaces (injected)
  PubSubDiscoveryPtr discovery_;
  PubSubTransportPtr transport_;
  PubSubEntitySerializerPtr serializer_;

  // Allocator for GPU staging during serialize/deserialize (optional)
  Handle<Allocator> allocator_;

  // Local registry for tracking endpoints and computing matches
  TopicRegistry local_registry_;

  // Mappings from GID to local transmitters/receivers (Handle from GXF runtime)
  std::unordered_map<Gid, Handle<Transmitter>> publisher_transmitters_;
  std::unordered_map<Gid, Handle<Receiver>> subscriber_receivers_;

  // Sequence numbers per publisher
  std::unordered_map<Gid, uint64_t> publisher_sequence_numbers_;

  // Mutex for thread safety
  mutable std::mutex mutex_;

  // Initialization state
  bool initialized_ = false;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_PUBSUB_CONTEXT_HPP_
