"""
Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
"""

load(
    "//gxf:gxf.bzl",
    "nv_gxf_cc_extension",
    "nv_gxf_cc_library",
)

nv_gxf_cc_extension(
    name = "stream",
    srcs = ["stream.cpp"],
    hdrs = ["stream_sync_id.hpp"],
    deps = [
        "stream_lib",
        "//gxf/std:extension_factory_helper",
    ],
    ext_deps = [
        "//gxf/std",
    ],
)

nv_gxf_cc_library(
    name = "stream_lib",
    hdrs = [
        "stream_nvsci.hpp",
        "stream_sync_id.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "stream_nvsci",
    ],
)

# Pure abstract stream-sync interface. The NvSciSync-based implementation
# (StreamSync) was removed in this NvSci-free build.
nv_gxf_cc_library(
    name = "stream_nvsci",
    hdrs = ["stream_nvsci.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//gxf/std",
    ],
)

load("//registry/build:registry.bzl", "register_extension")

exports_files(["LICENSE"])

register_extension(
    name = "register_gxf_stream_ext",
    badges = [""],
    extension = "stream",
    headers = [
        "stream_nvsci.hpp",
        "stream_sync_id.hpp",
    ],
    labels = ["gxf"],
    license_file = ":LICENSE",
    local_dependencies = ["//gxf/std:register_std_ext"],
    priority = "1",
    url = "www.nvidia.com",
    uuid = "918e6ad7-8e1a-43aa-9b49-251d4b6072b0",
    version = "0.5.0",
    visibility = ["//visibility:public"],
)
