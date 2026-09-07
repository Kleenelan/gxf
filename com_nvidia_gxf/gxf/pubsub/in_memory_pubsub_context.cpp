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
// InMemoryPubSubContext self-configures PubSubContext with the in-memory
// backends. In this build the backends are themselves stubs, so the context
// activates successfully but no pub/sub messages are ever delivered.

#include "gxf/pubsub/in_memory_pubsub_context.hpp"

#include <utility>

namespace nvidia {
namespace gxf {

gxf_result_t InMemoryPubSubContext::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= ExpectedOrCode(PubSubContext::registerInterface(registrar));
  // Parameter names match the holoscan InMemoryPubSubNetworkContext wrapper
  // (holoscan-sdk/src/pubsub/in_memory/network_contexts/gxf/
  //  in_memory_pubsub_network_context.cpp).
  result &= registrar->parameter(
      serializer_mode_, "serializer_mode", "Serializer Mode",
      "0 = kPassthrough (default, zero-copy UID transfer); "
      "1 = kFullSerialization (byte-level, requires a delegate serializer).",
      static_cast<int32_t>(0));
  result &= registrar->parameter(
      drop_pattern_, "drop_pattern", "Drop Pattern",
      "Cyclic drop pattern for deterministic fault injection "
      "(0 = deliver, non-zero = drop). Empty disables dropping.",
      std::vector<int32_t>{});
  result &= registrar->parameter(
      reorder_pattern_, "reorder_pattern", "Reorder Pattern",
      "Cyclic reorder pattern for deterministic fault injection "
      "(0 = deliver, non-zero = delay). Empty disables reordering.",
      std::vector<int32_t>{});
  return ToResultCode(result);
}

gxf_result_t InMemoryPubSubContext::initialize() {
  std::lock_guard<std::mutex> lock(init_mutex_);
  GXF_LOG_WARNING(
      "InMemoryPubSubContext (cid: %ld) is a compatibility stub in this build: "
      "pub/sub message delivery is not functional (no messages will be sent or received)",
      cid());
  // Create the concrete in-memory backends and inject them into the base
  // PubSubContext. The backends are stubs; wiring them keeps the typed
  // accessors (in_memory_discovery()/...) usable for introspection.
  if (!backends_.discovery) {
    backends_.discovery = std::make_shared<InMemoryDiscovery>();
  }
  if (!backends_.transport) {
    backends_.transport = std::make_shared<InMemoryTransport>();
  }
  if (!backends_.serializer) {
    backends_.serializer = std::make_shared<InMemorySerializer>();
  }
  if (serializer_mode_.try_get()) {
    backends_.serializer->set_mode(
        serializer_mode_.get() == 1 ? SerializerMode::kFullSerialization
                                    : SerializerMode::kPassthrough);
  }
  // Forward the fault-injection patterns (0 = deliver, non-zero = drop/delay).
  if (drop_pattern_.try_get()) {
    std::vector<bool> pattern;
    pattern.reserve(drop_pattern_.get().size());
    for (const auto flag : drop_pattern_.get()) {
      pattern.push_back(flag != 0);
    }
    backends_.transport->set_drop_pattern(std::move(pattern));
  }
  if (reorder_pattern_.try_get()) {
    std::vector<bool> pattern;
    pattern.reserve(reorder_pattern_.get().size());
    for (const auto flag : reorder_pattern_.get()) {
      pattern.push_back(flag != 0);
    }
    backends_.transport->set_reorder_pattern(std::move(pattern));
  }
  set_discovery(backends_.discovery);
  set_transport(backends_.transport);
  set_serializer(backends_.serializer);
  static_cast<void>(backends_.discovery->initialize());
  static_cast<void>(backends_.transport->initialize());
  return PubSubContext::initialize();
}

gxf_result_t InMemoryPubSubContext::deinitialize() {
  std::lock_guard<std::mutex> lock(init_mutex_);
  if (backends_.transport) {
    static_cast<void>(backends_.transport->shutdown());
  }
  if (backends_.discovery) {
    static_cast<void>(backends_.discovery->shutdown());
  }
  return PubSubContext::deinitialize();
}

}  // namespace gxf
}  // namespace nvidia
