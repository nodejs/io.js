#!/usr/bin/env vpython3
# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import logging
import re
import sys

from pathlib import Path

# Add depot tools to the sys path, for gerrit_util
BASE_PATH = Path(__file__).resolve().parent.parent.parent
DEPOT_TOOLS_PATH = BASE_PATH / 'third_party' / 'depot_tools'
sys.path.append(str(DEPOT_TOOLS_PATH))

import gerrit_util

GERRIT_HOST = 'chromium-review.googlesource.com'

TAGS_TO_STRIP = [
    "Change-Id",
    "Reviewed-on",
    "Reviewed-by",
    "Commit-Queue",
    "Cr-Commit-Position",
    "Auto-Submit",
]


# TODO(leszeks): Upstream to gerrit_util
def CherryPickWithMessage(host,
                          change,
                          destination,
                          revision='current',
                          message=None):
  """Create a cherry-pick commit from the given change, onto the given
    destination.
    """
  path = 'changes/%s/revisions/%s/cherrypick' % (change, revision)
  body = {'destination': destination}
  if message is not None:
    body['message'] = message
  conn = gerrit_util.CreateHttpConn(host, path, reqtype='POST', body=body)
  return gerrit_util.ReadHttpJsonResponse(conn)


def main(sys_args=None):
  sys_args = sys_args or sys.argv[1:]
  parser = argparse.ArgumentParser(
      description="Use the gerrit API to cherry-pick a revision onto a branch")
  parser.add_argument(
      "-r",
      "--reviewer",
      help="The author email used for code review (defaults to reviewers of the"
      "original CL). Can specify multiple reviewers. Appends @chromium.org if "
      "not specified.",
      default=[],
      nargs="*")
  parser.add_argument(
      "-b",
      "--branch",
      help="The branch to merge to (e.g. 12.0).",
      required=True)
  # TODO(leszeks): Add support for more than one revision. This will need to
  # cherry pick one of them using the gerrit API, then somehow applying the rest
  # onto it as additional patches.
  parser.add_argument("revision", nargs=1, help="The revision to merge.")

  options = parser.parse_args(sys_args)

  revision = options.revision[0]

  branch = options.branch
  if not re.match(r"[0-9]+\.[0-9]+", branch):
    print("Branch is not of the form 1.2")
    return 1
  branch_rev = f'refs/branch-heads/{branch}'

  reviewers = []
  for reviewer in options.reviewer:
    if not re.match(r".+@.+\..+", reviewer):
      reviewer = f"{reviewer}@chromium.org"
    reviewers.append(reviewer)

  print(f"Cherry-picking {revision} onto {branch_rev}")

  # Create the commit message, using the new version and information about the
  # original commit.
  print("Creating commit message...")
  original_commit = gerrit_util.GetChangeCommit(GERRIT_HOST, revision)

  commit_msg = original_commit['message'].splitlines()
  if not reviewers:
    for line in commit_msg:
      m = re.match(r"^Reviewed-by:\s*(.+)$", line)
      if m:
        reviewers.append(m.group(1))
    print(f"Picking reviewers from original commit: {','.join(reviewers)}")

  commit_msg[0] = f'Merged: {commit_msg[0]}'
  commit_msg[1:] = [
      line for line in commit_msg[1:]
      if not re.match(r"^(" + "|".join(TAGS_TO_STRIP) + r")\s*:", line)
  ]
  commit_msg.append(f"(cherry picked from commit {original_commit['commit']})")

  print("New commit message:")
  for line in commit_msg:
    print("| " + line)

  # Create a cherry pick commit from the original commit.
  cherry_pick = CherryPickWithMessage(
      GERRIT_HOST, revision, branch_rev, message="\n".join(commit_msg))
  # Use the cherry pick number to refer to it, rather than the 'id', because
  # cherry picks end up having the same Change-Id as the original CL.
  cherry_pick_id = cherry_pick['_number']
  print(
      f"Created cherry-pick: https://{GERRIT_HOST}/c/{cherry_pick['_number']}")

  # Set Auto-Submit +1
  print("Setting 'Auto-Submit +1'...")
  try:
    gerrit_util.SetReview(
        GERRIT_HOST, cherry_pick_id, labels={"Auto-Submit": 1})
  except:
    logging.warning("Could not set Auto-Submit +1")

  print(f"Adding {','.join(reviewers)} as reviewer(s)...")
  gerrit_util.AddReviewers(GERRIT_HOST, cherry_pick_id, reviewers=reviewers)

  print("Done.")


if __name__ == "__main__":  # pragma: no cover
  sys.exit(main())
