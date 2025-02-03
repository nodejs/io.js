#!/bin/sh

# Shell script to update amaro in the source tree to the latest release.

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

NEW_VERSION=$("$NODE" "$NPM" view amaro dist-tags.latest)

CURRENT_VERSION=$("$NODE" -p "require('./deps/amaro/package.json').version")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "amaro" "$NEW_VERSION" "$CURRENT_VERSION"

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

echo "Fetching amaro source archive..."

"$NODE" "$NPM" pack "amaro@$NEW_VERSION"

amaro_TGZ="amaro-$NEW_VERSION.tgz"

log_and_verify_sha256sum "amaro" "$amaro_TGZ"

cp ./* "$DEPS_DIR/amaro/LICENSE"

rm -r "$DEPS_DIR/amaro"/*

tar -xf "$amaro_TGZ"

cd package

rm -rf node_modules

mv ./* "$DEPS_DIR/amaro"

# update version information in src/undici_version.h
cat > "$ROOT/src/amaro_version.h" <<EOF
// This is an auto generated file, please do not edit.
// Refer to tools/dep_updaters/update-amaro.sh
#ifndef SRC_AMARO_VERSION_H_
#define SRC_AMARO_VERSION_H_
#define AMARO_VERSION "$NEW_VERSION"
#endif  // SRC_AMARO_VERSION_H_
EOF

echo "All done!"
echo ""
echo "Please git add amaro, commit the new version:"
echo ""
echo "$ git add -A deps/amaro"
echo "$ git commit -m \"deps: update amaro to $NEW_VERSION\""
echo ""

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "amaro" "$NEW_VERSION" "src/amaro_version.h"
