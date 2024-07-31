#!/usr/bin/env vpython3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import base64
import logging
import re
import sys
import time
import datetime
import urllib.parse

from pathlib import Path

# Add depot tools to the sys path, for gerrit_util
BASE_PATH = Path(__file__).resolve().parent.parent.parent
DEPOT_TOOLS_PATH = BASE_PATH / 'third_party' / 'depot_tools'
sys.path.append(str(DEPOT_TOOLS_PATH))

import gerrit_util

from common_includes import VERSION_FILE

GERRIT_HOST = 'chromium-review.googlesource.com'

ROLLER_BOT_EMAIL = "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com"

AUTO_ROLLER_URL = 'https://autoroll.skia.org/r/v8-chromium-autoroll'

VERSION_RE = re.compile(r"^\d+(?:\.\d+){2,3}$")


def ExtractVersion(include_file_text):
  version = {}
  for line in include_file_text.split('\n'):

    def ReadAndPersist(var_name, def_name):
      match = re.match(r"^#define %s\s+(\d*)" % def_name, line)
      if match:
        value = match.group(1)
        version[var_name] = int(value)

    for (var_name, def_name) in [("major", "V8_MAJOR_VERSION"),
                                 ("minor", "V8_MINOR_VERSION"),
                                 ("build", "V8_BUILD_NUMBER"),
                                 ("patch", "V8_PATCH_LEVEL")]:
      ReadAndPersist(var_name, def_name)
  return version


def repeat_until_true(fun):
  waiting_start_time = time.time()
  while True:
    # Print the waiting time so far
    elapsed_time = time.time() - waiting_start_time
    elapsed_time = datetime.timedelta(seconds=round(elapsed_time))
    print(f"\r - waiting time: {elapsed_time}", end="", flush=True)
    if fun():
      # New line after progress printing.
      print("")
      break

    time.sleep(5)


def normalize_version(version):
  """Returns an int tuple of the version with exactly 4 positions."""
  version = tuple(version.split('.'))
  assert 0 < len(version) <= 4
  version = tuple(int(c) for c in version)
  return (version + (0, ) * 3)[:4]


def main(sys_args=None):
  sys_args = sys_args or sys.argv[1:]
  parser = argparse.ArgumentParser(
      description="Use the gerrit API to cherry-pick a revision")
  parser.add_argument(
      "-a",
      "--author",
      default="",
      help="The author email used for code review.")
  parser.add_argument(
      "--branch",
      help="The branch to merge to (e.g. 10.3.171). Detected automatically from the latest roll CL if not provided",
      required=False)
  # TODO(leszeks): Add support for more than one revision. This will need to
  # cherry pick one of them using the gerrit API, then somehow applying the rest
  # onto it as additional patches.
  parser.add_argument("revision", nargs=1, help="The revision to merge.")

  options = parser.parse_args(sys_args)

  branch = options.branch
  if branch is None:
    print("Looking for latest roll CL...")
    changes = gerrit_util.QueryChanges(
        GERRIT_HOST, [
            ("owner", ROLLER_BOT_EMAIL),
            ("project", "chromium/src"),
            ("status", "NEW"),
        ],
        "Update V8 to version",
        limit=1)
    if len(changes) < 1:
      print("Didn't find a CL that looks like an active roll")
      return 1
    if len(changes) > 1:
      print("Found multiple CLs that look like an active roll:")
      for change in changes:
        print("  * %s: https://%s/c/%s" %
              (change['subject'], GERRIT_HOST, change['_number']))
      return 1

    roll_change = changes[0]
    subject = roll_change['subject']
    print("Found: %s" % subject)
    m = re.match(r"Update V8 to version ([0-9]+\.[0-9]+\.[0-9]+)", subject)
    if not m:
      print("CL subject is not of the form \"Update V8 to version 1.2.3\"")
      return 1
    branch = m.group(1)

  # Get the original commit.
  revision = options.revision[0]
  if not re.match(r"[0-9]+\.[0-9]+\.[0-9]+", branch):
    print("Branch is not of the form 1.2.3")
    return 1
  print("Cherry-picking %s onto %s" % (revision, branch))

  # Create a cherry pick commit from the original commit.
  cherry_pick = gerrit_util.CherryPick(GERRIT_HOST, revision, branch)
  # Use the cherry pick number to refer to it, rather than the 'id', because
  # cherry picks end up having the same Change-Id as the original CL.
  cherry_pick_id = cherry_pick['_number']
  print("Created cherry-pick: https://%s/c/%s" %
        (GERRIT_HOST, cherry_pick['_number']))

  print("Extracting target version...")
  # Get the version out of the cherry-picked commit's v8-version.h
  include_file_text = gerrit_util.GetFileContents(GERRIT_HOST, cherry_pick_id,
                                                  VERSION_FILE).decode('utf-8')
  version = ExtractVersion(include_file_text)
  print("... version is %s" % str(version))

  new_patch = version['patch'] + 1
  version_string = "%d.%d.%d.%d" % (version["major"], version["minor"],
                                    version["build"], new_patch)

  # Add the 'roll_merge' hashtag so that Rubberstamper knows that this is a
  # benign cherry pick.
  print("Setting 'roll-merge' hashtag...")
  gerrit_util.CallGerritApi(
      GERRIT_HOST,
      'changes/%s/hashtags' % cherry_pick_id,
      reqtype='POST',
      body={'add': ["roll-merge"]})

  # Increment the patch number in v8-version.h
  print("Updating %s to patch level %d..." % (VERSION_FILE, new_patch))
  include_file_text = re.sub(
      r"(?<=#define V8_PATCH_LEVEL)(?P<space>\s+)\d*$",
      r"\g<space>%d" % new_patch,
      include_file_text,
      flags=re.MULTILINE)
  gerrit_util.ChangeEdit(GERRIT_HOST, cherry_pick_id, VERSION_FILE,
                         include_file_text)

  # Create the commit message, using the new version and information about the
  # original commit.
  print("Updating commit message...")
  original_commit = gerrit_util.GetChangeCommit(GERRIT_HOST, revision)
  commit_msg = "\n".join([
      "Version %s (cherry-pick)" % version_string,  #
      "",  #
      "Merged %s" % original_commit['commit'],  #
      "",  #
      "%s" % original_commit['subject'],  #
      "",  #
      "Change-Id: %s" % cherry_pick['change_id'],  #
  ])
  gerrit_util.SetChangeEditMessage(GERRIT_HOST, cherry_pick_id, commit_msg)

  # Publish the change edit with the v8-version.h and commit message changes.
  print("Publishing changes...")
  gerrit_util.PublishChangeEdit(GERRIT_HOST, cherry_pick_id)

  # Set Owners-Override +1
  print("Setting 'Owners-Override +1'...")
  try:
    gerrit_util.SetReview(
        GERRIT_HOST, cherry_pick_id, labels={"Owners-Override": 1})
  except:
    logging.warning("Could not set Owners-Override +1")

  print("Adding Rubber Stamper as a reviewer...")
  gerrit_util.AddReviewers(
      GERRIT_HOST,
      cherry_pick_id,
      reviewers=['rubber-stamper@appspot.gserviceaccount.com'])

  print("Cherry-pick %s created successfully: https://%s/c/%s" %
        (version_string, GERRIT_HOST, cherry_pick['_number']))

  print("Waiting for Code-Review +1 or Bot-Commit +1...")
  while True:
    cherry_pick_review = gerrit_util.CallGerritApi(
        GERRIT_HOST,
        'changes/%s/revisions/current/review' % cherry_pick_id,
        reqtype='GET')
    if any(
        cr_label.get('value', None) == 1
        for cr_label in cherry_pick_review['labels']['Code-Review']['all']):
      break
    if any(
        cr_label.get('value', None) == 1
        for cr_label in cherry_pick_review['labels']['Bot-Commit']['all']):
      break
    time.sleep(5)

  print("Submitting...")
  cherry_pick = gerrit_util.SubmitChange(GERRIT_HOST, cherry_pick_id)
  assert cherry_pick['status'] == 'MERGED'

  cherry_pick_commit = gerrit_util.GetChangeCommit(GERRIT_HOST, cherry_pick_id,
                                                   'current')
  print("Found committed as %s..." % cherry_pick_commit['commit'])

  print("Setting %s tag..." % version_string)
  project = urllib.parse.quote_plus(cherry_pick["project"])
  gerrit_util.CreateGerritTag(GERRIT_HOST,
                              project,
                              version_string, cherry_pick_commit['commit'])

  def gerrit_project_get(url):
    return gerrit_util.CallGerritApi(
        GERRIT_HOST,
        f'projects/{project}/{url}',
        reqtype='GET',
        accept_statuses=[200, 404])

  def gerrit_file_content(commit, file_name):
    file_name = urllib.parse.quote_plus(file_name)
    url = f'projects/{project}/commits/{commit}/files/{file_name}/content'
    conn = gerrit_util.CreateHttpConn(GERRIT_HOST, url)
    fh = gerrit_util.ReadHttpResponse(conn, accept_statuses=[200, 404])
    return base64.b64decode(fh.read()).decode('utf-8')

  def is_safe_to_open_roller():
    """Return True if V8's infrastructure detected the patched roll and
    reset the roll ref.
    """
    ref_info = gerrit_project_get('branches/roll')
    assert ref_info['ref'] == 'refs/heads/roll'
    revision = ref_info['revision']

    roll_version_file = gerrit_file_content(revision, VERSION_FILE)
    roll_version = '{major}.{minor}.{build}.{patch}'.format(
        **ExtractVersion(roll_version_file))

    # The version V8's infra is currently trying to roll.
    roll_version = normalize_version(roll_version)

    # The new version we want to roll now after the patch.
    next_version = normalize_version(version_string)

    # A dummy current version, with patch-level - 1.
    current_version = next_version[:3] + (next_version[3] - 1,)

    # V8's infra resets the roll ref to the last version in Chrome.
    # We don't know exactly what it is here, but it must be older than
    # the current patched - 1 as we either patch a stuck roll (that
    # hasn't landed yet) or a reverted roll.
    return roll_version == next_version or roll_version < current_version

  print("Waiting until it's safe to reopen rolling, might take 2-3 minutes...")
  repeat_until_true(is_safe_to_open_roller)
  print(f"It's safe now to reopen the auto roller at {AUTO_ROLLER_URL}.")

  def is_pgo_tag_created():
    pgo_tag = gerrit_project_get(f'tags/{version_string}-pgo')
    if pgo_tag:
      assert pgo_tag['revision'] == cherry_pick_commit['commit'], (
          f"PGO tagged revision {pgo_tag['revision']} does not match tagged "
          f"cherry-pick {cherry_pick_commit['commit']}"
      )
      return True
    return False

  print("Waiting for PGO profile tag (%s-pgo), this can take 15-20 minutes..." %
        version_string)
  repeat_until_true(is_pgo_tag_created)
  print("Done.")


if __name__ == "__main__":  # pragma: no cover
  sys.exit(main())
