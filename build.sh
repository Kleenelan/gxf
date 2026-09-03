#!/bin/bash -e
#
# One-click build for the NvSci-free, intranet-free GXF 4.1 (x86_64_cuda_12_6).
#
# Usage:
#   ./build.sh           Build the full Isaac ROS release target set (35 targets)
#   ./build.sh test      Build + run gxe smoke test (test_ping.yaml)
#   ./build.sh clean     bazel clean
#   ./build.sh deps      (Re)generate local dependency tarballs from this machine's
#                        CUDA, UCX and Python
#
# CUDA / UCX locations:
#   CUDA_HOME=/usr/local/cuda UCX_HOME=/opt/ucx-1.18.0 ./build.sh
#   (defaults shown; CUDA must be 12.6.x). CUDA_HOME drives everything CUDA:
#   the dependency tarball, and the nvcc toolchain repo (cuda_home_repository,
#   injected via --repo_env). Tarballs are repacked automatically when the
#   homes change.
#
# Device compiler (DEVCC):
#   DEVCC=clang++ ./build.sh     (default: $CUDA_HOME/bin/nvcc)
#   Any compiler able to compile the CUDA sources can be used; it is injected
#   into the crosstool wrapper in place of nvcc. Note the wrapper still passes
#   nvcc-style flags, so a non-nvcc DEVCC must accept them.
#
# Python version:
#   Build against Python 3.10/3.11/3.12/3.13/3.14+ by setting GXF_PYTHON, e.g.
#     GXF_PYTHON=3.12 ./build.sh        (default: version of "python3" on PATH)
#   The selected interpreter plus its dev headers and shared libpython must be
#   installed (pythonX.Y-dev / libpythonX.Y-dev on Ubuntu). pybind11 is pinned
#   automatically: 2.11.1 for <=3.12, 2.13.6 for 3.13, 3.0.1 for >=3.14.
#
# Outputs:
#   gxf_without_nvsci/com_nvidia_gxf/bazel-bin/gxf/...   (*.so, gxe, ...)
#
# Requirements: Ubuntu 22.04, gcc-11, CUDA toolkit, UCX, curl, and a
#               pythonX.Y + dev files matching GXF_PYTHON.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
GXF_DIR="${ROOT}/gxf_without_nvsci/com_nvidia_gxf"
BAZEL="${ROOT}/tools/bazel"
DIST="${ROOT}/local_deps/dist"

export target_platform=x86_64_cuda_12_6 cpu=k8 compiler=gcc-11
BAZEL_OPTS=(--config=x86_64_cuda_12_6
            --repo_env=target_platform=x86_64_cuda_12_6
            --repo_env=cpu=k8
            --repo_env=compiler=gcc-11
            # Offline deps: cuda/ucx/python http_archives use placeholder
            # URLs that are resolved from this directory by file name +
            # sha256 (Bazel --distdir mechanism).
            --distdir="${ROOT}/local_deps/dist")

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
  # NOTE: TUNA's github-release mirror does NOT carry bazelbuild; use the
  # official bazel releases bucket, with GitHub as fallback.
  local url
  for url in \
    https://releases.bazel.build/6.0.0/release/bazel-6.0.0-linux-x86_64 \
    https://github.com/bazelbuild/bazel/releases/download/6.0.0/bazel-6.0.0-linux-x86_64; do
    info "Trying $url"
    curl -fSL --connect-timeout 15 -o "$BAZEL" "$url" && break
    rm -f "$BAZEL"
  done
  # -f makes curl fail on HTTP errors (e.g. 404) instead of saving the
  # error page; also sanity-check that we got an ELF binary, not HTML.
  [[ -s "$BAZEL" ]] && [[ "$(head -c 4 "$BAZEL")" == $'\x7fELF' ]] \
    || err "Bazel download failed"
  chmod +x "$BAZEL"
}
# --- 1b. Python version selection ------------------------------------------------
# GXF_PYTHON selects the Python to build against (3.10/3.11/3.12/3.13/3.14/...).
# Default: the version of the "python3" on PATH. The interpreter (with its dev
# headers and a shared libpython) must already be installed on this machine.
resolve_python() {
  GXF_PYTHON="${GXF_PYTHON:-}"
  if [[ -z "$GXF_PYTHON" ]]; then
    command -v python3 >/dev/null || err "python3 not found; set GXF_PYTHON"
    GXF_PYTHON="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
  fi
  command -v "python${GXF_PYTHON}" >/dev/null \
    || err "python${GXF_PYTHON} not found. Install it plus its dev packages \
(e.g. apt install python${GXF_PYTHON}-dev libpython${GXF_PYTHON}-dev), \
or set GXF_PYTHON to an installed version (e.g. GXF_PYTHON=3.10 $0)."
  export GXF_PYTHON
  info "Building against Python ${GXF_PYTHON} ($(command -v "python${GXF_PYTHON}"))"
}

# --- 1c. CUDA / UCX install locations --------------------------------------------
# CUDA_HOME / UCX_HOME select the local installs to build against, e.g.
#   CUDA_HOME=/usr/local/cuda UCX_HOME=/opt/ucx-1.18.0 ./build.sh
# CUDA must be 12.6.x (the x86_64_cuda_12_6 config, the cuda_x86_64_12060
# tarball layout and the nvcc_12_06 toolchain are all tied to 12.6).
resolve_homes() {
  CUDA_HOME="$(readlink -f "${CUDA_HOME:-/usr/local/cuda}")"
  UCX_HOME="$(readlink -f "${UCX_HOME:-/opt/ucx-1.18.0}")"
  [[ -d "$CUDA_HOME" ]] || err "CUDA not found at $CUDA_HOME (set CUDA_HOME)"
  [[ -d "$UCX_HOME/lib" && -d "$UCX_HOME/include" ]] \
    || err "UCX not found at $UCX_HOME (set UCX_HOME; needs lib/ and include/)"
  local ver=""
  if [[ -f "$CUDA_HOME/version.json" ]]; then
    ver="$(grep -oP '"cuda"\s*:\s*"\K[0-9]+\.[0-9]+' "$CUDA_HOME/version.json" | head -1)"
  fi
  if [[ -z "$ver" && -x "$CUDA_HOME/bin/nvcc" ]]; then
    ver="$("$CUDA_HOME/bin/nvcc" --version | grep -oP 'release \K[0-9]+\.[0-9]+' | head -1)"
  fi
  if [[ -n "$ver" && "$ver" != "12.6" ]]; then
    err "CUDA at $CUDA_HOME is $ver, but this build (x86_64_cuda_12_6) requires CUDA 12.6.x"
  fi

  # DEVCC: device-side CUDA compiler injected into the crosstool wrapper.
  # Defaults to the nvcc inside CUDA_HOME; override to use any compiler able
  # to compile the CUDA sources, e.g. DEVCC=clang++ ./build.sh
  DEVCC="${DEVCC:-$CUDA_HOME/bin/nvcc}"
  if [[ "$DEVCC" == */* ]]; then
    [[ -x "$DEVCC" ]] || err "DEVCC=$DEVCC not found or not executable"
  else
    command -v "$DEVCC" >/dev/null || err "DEVCC=$DEVCC not found on PATH"
  fi

  export CUDA_HOME UCX_HOME DEVCC
  BAZEL_OPTS+=(--repo_env=CUDA_HOME="$CUDA_HOME" --repo_env=DEVCC="$DEVCC")
  info "CUDA_HOME=$CUDA_HOME${ver:+ (CUDA $ver)}"
  info "UCX_HOME=$UCX_HOME"
  info "DEVCC=$DEVCC"
}

# --- 2. Local dependency tarballs (CUDA / UCX / Python) -------------------------
prepare_deps() {
  mkdir -p "$DIST"
  local LD="${ROOT}/local_deps"

  # CUDA: repackage local toolkit into the layout expected by cuda_x86_64_12060.BUILD.
  # Repack whenever CUDA_HOME changes (recorded in cuda_home.txt in the tarball).
  if [[ ! -f "$DIST/cuda_x86_64_12060.tar.gz" ]] || \
     [[ "$(tar -xzOf "$DIST/cuda_x86_64_12060.tar.gz" cuda_home.txt 2>/dev/null)" != "$CUDA_HOME" ]]; then
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
    echo "$CUDA_HOME" > "$SLIM/cuda_home.txt"
    (cd "$SLIM" && tar -czf "$DIST/cuda_x86_64_12060.tar.gz" usr cuda_home.txt)
    rm -rf "$SLIM"
  fi

  # UCX: repackage local install into ucx-install-with-cuda/usr layout.
  # Repack whenever UCX_HOME changes (recorded in ucx_home.txt in the tarball).
  if [[ ! -f "$DIST/ucx_x86_64_cuda_12_6.tar.gz" ]] || \
     [[ "$(tar -xzOf "$DIST/ucx_x86_64_cuda_12_6.tar.gz" ucx_home.txt 2>/dev/null)" != "$UCX_HOME" ]]; then
    info "Repackaging UCX from $UCX_HOME ..."
    local U="$LD/ucx_x86_64_cuda_12_6"
    rm -rf "$U"; mkdir -p "$U/ucx-install-with-cuda/usr"
    cp -rL "$UCX_HOME/lib" "$U/ucx-install-with-cuda/usr/lib"
    cp -rL "$UCX_HOME/include" "$U/ucx-install-with-cuda/usr/include"
    echo "$UCX_HOME" > "$U/ucx_home.txt"
    (cd "$U" && tar -czf "$DIST/ucx_x86_64_cuda_12_6.tar.gz" ucx-install-with-cuda ucx_home.txt)
  fi

  # Python (version selected by GXF_PYTHON): headers + shared libpython, in a
  # version-agnostic layout (include/ + lib/) consumed by python_local.BUILD.
  # Repack whenever the selected version differs from the packaged one
  # (the packaged version is recorded in python_version.txt inside the tarball).
  local PYBIN="python${GXF_PYTHON}"
  if [[ ! -f "$DIST/python_local.tar.gz" ]] || \
     [[ "$(tar -xzOf "$DIST/python_local.tar.gz" python_version.txt 2>/dev/null)" != "$GXF_PYTHON" ]]; then
    info "Repackaging Python ${GXF_PYTHON} (headers + libpython) ..."
    local P="$LD/python_local"
    rm -rf "$P"; mkdir -p "$P/include" "$P/lib"
    local INC LIBDIR LDLIB MULTIARCH
    INC="$("$PYBIN" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
    LIBDIR="$("$PYBIN" -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")')"
    LDLIB="$("$PYBIN" -c 'import sysconfig; print(sysconfig.get_config_var("LDLIBRARY") or "")')"
    MULTIARCH="$("$PYBIN" -c 'import sysconfig; print(sysconfig.get_config_var("MULTIARCH") or "")')"
    [[ -d "$INC" ]] \
      || err "Python ${GXF_PYTHON} headers not found (install python${GXF_PYTHON}-dev)"
    [[ -n "$LIBDIR" && "$LDLIB" == *.so* && -f "$LIBDIR/$LDLIB" ]] \
      || err "shared libpython for ${GXF_PYTHON} not found (${LIBDIR}/${LDLIB}). \
Install libpython${GXF_PYTHON}-dev or use an interpreter built with --enable-shared."
    cp -rL "$INC/." "$P/include/"
    # Debian/Ubuntu: the real pyconfig.h lives in the multiarch dir while the
    # main include dir only has a wrapper; merge it so the tarball is
    # self-contained (merge AFTER the main include so the real file wins).
    if [[ -n "$MULTIARCH" && -d "/usr/include/${MULTIARCH}/python${GXF_PYTHON}" ]]; then
      cp -rL "/usr/include/${MULTIARCH}/python${GXF_PYTHON}/." "$P/include/"
    fi
    cp -L "$LIBDIR/$LDLIB" "$P/lib/$LDLIB"
    echo "$GXF_PYTHON" > "$P/python_version.txt"
    (cd "$P" && tar -czf "$DIST/python_local.tar.gz" include lib python_version.txt)
    rm -rf "$P"
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

# --- 0. Keep .bzl sha256 pins in sync with the local tarballs -------------------
# The cuda/ucx/python repo rules use placeholder URLs + sha256, resolved via
# --distdir from ${ROOT}/local_deps/dist (no machine-specific paths anywhere).
# If the tarballs are (re)generated, their sha256 changes; update the sha256
# line right after each placeholder URL so the pins never go stale.
sync_dep_integrity() {
  local file name sha
  while read -r file name; do
    [[ -f "${DIST}/${name}" ]] || continue
    sha="$(sha256sum "${DIST}/${name}" | cut -d' ' -f1)"
    awk -v sha="$sha" -v url="local.invalid/local_deps/${name}" '
      $0 ~ url { pending = 1 }
      pending && /sha256 = "[0-9a-f]{64}"/ {
        sub(/sha256 = "[0-9a-f]{64}"/, "sha256 = \"" sha "\"")
        pending = 0
      }
      { print }
    ' "$file" > "$file.tmp" && mv "$file.tmp" "$file"
  done <<EOF
${GXF_DIR}/third_party/cuda.bzl cuda_x86_64_12060.tar.gz
${GXF_DIR}/third_party/gxf.bzl python_local.tar.gz
${GXF_DIR}/third_party/ucx/ucx.bzl ucx_x86_64_cuda_12_6.tar.gz
EOF
}

# --- 0b. Pin pybind11 to a version compatible with the selected Python --------
# pybind11 2.11.1 supports Python <= 3.12; 2.13.6 adds 3.13; 3.0.1 adds 3.14.
# Rewrite the url/strip_prefix/sha256 of the pybind11 repo rule in gxf.bzl.
sync_pybind11() {
  local min="${GXF_PYTHON##*.}" ver sha
  if (( 10#$min >= 14 )); then
    ver="3.0.1";  sha="741633da746b7c738bb71f1854f957b9da660bcd2dce68d71949037f0969d0ca"
  elif (( 10#$min >= 13 )); then
    ver="2.13.6"; sha="e08cb87f4773da97fa7b5f035de8763abc656d87d5773e62f6da0587d1f0ec20"
  else
    ver="2.11.1"; sha="d475978da0cdc2d43b73f30910786759d593a9d8ee05b1b6846d1eb16c6d2e0c"
  fi
  awk -v ver="$ver" -v sha="$sha" '
    /name = "pybind11"/ { inblock = 1 }
    inblock && /sha256 = "[0-9a-f]{64}"/ { sub(/"[0-9a-f]{64}"/, "\"" sha "\"") }
    inblock && /strip_prefix = "pybind11-[^"]*"/ { sub(/pybind11-[^"]*/, "pybind11-" ver) }
    inblock && /url = "https:\/\/github.com\/pybind\/pybind11\/archive\/refs\/tags\/v[^"]*\.tar\.gz"/ {
      sub(/tags\/v[^"]*\.tar\.gz/, "tags/v" ver ".tar.gz")
      inblock = 0
    }
    { print }
  ' "${GXF_DIR}/third_party/gxf.bzl" > "${GXF_DIR}/third_party/gxf.bzl.tmp" \
    && mv "${GXF_DIR}/third_party/gxf.bzl.tmp" "${GXF_DIR}/third_party/gxf.bzl"
}

# --- Phases ---------------------------------------------------------------------
do_build() {
  prepare_bazel
  resolve_python
  resolve_homes
  prepare_deps
  sync_dep_integrity
  sync_pybind11
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
    "${ROOT}/gxf_without_nvsci/build_gxf_release_content.yaml" \
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
  deps)  prepare_bazel; resolve_python; resolve_homes; rm -f "$DIST"/*.tar.gz; prepare_deps; sync_dep_integrity; sync_pybind11; info "Deps regenerated" ;;
  clean) cd "$GXF_DIR" && "$BAZEL" clean ;;
  *) echo "Usage: $0 [build|test|package|install <prefix>|deps|clean]"; exit 1 ;;
esac
