#!/bin/bash -e
#
# One-click build for the NvSci-free, intranet-free GXF 4.1 (x86_64_cuda_12_6).
#
# Usage:
#   ./build.sh           Build the full Isaac ROS release target set (35 targets)
#   ./build.sh test      Build + run gxe smoke test (test_ping.yaml)
#   ./build.sh clean     bazel clean
#   ./build.sh deps      (Re)generate local dependency tarballs from this machine's
#                        CUDA (/usr/local/cuda), UCX (/opt/ucx-1.20.0) and Python 3.10
#
# Outputs:
#   gxf/com_nvidia_gxf/bazel-bin/gxf/...   (*.so, gxe, ...)
#
# Requirements: Ubuntu 22.04, gcc-11, python3.10, CUDA toolkit, UCX, curl.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
GXF_DIR="${ROOT}/gxf/com_nvidia_gxf"
BAZEL="${ROOT}/tools/bazel"
DIST="${ROOT}/local_deps/dist"

export target_platform=x86_64_cuda_12_6 cpu=k8 compiler=gcc-11
BAZEL_OPTS=(--config=x86_64_cuda_12_6
            --repo_env=target_platform=x86_64_cuda_12_6
            --repo_env=cpu=k8
            --repo_env=compiler=gcc-11)

# Isaac ROS release target set (from build_gxf_release_content.yaml,
# with non-target file entries mapped to their producing rules)
TARGETS=(
  //gxf/behavior_tree:libgxf_behavior_tree.so
  //gxf/core:core_pybind.so //gxf/core:libgxf_core.so //gxf/core:libgxf_core
  //gxf/cuda:libgxf_cuda.so //gxf/cuda:cuda_pybind.so //gxf/cuda/tests:libgxf_test_cuda.so
  //gxf/gxe:gxe
  //gxf/ipc/grpc:libgxf_grpc.so //gxf/ipc/http:libgxf_http.so
  //gxf/logger:libgxf_logger.so
  //gxf/multimedia:libgxf_multimedia.so
  //gxf/npp:libgxf_npp.so
  //gxf/network:libgxf_network.so
  //gxf/python_codelet:libgxf_python_codelet.so //gxf/python_codelet:pycodelet.so
  //gxf/sample:libgxf_sample.so
  //gxf/serialization:libgxf_serialization.so
  //gxf/serialization:serialization_buffer //gxf/serialization:entity_serializer
  //gxf/std:allocator_pybind.so //gxf/std:clock_pybind.so //gxf/std:default_extension
  //gxf/std:dlpack_utils //gxf/std:libgxf_std.so //gxf/std:gxf_std_static
  //gxf/std:metric //gxf/std:tensor //gxf/std:yaml_file_loader
  //gxf/std:receiver_pybind.so //gxf/std:tensor_pybind.so //gxf/std:timestamp_pybind.so
  //gxf/std:transmitter_pybind.so //gxf/std:vault_pybind.so
  //gxf/test/extensions:libgxf_test.so
)

info() { echo -e "\e[32m[INFO]\e[0m $*"; }
err()  { echo -e "\e[31m[ERROR]\e[0m $*" >&2; exit 1; }

# --- 1. Bazel 6.0.0 -------------------------------------------------------------
prepare_bazel() {
  [[ -x "$BAZEL" ]] && return
  info "Downloading Bazel 6.0.0 ..."
  mkdir -p "${ROOT}/tools"
  curl -sfL --retry 3 -o "$BAZEL" \
    https://github.com/bazelbuild/bazel/releases/download/6.0.0/bazel-6.0.0-linux-x86_64 \
    || err "Bazel download failed"
  chmod +x "$BAZEL"
}

# --- 2. Local dependency tarballs (CUDA / UCX / Python) -------------------------
prepare_deps() {
  mkdir -p "$DIST"
  local LD="${ROOT}/local_deps"

  # CUDA: repackage local toolkit into the layout expected by cuda_x86_64_12060.BUILD
  if [[ ! -f "$DIST/cuda_x86_64_12060.tar.gz" ]]; then
    local CUDA_HOME
    CUDA_HOME="$(readlink -f /usr/local/cuda)"
    [[ -d "$CUDA_HOME" ]] || err "/usr/local/cuda not found"
    info "Repackaging CUDA from $CUDA_HOME (one-time, ~10 min) ..."
    local SLIM="$LD/cuda_slim"
    rm -rf "$SLIM"; mkdir -p "$SLIM/usr/local/cuda-12.6/lib64" \
      "$SLIM/usr/local/cuda-12.6/targets/x86_64-linux/lib"
    cp -rL "$CUDA_HOME/include" "$SLIM/usr/local/cuda-12.6/include"
    (cd "$CUDA_HOME/lib64" && \
      cp -rL libcudart.so* libcufft.so* libcurand.so* libcusolver.so* libcusparse.so* \
             libnvrtc.so* libnpp*.so* libcudnn*.so* libcublas*.so* \
             "$SLIM/usr/local/cuda-12.6/lib64/")
    cp -rL "$CUDA_HOME/targets/x86_64-linux/lib/stubs" \
           "$SLIM/usr/local/cuda-12.6/targets/x86_64-linux/lib/stubs"
    cp -rL "$CUDA_HOME"/targets/x86_64-linux/lib/libnvToolsExt.so* \
           "$SLIM/usr/local/cuda-12.6/targets/x86_64-linux/lib/"
    (cd "$SLIM" && tar -czf "$DIST/cuda_x86_64_12060.tar.gz" usr)
    rm -rf "$SLIM"
  fi

  # UCX: repackage local install into ucx-install-with-cuda/usr layout
  if [[ ! -f "$DIST/ucx_x86_64_cuda_12_6.tar.gz" ]]; then
    local UCX_HOME="${UCX_HOME:-/opt/ucx-1.20.0}"
    [[ -d "$UCX_HOME" ]] || err "UCX not found at $UCX_HOME (set UCX_HOME)"
    info "Repackaging UCX from $UCX_HOME ..."
    local U="$LD/ucx_x86_64_cuda_12_6"
    rm -rf "$U"; mkdir -p "$U/ucx-install-with-cuda/usr"
    cp -rL "$UCX_HOME/lib" "$U/ucx-install-with-cuda/usr/lib"
    cp -rL "$UCX_HOME/include" "$U/ucx-install-with-cuda/usr/include"
    (cd "$U" && tar -czf "$DIST/ucx_x86_64_cuda_12_6.tar.gz" ucx-install-with-cuda)
  fi

  # Python 3.10: system headers + libpython
  if [[ ! -f "$DIST/python_x86_64_3_10.tar.gz" ]]; then
    info "Repackaging Python 3.10 ..."
    local P="$LD/python_x86_64_3_10"
    rm -rf "$P"; mkdir -p "$P/include" \
      "$P/lib/python3.10/config-3.10-x86_64-linux-gnu"
    cp -rL /usr/include/python3.10 "$P/include/python3.10"
    cp -L /usr/lib/x86_64-linux-gnu/libpython3.10.so \
      "$P/lib/python3.10/config-3.10-x86_64-linux-gnu/libpython3.10.so"
    (cd "$P" && tar -czf "$DIST/python_x86_64_3_10.tar.gz" include lib)
  fi

  # Coverity stub (proprietary tool not needed; _coverity_* targets are no-ops)
  if [[ ! -f "$LD/coverity_stub/BUILD" ]]; then
    mkdir -p "$LD/coverity_stub"
    cat > "$LD/coverity_stub/BUILD" <<'EOF'
package(default_visibility = ["//visibility:public"])
filegroup(name = "tools", srcs = [])
EOF
  fi
}

# --- Phases ---------------------------------------------------------------------
do_build() {
  prepare_bazel
  prepare_deps
  info "Building GXF release targets (x86_64_cuda_12_6) ..."
  cd "$GXF_DIR"
  "$BAZEL" build "${BAZEL_OPTS[@]}" "${TARGETS[@]}"
  info "Build finished. Outputs at:"
  echo "  ${GXF_DIR}/bazel-bin/gxf/"
}

do_test() {
  do_build
  info "Running gxe smoke test (test_ping.yaml) ..."
  cd "$GXF_DIR"
  "$BAZEL" run "${BAZEL_OPTS[@]}" //gxf/gxe:gxe -- \
    --app=gxf/test/apps/test_ping.yaml --manifest=gxf/gxe/manifest.yaml
}

# Package the release tarball (headers + prebuilt libs + bazel release files),
# using the repository's own release/make_tarball.py, restricted to x86_64.
do_package() {
  do_build
  info "Packaging GXF release tarball (x86_cuda_12_6 only) ..."
  cd "$GXF_DIR"
  export PATH="${ROOT}/tools:${HOME}/.local/bin:${PATH}"
  rm -rf /tmp/gxf-release
  python3 release/make_tarball.py \
    "${ROOT}/gxf/build_gxf_release_content.yaml" \
    gxf_isaac_release.tar.gz /tmp/gxf-release \
    --single_platform x86_cuda_12_6 || err "make_tarball.py failed"
  mkdir -p "${ROOT}/dist"
  mv gxf_isaac_release.tar.gz "${ROOT}/dist/"
  info "Tarball available at: ${ROOT}/dist/gxf_isaac_release.tar.gz"
}

# Install headers + libs into a prefix (isaac_ros_gxf layout: include/ + lib/gxf_<platform>/)
do_install() {
  local PREFIX="${1:?Usage: $0 install <prefix>  (e.g. $0 install /opt/gxf or .../isaac_ros_gxf/gxf/core)}"
  local PATCHELF="${HOME}/.local/bin/patchelf"
  [[ -x "$PATCHELF" ]] || PATCHELF=patchelf
  [[ -f "${ROOT}/dist/gxf_isaac_release.tar.gz" ]] || do_package
  info "Installing GXF release to ${PREFIX} ..."
  local TMPD; TMPD=$(mktemp -d)
  tar -xzf "${ROOT}/dist/gxf_isaac_release.tar.gz" -C "$TMPD"
  # Public headers (same filter rules as upstream build_install_gxf_release.sh)
  mkdir -p "${PREFIX}/include"
  rsync -qrvm \
    --exclude "gxf_*/" --exclude "sample/" --exclude "gxf/sample/" \
    --exclude "gxf/python_codelet/" --exclude "gxf/ucx/" --exclude "gxf/rmm/" \
    --exclude "gxf/app/" --exclude "test/" \
    --exclude "*.sh" --exclude "*.py" --exclude "*.cpp" --exclude "*.bzl" \
    --exclude "*.yaml" --exclude "BUILD" --exclude "*.BUILD" \
    --include "*/" --include "LICENSE*" --include "*.h" --include "*.hpp" --exclude "*" \
    "${TMPD}/tmp/gxf-release/" "${PREFIX}/include" || err "header install failed"
  # Prebuilt shared libraries for this platform
  mkdir -p "${PREFIX}/lib/gxf_x86_64_cuda_12_6"
  rsync -qrvm --include "*/" --include "*.so" --exclude "*" \
    "${TMPD}/tmp/gxf-release/gxf_x86_64_cuda_12_6/" \
    "${PREFIX}/lib/gxf_x86_64_cuda_12_6" || err "lib install failed"
  # gxe executable
  cp -r "${TMPD}/tmp/gxf-release/gxf_x86_64_cuda_12_6/gxe" \
        "${PREFIX}/lib/gxf_x86_64_cuda_12_6/" 2>/dev/null || true
  # Prune manifest.yaml to extensions actually present in the tarball
  # (the stock manifest also lists extensions excluded from the release,
  #  e.g. stream/ucx/rmm, which would fail extension loading)
  local MF="${PREFIX}/lib/gxf_x86_64_cuda_12_6/gxe/manifest.yaml"
  if [[ -f "$MF" ]]; then
    local MF_TMP; MF_TMP=$(mktemp)
    grep -vE "^- " "$MF" > "$MF_TMP" || true
    grep -E "^- " "$MF" | while read -r line; do
      local rel="${line#- }"
      if [[ -f "${PREFIX}/lib/${rel}" ]]; then
        echo "$line"
      else
        info "manifest: dropping missing extension ${rel}"
      fi
    done >> "$MF_TMP"
    mv "$MF_TMP" "$MF"
  fi
  # Patch SONAMEs into shared libraries for name resolution (upstream behavior)
  info "Patching SONAME into GXF shared libraries"
  local so soname
  find "${PREFIX}" -name "libgxf_*.so" | while read -r so; do
    soname="$(basename "$so")"
    chmod +w "$so"
    "$PATCHELF" --set-soname "$soname" "$so"
  done
  rm -rf "$TMPD"
  info "Completed. Installed GXF to ${PREFIX}"
  echo "  headers: ${PREFIX}/include"
  echo "  libs:    ${PREFIX}/lib/gxf_x86_64_cuda_12_6"
}

case "${1:-build}" in
  build) do_build ;;
  test)  do_test ;;
  package) do_package ;;
  install) do_install "$2" ;;
  deps)  prepare_bazel; rm -f "$DIST"/*.tar.gz; prepare_deps; info "Deps regenerated" ;;
  clean) cd "$GXF_DIR" && "$BAZEL" clean ;;
  *) echo "Usage: $0 [build|test|package|install <prefix>|deps|clean]"; exit 1 ;;
esac
