#!/bin/sh

pkgid="$1"

if [ -z "$pkgid" ]; then
  echo "Usage: $0 <pkgid>"
  exit 1
fi

# shellcheck disable=SC2154
if [ -z "$NOTARIZATION_ID" ]; then
  echo "No NOTARIZATION_ID environment variable. Skipping notarization."
  exit 0
fi

if [ -z "$NOTARIZATION_PASSWORD" ]; then
  echo "No NOTARIZATION_PASSWORD environment variable. Skipping notarization."
  exit 0
fi

if [ -z "$NOTARIZATION_TEAM_ID" ]; then
  echo "No NOTARIZATION_TEAM_ID environment variable. Skipping notarization."
  exit 0
fi

echo "Notarization process is done with Notarytool."

if ! command -v xcrun notarytool > /dev/null
then
    echo "Notarytool is not present in the system. Notarization has failed."
    exit 1
fi

# heck each file type
for filetype in pkg tar.gz tar.xz; do
  for filename in node-"$pkgid"*."$filetype"; do
    # Check if the file exists
    if [ -f "$filename" ]; then
      echo "Found $filename. Submitting for notarization..."

      if [ $filetype = "pkg" ]; then
        if xcrun notarytool submit \
          --keychain-profile "NODE_RELEASE_PROFILE" \
          --wait \
          "$filename"
        then
          echo "Notarization $filename submitted successfully."
        else
          echo "Notarization $filename failed."
          exit 1
        fi

        if ! xcrun spctl --assess --type install --context context:primary-signature --ignore-cache --verbose=2 "$filename"; then
          echo "error: Signature will not be accepted by Gatekeeper!" 1>&2
          exit 1
        else
          echo "Verification was successful."
        fi

        xcrun stapler staple "$filename"
        echo "Stapler was successful."

      elif [ $filetype = "tar.gz" ] || [ $filetype = "tar.xz" ]; then
        echo "Converting tarball to zip for notarization..."

        mkdir -p "${filename%.*}"
        tar -xf "$filename" -C "${filename%.*}"
        zip -r "${filename%.*}.zip" "${filename%.*}"

        if xcrun notarytool submit \
          --keychain-profile "NODE_RELEASE_PROFILE" \
          --wait \
          "${filename%.*}.zip"
        then
          echo "Notarization ${filename%.*}.zip submitted successfully."
        else
          echo "Notarization ${filename%.*}.zip failed."
          exit 1
        fi

        echo "Converting zip back to tarball..."

        rm -rf "${filename%.*}"
        tar -czf "$filename" "${filename%.*}"
        rm "${filename%.*}.zip"
      fi
    fi
  done
done