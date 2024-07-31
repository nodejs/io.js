#!/bin/sh
set -e
# Shell script to update icu in the source tree to a specific version

BASE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEPS_DIR="$BASE_DIR/deps"
TOOLS_DIR="$BASE_DIR/tools"

[ -z "$NODE" ] && NODE="$BASE_DIR/out/Release/node"
[ -x "$NODE" ] || NODE=$(command -v node)

# shellcheck disable=SC1091
. "$BASE_DIR/tools/dep_updaters/utils.sh"

NEW_VERSION="$("$NODE" --input-type=module <<'EOF'
const res = await fetch('https://api.github.com/repos/unicode-org/icu/releases/latest',
  process.env.GITHUB_TOKEN && {
    headers: {
      "Authorization": `Bearer ${process.env.GITHUB_TOKEN}`
    },
  });
if (!res.ok) throw new Error(`FetchError: ${res.status} ${res.statusText}`, { cause: res });
const { tag_name } = await res.json();
console.log(tag_name.replace('release-', '').replace('-','.'));
EOF
)"

ICU_VERSION_H="$DEPS_DIR/icu-small/source/common/unicode/uvernum.h"

CURRENT_VERSION="$(grep "#define U_ICU_VERSION " "$ICU_VERSION_H" | cut -d'"' -f2)"

# This function exit with 0 if new version and current version are the same
compare_dependency_version "icu-small" "$NEW_VERSION" "$CURRENT_VERSION"

DASHED_NEW_VERSION=$(echo "$NEW_VERSION" | sed 's/\./-/g')

LOW_DASHED_NEW_VERSION=$(echo "$NEW_VERSION" | sed 's/\./_/g')

NEW_VERSION_TGZ="icu4c-${LOW_DASHED_NEW_VERSION}-src.tgz"

NEW_VERSION_TGZ_URL="https://github.com/unicode-org/icu/releases/download/release-${DASHED_NEW_VERSION}/$NEW_VERSION_TGZ"

NEW_VERSION_MD5="https://github.com/unicode-org/icu/releases/download/release-${DASHED_NEW_VERSION}/icu4c-${LOW_DASHED_NEW_VERSION}-sources.md5"

CHECKSUM=$(curl -sL "$NEW_VERSION_MD5" | grep "$NEW_VERSION_TGZ" | grep -v "\.asc$" | awk '{print $1}')

GENERATED_CHECKSUM=$( curl -sL "$NEW_VERSION_TGZ_URL" | md5sum | cut -d ' ' -f1)

echo "Comparing checksums: deposited '$CHECKSUM' with '$GENERATED_CHECKSUM'"

if [ "$CHECKSUM" != "$GENERATED_CHECKSUM" ]; then
  echo "Skipped because checksums do not match."
  exit 0
fi

./configure --with-intl=full-icu --with-icu-source="$NEW_VERSION_TGZ_URL"

"$TOOLS_DIR/icu/shrink-icu-src.py"

rm -rf "$DEPS_DIR/icu"

perl -i -pe "s|\"url\": .*|\"url\": \"$NEW_VERSION_TGZ_URL\",|" "$TOOLS_DIR/icu/current_ver.dep"

perl -i -pe "s|\"md5\": .*|\"md5\": \"$CHECKSUM\"|" "$TOOLS_DIR/icu/current_ver.dep"

rm -rf out "$DEPS_DIR/icu" "$DEPS_DIR/icu4c*"

# Update the version number on maintaining-dependencies.md
# and print the new version as the last line of the script as we need
# to add it to $GITHUB_ENV variable
finalize_version_update "icu-small" "$NEW_VERSION" "tools/icu/current_ver.dep"
