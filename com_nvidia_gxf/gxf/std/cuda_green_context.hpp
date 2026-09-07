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
#ifndef NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_HPP_
#define NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_HPP_

#include "cuda.h"

#include <string>

#include "gxf/core/component.hpp"
#include "gxf/core/expected.hpp"
#include "gxf/core/handle.hpp"
#include "gxf/std/cuda_green_context_pool.hpp"

namespace nvidia {
namespace gxf {

// @brief Holds and provides access to CUgreenCtx. CudaGreenContext is allocated and
// recycled by CudaGreenContextPool.
//
// GXF 5.1/5.7 COMPATIBILITY STUB (this tree is based on GXF 4.1 which has no
// Green Context support): the public API matches upstream so that dependent
// code (e.g. holoscan-sdk) compiles, links and runs while no real CUgreenCtx
// is held. initialize() succeeds, but greenContext()/cudaContext() fail with
// GXF_NOT_IMPLEMENTED.
class CudaGreenContext : public Component {
 public:
  CudaGreenContext() = default;
  ~CudaGreenContext() override = default;

  gxf_result_t registerInterface(Registrar* registrar) override;
  gxf_result_t initialize() override;
  gxf_result_t deinitialize() override;

  // Retrieves CUgreenCtx (stub: always fails with GXF_NOT_IMPLEMENTED)
  Expected<CUgreenCtx> greenContext() const;

  // Retrieves CUcontext that associated with this green context
  // (stub: always fails with GXF_NOT_IMPLEMENTED)
  Expected<CUcontext> cudaContext();

  uint32_t index() const { return pool_index_; }

  CudaGreenContextPool* cudaGreenContextPool() const {
    const auto& maybe_pool = cuda_green_context_pool_.try_get();
    return maybe_pool ? maybe_pool.value().get() : nullptr;
  }

 private:
  Parameter<int32_t> index_;
  Parameter<Handle<CudaGreenContextPool>> cuda_green_context_pool_;
  Parameter<std::string> nvtx_identifier_;

  uint32_t pool_index_ = 0;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_HPP_
