#!/bin/sh
set -e
# Shell script to update nghttp2 in the source tree to specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/nghttp2/nghttp2/releases/latest',
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

CURRENT_VERSION=$(grep "#define NGHTTP2_VERSION" ./deps/nghttp2/lib/includes/nghttp2/nghttp2ver.h | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "nghttp2" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace"

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

NGHTTP2_REF="v$NEW_VERSION"
NGHTTP2_TARBALL="nghttp2-$NEW_VERSION.tar.gz"

cd "$WORKSPACE"

echo "Fetching nghttp2 source archive"
curl -sL -o "$NGHTTP2_TARBALL" "https://github.com/nghttp2/nghttp2/releases/download/$NGHTTP2_REF/$NGHTTP2_TARBALL"

DEPOSITED_CHECKSUM=$(curl -sL "https://github.com/nghttp2/nghttp2/releases/download/$NGHTTP2_REF/checksums.txt" | grep "$NGHTTP2_TARBALL")

log_and_verify_sha256sum "nghttp2" "$NGHTTP2_TARBALL" "$DEPOSITED_CHECKSUM"

gzip -dc "$NGHTTP2_TARBALL" | tar xf -
rm "$NGHTTP2_TARBALL"
mv "nghttp2-$NEW_VERSION" nghttp2

echo "Removing everything, except lib/ and COPYING"
cd nghttp2
for dir in *; do
  if [ "$dir" = "lib" ] || [ "$dir" = "COPYING" ]; then
    continue
  fi
  rm -rf "$dir"
done

# Refs: https://github.com/nodejs/node/issues/45572
echo "Copying config.h file"
cp "$DEPS_DIR/nghttp2/lib/includes/config.h" "$WORKSPACE/nghttp2/lib/includes"

echo "Copying existing gyp files"
cp "$DEPS_DIR/nghttp2/nghttp2.gyp" "$WORKSPACE/nghttp2"

echo "Copying existing GN files"
cp "$DEPS_DIR/nghttp2/"*.gn "$DEPS_DIR/nghttp2/"*.gni "$WORKSPACE/nghttp2"

echo "Replacing existing nghttp2"
rm -rf "$DEPS_DIR/nghttp2"
mv "$WORKSPACE/nghttp2" "$DEPS_DIR/"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "nghttp2" "$NEW_VERSION"
