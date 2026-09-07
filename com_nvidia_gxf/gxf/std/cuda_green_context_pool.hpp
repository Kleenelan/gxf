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
#ifndef NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_POOL_HPP_
#define NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_POOL_HPP_

#include "cuda.h"

#include <mutex>
#include <string>
#include <vector>

#include "gxf/core/component.hpp"
#include "gxf/core/expected.hpp"
#include "gxf/std/resources.hpp"

namespace nvidia {
namespace gxf {

// @brief A pool containing a specified number of CUDA Green Contexts on a single device.
//
// GXF 5.1/5.7 COMPATIBILITY STUB (this tree is based on GXF 4.1 which has no
// Green Context support): the public API (class name, parameters, method
// signatures) matches upstream so that dependent code (e.g. holoscan-sdk)
// compiles, links and runs while Green Contexts are NOT actually created.
// initialize() succeeds, but every accessor fails with GXF_NOT_IMPLEMENTED.
// Applications that do not use SM partitioning are unaffected; applications
// that require real Green Contexts need genuine GXF >= 5.1.
class CudaGreenContextPool : public Component {
 public:
  CudaGreenContextPool() = default;
  ~CudaGreenContextPool() override = default;

  gxf_result_t initialize() override;
  gxf_result_t deinitialize() override;
  gxf_result_t registerInterface(Registrar* registrar) override;

  Expected<CUgreenCtx> getGreenContext(uint32_t index);
  Expected<CUcontext> getCudaContext(uint32_t index);
  Expected<uint32_t> getPartitionSms(uint32_t index);
  Expected<uint32_t> getDefaultContextIndex();
  Expected<CUgreenCtx> getDefaultContext();
  Expected<uint32_t> getDeviceTotalSms(uint32_t index);

 private:
  Resource<Handle<GPUDevice>> gpu_device_;
  Parameter<uint32_t> green_context_flags_;
  Parameter<uint32_t> num_partitions_;
  Parameter<uint32_t> min_sm_count_;
  Parameter<std::vector<int32_t>> sms_per_partition_;
  Parameter<std::string> nvtx_identifier_;
  Parameter<int32_t> default_context_;
  // Optional handle alias for the GPU device resource, kept for parameter
  // read-back compatibility with upstream consumers ("dev_id").
  Parameter<Handle<GPUDevice>> dev_id_;

  std::mutex mutex_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_CUDA_GREEN_CONTEXT_POOL_HPP_
