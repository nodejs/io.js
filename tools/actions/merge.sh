#!/bin/sh

# Requires [gh](https://cli.github.com/), [jq](https://jqlang.github.io), git, and grep. Also awk if you pass a URL.

# This script can be used to "purple-merge" PRs that are supposed to land as a single commit, using the "Squash and Merge" feature of GitHub.
# To land a PR with this tool:
# 1. Run `git node land <pr-id-or-url> --fixupAll`
# 2. Copy the hash of the commit at the top of the PR branch.
# 3. Run `tools/actions/merge.sh <pr-id-or-url> <commit-hash>`.

set -xe

pr=$1
commit_head=$2
shift 2 || { echo "Expected two arguments"; exit 1; }

OWNER=nodejs
REPOSITORY=node

if expr "X$pr" : 'Xhttps://github.com/[^/]\{1,\}/[^/]\{1,\}/pull/[0-9]\{1,\}' >/dev/null; then
  OWNER="$(echo "$pr" | awk 'BEGIN { FS = "/" } ; { print $4 }')"
  REPOSITORY="$(echo "$pr" | awk 'BEGIN { FS = "/" } ; { print $5 }')"
  pr="$(echo "$pr" | awk 'BEGIN { FS = "/" } ; { print $7 }')"
elif ! expr "X$pr" : 'X[0-9]\{1,\}' >/dev/null; then
  echo "The first argument should be the PR ID or URL"
fi

git log -1 HEAD  --pretty='format:%B' | git interpret-trailers --parse --no-divider | \
  grep -q -x "^PR-URL: https://github.com/$OWNER/$REPOSITORY/pull/$pr$" || {
    echo "Invalid PR-URL trailer"
    exit 1
  }
git log -1 HEAD^ --pretty='format:%B' | git interpret-trailers --parse --no-divider | \
  grep -q -x "^PR-URL: https://github.com/$OWNER/$REPOSITORY/pull/$pr$" && {
    echo "Refuse to squash and merge a PR landing in more than one commit"
    exit 1
  }

commit_title=$(git log -1 --pretty='format:%s')
commit_body=$(git log -1 --pretty='format:%b')

jq -n \
    --arg title "${commit_title}" \
    --arg body "${commit_body}" \
    --arg head "${commit_head}" \
    '{merge_method:"squash",commit_title:$title,commit_message:$body,sha:$head}' > output.json
cat output.json
if ! gh api -X PUT "repos/${OWNER}/${REPOSITORY}/pulls/${pr}/merge" --input output.json > output; then
    cat output
    echo "Failed to merge $pr"
    rm output output.json
    exit 1
fi
cat output
if ! commits="$(jq -r 'if .merged then .sha else error("not merged") end' < output)"; then
    echo "Failed to merge $pr"
    rm output output.json
    exit 1
fi
rm output.json output

gh pr comment "$pr" --repo "$OWNER/$REPOSITORY" --body "Landed in $commits"
