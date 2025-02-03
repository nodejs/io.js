#!/bin/sh
set -e
# Shell script to update sqlite in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

JOINT_OUTPUT="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://sqlite.org/download.html');
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const body = await res.text();
// The downloads page contains product info in a machine readable comment.
const match = body?.match(/PRODUCT,(\d+\.\d+\.\d+),(\d+\/sqlite-amalgamation-\d+\.zip),/);
if (!match) throw new Error('Could not find SQLite amalgamation product data');
const [, version, amalgamation] = match;
console.log(`${version}:${amalgamation}`);
EOF
)"

IFS=: read -r NEW_VERSION AMALGAMATION <<EOF
$JOINT_OUTPUT
EOF

CURRENT_VERSION=$(grep "#define SQLITE_VERSION\s" "$DEPS_DIR/sqlite/sqlite3.h" | cut -d '"' -f2)

# This function exits with 0 if new version and current version are the same
compare_dependency_version "sqlite" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace"

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')
echo "$WORKSPACE"
cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

SQLITE_ZIP="sqlite-$NEW_VERSION"
cd "$WORKSPACE"

echo "Fetching SQLITE source archive..."
curl -sL -o "$SQLITE_ZIP.zip" "https://sqlite.org/$AMALGAMATION"

log_and_verify_sha256sum "sqlite" "$SQLITE_ZIP.zip"

echo "Unzipping..."
unzip -j "$SQLITE_ZIP.zip" -d "$WORKSPACE/sqlite/"
rm "$SQLITE_ZIP.zip"

echo "Copying new files to deps folder"
cp "$WORKSPACE/sqlite/sqlite3.c" "$DEPS_DIR/sqlite/"
cp "$WORKSPACE/sqlite/sqlite3.h" "$DEPS_DIR/sqlite/"
cp "$WORKSPACE/sqlite/sqlite3ext.h" "$DEPS_DIR/sqlite/"

echo ""

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "sqlite" "$NEW_VERSION"
