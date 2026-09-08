/*
Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
#include "gxf/std/resources.hpp"

#include <map>
#include <utility>
#include <vector>

#include "gxf/core/gxf.h"
#include "gxf/core/registrar.hpp"

namespace nvidia {
namespace gxf {

gxf_result_t ThreadPool::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->parameter(
    initial_size_, "initial_size", "Initial ThreadPool Size",
    "Initial number of worker threads in the pool",
    0l);

  result &= registrar->parameter(
    priority_, "priority", "Thread Priorities",
    "Priority level for threads in the pool. Default is 0 (low)"
    "Can also be set to 1 (medium) or 2 (high)",
    0l);

  return ToResultCode(result);
}

gxf_result_t ThreadPool::initialize() {
  for (auto i = 0; i < initial_size_.get(); ++i) {
    this->addThread(kMaxComponents + kMaxEntities + i);
  }
  return GXF_SUCCESS;
}

int64_t ThreadPool::size() const {
  return thread_pool_.size();
}

int64_t ThreadPool::priority() const {
  return priority_;
}

Expected<gxf_uid_t> ThreadPool::addThread(gxf_uid_t uid) {
  return addThread(uid, kUnspecifiedUid);
}

Expected<gxf_uid_t> ThreadPool::addThread(gxf_uid_t uid, gxf_uid_t cpu_thread_cid) {
  Thread thread = {
    .uid = uid,
    .cpu_thread_cid = cpu_thread_cid
  };
  auto it = thread_pool_.emplace(uid, thread);
  if (it.second == true) {
    return thread.uid;
  } else {
    return Unexpected{GXF_NOT_FINISHED};
  }
}

Expected<gxf_uid_t> ThreadPool::addThread(gxf_uid_t uid,
                                          const std::vector<uint32_t>& pin_cores) {
  // The pin_cores overload is provided for API compatibility with GXF 5.1+.
  // Core pinning is applied later by the scheduler via the CPUThread component
  // referenced by cpu_thread_cid; this overload simply records that the thread
  // was created with explicit pinning intent.
  (void)pin_cores;
  return addThread(uid, kUnspecifiedUid);
}

const Expected<ThreadPool::Thread> ThreadPool::getThread(gxf_uid_t uid) const {
  auto it = thread_pool_.find(uid);
  if (it == thread_pool_.end()) {
    return Unexpected{GXF_RESOURCE_NOT_FOUND};
  }
  return it->second;
}

const std::map<gxf_uid_t, ThreadPool::Thread>& ThreadPool::get() const {
  return thread_pool_;
}

gxf_result_t GPUDevice::registerInterface(Registrar* registrar) {
  Expected<void> result;
  result &= registrar->parameter(
      dev_id_, "dev_id", "Device Id",
      "Create CUDA Stream on which device.", 0);
  return ToResultCode(result);
}

}  // namespace gxf
}  // namespace nvidia
