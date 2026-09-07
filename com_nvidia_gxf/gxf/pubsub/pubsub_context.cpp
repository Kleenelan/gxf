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
// PubSubContext coordinates discovery, transport and serialization for
// topic-based pub/sub messaging. In this build the pub/sub data plane is
// intentionally not functional: no messages are ever delivered. Lifecycle
// methods (initialize/deinitialize/init_context/addRoutes/removeRoutes)
// succeed so that graphs containing PubSub components can activate; all
// discovery/registration and message-sending operations fail with
// GXF_NOT_IMPLEMENTED and log an error.

#include "gxf/pubsub/pubsub_context.hpp"

#include <chrono>
#include <utility>

#include "gxf/pubsub/pubsub_receiver.hpp"
#include "gxf/pubsub/pubsub_transmitter.hpp"

namespace nvidia {
namespace gxf {

PubSubContext::~PubSubContext() = default;

gxf_result_t PubSubContext::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->parameter(
      node_name_param_, "node_name", "Node Name",
      "Name of this node/fragment for identification in discovery.", std::string{});
  result &= registrar->parameter(
      clock_param_, "clock", "Clock",
      "The clock used for pub/sub timestamps (creation_timestamp_ns, source_timestamp_ns).",
      Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
      native_buffer_policy_param_, "native_buffer_policy", "Native Buffer Policy",
      "Controls when the native-buffer (e.g. CUDA IPC) path is used: "
      "'disabled', 'preferred' (default) or 'required'.",
      std::string{"preferred"});
  return ToResultCode(result);
}

gxf_result_t PubSubContext::initialize() {
  GXF_LOG_WARNING(
      "PubSubContext (cid: %ld) is a compatibility stub in this build: pub/sub message "
      "delivery is not functional (no messages will be sent or received)", cid());
  std::lock_guard<std::mutex> lock(mutex_);
  node_name_ = node_name_param_.get();
  const auto& maybe_clock = clock_param_.try_get();
  if (maybe_clock) {
    clock_ = maybe_clock.value();
  }
  const std::string& policy_str = native_buffer_policy_param_.get();
  if (policy_str == "disabled") {
    native_buffer_policy_ = NativeBufferPolicy::kDisabled;
  } else if (policy_str == "required") {
    native_buffer_policy_ = NativeBufferPolicy::kRequired;
  } else {
    if (policy_str != "preferred") {
      GXF_LOG_WARNING(
          "PubSubContext: unrecognized native_buffer_policy '%s', defaulting to 'preferred'",
          policy_str.c_str());
    }
    native_buffer_policy_ = NativeBufferPolicy::kPreferred;
  }
  initialized_ = true;
  return GXF_SUCCESS;
}

gxf_result_t PubSubContext::deinitialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  return GXF_SUCCESS;
}

gxf_result_t PubSubContext::init_context() {
  GXF_LOG_WARNING(
      "PubSubContext::init_context (cid: %ld) is a compatibility stub: no discovery or "
      "transport backend is started", cid());
  return GXF_SUCCESS;
}

Expected<void> PubSubContext::addRoutes(const Entity& entity) {
  // Inject this context into any PubSub transmitters/receivers found in the
  // entity (same wiring pattern as UcxContext::addRoutes). Actual registration
  // is deferred to the component's initialize() and fails there (stub).
  const auto transmitters = entity.findAllHeap<PubSubTransmitter>();
  if (transmitters) {
    for (const auto maybe_tx : transmitters.value()) {
      if (maybe_tx) {
        maybe_tx.value()->set_context(this);
      }
    }
  }
  const auto receivers = entity.findAllHeap<PubSubReceiver>();
  if (receivers) {
    for (const auto maybe_rx : receivers.value()) {
      if (maybe_rx) {
        maybe_rx.value()->set_context(this);
      }
    }
  }
  return Success;
}

Expected<void> PubSubContext::removeRoutes(const Entity& entity) {
  (void)entity;
  return Success;
}

bool PubSubContext::are_connections_ready() const {
  // The stub never establishes connections; report ready so the scheduler is
  // not blocked waiting for pub/sub connections that will never exist.
  return true;
}

void PubSubContext::set_discovery(PubSubDiscoveryPtr discovery) {
  std::lock_guard<std::mutex> lock(mutex_);
  discovery_ = std::move(discovery);
}

void PubSubContext::set_transport(PubSubTransportPtr transport) {
  std::lock_guard<std::mutex> lock(mutex_);
  transport_ = std::move(transport);
}

void PubSubContext::set_serializer(PubSubEntitySerializerPtr serializer) {
  std::lock_guard<std::mutex> lock(mutex_);
  serializer_ = std::move(serializer);
}

void PubSubContext::set_allocator(Handle<Allocator> allocator) {
  std::lock_guard<std::mutex> lock(mutex_);
  allocator_ = allocator;
}

PubSubDiscoveryPtr PubSubContext::discovery() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return discovery_;
}

PubSubTransportPtr PubSubContext::transport() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return transport_;
}

PubSubEntitySerializerPtr PubSubContext::serializer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return serializer_;
}

void PubSubContext::set_clock(Handle<Clock> clock) {
  std::lock_guard<std::mutex> lock(mutex_);
  clock_ = clock;
}

Handle<Clock> PubSubContext::clock() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return clock_;
}

Handle<Allocator> PubSubContext::allocator() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return allocator_;
}

Expected<PublisherGid> PubSubContext::register_publisher(
    const std::string& topic_name, const std::string& type_name,
    const std::string& type_hash, const QoSProfile& qos,
    Handle<Transmitter> transmitter, int device_id) {
  (void)topic_name; (void)type_name; (void)type_hash; (void)qos;
  (void)transmitter; (void)device_id;
  GXF_LOG_ERROR(
      "PubSubContext::register_publisher is a compatibility stub in this build; "
      "pub/sub discovery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<SubscriberGid> PubSubContext::register_subscriber(
    const std::string& topic_name, const std::string& type_name,
    const std::string& type_hash, const QoSProfile& qos,
    Handle<Receiver> receiver, int device_id) {
  (void)topic_name; (void)type_name; (void)type_hash; (void)qos;
  (void)receiver; (void)device_id;
  GXF_LOG_ERROR(
      "PubSubContext::register_subscriber is a compatibility stub in this build; "
      "pub/sub discovery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<void> PubSubContext::unregister_publisher(const PublisherGid& gid) {
  (void)gid;
  // Safe no-op: publishers are never registered in this stub.
  return Success;
}

Expected<void> PubSubContext::unregister_subscriber(const SubscriberGid& gid) {
  (void)gid;
  // Safe no-op: subscribers are never registered in this stub.
  return Success;
}

Expected<void> PubSubContext::send_message(const PublisherGid& pub_gid, Entity entity) {
  (void)pub_gid; (void)entity;
  GXF_LOG_ERROR(
      "PubSubContext::send_message is a compatibility stub in this build; "
      "pub/sub message delivery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

std::vector<Gid> PubSubContext::registered_publisher_gids() const { return {}; }

std::vector<Gid> PubSubContext::registered_subscriber_gids() const { return {}; }

Expected<Handle<Transmitter>> PubSubContext::get_publisher_transmitter(const Gid& gid) const {
  (void)gid;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

Expected<Handle<Receiver>> PubSubContext::get_subscriber_receiver(const Gid& gid) const {
  (void)gid;
  return Unexpected{GXF_ENTITY_NOT_FOUND};
}

BackendCapabilities PubSubContext::backend_capabilities() const {
  std::lock_guard<std::mutex> lock(mutex_);
  BackendCapabilities caps;
  if (transport_) {
    caps.transport_model = transport_->transport_model();
    caps.native_topic_matching = transport_->native_topic_matching();
    caps.native_qos_enforcement = transport_->native_qos_enforcement();
    caps.supports_multicast = transport_->supports_multicast();
    caps.requires_explicit_connections = transport_->requires_explicit_connections();
    caps.supports_native_buffers = transport_->supports_native_buffers();
    caps.supports_mixed_local_remote_fanout = transport_->supports_mixed_local_remote_fanout();
  }
  if (discovery_) {
    caps.discovery_model = discovery_->discovery_model();
    caps.supports_wildcard_subscription = discovery_->supports_wildcard_subscription();
  }
  return caps;
}

std::vector<TopicInfo> PubSubContext::get_topics() const { return {}; }

size_t PubSubContext::get_publisher_count(const std::string& topic) const {
  (void)topic;
  return 0;
}

size_t PubSubContext::get_subscriber_count(const std::string& topic) const {
  (void)topic;
  return 0;
}

std::string PubSubContext::node_name() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return node_name_;
}

void PubSubContext::set_node_name(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  node_name_ = name;
}

NativeBufferPolicy PubSubContext::native_buffer_policy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return native_buffer_policy_;
}

void PubSubContext::set_native_buffer_policy(NativeBufferPolicy policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  native_buffer_policy_ = policy;
}

void PubSubContext::on_publisher_discovered(const PublisherInfo& info) {
  (void)info;
  // No-op: discovery callbacks are never wired up in this stub.
}

void PubSubContext::on_subscriber_discovered(const SubscriberInfo& info) {
  (void)info;
  // No-op: discovery callbacks are never wired up in this stub.
}

void PubSubContext::on_publisher_lost(const PublisherGid& gid) {
  (void)gid;
  // No-op.
}

void PubSubContext::on_subscriber_lost(const SubscriberGid& gid) {
  (void)gid;
  // No-op.
}

void PubSubContext::on_message_received(
    const Gid& source_gid, std::vector<uint8_t>&& payload,
    const MessageMetadata& metadata) {
  (void)source_gid; (void)payload; (void)metadata;
  // No-op: no transport ever delivers messages in this stub.
}

void PubSubContext::on_connection_established(const Gid& remote_gid) {
  (void)remote_gid;
  // No-op.
}

void PubSubContext::on_connection_lost(const Gid& remote_gid) {
  (void)remote_gid;
  // No-op.
}

void PubSubContext::on_local_match(const PublisherGid& pub_gid, const SubscriberGid& sub_gid) {
  (void)pub_gid; (void)sub_gid;
  // No-op: the local registry never computes matches in this stub.
}

void PubSubContext::on_local_unmatch(const PublisherGid& pub_gid, const SubscriberGid& sub_gid) {
  (void)pub_gid; (void)sub_gid;
  // No-op.
}

uint64_t PubSubContext::get_timestamp_ns() const {
  Handle<Clock> clock_handle;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    clock_handle = clock_;
  }
  if (clock_handle) {
    const int64_t timestamp = clock_handle->timestamp();
    if (timestamp >= 0) {
      return static_cast<uint64_t>(timestamp);
    }
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::string PubSubContext::node_name_locked() const { return node_name_; }

}  // namespace gxf
}  // namespace nvidia
