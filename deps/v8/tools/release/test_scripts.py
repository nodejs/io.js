#!/usr/bin/env python3
# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import os
import shutil
import tempfile
import traceback
import unittest

import auto_push
from auto_push import LastReleaseBailout
import common_includes
from common_includes import *
import create_release
from create_release import *
import merge_to_branch
from merge_to_branch import MergeToBranch
import roll_merge
from roll_merge import RollMerge

TEST_CONFIG = {
  "DEFAULT_CWD": None,
  "BRANCHNAME": "test-prepare-push",
  "PERSISTFILE_BASENAME": "/tmp/test-create-releases-tempfile",
  "PATCH_FILE": "/tmp/test-v8-create-releases-tempfile-tempfile-patch",
  "COMMITMSG_FILE": "/tmp/test-v8-create-releases-tempfile-commitmsg",
  "CHROMIUM": "/tmp/test-create-releases-tempfile-chromium",
  "SETTINGS_LOCATION": None,
  "ALREADY_MERGING_SENTINEL_FILE":
      "/tmp/test-merge-to-branch-tempfile-already-merging",
  "TEMPORARY_PATCH_FILE": "/tmp/test-merge-to-branch-tempfile-temporary-patch",
}


AUTO_PUSH_ARGS = [
  "-a", "author@chromium.org",
  "-r", "reviewer@chromium.org",
]


class ToplevelTest(unittest.TestCase):
  def testSaniniziteVersionTags(self):
    self.assertEquals("4.8.230", SanitizeVersionTag("4.8.230"))
    self.assertEquals("4.8.230", SanitizeVersionTag("tags/4.8.230"))
    self.assertEquals(None, SanitizeVersionTag("candidate"))

  def testNormalizeVersionTags(self):
    input = ["4.8.230",
              "tags/4.8.230",
              "tags/4.8.224.1",
              "4.8.224.1",
              "4.8.223.1",
              "tags/4.8.223",
              "tags/4.8.231",
              "candidates"]
    expected = ["4.8.230",
                "4.8.230",
                "4.8.224.1",
                "4.8.224.1",
                "4.8.223.1",
                "4.8.223",
                "4.8.231",
                ]
    self.assertEquals(expected, NormalizeVersionTags(input))

  def testCommand(self):
    """Ensure json can decode the output of commands."""
    json.dumps(Command('ls', pipe=True))


def Cmd(*args, **kwargs):
  """Convenience function returning a shell command test expectation."""
  return {
    "name": "command",
    "args": args,
    "ret": args[-1],
    "cb": kwargs.get("cb"),
    "cwd": kwargs.get("cwd", TEST_CONFIG["DEFAULT_CWD"]),
  }


def RL(text, cb=None):
  """Convenience function returning a readline test expectation."""
  return {
    "name": "readline",
    "args": [],
    "ret": text,
    "cb": cb,
    "cwd": None,
  }


def URL(*args, **kwargs):
  """Convenience function returning a readurl test expectation."""
  return {
    "name": "readurl",
    "args": args[:-1],
    "ret": args[-1],
    "cb": kwargs.get("cb"),
    "cwd": None,
  }


class SimpleMock(object):
  def __init__(self):
    self._recipe = []
    self._index = -1

  def Expect(self, recipe):
    self._recipe = recipe

  def Call(self, name, *args, **kwargs):  # pragma: no cover
    self._index += 1

    try:
      expected_call = self._recipe[self._index]
    except IndexError:
      raise NoRetryException("Calling %s %s" % (name, " ".join(args)))

    if not isinstance(expected_call, dict):
      raise NoRetryException("Found wrong expectation type for %s %s" %
                             (name, " ".join(args)))

    if expected_call["name"] != name:
      raise NoRetryException("Expected action: %s %s - Actual: %s" %
          (expected_call["name"], expected_call["args"], name))

    # Check if the given working directory matches the expected one.
    if expected_call["cwd"] != kwargs.get("cwd"):
      raise NoRetryException("Expected cwd: %s in %s %s - Actual: %s" %
          (expected_call["cwd"],
           expected_call["name"],
           expected_call["args"],
           kwargs.get("cwd")))

    # The number of arguments in the expectation must match the actual
    # arguments.
    if len(args) > len(expected_call['args']):
      raise NoRetryException("When calling %s with arguments, the "
          "expectations must consist of at least as many arguments." %
          name)

    # Compare expected and actual arguments.
    for (expected_arg, actual_arg) in zip(expected_call['args'], args):
      if expected_arg != actual_arg:
        raise NoRetryException("Expected: %s - Actual: %s" %
                               (expected_arg, actual_arg))

    # The expected call contains an optional callback for checking the context
    # at the time of the call.
    if expected_call['cb']:
      try:
        expected_call['cb']()
      except:
        tb = traceback.format_exc()
        raise NoRetryException("Caught exception from callback: %s" % tb)

    # If the return value is an exception, raise it instead of returning.
    if isinstance(expected_call['ret'], Exception):
      raise expected_call['ret']
    return expected_call['ret']

  def AssertFinished(self):  # pragma: no cover
    if self._index < len(self._recipe) -1:
      raise NoRetryException("Called mock too seldom: %d vs. %d" %
                             (self._index, len(self._recipe)))


class ScriptTest(unittest.TestCase):
  def MakeEmptyTempFile(self):
    handle, name = tempfile.mkstemp()
    os.close(handle)
    self._tmp_files.append(name)
    return name

  def MakeEmptyTempDirectory(self):
    name = tempfile.mkdtemp()
    self._tmp_files.append(name)
    return name


  def WriteFakeVersionFile(self, major=3, minor=22, build=4, patch=0):
    version_file = os.path.join(TEST_CONFIG["DEFAULT_CWD"], VERSION_FILE)
    if not os.path.exists(os.path.dirname(version_file)):
      os.makedirs(os.path.dirname(version_file))
    with open(version_file, "w") as f:
      f.write("  // Some line...\n")
      f.write("\n")
      f.write("#define V8_MAJOR_VERSION    %s\n" % major)
      f.write("#define V8_MINOR_VERSION    %s\n" % minor)
      f.write("#define V8_BUILD_NUMBER     %s\n" % build)
      f.write("#define V8_PATCH_LEVEL      %s\n" % patch)
      f.write("  // Some line...\n")
      f.write("#define V8_IS_CANDIDATE_VERSION 0\n")

  def WriteFakeWatchlistsFile(self):
    watchlists_file = os.path.join(TEST_CONFIG["DEFAULT_CWD"], WATCHLISTS_FILE)
    if not os.path.exists(os.path.dirname(watchlists_file)):
      os.makedirs(os.path.dirname(watchlists_file))
    with open(watchlists_file, "w") as f:

      content = """
    'merges': [
      # Only enabled on branches created with tools/release/create_release.py
      # 'v8-merges@googlegroups.com',
    ],
"""
      f.write(content)

  def MakeStep(self):
    """Convenience wrapper."""
    options = ScriptsBase(TEST_CONFIG, self, self._state).MakeOptions([])
    return MakeStep(step_class=Step, state=self._state,
                    config=TEST_CONFIG, side_effect_handler=self,
                    options=options)

  def RunStep(self, script=CreateRelease, step_class=Step, args=None):
    """Convenience wrapper."""
    args = args if args is not None else ["-m", "-a=author", "-r=reviewer", ]
    return script(TEST_CONFIG, self, self._state).RunSteps([step_class], args)

  def Call(self, fun, *args, **kwargs):
    print("Calling %s with %s and %s" % (str(fun), str(args), str(kwargs)))

  def Command(self, cmd, args="", prefix="", pipe=True, cwd=None):
    print("%s %s" % (cmd, args))
    print("in %s" % cwd)
    return self._mock.Call("command", cmd + " " + args, cwd=cwd)

  def ReadLine(self):
    return self._mock.Call("readline")

  def ReadURL(self, url, params):
    if params is not None:
      return self._mock.Call("readurl", url, params)
    else:
      return self._mock.Call("readurl", url)

  def Sleep(self, seconds):
    pass

  def GetUTCStamp(self):
    return "1000000"

  def Expect(self, *args):
    """Convenience wrapper."""
    self._mock.Expect(*args)

  def setUp(self):
    self._mock = SimpleMock()
    self._tmp_files = []
    self._state = {}
    TEST_CONFIG["DEFAULT_CWD"] = self.MakeEmptyTempDirectory()

  def tearDown(self):
    if os.path.exists(TEST_CONFIG["PERSISTFILE_BASENAME"]):
      shutil.rmtree(TEST_CONFIG["PERSISTFILE_BASENAME"])

    # Clean up temps. Doesn't work automatically.
    for name in self._tmp_files:
      if os.path.isfile(name):
        os.remove(name)
      if os.path.isdir(name):
        shutil.rmtree(name)

    self._mock.AssertFinished()

  def testGitMock(self):
    self.Expect([Cmd("git --version", "git version 1.2.3"),
                 Cmd("git dummy", "")])
    self.assertEquals("git version 1.2.3", self.MakeStep().Git("--version"))
    self.assertEquals("", self.MakeStep().Git("dummy"))

  def testCommonPrepareDefault(self):
    self.Expect([
      Cmd("git status -s -uno", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git fetch", ""),
      Cmd("git branch", "  branch1\n* %s" % TEST_CONFIG["BRANCHNAME"]),
      RL("Y"),
      Cmd("git branch -D %s" % TEST_CONFIG["BRANCHNAME"], ""),
    ])
    self.MakeStep().CommonPrepare()
    self.MakeStep().PrepareBranch()

  def testCommonPrepareNoConfirm(self):
    self.Expect([
      Cmd("git status -s -uno", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git fetch", ""),
      Cmd("git branch", "  branch1\n* %s" % TEST_CONFIG["BRANCHNAME"]),
      RL("n"),
    ])
    self.MakeStep().CommonPrepare()
    self.assertRaises(Exception, self.MakeStep().PrepareBranch)

  def testCommonPrepareDeleteBranchFailure(self):
    self.Expect([
      Cmd("git status -s -uno", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git fetch", ""),
      Cmd("git branch", "  branch1\n* %s" % TEST_CONFIG["BRANCHNAME"]),
      RL("Y"),
      Cmd("git branch -D %s" % TEST_CONFIG["BRANCHNAME"], None),
    ])
    self.MakeStep().CommonPrepare()
    self.assertRaises(Exception, self.MakeStep().PrepareBranch)

  def testInitialEnvironmentChecks(self):
    TextToFile("", os.path.join(TEST_CONFIG["DEFAULT_CWD"], ".git"))
    os.environ["EDITOR"] = "vi"
    self.Expect([
      Cmd("which vi", "/usr/bin/vi"),
    ])
    self.MakeStep().InitialEnvironmentChecks(TEST_CONFIG["DEFAULT_CWD"])

  def testTagTimeout(self):
    self.Expect([
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\"Title\" origin/tag_name", ""),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\"Title\" origin/tag_name", ""),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\"Title\" origin/tag_name", ""),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\"Title\" origin/tag_name", ""),
    ])
    args = ["--branch", "candidates", "ab12345"]
    self._state["version"] = "tag_name"
    self._state["commit_title"] = "Title"
    self.assertRaises(Exception,
        lambda: self.RunStep(RollMerge, TagRevision, args))

  def testReadAndPersistVersion(self):
    self.WriteFakeVersionFile(build=5)
    step = self.MakeStep()
    step.ReadAndPersistVersion()
    self.assertEquals("3", step["major"])
    self.assertEquals("22", step["minor"])
    self.assertEquals("5", step["build"])
    self.assertEquals("0", step["patch"])

  def testRegex(self):
    self.assertEqual("(issue 321)",
                     re.sub(r"BUG=v8:(.*)$", r"(issue \1)", "BUG=v8:321"))
    self.assertEqual("(Chromium issue 321)",
                     re.sub(r"BUG=(.*)$", r"(Chromium issue \1)", "BUG=321"))

    cl = "  too little\n\ttab\ttab\n         too much\n        trailing  "
    cl = MSub(r"\t", r"        ", cl)
    cl = MSub(r"^ {1,7}([^ ])", r"        \1", cl)
    cl = MSub(r"^ {9,80}([^ ])", r"        \1", cl)
    cl = MSub(r" +$", r"", cl)
    self.assertEqual("        too little\n"
                     "        tab        tab\n"
                     "        too much\n"
                     "        trailing", cl)

    self.assertEqual("//\n#define V8_BUILD_NUMBER  3\n",
                     MSub(r"(?<=#define V8_BUILD_NUMBER)(?P<space>\s+)\d*$",
                          r"\g<space>3",
                          "//\n#define V8_BUILD_NUMBER  321\n"))

  TAGS = """
4425.0
0.0.0.0
3.9.6
3.22.4
test_tag
"""

  # Version as tag: 3.22.4.0. Version on main: 3.22.6.
  # Make sure that the latest version is 3.22.6.0.
  def testIncrementVersion(self):
    self.Expect([
      Cmd("git fetch origin +refs/tags/*:refs/tags/*", ""),
      Cmd("git tag", self.TAGS),
      Cmd("git checkout -f origin/main -- include/v8-version.h",
          "", cb=lambda: self.WriteFakeVersionFile(3, 22, 6)),
    ])

    self.RunStep(CreateRelease, IncrementVersion)

    self.assertEquals("3", self._state["new_major"])
    self.assertEquals("22", self._state["new_minor"])
    self.assertEquals("7", self._state["new_build"])
    self.assertEquals("0", self._state["new_patch"])

  def testBootstrapper(self):
    work_dir = self.MakeEmptyTempDirectory()
    class FakeScript(ScriptsBase):
      def _Steps(self):
        return []

    # Use the test configuration without the fake testing default work dir.
    fake_config = dict(TEST_CONFIG)
    del(fake_config["DEFAULT_CWD"])

    self.Expect([
      Cmd("fetch v8", "", cwd=work_dir),
    ])
    FakeScript(fake_config, self).Run(["--work-dir", work_dir])

  def testCreateRelease(self):
    TextToFile("", os.path.join(TEST_CONFIG["DEFAULT_CWD"], ".git"))

    # The version file on main has build level 5.
    self.WriteFakeVersionFile(build=5)

    commit_msg = """Version 3.22.5"""

    def CheckVersionCommit():
      commit = FileToText(TEST_CONFIG["COMMITMSG_FILE"])
      self.assertEquals(commit_msg, commit)
      version = FileToText(
          os.path.join(TEST_CONFIG["DEFAULT_CWD"], VERSION_FILE))
      self.assertTrue(re.search(r"#define V8_MINOR_VERSION\s+22", version))
      self.assertTrue(re.search(r"#define V8_BUILD_NUMBER\s+5", version))
      self.assertFalse(re.search(r"#define V8_BUILD_NUMBER\s+6", version))
      self.assertTrue(re.search(r"#define V8_PATCH_LEVEL\s+0", version))
      self.assertTrue(
          re.search(r"#define V8_IS_CANDIDATE_VERSION\s+0", version))

    expectations = [
      Cmd("git fetch origin +refs/heads/*:refs/heads/*", ""),
      Cmd("git checkout -f origin/main", "", cb=self.WriteFakeWatchlistsFile),
      Cmd("git branch", ""),
      Cmd("git fetch origin +refs/tags/*:refs/tags/*", ""),
      Cmd("git tag", self.TAGS),
      Cmd("git checkout -f origin/main -- include/v8-version.h",
          "", cb=self.WriteFakeVersionFile),
      Cmd("git log -1 --format=%H 3.22.4", "release_hash\n"),
      Cmd("git log -1 --format=%s release_hash", "Version 3.22.4\n"),
      Cmd("git log -1 --format=%H release_hash^", "abc3\n"),
      Cmd("git log --format=%H abc3..push_hash", "rev1\n"),
      Cmd("git push origin push_hash:refs/heads/3.22.5", ""),
      Cmd("git reset --hard origin/main", ""),
      Cmd("git new-branch work-branch --upstream origin/3.22.5", ""),
      Cmd("git checkout -f 3.22.4 -- include/v8-version.h", "",
          cb=self.WriteFakeVersionFile),
      Cmd("git commit -aF \"%s\"" % TEST_CONFIG["COMMITMSG_FILE"], "",
          cb=CheckVersionCommit),
      Cmd("git cl upload --send-mail "
          "-f --set-bot-commit --bypass-hooks --no-autocc --message-file "
          "\"%s\"" % TEST_CONFIG["COMMITMSG_FILE"], ""),
      Cmd("git cl land --bypass-hooks -f", ""),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep="
          "\"Version 3.22.5\" origin/3.22.5", "hsh_to_tag"),
      Cmd("git tag 3.22.5 hsh_to_tag", ""),
      Cmd("git push origin refs/tags/3.22.5:refs/tags/3.22.5", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git branch", "* main\n  work-branch\n"),
      Cmd("git branch -D work-branch", ""),
      Cmd("git gc", ""),
    ]
    self.Expect(expectations)

    args = ["-a", "author@chromium.org",
            "-r", "reviewer@chromium.org",
            "--revision", "push_hash"]
    CreateRelease(TEST_CONFIG, self).Run(args)

    # Note: The version file is on build number 5 again in the end of this test
    # since the git command that merges to main is mocked out.

    # Check for correct content of the WATCHLISTS file

    watchlists_content = FileToText(os.path.join(TEST_CONFIG["DEFAULT_CWD"],
                                          WATCHLISTS_FILE))
    expected_watchlists_content = """
    'merges': [
      # Only enabled on branches created with tools/release/create_release.py
      'v8-merges@googlegroups.com',
    ],
"""
    self.assertEqual(watchlists_content, expected_watchlists_content)

  def testCheckLastPushRecently(self):
    self.Expect([
      Cmd("git fetch origin +refs/tags/*:refs/tags/*", ""),
      Cmd("git tag", self.TAGS),
      Cmd("git log -1 --format=%H 3.22.4", "release_hash\n"),
      Cmd("git log -1 --format=%s release_hash",
          "Version 3.22.4 (based on abc3)\n"),
      Cmd("git log --format=%H abc3..abc123", "\n"),
    ])

    self._state["candidate"] = "abc123"
    self.assertEquals(0, self.RunStep(
        auto_push.AutoPush, LastReleaseBailout, AUTO_PUSH_ARGS))

  def testAutoPush(self):
    self.Expect([
      Cmd("git fetch", ""),
      Cmd("git fetch origin +refs/heads/lkgr:refs/heads/lkgr", ""),
      Cmd("git show-ref -s refs/heads/lkgr", "abc123\n"),
      Cmd("git fetch origin +refs/tags/*:refs/tags/*", ""),
      Cmd("git tag", self.TAGS),
      Cmd("git log -1 --format=%H 3.22.4", "release_hash\n"),
      Cmd("git log -1 --format=%s release_hash",
          "Version 3.22.4 (based on abc3)\n"),
      Cmd("git log --format=%H abc3..abc123", "some_stuff\n"),
    ])

    auto_push.AutoPush(TEST_CONFIG, self).Run(AUTO_PUSH_ARGS + ["--push"])

    state = json.loads(FileToText("%s-state.json"
                                  % TEST_CONFIG["PERSISTFILE_BASENAME"]))

    self.assertEquals("abc123", state["candidate"])

  def testRollMerge(self):
    TEST_CONFIG["ALREADY_MERGING_SENTINEL_FILE"] = self.MakeEmptyTempFile()
    TextToFile("", os.path.join(TEST_CONFIG["DEFAULT_CWD"], ".git"))
    self.WriteFakeVersionFile(build=5)
    os.environ["EDITOR"] = "vi"
    extra_patch = self.MakeEmptyTempFile()

    def VerifyPatch(patch):
      return lambda: self.assertEquals(patch,
          FileToText(TEST_CONFIG["TEMPORARY_PATCH_FILE"]))

    msg = """Version 3.22.5.1 (cherry-pick)

Merged ab12345
Merged ab23456
Merged ab34567
Merged ab45678
Merged ab56789

Title4

Title2

Title3

Title1

Revert "Something"

BUG=123,234,345,456,567,v8:123
"""

    def VerifyLand():
      commit = FileToText(TEST_CONFIG["COMMITMSG_FILE"])
      self.assertEquals(msg, commit)
      version = FileToText(
          os.path.join(TEST_CONFIG["DEFAULT_CWD"], VERSION_FILE))
      self.assertTrue(re.search(r"#define V8_MINOR_VERSION\s+22", version))
      self.assertTrue(re.search(r"#define V8_BUILD_NUMBER\s+5", version))
      self.assertTrue(re.search(r"#define V8_PATCH_LEVEL\s+1", version))
      self.assertTrue(
          re.search(r"#define V8_IS_CANDIDATE_VERSION\s+0", version))

    self.Expect([
      Cmd("git status -s -uno", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git fetch", ""),
      Cmd("git branch", "  branch1\n* branch2\n"),
      Cmd("git new-branch %s --upstream refs/remotes/origin/candidates" %
          TEST_CONFIG["BRANCHNAME"], ""),
      Cmd(("git log --format=%H --grep=\"Port ab12345\" "
           "--reverse origin/main"),
          "ab45678\nab23456"),
      Cmd("git log -1 --format=%s ab45678", "Title1"),
      Cmd("git log -1 --format=%s ab23456", "Title2"),
      Cmd(("git log --format=%H --grep=\"Port ab23456\" "
           "--reverse origin/main"),
          ""),
      Cmd(("git log --format=%H --grep=\"Port ab34567\" "
           "--reverse origin/main"),
          "ab56789"),
      Cmd("git log -1 --format=%s ab56789", "Title3"),
      RL("Y"),  # Automatically add corresponding ports (ab34567, ab56789)?
      # Simulate git being down which stops the script.
      Cmd("git log -1 --format=%s ab12345", None),
      # Restart script in the failing step.
      Cmd("git log -1 --format=%s ab12345", "Title4"),
      Cmd("git log -1 --format=%s ab23456", "Title2"),
      Cmd("git log -1 --format=%s ab34567", "Title3"),
      Cmd("git log -1 --format=%s ab45678", "Title1"),
      Cmd("git log -1 --format=%s ab56789", "Revert \"Something\""),
      Cmd("git log -1 ab12345", "Title4\nBUG=123\nBUG=234"),
      Cmd("git log -1 ab23456", "Title2\n BUG = v8:123,345"),
      Cmd("git log -1 ab34567", "Title3\nBUG=567, 456"),
      Cmd("git log -1 ab45678", "Title1\nBUG="),
      Cmd("git log -1 ab56789", "Revert \"Something\"\nBUG=none"),
      Cmd("git log -1 -p ab12345", "patch4"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch4")),
      Cmd("git log -1 -p ab23456", "patch2"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch2")),
      Cmd("git log -1 -p ab34567", "patch3"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch3")),
      Cmd("git log -1 -p ab45678", "patch1"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch1")),
      Cmd("git log -1 -p ab56789", "patch5\n"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch5\n")),
      Cmd("git apply --index --reject \"%s\"" % extra_patch, ""),
      RL("Y"),  # Automatically increment patch level?
      Cmd("git commit -aF \"%s\"" % TEST_CONFIG["COMMITMSG_FILE"], ""),
      RL("reviewer@chromium.org"),  # V8 reviewer.
      Cmd("git cl upload --send-mail "
          "-r \"reviewer@chromium.org\" --bypass-hooks", ""),
      Cmd("git checkout -f %s" % TEST_CONFIG["BRANCHNAME"], ""),
      RL("LGTM"),  # Enter LGTM for V8 CL.
      Cmd("git cl presubmit", "Presubmit successfull\n"),
      Cmd("git cl land -f --bypass-hooks", "Closing issue\n",
          cb=VerifyLand),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\""
          "Version 3.22.5.1 (cherry-pick)"
          "\" refs/remotes/origin/candidates",
          ""),
      Cmd("git fetch", ""),
      Cmd("git log -1 --format=%H --grep=\""
          "Version 3.22.5.1 (cherry-pick)"
          "\" refs/remotes/origin/candidates",
          "hsh_to_tag"),
      Cmd("git tag 3.22.5.1 hsh_to_tag", ""),
      Cmd("git push origin refs/tags/3.22.5.1:refs/tags/3.22.5.1", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git branch -D %s" % TEST_CONFIG["BRANCHNAME"], ""),
    ])

    # ab12345 and ab34567 are patches. ab23456 (included) and ab45678 are the
    # MIPS ports of ab12345. ab56789 is the MIPS port of ab34567.
    args = ["-f", "-p", extra_patch, "--branch", "candidates",
            "ab12345", "ab23456", "ab34567"]

    # The first run of the script stops because of git being down.
    self.assertRaises(GitFailedException,
        lambda: RollMerge(TEST_CONFIG, self).Run(args))

    # Test that state recovery after restarting the script works.
    args += ["-s", "4"]
    RollMerge(TEST_CONFIG, self).Run(args)

  def testMergeToBranch(self):
    TEST_CONFIG["ALREADY_MERGING_SENTINEL_FILE"] = self.MakeEmptyTempFile()
    TextToFile("", os.path.join(TEST_CONFIG["DEFAULT_CWD"], ".git"))
    self.WriteFakeVersionFile(build=5)
    os.environ["EDITOR"] = "vi"
    extra_patch = self.MakeEmptyTempFile()


    def VerifyPatch(patch):
      return lambda: self.assertEquals(patch,
          FileToText(TEST_CONFIG["TEMPORARY_PATCH_FILE"]))

    info_msg = ("NOTE: This script will no longer automatically "
     "update include/v8-version.h "
     "and create a tag. This is done automatically by the autotag bot. "
     "Please call the merge_to_branch.py with --help for more information.")

    msg = """Merged: Squashed multiple commits.

Merged: Title4
Revision: ab12345

Merged: Title2
Revision: ab23456

Merged: Title3
Revision: ab34567

Merged: Title1
Revision: ab45678

Merged: Revert \"Something\"
Revision: ab56789

BUG=123,234,345,456,567,v8:123
"""

    def VerifyLand():
      commit = FileToText(TEST_CONFIG["COMMITMSG_FILE"])
      self.assertEquals(msg, commit)

    self.Expect([
      Cmd("git status -s -uno", ""),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git fetch", ""),
      Cmd("git branch", "  branch1\n* branch2\n"),
      Cmd("git new-branch %s --upstream refs/remotes/origin/candidates" %
          TEST_CONFIG["BRANCHNAME"], ""),
      Cmd(("git log --format=%H --grep=\"^[Pp]ort ab12345\" "
           "--reverse origin/main"),
          "ab45678\nab23456"),
      Cmd("git log -1 --format=%s ab45678", "Title1"),
      Cmd("git log -1 --format=%s ab23456", "Title2"),
      Cmd(("git log --format=%H --grep=\"^[Pp]ort ab23456\" "
           "--reverse origin/main"),
          ""),
      Cmd(("git log --format=%H --grep=\"^[Pp]ort ab34567\" "
           "--reverse origin/main"),
          "ab56789"),
      Cmd("git log -1 --format=%s ab56789", "Title3"),
      RL("Y"),  # Automatically add corresponding ports (ab34567, ab56789)?
      # Simulate git being down which stops the script.
      Cmd("git log -1 --format=%s ab12345", None),
      # Restart script in the failing step.
      Cmd("git log -1 --format=%s ab12345", "Title4"),
      Cmd("git log -1 --format=%s ab23456", "Title2"),
      Cmd("git log -1 --format=%s ab34567", "Title3"),
      Cmd("git log -1 --format=%s ab45678", "Title1"),
      Cmd("git log -1 --format=%s ab56789", "Revert \"Something\""),
      Cmd("git log -1 ab12345", "Title4\nBUG=123\nBUG=234"),
      Cmd("git log -1 ab23456", "Title2\n BUG = v8:123,345"),
      Cmd("git log -1 ab34567", "Title3\nBug: 567, 456,345"),
      Cmd("git log -1 ab45678", "Title1\nBug:"),
      Cmd("git log -1 ab56789", "Revert \"Something\"\nBUG=none"),
      Cmd("git log -1 -p ab12345", "patch4"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch4")),
      Cmd("git log -1 -p ab23456", "patch2"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch2")),
      Cmd("git log -1 -p ab34567", "patch3"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch3")),
      Cmd("git log -1 -p ab45678", "patch1"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch1")),
      Cmd("git log -1 -p ab56789", "patch5\n"),
      Cmd(("git apply --index --reject \"%s\"" %
           TEST_CONFIG["TEMPORARY_PATCH_FILE"]),
          "", cb=VerifyPatch("patch5\n")),
      Cmd("git apply --index --reject \"%s\"" % extra_patch, ""),
      Cmd("git commit -aF \"%s\"" % TEST_CONFIG["COMMITMSG_FILE"], ""),
      RL("reviewer@chromium.org"),  # V8 reviewer.
      Cmd("git cl upload --send-mail "
          "-r \"reviewer@chromium.org\" --bypass-hooks", ""),
      Cmd("git checkout -f %s" % TEST_CONFIG["BRANCHNAME"], ""),
      RL("LGTM"),  # Enter LGTM for V8 CL.
      Cmd("git cl presubmit", "Presubmit successfull\n"),
      Cmd("git cl land -f --bypass-hooks", "Closing issue\n",
          cb=VerifyLand),
      Cmd("git checkout -f origin/main", ""),
      Cmd("git branch -D %s" % TEST_CONFIG["BRANCHNAME"], ""),
    ])

    # ab12345 and ab34567 are patches. ab23456 (included) and ab45678 are the
    # MIPS ports of ab12345. ab56789 is the MIPS port of ab34567.
    args = ["-f", "-p", extra_patch, "--branch", "candidates",
            "ab12345", "ab23456", "ab34567"]

    # The first run of the script stops because of git being down.
    self.assertRaises(GitFailedException,
        lambda: MergeToBranch(TEST_CONFIG, self).Run(args))

    # Test that state recovery after restarting the script works.
    args += ["-s", "4"]
    MergeToBranch(TEST_CONFIG, self).Run(args)

if __name__ == '__main__':
  unittest.main()
