/*
Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
#include "gxf/std/cpu_thread.hpp"

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstring>
#include <utility>

#include "gxf/core/gxf.h"
#include "gxf/core/registrar.hpp"

namespace nvidia {
namespace gxf {

// Linux sched_attr structure and sched_setattr syscall numbers. These are not
// exposed by glibc headers on all distributions, so define them locally.
struct sched_attr {
  uint32_t size;
  uint32_t sched_policy;
  uint64_t sched_flags;
  int32_t sched_nice;
  uint32_t sched_priority;
  uint64_t sched_runtime;
  uint64_t sched_deadline;
  uint64_t sched_period;
  uint32_t sched_util_min;
  uint32_t sched_util_max;
};

#ifndef __NR_sched_setattr
#define __NR_sched_setattr 314
#endif
#ifndef __NR_sched_getattr
#define __NR_sched_getattr 315
#endif

gxf_result_t CPUThread::registerInterface(Registrar* registrar) {
  Expected<void> result;

  result &= registrar->parameter(
    pin_entity_, "pin_entity", "Pin Entity",
    "Set the cpu_core to be pinned to a worker thread or not.",
    false);

  // GXF 5.1+ compatibility parameters (holoscan-sdk thread pool configuration
  // forwards them): parsed and applied when a worker thread is created for the
  // entity which owns this CPUThread component.
  result &= registrar->parameter(
    pin_cores_, "pin_cores", "Pin Cores",
    "CPU cores to pin the worker thread to (empty means no core pinning).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_policy_, "sched_policy", "Scheduling Policy",
    "Real-time scheduling policy (SCHED_FIFO, SCHED_RR, SCHED_DEADLINE).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_priority_, "sched_priority", "Scheduling Priority",
    "Thread priority for FirstInFirstOut and RoundRobin policies.",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_runtime_, "sched_runtime", "Scheduling Runtime",
    "SCHED_DEADLINE runtime in ns.",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_deadline_, "sched_deadline", "Scheduling Deadline",
    "SCHED_DEADLINE deadline in ns.",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_period_, "sched_period", "Scheduling Period",
    "SCHED_DEADLINE period in ns.",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);

  return ToResultCode(result);
}

gxf_result_t ApplyCPUThreadConfiguration(gxf_context_t context, gxf_uid_t cpu_thread_cid) {
  if (cpu_thread_cid == kUnspecifiedUid) {
    return GXF_SUCCESS;
  }

  auto maybe_cpu_thread = Handle<CPUThread>::Create(context, cpu_thread_cid);
  if (!maybe_cpu_thread) {
    GXF_LOG_WARNING("Failed to get CPUThread [cid: %ld] for thread configuration: %s",
                    cpu_thread_cid, GxfResultStr(ToResultCode(maybe_cpu_thread)));
    return ToResultCode(maybe_cpu_thread);
  }
  const auto& cpu_thread = maybe_cpu_thread.value();

  // Apply core affinity first.
  const auto pin_cores = cpu_thread->pinCores();
  if (!pin_cores.empty()) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (uint32_t core : pin_cores) {
      if (core >= CPU_SETSIZE) {
        GXF_LOG_WARNING("CPUThread [cid: %ld] requests core %u which exceeds CPU_SETSIZE %d; "
                        "ignoring", cpu_thread_cid, core, CPU_SETSIZE);
        continue;
      }
      CPU_SET(core, &cpuset);
    }
    const int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (ret != 0) {
      GXF_LOG_WARNING("pthread_setaffinity_np failed for CPUThread [cid: %ld]: %s",
                      cpu_thread_cid, std::strerror(ret));
    } else {
      GXF_LOG_DEBUG("CPUThread [cid: %ld] pinned worker thread to configured cores",
                    cpu_thread_cid);
    }
  }

  // Apply real-time scheduling policy if configured.
  if (cpu_thread->isRealtime()) {
    sched_attr attr{};
    attr.size = sizeof(attr);

    const auto policy = cpu_thread->schedPolicy();
    switch (policy) {
      case SchedulingPolicy::kFirstInFirstOut:
        attr.sched_policy = SCHED_FIFO;
        attr.sched_priority = cpu_thread->schedPriority();
        break;
      case SchedulingPolicy::kRoundRobin:
        attr.sched_policy = SCHED_RR;
        attr.sched_priority = cpu_thread->schedPriority();
        break;
      case SchedulingPolicy::kDeadline:
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_runtime = cpu_thread->schedRuntime();
        attr.sched_deadline = cpu_thread->schedDeadline();
        attr.sched_period = cpu_thread->schedPeriod();
        break;
    }

    const long ret = syscall(__NR_sched_setattr, 0, &attr, 0);
    if (ret != 0) {
      GXF_LOG_WARNING("sched_setattr failed for CPUThread [cid: %ld]: %s",
                      cpu_thread_cid, std::strerror(errno));
    } else {
      GXF_LOG_DEBUG("CPUThread [cid: %ld] applied real-time scheduling policy",
                    cpu_thread_cid);
    }
  }

  return GXF_SUCCESS;
}

}  // namespace gxf
}  // namespace nvidia
