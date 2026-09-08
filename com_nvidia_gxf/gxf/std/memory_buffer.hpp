/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#ifndef NVIDIA_GXF_STD_MEMORY_BUFFER_HPP_
#define NVIDIA_GXF_STD_MEMORY_BUFFER_HPP_

#include <functional>
#include <utility>

#include "common/byte.hpp"
#include "gxf/core/expected.hpp"
#include "gxf/core/handle.hpp"
#include "gxf/std/allocator.hpp"

namespace nvidia {
namespace gxf {

// Lifecycle state of a CUDA stream associated with a MemoryBuffer.
// kNone    - No stream has been associated with this buffer.
// kPending - A stream has been set and the buffer may be freed asynchronously
//            once the stream completes pending work.
// kReady   - Stream-ordered work is known to be complete (used internally by
//            stream-aware allocators).
enum class StreamState {
  kNone = 0,
  kPending,
  kReady,
};

class MemoryBuffer {
 public:
  MemoryBuffer() = default;
  MemoryBuffer(const MemoryBuffer&) = delete;
  MemoryBuffer& operator=(const MemoryBuffer&) = delete;

  MemoryBuffer(MemoryBuffer&& other) { *this = std::move(other); }

  MemoryBuffer& operator=(MemoryBuffer&& other) {
    size_ = other.size_;
    storage_type_ = other.storage_type_;
    pointer_ = other.pointer_;
    stream_ = other.stream_;
    stream_state_ = other.stream_state_;
    release_func_ =  std::move(other.release_func_);

    other.pointer_ = nullptr;
    other.stream_ = nullptr;
    other.stream_state_ = StreamState::kNone;
    other.release_func_ = nullptr;

    return *this;
  }

  // Type of the callback function to release memory passed to the MemoryBuffer
  // using the wrapMemory method. The stream argument is the CUDA stream that
  // was associated with the buffer via setStream() (may be nullptr).
  using release_function_t = std::function<Expected<void> (void* pointer, void* stream)>;

  Expected<void> freeBuffer() {
    if (release_func_ && pointer_) {
      const Expected<void> result = release_func_(pointer_, stream_);
      if (!result) { return ForwardError(result); }

      release_func_ = nullptr;
      pointer_ = nullptr;
      stream_ = nullptr;
      stream_state_ = StreamState::kNone;
      size_ = 0;
    }

    return Success;
  }

  virtual ~MemoryBuffer() { freeBuffer(); }

  // Records the CUDA stream that will operate on this buffer. The allocator
  // can use this stream to defer the free until the stream completes.
  void setStream(void* stream) {
    stream_ = stream;
    stream_state_ = (stream_ != nullptr) ? StreamState::kPending : StreamState::kNone;
  }

  // Returns the stream associated with this buffer (may be nullptr).
  void* stream() const { return stream_; }

  // Returns the stream state of this buffer.
  StreamState streamState() const { return stream_state_; }

  Expected<void> resize(Handle<Allocator> allocator, uint64_t size,
                         MemoryStorageType storage_type, void* stream = nullptr) {
    const auto result = freeBuffer();
    if (!result) {
      GXF_LOG_ERROR("Failed to free memory. Error code: %s", GxfResultStr(result.error()));
      return ForwardError(result);
    }

    const auto maybe = allocator->allocate(size, storage_type);
    if (!maybe) {
      GXF_LOG_ERROR("%s Failed to allocate %ld size of memory of type %d. Error code: %s",
                    allocator->name(), size, static_cast<int32_t>(storage_type),
                    GxfResultStr(maybe.error()));
      return ForwardError(maybe);
    }

    storage_type_ = storage_type;
    pointer_ = maybe.value();
    size_ = size;
    setStream(stream);

    release_func_ = [allocator] (void *data, void *release_stream) {
      return allocator->free(reinterpret_cast<byte*>(data), release_stream);
    };

    return Success;
  }

  // Wrap existing memory inside the MemoryBuffer. A callback function of type
  // release_function_t may be passed that will be called when the MemoryBuffer
  // wants to release the memory. The callback receives the buffer's associated
  // stream as its second argument.
  Expected<void> wrapMemory(void* pointer, uint64_t size,
                            MemoryStorageType storage_type,
                            release_function_t release_func) {
    if (pointer != this->pointer()) {
      const auto result = freeBuffer();
      if (!result) { return ForwardError(result); }

      pointer_ = reinterpret_cast<byte*>(pointer);
    }
    storage_type_ = storage_type;
    size_ = size;
    release_func_ = release_func;

    return Success;
  }

  // The type of memory where the data is stored.
  MemoryStorageType storage_type() const { return storage_type_; }

  // Raw pointer to the first byte of elements stored in the buffer.
  byte* pointer() const { return pointer_; }

  // Size of buffer contents in bytes
  uint64_t size() const { return size_; }

 protected:
  uint64_t size_ = 0;
  byte* pointer_ = nullptr;
  MemoryStorageType storage_type_ = MemoryStorageType::kHost;
  void* stream_ = nullptr;
  StreamState stream_state_ = StreamState::kNone;
  release_function_t release_func_ = nullptr;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_MEMORY_BUFFER_HPP_
