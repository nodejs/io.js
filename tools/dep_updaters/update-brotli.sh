#!/bin/sh
set -e
# Shell script to update brotli in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/google/brotli/releases/latest',
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

CURRENT_MAJOR_VERSION=$(grep "#define BROTLI_VERSION_MAJOR" ./deps/brotli/c/common/version.h | sed -n "s/^.*MAJOR \(.*\)/\1/p")
CURRENT_MINOR_VERSION=$(grep "#define BROTLI_VERSION_MINOR" ./deps/brotli/c/common/version.h | sed -n "s/^.*MINOR \(.*\)/\1/p")
CURRENT_PATCH_VERSION=$(grep "#define BROTLI_VERSION_PATCH" ./deps/brotli/c/common/version.h | sed -n "s/^.*PATCH \(.*\)/\1/p")
CURRENT_VERSION="$CURRENT_MAJOR_VERSION.$CURRENT_MINOR_VERSION.$CURRENT_PATCH_VERSION"

# This function exit with 0 if new version and current version are the same
compare_dependency_version "brotli" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace"

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

cd "$WORKSPACE"

BROTLI_TARBALL="brotli-v$NEW_VERSION.tar.gz"

echo "Fetching brotli source archive"
curl -sL -o "$BROTLI_TARBALL" "https://github.com/google/brotli/archive/v$NEW_VERSION.tar.gz"
log_and_verify_sha256sum "brotli" "$BROTLI_TARBALL"
gzip -dc "$BROTLI_TARBALL" | tar xf -
rm "$BROTLI_TARBALL"
mv "brotli-$NEW_VERSION" "brotli"

echo "Copying existing gyp file"
cp "$DEPS_DIR/brotli/brotli.gyp" "$WORKSPACE/brotli"

echo "Copying existing GN files"
cp "$DEPS_DIR/brotli/"*.gn "$DEPS_DIR/brotli/"*.gni "$WORKSPACE/brotli"

echo "Deleting existing brotli"
rm -rf "$DEPS_DIR/brotli"
mkdir "$DEPS_DIR/brotli"

echo "Update c and LICENSE"
mv "$WORKSPACE/brotli/"*.gn "$WORKSPACE/brotli/"*.gni "$WORKSPACE/brotli/c" "$WORKSPACE/brotli/LICENSE" "$WORKSPACE/brotli/brotli.gyp" "$DEPS_DIR/brotli"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "brotli" "$NEW_VERSION"
