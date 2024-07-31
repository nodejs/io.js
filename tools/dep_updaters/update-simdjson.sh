#!/bin/sh
set -e
# Shell script to update simdjson in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/simdjson/simdjson/releases/latest');
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const { tag_name } = await res.json();
console.log(tag_name.replace('v', ''));
EOF
)"
CURRENT_VERSION=$(grep "#define SIMDJSON_VERSION" "$DEPS_DIR/simdjson/simdjson.h" | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

if [ "$NEW_VERSION" = "$CURRENT_VERSION" ]; then
  echo "Skipped because simdjson is on the latest version."
  exit 0
fi

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

SIMDJSON_REF="v$NEW_VERSION"
SIMDJSON_ZIP="simdjson-$NEW_VERSION.zip"

cd "$WORKSPACE"

echo "Fetching simdjson source archive..."
curl -sL -o "$SIMDJSON_ZIP" "https://github.com/simdjson/simdjson/archive/refs/tags/$SIMDJSON_REF.zip"
unzip "$SIMDJSON_ZIP"
cd "simdjson-$NEW_VERSION"
mv "singleheader/simdjson.h" "$DEPS_DIR/simdjson"
mv "singleheader/simdjson.cpp" "$DEPS_DIR/simdjson"
mv "LICENSE" "$DEPS_DIR/simdjson"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "simdjson" "$NEW_VERSION"
