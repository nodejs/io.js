#!/bin/sh
set -e
# Shell script to update simdutf in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/simdutf/simdutf/releases/latest',
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
CURRENT_VERSION=$(grep "#define SIMDUTF_VERSION" "$DEPS_DIR/simdutf/simdutf.h" | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "simdutf" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

SIMDUTF_REF="v$NEW_VERSION"
SIMDUTF_ZIP="simdutf-$SIMDUTF_REF.zip"
SIMDUTF_LICENSE="LICENSE-MIT"

cd "$WORKSPACE"

echo "Fetching simdutf source archive..."
curl -sL -o "$SIMDUTF_ZIP" "https://github.com/simdutf/simdutf/releases/download/$SIMDUTF_REF/singleheader.zip"
log_and_verify_sha256sum "simdutf" "$SIMDUTF_ZIP"
unzip "$SIMDUTF_ZIP"
rm "$SIMDUTF_ZIP"
rm ./*_demo.cpp

curl -sL -o "$SIMDUTF_LICENSE" "https://raw.githubusercontent.com/simdutf/simdutf/HEAD/LICENSE-MIT"

echo "Replacing existing simdutf (except GYP and GN build files)"
mv "$DEPS_DIR/simdutf/"*.gyp "$DEPS_DIR/simdutf/"*.gn "$DEPS_DIR/simdutf/"*.gni "$DEPS_DIR/simdutf/README.md" "$WORKSPACE/"
rm -rf "$DEPS_DIR/simdutf"
mv "$WORKSPACE" "$DEPS_DIR/simdutf"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "simdutf" "$NEW_VERSION"
