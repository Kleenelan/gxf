"""
 SPDX-FileCopyrightText: Copyright (c) 2020-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 SPDX-License-Identifier: Apache-2.0

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
"""

# Version-agnostic Python headers + libpython, repackaged by build2.sh from
# the interpreter selected via GXF_PYTHON (supports 3.10/3.11/3.12/3.13/3.14+).
# The tarball layout is fixed (include/ + lib/) so no per-version BUILD file
# is needed; the library soname keeps its original libpythonX.Y.so name.
cc_library(
    name = "python_local",
    srcs = glob(["lib/libpython*.so*"]),
    hdrs = glob(["include/**/*.h"]),
    linkopts = [],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [],
)
