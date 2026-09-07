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
#ifndef NVIDIA_GXF_PUBSUB_PUBSUB_NATIVE_BUFFER_HPP_
#define NVIDIA_GXF_PUBSUB_PUBSUB_NATIVE_BUFFER_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "gxf/core/gxf.h"  // for gxf_uid_t, kNullUid

namespace nvidia {
namespace gxf {

/// Lightweight payload carrying a serialized native buffer descriptor.
/// Produced by PubSubEntitySerializer::export_native_descriptor().
/// descriptor_bytes is opaque -- interpreted by the backend that serialized it.
struct NativeDescriptorPayload {
  std::vector<uint8_t> descriptor_bytes;  // protocol-specific serialized descriptor bytes
  gxf_uid_t            source_entity_uid{kNullUid};
  uint8_t              descriptor_format_version{0};  // set by serializer
  // Selected native protocol for this payload (for example "cuda_ipc").
  // On the receive path this may be empty when the transport does not surface
  // the protocol name separately from descriptor_bytes.
  std::string          protocol_name;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_PUBSUB_NATIVE_BUFFER_HPP_
