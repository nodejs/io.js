#!/bin/bash
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex

cd "$( dirname "${BASH_SOURCE[0]}" )"
cd ../..

BUILD_DIR=out/mk_wasm_fuzzer_corpus
REPO_DIR=out/wasm_fuzzer_corpus

rm -rf $BUILD_DIR $REPO_DIR

# Clone repo but to a state where all files are not checked in (i.e. deleted)
git clone --filter="blob:none" --no-checkout https://chromium.googlesource.com/v8/fuzzer_wasm_corpus $REPO_DIR

# Build optdebug such that the --dump-wasm-module flag is available.
gn gen $BUILD_DIR --args='is_debug=true v8_optimized_debug=true target_cpu="x64" use_remoteexec=true'
autoninja -C $BUILD_DIR

./tools/run-tests.py --outdir=$BUILD_DIR --extra-flags="--dump-wasm-module \
  --dump-wasm-module-path=./$REPO_DIR/"

rm -rf $BUILD_DIR

# Delete items over 20k.
find $REPO_DIR -type f -size +20k | xargs rm

# Upload changes.
echo "Generation completed."

git -C $REPO_DIR add .
git -C $REPO_DIR commit -m "Update corpus"
git -C $REPO_DIR push origin HEAD:refs/for/main

echo "Once the change is submitted, Skia autoroller will update chromium/src."
