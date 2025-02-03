#!/bin/sh

# Shell script to update minimatch in the source tree to the latest release.

# This script must be in the tools directory when it runs because it uses the
# script source file path to determine directories to work in.

set -ex

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)
DEPS_DIR="$BASE_DIR/deps"
NPM="$DEPS_DIR/npm/bin/npm-cli.js"

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION=$("$NODE" "$NPM" view minimatch dist-tags.latest)
CURRENT_VERSION=$("$NODE" -p "require('./deps/minimatch/package.json').version")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "minimatch" "$NEW_VERSION" "$CURRENT_VERSION"

cd "$( dirname "$0" )/../.." || exit

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

cd "$WORKSPACE"

echo "Fetching minimatch source archive..."

"$NODE" "$NPM" pack "minimatch@$NEW_VERSION"

MINIMATCH_TGZ="minimatch-$NEW_VERSION.tgz"

log_and_verify_sha256sum "minimatch" "$MINIMATCH_TGZ"

rm -r "$DEPS_DIR/minimatch"/*

tar -xf "$MINIMATCH_TGZ"

cd package

"$NODE" "$NPM" install esbuild --save-dev

"$NODE" "$NPM" pkg set scripts.node-build="esbuild ./dist/commonjs/index.js --bundle --platform=node --outfile=index.js"

"$NODE" "$NPM" run node-build

rm -rf node_modules

mv ./* "$DEPS_DIR/minimatch"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "minimatch" "$NEW_VERSION"
