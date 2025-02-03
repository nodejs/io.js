#!/bin/sh

set -xe

BUILD_ARCH_TYPE=$1
V8_BUILD_OPTIONS=$2

cd deps/v8 || exit
find . -type d -name .git -print0 | xargs -0 rm -rf
../../tools/v8/fetch_deps.py .

JOBS_ARG=
if [ "${JOBS}" ]; then
  JOBS_ARG="-j ${JOBS}"
fi

ARCH=$(arch)
if [ "$ARCH" = "s390x" ] || [ "$ARCH" = "ppc64le" ]; then
  TARGET_ARCH=$ARCH
  if [ "$ARCH" = "ppc64le" ]; then
    TARGET_ARCH="ppc64"
  fi
  # set paths manually for now to use locally installed gn
  export BUILD_TOOLS=/home/iojs/build-tools
  export LD_LIBRARY_PATH="$BUILD_TOOLS:$LD_LIBRARY_PATH"
  rm -f "$BUILD_TOOLS/g++"
  rm -f "$BUILD_TOOLS/gcc"
  # V8's build config looks for binaries called `gcc` and `g++` if not using
  # clang. Ensure that `gcc` and `g++` point to the compilers we want to
  # invoke, creating symbolic links placed at the front of PATH, if needed.
  # Avoid linking to ccache symbolic links as ccache decides which binary
  # to run based on the name of the link (i.e. `gcc`/`g++` in our case).
  # shellcheck disable=SC2154
  if [ "$CC" != "" ] && [ "$CC" != "gcc" ]; then
    CC_PATH=$(command -v "$CC" gcc | grep -v ccache | head -n 1)
    ln -s "$CC_PATH" "$BUILD_TOOLS/gcc"
  fi
  # shellcheck disable=SC2154
  if [ "$CXX" != "" ] && [ "$CXX" != "g++" ]; then
    CXX_PATH=$(command -v "$CXX" g++ | grep -v ccache | head -n 1)
    ln -s "$CXX_PATH" "$BUILD_TOOLS/g++"
  fi
  export PATH="$BUILD_TOOLS:$PATH"
  # Propagate ccache to gn.
  case "$CXX" in
    *ccache*) CC_WRAPPER="cc_wrapper=\"ccache\"" ;;
    *) ;;
  esac

  g++ --version
  gcc --version
  export PKG_CONFIG_PATH=$BUILD_TOOLS/pkg-config
  gn gen -v "out.gn/$BUILD_ARCH_TYPE" --args="is_component_build=false is_debug=false use_goma=false goma_dir=\"None\" use_custom_libcxx=false v8_target_cpu=\"$TARGET_ARCH\" target_cpu=\"$TARGET_ARCH\" v8_enable_backtrace=true $CC_WRAPPER"
  ninja -v -C "out.gn/$BUILD_ARCH_TYPE" "${JOBS_ARG}" d8 cctest inspector-test
else
  DEPOT_TOOLS_DIR="$(cd _depot_tools && pwd)"
  # shellcheck disable=SC2086
  PATH="$DEPOT_TOOLS_DIR":$PATH tools/dev/v8gen.py "$BUILD_ARCH_TYPE" $V8_BUILD_OPTIONS
  PATH="$DEPOT_TOOLS_DIR":$PATH ninja -C "out.gn/$BUILD_ARCH_TYPE/" "${JOBS_ARG}" d8 cctest inspector-test
fi
