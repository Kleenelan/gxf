"""
Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
"""

cc_library(
    name = "ucx_x86_64_cuda_12_6",
    # Glob instead of an explicit list: the set of UCX transport plugins
    # under usr/lib/ucx/ depends on how the local UCX was built (e.g. a build
    # without IB/RDMA support has no libuct_ib.so / libuct_rdmacm.so).
    srcs = glob(["ucx-install-with-cuda/usr/lib/**/*.so*"]),
    hdrs = [
        "ucx-install-with-cuda/usr/include/ucm/api/ucm.h",
        "ucx-install-with-cuda/usr/include/ucp/api/ucp_compat.h",
        "ucx-install-with-cuda/usr/include/ucp/api/ucp_def.h",
        "ucx-install-with-cuda/usr/include/ucp/api/ucp.h",
        "ucx-install-with-cuda/usr/include/ucp/api/ucp_version.h",
        "ucx-install-with-cuda/usr/include/ucs/memory/memory_type.h",
        "ucx-install-with-cuda/usr/include/ucs/sys/compiler_def.h",
        "ucx-install-with-cuda/usr/include/ucs/type/status.h",
        "ucx-install-with-cuda/usr/include/ucs/config/types.h",
        "ucx-install-with-cuda/usr/include/ucs/type/thread_mode.h",
        "ucx-install-with-cuda/usr/include/ucs/type/cpu_set.h",
    ],
    includes = ["include"],
    linkopts = [
        "-Wl,--no-as-needed," +
        "--as-needed",
    ],
    strip_include_prefix = "ucx-install-with-cuda/usr/include",
    visibility = ["//visibility:public"],
)