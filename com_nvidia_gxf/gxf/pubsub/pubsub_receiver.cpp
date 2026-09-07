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
// PubSubReceiver subscribes to a topic via PubSubContext. In this build the
// pub/sub data plane is intentionally not functional: the receiver queue is
// always empty (pop/peek/receive report GXF_QUERY_NOT_FOUND, sizes are 0,
// sync/wait succeed immediately) and no publishers are ever matched.

#include "gxf/pubsub/pubsub_receiver.hpp"

#include <utility>

#include "gxf/pubsub/pubsub_context.hpp"

namespace nvidia {
namespace gxf {

gxf_result_t PubSubReceiver::registerInterface(Registrar* registrar) {
  Expected<void> result;
  // Parameter names match the holoscan PubSubReceiver wrapper
  // (holoscan-sdk/src/core/resources/gxf/pubsub_receiver.cpp).
  result &= registrar->parameter(topic_name_param_, "topic_name", "Topic Name",
                                 "Topic to subscribe to");
  result &= registrar->parameter(capacity_, "capacity", "Capacity", "Queue capacity", 1UL);
  result &= registrar->parameter(policy_, "policy", "Policy", "0: pop, 1: reject, 2: fault", 2UL);
  return ToResultCode(result);
}

gxf_result_t PubSubReceiver::initialize() {
  GXF_LOG_WARNING(
      "PubSubReceiver '%s' (cid: %ld) is a compatibility stub in this build: "
      "subscribing to a topic is not functional (no messages are ever received)",
      name(), cid());
  if (topic_name_param_.try_get()) {
    topic_name_ = topic_name_param_.get();
  }
  registered_ = false;
  // Attempt deferred registration with the injected context. This fails in the
  // stub (PubSubContext::register_subscriber returns GXF_NOT_IMPLEMENTED); the
  // failure is non-fatal so that graph activation can still succeed.
  if (network_context_ != nullptr) {
    const gxf_result_t result = register_with_context(network_context_);
    if (result != GXF_SUCCESS) {
      GXF_LOG_WARNING(
          "PubSubReceiver '%s' (cid: %ld): registration with PubSubContext failed "
          "(error %d); continuing unregistered (stub)",
          name(), cid(), static_cast<int>(result));
    }
  }
  return GXF_SUCCESS;
}

gxf_result_t PubSubReceiver::deinitialize() {
  if (registered_ && network_context_ != nullptr) {
    static_cast<void>(network_context_->unregister_subscriber(gid_));
  }
  registered_ = false;
  network_context_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(wait_mutex_);
    queue_.reset();
  }
  wait_cv_.notify_all();
  return GXF_SUCCESS;
}

gxf_result_t PubSubReceiver::pop_abi(gxf_uid_t* uid) {
  if (uid == nullptr) { return GXF_ARGUMENT_NULL; }
  // Empty-queue semantics: nothing was ever received.
  return GXF_QUERY_NOT_FOUND;
}

gxf_result_t PubSubReceiver::push_abi(gxf_uid_t other) {
  (void)other;
  GXF_LOG_ERROR(
      "PubSubReceiver::push_abi is a compatibility stub in this build; "
      "received entities are not staged (GXF_FAILURE)");
  return GXF_FAILURE;
}

gxf_result_t PubSubReceiver::peek_abi(gxf_uid_t* uid, int32_t index) {
  (void)index;
  if (uid == nullptr) { return GXF_ARGUMENT_NULL; }
  // Empty-queue semantics.
  return GXF_QUERY_NOT_FOUND;
}

size_t PubSubReceiver::capacity_abi() {
  const auto capacity = capacity_.try_get();
  return capacity ? capacity.value() : 0;
}

size_t PubSubReceiver::size_abi() {
  // Empty-queue semantics.
  return 0;
}

gxf_result_t PubSubReceiver::receive_abi(gxf_uid_t* uid) {
  // Same as pop: the queue is always empty.
  return pop_abi(uid);
}

size_t PubSubReceiver::back_size_abi() {
  // Empty-queue semantics.
  return 0;
}

Receiver::StageSizeSnapshot PubSubReceiver::stage_sizes_abi() {
  // Empty-queue semantics: coherent snapshot of two empty stages.
  return StageSizeSnapshot{0, 0};
}

gxf_result_t PubSubReceiver::peek_back_abi(gxf_uid_t* uid, int32_t index) {
  (void)index;
  if (uid == nullptr) { return GXF_ARGUMENT_NULL; }
  // Empty-queue semantics.
  return GXF_QUERY_NOT_FOUND;
}

gxf_result_t PubSubReceiver::sync_abi() {
  // Empty-queue semantics: nothing to move from backstage to main stage.
  return GXF_SUCCESS;
}

gxf_result_t PubSubReceiver::wait_abi() {
  // Do not block: no message will ever arrive in this stub. Returning success
  // immediately mirrors the GXF 4.1 default Receiver::wait_abi().
  return GXF_SUCCESS;
}

const std::string& PubSubReceiver::topic_name() const { return topic_name_; }

const QoSProfile& PubSubReceiver::qos() const { return qos_; }

size_t PubSubReceiver::matched_publisher_count() const {
  std::lock_guard<std::mutex> lock(matched_mutex_);
  return matched_publishers_.size();
}

bool PubSubReceiver::has_matched_publishers() const {
  return matched_publisher_count() > 0;
}

std::vector<PublisherGid> PubSubReceiver::matched_publisher_gids() const {
  std::lock_guard<std::mutex> lock(matched_mutex_);
  return {matched_publishers_.begin(), matched_publishers_.end()};
}

gxf_result_t PubSubReceiver::register_with_context(PubSubContext* ctx) {
  if (ctx == nullptr) {
    GXF_LOG_ERROR("PubSubReceiver::register_with_context: null context");
    return GXF_ARGUMENT_NULL;
  }
  network_context_ = ctx;
  auto result = ctx->register_subscriber(topic_name_, /*type_name=*/"", /*type_hash=*/"", qos_,
                                         Handle<Receiver>(context(), cid()));
  if (!result) {
    registered_ = false;
    return result.error();
  }
  gid_ = result.value();
  registered_ = true;
  return GXF_SUCCESS;
}

void PubSubReceiver::on_publisher_matched(const PublisherInfo& info) {
  (void)info;
  // No-op: the stub context never reports matches.
}

void PubSubReceiver::on_publisher_unmatched(const PublisherGid& gid) {
  (void)gid;
  // No-op.
}

void PubSubReceiver::push_received_entity(Entity entity) {
  (void)entity;
  // No-op: the stub context never delivers entities; dropped by design.
}

void PubSubReceiver::set_qos(const QoSProfile& qos) { qos_ = qos; }

}  // namespace gxf
}  // namespace nvidia
