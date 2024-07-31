#!/bin/sh
set -e
# Shell script to update nbytes in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/nodejs/nbytes/releases/latest',
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

CURRENT_VERSION=$(grep "#define NBYTES_VERSION" "$DEPS_DIR/nbytes/nbytes.h" | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "nbytes" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

cd "$WORKSPACE"

echo "Fetching nbytes source archive..."
NBYTES_TARBALL="nbytes-v$NEW_VERSION.tar.gz"
curl -sL -o "$NBYTES_TARBALL" "https://api.github.com/repos/nodejs/nbytes/tarball/v$NEW_VERSION"
log_and_verify_sha256sum "nbytes" "$NBYTES_TARBALL"
gzip -dc "$NBYTES_TARBALL" | tar xf -
mv nodejs-nbytes-* source
rm "$NBYTES_TARBALL"

echo "Replacing existing nbytes (except GYP and GN build files)"
mv "$DEPS_DIR/nbytes/"*.gyp "$DEPS_DIR/nbytes/"*.gn "$DEPS_DIR/nbytes/"*.gni "$DEPS_DIR/nbytes/README.md" "$WORKSPACE/source"
rm -rf "$DEPS_DIR/nbytes"
mv "$WORKSPACE/source" "$DEPS_DIR/nbytes"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "nbytes" "$NEW_VERSION"
