# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import mocks
import textwrap
import unittest

from pyfakefs import fake_filesystem_unittest
from v8_importer import (DeletedTestsRemover, GitFileStatus,
                         RenamedTestsUpdater, V8TestImporter)
from v8configs import to_object

fake_host = to_object({
    'project_config': {
        'project_root':
            '.',
        'paths_to_sync': [{
            "source": "test/staging",
            "destination": "test/test262/local-tests/test/staging"
        }]
    },
})
V8_REVISION = 'abcdef123456'
TEST262_REVISION = '123456abcdef'

class Test_TestV8Importer(fake_filesystem_unittest.TestCase):

  def test_phases(self):
    importer = V8TestImporter('ALL', fake_host)
    self.assertTrue(importer.run_prebuild_phase())
    self.assertTrue(importer.run_build_phase())
    self.assertTrue(importer.run_postbuild_phase())
    self.assertTrue(importer.run_upload_phase())

    importer = V8TestImporter('PREBUILD', fake_host)
    self.assertTrue(importer.run_prebuild_phase())
    self.assertFalse(importer.run_build_phase())
    self.assertFalse(importer.run_postbuild_phase())
    self.assertFalse(importer.run_upload_phase())

    importer = V8TestImporter('POSTBUILD', fake_host)
    self.assertFalse(importer.run_prebuild_phase())
    self.assertFalse(importer.run_build_phase())
    self.assertTrue(importer.run_postbuild_phase())
    self.assertFalse(importer.run_upload_phase())

    importer = V8TestImporter('UPLOAD', fake_host)
    self.assertFalse(importer.run_prebuild_phase())
    self.assertFalse(importer.run_build_phase())
    self.assertFalse(importer.run_postbuild_phase())
    self.assertTrue(importer.run_upload_phase())

  def test_sync_folders(self):
    self.setUpPyfakefs(allow_root_user=True)

    destination = 'test/test262/local-tests/test/staging'
    self.fs.create_file(f'{destination}/test1.js')
    self.fs.create_file(f'{destination}/features.txt')
    self.fs.create_file(f'{destination}/f1/test1.js')
    self.fs.create_file(f'{destination}/f1/test2.js')

    def get_git_file_status(*args):
      path = str(args[2])
      self.assertFalse(path.endswith('features.txt'))
      return GitFileStatus.ADDED if path.endswith(
          'test1.js') else GitFileStatus.UNKNOWN

    importer = V8TestImporter('X', fake_host)
    importer.local_test262 = to_object({
        'path': '.',
    })
    importer.get_git_file_status = get_git_file_status

    importer.sync_folders(V8_REVISION, TEST262_REVISION)

    self.assertFalse(self.fs.exists(f'{destination}/test1.js'))
    self.assertTrue(self.fs.exists(f'{destination}/features.txt'))
    self.assertFalse(self.fs.exists(f'{destination}/f1/test1.js'))
    self.assertTrue(self.fs.exists(f'{destination}/f1/test2.js'))

  def test_remove_and_rename_tests(self):
    self.setUpPyfakefs(allow_root_user=True)
    self.fs.create_file(
        'test/test262/test262.status',
        contents=textwrap.dedent("""\
                n'importe quoi
                ...
                  'folder1/sometest1': [FAIL],
                  'deleted_testname': [FAIL],
                  'renamed_testname': [FAIL],
                  'folder2/sometest1': [FAIL],
                  'folder2/sometest2': [FAIL],
                ...
                """))
    importer = V8TestImporter('X', fake_host)
    importer.test262_git = to_object({
        'run':
            lambda *args: 'test/deleted_testname.js\n'
            'R001   test/renamed_testname.js    test/updated_testname.js\n',
    })

    tests = importer.remove_and_rename(V8_REVISION, TEST262_REVISION)

    self.assertEquals(
        textwrap.dedent("""\
                n'importe quoi
                ...
                  'folder1/sometest1': [FAIL],
                  'updated_testname': [FAIL],
                  'folder2/sometest1': [FAIL],
                  'folder2/sometest2': [FAIL],
                ...
                """), ''.join(tests))

  def test_failed_tests_to_status_lines(self):
    importer = V8TestImporter('X', fake_host)
    result = importer.failed_tests_to_status_lines(['test1', 'test2'])
    self.assertSequenceEqual(["  'test1': [FAIL],\n", "  'test2': [FAIL],\n"],
                             result)

  def test_rewrite_status_file_content(self):
    # Below \n is used inside the text block to avoid a trailing whitespace
    # check
    updated_status = textwrap.dedent("""\
                some_testname
                some random line
                some other testname\n
                ]
                """)
    updated_status = updated_status.splitlines(keepends=True)
    added_lines = ['  new test 1\n', '  new test 2\n']

    importer = V8TestImporter('X', fake_host)
    result = importer.rewrite_status_file_content(updated_status, added_lines,
                                                  V8_REVISION, TEST262_REVISION)

    self.assertEquals(textwrap.dedent("""\
                some_testname
                some random line
                some other testname\n
                ####
                # Import test262@123456ab
                # https://chromium.googlesource.com/external/github.com/tc39/test262/+log/abcdef12..123456ab
                [ALWAYS, {
                  new test 1
                  new test 2
                }],
                # End import test262@123456ab
                ####\n
                ]
                """), ''.join(result))

  def test_deletion_updater(self):
    importer = V8TestImporter('X', fake_host)
    importer.test262_git = to_object({
        'run': lambda *args: textwrap.dedent("""\
                        test/should_not_match.js extra garbage
                         test/should_not_match2.js
                        test/some_testname.js
                        practically garbage
                        """),
    })

    updater = DeletedTestsRemover(importer, 'a', 'b')
    self.assertEquals(['some_testname'], updater.to_update)

  def test_rename_updater(self):
    importer = V8TestImporter('X', fake_host)
    importer.test262_git = to_object({
        'run':
            lambda *args: textwrap.dedent("""\
                        M test/should_not_match.js test/should_not_match.js
                         test/should_not_match2.js
                        R001 test/rename_testname.js   test/updated_testname.js
                        R002  test/folder1/rename_testname.js  test/folder2/updated_testname.js
                        practically garbage
                        """),
    })

    updater = RenamedTestsUpdater(importer, 'a', 'b')
    expected = {
        'rename_testname': 'updated_testname',
        'folder1/rename_testname': 'folder2/updated_testname',
    }
    self.assertEquals(expected, updater.to_update)


if __name__ == '__main__':
  unittest.main()
