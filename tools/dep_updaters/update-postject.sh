#!/bin/sh

# Shell script to update postject in the source tree to the latest release.

# This script must be in the tools directory when it runs because it uses the
# script source file path to determine directories to work in.

set -ex

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)
NPM="$BASE_DIR/deps/npm/bin/npm-cli.js"

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION=$("$NODE" "$NPM" view postject dist-tags.latest)
CURRENT_VERSION=$("$NODE" -p "require('./test/fixtures/postject-copy/node_modules/postject/package.json').version")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "postject" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

cd "$WORKSPACE"

echo "Installing postject npm package..."

"$NODE" "$NPM" init --yes

"$NODE" "$NPM" install --no-bin-links --ignore-scripts "postject@$NEW_VERSION"

echo "Replacing existing postject (except GN build files)"

mv "$DEPS_DIR/postject/"*.gn "$DEPS_DIR/postject/"*.gni "$WORKSPACE/"
rm -rf "$DEPS_DIR/postject"
mkdir "$DEPS_DIR/postject"
mv "$WORKSPACE/"*.gn "$WORKSPACE/"*.gni "$DEPS_DIR/postject"
mv "$WORKSPACE/node_modules/postject/LICENSE" "$DEPS_DIR/postject"
mv "$WORKSPACE/node_modules/postject/dist/postject-api.h" "$DEPS_DIR/postject"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "postject" "$NEW_VERSION"
