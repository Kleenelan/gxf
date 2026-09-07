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
// PubSubTransmitter publishes entities to a topic via PubSubContext. In this
// build the pub/sub data plane is intentionally not functional: publish and
// queue-mutation operations fail and log an error, query operations report an
// empty queue, and no subscribers are ever matched.

#include "gxf/pubsub/pubsub_transmitter.hpp"

#include <utility>

#include "gxf/pubsub/pubsub_context.hpp"

namespace nvidia {
namespace gxf {

gxf_result_t PubSubTransmitter::registerInterface(Registrar* registrar) {
  Expected<void> result;
  // Parameter names match the holoscan PubSubTransmitter wrapper
  // (holoscan-sdk/src/core/resources/gxf/pubsub_transmitter.cpp).
  result &= registrar->parameter(topic_name_param_, "topic_name", "Topic Name",
                                 "Topic to publish to");
  result &= registrar->parameter(capacity_, "capacity", "Capacity", "Queue capacity", 1UL);
  result &= registrar->parameter(policy_, "policy", "Policy", "0: pop, 1: reject, 2: fault", 2UL);
  return ToResultCode(result);
}

gxf_result_t PubSubTransmitter::initialize() {
  GXF_LOG_WARNING(
      "PubSubTransmitter '%s' (cid: %ld) is a compatibility stub in this build: "
      "publishing to topic is not functional (messages are never delivered)",
      name(), cid());
  if (topic_name_param_.try_get()) {
    topic_name_ = topic_name_param_.get();
  }
  registered_ = false;
  // Attempt deferred registration with the injected context. This fails in the
  // stub (PubSubContext::register_publisher returns GXF_NOT_IMPLEMENTED); the
  // failure is non-fatal so that graph activation can still succeed.
  if (network_context_ != nullptr) {
    const gxf_result_t result = register_with_context(network_context_);
    if (result != GXF_SUCCESS) {
      GXF_LOG_WARNING(
          "PubSubTransmitter '%s' (cid: %ld): registration with PubSubContext failed "
          "(error %d); continuing unregistered (stub)",
          name(), cid(), static_cast<int>(result));
    }
  }
  return GXF_SUCCESS;
}

gxf_result_t PubSubTransmitter::deinitialize() {
  if (registered_ && network_context_ != nullptr) {
    static_cast<void>(network_context_->unregister_publisher(gid_));
  }
  registered_ = false;
  network_context_ = nullptr;
  queue_.reset();
  return GXF_SUCCESS;
}

gxf_result_t PubSubTransmitter::pop_abi(gxf_uid_t* uid) {
  if (uid == nullptr) { return GXF_ARGUMENT_NULL; }
  // Empty-queue semantics: nothing was ever staged.
  return GXF_QUERY_NOT_FOUND;
}

gxf_result_t PubSubTransmitter::push_abi(gxf_uid_t other) {
  (void)other;
  GXF_LOG_ERROR(
      "PubSubTransmitter::push_abi is a compatibility stub in this build; "
      "entities are not staged for publication (GXF_FAILURE)");
  return GXF_FAILURE;
}

gxf_result_t PubSubTransmitter::peek_abi(gxf_uid_t* uid, int32_t index) {
  (void)index;
  if (uid == nullptr) { return GXF_ARGUMENT_NULL; }
  // Empty-queue semantics.
  return GXF_QUERY_NOT_FOUND;
}

size_t PubSubTransmitter::capacity_abi() {
  const auto capacity = capacity_.try_get();
  return capacity ? capacity.value() : 0;
}

size_t PubSubTransmitter::size_abi() {
  // Empty-queue semantics.
  return 0;
}

gxf_result_t PubSubTransmitter::publish_abi(gxf_uid_t uid) {
  (void)uid;
  GXF_LOG_ERROR(
      "PubSubTransmitter::publish_abi is a compatibility stub in this build; "
      "pub/sub message delivery is not functional (GXF_FAILURE)");
  return GXF_FAILURE;
}

size_t PubSubTransmitter::back_size_abi() {
  // Empty-queue semantics.
  return 0;
}

gxf_result_t PubSubTransmitter::sync_abi() {
  // Empty-queue semantics: nothing to move from backstage to main stage.
  return GXF_SUCCESS;
}

const std::string& PubSubTransmitter::topic_name() const { return topic_name_; }

const QoSProfile& PubSubTransmitter::qos() const { return qos_; }

size_t PubSubTransmitter::matched_subscriber_count() const {
  std::lock_guard<std::mutex> lock(matched_mutex_);
  return matched_subscribers_.size();
}

bool PubSubTransmitter::has_matched_subscribers() const {
  return matched_subscriber_count() > 0;
}

std::vector<SubscriberGid> PubSubTransmitter::matched_subscriber_gids() const {
  std::lock_guard<std::mutex> lock(matched_mutex_);
  return {matched_subscribers_.begin(), matched_subscribers_.end()};
}

gxf_result_t PubSubTransmitter::register_with_context(PubSubContext* ctx) {
  if (ctx == nullptr) {
    GXF_LOG_ERROR("PubSubTransmitter::register_with_context: null context");
    return GXF_ARGUMENT_NULL;
  }
  network_context_ = ctx;
  auto result = ctx->register_publisher(topic_name_, /*type_name=*/"", /*type_hash=*/"", qos_,
                                        Handle<Transmitter>(context(), cid()));
  if (!result) {
    registered_ = false;
    return result.error();
  }
  gid_ = result.value();
  registered_ = true;
  return GXF_SUCCESS;
}

void PubSubTransmitter::on_subscriber_matched(const SubscriberInfo& info) {
  (void)info;
  // No-op: the stub context never reports matches.
}

void PubSubTransmitter::on_subscriber_unmatched(const SubscriberGid& gid) {
  (void)gid;
  // No-op.
}

void PubSubTransmitter::set_qos(const QoSProfile& qos) { qos_ = qos; }

}  // namespace gxf
}  // namespace nvidia
