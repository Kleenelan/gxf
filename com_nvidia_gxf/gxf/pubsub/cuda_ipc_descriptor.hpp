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
#ifndef NVIDIA_GXF_PUBSUB_CUDA_IPC_DESCRIPTOR_HPP_
#define NVIDIA_GXF_PUBSUB_CUDA_IPC_DESCRIPTOR_HPP_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime_api.h>

#include "gxf/core/expected.hpp"

namespace nvidia {
namespace gxf {

/// Tensor shape/stride/dtype metadata carried alongside the IPC handle.
/// Fields mirror GXF TensorHeader (std_component_serializer.cpp) so that
/// the importer can reconstruct a Tensor without additional lookups.
struct CudaTensorDescriptor {
  std::vector<int64_t> shape;
  /// Per-dimension strides in **bytes** (following the GXF Tensor convention).
  /// May be empty, which indicates a compact row-major (C-contiguous) layout
  /// consistent with the DLPack convention of NULL strides.
  /// Note: DLPack stores strides in elements; convert when bridging.
  std::vector<int64_t> strides;
  std::string          dtype;              // e.g. "float32", "uint8", "float16"
  uint8_t              storage_type{1};    // maps to MemoryStorageType (0=Host,1=Device,2=System,3=Managed)
  uint64_t             bytes_per_element{0};
};

/// Descriptor carrying a CUDA IPC memory handle and associated metadata.
struct CudaIpcDescriptor {
  cudaIpcMemHandle_t   mem_handle{};
  cudaIpcEventHandle_t event_handle{};
  bool                 has_event_handle{false};

  std::string          gpu_device_uuid;
  int32_t              gpu_device_id{0};
  size_t               byte_size{0};
  CudaTensorDescriptor tensor_info;

  uint64_t             producing_timestamp_ns{0};
  uint32_t             sequence_number{0};
};

/// Wire format constants.
static constexpr uint32_t kCudaIpcMagic = 0x43504943;  // "CIPC" little-endian
static constexpr uint8_t  kCudaIpcFormatVersion = 1;
static constexpr size_t   kCudaIpcUuidFieldSize = 40;

/// RAII wrapper for an imported CUDA IPC buffer.
/// Calls cudaIpcCloseMemHandle on destruction.
struct CudaIpcMemHandleCloser {
  void operator()(void* ptr) const noexcept;
};

class CudaIpcImportedBuffer {
 public:
  CudaIpcImportedBuffer() = default;
  ~CudaIpcImportedBuffer() = default;

  CudaIpcImportedBuffer(const CudaIpcImportedBuffer&) = delete;
  CudaIpcImportedBuffer& operator=(const CudaIpcImportedBuffer&) = delete;

  CudaIpcImportedBuffer(CudaIpcImportedBuffer&& other) noexcept = default;
  CudaIpcImportedBuffer& operator=(CudaIpcImportedBuffer&& other) noexcept = default;

  void*  device_ptr() const { return device_ptr_.get(); }
  size_t byte_size()  const { return byte_size_; }
  bool   valid()      const { return static_cast<bool>(device_ptr_); }

 private:
  friend Expected<CudaIpcImportedBuffer> import_cuda_ipc_descriptor(
      const CudaIpcDescriptor&, std::chrono::milliseconds);
  std::unique_ptr<void, CudaIpcMemHandleCloser> device_ptr_{nullptr};
  size_t byte_size_{0};
};

/// Export: caller owns device_ptr and must keep the allocation alive until
/// descriptor consumers have imported it.
///
/// @note `export_event_handle=true` is currently not implemented. Correct support
/// would require an exporter-side lifetime object that keeps the source
/// `cudaEvent_t` alive for the full IPC lifetime.
Expected<CudaIpcDescriptor> export_cuda_ipc_descriptor(
    void*                       device_ptr,
    const CudaTensorDescriptor& tensor_info,
    const std::string&          device_uuid,
    int32_t                     device_id,
    uint32_t                    sequence_number,
    bool                        export_event_handle = false);

/// Import: opens the CUDA IPC handle. Retries up to @p timeout if the source
/// allocation is not yet visible (transient CUDA driver visibility race).
///
/// @note This function **blocks the calling thread** (sleep + retry loop).
/// Do not call from a DDS listener thread or any thread where blocking could
/// cause message drops. The holoipc integration path does not call this
/// directly -- it uses ipc::Context::acquire_pointer() which performs the
/// cudaIpcOpenMemHandle after receiving an ACK on its own worker thread.
Expected<CudaIpcImportedBuffer> import_cuda_ipc_descriptor(
    const CudaIpcDescriptor&  desc,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{100});

/// Serialize a CudaIpcDescriptor to the wire format (version 1).
Expected<std::vector<uint8_t>> serialize_cuda_ipc_descriptor(
    const CudaIpcDescriptor& desc);

/// Deserialize from wire format. Rejects unknown magic or format_version.
Expected<CudaIpcDescriptor> deserialize_cuda_ipc_descriptor(
    const uint8_t* data, size_t size);

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_CUDA_IPC_DESCRIPTOR_HPP_
