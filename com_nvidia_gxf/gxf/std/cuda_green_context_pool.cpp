/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

// GXF 5.1/5.7 COMPATIBILITY STUB — see cuda_green_context_pool.hpp.
// No CUDA Green Contexts are created; only the component interface is kept so
// that graphs referencing this component type can be loaded and executed.

#include "gxf/std/cuda_green_context_pool.hpp"

namespace nvidia {
namespace gxf {

namespace {
constexpr uint32_t kDefaultNumPartitions = 1;
constexpr uint32_t kDefaultMinSmCount = 0;
constexpr int32_t kDefaultContextIndex = -1;

void greenContextStubNotImplemented(const char* what) {
  GXF_LOG_ERROR(
      "CudaGreenContextPool::%s is not available: this GXF build is based on 4.1 and only "
      "provides a compatibility stub for CUDA Green Contexts (requires genuine GXF >= 5.1)",
      what);
}
}  // namespace

gxf_result_t CudaGreenContextPool::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->resource(gpu_device_, "GPU device on which green contexts are created");
  result &= registrar->parameter(
      green_context_flags_, "green_context_flags", "Green Context Flags",
      "Flags for CUDA green contexts in the pool (unused in this compatibility stub).",
      uint32_t(0));
  result &= registrar->parameter(
      num_partitions_, "num_partitions", "Number of Partitions",
      "Number of partitions to create for the green context pool "
      "(unused in this compatibility stub).",
      kDefaultNumPartitions);
  result &= registrar->parameter(
      min_sm_count_, "min_sm_count", "Minimum SM Count",
      "The minimum number of SMs used for green context creation "
      "(unused in this compatibility stub).",
      kDefaultMinSmCount);
  result &= registrar->parameter(
      sms_per_partition_, "sms_per_partition", "SMs per Partition",
      "The number of SMs to allocate per partition (unused in this compatibility stub).",
      std::vector<int32_t>{});
  result &= registrar->parameter(
      nvtx_identifier_, "nvtx_identifier", "NVTX Identifier",
      "The NVTX identifier of the green context pool (unused in this compatibility stub).",
      std::string(""));
  result &= registrar->parameter(
      default_context_, "default_context", "Default Context",
      "The index of the default green context to use (unused in this compatibility stub).",
      kDefaultContextIndex);
  result &= registrar->parameter(
      dev_id_, "dev_id", "Device Id",
      "Optional handle alias of the GPU device resource (compatibility only; "
      "prefer the type-based GPUDevice resource).",
      Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  return ToResultCode(result);
}

gxf_result_t CudaGreenContextPool::initialize() {
  GXF_LOG_WARNING(
      "CudaGreenContextPool (cid: %ld) is a compatibility stub: no CUDA Green Contexts will "
      "be created (this GXF build is based on 4.1). SM partitioning will NOT take effect.",
      cid());
  return GXF_SUCCESS;
}

gxf_result_t CudaGreenContextPool::deinitialize() {
  return GXF_SUCCESS;
}

Expected<CUgreenCtx> CudaGreenContextPool::getGreenContext(uint32_t index) {
  (void)index;
  greenContextStubNotImplemented("getGreenContext");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<CUcontext> CudaGreenContextPool::getCudaContext(uint32_t index) {
  (void)index;
  greenContextStubNotImplemented("getCudaContext");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<uint32_t> CudaGreenContextPool::getPartitionSms(uint32_t index) {
  (void)index;
  greenContextStubNotImplemented("getPartitionSms");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<uint32_t> CudaGreenContextPool::getDefaultContextIndex() {
  greenContextStubNotImplemented("getDefaultContextIndex");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<CUgreenCtx> CudaGreenContextPool::getDefaultContext() {
  greenContextStubNotImplemented("getDefaultContext");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<uint32_t> CudaGreenContextPool::getDeviceTotalSms(uint32_t index) {
  (void)index;
  greenContextStubNotImplemented("getDeviceTotalSms");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

}  // namespace gxf
}  // namespace nvidia
