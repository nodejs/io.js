#!/bin/sh
set -e
# Shell script to update gyp-next in the source tree to specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/nodejs/gyp-next/releases/latest');
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const { tag_name } = await res.json();
console.log(tag_name.replace('v', ''));
EOF
)"

CURRENT_VERSION=$(grep -m 1 version ./tools/gyp/pyproject.toml | tr -s ' ' | tr -d '"' | tr -d "'" | cut -d' ' -f3)

# This function exit with 0 if new version and current version are the same
compare_dependency_version "gyp-next" "$NEW_VERSION" "$CURRENT_VERSION"

echo "Making temporary workspace"

WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

trap cleanup INT TERM EXIT

GYP_NEXT_TARBALL="$NEW_VERSION.tar.gz"

cd "$WORKSPACE"

curl -sL -o "$GYP_NEXT_TARBALL"  "https://github.com/nodejs/gyp-next/archive/refs/tags/v$NEW_VERSION.tar.gz"

log_and_verify_sha256sum "gyp-next" "$GYP_NEXT_TARBALL"

gzip -dc "$GYP_NEXT_TARBALL" | tar xf -

rm "$GYP_NEXT_TARBALL"

mv "gyp-next-$NEW_VERSION" gyp

rm -rf "$WORKSPACE/gyp/.github"

rm -rf "$BASE_DIR/tools/gyp"

mv "$WORKSPACE/gyp" "$BASE_DIR/tools/"

echo "All done!"
echo ""
echo "Please git add gyp-next and commit the new version:"
echo ""
echo "$ git add -A tools/gyp"
echo "$ git commit -m \"tools: update gyp-next to $NEW_VERSION\""
echo ""

# The last line of the script should always print the new version,
# as we need to add it to $GITHUB_ENV variable.
echo "NEW_VERSION=$NEW_VERSION"
