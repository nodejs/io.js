#!/bin/sh
set -e

# Shell script to update llhttp in the source tree to specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="${BASE_DIR}/deps"

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

if [ -n "$LOCAL_COPY" ]; then
  NEW_VERSION=$(node -e "console.log(JSON.parse(require('fs').readFileSync('$LOCAL_COPY/package.json', 'utf-8')).version)")
else
  NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
  const res = await fetch('https://api.github.com/repos/nodejs/llhttp/releases/latest',
    process.env.GITHUB_TOKEN && {
      headers: {
        "Authorization": `Bearer ${process.env.GITHUB_TOKEN}`
      },
    });
  if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
  const { tag_name } = await res.json();
  console.log(tag_name.replace('release/v', ''));
EOF
)"
fi

CURRENT_MAJOR_VERSION=$(grep "#define LLHTTP_VERSION_MAJOR" ./deps/llhttp/include/llhttp.h | sed -n "s/^.*MAJOR \(.*\)/\1/p")
CURRENT_MINOR_VERSION=$(grep "#define LLHTTP_VERSION_MINOR" ./deps/llhttp/include/llhttp.h | sed -n "s/^.*MINOR \(.*\)/\1/p")
CURRENT_PATCH_VERSION=$(grep "#define LLHTTP_VERSION_PATCH" ./deps/llhttp/include/llhttp.h | sed -n "s/^.*PATCH \(.*\)/\1/p")
CURRENT_VERSION="$CURRENT_MAJOR_VERSION.$CURRENT_MINOR_VERSION.$CURRENT_PATCH_VERSION"

# This function exit with 0 if new version and current version are the same
compare_dependency_version "llhttp" "$NEW_VERSION" "$CURRENT_VERSION"

cleanup () {
  EXIT_CODE=$?
  [ -d "$WORKSPACE" ] && rm -rf "$WORKSPACE"
  exit $EXIT_CODE
}

echo "Making temporary workspace ..."
WORKSPACE=$(mktemp -d 2> /dev/null || mktemp -d -t 'tmp')
trap cleanup INT TERM EXIT

cd "$WORKSPACE"

echo "Replacing existing llhttp (except GYP and GN build files)"
mv "$DEPS_DIR/llhttp/"*.gn "$DEPS_DIR/llhttp/"*.gni "$WORKSPACE/"

if [ -n "$LOCAL_COPY" ]; then
  echo "Copying llhttp release from $LOCAL_COPY ..."
  
  echo "Building llhttp ..."
  cd "$BASE_DIR"
  cd "$LOCAL_COPY"
  npm install
  RELEASE=$NEW_VERSION make release

  echo "Copying llhttp release ..."
  rm -rf "$DEPS_DIR/llhttp"
  cp -a release "$DEPS_DIR/llhttp"
elif echo "$NEW_VERSION" | grep -qs "/" ; then # Download a release
  REPO="git@github.com:$NEW_VERSION.git"
  BRANCH=$2
  [ -z "$BRANCH" ] && BRANCH=main

  echo "Cloning llhttp source archive $REPO ..."
  git clone "$REPO" llhttp
  cd llhttp
  echo "Checking out branch $BRANCH ..."
  git checkout "$BRANCH"

  echo "Building llhttp ..."
  npm install
  RELEASE=$NEW_VERSION make release

  echo "Copying llhttp release ..."
  rm -rf "$DEPS_DIR/llhttp"
  cp -a release "$DEPS_DIR/llhttp"
else
  echo "Download llhttp release $NEW_VERSION ..."
  LLHTTP_TARBALL="llhttp-v$NEW_VERSION.tar.gz"
  curl -sL -o "$LLHTTP_TARBALL" "https://github.com/nodejs/llhttp/archive/refs/tags/release/v$NEW_VERSION.tar.gz"
  gzip -dc "$LLHTTP_TARBALL" | tar xf -

  echo "Copying llhttp release ..."
  rm -rf "$DEPS_DIR/llhttp"
  cp -a "llhttp-release-v$NEW_VERSION" "$DEPS_DIR/llhttp"
fi

mv "$WORKSPACE/"*.gn "$WORKSPACE/"*.gni "$DEPS_DIR/llhttp"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "llhttp" "$NEW_VERSION"
