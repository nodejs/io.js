#!/bin/sh
set -e
# Shell script to update ngtcp2 in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/ngtcp2/ngtcp2/releases',
  process.env.GITHUB_TOKEN && {
    headers: {
      "Authorization": `Bearer ${process.env.GITHUB_TOKEN}`
    },
  });
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const releases = await res.json()
const { tag_name } = releases.at(0);
console.log(tag_name.replace('v', ''));
EOF
)"

NGTCP2_VERSION_H="$DEPS_DIR/ngtcp2/ngtcp2/lib/includes/ngtcp2/version.h"

CURRENT_VERSION=$(grep "#define NGTCP2_VERSION" "$NGTCP2_VERSION_H" | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "ngtcp2" "$NEW_VERSION" "$CURRENT_VERSION"

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

NGTCP2_REF="v$NEW_VERSION"
NGTCP2_ZIP="ngtcp2-$NEW_VERSION"

cd "$WORKSPACE"

echo "Fetching ngtcp2 source archive..."
curl -sL -o "$NGTCP2_ZIP.zip" "https://github.com/ngtcp2/ngtcp2/archive/refs/tags/$NGTCP2_REF.zip"
log_and_verify_sha256sum "ngtcp2" "$NGTCP2_ZIP.zip"
unzip "$NGTCP2_ZIP.zip"
rm "$NGTCP2_ZIP.zip"
mv "$NGTCP2_ZIP" ngtcp2

cd ngtcp2

autoreconf -i

# For Mac users who have installed libev with MacPorts, append
# ',-L/opt/local/lib' to LDFLAGS, and also pass
# CPPFLAGS="-I/opt/local/include" to ./configure.

./configure --prefix="$PWD/build" --enable-lib-only 

replace_dir "$DEPS_DIR/ngtcp2/ngtcp2/lib" "lib"
replace_dir "$DEPS_DIR/ngtcp2/ngtcp2/crypto" "crypto"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "ngtcp2" "$NEW_VERSION"
