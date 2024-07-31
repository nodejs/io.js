#!/bin/sh
set -e
set -x
# Shell script to update libuv in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/libuv/libuv/releases/latest',
  process.env.GITHUB_TOKEN && {
    headers: {
      "Authorization": `Bearer ${process.env.GITHUB_TOKEN}`
    },
  });
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const { tag_name } = await res.json();
console.log(tag_name.replace('v', ''));
EOF
)"

VERSION_H="$DEPS_DIR/uv/include/uv/version.h"
CURRENT_MAJOR_VERSION=$(grep "#define UV_VERSION_MAJOR" "$VERSION_H" | sed -n "s/^.*MAJOR \(.*\)/\1/p")
CURRENT_MINOR_VERSION=$(grep "#define UV_VERSION_MINOR" "$VERSION_H" | sed -n "s/^.*MINOR \(.*\)/\1/p")
CURRENT_PATCH_VERSION=$(grep "#define UV_VERSION_PATCH" "$VERSION_H" | sed -n "s/^.*PATCH \(.*\)/\1/p")
CURRENT_IS_RELEASE=$(grep "#define UV_VERSION_IS_RELEASE" "$VERSION_H" | sed -n "s/^.*RELEASE \(.*\)/\1/p")
CURRENT_SUFFIX_VERSION=$(grep "#define UV_VERSION_SUFFIX" "$VERSION_H" | sed -n "s/^.*SUFFIX \"\(.*\)\"/\1/p")
SUFFIX_STRING=$([ "$CURRENT_IS_RELEASE" = 1 ] || [ -z "$CURRENT_SUFFIX_VERSION" ] && echo "" || echo "-$CURRENT_SUFFIX_VERSION")
CURRENT_VERSION="$CURRENT_MAJOR_VERSION.$CURRENT_MINOR_VERSION.$CURRENT_PATCH_VERSION$SUFFIX_STRING"

# This function exit with 0 if new version and current version are the same
compare_dependency_version "libuv" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

cd "$WORKSPACE"

LIBUV_TARBALL="libuv-v$NEW_VERSION.tar.gz"

echo "Fetching libuv source archive..."
curl -sL -o "$LIBUV_TARBALL" "https://api.github.com/repos/libuv/libuv/tarball/v$NEW_VERSION"
log_and_verify_sha256sum "libuv" "$LIBUV_TARBALL"
gzip -dc "$LIBUV_TARBALL" | tar xf -
rm "$LIBUV_TARBALL"
mv libuv-libuv-* uv

echo "Replacing existing libuv (except GYP and GN build files)"
mv "$DEPS_DIR/uv/"*.gyp "$DEPS_DIR/uv/"*.gypi "$DEPS_DIR/uv/"*.gn "$DEPS_DIR/uv/"*.gni "$WORKSPACE/uv/"
rm -rf "$DEPS_DIR/uv"
mv "$WORKSPACE/uv" "$DEPS_DIR/"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "libuv" "$NEW_VERSION"
