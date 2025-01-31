#!/bin/sh

# To promote and sign a release that has been prepared by the build jobs, use:
#  release.sh

# To _only_ sign an existing release, use:
#  release.sh -s vx.y.z

set -e

[ -z "$NODEJS_RELEASE_HOST" ] && NODEJS_RELEASE_HOST=direct.nodejs.org

webhost=$NODEJS_RELEASE_HOST
webuser=dist
promotablecmd=dist-promotable
promotecmd=dist-promote
signcmd=dist-sign
allPGPKeys=""
customsshkey="" # let ssh and scp use default key
readmePath="README.md"
signversion=""
cloudflare_bucket="r2:dist-prod"

while getopts ":i:r:s:a" option; do
    case "${option}" in
        a)
            # With -a, local keys are not filtered based on the one listed in the README
            # useful if you want to sign with a subkey.
            allPGPKeys="true"
            ;;
        i)
            customsshkey="-i ${OPTARG}"
            ;;
        r)
            readmePath="${OPTARG}"
            ;;
        s)
            signversion="${OPTARG}"
            ;;
        \?)
            echo "Invalid option -$OPTARG."
            exit 1
            ;;
        *)
            echo "Option -$OPTARG takes a parameter."
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))

################################################################################
## Select a GPG key to use

echo "# Selecting GPG key ..."


if [ -z "$allPGPKeys" ]; then
  gpgkey="$(awk '{
    if ($1 == "gpg" && $2 == "--keyserver" && $4 == "--recv-keys" && (1 == 2'"$(
      gpg --list-secret-keys | awk -F' = ' '/^ +Key fingerprint/{ gsub(/ /,"",$2); print " || $5 == \"" $2 "\"" }' || true
    )"')) { print substr($5, 33) }
  }' "$readmePath")"
else
  gpgkey=$(gpg --list-secret-keys --keyid-format SHORT | awk -F'( +|/)' '/^(sec|ssb)/{print $3}')
fi
keycount=$(echo "$gpgkey" | wc -w)

if [ "$keycount" -eq 0 ]; then
  # shellcheck disable=SC2016
  echo 'Need at least one GPG key, please make one with `gpg --gen-key`'
  echo 'You will also need to submit your key to a public keyserver, e.g.'
  echo '  https://sks-keyservers.net/i/#submit'
  exit 1
elif [ "$keycount" -ne 1 ]; then
  printf "You have multiple GPG keys:\n\n"

  gpg --list-secret-keys

  keynum=
  while [ -z "${keynum##*[!0-9]*}" ] || [ "$keynum" -le 0 ] || [ "$keynum" -gt "$keycount" ]; do
    echo "$gpgkey" | awk '{ print NR ") " $0; }'
    printf 'Select a key: '
    read -r keynum
  done
  echo ""
  gpgkey=$(echo "$gpgkey" | sed -n "${keynum}p")
fi

gpgfing=$(gpg --keyid-format 0xLONG --fingerprint "$gpgkey" | awk -F' = ' '/^ +Key fingerprint/{gsub(/ /,"",$2);print $2}')

grep -q "$gpgfing" "$readmePath" || {
  echo "Error: this GPG key fingerprint is not listed in $readmePath"
  exit 1
}

echo "Using GPG key: $gpgkey"
echo "  Fingerprint: $gpgfing"

checktag() {
  # local version=$1

  if ! git tag -v "$1" 2>&1 | grep "${gpgkey}" | grep key > /dev/null; then
    echo "Could not find signed tag for \"$1\" or GPG key is not yours"
    exit 1
  fi
}

################################################################################
## Create and sign checksums file for a given version

sign() {
  printf "\n# Creating SHASUMS256.txt ...\n"

  # local version=$1

  ghtaggedversion=$(curl -sL "https://raw.githubusercontent.com/nodejs/node/$1/src/node_version.h" \
      | awk '/define NODE_(MAJOR|MINOR|PATCH)_VERSION/{ v = v "." $3 } END{ v = "v" substr(v, 2); print v }')
  if [ "$1" != "${ghtaggedversion}" ]; then
    echo "Could not find tagged version on github.com/nodejs/node, did you push your tag?"
    exit 1
  fi

  # /home/dist/${site}/release
  # shellcheck disable=SC2086,SC2029
  shapath=$(ssh ${customsshkey} "${webuser}@${webhost}" $signcmd nodejs $1)

  echo "${shapath}" | grep -q '^/.*/SHASUMS256.txt$' || (\
    echo 'Error: No SHASUMS file returned by sign!' &&\
    exit 1)

  echo ""
  echo "# Signing SHASUMS for $1..."

  shafile=$(basename "$shapath")
  shadir=$(dirname "$shapath")
  tmpdir="/tmp/_node_release.$$"

  mkdir -p $tmpdir

  # shellcheck disable=SC2086
  scp ${customsshkey} "${webuser}@${webhost}:${shapath}" "${tmpdir}/${shafile}"

  gpg --default-key "$gpgkey" --clearsign --digest-algo SHA256 "${tmpdir}/${shafile}"
  gpg --default-key "$gpgkey" --detach-sign --digest-algo SHA256 "${tmpdir}/${shafile}"

  echo "Wrote to ${tmpdir}/"

  echo "Your signed ${shafile}.asc:"
  echo ""

  cat "${tmpdir}/${shafile}.asc"

  echo ""

  while true; do
    printf "Upload files to %s and Cloudflare R2? [y/n] " "$webhost"
    yorn=""
    read -r yorn

    if [ "X${yorn}" = "Xn" ]; then
      break
    fi

    if [ "X${yorn}" = "Xy" ]; then
      # Copy SHASUMS256.txt and its signatures to the web host:
      # shellcheck disable=SC2086
      scp ${customsshkey} "${tmpdir}/${shafile}" "${tmpdir}/${shafile}.asc" "${tmpdir}/${shafile}.sig" "${webuser}@${webhost}:${shadir}/"
      # shellcheck disable=SC2086,SC2029
      ssh ${customsshkey} "${webuser}@${webhost}" chmod 644 "${shadir}/${shafile}.asc" "${shadir}/${shafile}.sig"

      # Copy the signatures to Cloudflare R2:
      # Note: the binaries and SHASUMS256.txt should already be in the bucket
      # since the promotion script should take care of uploading them.

      # Remove /home/dist/ part
      r2dir=$(echo "$shadir" | cut -c 12-)

      # Copy SHASUMS256.txt.asc
      # shellcheck disable=SC2086,SC2029
      ssh ${customsshkey} "${webuser}@${webhost}" rclone copyto "${shadir}/${shafile}.asc" "${cloudflare_bucket}/${r2dir}/${shafile}.asc"

      # Copy SHASUMS256.txt.sig
      # shellcheck disable=SC2086,SC2029
      ssh ${customsshkey} "${webuser}@${webhost}" rclone copyto "${shadir}/${shafile}.sig" "${cloudflare_bucket}/${r2dir}/${shafile}.sig"
      break
    fi
  done

  rm -rf $tmpdir
}


if [ -n "${signversion}" ]; then
		checktag "$signversion"
    sign "$signversion"
    exit 0
fi

# else: do a normal release & promote

################################################################################
## Look for releases to promote

printf "\n# Checking for releases ...\n"

# shellcheck disable=SC2086,SC2029
promotable=$(ssh ${customsshkey} "$webuser@$webhost" $promotablecmd nodejs)

if [ "X${promotable}" = "X" ]; then
  echo "No releases to promote!"
  exit 0
fi

echo "Found the following releases / builds ready to promote:"
echo ""
echo "$promotable" | sed 's/^/ * /'
echo ""

versions=$(echo "$promotable" | cut -d: -f1)

################################################################################
## Promote releases

for version in $versions; do
  while true; do
    files=$(echo "$promotable" | grep "^${version}" | sed 's/^'"${version}"': //')
    printf "Promote %s files (%s)? [y/n] " "${version}" "${files}"
    yorn=""
    read -r yorn

    if [ "X${yorn}" = "Xn" ]; then
      break
    fi

    if [ "X${yorn}" != "Xy" ]; then
      continue
    fi

		checktag "$version"

    echo ""
    echo "# Promoting ${version}..."

    # shellcheck disable=SC2086,SC2029
    ssh ${customsshkey} "$webuser@$webhost" $promotecmd nodejs $version && \
      sign "$version"

    break
  done
done
