#!/bin/sh
set -e
# Shell script to update ada in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/ada-url/ada/releases/latest',
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

CURRENT_VERSION=$(grep "#define ADA_VERSION" "$DEPS_DIR/ada/ada.h" | sed -n "s/^.*VERSION \"\(.*\)\"/\1/p")

# This function exit with 0 if new version and current version are the same
compare_dependency_version "ada" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace..."

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

ADA_REF="v$NEW_VERSION"
ADA_ZIP="ada-$ADA_REF.zip"
ADA_LICENSE="LICENSE-MIT"

cd "$WORKSPACE"

echo "Fetching ada source archive..."
curl -sL -o "$ADA_ZIP" "https://github.com/ada-url/ada/releases/download/$ADA_REF/singleheader.zip"
log_and_verify_sha256sum "ada" "$ADA_ZIP"
unzip "$ADA_ZIP"
rm "$ADA_ZIP"

curl -sL -o "$ADA_LICENSE" "https://raw.githubusercontent.com/ada-url/ada/HEAD/LICENSE-MIT"

echo "Replacing existing ada (except GYP and GN build files)"
mv "$DEPS_DIR/ada/"*.gyp "$DEPS_DIR/ada/"*.gn "$DEPS_DIR/ada/"*.gni "$DEPS_DIR/ada/README.md" "$WORKSPACE/"
rm -rf "$DEPS_DIR/ada"
mv "$WORKSPACE" "$DEPS_DIR/ada"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "ada" "$NEW_VERSION"
