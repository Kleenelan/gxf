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
#ifndef NVIDIA_GXF_PUBSUB_IN_MEMORY_SERIALIZER_HPP_
#define NVIDIA_GXF_PUBSUB_IN_MEMORY_SERIALIZER_HPP_

#include <cstdint>
#include <mutex>
#include <vector>

#include "gxf/pubsub/pubsub_entity_serializer.hpp"

namespace nvidia {
namespace gxf {

/// @brief Serialization strategy for `InMemorySerializer`
enum class SerializerMode : int32_t {
  /// Encode only the entity UID as 8 bytes; deserialization recovers the live
  /// entity via `Entity::Shared()`.  Zero-copy and very fast, but only valid
  /// within a single GXF context (publisher and subscriber must share the same
  /// context).
  kPassthrough = 0,

  /// Delegate to a real `PubSubEntitySerializer` set via `set_delegate()`.
  /// Useful for testing full serialization round-trips without a network
  /// transport.  The delegate must be set before the first serialize() call.
  kFullSerialization = 1,
};

/// @brief In-memory `PubSubEntitySerializer` for single-process use
///
/// Supports two modes (see `SerializerMode`):
///
/// **kPassthrough** (default):
///   Encodes the entity UID as 8 raw bytes; `deserialize()` recovers the
///   original entity via `Entity::Shared()`.  This avoids any data copy and
///   is suitable for all in-process pub/sub scenarios.
///   - `supports_zero_copy()` returns `true` in this mode.
///
/// **kFullSerialization**:
///   Delegates to a user-supplied `PubSubEntitySerializer` (set via
///   `set_delegate()`).  Useful for validating that a serializer produces
///   correct round-trip results without requiring a live network transport.
///
/// Thread Safety: all public methods are thread-safe.
class InMemorySerializer : public PubSubEntitySerializer {
 public:
  InMemorySerializer() = default;
  explicit InMemorySerializer(SerializerMode mode) : mode_(mode) {}
  ~InMemorySerializer() override = default;

  // Non-copyable, non-movable (contains mutex)
  InMemorySerializer(const InMemorySerializer&) = delete;
  InMemorySerializer& operator=(const InMemorySerializer&) = delete;
  InMemorySerializer(InMemorySerializer&&) = delete;
  InMemorySerializer& operator=(InMemorySerializer&&) = delete;

  //----------------------------------------------------------------------------
  // Configuration
  //----------------------------------------------------------------------------

  /// @brief Set the serialization mode (must be called before first use)
  void set_mode(SerializerMode mode);

  /// @brief Set the delegate serializer used in kFullSerialization mode
  ///
  /// The pointer must remain valid for as long as this serializer is in use.
  /// Not owned by InMemorySerializer.
  void set_delegate(PubSubEntitySerializer* delegate);

  /// @brief Get the current serialization mode
  SerializerMode mode() const;

  //----------------------------------------------------------------------------
  // PubSubEntitySerializer
  //----------------------------------------------------------------------------

  Expected<std::vector<uint8_t>> serialize(
      Entity entity,
      Handle<Allocator> allocator = Handle<Allocator>()) override;

  Expected<Entity> deserialize(
      const std::vector<uint8_t>& data,
      gxf_context_t context,
      Handle<Allocator> allocator) override;

  size_t estimate_size(Entity entity) override;

  const char* name() const override { return "InMemorySerializer"; }

  /// In `kPassthrough` mode: always `true` (UID reference, no data copy).
  /// In `kFullSerialization` mode: delegates to the configured serializer.
  bool supports_zero_copy() const override;

  /// In `kPassthrough` mode: always `false` (UIDs are CPU-side scalars).
  /// In `kFullSerialization` mode: delegates to the configured serializer.
  bool supports_gpu_tensors() const override;

  /// In `kPassthrough` mode: always `false`.
  /// In `kFullSerialization` mode: delegates to the configured serializer.
  bool supports_direct_buffer_serialization() const override;

  //----------------------------------------------------------------------------
  // Diagnostics
  //----------------------------------------------------------------------------

  /// @brief Total number of successful serialize() calls
  size_t serialize_count() const;

  /// @brief Total number of successful deserialize() calls
  size_t deserialize_count() const;

  /// @brief Reset serialize/deserialize counters to zero
  void clear();

 private:
  mutable std::mutex mutex_;
  SerializerMode mode_ = SerializerMode::kPassthrough;
  PubSubEntitySerializer* delegate_ = nullptr;
  size_t serialize_count_ = 0;
  size_t deserialize_count_ = 0;
};

}  // namespace gxf
}  // namespace nvidia

#endif /* NVIDIA_GXF_PUBSUB_IN_MEMORY_SERIALIZER_HPP_ */
