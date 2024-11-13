#!/bin/sh

set -xe

OWNER=$1
REPOSITORY=$2
shift 2

UPSTREAM=origin
DEFAULT_BRANCH=main

COMMIT_QUEUE_LABEL="commit-queue"
COMMIT_QUEUE_FAILED_LABEL="commit-queue-failed"

commit_queue_failed() {
  pr=$1

  gh pr edit "$pr" --add-label "${COMMIT_QUEUE_FAILED_LABEL}"

  # shellcheck disable=SC2154
  cqurl="${GITHUB_SERVER_URL}/${OWNER}/${REPOSITORY}/actions/runs/${GITHUB_RUN_ID}"
  body="<details><summary>Commit Queue failed</summary><pre>$(sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' output)</pre><a href='$cqurl'>$cqurl</a></details>"
  echo "$body"

  gh pr comment "$pr" --body "$body"

  rm output
}

# TODO(mmarchini): should this be set with whoever added the label for each PR?
git config --local user.email "github-bot@iojs.org"
git config --local user.name "Node.js GitHub Bot"

for pr in "$@"; do
  gh pr view "$pr" --json labels --jq ".labels" > labels.json
  # Skip PR if CI was requested
  if jq -e 'map(.name) | index("request-ci")' < labels.json; then
    echo "pr ${pr} skipped, waiting for CI to start"
    continue
  fi

  # Skip PR if CI is still running
  if gh pr checks "$pr" | grep -q "\spending\s"; then
    echo "pr ${pr} skipped, CI still running"
    continue
  fi

  # Delete the commit queue label
  gh pr edit "$pr" --remove-label "$COMMIT_QUEUE_LABEL"

  if jq -e 'map(.name) | index("commit-queue-squash")' < labels.json; then
    MULTIPLE_COMMIT_POLICY="--fixupAll"
  elif jq -e 'map(.name) | index("commit-queue-rebase")' < labels.json; then
    MULTIPLE_COMMIT_POLICY=""
  else
    MULTIPLE_COMMIT_POLICY="--oneCommitMax"
  fi

  git node land --autorebase --yes $MULTIPLE_COMMIT_POLICY "$pr" >output 2>&1 || echo "Failed to land #${pr}"
  # cat here otherwise we'll be suppressing the output of git node land
  cat output

  # TODO(mmarchini): workaround for ncu not returning the expected status code,
  # if the "Landed in..." message was not on the output we assume land failed
  if ! grep -q '. Post "Landed in .*/pull/'"${pr}" output; then
    commit_queue_failed "$pr"
    # If `git node land --abort` fails, we're in unknown state. Better to stop
    # the script here, current PR was removed from the queue so it shouldn't
    # interfere again in the future.
    git node land --abort --yes
    continue
  fi

  if [ -z "$MULTIPLE_COMMIT_POLICY" ]; then
    start_sha=$(git rev-parse $UPSTREAM/$DEFAULT_BRANCH)
    end_sha=$(git rev-parse HEAD)
    commits="${start_sha}...${end_sha}"

    if ! git push $UPSTREAM $DEFAULT_BRANCH >> output 2>&1; then
      commit_queue_failed "$pr"
      continue
    fi
  else
    # If there's only one commit, we can use the Squash and Merge feature from GitHub.
    # TODO: use `gh pr merge` when the GitHub CLI allows to customize the commit title (https://github.com/cli/cli/issues/1023).
    commit_title=$(git log -1 --pretty='format:%s')
    commit_body=$(git log -1 --pretty='format:%b')
    commit_head=$(grep 'Fetched commits as' output | cut -d. -f3 | xargs git rev-parse)

    jq -n \
      --arg title "${commit_title}" \
      --arg body "${commit_body}" \
      --arg head "${commit_head}" \
      '{merge_method:"squash",commit_title:$title,commit_message:$body,sha:$head}' > output.json
    cat output.json
    if ! gh api -X PUT "repos/${OWNER}/${REPOSITORY}/pulls/${pr}/merge" --input output.json > output; then
      commit_queue_failed "$pr"
      continue
    fi
    cat output
    if ! commits="$(jq -r 'if .merged then .sha else error("not merged") end' < output)"; then
      commit_queue_failed "$pr"
      continue
    fi
    rm output.json
  fi

  rm output

  gh pr comment "$pr" --body "Landed in $commits"

  [ -z "$MULTIPLE_COMMIT_POLICY" ] && gh pr close "$pr"
done

rm -f labels.json
