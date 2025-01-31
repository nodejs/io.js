#!/bin/sh

set -xe

GITHUB_REPOSITORY=${GITHUB_REPOSITORY:-nodejs/node}
BOT_TOKEN=${BOT_TOKEN:-}

RELEASE_DATE=$1
RELEASE_LINE=$2
RELEASER=$3

if [ -z "$RELEASE_DATE" ] || [ -z "$RELEASE_LINE" ]; then
  echo "Usage: $0 <RELEASE_DATE> <RELEASE_LINE>"
  exit 1
fi

if [ -z "$GITHUB_REPOSITORY" ] || [ -z "$BOT_TOKEN" ]; then
  echo "Invalid value in env for GITHUB_REPOSITORY and BOT_TOKEN"
  exit 1
fi

if ! command -v node || ! command -v gh || ! command -v git || ! command -v awk; then
  echo "Missing required dependencies"
  exit 1
fi

git node release --prepare --skipBranchDiff --yes --releaseDate "$RELEASE_DATE"

HEAD_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
HEAD_SHA="$(git rev-parse HEAD^)"

TITLE="$(git log -1 --format=%s)"

# Use a temporary file for the PR body
TEMP_BODY="$(awk "/## ${RELEASE_DATE}/,/^<a id=/{ if (!/^<a id=/) print }" "doc/changelogs/CHANGELOG_V${RELEASE_LINE}.md")"

# Create the proposal branch
gh api \
  --method POST \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "/repos/${GITHUB_REPOSITORY}/git/refs" \
   -f "ref=refs/heads/$HEAD_BRANCH" -f "sha=$HEAD_SHA"

# Create the proposal PR
PR_URL="$(gh api \
  --method POST \
  --jq .html_url \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "/repos/${GITHUB_REPOSITORY}/pulls" \
   -f "title=$TITLE" -f "body=$TEMP_BODY" -f "head=$HEAD_BRANCH" -f "base=v$RELEASE_LINE.x" -f draft=true)"

# Push the release commit to the proposal branch using `BOT_TOKEN` from the env
node --input-type=module - \
    "$GITHUB_REPOSITORY" \
    "$HEAD_BRANCH" \
    "$HEAD_SHA" \
    "$TITLE" \
    "$(git log -1 HEAD --format=%b | awk -v PR_URL="$PR_URL" '{sub(/^PR-URL: TODO$/, "PR-URL: " PR_URL)} 1' || true)" \
    "$(git show HEAD --diff-filter=d --name-only --format= || true)" \
    "$(git show HEAD --diff-filter=D --name-only --format= || true)" \
<<'EOF'
const [,,
  repo,
  branch,
  parentCommitSha,
  commit_title,
  commit_body,
  modifiedOrAddedFiles,
  deletedFiles,
] = process.argv;

import { readFileSync } from 'node:fs';
import util from 'node:util';

const query = `
mutation ($repo: String! $branch: String!, $parentCommitSha: GitObjectID!, $changes: FileChanges!, $commit_title: String!, $commit_body: String) {
  createCommitOnBranch(input: {
    branch: {
      repositoryNameWithOwner: $repo,
      branchName: $branch
    },
    message: {
      headline: $commit_title,
      body: $commit_body
    },
    expectedHeadOid: $parentCommitSha,
    fileChanges: $changes
  }) {
    commit {
      url
    }
  }
}
`;
const response = await fetch('https://api.github.com/graphql', {
  method: 'POST',
  headers: {
    'Authorization': `bearer ${process.env.BOT_TOKEN}`,
  },
  body: JSON.stringify({
    query,
    variables: {
      repo,
      branch,
      parentCommitSha,
      commit_title,
      commit_body,
      changes: {
        additions: modifiedOrAddedFiles.split('\n').filter(Boolean)
          .map(path => ({ path, contents: readFileSync(path).toString('base64') })),
        deletions: deletedFiles.split('\n').filter(Boolean),
      }
    },
  })
});
if (!response.ok) {
  console.log({statusCode: response.status, statusText: response.statusText});
  process.exitCode ||= 1;
}
const data = await response.json();
if (data.errors?.length) {
  throw new Error('Endpoint returned an error', { cause: data });
}
console.log(util.inspect(data, { depth: Infinity }));
EOF

gh pr edit "$PR_URL" --add-label release --add-label "v$RELEASE_LINE.x" --add-assignee "$RELEASER"
