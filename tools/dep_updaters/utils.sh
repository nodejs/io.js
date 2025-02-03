#!/bin/sh

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export ROOT

# This function compare new version with current version of a dependency and
# exit the script if the versions are the same
#
# $1 is the package name e.g. 'acorn', 'ada', 'base64' etc. See the file
# https://github.com/nodejs/node/blob/main/doc/contributing/maintaining/maintaining-dependencies.md
# for a complete list of package name
# $2 is the new version.
compare_dependency_version() {
  package_name="$1"
  new_version="$2"
  current_version="$3"
  echo "Comparing $new_version with $current_version"
  if [ "$new_version" = "$current_version" ]; then
    echo "Skipped because $package_name is on the latest version."
    exit 0
  fi
}

# This function inform to commit the new version of a maintained dependency
# and print the last line of the script "NEW_VERSION=$NEW_VERSION" as we need
# to add it to $GITHUB_ENV variable.
#
# $1 is the package name e.g. 'acorn', 'ada', 'base64' etc. See the file
# https://github.com/nodejs/node/blob/main/doc/contributing/maintaining/maintaining-dependencies.md
# for a complete list of package name
# $2 is the new version.
# $3 (optional) other files to be git added apart from the deps/package_name
finalize_version_update() {
  package_name="$1"
  new_version="$2"
  extra_files="$3"

  echo "All done!"
  echo ""
  echo "Please git add $package_name and commit the new version:"
  echo ""
  echo "$ git add -A deps/$package_name $extra_files"
  echo "$ git commit -m \"deps: update $package_name to $new_version\""
  echo ""

  # The last line of the script should always print the new version,
  # as we need to add it to $GITHUB_ENV variable.
  echo "NEW_VERSION=$new_version"
}

# This function logs the archive checksum and, if provided, compares it with
# the deposited checksum
#
# $1 is the package name e.g. 'acorn', 'ada', 'base64' etc. See the file
# https://github.com/nodejs/node/blob/main/doc/contributing/maintaining/maintaining-dependencies.md
# for a complete list of package name
# $2 is the downloaded archive
# $3 (optional) is the deposited sha256 checksum. When provided, it is checked
# against the checksum generated from the archive
log_and_verify_sha256sum() {
  package_name="$1"
  archive="$2"
  checksum="$3"
  bsd_formatted_checksum=$(shasum -a 256 --tag "$archive")
  if [ -z "$3" ]; then
    echo "$bsd_formatted_checksum"
  else
    archive_checksum=$(shasum -a 256 "$archive")
    if [ "$checksum" = "$archive_checksum" ]; then
      echo "Valid $package_name checksum"
      echo "$bsd_formatted_checksum"
    else
      echo "ERROR - Invalid $package_name checksum:"
      echo "deposited: $checksum"
      echo "generated: $archive_checksum"
      exit 1
    fi
  fi
}

# This function replaces the directory of a dependency with the new one.
replace_dir() {
  old_dir="$1"
  new_dir="$2"
  rm -rf "$old_dir"
  mv "$new_dir" "$old_dir"
}
