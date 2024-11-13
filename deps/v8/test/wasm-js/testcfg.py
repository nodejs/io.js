# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re

from pathlib import Path

from testrunner.local import testsuite
from testrunner.objects import testcase

ANY_JS = ".any.js"
WPT_ROOT = "/wasm/jsapi/"
META_SCRIPT_REGEXP = re.compile(r"META:\s*script=(.*)")
META_TIMEOUT_REGEXP = re.compile(r"META:\s*timeout=(.*)")

proposal_flags = [
    {
        'name': 'js-types',
        'flags': ['--experimental-wasm-type-reflection']
    },
    {
        'name': 'tail-call',
        'flags': []
    },
    {
        'name': 'memory64',
        'flags': ['--experimental-wasm-memory64']
    },
    {
        'name': 'extended-const',
        'flags': []
    },
    {
        'name': 'function-references',
        'flags': []
    },
    {
        'name': 'gc',
        'flags': []
    },
    {
        'name': 'jspi',
        'flags': ['--experimental-wasm-jspi']
    },
]


class TestLoader(testsuite.JSTestLoader):
  @property
  def extensions(self):
    return [ANY_JS]


class TestSuite(testsuite.TestSuite):

  def __init__(self, ctx, *args, **kwargs):
    super(TestSuite, self).__init__(ctx, *args, **kwargs)
    self.mjsunit_js = self.root.parent / "mjsunit" /"mjsunit.js"
    self.test_root = self.root / "tests"
    self._test_loader.test_root = self.test_root

  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase

def get_proposal_identifier(proposal):
  return f"proposals/{proposal['name']}"

class TestCase(testcase.D8TestCase):
  def _get_timeout_param(self):
    source = self.get_source()
    timeout_params = META_TIMEOUT_REGEXP.findall(source)
    if not timeout_params:
      return None

    if timeout_params[0] in ["long"]:
      return timeout_params[0]
    else:
      print("unknown timeout param %s in %s%s"
            % (timeout_params[0], self.path, ANY_JS))
      return None

  def _get_files_params(self):
    files = [self.suite.mjsunit_js,
             self.suite.root / "third_party" / "testharness.js",
             self.suite.root / "testharness-additions.js",
             self.suite.root / "report.js"]

    source = self.get_source()
    current_dir = self._get_source_path().parent
    for script in META_SCRIPT_REGEXP.findall(source):
      if script.startswith(WPT_ROOT):
        # Matched an absolute path, strip the root and replace it with our
        # local root.
        found = False
        for proposal in proposal_flags:
          prop_path = get_proposal_identifier(proposal)
          if prop_path in current_dir.as_posix():
            found = True
            script = self.suite.test_root / prop_path / script[len(WPT_ROOT):]
        if 'wpt' in current_dir.as_posix():
          found = True
          script = self.suite.test_root / 'wpt' / script[len(WPT_ROOT):]
        if not found:
          script = self.suite.test_root / script[len(WPT_ROOT):]
      elif not Path(script).is_absolute():
        # Matched a relative path, prepend this test's directory.
        script = current_dir / script
      else:
        raise Exception(f"Unexpected absolute path for script: \"{script}\"");

      files.append(script)

    files.extend([self._get_source_path(), self.suite.root / "after.js"])
    return files

  def _get_source_flags(self):
    for proposal in proposal_flags:
      if get_proposal_identifier(proposal) in self.name:
        return proposal['flags']
    return ['--wasm-staging']

  def _get_source_path(self):
    # All tests are named `path/name.any.js`
    return self.suite.test_root / self.path_and_suffix(ANY_JS)
