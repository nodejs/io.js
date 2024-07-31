# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import namedtuple
from pyfakefs import fake_filesystem_unittest

import json
import os
import sys
import unittest

# Needed because the test runner contains relative imports.
TOOLS_PATH = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(TOOLS_PATH)

# Standard library imports...
from testrunner.testproc.resultdb import ResultDBIndicator, write_artifact
from testrunner.testproc.resultdb_server_mock import RDBMockServer

from testrunner.local.command import BaseCommand
from testrunner.objects.output import Output


def mock_test262(is_fail, rdb_test_id):
  Test = namedtuple('Test', [
      'is_fail', 'rdb_test_id', 'expected_outcomes', 'framework_name',
      'full_name', 'random_seed', 'shard_id', 'shard_count', 'shell', 'variant',
      'variant_flags', 'processor_name', 'subtest_id', 'path', 'skip_rdb'
  ])

  def skip_if_expected(result):
    return not result.has_unexpected_output

  return Test(is_fail, rdb_test_id, [], 'unittest', 'test-123', 0, 1, 2, '',
              'default', [], 'none', None, '/tmp/test/some_test.js',
              skip_if_expected)


def mock_result(is_fail, duration=0):
  Result = namedtuple('Result', ['has_unexpected_output', 'output', 'cmd'])
  return Result(
      is_fail,
      Output(start_time=100.0, end_time=100.0 + duration),
      BaseCommand('echo'))


class TestResultDBIndicator(unittest.TestCase):

  def test_send(self):

    def assert_sent_count(count):
      self.assertEqual(len(server.post_data), count)

    server = RDBMockServer()
    self.rdb = ResultDBIndicator(
        context=None,
        options=None,
        test_count=0,
        sink=dict(auth_token='none', address=server.address))

    self.rdb.send_result(
        mock_test262(True, 'expected2fail-fail'), mock_result(False), 0)
    assert_sent_count(0)

    self.rdb.send_result(
        mock_test262(True, 'expected2fail-pass'), mock_result(True), 0)
    assert_sent_count(1)

    self.rdb.send_result(
        mock_test262(False, 'expected2pass-fail'), mock_result(True), 0)
    assert_sent_count(2)

    self.rdb.send_result(
        mock_test262(False, 'expected2pass-pass'), mock_result(False), 0)
    assert_sent_count(2)

    self.rdb.send_result(
        mock_test262(True, 'fast'), mock_result(True, duration=0.00001), 0)
    assert_sent_count(3)

    def assert_testid_at_index(testid, idx):
      data = json.loads(server.post_data[idx])
      self.assertEqual(data['testResults'][0]['testId'], testid)

    def assert_duration_at_index(duration_str, idx):
      data = json.loads(server.post_data[idx])
      self.assertEqual(data['testResults'][0]['duration'], duration_str)

    assert_testid_at_index('expected2fail-pass', 0)
    assert_testid_at_index('expected2pass-fail', 1)
    assert_duration_at_index('0.000010s', 2)


class TestFileOutput(fake_filesystem_unittest.TestCase):
  def test_write_artifact_encoded(self):
    """Test that characters mapping to utf-8 encoding work."""
    self.setUpPyfakefs(allow_root_user=True)
    with open(write_artifact('\u0394')['filePath']) as f:
      self.assertEqual('\u0394', f.read())


if __name__ == '__main__':
  unittest.main()
