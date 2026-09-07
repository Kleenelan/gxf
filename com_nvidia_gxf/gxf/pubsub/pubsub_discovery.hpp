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
#ifndef NVIDIA_GXF_PUBSUB_IDISCOVERY_HPP_
#define NVIDIA_GXF_PUBSUB_IDISCOVERY_HPP_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gxf/core/expected.hpp"
#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/gid.hpp"

// Forward declaration — avoids pulling in topic_registry.hpp
// (only needed as a pointer parameter for set_topic_registry)
namespace nvidia { namespace gxf { class TopicRegistry; } }

namespace nvidia {
namespace gxf {

/// @brief Discovery communication model
///
/// Describes how a discovery backend announces and discovers endpoints.
/// PubSubContext can use this to optimize behavior (e.g., skip rollback
/// on announce failure for passive backends).
enum class DiscoveryModel : int32_t {
  /// Centralized discovery via an external service (e.g., gRPC discovery server).
  /// announce_publisher()/announce_subscriber() make network calls (RPCs) to a
  /// central server. The server must be running for discovery to function.
  kCentralized = 0,

  /// Decentralized active discovery (e.g., Zenoh liveliness tokens).
  /// announce_publisher()/announce_subscriber() perform lightweight network
  /// operations (e.g., declare liveliness tokens, publish metadata on key
  /// expressions) but do not require a central server. Discovery is peer-to-peer.
  kDecentralizedActive = 1,

  /// Decentralized passive discovery (e.g., DDS SPDP/SEDP).
  /// announce_publisher()/announce_subscriber() are purely local bookkeeping.
  /// Actual network discovery happens implicitly when transport endpoints
  /// (DataWriters/DataReaders) are created via create_publisher_endpoint() /
  /// create_subscriber_endpoint().
  kDecentralizedPassive = 2,
};

/// @brief Abstract discovery interface for pub/sub endpoint discovery
///
/// Backends implement this interface to provide discovery functionality.
/// Discovery is responsible for:
/// - Announcing local publishers/subscribers to the network
/// - Learning about remote publishers/subscribers
/// - Notifying the application when endpoints appear or disappear
///
/// Possible Concrete Implementations:
/// - InMemoryDiscovery: Single-process, deterministic (for testing)
/// - GrpcDiscoveryClient: Centralized discovery via gRPC service
/// - DdsDiscoveryWrapper: Wraps DDS SPDP/SEDP discovery protocols
///
/// Discovery Models (see DiscoveryModel enum):
/// - Centralized (gRPC): announce() sends RPCs to a central server
/// - Decentralized Active (Zenoh): announce() performs lightweight network ops
/// - Decentralized Passive (DDS): announce() is local bookkeeping only;
///   actual discovery is triggered when transport endpoints are created
///
/// Thread Safety: Implementations must be thread-safe. Callbacks may be
/// invoked from any thread, so callback implementations should be thread-safe.
///
/// Lifecycle:
/// 1. Create instance
/// 2. Set callbacks (before initialize)
/// 3. Call initialize()
/// 4. Use announce/remove/query methods
/// 5. Call shutdown()
///
/// @note Callbacks are typically invoked asynchronously when remote endpoints
/// are discovered or lost. The exact threading model depends on the backend.
class PubSubDiscovery {
 public:
  virtual ~PubSubDiscovery() = default;

  //----------------------------------------------------------------------------
  // Lifecycle
  //----------------------------------------------------------------------------

  /// @brief Initialize the discovery backend
  ///
  /// Must be called before any other methods (except setters).
  /// Connects to discovery service, starts background threads, etc.
  ///
  /// @return Success or error code
  virtual Expected<void> initialize() = 0;

  /// @brief Shutdown the discovery backend
  ///
  /// Disconnects from discovery service, releases resources, stops
  /// background threads.
  ///
  /// **Shutdown Contract** (implementors must guarantee):
  /// 1. After shutdown() returns, **no more** PublisherDiscoveredCallback,
  ///    SubscriberDiscoveredCallback, PublisherLostCallback, or
  ///    SubscriberLostCallback invocations will occur.
  /// 2. Any callbacks that are **currently executing** at the time
  ///    shutdown() is called will **complete** before shutdown() returns.
  ///    (i.e., shutdown blocks until in-flight callbacks finish.)
  /// 3. Calling announce_publisher() / announce_subscriber() after
  ///    shutdown() returns an error (GXF_CONTRACT_INVALID_SEQUENCE or
  ///    backend-specific code). query_*() methods may also return errors.
  /// 4. Safe to call multiple times (idempotent). A second call is a
  ///    no-op and returns Success.
  ///
  /// **Shutdown Ordering** (enforced by PubSubContext::deinitialize()):
  ///   a. Callbacks are cleared (set to nullptr) on both discovery and transport.
  ///   b. Discovery: remove_publisher/remove_subscriber for all endpoints,
  ///      then discovery_->shutdown().
  ///   c. Transport: remove endpoints, then transport_->shutdown().
  /// Discovery shutdown always happens **before** transport shutdown. This
  /// ensures discovery can notify the network of endpoint removal while
  /// the transport is still capable of sending messages.
  ///
  /// @return Success or error code
  virtual Expected<void> shutdown() = 0;

  /// @brief Check if the discovery backend is initialized
  /// @return true if initialized and ready for use
  virtual bool is_initialized() const = 0;

  //----------------------------------------------------------------------------
  // Discovery Model
  //----------------------------------------------------------------------------

  /// @brief Query the discovery communication model
  ///
  /// Returns the model that describes how this backend performs discovery.
  /// PubSubContext may use this to:
  /// - Skip rollback on announce failure for passive backends (announce is local-only)
  /// - Log a warning if a centralized backend fails to reach the server
  /// - Adjust shutdown ordering (passive backends don't need network teardown for
  ///   discovery announcements; the transport handles that)
  ///
  /// Default is kDecentralizedPassive (DDS-like), matching the first implemented
  /// backend. Backends with different models should override.
  ///
  /// @return The discovery model for this backend
  virtual DiscoveryModel discovery_model() const { return DiscoveryModel::kDecentralizedPassive; }

  //----------------------------------------------------------------------------
  // Backend Capabilities (Optional)
  //----------------------------------------------------------------------------

  /// @brief Whether the discovery backend supports wildcard subscriptions
  ///
  /// When true, subscribers can use pattern-based topic matching (e.g.,
  /// Zenoh key expression wildcards like "sensor/*/data"). When false,
  /// subscribers must use exact topic names.
  ///
  /// - Zenoh: true — key expression wildcards (e.g., "sensor/**", "*/data").
  /// - DDS: false — DDS topic matching is exact (content filtering uses a
  ///   separate mechanism).
  /// - UCX: false (default) — no native pattern matching.
  virtual bool supports_wildcard_subscription() const { return false; }

  /// @brief Get the native buffer capability for a specific GPU device
  ///
  /// Backends that advertise native-buffer capability (for example via DDS
  /// participant user data) should override this so PubSubContext can attach
  /// the correct capability to local EndpointInfo records used for
  /// match/routing decisions. Backends that do not support native buffers
  /// may keep the default empty capability.
  ///
  /// @param device_id Device ordinal to build the capability for (e.g. GPU
  ///        device ID for CUDA IPC), or -1 to use the node-wide default.
  /// @return NativeBufferCapability for the given device (empty if unsupported)
  virtual NativeBufferCapability local_native_capability(int device_id = -1) const {
    (void)device_id;
    return NativeBufferCapability{};
  }

  //----------------------------------------------------------------------------
  // Shared TopicRegistry (Optional)
  //----------------------------------------------------------------------------

  /// @brief Share a TopicRegistry with this discovery backend
  ///
  /// Called by PubSubContext during init_context() to provide the context's
  /// TopicRegistry to the discovery backend. This eliminates duplicate
  /// endpoint tracking — the discovery backend can use the shared registry
  /// for query methods (query_publishers, query_subscribers, get_all_topics)
  /// instead of maintaining its own local_publishers_ / local_subscribers_
  /// maps.
  ///
  /// The pointer is valid from the time of this call until either:
  /// - set_topic_registry(nullptr) is called (during PubSubContext::deinitialize)
  /// - The discovery backend is shut down
  ///
  /// **Backend usage**:
  /// - **DDS (kDecentralizedPassive)**: Can delegate local endpoint queries
  ///   to the shared registry, eliminating redundant local_publishers_ /
  ///   local_subscribers_ maps. Remote endpoints (discovered via SEDP) are
  ///   still tracked independently by the discovery backend.
  /// - **Zenoh (kDecentralizedActive)**: Same benefit as DDS.
  ///
  /// The default implementation stores the pointer in topic_registry_
  /// (accessible via the protected getter). All backends automatically
  /// receive the shared registry without needing to override this method.
  ///
  /// @param registry Pointer to the context's TopicRegistry, or nullptr to
  ///                 clear the reference. Not owned by the discovery backend.
  virtual void set_topic_registry(TopicRegistry* registry) {
    topic_registry_ = registry;
  }

  //----------------------------------------------------------------------------
  // Registration (announce local endpoints to the network)
  //----------------------------------------------------------------------------

  /// @brief Announce a local publisher to the discovery network
  ///
  /// After this call, remote subscribers may discover this publisher.
  /// The announcement is persistent until remove_publisher is called
  /// or shutdown occurs.
  ///
  /// @param info Publisher information to announce
  /// @return Success or error code
  virtual Expected<void> announce_publisher(const PublisherInfo& info) = 0;

  /// @brief Announce a local subscriber to the discovery network
  ///
  /// After this call, remote publishers may discover this subscriber.
  /// The announcement is persistent until remove_subscriber is called
  /// or shutdown occurs.
  ///
  /// @param info Subscriber information to announce
  /// @return Success or error code
  virtual Expected<void> announce_subscriber(const SubscriberInfo& info) = 0;

  //----------------------------------------------------------------------------
  // Deregistration (remove local endpoints from the network)
  //----------------------------------------------------------------------------

  /// @brief Remove a previously announced publisher
  ///
  /// After this call, the publisher will no longer be visible to remote
  /// subscribers. Safe to call even if the publisher was never announced
  /// (no-op in that case).
  ///
  /// @param gid GID of the publisher to remove
  /// @return Success or error code
  virtual Expected<void> remove_publisher(const PublisherGid& gid) = 0;

  /// @brief Remove a previously announced subscriber
  ///
  /// After this call, the subscriber will no longer be visible to remote
  /// publishers. Safe to call even if the subscriber was never announced
  /// (no-op in that case).
  ///
  /// @param gid GID of the subscriber to remove
  /// @return Success or error code
  virtual Expected<void> remove_subscriber(const SubscriberGid& gid) = 0;

  //----------------------------------------------------------------------------
  // Query (get current state of remote endpoints)
  //----------------------------------------------------------------------------

  /// @brief Query all known publishers on a specific topic
  ///
  /// Returns a snapshot of currently known publishers. For continuous
  /// updates, use the discovery callbacks instead.
  ///
  /// @param topic_name Topic to query (empty string for all topics)
  /// @return List of publisher info, or error code
  virtual Expected<std::vector<PublisherInfo>> query_publishers(
      const std::string& topic_name) = 0;

  /// @brief Query all known subscribers on a specific topic
  ///
  /// Returns a snapshot of currently known subscribers. For continuous
  /// updates, use the discovery callbacks instead.
  ///
  /// @param topic_name Topic to query (empty string for all topics)
  /// @return List of subscriber info, or error code
  virtual Expected<std::vector<SubscriberInfo>> query_subscribers(
      const std::string& topic_name) = 0;

  /// @brief Get all known topic names
  ///
  /// Returns a list of all topics that have at least one publisher
  /// or subscriber.
  ///
  /// @return List of topic names, or error code
  virtual Expected<std::vector<std::string>> get_all_topics() = 0;

  //----------------------------------------------------------------------------
  // Discovery Callbacks (asynchronous notifications)
  //----------------------------------------------------------------------------

  /// @brief Callback invoked when a new publisher is discovered
  ///
  /// @note May be invoked from any thread (e.g., a discovery network thread,
  /// a DDS SEDP listener, or a Zenoh liveliness thread). Callback
  /// implementations should minimize blocking and must not call back into
  /// discovery methods that could deadlock.
  ///
  /// After shutdown() returns, this callback will not be invoked.
  using PublisherDiscoveredCallback = std::function<void(const PublisherInfo&)>;

  /// @brief Callback invoked when a new subscriber is discovered
  ///
  /// @note Same threading and reentrancy considerations as
  /// PublisherDiscoveredCallback. After shutdown() returns, this
  /// callback will not be invoked.
  using SubscriberDiscoveredCallback = std::function<void(const SubscriberInfo&)>;

  /// @brief Callback invoked when a publisher is no longer available
  ///
  /// @note After shutdown() returns, this callback will not be invoked.
  using PublisherLostCallback = std::function<void(const PublisherGid&)>;

  /// @brief Callback invoked when a subscriber is no longer available
  ///
  /// @note After shutdown() returns, this callback will not be invoked.
  using SubscriberLostCallback = std::function<void(const SubscriberGid&)>;

  /// @brief Set callback for publisher discovery events
  ///
  /// Called when a new remote publisher is discovered. Should be set
  /// before initialize() to avoid missing early discoveries.
  ///
  /// @param callback Function to call when a publisher is discovered
  virtual void set_on_publisher_discovered(PublisherDiscoveredCallback callback) = 0;

  /// @brief Set callback for subscriber discovery events
  ///
  /// Called when a new remote subscriber is discovered. Should be set
  /// before initialize() to avoid missing early discoveries.
  ///
  /// @param callback Function to call when a subscriber is discovered
  virtual void set_on_subscriber_discovered(SubscriberDiscoveredCallback callback) = 0;

  /// @brief Set callback for publisher lost events
  ///
  /// Called when a previously discovered publisher is no longer available
  /// (either graceful removal or detected failure).
  ///
  /// @param callback Function to call when a publisher is lost
  virtual void set_on_publisher_lost(PublisherLostCallback callback) = 0;

  /// @brief Set callback for subscriber lost events
  ///
  /// Called when a previously discovered subscriber is no longer available
  /// (either graceful removal or detected failure).
  ///
  /// @param callback Function to call when a subscriber is lost
  virtual void set_on_subscriber_lost(SubscriberLostCallback callback) = 0;

 protected:
  /// @brief Get the shared TopicRegistry (set by PubSubContext)
  ///
  /// Returns nullptr if no registry has been shared (e.g., before
  /// init_context() or after deinitialize()).
  TopicRegistry* topic_registry() const { return topic_registry_; }

 private:
  TopicRegistry* topic_registry_ = nullptr;
};

/// @brief Shared pointer type for PubSubDiscovery
using PubSubDiscoveryPtr = std::shared_ptr<PubSubDiscovery>;

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_IDISCOVERY_HPP_
