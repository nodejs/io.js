#!/bin/bash
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Exit immediately if a command exits with a non-zero status.
set -e

# Treat unset variables as an error when substituting.
set -u

# return value of a pipeline is the status of the last command to exit with a
# non-zero status, or zero if no command exited with a non-zero status
set -o pipefail

log() {
  echo ">>" $*
}

log_and_run() {
  log $*
  if ! $*; then
    echo "sub-command failed: $*"
    exit 1
  fi
}

# Copy file $1 located in directory $2 to directory $3 under the relative
# filename of $1 within $2.
copy_file_relative() {
  SRC_FILE=$1
  SRC_DIR=$2
  DST_DIR=$3

  REL_FILENAME=${SRC_FILE#${SRC_DIR}/}
  DST_FILE=${DST_DIR}/${REL_FILENAME}
  # Consistency check:
  if [ "${REL_FILENAME}" = "${SRC_FILE}" ]; then
    echo "Incorrect usage of copy_file_relative: ${SRC_FILE}  is not within ${SRC_DIR}"
    exit 1
  fi
  [[ -d $(dirname ${DST_FILE}) ]] || log_and_run mkdir -pv $(dirname ${DST_FILE})
  log_and_run cp -v ${SRC_FILE} ${DST_FILE}
}

# Copy files from $1 to $2 under their relative name within $2.
# $3 specifies the extension of the files to copy.
copy_files_relative() {
  SRC_DIR=$1
  DST_DIR=$2
  EXT=$3

  for filename in $(find ${SRC_DIR} -name "*.${EXT}"); do
    copy_file_relative ${filename} ${SRC_DIR} ${DST_DIR}
  done
}

new_section() {
  echo
  echo '###########################################################'
  echo '# '$1
  echo '###########################################################'
  echo
}

###############################################################################
# Setup directories.
###############################################################################

TOOLS_WASM_DIR=$(cd $(dirname ${BASH_SOURCE[0]}) && pwd)
V8_DIR=$(cd ${TOOLS_WASM_DIR}/../.. && pwd)
SPEC_TEST_DIR=${V8_DIR}/test/wasm-spec-tests
TMP_DIR=$(mktemp -d)
TMP_BUILD_DIR=${TMP_DIR}/build

JS_API_TEST_DIR=${V8_DIR}/test/wasm-js

# Remove old test directories; they will be recreated by this script.
log_and_run rm -rf ${SPEC_TEST_DIR}/tests
log_and_run rm -rf ${JS_API_TEST_DIR}/tests
# Create a build directory as output for wast->js conversions. This step
# creates multiple temporary files, so we put this in a separate directory and
# only copy over the resulting .js file.
log_and_run mkdir -v ${TMP_BUILD_DIR}

###############################################################################
# Generate the spec tests.
###############################################################################

new_section "Process spec"
log_and_run cd ${TMP_DIR}
# Note: We use the wasm-3.0 staging branch which has many features merged and
# has fewer outdated and thus failing tests.
log_and_run git clone --single-branch --no-tags -b wasm-3.0 https://github.com/WebAssembly/spec
log_and_run cd spec
log git rev-parse HEAD
SPEC_HASH=$(git rev-parse HEAD)

# The next step requires that ocaml is installed. See the README.md in
# https://github.com/WebAssembly/spec/tree/master/interpreter/.
log_and_run make -C interpreter distclean wasm

# Iterate the core spec tests, copy them over and converted to .js tests.
for filename in $(find test/core -name '*.wast'); do
  copy_file_relative ${filename} test/core ${SPEC_TEST_DIR}/tests
  log_and_run test/core/run.py --wasm interpreter/wasm --out ${TMP_BUILD_DIR} ${filename}
  REL_FILENAME=${filename#test/core/}
  DST_DIR=$(dirname ${SPEC_TEST_DIR}/tests/${REL_FILENAME})
  log_and_run mv -v ${TMP_BUILD_DIR}/*.js ${DST_DIR}
done
copy_files_relative test/js-api ${JS_API_TEST_DIR}/tests js

###############################################################################
# Generate the wpt tests.
###############################################################################

new_section "Process wpt"
log_and_run cd ${TMP_DIR}
log_and_run git clone --depth 1 https://github.com/web-platform-tests/wpt
log_and_run cd wpt

# Copy over test files (ending in '.any.js') which differ from the spec repo.
for filename in $(find wasm/jsapi -name '*.any.js'); do
  rel_filename=${filename#wasm/jsapi/}
  spec_test_name=${JS_API_TEST_DIR}/tests/wpt/${rel_filename}
  if [ -f ${spec_test_name} ] && cmp -s ${spec_test_name} ${filename}; then
    echo "Skipping WPT test which is identical to wasm-spec test"
  else
    copy_file_relative ${filename} wasm/jsapi ${JS_API_TEST_DIR}/tests/wpt
  fi
done

# Copy over helper JS files (ending in '.js' but not '.any.js').
for filename in $(find wasm/jsapi -name '*.js' -not -name '*.any.js'); do
  copy_file_relative ${filename} wasm/jsapi ${JS_API_TEST_DIR}/tests/wpt
done

log_and_run cd ${TMP_DIR}
log_and_run rm -rf wpt

###############################################################################
# Generate the proposal tests.
###############################################################################

repos='js-promise-integration exception-handling js-types tail-call memory64 extended-const multi-memory function-references gc'

for repo in ${repos}; do
  new_section "Process ${repo}: core tests"
  # Add the proposal repo to the existing spec repo to reduce download size, then copy it over.
  log_and_run cd ${TMP_DIR}/spec
  log_and_run git fetch https://github.com/WebAssembly/${repo} main
  log_and_run cd ${TMP_DIR}
  log_and_run cp -r spec ${repo}
  log_and_run cd ${repo}
  log_and_run git checkout FETCH_HEAD
  # Determine the merge base, i.e. the first common commit in the history.
  log git merge-base HEAD ${SPEC_HASH}
  MERGE_BASE=$(git merge-base HEAD ${SPEC_HASH})
  echo "Using merge base ${MERGE_BASE}"
  CHANGED_FILES=${TMP_DIR}/${repo}/changed-files-since-merge-base
  log git diff --name-only ${MERGE_BASE} -- test/core test/js-api
  git diff --name-only ${MERGE_BASE} -- test/core test/js-api >$CHANGED_FILES
  # Compile the spec interpreter to generate the .js test cases later.
  # TODO: Switch to distclean and remove this manual "rm" once all proposals are rebased.
  log_and_run rm -f interpreter/wasm
  log_and_run make -C interpreter clean wasm

  DST_DIR=${SPEC_TEST_DIR}/tests/proposals/${repo}
  # Iterate over all proposal tests. Those which differ from the spec tests are
  # copied to the output directory and converted to .js tests.
  for filename in $(find test/core -name '*.wast'); do
    if [ -f ${TMP_DIR}/spec/$filename ] && cmp -s $filename ${TMP_DIR}/spec/$filename; then
      echo "Test identical to the spec repo: ${filename}"
    elif ! grep -E "^${filename}$" $CHANGED_FILES >/dev/null; then
      echo "Test unchanged since merge base: ${filename}"
    else
      echo "Changed test: ${filename}"
      copy_file_relative ${filename} test/core ${DST_DIR}
      log_and_run test/core/run.py --wasm interpreter/wasm --out ${TMP_BUILD_DIR} ${filename}
      DST_FILE=${DST_DIR}/${filename#test/core/}
      log_and_run mv -v ${TMP_BUILD_DIR}/*.js $(dirname ${DST_FILE})
    fi
  done

  new_section "Process ${repo}: js-api tests"
  for filename in $(find test/js-api -name '*.any.js'); do
    if [ -f ${TMP_DIR}/spec/$filename ] && cmp -s $filename ${TMP_DIR}/spec/$filename; then
      echo "Test identical to the spec repo: ${filename}"
    elif ! grep -E "^${filename}$" $CHANGED_FILES >/dev/null; then
      echo "Test unchanged since merge base: ${filename}"
    else
      echo "Changed test: ${filename}"
      copy_file_relative ${filename} test/js-api ${JS_API_TEST_DIR}/tests/proposals/${repo}
    fi
  done

  # Copy over helper JS files (ending in '.js' but not '.any.js').
  for filename in $(find test/js-api -name '*.js' -not -name '*.any.js'); do
    copy_file_relative ${filename} test/js-api ${JS_API_TEST_DIR}/tests/proposals/${repo}
  done

  log_and_run rm -rf ${repo}
done

###############################################################################
# Report and cleanup.
###############################################################################

cd ${SPEC_TEST_DIR}
new_section "The following files will get uploaded:"
ls -R tests
echo

cd ${JS_API_TEST_DIR}
ls -R tests
echo

log_and_run rm -rf ${TMP_DIR}

###############################################################################
# Upload all spec tests.
###############################################################################

echo "****************************************************************************"
echo "* For the following command you first have to authenticate with google cloud"
echo "* storage. For that you have to execute"
echo "*"
echo "* > gsutil.py config"
echo "*"
echo "* When the script asks you for your project-id, use 0."
echo "****************************************************************************"
log_and_run cd ${SPEC_TEST_DIR}
log_and_run upload_to_google_storage.py -a -b v8-wasm-spec-tests tests

log_and_run cd ${JS_API_TEST_DIR}
log_and_run upload_to_google_storage.py -a -b v8-wasm-spec-tests tests
