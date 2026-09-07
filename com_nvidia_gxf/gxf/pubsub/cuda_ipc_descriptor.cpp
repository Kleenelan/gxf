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
// CUDA IPC descriptor export/import is part of the native-buffer pub/sub
// data plane, which is not functional in this build. All entry points fail
// with GXF_NOT_IMPLEMENTED and never touch the CUDA runtime.

#include "gxf/pubsub/cuda_ipc_descriptor.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

void CudaIpcMemHandleCloser::operator()(void* ptr) const noexcept {
  if (ptr != nullptr) {
    // Unreachable in this stub (import_cuda_ipc_descriptor never succeeds).
    // Deliberately does not call cudaIpcCloseMemHandle.
    GXF_LOG_ERROR(
        "CudaIpcMemHandleCloser invoked in the pub/sub compatibility stub; "
        "leaking the (unexpected) imported handle instead of calling into CUDA");
  }
}

Expected<CudaIpcDescriptor> export_cuda_ipc_descriptor(
    void* device_ptr,
    const CudaTensorDescriptor& tensor_info,
    const std::string& device_uuid,
    int32_t device_id,
    uint32_t sequence_number,
    bool export_event_handle) {
  (void)device_ptr; (void)tensor_info; (void)device_uuid; (void)device_id;
  (void)sequence_number; (void)export_event_handle;
  GXF_LOG_ERROR(
      "export_cuda_ipc_descriptor is a compatibility stub in this build; "
      "the pub/sub native-buffer path is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<CudaIpcImportedBuffer> import_cuda_ipc_descriptor(
    const CudaIpcDescriptor& desc,
    std::chrono::milliseconds timeout) {
  (void)desc; (void)timeout;
  GXF_LOG_ERROR(
      "import_cuda_ipc_descriptor is a compatibility stub in this build; "
      "the pub/sub native-buffer path is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<std::vector<uint8_t>> serialize_cuda_ipc_descriptor(
    const CudaIpcDescriptor& desc) {
  (void)desc;
  GXF_LOG_ERROR(
      "serialize_cuda_ipc_descriptor is a compatibility stub in this build; "
      "the pub/sub native-buffer path is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<CudaIpcDescriptor> deserialize_cuda_ipc_descriptor(
    const uint8_t* data, size_t size) {
  (void)data; (void)size;
  GXF_LOG_ERROR(
      "deserialize_cuda_ipc_descriptor is a compatibility stub in this build; "
      "the pub/sub native-buffer path is not functional (GXF_NOT_IMPLEMENTED)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

}  // namespace gxf
}  // namespace nvidia
