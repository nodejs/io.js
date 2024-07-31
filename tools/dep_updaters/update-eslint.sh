#!/bin/sh

# Shell script to update ESLint in the source tree to the latest release.

# This script must be in the tools directory when it runs because it uses the
# script source file path to determine directories to work in.

set -ex

ROOT=$(cd "$(dirname "$0")/../.." && pwd)

[ -z "$NODE" ] && NODE="$ROOT/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)
NPM="$ROOT/deps/npm/bin/npm-cli.js"

# shellcheck disable=SC1091
. "$ROOT/tools/dep_updaters/utils.sh"

NEW_VERSION=$("$NODE" "$NPM" view eslint dist-tags.latest)
CURRENT_VERSION=$("$NODE" -p "require('./tools/eslint/node_modules/eslint/package.json').version")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "eslint" "$NEW_VERSION" "$CURRENT_VERSION"

cd "$( dirname "$0" )" || exit
rm -rf ../eslint/node_modules ../eslint/package-lock.json

cd ../eslint
"$NODE" "$NPM" install \
    --ignore-scripts \
    --no-bin-links \
    "eslint@$NEW_VERSION" \
    eslint-formatter-tap \
    eslint-plugin-jsdoc \
    eslint-plugin-markdown \
    globals \
    @babel/core \
    @babel/eslint-parser \
    @babel/plugin-syntax-import-attributes \
    @stylistic/eslint-plugin-js

# Use dmn to remove some unneeded files.
"$NODE" "$NPM" exec --package=dmn@3.0.1 --yes -- dmn -f clean
# TODO: Get this into dmn.
find node_modules \( -name .package-lock.json -or -name 'README*' \) -exec rm {} \;

# The last line of the script should always print the new version,
# as we need to add it to $GITHUB_ENV variable.
echo "NEW_VERSION=$NEW_VERSION"
