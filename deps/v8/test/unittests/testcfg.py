# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

from testrunner.local import command
from testrunner.local import utils
from testrunner.local import testsuite
from testrunner.objects import testcase


ADDITIONAL_VARIANTS = set([
    "maglev",
    "minor_ms",
    "stress_maglev",
])
SHELL = "v8_unittests"


class VariantsGenerator(testsuite.VariantsGenerator):

  def __init__(self, variants):
    super().__init__(variants)
    self._supported_variants = self._standard_variant + [
        v for v in variants if v in ADDITIONAL_VARIANTS
    ]

  def _get_variants(self, test):
    if test.only_standard_variant:
      return self._standard_variant
    return self._supported_variants


class TestLoader(testsuite.TestLoader):
  def _list_test_filenames(self):
    args = ['--gtest_list_tests'] + self.test_config.extra_flags
    shell = self.ctx.platform_shell(SHELL, args, self.test_config.shell_dir)
    output = None
    for i in range(3): # Try 3 times in case of errors.
      cmd = self.ctx.command(
          cmd_prefix=self.test_config.command_prefix, shell=shell, args=args)
      output = cmd.execute()
      if output.exit_code == 0:
        break

      print("Test executable failed to list the tests (try %d).\n\nCmd:" % i)
      print(cmd)
      print("\nStdout:")
      print(output.stdout)
      print("\nStderr:")
      print(output.stderr)
      print("\nExit code: %d" % output.exit_code)
    else:
      raise Exception("Test executable failed to list the tests.")

    # TODO create an ExecutableTestLoader for refactoring this similar to
    # JSTestLoader.
    test_names = []
    test_case = ''
    for line in output.stdout.splitlines():
      # When the command runs through another executable (e.g. iOS Simulator),
      # it is possible that the stdout will show something else besides the
      # actual test output, so it is necessary to harness this case by checking
      # whether the line exists here.
      if not line.strip().split():
        continue
      test_desc = line.strip().split()[0]
      if test_desc.endswith('.'):
        test_case = test_desc
      elif test_case and test_desc:
        test_names.append(test_case + test_desc)

    return sorted(test_names)


class TestSuite(testsuite.TestSuite):
  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase

  def _variants_gen_class(self):
    return VariantsGenerator


class TestCase(testcase.TestCase):
  def _get_suite_flags(self):
    return (
        [f"--gtest_filter={self.name}"] +
        [f"--gtest_random_seed={self.random_seed}"] +
        ["--gtest_print_time=0"]
    )

  def get_shell(self):
    return SHELL

  def _get_cmd_env(self):
    # FuzzTest uses this seed when running fuzz tests as normal gtests.
    # Setting a fixed value guarantees predictable behavior from run to run.
    # It's a base64 encoded vector of 8 zero bytes. In other unit tests this
    # has no effect.
    return {'FUZZTEST_PRNG_SEED': 43 * 'A'}

  def get_android_resources(self):
    # Bytecode-generator tests are the only ones requiring extra files on
    # Android.
    parts = self.name.split('.')
    if parts[0] == 'BytecodeGeneratorTest':
      expectation_file = (
          self.suite.root / 'interpreter' / 'bytecode_expectations' /
          f'{parts[1]}.golden')
      if expectation_file.exists():
        return [expectation_file]
    return []
