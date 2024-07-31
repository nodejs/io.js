#!/bin/sh

# Shell script to update acorn-walk in the source tree to the latest release.

# This script must be in the tools directory when it runs because it uses the
# script source file path to determine directories to work in.

set -ex

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)
NPM="$BASE_DIR/deps/npm/bin/npm-cli.js"
DEPS_DIR="$BASE_DIR/deps"

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION=$("$NODE" "$NPM" view acorn-walk dist-tags.latest)
CURRENT_VERSION=$("$NODE" -p "require('./deps/acorn/acorn-walk/package.json').version")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "acorn-walk" "$NEW_VERSION" "$CURRENT_VERSION"

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

echo "Fetching acorn-walk source archive..."

"$NODE" "$NPM" pack "acorn-walk@$NEW_VERSION"

ACORN_WALK_TGZ="acorn-walk-$NEW_VERSION.tgz"

log_and_verify_sha256sum "acorn-walk" "$ACORN_WALK_TGZ"

rm -r "$DEPS_DIR/acorn/acorn-walk"/*

tar -xf "$ACORN_WALK_TGZ"

mv package/* "$DEPS_DIR/acorn/acorn-walk"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "acorn-walk" "$NEW_VERSION"
