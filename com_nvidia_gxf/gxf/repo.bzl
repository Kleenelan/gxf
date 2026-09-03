"""
 SPDX-FileCopyrightText: Copyright (c) 2020 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def nv_gxf_http_archive(licenses, name, **kwargs):
    """
    A GXF HTTP third party archive. Augment the standard Bazel HTTP archive workspace rule.
    Mandatory licenses label.
    """
    maybe(
        repo_rule = http_archive,
        name = name,
        **kwargs
    )

def nv_gxf_new_git_repository(licenses, name, **kwargs):
    """
    A GXF Git third party repository. Augment the standard new Bazel Git repository workspace
    rule. Mandatory licenses label.
    """
    maybe(
        repo_rule = new_git_repository,
        name = name,
        **kwargs
    )

def nv_gxf_git_repository(licenses, name, **kwargs):
    """
    A GXF Git third party repository. Augment the standard Bazel Git repository workspace rule.
    Mandatory licenses label.
    """
    maybe(
        repo_rule = git_repository,
        name = name,
        **kwargs
    )

def nv_gxf_new_local_repository(licenses, name, **kwargs):
    """
    A GXF local third party repository. Augment the standard Bazel Git repository workspace rule.
    Mandatory licenses label.
    """
    maybe(
        repo_rule = native.new_local_repository,
        name = name,
        **kwargs
    )

def _cuda_home_repository_impl(repository_ctx):
    """
    Exposes the CUDA toolkit under $CUDA_HOME (default /usr/local/cuda,
    overridable via --repo_env=CUDA_HOME=...) as a Bazel repository, without
    hardcoding any machine-specific absolute path in the source tree.

    Only the subtrees consumed by the toolchain (bin/, nvvm/) are linked in;
    the provided build_file is installed as the repo BUILD file.
    """
    cuda_home = repository_ctx.os.environ.get("CUDA_HOME", "/usr/local/cuda")
    if not repository_ctx.path(cuda_home + "/bin").exists:
        fail("CUDA_HOME not found at '%s' (set it via --repo_env=CUDA_HOME=...)" % cuda_home)
    repository_ctx.symlink(cuda_home + "/bin", "bin")
    repository_ctx.symlink(cuda_home + "/nvvm", "nvvm")
    repository_ctx.symlink(repository_ctx.attr.build_file, "BUILD")

cuda_home_repository = repository_rule(
    implementation = _cuda_home_repository_impl,
    attrs = {
        "build_file": attr.label(mandatory = True),
    },
    environ = ["CUDA_HOME"],
    local = True,
)

