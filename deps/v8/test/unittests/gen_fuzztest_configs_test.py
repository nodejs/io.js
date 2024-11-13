#!/usr/bin/env python3
# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest

from mock import patch
from pyfakefs import fake_filesystem_unittest
from textwrap import dedent

import gen_fuzztest_configs


class TestFileName(unittest.TestCase):

  def test_errors(self):

    def check_error(test_name):
      with self.assertRaises(AssertionError):
        gen_fuzztest_configs.fuzz_test_to_file_name(test_name)

    check_error('')
    check_error(' ')
    check_error('foo')
    check_error('SimpleTestSuite')
    check_error('ExampleFuzzTest')
    check_error('suite_test_fuzztest')
    check_error('Too.Many.Dots')
    check_error('No.White space')

  def test_conversion(self):

    def check_conversion(test_name, file_name):
      self.assertEqual(file_name,
                       gen_fuzztest_configs.fuzz_test_to_file_name(test_name))

    check_conversion('Foo.Bar', 'v8_foo_bar_fuzztest')
    check_conversion('foo.bar', 'v8_foo_bar_fuzztest')
    check_conversion('SimpleSuite.SimpleTest',
                     'v8_simple_suite_simple_fuzztest')
    check_conversion('RemoveFuzzTest.SimpleTest',
                     'v8_remove_simple_fuzztest')
    check_conversion('IPConversionABC.I24P',
                     'v8_ip_conversion_abc_i24_p_fuzztest')


class TestFullRun(fake_filesystem_unittest.TestCase):

  def test_no_executable(self):
    self.setUpPyfakefs(allow_root_user=True)
    with self.assertRaises(AssertionError):
      # The expected executable is missing.
      gen_fuzztest_configs.main()

  def _set_up_executable(self):
    os.makedirs('/out/build')
    os.chdir('/out/build')
    with open('/out/build/v8_unittests', 'w') as f:
      f.write('I am an executable')

  def test_no_fuzzers(self):
    self.setUpPyfakefs(allow_root_user=True)
    self._set_up_executable()

    with patch('subprocess.check_output', return_value=b''):
      with self.assertRaises(AssertionError) as e:
        gen_fuzztest_configs.main()

  def test_three_fuzzers(self):
    self.setUpPyfakefs(allow_root_user=True)
    self._set_up_executable()

    os.makedirs('/out/build/fuzztests')
    with open('/out/build/fuzztests/leftover_garbage', 'w') as f:
      f.write('')

    fake_fuzz_test_list = dedent("""\
      [*] Fuzz test: FooTest.Test1
      [*] Fuzz test: FooTest.Test2
      [*] Fuzz test: AlphaSortThis.FooBarXYZ""")

    with patch(
        'subprocess.check_output',
        return_value=fake_fuzz_test_list.encode('utf-8')):
      gen_fuzztest_configs.main()

    fuzz_test_output = sorted(os.listdir('/out/build/fuzztests'))
    expexted_fuzz_test_output = [
        'centipede',  # Bash wrapper to ../centipede
        'fuzztests.stamp',
        'v8_alpha_sort_this_foo_bar_xyz_fuzztest',
        'v8_foo_1_fuzztest',
        'v8_foo_2_fuzztest',
    ]
    self.assertEqual(expexted_fuzz_test_output, fuzz_test_output)

    with open('/out/build/fuzztests/fuzztests.stamp') as f:
      self.assertEqual('AlphaSortThis.FooBarXYZ\nFooTest.Test1\nFooTest.Test2',
                       f.read())

    expected_wrapper = dedent("""\
      #!/bin/sh
      BINARY_DIR="$(cd "${0%/*}"/..; pwd)"
      cd $BINARY_DIR
      # Normal fuzzing.
      if [ "$#" -eq  "0" ]; then
         exec $BINARY_DIR/v8_unittests --fuzz=FooTest.Test1 --corpus_database=""
      fi
      # Fuzztest replay.
      if [ "$#" -eq  "1" ]; then
         unset CENTIPEDE_RUNNER_FLAGS
         FUZZTEST_REPLAY=$1 exec $BINARY_DIR/v8_unittests --fuzz=FooTest.Test1 --corpus_database=""
      fi
      """)
    with open('/out/build/fuzztests/v8_foo_1_fuzztest') as f:
      self.assertEqual(expected_wrapper, f.read())


if __name__ == '__main__':
  unittest.main()
