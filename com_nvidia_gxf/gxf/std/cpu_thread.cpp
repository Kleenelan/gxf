/*
Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
#include "gxf/std/cpu_thread.hpp"

#include <utility>

#include "gxf/core/gxf.h"
#include "gxf/core/registrar.hpp"

namespace nvidia {
namespace gxf {

gxf_result_t CPUThread::registerInterface(Registrar* registrar) {
  Expected<void> result;

  result &= registrar->parameter(
    pin_entity_, "pin_entity", "Pin Entity",
    "Set the cpu_core to be pinned to a worker thread or not.",
    false);

  // GXF 5.1 compatibility parameters (holoscan-sdk thread pool configuration
  // forwards them): parsed and accepted but intentionally NOT applied — this
  // 4.1-based runtime does not implement core pinning / real-time scheduling.
  result &= registrar->parameter(
    pin_cores_, "pin_cores", "Pin Cores",
    "CPU cores to pin the worker thread to (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_policy_, "sched_policy", "Scheduling Policy",
    "Real-time scheduling policy (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_priority_, "sched_priority", "Scheduling Priority",
    "Real-time scheduling priority (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_runtime_, "sched_runtime", "Scheduling Runtime",
    "SCHED_DEADLINE runtime in ns (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_deadline_, "sched_deadline", "Scheduling Deadline",
    "SCHED_DEADLINE deadline in ns (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);
  result &= registrar->parameter(
    sched_period_, "sched_period", "Scheduling Period",
    "SCHED_DEADLINE period in ns (accepted but not applied in this "
    "GXF 4.1-based build).",
    Registrar::NoDefaultParameter(), GXF_PARAMETER_FLAGS_OPTIONAL);

  return ToResultCode(result);
}

}  // namespace gxf
}  // namespace nvidia
