#!/usr/bin/env bash

# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script will build libgcmole.so as well as a corresponding recent
# version of Clang and LLVM. The Clang will be built with the locally
# installed compiler and statically link against the local libstdc++ so
# that the resulting binary is easier transferable between different
# environments.

LLVM_RELEASE=19.1.3

BUILD_TYPE="Release"
# BUILD_TYPE="Debug"
THIS_DIR="$(readlink -f "$(dirname "${0}")")"
LLVM_PROJECT_DIR="${THIS_DIR}/bootstrap/llvm"
BUILD_DIR="${THIS_DIR}/bootstrap/build"

# Die if any command dies.
set -e

OS="$(uname -s)"

# Xcode and clang don't get along when predictive compilation is enabled.
# http://crbug.com/96315
if [[ "${OS}" = "Darwin" ]] && xcodebuild -version | grep -q 'Xcode 3.2' ; then
  XCONF=com.apple.Xcode
  if [[ "${GYP_GENERATORS}" != "make" ]] && \
     [ "$(defaults read "${XCONF}" EnablePredictiveCompilation)" != "0" ]; then
    echo
    echo "          HEARKEN!"
    echo "You're using Xcode3 and you have 'Predictive Compilation' enabled."
    echo "This does not work well with clang (http://crbug.com/96315)."
    echo "Disable it in Preferences->Building (lower right), or run"
    echo "    defaults write ${XCONF} EnablePredictiveCompilation -boolean NO"
    echo "while Xcode is not running."
    echo
  fi

  SUB_VERSION=$(xcodebuild -version | sed -Ene 's/Xcode 3\.2\.([0-9]+)/\1/p')
  if [[ "${SUB_VERSION}" < 6 ]]; then
    echo
    echo "          YOUR LD IS BUGGY!"
    echo "Please upgrade Xcode to at least 3.2.6."
    echo
  fi
fi

echo Getting LLVM release "${LLVM_RELEASE}" in "${LLVM_PROJECT_DIR}"
if ! [ -d "${LLVM_PROJECT_DIR}" ] || ! git -C "${LLVM_PROJECT_DIR}" remote get-url origin | grep -q -F "https://github.com/llvm/llvm-project.git" ; then
  rm -rf "${LLVM_PROJECT_DIR}"
  git clone --depth=1 --branch "llvmorg-${LLVM_RELEASE}" "https://github.com/llvm/llvm-project.git" "${LLVM_PROJECT_DIR}"
else
  git -C "${LLVM_PROJECT_DIR}" fetch --depth=1 origin "llvmorg-${LLVM_RELEASE}"
  git -C "${LLVM_PROJECT_DIR}" checkout FETCH_HEAD
fi

# Echo all commands
set -x

NUM_JOBS=3
if [[ "${OS}" = "Linux" ]]; then
  if [[ -e "/proc/cpuinfo" ]]; then
    NUM_JOBS="$(grep -c "^processor" /proc/cpuinfo)"
  else
    # Hack when running in chroot
    NUM_JOBS="32"
  fi
elif [ "${OS}" = "Darwin" ]; then
  NUM_JOBS="$(sysctl -n hw.ncpu)"
fi

# Build clang.
if [ ! -e "${BUILD_DIR}" ]; then
  mkdir "${BUILD_DIR}"
fi
cd "${BUILD_DIR}"
cmake -GNinja -DCMAKE_CXX_FLAGS="-static-libstdc++" -DLLVM_ENABLE_TERMINFO=OFF \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DLLVM_ENABLE_PROJECTS=clang \
    -DLLVM_ENABLE_Z3_SOLVER=OFF "${LLVM_PROJECT_DIR}/llvm"
MACOSX_DEPLOYMENT_TARGET=10.5 ninja -j"${NUM_JOBS}" clang

if [[ "${BUILD_TYPE}" = "Release" ]]; then
  # Strip the clang binary.
  STRIP_FLAGS=
  if [ "${OS}" = "Darwin" ]; then
    # See http://crbug.com/256342
    STRIP_FLAGS=-x
  fi
  strip ${STRIP_FLAGS} bin/clang
fi
cd -

# Build libgcmole.so
make -C "${THIS_DIR}" clean
make -C "${THIS_DIR}" LLVM_SRC_ROOT="${LLVM_PROJECT_DIR}/llvm" \
    CLANG_SRC_ROOT="${LLVM_PROJECT_DIR}/clang" \
    BUILD_ROOT="${BUILD_DIR}" $BUILD_TYPE

set +x

echo '#########################################################################'
echo 'Congratulations you compiled clang and libgcmole.so'
echo
echo '# You can now run gcmole:'
echo 'tools/gcmole/gcmole.py \'
echo '   --clang-bin-dir="tools/gcmole/bootstrap/build/bin" \'
echo '   --clang-plugins-dir="tools/gcmole" \'
echo '   --v8-target-cpu=$CPU'
echo
