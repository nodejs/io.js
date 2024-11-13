#!/usr/bin/env vpython3
# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import base64
import contextlib
import io
import os
import sys
import unittest

from mock import patch
from pathlib import Path

import roll_merge_gerrit


TEST_DATA = Path(__file__).resolve().parent / 'testdata'
HAPPY_PATH_LOG = TEST_DATA / 'roll_merge_gerrit_happy_path.txt'

ROLL_COMMIT_MESSAGE = """V8 roll from 123abc to 456def

https://chromium.googlesource.com/v8/v8.git/+log/123abc..456def

2038-01-19 v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com Version 1.2.3
"""

V8_VERSION_FILE_AT_CHERRY_PICK = """
#define V8_MAJOR_VERSION 1
#define V8_MINOR_VERSION 2
#define V8_BUILD_NUMBER 3
#define V8_PATCH_LEVEL 0
""".encode('utf-8')

V8_VERSION_FILE_AT_ROLL = """
#define V8_MAJOR_VERSION 1
#define V8_MINOR_VERSION 2
#define V8_BUILD_NUMBER 1
#define V8_PATCH_LEVEL 0
""".encode('utf-8')

V8_VERSION_ROLL_RESPONSE = io.StringIO(
    base64.b64encode(V8_VERSION_FILE_AT_ROLL).decode('utf-8'))


class TestStats(unittest.TestCase):

  @patch(
      'gerrit_util.QueryChanges',
      return_value=[{
          'subject': ROLL_COMMIT_MESSAGE.splitlines()[0],
          'current_revision': '789acf',
          'revisions': {
              '789acf': {
                  'commit': {
                      'message': ROLL_COMMIT_MESSAGE
                  }
              }
          }
      }])
  @patch(
      'gerrit_util.CherryPick', return_value={
          '_number': 42,
          'change_id': 23
      })
  @patch(
      'gerrit_util.GetFileContents',
      return_value=V8_VERSION_FILE_AT_CHERRY_PICK)
  @patch(
      'gerrit_util.CallGerritApi',
      side_effect=[
          None, {
              'labels': {
                  'Code-Review': {
                      'all': [{
                          'value': 1
                      }]
                  }
              }
          }, {
              'ref': 'refs/heads/roll',
              'revision': 'beefdead'
          }, {
              'revision': 'deadbeefce'
          }
      ])
  @patch('gerrit_util.ChangeEdit')
  @patch('gerrit_util.GetChangeCommit', side_effect=[
      {'commit': 'deadbeef', 'subject': 'Fix everything'},
      {'commit': 'deadbeefce'}])
  @patch('gerrit_util.SetChangeEditMessage')
  @patch('gerrit_util.PublishChangeEdit')
  @patch('gerrit_util.SetReview')
  @patch('gerrit_util.AddReviewers')
  @patch('gerrit_util.SubmitChange',
         return_value={'status': 'MERGED', 'project': 'v8/v8'})
  @patch('gerrit_util.CreateGerritTag')
  @patch('gerrit_util.CreateHttpConn')
  @patch('gerrit_util.ReadHttpResponse',
         return_value=V8_VERSION_ROLL_RESPONSE)
  def test_happy_path(self, *args):
    """Test the path that succeeds in every step.

    The test data above is composed of dummy values, designed
    to get the script through from end to end.
    """
    stdout = io.StringIO()
    with contextlib.redirect_stdout(stdout):
      roll_merge_gerrit.main(['deadbeef'])
    actual_stdout = stdout.getvalue().replace('\r', '')
    if os.environ.get('GENERATE') == 'true':
      with open(HAPPY_PATH_LOG, 'w') as f:
        f.write(actual_stdout)
    with open(HAPPY_PATH_LOG) as f:
      self.assertEqual(
          f.read(),
          actual_stdout,
          'Call testing with the GENERATE=true env var to update the log.')


if __name__ == '__main__':
  unittest.main()
