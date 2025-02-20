# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Dummy test suite extension with some fruity tests expecting numfuzz behavior.
"""

from testrunner.local import testsuite
from testrunner.objects import testcase


class TestLoader(testsuite.TestLoader):
  def _list_test_filenames(self):
    return ['bananas', 'apples']


class TestSuite(testsuite.TestSuite):
  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase


class TestCase(testcase.D8TestCase):
  def get_shell(self):
    return 'd8_mocked.py'

  def _get_files_params(self):
    assert self.suite.framework_name == 'num_fuzzer'
    return [self.name]
