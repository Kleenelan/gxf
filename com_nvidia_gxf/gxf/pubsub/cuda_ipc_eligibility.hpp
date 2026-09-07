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
#ifndef NVIDIA_GXF_PUBSUB_CUDA_IPC_ELIGIBILITY_HPP_
#define NVIDIA_GXF_PUBSUB_CUDA_IPC_ELIGIBILITY_HPP_

#include <cstdint>
#include <string>

#include "gxf/pubsub/endpoint_info.hpp"
#include "gxf/pubsub/pubsub_context.hpp"  // for NativeBufferPolicy

namespace nvidia {
namespace gxf {

/// Status of a CUDA IPC eligibility check for a publisher/subscriber pair.
enum class CudaIpcEligibilityStatus : uint8_t {
  kEligible                           = 0,
  kPublisherDoesNotAdvertiseProtocol  = 1,
  kSubscriberDoesNotAdvertiseProtocol = 2,
  kDeviceUuidMismatch                 = 3,
  kSameProcess                        = 4,  // IPC only makes sense cross-process
  kProfileMismatch                    = 5,  // native_buffer_profile strings differ
  kPolicyDisabled                     = 6,
};

/// Result of a CUDA IPC eligibility check.
struct CudaIpcEligibilityResult {
  CudaIpcEligibilityStatus reason{CudaIpcEligibilityStatus::kEligible};
  std::string                debug_message;
  bool eligible() const {
    return reason == CudaIpcEligibilityStatus::kEligible;
  }
};

/// Single entry point called by both DDS and Zenoh backends before choosing
/// the native-buffer path.  No CUDA calls; pure metadata comparison.
///
/// @param publisher   Publisher endpoint info (with NativeBufferCapability)
/// @param subscriber  Subscriber endpoint info (with NativeBufferCapability)
/// @param policy      The active NativeBufferPolicy
/// @param is_same_process  True if publisher and subscriber are in the same process
///                         (backends know this at endpoint-creation time)
/// @return Eligibility result with reason and debug message
CudaIpcEligibilityResult check_cuda_ipc_eligibility(
    const EndpointInfo& publisher,
    const EndpointInfo& subscriber,
    NativeBufferPolicy  policy = NativeBufferPolicy::kPreferred,
    bool is_same_process = false);

/// Query GPU device properties for CUDA IPC capability.
/// Returns the device UUID string and whether CUDA IPC is supported on that device.
/// On failure, uuid is empty and cuda_ipc_supported is false.
struct CudaDeviceIpcInfo {
  std::string uuid;
  bool cuda_ipc_supported = false;

  /// Query info for the given device_id. Returns default (empty) on CUDA errors.
  static CudaDeviceIpcInfo query(int device_id);
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_CUDA_IPC_ELIGIBILITY_HPP_
