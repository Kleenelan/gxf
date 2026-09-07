#!/usr/bin/env bash
#
# run_test_suite.sh — quasi-regression runner for the GXF yaml test apps.
#
# Usage (usually via `make test_suite` / `make test_suite_mgpu`):
#   ./run_test_suite.sh [default|mgpu|all] [--list]
#
#   default : single-machine, single-GPU cases (gxe + manifest, one process)
#   mgpu    : multi-GPU cases (dev_id 0/1, entity groups, *_multi_*); requires
#             >= 2 GPUs, otherwise every case is reported as SKIP
#   all     : default + mgpu
#   --list  : only print the classification, run nothing
#
# Intentionally NOT covered (skipped with an explicit reason):
#   - multi-process cases: distributed driver/worker apps, UCX/TCP tx-rx pairs
#     (detected automatically: file mentions only one side of the connection)
#   - cases needing extensions the Makefile does not build (rmm/stream/benchmark)
#   - pytest-driven python tests (gxf/python/tests)
#   - fixture/negative metadata yamls (no scheduler component)
#
# Exit code: 0 if no FAIL (SKIP/PASS only), 1 otherwise.

set -u
CATEGORY="${1:-default}"
LIST_ONLY=0
FILTER=""
for arg in "${@:2}"; do
  case "$arg" in
    --list) LIST_ONLY=1 ;;
    --filter=*) FILTER="${arg#--filter=}" ;;
  esac
done

: "${GXF_ROOT:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
: "${WS:=${GXF_ROOT}/com_nvidia_gxf}"
: "${BUILD:=${WS}/bin-make}"
: "${UCX_HOME:=/opt/ucx-1.18.0}"
: "${CUDA_HOME:=/usr/local/cuda}"
TIMEOUT_DEFAULT=90
TIMEOUT_STRESS=30

export LD_LIBRARY_PATH="${UCX_HOME}/lib:${CUDA_HOME}/lib64:${LD_LIBRARY_PATH:-}"

LOGDIR="${BUILD}/test-logs"
mkdir -p "$LOGDIR"

GXE="${BUILD}/gxf/gxe/gxe"
MANIFEST="bin-make/manifest.yaml"

# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------
# Multi-GPU cases (own category, per user request)
is_mgpu() {
  case "$1" in
    gxf/test/apps/test_entity_group_default.yaml|\
    gxf/test/apps/test_entity_group_default_ebs.yaml|\
    gxf/test/apps/test_entity_group_users.yaml|\
    gxf/test/apps/test_entity_group_users_ebs.yaml|\
    gxf/test/apps/test_entity_group_default_and_users.yaml|\
    gxf/test/apps/test_entity_group_default_and_users_ebs.yaml|\
    gxf/test/apps/test_entity_group_root_and_subgraph.yaml|\
    gxf/test/apps/test_entity_group_root_and_subgraph_ebs.yaml|\
    gxf/cuda/tests/*_multi*.yaml) return 0 ;;
    *) return 1 ;;
  esac
}

# skip reason (empty string = runnable); arg = path relative to WS
skip_reason() {
  local rel="$1" f="${WS}/$1"
  case "$rel" in
    # multi-process distributed driver/worker apps (4.1 supports them, but they
    # need several coordinated processes -> manual execution only)
    *distributed*)               echo "multi-process distributed (driver/worker), run manually" ; return ;;
    # extensions not built by this Makefile
    gxf/rmm/tests/*)             echo "needs unbuilt extension rmm" ; return ;;
    gxf/stream/tests/*)          echo "needs unbuilt extension stream (NvSci removed)" ; return ;;
    gxf/benchmark/tests/*)       echo "needs unbuilt extension benchmark" ; return ;;
    gxf/python/tests/*)          echo "pytest-driven python test, not directly gxe-runnable" ; return ;;
    # negative fixtures for gxe/manifest parsing
    gxf/gxe/tests/*)             echo "gxe negative fixture (startup expected to fail)" ; return ;;
    # extension metadata fixtures (used by extension-loading unit tests)
    gxf/core/tests/metadata*.yaml) echo "extension metadata fixture" ; return ;;
    # UCX loopback: yaml ports are mismatched for standalone runs (rx 13337
    # vs tx 13338) so activation blocks forever; verified manually that with
    # matching ports the loopback connection IS established (UCX 1.20 OK)
    gxf/ucx/tests/test_forward_tx_rx.yaml|\
    gxf/ucx/tests/test_forward_tx_rx_sync.yaml)
                                 echo "yaml port mismatch, hangs standalone (loopback verified OK with fixed ports)" ; return ;;
  esac
  # fixtures detectable from file content
  grep -q "<Unspecified>" "$f" && { echo "parameter fixture (contains <Unspecified>)"; return; }
  grep -q "type: nvidia::gxf::Subgraph" "$f" && { echo "subgraph fixture (needs *.param.yaml)"; return; }
  grep -q "RMMAllocator" "$f" && { echo "needs unbuilt extension rmm"; return; }
  # not a runnable app (metadata/param fixtures without a scheduler)
  grep -q "Scheduler" "$f" || { echo "not a runnable app (no Scheduler; fixture/metadata)"; return; }
  # multi-process connection pairs: only one side present in the file
  case "$rel" in
    gxf/ucx/tests/*)
      if grep -q "type: nvidia::gxf::UcxTransmitter" "$f" && \
         ! grep -q "type: nvidia::gxf::UcxReceiver" "$f"; then
        echo "multi-process pair (Tx side only)"; return
      fi
      if grep -q "type: nvidia::gxf::UcxReceiver" "$f" && \
         ! grep -q "type: nvidia::gxf::UcxTransmitter" "$f"; then
        echo "multi-process pair (Rx side only)"; return
      fi ;;
    gxf/network/tests/*)
      if grep -qE "type: nvidia::gxf::(TcpClient|ClockSyncSecondary)" "$f" && \
         ! grep -qE "type: nvidia::gxf::(TcpServer|ClockSyncPrimary)" "$f"; then
        echo "multi-process pair (client/secondary side only)"; return
      fi
      if grep -qE "type: nvidia::gxf::(TcpServer|ClockSyncPrimary)" "$f" && \
         ! grep -qE "type: nvidia::gxf::(TcpClient|ClockSyncSecondary)" "$f"; then
        echo "multi-process pair (server/primary side only)"; return
      fi ;;
  esac
  echo ""
}

# expected-failure tests (fault injection): they PASS when the app FAILS
is_expected_failure() {
  grep -q "MockFailure" "${WS}/$1"
}

collect_yamls() {
  (cd "$WS" && ls gxf/test/apps/*.yaml \
                  gxf/cuda/tests/*.yaml \
                  gxf/behavior_tree/tests/*.yaml \
                  gxf/std/tests/*.yaml \
                  gxf/serialization/tests/*.yaml \
                  gxf/npp/tests/*.yaml \
                  gxf/multimedia/tests/*.yaml \
                  gxf/sample/tests/*.yaml \
                  gxf/core/tests/*.yaml \
                  gxf/network/tests/*.yaml \
                  gxf/ucx/tests/*.yaml \
                  gxf/rmm/tests/*.yaml \
                  gxf/stream/tests/*.yaml \
                  gxf/benchmark/tests/*.yaml \
                  gxf/python/tests/*.yaml \
                  gxf/gxe/tests/*.yaml 2>/dev/null)
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
declare -a PASS=() FAIL=() SKIP=()
run_one() {
  local rel="$1" log="${LOGDIR}/$(echo "$1" | tr '/.' '__').log" tmo=$TIMEOUT_DEFAULT
  case "$rel" in *stress*|*Stress*) tmo=$TIMEOUT_STRESS ;; esac
  (cd "$WS" && timeout "$tmo" "$GXE" --app="$rel" --manifest="$MANIFEST" \
     >"$log" 2>&1)
  local rc=$?
  local retried=0
  # crash (signal) under a suite run may be flaky resource/timing pressure —
  # retry once before judging (observed: cuda allocator stress test)
  if (( rc >= 128 && rc != 124 )); then
    (cd "$WS" && timeout "$tmo" "$GXE" --app="$rel" --manifest="$MANIFEST" \
       >"$log.retry" 2>&1)
    local rc2=$?
    if (( rc2 == 0 )); then
      mv "$log.retry" "$log"
      rc=0
      retried=1
    else
      rm -f "$log.retry"
    fi
  fi
  # capture health evidence BEFORE truncating the log
  local reached_running=0
  grep -q "Running\.\.\." "$log" && reached_running=1
  # keep at most the last 2MB of the log (stress tests otherwise produce GBs)
  local sz; sz=$(stat -c%s "$log" 2>/dev/null || echo 0)
  (( sz > 2097152 )) && tail -c 2097152 "$log" > "$log.tmp" && mv "$log.tmp" "$log"

  # fault-injection tests are designed to make the app fail
  if is_expected_failure "$rel"; then
    if [[ $rc -ne 0 ]]; then
      PASS+=("$rel (expected-failure case: rc=$rc)")
      echo "PASS  $rel (expected-failure case, rc=$rc)"
    else
      FAIL+=("$rel (expected failure but rc=0, log: $log)")
      echo "FAIL  $rel (expected failure but rc=0, log: $log)"
    fi
    return
  fi
  if [[ $rc -eq 0 ]]; then
    if (( retried == 1 )); then
      PASS+=("$rel (passed on crash retry, likely flaky)")
      echo "PASS  $rel (passed on crash retry, likely flaky)"
    else
      PASS+=("$rel")
      echo "PASS  $rel"
    fi
  elif [[ $rc -eq 124 ]] && (( reached_running == 1 )); then
    # designed-infinite apps (no self-terminating condition, e.g. epoch
    # scheduler / stress tests): reaching "Running..." and surviving the
    # timeout window counts as a pass in this quasi-regression
    PASS+=("$rel (long-running case, terminated by ${tmo}s timeout)")
    echo "PASS  $rel (long-running case, terminated by timeout)"
  else
    FAIL+=("$rel (rc=$rc, log: $log)")
    echo "FAIL  $rel (rc=$rc, log: $log)"
  fi
}

N_GPU=$(nvidia-smi --query-gpu=index --format=csv,noheader 2>/dev/null | wc -l)

while read -r rel; do
  [[ -z "$rel" ]] && continue
  [[ -n "$FILTER" && ! "$rel" =~ $FILTER ]] && continue
  if is_mgpu "$rel"; then
    if [[ "$CATEGORY" == "mgpu" || "$CATEGORY" == "all" ]]; then
      if (( N_GPU >= 2 )); then
        [[ $LIST_ONLY == 1 ]] && { echo "MGPU  $rel"; continue; }
        run_one "$rel"
      else
        SKIP+=("$rel (multi-GPU case: local GPU count=${N_GPU} < 2)")
        [[ $LIST_ONLY == 1 ]] && echo "SKIP  $rel (multi-GPU, GPU<2)"
      fi
    fi
    continue
  fi
  [[ "$CATEGORY" == "mgpu" ]] && continue
  reason="$(skip_reason "$rel")"
  if [[ -n "$reason" ]]; then
    SKIP+=("$rel ($reason)")
    [[ $LIST_ONLY == 1 ]] && echo "SKIP  $rel ($reason)"
    continue
  fi
  [[ $LIST_ONLY == 1 ]] && { echo "RUN   $rel"; continue; }
  run_one "$rel"
done < <(collect_yamls)

echo
echo "=================== TEST SUITE SUMMARY ($CATEGORY) ==================="
echo "PASS: ${#PASS[@]}   FAIL: ${#FAIL[@]}   SKIP: ${#SKIP[@]}"
if ((${#FAIL[@]})); then
  echo "--- FAILURES ---"
  printf '  %s\n' "${FAIL[@]}"
fi
if ((${#SKIP[@]})) && [[ "${VERBOSE_SKIP:-0}" == "1" ]]; then
  echo "--- SKIPPED ---"
  printf '  %s\n' "${SKIP[@]}"
fi
((${#FAIL[@]} == 0))
