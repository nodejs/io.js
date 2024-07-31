#!/usr/bin/env python3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import logging
import os
import re
import sys
import time
import datetime
import httplib2
import json
from io import StringIO
import urllib.parse

# Add depot tools to the sys path, for gerrit_util
sys.path.append(
    os.path.abspath(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            '../../third_party/depot_tools')))

import gerrit_util
import auth

from common_includes import VERSION_FILE

GERRIT_HOST = 'chromium-review.googlesource.com'

ROLLER_BOT_EMAIL = "chromium-autoroll@skia-public.iam.gserviceaccount.com"


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


class HttpError(Exception):
  """Exception class for errors commuicating with a http service."""

  def __init__(self, http_status, message, *args, **kwargs):
    super(HttpError, self).__init__(*args, **kwargs)
    self.http_status = http_status
    self.message = '(%d) %s' % (self.http_status, message)

  def __str__(self):
    return self.message


def HttpQuery(uri, *, timeout=300, **params):
  conn = httplib2.Http(timeout=timeout)
  response, contents = conn.request(uri=uri, **params)
  contents = contents.decode("utf-8", "replace")
  return StringIO(contents)


def HttpJSONQuery(*args, **params):
  headers = params.setdefault("headers", {})
  headers.setdefault("Accept", "application/json")

  if "body" in params:
    assert (params.get("method", "GET") != "GET")
    if not isinstance(params["body"], str):
      params["body"] = json.dumps(params["body"])
      headers.setdefault("Content-Type", "application/json")

  fh = HttpQuery(*args, **params)
  s = fh.readline()
  if s and s.rstrip() != ")]}'":
    raise HttpError(200, "Unexpected json output: %s" % s)
  s = fh.read()
  if not s:
    return None
  return json.loads(s)


def QueryTryBotsForFailures(change, patchset, timeout=300):
  builds = HttpJSONQuery(
      "https://cr-buildbucket.appspot.com/prpc/buildbucket.v2.Builds/SearchBuilds",
      method="POST",
      body={
          "fields":
              "builds.*.builder,builds.*.id,builds.*.status,builds.*.startTime,builds.*.endTime",
          "predicate": {
              "tags": [{
                  "key": "cq_experimental",
                  "value": "false"
              }],
              "status":
                  "FAILURE",
              "gerritChanges": [{
                  "project": "v8%2Fv8",
                  "host": "chromium-review.googlesource.com",
                  "patchset": patchset,
                  "change": change,
              },],
          },
      },
      timeout=timeout)
  return builds["builds"]


def main():
  parser = argparse.ArgumentParser(
      description="Bisect a roll CL using a CQ bot")
  # TODO(leszeks): Allow bisecting two arbitrary V8 CLs instead of a specific
  # roll.
  parser.add_argument(
      "roll",
      nargs='?',
      help="The roll CL (default: find the most recent active roll)",
      default=None)
  parser.add_argument(
      "bot",
      nargs='?',
      help="The bot to test, e.g. chromium/try/chromeos-amd64-generic-rel (default: find the most-often crashing, shortest running, existing failing bot on the given roll CL)",
      default=None)

  options = parser.parse_args()

  roll = options.roll
  if roll is None:
    print("Looking for latest roll CL...")
    changes = gerrit_util.QueryChanges(
        GERRIT_HOST, [("owner", ROLLER_BOT_EMAIL), ("project", "chromium/src"),
                      ("status", "NEW")],
        "Roll V8 from",
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
    roll = roll_change["_number"]

  roll_change = gerrit_util.GetChangeDetail(GERRIT_HOST, roll,
                                            ["CURRENT_REVISION"])
  subject = roll_change['subject']
  print("Found: %s" % subject)

  m = re.match(r"Roll V8 from ([0-9a-f]+) to ([0-9a-f]+)", subject)
  if not m:
    print("CL subject is not of the form \"Roll V8 from 123abc to 456def\"")
    return 1
  roll = roll_change["_number"]
  current_revision = roll_change["current_revision"]
  patchset = roll_change["revisions"][current_revision]["_number"]
  print("Bisecting https://%s/c/%s" % (GERRIT_HOST, roll))

  bot = options.bot
  if bot is None:
    bots = QueryTryBotsForFailures(roll, patchset)
    failing_bots = {}
    f = "%Y-%m-%dT%H:%M:%S.%fZ"
    for bot in bots:
      key = "/".join(
          bot["builder"][x] for x in ["project", "bucket", "builder"])
      print(bot["startTime"], bot["endTime"])
      startTime = datetime.datetime.fromisoformat(bot["startTime"])
      endTime = datetime.datetime.fromisoformat(bot["endTime"])
      duration = endTime - startTime
      if key in failing_bots:
        failing_bots[key] = (failing_bots[key][0] + 1,
                             min(failing_bots[key][1], duration))
      else:
        failing_bots[key] = (1, duration)

      print(
          "* Failure on %s (https://cr-buildbucket.appspot.com/build/%s) after %s"
          % (bot["builder"]["builder"], bot["id"], duration))

    if len(failing_bots) == 0:
      print("No failing bots found.")
      return 1

    # Sort descending by count, and ascending by duration:
    def sort_key(item):
      key, value = item
      count, duration = value
      return (-count, duration)

    failing_bots = sorted(failing_bots.items(), key=sort_key)

    print("Failing bots:")
    print("\n".join("* %s (%d, %s)" % (bot, count, duration)
                    for (bot, [count, duration]) in failing_bots))

    bot = failing_bots[0][0]

  print("Bisecting with bot %s" % bot)
  bot_project, bot_bucket, bot_builder = bot.split("/")

  diff = gerrit_util.CallGerritApi(
      GERRIT_HOST,
      'changes/%s/revisions/%s/files/DEPS/diff' % (roll, patchset),
      reqtype='GET')
  for content in diff["content"]:
    if "ab" in content:
      continue
    a = content["a"]
    b = content["b"]
    if len(a) == 1 and len(b) == 1:
      version_before = re.search("'v8_revision': '([0-9a-fA-F]+)'", a[0])
      version_after = re.search("'v8_revision': '([0-9a-fA-F]+)'", b[0])
      if version_before and version_after:
        version_before = version_before.group(1)
        version_after = version_after.group(1)
        break
    print("Found unexpected change:")
    print("\n".join("- " + line for line in a))
    print("\n".join("+ " + line for line in b))
    return 1
  else:
    print("Didn't find a change in DEPS")
    return 1

  print("V8 roll:")
  print("%s -> %s" % (version_before, version_after))

  revision_range_log = HttpJSONQuery(
      "https://chromium.googlesource.com/v8/v8/+log/%s..%s?format=JSON" %
      (version_before, version_after))["log"]

  suspect_shas = []
  for revision in revision_range_log:
    if revision["author"][
        "email"] != "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com":
      suspect_shas.append(revision["commit"])

  first_bad = 0
  last_good = len(suspect_shas)

  def IndexState(i):
    if i <= first_bad:
      return "Bad"
    elif i >= last_good:
      return "Good"
    else:
      return "?"

  def SuspectListString():
    return "\n".join("* %s (%s)" % (sha, IndexState(i))
                     for i, sha in enumerate(suspect_shas))

  if first_bad < last_good - 1:
    suspect_sha = None
    print("Creating bisect change...")
    bisect_change = gerrit_util.CreateChange(
        GERRIT_HOST,
        roll_change["project"],
        subject="[DO NOT SUBMIT] V8 roll bisect")
    print("https://%s/c/%s" % (GERRIT_HOST, bisect_change['_number']))

    try:
      DEPS_file_text = gerrit_util.GetFileContents(GERRIT_HOST,
                                                   bisect_change['_number'],
                                                   "DEPS").decode('utf-8')

      while first_bad < last_good - 1:
        print("Suspects:\n%s" % SuspectListString())

        # Index 0 is known bad, so search within the remaining indices.
        bisect_index = first_bad + (last_good - first_bad) // 2
        bisect_sha = suspect_shas[bisect_index]

        # Test sha
        print("Testing %s" % bisect_sha)

        print("Updating v8_revision in DEPS to %s..." % bisect_sha)
        DEPS_file_text = re.sub(r"'v8_revision': '[0-9a-fA-F]+'",
                                r"'v8_revision': '%s'" % bisect_sha,
                                DEPS_file_text)
        print("Editing DEPS file...")
        gerrit_util.ChangeEdit(GERRIT_HOST, bisect_change['_number'], "DEPS",
                               DEPS_file_text)
        print("Updating commit message...")
        commit_msg = "\n".join([
            "[DO NOT SUBMIT] V8 roll bisect",  #
            "",  #
            "For '%s': https://%s/c/%s" %
            (roll_change['subject'], GERRIT_HOST, roll_change['_number']),  #
            "",  #
            "Suspects:",  #
            SuspectListString(),
            "",  #
            "Change-Id: %s" % bisect_change['change_id'],  #
        ])
        gerrit_util.SetChangeEditMessage(GERRIT_HOST, bisect_change['_number'],
                                         commit_msg)
        print("Publishing change edit...")
        gerrit_util.PublishChangeEdit(GERRIT_HOST, bisect_change['_number'])

        bisect_change = gerrit_util.GetChangeDetail(GERRIT_HOST,
                                                    bisect_change['_number'],
                                                    ["CURRENT_REVISION"])
        bisect_patchset = bisect_change["revisions"][
            bisect_change["current_revision"]]["_number"]

        bot_test = HttpJSONQuery(
            'https://cr-buildbucket.appspot.com/prpc/buildbucket.v2.Builds/ScheduleBuild',
            method="POST",
            headers={
                'Authorization':
                    'Bearer %s' % auth.Authenticator().get_access_token().token
            },
            body={
                "builder": {
                    "project": bot_project,
                    "bucket": bot_bucket,
                    "builder": bot_builder
                },
                "gerritChanges": [{
                    "host": "chromium-review.googlesource.com",
                    "project": "chromium/src",
                    "change": bisect_change["_number"],
                    "patchset": bisect_patchset
                }],
                "tags": [{
                    "key": "builder",
                    "value": "chromeos-amd64-generic-rel"
                }, {
                    "key": "user_agent",
                    "value": "leszeks_testing"
                }]
            })

        test_id = bot_test["id"]

        waiting_start_time = time.time()
        while True:
          time.sleep(10)

          # Print the waiting time so far
          elapsed_time = time.time() - waiting_start_time
          print(
              "\r - waiting time: %s" %
              datetime.timedelta(seconds=round(elapsed_time)),
              end="",
              flush=True)

          test_result_json = HttpJSONQuery(
              'https://cr-buildbucket.appspot.com/prpc/buildbucket.v2.Builds/GetBuild',
              method="POST",
              body={"id": test_id})

          if test_result_json["status"] == "STARTED":
            continue
          elif test_result_json["status"] == "SCHEDULED":
            continue
          elif test_result_json["status"] == "SUCCESS":
            test_result = True
            break
          elif test_result_json["status"] == "FAILURE":
            test_result = False
            break
          else:
            print("Unexpected status: %s" % test_result_json["status"])
            return 1

        if test_result:
          print("%s is good" % bisect_sha)
          last_good = bisect_index
        else:
          print("%s is bad" % bisect_sha)
          first_bad = bisect_index

      suspect_sha = suspect_shas[first_bad]

      print("Updating v8_revision in DEPS to %s..." % suspect_shas[first_bad])
      DEPS_file_old = DEPS_file_text
      DEPS_file_text = re.sub(r"'v8_revision': '[0-9a-fA-F]+'",
                              r"'v8_revision': '%s'" % suspect_sha,
                              DEPS_file_text)
      if DEPS_file_old != DEPS_file_text:
        print("Editing DEPS file...")
        gerrit_util.ChangeEdit(GERRIT_HOST, bisect_change['_number'], "DEPS",
                               DEPS_file_text)
      else:
        print("DEPS file left unchanged...")
      print("Updating commit message...")
      commit_msg = "\n".join([
          "[DO NOT SUBMIT] V8 roll bisect (complete)",  #
          "",  #
          "For roll %s: https://%s/c/%s" %
          (roll_change['subject'], GERRIT_HOST, roll_change['_number']),  #
          "",  #
          "Suspects:",  #
          SuspectListString(),
          "",  #
          "Change-Id: %s" % bisect_change['change_id'],  #
      ])
      gerrit_util.SetChangeEditMessage(GERRIT_HOST, bisect_change['_number'],
                                       commit_msg)
      print("Publishing change edit...")
      gerrit_util.PublishChangeEdit(GERRIT_HOST, bisect_change['_number'])

      gerrit_util.AbandonChange(GERRIT_HOST, bisect_change['_number'],
                                "Complete, suspecting: %s" % suspect_sha)

    finally:
      if suspect_sha is None:
        gerrit_util.AbandonChange(GERRIT_HOST, bisect_change['_number'],
                                  "No suspect found")

  else:
    assert (first_bad == 0)
    suspect_sha = suspect_shas[0]

  print("Suspecting %s" % suspect_sha)

  print("Done.")


if __name__ == "__main__":  # pragma: no cover
  sys.exit(main())
