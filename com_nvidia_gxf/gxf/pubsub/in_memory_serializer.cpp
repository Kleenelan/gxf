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
// In-memory entity serializer. In this build serialization is not functional:
// serialize/deserialize fail with GXF_NOT_IMPLEMENTED; mode/delegate setters
// and diagnostics behave as declared.

#include "gxf/pubsub/in_memory_serializer.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

void InMemorySerializer::set_mode(SerializerMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  mode_ = mode;
}

void InMemorySerializer::set_delegate(PubSubEntitySerializer* delegate) {
  std::lock_guard<std::mutex> lock(mutex_);
  delegate_ = delegate;
}

SerializerMode InMemorySerializer::mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

Expected<std::vector<uint8_t>> InMemorySerializer::serialize(
    Entity entity, Handle<Allocator> allocator) {
  (void)entity; (void)allocator;
  GXF_LOG_ERROR(
      "InMemorySerializer::serialize is a compatibility stub in this build; "
      "pub/sub serialization is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<Entity> InMemorySerializer::deserialize(
    const std::vector<uint8_t>& data, gxf_context_t context,
    Handle<Allocator> allocator) {
  (void)data; (void)context; (void)allocator;
  GXF_LOG_ERROR(
      "InMemorySerializer::deserialize is a compatibility stub in this build; "
      "pub/sub serialization is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

size_t InMemorySerializer::estimate_size(Entity entity) {
  (void)entity;
  return 0;
}

bool InMemorySerializer::supports_zero_copy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == SerializerMode::kPassthrough) { return true; }
  return delegate_ != nullptr ? delegate_->supports_zero_copy() : false;
}

bool InMemorySerializer::supports_gpu_tensors() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == SerializerMode::kPassthrough) { return false; }
  return delegate_ != nullptr ? delegate_->supports_gpu_tensors() : false;
}

bool InMemorySerializer::supports_direct_buffer_serialization() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == SerializerMode::kPassthrough) { return false; }
  return delegate_ != nullptr ? delegate_->supports_direct_buffer_serialization() : false;
}

size_t InMemorySerializer::serialize_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return serialize_count_;
}

size_t InMemorySerializer::deserialize_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return deserialize_count_;
}

void InMemorySerializer::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  serialize_count_ = 0;
  deserialize_count_ = 0;
}

}  // namespace gxf
}  // namespace nvidia
