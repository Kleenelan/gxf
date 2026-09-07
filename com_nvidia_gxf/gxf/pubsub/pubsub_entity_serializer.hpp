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
#ifndef NVIDIA_GXF_PUBSUB_IENTITY_SERIALIZER_HPP_
#define NVIDIA_GXF_PUBSUB_IENTITY_SERIALIZER_HPP_

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gxf/core/entity.hpp"
#include "gxf/core/expected.hpp"
#include "gxf/core/handle.hpp"
#include "gxf/std/allocator.hpp"

namespace nvidia {
namespace gxf {

// Forward declaration -- full definition in pubsub_native_buffer.hpp (no CUDA dependency).
struct NativeDescriptorPayload;

/// @brief Abstract interface for GXF entity serialization
///
/// This interface abstracts the serialization and deserialization of GXF
/// entities to/from byte arrays. Different implementations can use different
/// serialization formats and strategies.
///
/// Possible Concrete Implementations:
/// - MockSerializer: Simple test implementation (deterministic, no GPU)
/// - CodecRegistrySerializer: Could serialize based on Holoscan's CodecRegistry
/// - CdrSerializer: CDR format with GPU staging support
///
/// GPU Memory Handling:
/// Implementations should handle GPU tensors appropriately. This may involve:
/// - Staging to CPU memory before serialization
/// - Direct GPU-to-GPU transfer (RDMA) for supported transports
/// - Lazy deserialization to avoid unnecessary copies
///
/// Thread Safety: Implementations should be thread-safe for concurrent
/// serialize/deserialize calls on different entities. Concurrent operations
/// on the same entity are not required to be safe.
///
/// Performance Considerations:
/// - Use estimate_size() to pre-allocate buffers and avoid reallocations
/// - Consider zero-copy deserialization where possible
/// - For large tensors, consider streaming or chunked serialization
class PubSubEntitySerializer {
 public:
  virtual ~PubSubEntitySerializer() = default;

  //----------------------------------------------------------------------------
  // Serialization
  //----------------------------------------------------------------------------

  /// @brief Serialize a GXF entity to a byte array
  ///
  /// Converts all components in the entity to a portable byte representation.
  /// The format is implementation-specific but must be deserializable by
  /// the same implementation.
  ///
  /// @param entity The GXF entity to serialize
  /// @param allocator Allocator for staging buffers (e.g., pinned host memory
  ///                  for D2H GPU tensor staging). May be null if no GPU
  ///                  tensors are expected or if the serializer manages its
  ///                  own allocator internally.
  /// @return Serialized byte array, or error code on failure
  ///
  /// @note For entities containing GPU tensors, the implementation may need
  /// to stage data to CPU memory. This can be expensive for large tensors.
  /// The allocator parameter provides symmetry with deserialize(), which
  /// also receives an allocator for H2D staging.
  virtual Expected<std::vector<uint8_t>> serialize(
      Entity entity,
      Handle<Allocator> allocator = Handle<Allocator>()) = 0;

  //----------------------------------------------------------------------------
  // Deserialization
  //----------------------------------------------------------------------------

  /// @brief Deserialize a byte array to a GXF entity
  ///
  /// Reconstructs an entity from a previously serialized byte representation.
  /// The entity is created in the provided context using the given allocator
  /// for any memory allocations (e.g., tensor data).
  ///
  /// @param data Serialized byte array
  /// @param context GXF context for creating the entity
  /// @param allocator Allocator to use for memory allocations
  /// @return Deserialized entity, or error code on failure
  ///
  /// @note The returned entity is owned by the caller and must be properly
  /// managed (typically by adding it to a graph or explicitly destroying it).
  virtual Expected<Entity> deserialize(
      const std::vector<uint8_t>& data,
      gxf_context_t context,
      Handle<Allocator> allocator) = 0;

  /// @brief Move-semantic variant of deserialize(). Allows the
  /// implementation to take ownership of the buffer for zero-copy
  /// strategies. Default delegates to the const-ref overload.
  virtual Expected<Entity> deserialize(
      std::vector<uint8_t>&& data,
      gxf_context_t context,
      Handle<Allocator> allocator) {
    // Default: delegate to const ref version
    return deserialize(data, context, allocator);
  }

  //----------------------------------------------------------------------------
  // Size Estimation
  //----------------------------------------------------------------------------

  /// @brief Estimate the serialized size of an entity
  ///
  /// Returns an estimate of how many bytes serialize() will produce.
  /// This is useful for pre-allocating buffers to avoid reallocations.
  ///
  /// The estimate should be >= the actual size (over-estimation is acceptable,
  /// under-estimation may cause reallocation).
  ///
  /// @param entity The entity to estimate
  /// @return Estimated serialized size in bytes
  virtual size_t estimate_size(Entity entity) = 0;

  //----------------------------------------------------------------------------
  // Direct Buffer Serialization (Optional - SHM Optimization)
  //----------------------------------------------------------------------------

  /// @brief Serialize a GXF entity directly into a pre-allocated buffer
  ///
  /// Allows the transport to provide a buffer (which may be SHM-backed) for
  /// the serializer to write into directly, avoiding an intermediate copy
  /// from std::vector to the transport buffer.
  ///
  /// The default implementation delegates to serialize() and copies the result
  /// into the provided buffer. Implementations may override this to write
  /// directly into the buffer for better performance.
  ///
  /// Typical usage pattern:
  /// @code
  ///   size_t est = serializer->estimate_size(entity);
  ///   uint8_t* shm_buf = transport->allocate_shm_buffer(est);
  ///   auto written = serializer->serialize_into(entity, shm_buf, est, allocator);
  ///   if (written) transport->send_shm_buffer(shm_buf, *written);
  /// @endcode
  ///
  /// @param entity The GXF entity to serialize
  /// @param buffer Pre-allocated buffer to write into (may be SHM-backed)
  /// @param buffer_size Available buffer size in bytes
  /// @param allocator Allocator for staging buffers (forwarded to serialize())
  /// @return Number of bytes written, or error code on failure
  ///
  /// @note If the serialized size exceeds buffer_size, returns
  /// GXF_EXCEEDING_PREALLOCATED_SIZE. Use estimate_size() to pre-allocate
  /// an appropriately sized buffer.
  virtual Expected<size_t> serialize_into(
      Entity entity, uint8_t* buffer, size_t buffer_size,
      Handle<Allocator> allocator = Handle<Allocator>()) {
    // Default: serialize to vector and copy into the provided buffer
    auto vec = serialize(entity, allocator);
    if (!vec) { return ForwardError(vec); }
    if (vec->size() > buffer_size) {
      return Unexpected{GXF_EXCEEDING_PREALLOCATED_SIZE};
    }
    std::memcpy(buffer, vec->data(), vec->size());
    return vec->size();
  }

  //----------------------------------------------------------------------------
  // Capabilities (Optional)
  //----------------------------------------------------------------------------

  /// @brief Check if this serializer supports GPU tensors
  ///
  /// If true, the serializer can handle entities containing GPU tensors
  /// (either by staging or direct serialization).
  ///
  /// @return true if GPU tensors are supported
  virtual bool supports_gpu_tensors() const { return false; }

  /// @brief Check if this serializer supports zero-copy deserialization
  ///
  /// If true, deserialize() may return entities that reference the input
  /// buffer directly, avoiding copies. The input buffer must remain valid
  /// for the lifetime of the deserialized entity.
  ///
  /// @return true if zero-copy is supported
  virtual bool supports_zero_copy() const { return false; }

  /// @brief Check if this serializer can produce/consume native descriptors
  ///
  /// When true, the serializer supports export_native_descriptor() and
  /// import_native_descriptor() for native buffer (e.g. CUDA IPC) paths.
  ///
  /// @return true if native descriptor export/import is supported
  virtual bool supports_native_descriptors() const { return false; }

  /// @brief Export GPU tensor(s) from an entity as a native descriptor
  ///
  /// Produces a lightweight NativeDescriptorPayload (e.g. containing a
  /// serialized CUDA IPC handle) from the entity's GPU allocations.
  /// The entity's GPU allocations must remain valid until all importers
  /// have closed the descriptor or the descriptor TTL expires.
  ///
  /// @param entity_uid UID of the entity to export
  /// @param context GXF context
  /// @param allocator Allocator for any staging needed
  /// @param out_payload Output: the native descriptor payload
  /// @param protocol_name Optional selected native protocol. Empty means the
  ///        serializer may use its default/native-only protocol. When non-empty,
  ///        serializers that support multiple protocols should export for the
  ///        named protocol or return GXF_NOT_IMPLEMENTED.
  /// @return Success or GXF_NOT_IMPLEMENTED if not supported
  virtual Expected<void> export_native_descriptor(
      gxf_uid_t entity_uid,
      gxf_context_t context,
      Handle<Allocator> allocator,
      NativeDescriptorPayload& out_payload,
      const std::string& protocol_name = "") {
    (void)entity_uid; (void)context; (void)allocator; (void)out_payload; (void)protocol_name;
    return Unexpected{GXF_NOT_IMPLEMENTED};
  }

  /// @brief Reconstruct a GXF entity from a native descriptor
  ///
  /// On success, returns an owning Entity handle in the provided context that
  /// wraps the imported native buffer.
  ///
  /// @param payload The native descriptor payload to import
  /// @param context GXF context for creating the entity
  /// @param allocator Allocator for any memory needed
  /// @return Owning Entity on success, or GXF_NOT_IMPLEMENTED if not supported
  virtual Expected<Entity> import_native_descriptor(
      const NativeDescriptorPayload& payload,
      gxf_context_t context,
      Handle<Allocator> allocator) {
    (void)payload; (void)context; (void)allocator;
    return Unexpected{GXF_NOT_IMPLEMENTED};
  }

  /// @brief Check if this serializer has an optimized serialize_into() override
  ///
  /// If true, the serializer writes directly into the provided buffer
  /// without an intermediate std::vector allocation. Transports can use
  /// this to decide whether allocating a SHM buffer for serialize_into()
  /// is worthwhile (vs. just calling serialize() and copying).
  ///
  /// @return true if serialize_into() is optimized (not just the default
  /// delegate-to-serialize fallback)
  virtual bool supports_direct_buffer_serialization() const { return false; }

  /// @brief Get a human-readable name for this serializer
  ///
  /// Useful for logging and debugging.
  ///
  /// @return Serializer name (e.g., "CodecRegistrySerializer", "CdrSerializer")
  virtual const char* name() const = 0;
};

/// @brief Shared pointer type for PubSubEntitySerializer
using PubSubEntitySerializerPtr = std::shared_ptr<PubSubEntitySerializer>;

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_IENTITY_SERIALIZER_HPP_
