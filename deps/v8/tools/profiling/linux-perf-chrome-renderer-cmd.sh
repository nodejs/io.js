#!/usr/bin/env bash
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

PERF_DATA_DIR="."
PERF_DATA_PREFIX="chrome_renderer"
PERF_CLOCKID="mono"
PERF_CALL_GRAPH="fp"
PERF_FREQ_DEFAULT=10000
PERF_FREQ=""
PERF_COUNT=""
PERF_RAW_ARGS=""
RENDERER_ID="0"

for i in "$@"; do
  case $i in
    -h|--help)
cat <<EOF
Usage: path/to/chrome --renderer-cmd-prefix='$0 [OPTION]' [CHROME OPTIONS]
This script is mostly used in conjuction with linux_perf.py to run linux-perf
for each renderer process.
It generates perf.data files that can be read by pprof or linux-perf.

File naming: \$OUT_DIR/\$PREFIX_\$PARENT_PID_\$RENDERER_ID.perf.data

Options:
  --perf-data-dir=OUT_DIR    Change the location where perf.data is written.
                             Default: '${PERF_DATA_DIR}'
  --perf-data-prefix=PREFIX  Set a custom prefix for all generated
                             perf.data files.
                             Default: '${PERF_DATA_PREFIX}'

Pass-though Options:
  --perf-count=SAMPLE_DELTA    Forwards options to 'perf record --count'
                               Default: not used
  --perf-freq=FREQUENCY        Forwards options to 'perf record --freq'
                               Default: ${PERF_FREQ_DEFAULT}
  --perf-clockid=CLOCK_TYPE    Forwards options to 'perf record --clockid'
                               Default: '${PERF_CLOCKID}'
  --perf-call-graph=MODE       Forwards options to 'perf record --call-graph'
                               Default: '${PERF_CALL_GRAPH}'
  --perf-args=ARGS             Forwards raw options to 'perf record \$ARGS'
                               Default: not used
EOF
      exit
      ;;
    --perf-data-dir=*)
      PERF_DATA_DIR="${i#*=}"
      shift
    ;;
    --perf-data-prefix=*)
      PERF_DATA_PREFIX="${i#*=}"
      shift
    ;;
    --perf-freq=*)
      PERF_FREQ="${i#*=}"
      shift
    ;;
    --perf-count=*)
      PERF_COUNT="${i#*=}"
      shift
    ;;
    --perf-call-graph*)
      PERF_CALL_GRAPH="${i#*=}"
      shift
    ;;
    --perf-clockid*)
      PERF_CLOCKID="${i#*=}"
      shift
    ;;
    --perf-args*)
      PERF_RAW_ARGS="${i#*=}"
      shift
    ;;
    --renderer-client-id=*)
      # Don't shift this option since it is passed in (and used by) chrome.
      RENDERER_ID="${i#*=}"
    ;;
    *)
      ;;
  esac
done

SAMPLE_TRIGGER=""
if [ -n "${PERF_COUNT}" ]; then
  SAMPLE_TRIGGER+="--count=${PERF_COUNT}"
elif [ -n "${PERF_FREQ}" ]; then
  SAMPLE_TRIGGER+=" --freq=${PERF_FREQ}"
else
  SAMPLE_TRIGGER="--freq=${PERF_FREQ_DEFAULT}"
fi

# Make sure `perf record` doesn't create `.debug/` in the home directory.
export JITDUMPDIR="${PERF_DATA_DIR}";
PERF_OUTPUT="${PERF_DATA_DIR}/${PERF_DATA_PREFIX}_${PPID}_${RENDERER_ID}.perf.data";
perf record \
  --call-graph=${PERF_CALL_GRAPH} \
  --clockid=${PERF_CLOCKID} \
  ${SAMPLE_TRIGGER} \
  ${PERF_RAW_ARGS} \
  --output="${PERF_OUTPUT}" \
  -- $@
