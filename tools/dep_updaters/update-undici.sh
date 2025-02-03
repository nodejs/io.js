#!/bin/sh

# Shell script to update undici in the source tree to the latest release.

# This script must be in the tools directory when it runs because it uses the
# script source file path to determine directories to work in.

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$ROOT/deps"
[ -z "$NODE" ] && NODE="$ROOT/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)
NPM="$ROOT/deps/npm/bin/npm-cli.js"

# shellcheck disable=SC1091
. "$ROOT/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/nodejs/undici/releases/latest',
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

CURRENT_VERSION=$("$NODE" -p "require('$DEPS_DIR/undici/src/package.json').version")

echo "$CURRENT_VERSION"
echo "$NEW_VERSION"

# This function exit with 0 if new version and current version are the same
compare_dependency_version "undici" "$NEW_VERSION" "$CURRENT_VERSION"

rm -rf deps/undici/src
rm -f deps/undici/undici.js

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')
echo "$WORKSPACE"
cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

UNDICI_ZIP="undici-$NEW_VERSION"
cd "$WORKSPACE"

echo "Fetching UNDICI source archive..."
curl -sL -o "$UNDICI_ZIP.zip" "https://github.com/nodejs/undici/archive/refs/tags/v$NEW_VERSION.zip"

log_and_verify_sha256sum "undici" "$UNDICI_ZIP.zip"

echo "Unzipping..."
unzip "$UNDICI_ZIP.zip" -d "src"
mv "src/$UNDICI_ZIP" "$DEPS_DIR/undici/src"
rm "$UNDICI_ZIP.zip"
cd "$ROOT"

(
  cd "$DEPS_DIR/undici/src"

  # remove components we don't need to keep in nodejs/deps
  rm -rf .husky || true
  rm -rf .github || true
  rm -rf test
  rm -rf benchmarks
  rm -rf docs-tmp
  mv docs docs-tmp
  mkdir docs
  mv docs-tmp/docs docs/docs
  rm -rf docs-tmp

  # Rebuild components from source
  rm lib/llhttp/llhttp*.*
  "$NODE" "$NPM" install --ignore-scripts
  "$NODE" "$NPM" run build:wasm > lib/llhttp/wasm_build_env.txt
  "$NODE" "$NPM" run build:node
  "$NODE" "$NPM" prune --production
)

# update version information in src/undici_version.h
cat > "$ROOT/src/undici_version.h" <<EOF
// This is an auto generated file, please do not edit.
// Refer to tools/dep_updaters/update-undici.sh
#ifndef SRC_UNDICI_VERSION_H_
#define SRC_UNDICI_VERSION_H_
#define UNDICI_VERSION "$NEW_VERSION"
#endif  // SRC_UNDICI_VERSION_H_
EOF

mv deps/undici/src/undici-fetch.js deps/undici/undici.js
cp deps/undici/src/LICENSE deps/undici/LICENSE

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "undici" "$NEW_VERSION" "src/undici_version.h"
