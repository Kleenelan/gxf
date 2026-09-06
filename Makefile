# Makefile — GXF build (replaces the compile/link part of bazel).
#
# Workflow:
#   source ./setup_env.sh   # part 1: download/build deps and export env vars
#   make -j$(nproc)         # part 2: build GXF (this file)
#
# Targets:
#   make all        all 35 release targets (15 extension/core .so + 10 pybind
#                   modules + gxe + static .lo archives needed for packaging)
#   make <module>   core / std / gxe / logger / sample / behavior_tree /
#                   serialization / network / multimedia / npp / cuda /
#                   test_cuda / test_ext / grpc / http / python_codelet / pybinds
#   make test       build and run the gxe smoke test (test_ping.yaml)
#   make package    build the release tarball with release/make_tarball.py
#                   (src_lib redirected to bin-make)
#   make clean      remove com_nvidia_gxf/bin-make
#
# Linux only (x86_64, CUDA 12.6/12.8). All build parameters (compiler flags,
# target composition) are taken from the actual bazel build artifacts
# (toolchain crosstool + link params files), guaranteeing equivalence
# with the bazel build.

# ---------------------------------------------------------------------------
# 0. Paths and tools
# ---------------------------------------------------------------------------
GXF_ROOT  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
WS        := $(GXF_ROOT)/com_nvidia_gxf
BUILD     := $(WS)/bin-make
OBJ       := $(BUILD)/obj

# Environment variables (exported by setup_env.sh; sane defaults are given
# here in case it was not sourced — env-check will catch invalid setups)
GXF_DEPS_DIR    ?= $(GXF_ROOT)/deps
GXF_DEPS_PREFIX ?= $(GXF_DEPS_DIR)/install
GXF_GEN_DIR     ?= $(GXF_DEPS_DIR)/gen
GXF_PROTO_DIR   ?= $(GXF_DEPS_DIR)/proto
CUDA_HOME       ?= /usr/local/cuda
CUDA_LIB64      ?= $(CUDA_HOME)/lib64
DEVCC           ?= $(CUDA_HOME)/bin/nvcc
GXF_PYTHON      ?= $(shell python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null)
PYTHON_INCLUDE  ?= $(shell python$(GXF_PYTHON) -c 'import sysconfig; print(sysconfig.get_paths()["include"])' 2>/dev/null)
PYTHON_LIBDIR   ?= $(shell python$(GXF_PYTHON) -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or "")' 2>/dev/null)
PYTHON_LIB      ?= python$(GXF_PYTHON)
BOOST_INCLUDE_DIR ?= /usr/include
BOOST_LIBRARY_DIR ?= /usr/lib/x86_64-linux-gnu

# Pin gcc-11 to match the bazel toolchain (make's built-in default is g++;
# only override it here when CXX was not set explicitly via cmdline/env)
ifeq ($(origin CXX),default)
CXX := g++-11
endif
AR  ?= ar
NVCC := $(DEVCC)
PROTOC := $(GXF_DEPS_PREFIX)/bin/protoc
GRPC_CPP_PLUGIN := $(GXF_DEPS_PREFIX)/bin/grpc_cpp_plugin

# ---------------------------------------------------------------------------
# 1. Compile/link flags (identical to bazel crosstool k8/gcc-11/-c opt)
# ---------------------------------------------------------------------------
BASE_DEFS   := -D_DEFAULT_SOURCE -U_FORTIFY_SOURCE -D__STDC_FORMAT_MACROS \
               -DNDEBUG -D_FORTIFY_SOURCE=2
BASE_WARN   := -Wall -Wunused-result -Werror -Wunused-but-set-parameter \
               -Wno-free-nonheap-object -Wno-unused-function
BASE_OPT    := -O3 -ggdb2 -fno-omit-frame-pointer -fstack-protector \
               -ffunction-sections -fdata-sections
INCLUDES    := -I$(WS) \
               -isystem $(GXF_GEN_DIR) \
               -isystem $(GXF_DEPS_PREFIX)/include \
               -isystem $(GXF_DEPS_PREFIX)/include/breakpad \
               -isystem $(CUDA_HOME)/include \
               -isystem $(PYTHON_INCLUDE)
# Boost headers: skip the flag when using system boost in /usr/include —
# "-isystem /usr/include" reorders the system header chain and breaks gcc's
# #include_next (e.g. stdlib.h not found via cstdlib).
ifneq ($(BOOST_INCLUDE_DIR),/usr/include)
INCLUDES    += -isystem $(BOOST_INCLUDE_DIR)
endif
# Note: EXTRA_CXXFLAGS (per-translation-unit extra options) is referenced
# directly in the rules so that target-specific variables are evaluated at
# recipe-expansion time (CXXFLAGS is :=, i.e. expanded immediately).
CXXFLAGS    := -std=c++17 $(BASE_DEFS) $(BASE_WARN) $(BASE_OPT) -fPIC -B/usr/bin \
               $(INCLUDES)

# Linking (gcc driver + gold; same as bazel's toolchain cxx_link_opts)
LDFLAGS     := -fuse-ld=gold -Wl,-no-as-needed -Wl,-z,relro,-z,now -B/usr/bin \
               -pass-exit-codes -fPIC -Wl,--gc-sections -Wl,--disable-new-dtags -Wl,-S
SYS_LIBS    := -lstdc++ -lm

RPATH_ORIGIN := -Wl,-rpath,'$$ORIGIN'
RPATH_CUDA   := -Wl,-rpath,$(CUDA_LIB64)

# Third-party libraries
YAML_CPP_A  := $(GXF_DEPS_PREFIX)/lib/libyaml-cpp.a
GFLAGS_A    := $(GXF_DEPS_PREFIX)/lib/libgflags.a
BREAKPAD_A  := $(GXF_DEPS_PREFIX)/lib/libbreakpad.a
CPPREST_A   := $(GXF_DEPS_PREFIX)/lib/libcpprest.a

CUDART_LINK := -L$(CUDA_LIB64) -Wl,--no-as-needed,-l:libcudart.so,--as-needed $(RPATH_CUDA)
CUBLAS_LINK := -L$(CUDA_LIB64) -Wl,--no-as-needed,-l:libcublasLt.so,-l:libcublas.so,--as-needed
NPP_LINK    := -L$(CUDA_LIB64) -Wl,--no-as-needed,-l:libnppial.so,-l:libnppidei.so,-l:libnppc.so,-l:libnpps.so,--as-needed
CUDA_DRV_LINK := -L$(CUDA_HOME)/targets/x86_64-linux/lib/stubs \
                 -L$(CUDA_HOME)/targets/x86_64-linux/lib \
                 -lcuda -l:libnvToolsExt.so.1 \
                 -Wl,-rpath,$(CUDA_HOME)/targets/x86_64-linux/lib
PYTHON_LINK := -L$(PYTHON_LIBDIR) -l$(PYTHON_LIB) -Wl,-rpath,$(PYTHON_LIBDIR)
BOOST_LINK  := -L$(BOOST_LIBRARY_DIR) -lboost_system -lboost_thread -lboost_chrono \
               -lboost_atomic -lboost_regex -lboost_date_time -lboost_filesystem -lboost_random
SSL_LINK    := -lssl -lcrypto

# gRPC static library group (merged static libs from the grpc 1.48 cmake
# install + all absl libs; --start-group resolves circular deps between
# static archives)
GRPC_LIBS   := -Wl,--start-group \
               $(GXF_DEPS_PREFIX)/lib/libgrpc++.a \
               $(GXF_DEPS_PREFIX)/lib/libgrpc.a \
               $(GXF_DEPS_PREFIX)/lib/libgpr.a \
               $(GXF_DEPS_PREFIX)/lib/libaddress_sorting.a \
               $(GXF_DEPS_PREFIX)/lib/libupb.a \
               $(wildcard $(GXF_DEPS_PREFIX)/lib/libutf8_range.a) \
               $(GXF_DEPS_PREFIX)/lib/libprotobuf.a \
               $(GXF_DEPS_PREFIX)/lib/libre2.a \
               $(GXF_DEPS_PREFIX)/lib/libcares.a \
               $(GXF_DEPS_PREFIX)/lib/libz.a \
               $(GXF_DEPS_PREFIX)/lib/libssl.a \
               $(GXF_DEPS_PREFIX)/lib/libcrypto.a \
               $(wildcard $(GXF_DEPS_PREFIX)/lib/libabsl_*.a) \
               -Wl,--end-group

# ---------------------------------------------------------------------------
# 2. GXF internal libraries (.lo static archives, 1:1 with bazel's
#    alwayslink cc_library targets)
# ---------------------------------------------------------------------------
L := $(BUILD)

# --- common ---
COMMON_LO := $(L)/common/libtype_name.lo $(L)/common/libbacktrace.lo $(L)/common/liblogger.lo
# --- gxf/logger ---
LOGGER_LO := $(L)/gxf/logger/libgxf_logger.lo $(L)/gxf/logger/libcommon_logger.lo
# --- gxf/core ---
CORE4_LO  := $(L)/gxf/core/libparameter_registrar.lo $(L)/gxf/core/libparameter_storage.lo \
             $(L)/gxf/core/libresource_manager.lo $(L)/gxf/core/libtype_registry.lo
# --- gxf/std runtime subset (transitive closure of //gxf/core:gxf,
#     linked into libgxf_core.so) ---
STD_RT_NAMES := extension_loader extension program entity_warden component_factory \
             component_allocator entity_resource_helper cpu_thread router_group \
             entity_executor codelet job_statistics message_router topic \
             network_router monitor scheduling_terms allocator clock connection \
             receiver transmitter queue timestamp system_group resources system \
             yaml_file_loader
STD_RT_LO   := $(addprefix $(L)/gxf/std/lib,$(addsuffix .lo,$(STD_RT_NAMES))) \
               $(L)/gxf/std/gems/utils/libtime.lo
# --- base embedded in every extension (+ default_extension from the
#     extension_factory_helper chain) ---
EXT_BASE_LO := $(L)/gxf/core/libgxf.lo $(CORE4_LO) $(STD_RT_LO) \
               $(L)/gxf/std/libdefault_extension.lo $(COMMON_LO) $(LOGGER_LO)
# --- full gxf/std (transitive closure of //gxf/std) ---
STD_FULL_NAMES := async_buffer_receiver async_buffer_transmitter block_memory_pool \
             broadcast double_buffer_receiver double_buffer_transmitter \
             epoch_scheduler gather graph_driver graph_worker \
             graph_driver_worker_common greedy_scheduler metric \
             multi_thread_scheduler event_based_scheduler synchronization \
             synthetic_clock tensor_copier tensor dlpack_utils timed_throttler \
             unbounded_allocator vault
STD_FULL_LO := $(EXT_BASE_LO) \
               $(addprefix $(L)/gxf/std/lib,$(addsuffix .lo,$(STD_FULL_NAMES)))
# --- gxf/cuda libraries ---
CUDA_LIB_NAMES := cuda_stream_sync cuda_scheduling_terms cuda_stream_pool \
             stream_ordered_allocator cuda_stream cuda_allocator cuda_event
CUDA_LIBS_LO  := $(addprefix $(L)/gxf/cuda/lib,$(addsuffix .lo,$(CUDA_LIB_NAMES))) \
               $(L)/gxf/std/gems/utils/libstorage_size.lo
# cuda subset used by python_codelet / cuda tests
CUDA_SUB_LO   := $(L)/gxf/cuda/libcuda_stream_pool.lo $(L)/gxf/cuda/libcuda_stream.lo \
               $(L)/gxf/cuda/libcuda_allocator.lo $(L)/gxf/cuda/libcuda_event.lo \
               $(L)/gxf/std/gems/utils/libstorage_size.lo
# --- gxf/serialization libraries ---
SER_NAMES   := entity_recorder entity_replayer file_stream file serialization_buffer \
             std_component_serializer std_entity_id_serializer std_entity_serializer \
             component_serializer entity_serializer endpoint
SER_LO      := $(addprefix $(L)/gxf/serialization/lib,$(addsuffix .lo,$(SER_NAMES)))
# --- gxf/behavior_tree libraries ---
BT_NAMES    := constant_behavior entity_count_failure_repeat_controller \
             parallel_behavior repeat_behavior selector_behavior sequence_behavior \
             switch_behavior timer_behavior
BT_LO       := $(addprefix $(L)/gxf/behavior_tree/lib,$(addsuffix .lo,$(BT_NAMES)))
# --- gxf/network libraries ---
NET_NAMES   := clock_sync_primary clock_sync_secondary tcp_client tcp_server \
             tcp_codelet tcp_server_socket tcp_client_socket
NET_LO      := $(addprefix $(L)/gxf/network/lib,$(addsuffix .lo,$(NET_NAMES)))
# --- gxf/sample libraries ---
SAMPLE_LO   := $(addprefix $(L)/gxf/sample/lib,$(addsuffix .lo,\
             ping_rx ping_rx_async ping_tx ping_tx_async ping_batch_rx multi_ping_rx))
# --- gxf/test/components libraries ---
TESTCOMP_LO := $(addprefix $(L)/gxf/test/components/lib,$(addsuffix .lo,\
             entity_monitor mock_allocator mock_codelet mock_failure \
             mock_receiver mock_transmitter tensor_comparator tensor_generator))

# ---------------------------------------------------------------------------
# 3. Artifact list
# ---------------------------------------------------------------------------
SO_CORE      := $(L)/gxf/core/libgxf_core.so
SO_LOGGER    := $(L)/gxf/logger/libgxf_logger.so
SO_STD       := $(L)/gxf/std/libgxf_std.so
SO_SAMPLE    := $(L)/gxf/sample/libgxf_sample.so
SO_BT        := $(L)/gxf/behavior_tree/libgxf_behavior_tree.so
SO_SER       := $(L)/gxf/serialization/libgxf_serialization.so
SO_NET       := $(L)/gxf/network/libgxf_network.so
SO_MM        := $(L)/gxf/multimedia/libgxf_multimedia.so
SO_NPP       := $(L)/gxf/npp/libgxf_npp.so
SO_CUDA      := $(L)/gxf/cuda/libgxf_cuda.so
SO_TESTCUDA  := $(L)/gxf/cuda/tests/libgxf_test_cuda.so
SO_TEST      := $(L)/gxf/test/extensions/libgxf_test.so
SO_GRPC      := $(L)/gxf/ipc/grpc/libgxf_grpc.so
SO_HTTP      := $(L)/gxf/ipc/http/libgxf_http.so
SO_PYCODELET := $(L)/gxf/python_codelet/libgxf_python_codelet.so
GXE          := $(L)/gxf/gxe/gxe

PYBINDS      := $(L)/gxf/core/core_pybind.so \
               $(addprefix $(L)/gxf/std/,allocator_pybind.so clock_pybind.so \
                 receiver_pybind.so tensor_pybind.so timestamp_pybind.so \
                 transmitter_pybind.so vault_pybind.so) \
               $(L)/gxf/cuda/cuda_pybind.so \
               $(L)/gxf/python_codelet/pycodelet.so

EXT_SOS      := $(SO_LOGGER) $(SO_CORE) $(SO_STD) $(SO_SAMPLE) $(SO_BT) $(SO_SER) \
               $(SO_NET) $(SO_MM) $(SO_NPP) $(SO_CUDA) $(SO_TESTCUDA) $(SO_TEST) \
               $(SO_GRPC) $(SO_HTTP) $(SO_PYCODELET)

# Static archives required by the release packaging manifest (same .lo
# names as in bazel-bin)
STATIC_LOS   := $(L)/gxf/core/libgxf.lo \
               $(L)/gxf/std/libdefault_extension.lo $(L)/gxf/std/libdlpack_utils.lo \
               $(L)/gxf/std/libgxf_std_static.lo $(L)/gxf/std/libmetric.lo \
               $(L)/gxf/std/libtensor.lo $(L)/gxf/std/libyaml_file_loader.lo \
               $(L)/gxf/serialization/libserialization_buffer.lo \
               $(L)/gxf/serialization/libentity_serializer.lo

ARTIFACTS    := $(EXT_SOS) $(PYBINDS) $(GXE) $(STATIC_LOS)

# ---------------------------------------------------------------------------
# 4. Generic rules
# ---------------------------------------------------------------------------
.PHONY: all help env-check test package dist clean \
        core std gxe logger sample behavior_tree serialization network \
        multimedia npp cuda test_cuda test_ext grpc http python_codelet pybinds

all: env-check $(ARTIFACTS) $(BUILD)/manifest.yaml
	@echo "==> Build finished. Artifacts at $(BUILD)/gxf/"

help:
	@echo "Usage: source ./setup_env.sh && make -j\$$(nproc)"
	@echo "Targets: all (default), core, std, gxe, logger, sample, behavior_tree,"
	@echo "         serialization, network, multimedia, npp, cuda, test_cuda,"
	@echo "         test_ext, grpc, http, python_codelet, pybinds, test, package,"
	@echo "         dist, clean"

env-check:
	@test -f "$(YAML_CPP_A)" || { echo "ERROR: $(YAML_CPP_A) not found."; \
	  echo "Run 'source ./setup_env.sh' first to build third-party deps."; exit 1; }
	@test -f "$(GRPC_CPP_PLUGIN)" || { echo "ERROR: gRPC toolchain not found in $(GXF_DEPS_PREFIX)."; \
	  echo "Run 'source ./setup_env.sh' first."; exit 1; }
	@test -d "$(CUDA_HOME)/include" || { echo "ERROR: CUDA_HOME=$(CUDA_HOME) invalid."; exit 1; }
	@test -f "$(PYTHON_INCLUDE)/Python.h" || { echo "ERROR: Python headers not found ($(PYTHON_INCLUDE))."; exit 1; }
	@ls $(BOOST_LIBRARY_DIR)/libboost_system.* >/dev/null 2>&1 || { \
	  echo "ERROR: boost libraries not found in BOOST_LIBRARY_DIR=$(BOOST_LIBRARY_DIR)."; \
	  echo "Run 'source ./setup_env.sh' first (it builds boost 1.80 or detects the system boost)."; exit 1; }
	@command -v $(CXX) >/dev/null || { echo "ERROR: $(CXX) not found."; exit 1; }
	@test -x "$(NVCC)" || command -v "$(NVCC)" >/dev/null || { echo "ERROR: DEVCC=$(NVCC) not found."; exit 1; }

# C++ compilation (with header dependency tracking)
$(OBJ)/%.o: $(WS)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(EXTRA_CXXFLAGS) -MD -MF $(@:.o=.d) -c $< -o $@

# Generated protobuf code (third-party generated code, no -Werror)
$(OBJ)/gen/%.o: $(GXF_GEN_DIR)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(filter-out -Werror,$(CXXFLAGS)) -MD -MF $(@:.o=.d) -c $< -o $@

# The only CUDA source (nvcc; matches bazel cc_cuda_library / the crosstool
# nvcc wrapper. Note: the quotes around code=... must reach nvcc as part of
# the value, hence the \" escaping)
CUDA_GENCODES := $(foreach a,52 53 60 61 62 70 75 86 89 90,-gencode=arch=compute_$(a),\"code=sm_$(a),compute_$(a)\")
$(OBJ)/gxf/cuda/tests/convolution.o: $(WS)/gxf/cuda/tests/convolution.cu.cpp
	@mkdir -p $(@D)
	$(NVCC) -D_FORCE_INLINES $(CUDA_GENCODES) -U_FORTIFY_SOURCE \
	  -D_DEFAULT_SOURCE -D__STDC_FORMAT_MACROS -DNDEBUG -D_FORTIFY_SOURCE=2 \
	  -std=c++17 -O3 -x cu --compiler-bindir=$(CXX) \
	  --compiler-options "-fPIC" -I$(WS) -isystem $(CUDA_HOME)/include \
	  -c $< -o $@

# gRPC / protobuf code generation (grpc_service.proto + health.proto from
# the gRPC source tree)
GRPC_GEN_SRCS := $(GXF_GEN_DIR)/gxf/ipc/grpc/grpc_service.pb.cc \
                 $(GXF_GEN_DIR)/gxf/ipc/grpc/grpc_service.grpc.pb.cc \
                 $(GXF_GEN_DIR)/src/proto/grpc/health/v1/health.pb.cc \
                 $(GXF_GEN_DIR)/src/proto/grpc/health/v1/health.grpc.pb.cc
GRPC_GEN_OBJS := $(patsubst $(GXF_GEN_DIR)/%.cc,$(OBJ)/gen/%.o,$(GRPC_GEN_SRCS))

$(GRPC_GEN_SRCS) &: $(WS)/gxf/ipc/grpc/grpc_service.proto \
                  $(GXF_PROTO_DIR)/src/proto/grpc/health/v1/health.proto
	@mkdir -p $(GXF_GEN_DIR)
	$(PROTOC) -I$(WS) --cpp_out=$(GXF_GEN_DIR) --grpc_out=$(GXF_GEN_DIR) \
	  --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $(WS)/gxf/ipc/grpc/grpc_service.proto
	$(PROTOC) -I$(GXF_PROTO_DIR) --cpp_out=$(GXF_GEN_DIR) --grpc_out=$(GXF_GEN_DIR) \
	  --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) \
	  $(GXF_PROTO_DIR)/src/proto/grpc/health/v1/health.proto

# .lo archives (ar rcsD, same as bazel; linked with --whole-archive)
define LO_RULE
$(1): $(2)
	@mkdir -p $$(@D)
	$(AR) rcsD $$@ $$^
endef

# --- common ---
$(eval $(call LO_RULE,$(L)/common/libbacktrace.lo,$(OBJ)/common/backtrace.o))
$(eval $(call LO_RULE,$(L)/common/liblogger.lo,$(OBJ)/common/logger.o))
$(eval $(call LO_RULE,$(L)/common/libtype_name.lo,$(OBJ)/common/type_name.o))
# --- gxf/logger ---
$(eval $(call LO_RULE,$(L)/gxf/logger/libgxf_logger.lo,$(OBJ)/gxf/logger/gxf_logger.o))
$(eval $(call LO_RULE,$(L)/gxf/logger/libcommon_logger.lo,$(OBJ)/gxf/logger/logger.o))
# --- gxf/core ---
$(eval $(call LO_RULE,$(L)/gxf/core/libgxf.lo,$(OBJ)/gxf/core/gxf.o $(OBJ)/gxf/core/runtime.o))
$(eval $(call LO_RULE,$(L)/gxf/core/libparameter_registrar.lo,$(OBJ)/gxf/core/parameter_registrar.o))
$(eval $(call LO_RULE,$(L)/gxf/core/libparameter_storage.lo,$(OBJ)/gxf/core/parameter_storage.o))
$(eval $(call LO_RULE,$(L)/gxf/core/libresource_manager.lo,$(OBJ)/gxf/core/resource_manager.o))
$(eval $(call LO_RULE,$(L)/gxf/core/libtype_registry.lo,$(OBJ)/gxf/core/type_registry.o))
$(eval $(call LO_RULE,$(L)/gxf/core/libcore_pybind_pybind.lo,$(OBJ)/gxf/core/bindings/core.o))
# --- gxf/std (generic pattern for single-source libraries) ---
STD_SINGLE := allocator async_buffer_receiver async_buffer_transmitter \
  block_memory_pool broadcast clock codelet component_allocator component_factory \
  connection cpu_thread default_extension dlpack_utils double_buffer_receiver \
  double_buffer_transmitter entity_executor entity_resource_helper entity_warden \
  epoch_scheduler event_based_scheduler extension extension_loader gather \
  graph_driver graph_driver_worker_common graph_worker greedy_scheduler \
  job_statistics message_router metric monitor multi_thread_scheduler \
  network_router program queue receiver resources router_group synchronization \
  synthetic_clock system system_group tensor tensor_copier timed_throttler \
  timestamp topic transmitter unbounded_allocator vault yaml_file_loader
$(foreach n,$(STD_SINGLE),$(eval $(call LO_RULE,$(L)/gxf/std/lib$(n).lo,$(OBJ)/gxf/std/$(n).o)))
$(eval $(call LO_RULE,$(L)/gxf/std/libscheduling_terms.lo,$(OBJ)/gxf/std/scheduling_condition.o $(OBJ)/gxf/std/scheduling_terms.o))
$(eval $(call LO_RULE,$(L)/gxf/std/libstd_src.lo,$(OBJ)/gxf/std/std.o))
$(eval $(call LO_RULE,$(L)/gxf/std/libgxf_std_static.lo,$(OBJ)/gxf/std/std.o))
$(eval $(call LO_RULE,$(L)/gxf/std/gems/utils/libtime.lo,$(OBJ)/gxf/std/gems/utils/time.o))
$(eval $(call LO_RULE,$(L)/gxf/std/gems/utils/libstorage_size.lo,$(OBJ)/gxf/std/gems/utils/storage_size.o))
# gxf/std pybind modules
$(foreach n,allocator clock receiver tensor timestamp transmitter vault,\
  $(eval $(call LO_RULE,$(L)/gxf/std/lib$(n)_pybind_pybind.lo,$(OBJ)/gxf/std/bindings/$(n).o)))
# --- gxf/cuda libraries ---
$(eval $(call LO_RULE,$(L)/gxf/cuda/libcuda_src.lo,$(OBJ)/gxf/cuda/cuda.o))
$(foreach n,$(CUDA_LIB_NAMES),$(eval $(call LO_RULE,$(L)/gxf/cuda/lib$(n).lo,$(OBJ)/gxf/cuda/$(n).o)))
$(eval $(call LO_RULE,$(L)/gxf/cuda/libcuda_pybind_pybind.lo,$(OBJ)/gxf/cuda/bindings/cuda.o))
# --- gxf/cuda/tests ---
$(eval $(call LO_RULE,$(L)/gxf/cuda/tests/libtest_cuda_src.lo,$(OBJ)/gxf/cuda/tests/test_cuda_ext.o))
$(L)/gxf/cuda/tests/libconvolution.a: $(OBJ)/gxf/cuda/tests/convolution.o
	@mkdir -p $(@D)
	$(AR) rcsD $@ $^
# --- gxf/serialization ---
$(eval $(call LO_RULE,$(L)/gxf/serialization/libserialization_src.lo,$(OBJ)/gxf/serialization/serialization.o))
$(foreach n,$(SER_NAMES),$(eval $(call LO_RULE,$(L)/gxf/serialization/lib$(n).lo,$(OBJ)/gxf/serialization/$(n).o)))
# --- gxf/behavior_tree ---
$(eval $(call LO_RULE,$(L)/gxf/behavior_tree/libbehavior_tree_src.lo,$(OBJ)/gxf/behavior_tree/behavior_tree.o))
$(foreach n,$(BT_NAMES),$(eval $(call LO_RULE,$(L)/gxf/behavior_tree/lib$(n).lo,$(OBJ)/gxf/behavior_tree/$(n).o)))
# --- gxf/network ---
$(eval $(call LO_RULE,$(L)/gxf/network/libnetwork_src.lo,$(OBJ)/gxf/network/network.o))
$(foreach n,$(NET_NAMES),$(eval $(call LO_RULE,$(L)/gxf/network/lib$(n).lo,$(OBJ)/gxf/network/$(n).o)))
# --- gxf/multimedia ---
$(eval $(call LO_RULE,$(L)/gxf/multimedia/libmultimedia_src.lo,$(OBJ)/gxf/multimedia/multimedia.o))
$(eval $(call LO_RULE,$(L)/gxf/multimedia/libaudio.lo,$(OBJ)/gxf/multimedia/audio.o))
$(eval $(call LO_RULE,$(L)/gxf/multimedia/libvideo.lo,$(OBJ)/gxf/multimedia/video.o))
# --- gxf/npp ---
$(eval $(call LO_RULE,$(L)/gxf/npp/libnpp_src.lo,$(OBJ)/gxf/npp/npp.o))
$(eval $(call LO_RULE,$(L)/gxf/npp/libnppi_mul_c.lo,$(OBJ)/gxf/npp/nppi_mul_c.o))
$(eval $(call LO_RULE,$(L)/gxf/npp/libnppi_set.lo,$(OBJ)/gxf/npp/nppi_set.o))
# --- gxf/sample ---
$(eval $(call LO_RULE,$(L)/gxf/sample/libsample_src.lo,$(OBJ)/gxf/sample/sample.o))
$(foreach n,ping_rx ping_rx_async ping_tx ping_tx_async ping_batch_rx multi_ping_rx,\
  $(eval $(call LO_RULE,$(L)/gxf/sample/lib$(n).lo,$(OBJ)/gxf/sample/$(n).o)))
# --- gxf/test ---
$(eval $(call LO_RULE,$(L)/gxf/test/extensions/libtest_src.lo,$(OBJ)/gxf/test/extensions/test.o))
$(foreach n,entity_monitor mock_allocator mock_codelet mock_failure mock_receiver \
  mock_transmitter tensor_comparator tensor_generator,\
  $(eval $(call LO_RULE,$(L)/gxf/test/components/lib$(n).lo,$(OBJ)/gxf/test/components/$(n).o)))
# --- gxf/ipc ---
$(eval $(call LO_RULE,$(L)/gxf/ipc/grpc/libgrpc_src.lo,$(OBJ)/gxf/ipc/grpc/grpc_ext.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/grpc/libgrpc_client.lo,$(OBJ)/gxf/ipc/grpc/grpc_client.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/grpc/libgrpc_server.lo,$(OBJ)/gxf/ipc/grpc/grpc_server.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/http/libhttp_src.lo,$(OBJ)/gxf/ipc/http/http.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/http/libhttp_client.lo,$(OBJ)/gxf/ipc/http/http_client_cpprest_impl.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/http/libhttp_ipc_client.lo,$(OBJ)/gxf/ipc/http/http_ipc_client.o))
$(eval $(call LO_RULE,$(L)/gxf/ipc/http/libhttp_server.lo,$(OBJ)/gxf/ipc/http/http_server.o))
# --- gxf/python_codelet ---
$(eval $(call LO_RULE,$(L)/gxf/python_codelet/libpython_codelet_src.lo,$(OBJ)/gxf/python_codelet/python_codelet.o))
$(eval $(call LO_RULE,$(L)/gxf/python_codelet/libpy_codelet.lo,$(OBJ)/gxf/python_codelet/py_codelet.o))
$(eval $(call LO_RULE,$(L)/gxf/python_codelet/libpycodelet_pybind.lo,$(OBJ)/gxf/python_codelet/bindings/pycodelet.o))

# Special compile options for individual translation units (same as the
# bazel BUILD files)
$(OBJ)/gxf/ipc/grpc/grpc_client.o $(OBJ)/gxf/ipc/grpc/grpc_server.o: \
  EXTRA_CXXFLAGS := -fvisibility=hidden -fvisibility-inlines-hidden

# All GXF sources of the gRPC module include protoc-generated headers
# ("gxf/ipc/grpc/grpc_service.grpc.pb.h" and health.grpc.pb.h); they must
# depend on the generated code explicitly, otherwise parallel builds race.
$(OBJ)/gxf/ipc/grpc/grpc_client.o \
$(OBJ)/gxf/ipc/grpc/grpc_server.o \
$(OBJ)/gxf/ipc/grpc/grpc_ext.o: $(GRPC_GEN_SRCS)

# ---------------------------------------------------------------------------
# 5. Artifact link rules
# ---------------------------------------------------------------------------
# link_so <lo list> <extra libs/flags>
# Uniform shape of an extension .so: -shared + all .lo under --whole-archive
# + external static libs + system libs; the soname is derived from the
# artifact file name (same as bazel nv_gxf_cc_extension's
# -Wl,-soname,libgxf_X.so).
# Note: $(call) arguments are split on literal commas, so any flag
# containing commas must go through a variable.
define link_so
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ \
	  -Wl,--whole-archive $(1) -Wl,--no-whole-archive \
	  $(2) -Wl,-soname,$(notdir $@) $(LDFLAGS) $(SYS_LIBS)
endef

$(SO_LOGGER): $(L)/gxf/logger/libcommon_logger.lo
	$(call link_so,$^,)

EXCLUDE_YAML := -Wl,--exclude-libs,libyaml_file_loader.lo -Wl,--exclude-libs,libyaml-cpp.a
$(SO_CORE): $(L)/gxf/core/libgxf.lo $(CORE4_LO) $(STD_RT_LO) $(COMMON_LO) $(LOGGER_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(EXCLUDE_YAML) -pthread -ldl)

$(SO_STD): $(L)/gxf/std/libstd_src.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_SAMPLE): $(L)/gxf/sample/libsample_src.lo $(SAMPLE_LO) $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_BT): $(L)/gxf/behavior_tree/libbehavior_tree_src.lo $(BT_LO) $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_SER): $(L)/gxf/serialization/libserialization_src.lo $(SER_LO) $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_NET): $(L)/gxf/network/libnetwork_src.lo $(NET_LO) \
           $(L)/gxf/serialization/libentity_serializer.lo $(L)/gxf/serialization/libendpoint.lo \
           $(L)/gxf/std/libsynthetic_clock.lo $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_MM): $(L)/gxf/multimedia/libmultimedia_src.lo $(L)/gxf/multimedia/libaudio.lo \
          $(L)/gxf/multimedia/libvideo.lo $(L)/gxf/std/libtensor.lo \
          $(L)/gxf/std/libdlpack_utils.lo $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_NPP): $(L)/gxf/npp/libnpp_src.lo $(L)/gxf/npp/libnppi_mul_c.lo \
           $(L)/gxf/npp/libnppi_set.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(NPP_LINK) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_CUDA): $(L)/gxf/cuda/libcuda_src.lo $(CUDA_LIBS_LO) $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_TESTCUDA): $(L)/gxf/cuda/tests/libtest_cuda_src.lo $(L)/gxf/cuda/tests/libconvolution.a \
                $(CUDA_SUB_LO) $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(L)/gxf/cuda/tests/libconvolution.a $(YAML_CPP_A) \
	  $(CUBLAS_LINK) $(CUDART_LINK) $(CUDA_DRV_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_TEST): $(L)/gxf/test/extensions/libtest_src.lo $(TESTCOMP_LO) $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_GRPC): $(L)/gxf/ipc/grpc/libgrpc_src.lo $(L)/gxf/ipc/grpc/libgrpc_client.lo \
            $(L)/gxf/ipc/grpc/libgrpc_server.lo $(GRPC_GEN_OBJS) $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo %.o,$^),$(YAML_CPP_A) $(GRPC_LIBS) \
	  $(RPATH_ORIGIN) -pthread -ldl)

$(SO_HTTP): $(L)/gxf/ipc/http/libhttp_src.lo $(L)/gxf/ipc/http/libhttp_client.lo \
            $(L)/gxf/ipc/http/libhttp_ipc_client.lo $(L)/gxf/ipc/http/libhttp_server.lo \
            $(STD_FULL_LO) $(YAML_CPP_A) $(CPPREST_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CPPREST_A) $(BOOST_LINK) $(SSL_LINK) \
	  $(CUDART_LINK) $(RPATH_ORIGIN) -pthread -ldl)

$(SO_PYCODELET): $(L)/gxf/python_codelet/libpython_codelet_src.lo \
                 $(L)/gxf/python_codelet/libpy_codelet.lo $(CUDA_SUB_LO) \
                 $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_so,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) \
	  $(RPATH_ORIGIN) -pthread -ldl)

# --- gxe executable ---
$(GXE): $(OBJ)/gxf/gxe/gxe.o $(COMMON_LO) $(LOGGER_LO) $(SO_CORE) \
        $(YAML_CPP_A) $(GFLAGS_A) $(BREAKPAD_A)
	@mkdir -p $(@D)
	$(CXX) -o $@ $(OBJ)/gxf/gxe/gxe.o \
	  -Wl,--whole-archive $(COMMON_LO) $(LOGGER_LO) -Wl,--no-whole-archive \
	  $(YAML_CPP_A) $(GFLAGS_A) -L$(L)/gxf/core -lgxf_core $(BREAKPAD_A) \
	  -Wl,-rpath,'$$ORIGIN/../core' -lpthread $(LDFLAGS) $(SYS_LIBS)

# --- pybind modules (.so, no libgxf_ prefix, no soname, rpath $ORIGIN) ---
define link_pybind
	@mkdir -p $(@D)
	$(CXX) -shared -o $@ \
	  -Wl,--whole-archive $(1) -Wl,--no-whole-archive \
	  $(2) $(RPATH_ORIGIN) $(LDFLAGS) $(SYS_LIBS)
endef

$(L)/gxf/core/core_pybind.so: $(L)/gxf/core/libcore_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)

$(L)/gxf/std/allocator_pybind.so: $(L)/gxf/std/liballocator_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/clock_pybind.so: $(L)/gxf/std/libclock_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/receiver_pybind.so: $(L)/gxf/std/libreceiver_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/tensor_pybind.so: $(L)/gxf/std/libtensor_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/timestamp_pybind.so: $(L)/gxf/std/libtimestamp_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/transmitter_pybind.so: $(L)/gxf/std/libtransmitter_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)
$(L)/gxf/std/vault_pybind.so: $(L)/gxf/std/libvault_pybind_pybind.lo $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)

$(L)/gxf/cuda/cuda_pybind.so: $(L)/gxf/cuda/libcuda_pybind_pybind.lo $(CUDA_LIBS_LO) $(EXT_BASE_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)

$(L)/gxf/python_codelet/pycodelet.so: $(L)/gxf/python_codelet/libpycodelet_pybind.lo \
    $(L)/gxf/python_codelet/libpy_codelet.lo $(CUDA_SUB_LO) $(STD_FULL_LO) $(YAML_CPP_A)
	$(call link_pybind,$(filter %.lo,$^),$(YAML_CPP_A) $(CUDART_LINK) $(PYTHON_LINK) -pthread -ldl)

# ---------------------------------------------------------------------------
# 6. manifest / test / package
# ---------------------------------------------------------------------------
# Equivalent of bazel run: gxe is executed under $(WS) and the manifest uses
# relative paths (the unbuilt stream/ucx/rmm extensions are pruned, same as
# the install logic in build.sh)
$(BUILD)/manifest.yaml: $(EXT_SOS)
	@echo "extensions:" > $@
	@for e in gxf/std/libgxf_std.so gxf/sample/libgxf_sample.so \
	    gxf/cuda/libgxf_cuda.so gxf/cuda/tests/libgxf_test_cuda.so \
	    gxf/npp/libgxf_npp.so gxf/serialization/libgxf_serialization.so \
	    gxf/network/libgxf_network.so gxf/multimedia/libgxf_multimedia.so \
	    gxf/test/extensions/libgxf_test.so gxf/behavior_tree/libgxf_behavior_tree.so; do \
	  echo "- bin-make/$$e" >> $@; done

# Per-module convenience targets
core: $(SO_CORE) $(L)/gxf/core/core_pybind.so
std: $(SO_STD)
gxe: $(GXE)
logger: $(SO_LOGGER)
sample: $(SO_SAMPLE)
behavior_tree: $(SO_BT)
serialization: $(SO_SER)
network: $(SO_NET)
multimedia: $(SO_MM)
npp: $(SO_NPP)
cuda: $(SO_CUDA) $(L)/gxf/cuda/cuda_pybind.so
test_cuda: $(SO_TESTCUDA)
test_ext: $(SO_TEST)
grpc: $(SO_GRPC)
http: $(SO_HTTP)
python_codelet: $(SO_PYCODELET) $(L)/gxf/python_codelet/pycodelet.so
pybinds: $(PYBINDS)

test: all
	@echo "==> Running gxe smoke test (test_ping.yaml) ..."
	cd $(WS) && bin-make/gxf/gxe/gxe \
	  --app=gxf/test/apps/test_ping.yaml --manifest=bin-make/manifest.yaml

# Release packaging: reuses the repository's make_tarball.py.
# Two adaptations: 1) the platform src_lib is redirected to bin-make
# (artifacts are produced by make, not bazel);
# 2) make_tarball.py internally invokes `bazel build` to build the targets —
#    the artifacts are already built by make, so a no-op bazel shim is
#    prepended to PATH and the script goes straight to the copy stage.
package: all
	@echo "==> Packaging GXF release tarball ..."
	@mkdir -p /tmp/gxf-make-pkg-bin && printf '#!/bin/sh\n# no-op: artifacts already built by make\nexit 0\n' \
	  > /tmp/gxf-make-pkg-bin/bazel && chmod +x /tmp/gxf-make-pkg-bin/bazel
	cd $(WS) && sed 's|src_lib: "bazel-out/k8-opt/bin"|src_lib: "bin-make"|' \
	  $(GXF_ROOT)/build_gxf_release_content.yaml > /tmp/gxf_release_content_make.yaml && \
	  rm -rf /tmp/gxf-release && \
	  PATH="/tmp/gxf-make-pkg-bin:$$PATH" python3 release/make_tarball.py \
	    /tmp/gxf_release_content_make.yaml gxf_isaac_release.tar.gz /tmp/gxf-release \
	    --single_platform x86_cuda_12_6
	mkdir -p $(GXF_ROOT)/dist
	mv $(WS)/gxf_isaac_release.tar.gz $(GXF_ROOT)/dist/
	@echo "==> Tarball at $(GXF_ROOT)/dist/gxf_isaac_release.tar.gz"

# Dist: stage the make-built artifacts into the official gxf-install/
# {bin,include,lib} layout (same file organization as the released
# gxf_5.1.0_..._x86_64.tar.gz) and pack it into dist/. All staged files come
# from this repository; see make_dist.sh for the exact mapping.
dist: all
	@echo "==> Creating gxf-install dist tarball ..."
	@GXF_ROOT='$(GXF_ROOT)' WS='$(WS)' BUILD='$(BUILD)' OBJ='$(OBJ)' \
	  GXF_DEPS_PREFIX='$(GXF_DEPS_PREFIX)' CXX='$(CXX)' AR='$(AR)' \
	  EXT_SOS='$(EXT_SOS)' PYBINDS='$(PYBINDS)' \
	  COMMON_LO='$(COMMON_LO)' LOGGER_LO='$(LOGGER_LO)' \
	  YAML_CPP_A='$(YAML_CPP_A)' GFLAGS_A='$(GFLAGS_A)' BREAKPAD_A='$(BREAKPAD_A)' \
	  LDFLAGS='$(LDFLAGS)' SYS_LIBS='$(SYS_LIBS)' \
	  ./make_dist.sh

clean:
	rm -rf $(BUILD)

# Header dependencies
-include $(shell [ -d $(OBJ) ] && find $(OBJ) -name '*.d' 2>/dev/null)
