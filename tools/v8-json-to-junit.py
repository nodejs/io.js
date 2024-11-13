#!/usr/bin/env python
# Large parts of this file are modified from
# deps/v8/tools/testrunner/local/junit_output.py, which no longer exists in
# latest V8.
#
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
import utils
import signal
import sys
import xml.etree.ElementTree as ET

def IsExitCodeCrashing(exit_code):
  if utils.IsWindows():
    return 0x80000000 & exit_code and not (0x3FFFFF00 & exit_code)
  return exit_code < 0 and exit_code != -signal.SIGABRT


class JUnitTestOutput:
  def __init__(self, test_suite_name):
    self.root = ET.Element("testsuite")
    self.root.attrib["name"] = test_suite_name

  def HasRunTest(self, test_name, test_cmd, test_duration, test_failure):
    test_case_element = ET.Element("testcase")
    test_case_element.attrib["name"] = test_name
    test_case_element.attrib["cmd"] = test_cmd
    test_case_element.attrib["time"] = str(round(test_duration, 3))
    if test_failure is not None:
      failure_element = ET.Element("failure")
      failure_element.text = test_failure
      test_case_element.append(failure_element)
    self.root.append(test_case_element)

  def FinishAndWrite(self, f):
    ET.ElementTree(self.root).write(f, "UTF-8")


def Main():
  test_results = json.load(sys.stdin)

  # V8's JSON test runner only logs failing and flaky tests under "results". We
  # assume the caller has put a large number for --slow-tests-cutoff, to ensure
  # that all the tests appear under "slowest_tests".

  failing_tests = {result["name"]: result for result in test_results["results"]}
  all_tests = {result["name"]: result for result in test_results["slowest_tests"]}
  passing_tests = {
    name: result for name, result in all_tests.items() if name not in failing_tests
  }

  # These check that --slow-tests-cutoff was passed correctly.
  assert len(failing_tests) + len(passing_tests) == len(all_tests)
  assert len(all_tests) == len(test_results["slowest_tests"])

  output = JUnitTestOutput("v8tests")

  for name, failing_test in failing_tests.items():
    failing_output = []

    stdout = failing_test["stdout"].strip()
    if len(stdout):
      failing_output.append("stdout:")
      failing_output.append(stdout)

    stderr = failing_test["stderr"].strip()
    if len(stderr):
      failing_output.append("stderr:")
      failing_output.append(stderr)

    failing_output.append("Command: " + failing_test["command"])

    exit_code = failing_test["exit_code"]
    if failing_test["result"] == "TIMEOUT":
      failing_output.append("--- TIMEOUT ---")
    elif IsExitCodeCrashing(exit_code):
      failing_output.append("exit code: " + str(exit_code))
      failing_output.append("--- CRASHED ---")

    output.HasRunTest(
      test_name=name,
      test_cmd=failing_test["command"],
      test_duration=failing_test["duration"],
      test_failure="\n".join(failing_output),
    )

  for name, passing_test in passing_tests.items():
    output.HasRunTest(
      test_name=name,
      test_cmd=passing_test["command"],
      test_duration=passing_test["duration"],
      test_failure=None,
    )

  output.FinishAndWrite(sys.stdout.buffer)

if __name__ == '__main__':
  Main()
