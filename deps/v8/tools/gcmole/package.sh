#!/usr/bin/env bash

# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script will package a built gcmole plugin together with the
# corresponding clang binary into an archive which can be used on the
# buildbot infrastructure to be run against V8 checkouts.

THIS_DIR="$(readlink -f "$(dirname "${0}")")"

PACKAGE_DIR="${THIS_DIR}/gcmole-tools"
PACKAGE_FILE="${THIS_DIR}/gcmole-tools.tar.gz"
PACKAGE_SUM="${THIS_DIR}/gcmole-tools.tar.gz.sha1"
BUILD_DIR="${THIS_DIR}/bootstrap/build"
V8_ROOT_DIR= `realpath "${THIS_DIR}/../.."`

# Echo all commands
set -x

# Clean out any old files
if [ -e "${PACKAGE_DIR:?}/bin" ] ; then
  rm -rf "${PACKAGE_DIR:?}/bin"
fi
if [ -e "${PACKAGE_DIR:?}/lib" ] ; then
  rm -rf "${PACKAGE_DIR:?}/lib"
fi

# Copy all required files
mkdir -p "${PACKAGE_DIR}/bin"
cp "${BUILD_DIR}/bin/clang++" "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/lib"
cp -r "${BUILD_DIR}/lib/clang" "${PACKAGE_DIR}/lib"
cp "${THIS_DIR}/libgcmole.so" "${PACKAGE_DIR}"

# Generate the archive. Set some flags on tar to make the output more
# deterministic (e.g. not dependent on timestamps).
cd "$(dirname "${PACKAGE_DIR}")"
tar \
  --sort=name \
  --owner=root:0 \
  --group=root:0 \
  --mtime="UTC 1970-01-01" \
  --create \
  "$(basename "${PACKAGE_DIR}")" | gzip --no-name >"${PACKAGE_FILE}"

# Generate checksum
sha1sum "${PACKAGE_FILE}" | awk '{print $1}' > "${PACKAGE_SUM}"

set +x

echo
echo You can find a packaged version of gcmole here:
echo
echo $(readlink -f "${PACKAGE_FILE}")
echo
echo Upload the update package to the chrome infra:
echo
echo 'gsutil.py cp tools/gcmole/gcmole-tools.tar.gz gs://chrome-v8-gcmole/$(cat tools/gcmole/gcmole-tools.tar.gz.sha1)'
echo
echo Run bootstrap.sh in chroot if glibc versions mismatch with bots:
echo '# Create chroot'
echo 'mkdir -p $CHROOT_DIR'
echo 'sudo debootstrap $DEBIAN_VERSION $CHROOT_DIR'
echo 'sudo chroot $CHROOT_DIR apt install g++ cmake python git'
echo '# Mount ./depot_tools and ./v8 dirs in the chroot:'
echo 'sudo chroot $CHROOT_DIR mkdir /docs'
echo 'sudo mount --bind /path/to/workspace /docs'
echo '# Build gcmole'
echo "sudo chroot \$CHROOT_DIR bash -c 'PATH=/docs/depot_tools:\$PATH; /docs/v8/v8/tools/gcmole/bootstrap.sh'"
echo
echo You can now run gcmole using this command:
echo
echo 'tools/gcmole/gcmole.py \'
echo '   --clang-bin-dir="tools/gcmole/gcmole-tools/bin" \'
echo '   --clang-plugins-dir="tools/gcmole/gcmole-tools" \'
echo '   --v8-target-cpu=$CPU'
echo
