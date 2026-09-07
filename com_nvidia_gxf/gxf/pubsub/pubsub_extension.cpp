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

// GXF 5.7.1 pub/sub compatibility stub extension (built on the GXF 4.1 tree).
//
// Registers the GXF 5.7.1 pub/sub component types so that applications built
// against the 5.7.1 API (e.g. holoscan-sdk) can create them. The components
// are stubs: graphs containing them activate, but no pub/sub messages are
// ever delivered.
//
// Note: InMemoryDiscovery, InMemoryTransport and InMemorySerializer are plain
// backend classes (not GXF Components) and therefore are not registered here.

#include "gxf/pubsub/in_memory_pubsub_context.hpp"
#include "gxf/pubsub/pubsub_context.hpp"
#include "gxf/pubsub/pubsub_receiver.hpp"
#include "gxf/pubsub/pubsub_transmitter.hpp"
#include "gxf/std/extension_factory_helper.hpp"

extern "C" {

GXF_EXT_FACTORY_BEGIN()

GXF_EXT_FACTORY_SET_INFO(0x718902da96568509, 0x3cc29a5647f6f8e3, "PubSubExtension",
                         "GXF 5.7.1 pub/sub API compatibility stub extension "
                         "(components activate but do not deliver messages)",
                         "NVIDIA", "1.0.0", "LICENSE");

GXF_EXT_FACTORY_ADD(0x44bcc958b980468c, 0xcd6f1661c723a15e,
                    nvidia::gxf::PubSubContext, nvidia::gxf::NetworkContext,
                    "Backend-agnostic pub/sub context wiring discovery, transport and "
                    "serialization together (compatibility stub: no message delivery)");

GXF_EXT_FACTORY_ADD(0xd015b4065ca8bcc8, 0xf0e3f27eb2822452,
                    nvidia::gxf::PubSubTransmitter, nvidia::gxf::Transmitter,
                    "Backend-agnostic publisher component for topic-based pub/sub "
                    "(compatibility stub: publish fails, no message delivery)");

GXF_EXT_FACTORY_ADD(0xcdfdc1c30bc7ad7c, 0xf55fbf2ae7e0e1eb,
                    nvidia::gxf::PubSubReceiver, nvidia::gxf::Receiver,
                    "Backend-agnostic subscriber component for topic-based pub/sub "
                    "(compatibility stub: queue always empty, no message delivery)");

GXF_EXT_FACTORY_ADD(0xfd8589b8a9ebdc4a, 0x54c57404ddc08f50,
                    nvidia::gxf::InMemoryPubSubContext, nvidia::gxf::PubSubContext,
                    "PubSubContext that self-configures with in-memory backends "
                    "(compatibility stub: no message delivery)");

GXF_EXT_FACTORY_END()

}  // extern "C"
