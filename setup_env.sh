#!/usr/bin/env bash
#
# setup_env.sh — GXF build environment setup (replaces bazel's dependency
# download and environment configuration).
#
# This is part 1 of the new build workflow:
#   1) source ./setup_env.sh   —— downloads/builds all third-party deps into
#      ./deps and exports CUDA_HOME / UCX_HOME / GXF_DEPS_PREFIX etc.;
#   2) make                    —— builds GXF with those variables (see the
#      Makefile at the repository root).
#
# Usage:
#   source ./setup_env.sh          set up the environment (idempotent;
#                                  completed steps are skipped automatically)
#   source ./setup_env.sh clean    remove everything downloaded/built under ./deps
#   ./setup_env.sh                 may also be executed directly (deps are
#                                  still built, but the exported variables do
#                                  not persist in your shell; a hint to
#                                  re-source is printed at the end)
#
# Defaults overridable via environment variables:
#   CUDA_HOME=/usr/local/cuda        CUDA 12.6.x install dir (must be 12.6.x)
#   UCX_HOME=/opt/ucx-1.18.0         UCX install dir (must contain lib/ and include/)
#   DEVCC=$CUDA_HOME/bin/nvcc        CUDA device compiler
#   GXF_PYTHON=<python3 version>     e.g. 3.10/3.11/3.12/3.13/3.14
#   GXF_DEPS_DIR=<repo>/deps         root dir for dep download/build/install
#   GXF_JOBS=<nproc>                 parallel jobs for building deps
#
# All dependencies come from official open-source URLs (github releases /
# official mirrors), at the same versions pinned by bazel in the .bzl files:
#   yaml-cpp 0.6.3, gflags(e292e04), breakpad(bae713b)+lss(93426bd),
#   magic_enum 0.9.3, dlpack 0.8, nlohmann-json 3.10.5, pybind11 (per Python),
#   gRPC 1.48.0 (bundles protobuf 21.7/abseil/re2/c-ares/zlib/boringssl),
#   cpprestsdk 2.10.18, boost 1.80 (built from source only when the system
#   has no boost >= 1.74).
#

# Must be able to return (exit would close the user's shell when sourced)
if (return 0 2>/dev/null); then
  _GXF_SOURCED=1
else
  _GXF_SOURCED=0
fi
_gxf_ret() {  # uniform return: `return` when sourced, `exit` when executed
  if [[ "$_GXF_SOURCED" == 1 ]]; then return "$1"; else exit "$1"; fi
}

# ---------------------------------------------------------------------------
# 0. Basic paths and defaults
# ---------------------------------------------------------------------------
GXF_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
export GXF_ROOT
export GXF_WS="${GXF_ROOT}/com_nvidia_gxf"           # GXF source (bazel workspace) root
export GXF_DEPS_DIR="${GXF_DEPS_DIR:-${GXF_ROOT}/deps}"
export GXF_DEPS_PREFIX="${GXF_DEPS_DIR}/install"     # dep install prefix (include/lib/bin)
export GXF_GEN_DIR="${GXF_DEPS_DIR}/gen"             # where the Makefile generates protobuf code
export GXF_PROTO_DIR="${GXF_DEPS_DIR}/proto"         # .proto files awaiting codegen
export GXF_JOBS="${GXF_JOBS:-$(nproc)}"

_DIST="${GXF_DEPS_DIR}/dist"                          # downloaded archives
_SRC="${GXF_DEPS_DIR}/src"                            # extracted/cloned sources
_BLD="${GXF_DEPS_DIR}/build"                          # cmake build dirs
_STAMPS="${GXF_DEPS_DIR}/.stamps"                     # completion stamps

_gxf_info() { echo -e "\e[32m[setup_env]\e[0m $*"; }
_gxf_warn() { echo -e "\e[33m[setup_env]\e[0m $*" >&2; }
_gxf_err()  { echo -e "\e[31m[setup_env ERROR]\e[0m $*" >&2; return 1; }

if [[ "${1:-}" == "clean" ]]; then
  _gxf_info "Removing ${GXF_DEPS_DIR} ..."
  rm -rf "${GXF_DEPS_DIR}"
  _gxf_ret 0
fi

# ---------------------------------------------------------------------------
# 1. System tool check
# ---------------------------------------------------------------------------
_gxf_check_tools() {
  local missing=() t
  for t in git curl patch make cmake ar rsync tar unzip; do
    command -v "$t" >/dev/null || missing+=("$t")
  done
  command -v g++-11 >/dev/null || command -v g++ >/dev/null || missing+=("g++-11")
  command -v ld.gold >/dev/null || missing+=("binutils-gold")
  [[ -f /usr/include/openssl/ssl.h ]] || missing+=("libssl-dev")
  if ((${#missing[@]})); then
    _gxf_err "Missing tools/packages: ${missing[*]}
Install them e.g.: sudo apt install build-essential gcc-11 g++-11 binutils-gold \
cmake git curl patch rsync tar unzip libssl-dev"
  fi
}

# ---------------------------------------------------------------------------
# 2. CUDA / UCX / Python / DEVCC
# ---------------------------------------------------------------------------
_gxf_resolve_homes() {
  CUDA_HOME="$(readlink -f "${CUDA_HOME:-/usr/local/cuda}")"
  UCX_HOME="$(readlink -f "${UCX_HOME:-/opt/ucx-1.18.0}")"
  [[ -d "$CUDA_HOME" ]] || { _gxf_err "CUDA not found at $CUDA_HOME (set CUDA_HOME)"; return 1; }
  [[ -d "$UCX_HOME/lib" && -d "$UCX_HOME/include" ]] \
    || { _gxf_err "UCX not found at $UCX_HOME (set UCX_HOME; needs lib/ and include/)"; return 1; }
  local ver=""
  if [[ -f "$CUDA_HOME/version.json" ]]; then
    ver="$(grep -oP '"cuda"\s*:\s*"\K[0-9]+\.[0-9]+' "$CUDA_HOME/version.json" | head -1)"
  fi
  if [[ -z "$ver" && -x "$CUDA_HOME/bin/nvcc" ]]; then
    ver="$("$CUDA_HOME/bin/nvcc" --version | grep -oP 'release \K[0-9]+\.[0-9]+' | head -1)"
  fi
  if [[ -n "$ver" && "$ver" != "12.6" ]]; then
    _gxf_err "CUDA at $CUDA_HOME is $ver, but this build (x86_64_cuda_12_6) requires CUDA 12.6.x"
    return 1
  fi
  DEVCC="${DEVCC:-$CUDA_HOME/bin/nvcc}"
  if [[ "$DEVCC" == */* ]]; then
    [[ -x "$DEVCC" ]] || { _gxf_err "DEVCC=$DEVCC not found or not executable"; return 1; }
  else
    command -v "$DEVCC" >/dev/null || { _gxf_err "DEVCC=$DEVCC not found on PATH"; return 1; }
  fi
  export CUDA_HOME UCX_HOME DEVCC
  export CUDA_LIB64="${CUDA_HOME}/lib64"
  _gxf_info "CUDA_HOME=$CUDA_HOME${ver:+ (CUDA $ver)}"
  _gxf_info "UCX_HOME=$UCX_HOME"
  _gxf_info "DEVCC=$DEVCC"
}

_gxf_resolve_python() {
  GXF_PYTHON="${GXF_PYTHON:-}"
  if [[ -z "$GXF_PYTHON" ]]; then
    command -v python3 >/dev/null || { _gxf_err "python3 not found; set GXF_PYTHON"; return 1; }
    GXF_PYTHON="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
  fi
  local pybin="python${GXF_PYTHON}"
  command -v "$pybin" >/dev/null \
    || { _gxf_err "$pybin not found. Install it plus its dev packages \
(apt install python${GXF_PYTHON}-dev libpython${GXF_PYTHON}-dev), \
or set GXF_PYTHON to an installed version."; return 1; }
  export GXF_PYTHON
  export PYTHON_INCLUDE="$("$pybin" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
  export PYTHON_LIBDIR="$("$pybin" -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")')"
  export PYTHON_LIB="python${GXF_PYTHON}"
  local ldlib
  ldlib="$("$pybin" -c 'import sysconfig; print(sysconfig.get_config_var("LDLIBRARY") or "")')"
  [[ -d "$PYTHON_INCLUDE" && -f "$PYTHON_INCLUDE/Python.h" ]] \
    || { _gxf_err "Python ${GXF_PYTHON} headers not found (install python${GXF_PYTHON}-dev)"; return 1; }
  [[ -n "$ldlib" && "$ldlib" == *.so* && -f "$PYTHON_LIBDIR/$ldlib" ]] \
    || { _gxf_err "shared libpython for ${GXF_PYTHON} not found (${PYTHON_LIBDIR}/${ldlib}). \
Install libpython${GXF_PYTHON}-dev or use an interpreter built with --enable-shared."; return 1; }
  _gxf_info "Building against Python ${GXF_PYTHON} ($(command -v "$pybin"))"
}

# ---------------------------------------------------------------------------
# 3. Download/extract helpers
# ---------------------------------------------------------------------------
# _gxf_fetch <name> <url> [sha256]  -> prints the extracted source dir
# (logs go to stderr; only the path is written to stdout for $(...) capture)
_gxf_fetch() {
  local name="$1" url="$2" sha="${3:-}"
  local file="${_DIST}/$(basename "$url")"
  mkdir -p "$_DIST" "$_SRC"
  if [[ ! -f "$file" ]]; then
    echo -e "\e[32m[setup_env]\e[0m Downloading $name <- $url" >&2
    curl -fSL --connect-timeout 20 --retry 3 -o "$file" "$url" || return 1
    if [[ -z "$sha" ]]; then
      echo -e "\e[33m[setup_env]\e[0m $name sha256: $(sha256sum "$file" | cut -d' ' -f1) (unpinned)" >&2
    fi
  fi
  if [[ -n "$sha" ]]; then
    echo "$sha  $file" | sha256sum -c - >/dev/null \
      || { _gxf_err "sha256 mismatch for $file"; return 1; }
  fi
  local out="${_SRC}/${name}"
  if [[ ! -d "$out" ]]; then
    echo -e "\e[32m[setup_env]\e[0m Extracting $name" >&2
    local tmp="${out}.extracting"
    rm -rf "$tmp"; mkdir -p "$tmp"
    # Detect the format by magic bytes (some mirror URLs have no extension)
    local magic; magic="$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')"
    case "$magic" in
      504b*)        unzip -q "$file" -d "$tmp" ;;              # PK..  zip
      1f8b*)        tar -xzf "$file" -C "$tmp" ;;              # gzip
      fd377a585a*)  tar -xJf "$file" -C "$tmp" ;;              # xz
      *)            rm -rf "$tmp"; _gxf_err "unknown archive type: $file (magic=$magic)"; return 1 ;;
    esac || { rm -rf "$tmp"; return 1; }
    # Strip a single top-level directory (using only atomic single-dir renames)
    local entries=("$tmp"/*)
    if [[ ${#entries[@]} == 1 && -d "${entries[0]}" ]]; then
      mv "${entries[0]}" "${_SRC}/.${name}.final"
      rm -rf "$tmp"
      mv "${_SRC}/.${name}.final" "$out"
    else
      mv "$tmp" "$out"
    fi
  fi
  echo "$out"
}

# _gxf_done <stamp> / _gxf_mark <stamp>
_gxf_done() { [[ -f "${_STAMPS}/$1" ]]; }
_gxf_mark() { mkdir -p "$_STAMPS"; date > "${_STAMPS}/$1"; }

# ---------------------------------------------------------------------------
# 4. Third-party dependencies
# ---------------------------------------------------------------------------

# lss: header + lss_gcc.patch (needed by breakpad, indirectly by gxe)
_gxf_dep_lss() {
  _gxf_done lss && return 0
  local d
  d=$(_gxf_fetch lss "https://chromium.googlesource.com/linux-syscall-support/+archive/93426bda6535943ff1525d0460aab5cc0870ccaf.tar.gz") || return 1
  patch -d "$d" -p0 < "${GXF_WS}/third_party/lss_gcc.patch" || return 1
  mkdir -p "${GXF_DEPS_PREFIX}/include/third_party/lss"
  cp "$d/linux_syscall_support.h" "${GXF_DEPS_PREFIX}/include/third_party/lss/"
  _gxf_mark lss
}

# yaml-cpp 0.6.3 -> static libyaml-cpp.a
_gxf_dep_yaml_cpp() {
  _gxf_done yaml-cpp && return 0
  local d b
  d=$(_gxf_fetch yaml-cpp-0.6.3 "https://developer.nvidia.com/isaac/download/third_party/yaml-cpp-0-6-3-tar-gz" \
    f38a7a7637993943c4c890e352b1fa3f3bf420535634e9a506d9a21c3890d505) || return 1
  b="${_BLD}/yaml-cpp"
  cmake -S "$d" -B "$b" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="${GXF_DEPS_PREFIX}" || return 1
  cmake --build "$b" -j "$GXF_JOBS" || return 1
  cmake --install "$b" || return 1
  _gxf_mark yaml-cpp
}

# gflags -> static libgflags.a (the cmake install produces headers equivalent
# to bazel's genrules)
_gxf_dep_gflags() {
  _gxf_done gflags && return 0
  local d b
  d=$(_gxf_fetch gflags-e292e04 "https://developer.nvidia.com/isaac/download/third_party/gflags-e292e0452fcfd5a8ae055b59052fc041cbab4abf-tar-gz" \
    a4c5171355e67268b4fd2f31c3f7f2d125683d12e0686fc14893a3ca8c803659) || return 1
  b="${_BLD}/gflags"
  cmake -S "$d" -B "$b" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DGFLAGS_REGISTER_INSTALL_PREFIX=OFF -DGFLAGS_BUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="${GXF_DEPS_PREFIX}" || return 1
  cmake --build "$b" -j "$GXF_JOBS" || return 1
  cmake --install "$b" || return 1
  _gxf_mark gflags
}

# breakpad(bae713b) + lss -> static libbreakpad.a (compiled directly from the
# 22 source files listed in third_party/breakpad.BUILD, exactly like bazel;
# headers installed preserving the src/ layout)
_gxf_dep_breakpad() {
  _gxf_done breakpad && return 0
  _gxf_dep_lss || return 1
  local d b
  d=$(_gxf_fetch breakpad-bae713b "https://github.com/google/breakpad/archive/bae713be2e51faa5cbe0ac4bcd21c0a3ee72ff8e.tar.gz" \
    65a0dd6db9065dc539ddf35f969d10b5ad8a7b2c305d2dc5a66a1f8d46f4a904) || return 1
  # Put the lss header into src/third_party/lss (breakpad sources reference
  # it as "third_party/lss/...")
  mkdir -p "$d/src/third_party/lss"
  cp "${GXF_DEPS_PREFIX}/include/third_party/lss/linux_syscall_support.h" "$d/src/third_party/lss/"
  b="${_BLD}/breakpad"; rm -rf "$b"; mkdir -p "$b"
  local srcs=(
    client/linux/crash_generation/crash_generation_client.cc
    client/linux/dump_writer_common/thread_info.cc
    client/linux/dump_writer_common/ucontext_reader.cc
    client/linux/handler/exception_handler.cc
    client/linux/handler/minidump_descriptor.cc
    client/linux/log/log.cc
    client/linux/microdump_writer/microdump_writer.cc
    client/linux/minidump_writer/linux_dumper.cc
    client/linux/minidump_writer/linux_ptrace_dumper.cc
    client/linux/minidump_writer/minidump_writer.cc
    client/linux/minidump_writer/pe_file.cc
    client/minidump_file_writer.cc
    common/convert_UTF.cc
    common/linux/elfutils.cc
    common/linux/file_id.cc
    common/linux/guid_creator.cc
    common/linux/linux_libc_support.cc
    common/linux/memory_mapped_file.cc
    common/linux/safe_readlink.cc
    common/md5.cc
    common/simple_string_dictionary.cc
    common/string_conversion.cc
  )
  _gxf_info "Compiling breakpad (${#srcs[@]} files)"
  local objs=() f o
  for f in "${srcs[@]}"; do
    o="${b}/$(basename "${f%.cc}").o"
    g++ -std=c++17 -O2 -fPIC -DHAVE_GETCONTEXT -Wno-maybe-uninitialized -Wno-array-bounds \
        -I "$d/src" -c "$d/src/$f" -o "$o" || return 1
    objs+=("$o")
  done
  ar rcsD "${GXF_DEPS_PREFIX}/lib/libbreakpad.a" "${objs[@]}" || return 1
  # Headers (preserve the src/ relative layout; gxe.cpp includes
  # "client/linux/handler/exception_handler.h")
  mkdir -p "${GXF_DEPS_PREFIX}/include/breakpad"
  rsync -am --include='*/' --include='*.h' --exclude='*' \
    "$d/src/" "${GXF_DEPS_PREFIX}/include/breakpad/" || return 1
  _gxf_mark breakpad
}

# Header-only deps: magic_enum / dlpack / nlohmann-json / pybind11
_gxf_dep_headers() {
  _gxf_done hdrs && return 0
  local d
  d=$(_gxf_fetch magic_enum-0.9.3 "https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.3.zip" \
    2ac5f5f0591c8f587b53b89c3ef64c85cc24ebaaa389a659c6bf36a0aa192fe6) || return 1
  cp "$d"/include/magic_enum*.hpp "${GXF_DEPS_PREFIX}/include/"

  d=$(_gxf_fetch dlpack-0.8 "https://github.com/dmlc/dlpack/archive/refs/tags/v0.8.tar.gz" \
    cf965c26a5430ba4cc53d61963f288edddcd77443aa4c85ce722aaf1e2f29513) || return 1
  cp -r "$d/include/dlpack" "${GXF_DEPS_PREFIX}/include/"

  d=$(_gxf_fetch nlohmann-json-3.10.5 "https://github.com/nlohmann/json/archive/refs/tags/v3.10.5.zip" \
    ea4b0084709fb934f92ca0a68669daa0fe6f2a2c6400bf353454993a834bb0bb) || return 1
  cp -r "$d/single_include/nlohmann" "${GXF_DEPS_PREFIX}/include/"

  # pybind11 version follows the Python version (same policy as build.sh)
  local min="${GXF_PYTHON##*.}" ver sha
  if (( 10#$min >= 14 )); then
    ver="3.0.1";  sha="741633da746b7c738bb71f1854f957b9da660bcd2dce68d71949037f0969d0ca"
  elif (( 10#$min >= 13 )); then
    ver="2.13.6"; sha="e08cb87f4773da97fa7b5f035de8763abc656d87d5773e62f6da0587d1f0ec20"
  else
    ver="2.11.1"; sha="d475978da0cdc2d43b73f30910786759d593a9d8ee05b1b6846d1eb16c6d2e0c"
  fi
  d=$(_gxf_fetch "pybind11-${ver}" "https://github.com/pybind/pybind11/archive/refs/tags/v${ver}.tar.gz" "$sha") || return 1
  cp -r "$d/include/pybind11" "${GXF_DEPS_PREFIX}/include/"
  _gxf_mark hdrs
}

# boost: prefer the system boost (>= 1.74 with the required libs); otherwise
# build 1.80 from source (static, PIC)
_gxf_dep_boost() {
  if [[ -f /usr/include/boost/version.hpp ]] && \
     [[ "$(grep -oP 'BOOST_LIB_VERSION "\K[0-9]+_[0-9]+' /usr/include/boost/version.hpp | head -1)" > "1_73" ]] && \
     ls /usr/lib/x86_64-linux-gnu/libboost_system.* >/dev/null 2>&1; then
    export BOOST_INCLUDE_DIR=/usr/include
    export BOOST_LIBRARY_DIR=/usr/lib/x86_64-linux-gnu
    _gxf_info "Using system boost ($(grep -oP 'BOOST_LIB_VERSION "\K[^"]+' /usr/include/boost/version.hpp))"
    _gxf_mark boost
    return 0
  fi
  _gxf_done boost && {
    export BOOST_INCLUDE_DIR="${_SRC}/boost_1_80_0"
    export BOOST_LIBRARY_DIR="${_SRC}/boost_1_80_0/stage/lib"
    return 0
  }
  _gxf_info "No suitable system boost found; building boost 1.80.0 from source (one-time)"
  local d
  d=$(_gxf_fetch boost_1_80_0 "https://archives.boost.io/release/1.80.0/source/boost_1_80_0.tar.gz" \
    4b2136f98bdd1f5857f1c3dea9ac2018effe65286cf251534b6ae20cc45e1847) || return 1
  (cd "$d" && ./bootstrap.sh --with-libraries=system,thread,chrono,atomic,regex,date_time,filesystem,random,container \
    && ./b2 -j "$GXF_JOBS" variant=release link=static threading=multi cxxflags=-fPIC stage) || return 1
  export BOOST_INCLUDE_DIR="$d"
  export BOOST_LIBRARY_DIR="$d/stage/lib"
  _gxf_mark boost
}

# gRPC 1.48.0 (git submodules bundle protobuf 21.7 / abseil / re2 / c-ares /
# zlib / boringssl). All static, PIC, installed into GXF_DEPS_PREFIX;
# protoc and grpc_cpp_plugin are installed as well.
_gxf_dep_grpc() {
  _gxf_done grpc && return 0
  local d="${_SRC}/grpc-1.48.0" b
  if [[ ! -d "$d" ]]; then
    _gxf_info "Cloning gRPC v1.48.0 (with submodules, ~300MB, one-time) ..."
    git clone --depth 1 --branch v1.48.0 \
      --recurse-submodules --shallow-submodules --jobs 8 \
      https://github.com/grpc/grpc "$d" || return 1
  fi
  b="${_BLD}/grpc"
  cmake -S "$d" -B "$b" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="${GXF_DEPS_PREFIX}" -DCMAKE_INSTALL_LIBDIR=lib \
    -DgRPC_BUILD_TESTS=OFF -DgRPC_BUILD_CSHARP_EXT=OFF -DgRPC_INSTALL=ON \
    -DgRPC_ABSL_PROVIDER=module -DgRPC_CARES_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=module -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=module -DgRPC_ZLIB_PROVIDER=module \
    -DgRPC_BENCHMARK_PROVIDER=none || return 1
  _gxf_info "Building gRPC 1.48.0 (this takes a while, one-time) ..."
  cmake --build "$b" -j "$GXF_JOBS" || return 1
  cmake --install "$b" || return 1
  # health.proto (needed by gxf/ipc/grpc; an installed gRPC does not ship
  # it, so copy it from the source tree)
  mkdir -p "${GXF_PROTO_DIR}/src/proto/grpc/health/v1"
  cp "$d/src/proto/grpc/health/v1/health.proto" "${GXF_PROTO_DIR}/src/proto/grpc/health/v1/"
  _gxf_mark grpc
}

# cpprestsdk 2.10.18 (static, PIC, websockets excluded; uses the system OpenSSL)
_gxf_dep_cpprestsdk() {
  _gxf_done cpprestsdk && return 0
  _gxf_dep_boost || return 1
  local d b
  d=$(_gxf_fetch cpprestsdk-2.10.18 "https://github.com/microsoft/cpprestsdk/archive/refs/tags/2.10.18.tar.gz" \
    6bd74a637ff182144b6a4271227ea8b6b3ea92389f88b25b215e6f94fd4d41cb) || return 1
  b="${_BLD}/cpprestsdk"
  cmake -S "$d" -B "$b" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="${GXF_DEPS_PREFIX}" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCPPREST_EXCLUDE_WEBSOCKETS=ON -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTS=OFF -DBUILD_SAMPLES=OFF -DWERROR=OFF \
    -DBOOST_ROOT="${BOOST_INCLUDE_DIR}" -DBoost_INCLUDE_DIR="${BOOST_INCLUDE_DIR}" \
    -DBoost_LIBRARY_DIR="${BOOST_LIBRARY_DIR}" || return 1
  cmake --build "$b" -j "$GXF_JOBS" || return 1
  cmake --install "$b" || return 1
  _gxf_mark cpprestsdk
}

# ---------------------------------------------------------------------------
# 5. Final exports
# ---------------------------------------------------------------------------
_gxf_export_env() {
  export PATH="${GXF_DEPS_PREFIX}/bin:${PATH}"
  if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
    export LD_LIBRARY_PATH="${CUDA_LIB64}:${UCX_HOME}/lib:${GXF_DEPS_PREFIX}/lib:${LD_LIBRARY_PATH}"
  else
    export LD_LIBRARY_PATH="${CUDA_LIB64}:${UCX_HOME}/lib:${GXF_DEPS_PREFIX}/lib"
  fi
}

_gxf_main() {
  _gxf_check_tools || return 1
  _gxf_resolve_homes || return 1
  _gxf_resolve_python || return 1
  mkdir -p "$GXF_DEPS_PREFIX/include" "$GXF_DEPS_PREFIX/lib" "$GXF_DEPS_PREFIX/bin"

  _gxf_dep_lss        || return 1
  _gxf_dep_yaml_cpp   || return 1
  _gxf_dep_gflags     || return 1
  _gxf_dep_breakpad   || return 1
  _gxf_dep_headers    || return 1
  _gxf_dep_boost      || return 1
  _gxf_dep_grpc       || return 1
  _gxf_dep_cpprestsdk || return 1
  _gxf_export_env

  # The boost variables (system or from-source) must be exported on every
  # source, not just after a fresh build
  if [[ -z "${BOOST_INCLUDE_DIR:-}" ]]; then
    if [[ -f /usr/include/boost/version.hpp ]] && \
       ls /usr/lib/x86_64-linux-gnu/libboost_system.* >/dev/null 2>&1; then
      export BOOST_INCLUDE_DIR=/usr/include
      export BOOST_LIBRARY_DIR=/usr/lib/x86_64-linux-gnu
    else
      export BOOST_INCLUDE_DIR="${_SRC}/boost_1_80_0"
      export BOOST_LIBRARY_DIR="${_SRC}/boost_1_80_0/stage/lib"
    fi
  fi

  echo
  _gxf_info "Environment ready. Key variables:"
  cat <<EOF
  GXF_DEPS_PREFIX = ${GXF_DEPS_PREFIX}
  CUDA_HOME       = ${CUDA_HOME}
  UCX_HOME        = ${UCX_HOME}
  DEVCC           = ${DEVCC}
  GXF_PYTHON      = ${GXF_PYTHON}
  BOOST_INCLUDE_DIR / BOOST_LIBRARY_DIR set for cpprestsdk
EOF
  _gxf_info "Now run:  make -j${GXF_JOBS}        # in ${GXF_ROOT}"
  if [[ "$_GXF_SOURCED" != 1 ]]; then
    _gxf_warn "Script was executed, not sourced — the variables above are NOT"
    _gxf_warn "in your current shell. Run:  source ./setup_env.sh"
  fi
}

_gxf_main "$@"
_gxf_ret $?
