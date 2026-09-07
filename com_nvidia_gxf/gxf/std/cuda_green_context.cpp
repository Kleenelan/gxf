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

// GXF 5.1/5.7 COMPATIBILITY STUB — see cuda_green_context.hpp.

#include "gxf/std/cuda_green_context.hpp"

namespace nvidia {
namespace gxf {

gxf_result_t CudaGreenContext::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->parameter(
      index_, "index", "Index", "The index of the green context to use.", int32_t(-1));
  result &= registrar->parameter(
      cuda_green_context_pool_, "cuda_green_context_pool", "Green Context Pool",
      "The green context pool to use.",
      Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
      nvtx_identifier_, "nvtx_identifier", "NVTX Identifier",
      "The NVTX identifier of the green context (unused in this compatibility stub).",
      std::string(""));
  return ToResultCode(result);
}

gxf_result_t CudaGreenContext::initialize() {
  const int32_t requested = index_.try_get().value_or(-1);
  pool_index_ = requested >= 0 ? static_cast<uint32_t>(requested) : 0;
  GXF_LOG_WARNING(
      "CudaGreenContext (cid: %ld) is a compatibility stub: no CUgreenCtx is held "
      "(this GXF build is based on 4.1). SM partitioning will NOT take effect.",
      cid());
  return GXF_SUCCESS;
}

gxf_result_t CudaGreenContext::deinitialize() {
  return GXF_SUCCESS;
}

Expected<CUgreenCtx> CudaGreenContext::greenContext() const {
  GXF_LOG_ERROR(
      "CudaGreenContext::greenContext is not available: this GXF build is based on 4.1 and "
      "only provides a compatibility stub for CUDA Green Contexts (requires genuine GXF >= 5.1)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

Expected<CUcontext> CudaGreenContext::cudaContext() {
  GXF_LOG_ERROR(
      "CudaGreenContext::cudaContext is not available: this GXF build is based on 4.1 and "
      "only provides a compatibility stub for CUDA Green Contexts (requires genuine GXF >= 5.1)");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

}  // namespace gxf
}  // namespace nvidia
