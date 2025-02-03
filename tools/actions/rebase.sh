#!/bin/sh

set -xe

# shellcheck disable=SC2016
gh api graphql -F "prID=$(gh pr view "$1" --json id --jq .id || true)" -f query='
mutation RebasePR($prID: ID!) {
    updatePullRequestBranch(input:{pullRequestId:$prID,updateMethod:REBASE}) {
        clientMutationId
    }
}'
