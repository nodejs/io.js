#!/usr/bin/env python3
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

from pathlib import Path

# Needed because the test runner contains relative imports.
TOOLS_PATH = Path(__file__).resolve().parents[2]
sys.path.append(str(TOOLS_PATH))

from testrunner.local.command import PosixCommand
from testrunner.local.context import DefaultOSContext
from testrunner.local.testsuite import TestSuite
from testrunner.test_config import TestConfig


class TestSuiteTest(unittest.TestCase):

  def setUp(self):
    test_dir = Path(__file__).resolve().parent
    self.test_root = test_dir / "fake_testsuite"
    self.test_config = TestConfig(
        command_prefix=[],
        extra_flags=[],
        extra_d8_flags=[],
        framework_name='standard_runner',
        isolates=False,
        log_process_stats=False,
        mode_flags=[],
        no_harness=False,
        noi18n=False,
        random_seed=0,
        run_skipped=False,
        shard_count=1,
        shard_id=0,
        shell_dir=Path('fake_testsuite/fake_d8'),
        target_os='macos',
        timeout=10,
        verbose=False,
    )

    self.suite = TestSuite.Load(
        DefaultOSContext(PosixCommand), self.test_root, self.test_config)

  def testLoadingTestSuites(self):
    self.assertEqual(self.suite.name, "fake_testsuite")
    self.assertEqual(self.suite.test_config, self.test_config)

    # Verify that the components of the TestSuite aren't loaded yet.
    self.assertIsNone(self.suite.tests)
    self.assertIsNone(self.suite.statusfile)

  def testLoadingTestsFromDisk(self):
    tests = self.suite.load_tests_from_disk(statusfile_variables={})

    def is_generator(iterator):
      return iterator == iter(iterator)

    self.assertTrue(is_generator(tests))
    self.assertEqual(tests.test_count_estimate, 3)

    heavy_tests = list(tests.heavy_tests)
    slow_tests = list(tests.slow_tests)
    remaining_tests = list(tests.remaining_tests)

    # Verify that the components of the TestSuite are loaded.
    self.assertTrue(
        len(heavy_tests) == len(slow_tests) == len(remaining_tests) == 1)
    self.assertTrue(all(test.is_heavy for test in heavy_tests))
    self.assertTrue(all(test.is_slow for test in slow_tests))
    self.assertFalse(any(test.is_heavy for test in remaining_tests))
    self.assertFalse(any(test.is_slow for test in remaining_tests))
    self.assertIsNotNone(self.suite.statusfile)

  def testMergingTestGenerators(self):
    tests = self.suite.load_tests_from_disk(statusfile_variables={})
    more_tests = self.suite.load_tests_from_disk(statusfile_variables={})

    # Merge the test generators
    tests.merge(more_tests)
    self.assertEqual(tests.test_count_estimate, 6)

    # Check the tests are sorted by memory and speed.
    tests = list(tests)
    heavy_tests = [test.is_heavy for test in tests]
    slow_tests = [test.is_slow for test in tests]

    self.assertEqual(heavy_tests, [True, True, False, False, False, False])
    self.assertEqual(slow_tests, [False, False, True, True, False, False])


if __name__ == '__main__':
  unittest.main()
