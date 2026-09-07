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
// check_cuda_ipc_eligibility() is pure metadata comparison (no CUDA calls) and
// is implemented for real following the documented rules.
// CudaDeviceIpcInfo::query() is a stub: it never touches the CUDA runtime and
// reports "not supported".

#include "gxf/pubsub/cuda_ipc_eligibility.hpp"

#include <algorithm>

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

namespace {

bool advertises_protocol(const EndpointInfo& endpoint, const std::string& protocol) {
  const auto& protocols = endpoint.native_buffer_capability.native_buffer_protocols;
  return std::find(protocols.begin(), protocols.end(), protocol) != protocols.end();
}

}  // namespace

CudaIpcEligibilityResult check_cuda_ipc_eligibility(
    const EndpointInfo& publisher,
    const EndpointInfo& subscriber,
    NativeBufferPolicy policy,
    bool is_same_process) {
  CudaIpcEligibilityResult result;

  if (policy == NativeBufferPolicy::kDisabled) {
    result.reason = CudaIpcEligibilityStatus::kPolicyDisabled;
    result.debug_message = "native buffer policy is disabled";
    return result;
  }
  if (is_same_process) {
    result.reason = CudaIpcEligibilityStatus::kSameProcess;
    result.debug_message = "publisher and subscriber are in the same process; "
                           "IPC only makes sense cross-process";
    return result;
  }
  constexpr const char* kCudaIpcProtocol = "cuda_ipc";
  if (!advertises_protocol(publisher, kCudaIpcProtocol)) {
    result.reason = CudaIpcEligibilityStatus::kPublisherDoesNotAdvertiseProtocol;
    result.debug_message = "publisher does not advertise the cuda_ipc protocol";
    return result;
  }
  if (!advertises_protocol(subscriber, kCudaIpcProtocol)) {
    result.reason = CudaIpcEligibilityStatus::kSubscriberDoesNotAdvertiseProtocol;
    result.debug_message = "subscriber does not advertise the cuda_ipc protocol";
    return result;
  }
  const auto& pub_uuid = publisher.native_buffer_capability.gpu_device_uuid;
  const auto& sub_uuid = subscriber.native_buffer_capability.gpu_device_uuid;
  if (!pub_uuid.empty() && !sub_uuid.empty() && pub_uuid != sub_uuid) {
    result.reason = CudaIpcEligibilityStatus::kDeviceUuidMismatch;
    result.debug_message = "publisher and subscriber GPU device UUIDs differ";
    return result;
  }
  const auto& pub_profile = publisher.native_buffer_capability.native_buffer_profile;
  const auto& sub_profile = subscriber.native_buffer_capability.native_buffer_profile;
  if (pub_profile != sub_profile) {
    result.reason = CudaIpcEligibilityStatus::kProfileMismatch;
    result.debug_message = "native_buffer_profile strings differ";
    return result;
  }

  result.reason = CudaIpcEligibilityStatus::kEligible;
  result.debug_message = "eligible";
  return result;
}

CudaDeviceIpcInfo CudaDeviceIpcInfo::query(int device_id) {
  (void)device_id;
  // Stub: never queries the CUDA runtime; reports no IPC capability.
  GXF_LOG_WARNING(
      "CudaDeviceIpcInfo::query is a compatibility stub in this build; "
      "reporting cuda_ipc_supported=false");
  return CudaDeviceIpcInfo{};
}

}  // namespace gxf
}  // namespace nvidia
