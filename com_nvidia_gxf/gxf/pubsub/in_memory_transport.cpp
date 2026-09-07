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
// In-memory transport backend. In this build message transport is not
// functional: lifecycle and connection bookkeeping succeed, send operations
// fail with GXF_NOT_IMPLEMENTED, no receive/connection callbacks are ever
// invoked and all queue metrics are zero.

#include "gxf/pubsub/in_memory_transport.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

Expected<void> InMemoryTransport::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  GXF_LOG_WARNING(
      "InMemoryTransport is a compatibility stub in this build: pub/sub message "
      "transport is not functional");
  if (source_gid_.is_null()) {
    source_gid_ = Gid::generate();
  }
  initialized_ = true;
  return Success;
}

Expected<void> InMemoryTransport::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  connections_.clear();
  return Success;
}

bool InMemoryTransport::is_initialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_;
}

Expected<void> InMemoryTransport::connect_to(const EndpointInfo& remote_endpoint) {
  // Benign bookkeeping only; no handshake is performed.
  std::lock_guard<std::mutex> lock(mutex_);
  connections_[remote_endpoint.gid] = remote_endpoint;
  return Success;
}

Expected<void> InMemoryTransport::disconnect_from(const Gid& remote_gid) {
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.erase(remote_gid);
  return Success;
}

bool InMemoryTransport::is_connected_to(const Gid& remote_gid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.find(remote_gid) != connections_.end();
}

Expected<void> InMemoryTransport::send(
    const Gid& destination_gid, const std::vector<uint8_t>& payload,
    const MessageMetadata& metadata) {
  (void)destination_gid; (void)payload; (void)metadata;
  GXF_LOG_ERROR(
      "InMemoryTransport::send is a compatibility stub in this build; "
      "pub/sub message delivery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<void> InMemoryTransport::send(
    const Gid& destination_gid, std::vector<uint8_t>&& payload,
    const MessageMetadata& metadata) {
  (void)destination_gid; (void)payload; (void)metadata;
  GXF_LOG_ERROR(
      "InMemoryTransport::send is a compatibility stub in this build; "
      "pub/sub message delivery is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

void InMemoryTransport::set_on_receive(ReceiveCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  default_receive_callback_ = std::move(callback);
}

void InMemoryTransport::set_on_connection_established(ConnectionEstablishedCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_connection_established_ = std::move(callback);
}

void InMemoryTransport::set_on_connection_lost(ConnectionLostCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_connection_lost_ = std::move(callback);
}

size_t InMemoryTransport::get_send_queue_size() const { return 0; }

size_t InMemoryTransport::get_receive_queue_size() const { return 0; }

size_t InMemoryTransport::get_connection_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size();
}

void InMemoryTransport::register_subscriber_endpoint(const Gid& subscriber_gid,
                                                     ReceiveCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriber_callbacks_[subscriber_gid] = std::move(callback);
}

void InMemoryTransport::unregister_subscriber_endpoint(const Gid& subscriber_gid) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriber_callbacks_.erase(subscriber_gid);
}

void InMemoryTransport::set_source_gid(const Gid& gid) {
  std::lock_guard<std::mutex> lock(mutex_);
  source_gid_ = gid;
}

Gid InMemoryTransport::get_source_gid() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return source_gid_;
}

void InMemoryTransport::set_drop_pattern(std::vector<bool> pattern) {
  std::lock_guard<std::mutex> lock(mutex_);
  drop_pattern_ = std::move(pattern);
  drop_index_ = 0;
}

void InMemoryTransport::set_reorder_pattern(std::vector<bool> pattern) {
  std::lock_guard<std::mutex> lock(mutex_);
  reorder_pattern_ = std::move(pattern);
  reorder_index_ = 0;
}

void InMemoryTransport::clear_fault_injection() {
  std::lock_guard<std::mutex> lock(mutex_);
  drop_pattern_.clear();
  reorder_pattern_.clear();
  drop_index_ = 0;
  reorder_index_ = 0;
}

void InMemoryTransport::flush_delayed_messages() {
  // No-op: no message is ever buffered in this stub.
}

size_t InMemoryTransport::get_dropped_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_count_;
}

size_t InMemoryTransport::get_delivered_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return delivered_count_;
}

size_t InMemoryTransport::get_delayed_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return delayed_messages_.size();
}

void InMemoryTransport::reset_statistics() {
  std::lock_guard<std::mutex> lock(mutex_);
  dropped_count_ = 0;
  delivered_count_ = 0;
}

void InMemoryTransport::deliver_message(
    const Gid& source_gid, const Gid& destination_gid,
    std::vector<uint8_t>&& payload, const MessageMetadata& metadata) {
  (void)source_gid; (void)destination_gid; (void)payload; (void)metadata;
  // No-op: send() always fails in this stub, so nothing is ever delivered.
}

}  // namespace gxf
}  // namespace nvidia
