#!/usr/bin/env python3
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Global system tests for V8 test runners and fuzzers.

This hooks up the framework under tools/testrunner testing high-level scenarios
with different test suite extensions and build configurations.
"""

# TODO(machenbach): Mock out util.GuessOS to make these tests really platform
# independent.
# TODO(machenbach): Move coverage recording to a global test entry point to
# include other unittest suites in the coverage report.
# TODO(machenbach): Coverage data from multiprocessing doesn't work.
# TODO(majeski): Add some tests for the fuzzers.

from collections import deque
from pathlib import Path

import re
import sys
import unittest
from mock import patch

TOOLS_ROOT = Path(__file__).resolve().parent.parent
sys.path.append(str(TOOLS_ROOT))

from testrunner import standard_runner
from testrunner import num_fuzzer
from testrunner.testproc import base
from testrunner.testproc import fuzzer
from testrunner.testproc import resultdb
from testrunner.testproc.resultdb_server_mock import RDBMockServer
from testrunner.utils.test_utils import (
    temp_base,
    TestRunnerTest,
    with_json_output,
    FakeOSContext,
)


class StandardRunnerTest(TestRunnerTest):

  def setUp(self):
    self.mock_rdb_server = RDBMockServer()
    resultdb.TESTING_SINK = dict(
        auth_token='none', address=self.mock_rdb_server.address)

  def tearDown(self):
    resultdb.TESTING_SINK = None

  def get_runner_class(self):
    return standard_runner.StandardTestRunner

  def testPass(self):
    """Test running only passing tests in two variants.

    Also test printing durations.
    """
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        '--time',
        'sweet/bananas',
        'sweet/raspberries',
    )
    result.stdout_includes('sweet/bananas default: PASS')
    # TODO(majeski): Implement for test processors
    # self.assertIn('Total time:', result.stderr, result)
    # self.assertIn('sweet/bananas', result.stderr, result)
    result.has_returncode(0)

  def testPassHeavy(self):
    """Test running with some tests marked heavy."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        '-j2',
        'sweet',
        baseroot='testroot3',
    )
    result.stdout_includes('7 tests ran')
    result.has_returncode(0)

  def testShardedProc(self):
    for shard in [1, 2]:
      result = self.run_tests(
          '--progress=verbose',
          '--variants=default,stress',
          '--shard-count=2',
          '--shard-run=%d' % shard,
          'sweet/blackberries',
          'sweet/raspberries',
          infra_staging=False,
      )
      # One of the shards gets one variant of each test.
      result.stdout_includes('2 tests ran')
      if shard == 1:
        result.stdout_includes('sweet/raspberries default')
        result.stdout_includes('sweet/raspberries stress')
        result.has_returncode(0)
      else:
        result.stdout_includes(
          'sweet/blackberries default: FAIL')
        result.stdout_includes(
          'sweet/blackberries stress: FAIL')
        result.has_returncode(1)

  @unittest.skip("incompatible with test processors")
  def testSharded(self):
    """Test running a particular shard."""
    for shard in [1, 2]:
      result = self.run_tests(
          '--progress=verbose',
          '--variants=default,stress',
          '--shard-count=2',
          '--shard-run=%d' % shard,
          'sweet/bananas',
          'sweet/raspberries',
      )
      # One of the shards gets one variant of each test.
      result.stdout_includes('Running 2 tests')
      result.stdout_includes('sweet/bananas')
      result.stdout_includes('sweet/raspberries')
      result.has_returncode(0)

  def testFail(self):
    """Test running only failing tests in two variants."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        'sweet/strawberries',
        infra_staging=False,
    )
    result.stdout_includes('sweet/strawberries default: FAIL')
    result.has_returncode(1)

  def testGN(self):
    """Test setup with legacy GN out dir."""
    result = self.run_tests('--gn', baseroot="testroot5", outdir='out.gn')
    result.stdout_includes('>>> Latest GN build found: build')
    result.stdout_includes('Build found: ')
    result.stdout_includes('_v8_test/out.gn/build')
    result.has_returncode(2)

  def testMalformedJsonConfig(self):
    """Test running only failing tests in two variants."""
    result = self.run_tests(baseroot="testroot4")
    result.stdout_includes('contains invalid json')
    result.stdout_includes('Failed to load build config')
    result.stderr_includes('testrunner.base_runner.TestRunnerError')
    result.has_returncode(5)

  def testFailWithRerunAndJSON(self):
    """Test re-running a failing test and output to json."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        '--rerun-failures-count=2',
        '--random-seed=123',
        '--json-test-results', with_json_output,
        'sweet/strawberries',
        infra_staging=False,
    )
    result.stdout_includes('sweet/strawberries default: FAIL')
    # With test processors we don't count reruns as separated failures.
    # TODO(majeski): fix it?
    result.stdout_includes('1 tests failed')
    result.has_returncode(1)

    # TODO(majeski): Previously we only reported the variant flags in the
    # flags field of the test result.
    # After recent changes we report all flags, including the file names.
    # This is redundant to the command. Needs investigation.
    result.json_content_equals('expected_test_results1.json')

  def testRDB(self):
    with self.with_fake_rdb() as records:
      # sweet/bananaflakes fails first time on stress but passes on default
      def tag_dict(tags):
        return {t['key']: t['value'] for t in tags}

      self.run_tests(
          '--variants=default,stress',
          '--rerun-failures-count=2',
          '--time',
          'sweet',
          baseroot='testroot2',
          infra_staging=False,
      )

      self.assertEqual(len(records), 3)
      self.assertEqual(records[0]['testId'], 'sweet/bananaflakes//stress')
      self.assertEqual(tag_dict(records[0]['tags'])['run'], '1')
      self.assertFalse(records[0]['expected'])

      self.assertEqual(records[1]['testId'], 'sweet/bananaflakes//stress')
      self.assertEqual(tag_dict(records[1]['tags'])['run'], '2')
      self.assertTrue(records[1]['expected'])

      self.assertEqual(records[2]['testId'], 'sweet/bananaflakes//default')
      self.assertEqual(tag_dict(records[2]['tags'])['run'], '1')
      self.assertTrue(records[2]['expected'])

  def testFlakeWithRerunAndJSON(self):
    """Test re-running a failing test and output to json."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        '--rerun-failures-count=2',
        '--random-seed=123',
        '--json-test-results', with_json_output,
        'sweet',
        baseroot='testroot2',
        infra_staging=False,
    )
    result.stdout_includes('sweet/bananaflakes default: FAIL PASS')
    result.stdout_includes('=== sweet/bananaflakes (flaky) ===')
    result.stdout_includes('1 tests failed')
    result.stdout_includes('1 tests were flaky')
    result.has_returncode(1)
    result.json_content_equals('expected_test_results2.json')
    self.assertTrue(re.search(
        r'sweet/bananaflakes default: FAIL \(\d+\.\d+:\d+\.\d+\)',
        result.test_schedule))
    self.assertTrue(re.search(
        r'sweet/bananaflakes default: PASS \(\d+\.\d+:\d+\.\d+\)',
        result.test_schedule))

  def testAutoDetect(self):
    """Fake a build with several auto-detected options.

    Using all those options at once doesn't really make much sense. This is
    merely for getting coverage.
    """
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        config_overrides=dict(
            arch="ia32",
            asan=True,
            cfi=True,
            dcheck_always_on=True,
            has_webassembly=True,
            msan=True,
            target_cpu='x86',
            tsan=True,
            ubsan=True,
            use_sanitizer=True,
            v8_target_cpu='x86',
        ))
    result.stdout_includes('>>> Statusfile variables:')
    result.stdout_includes(
        "DEBUG_defined=False, arch=ia32, asan=True, byteorder=little, "
        "cfi=True, code_comments=False, component_build=False, "
        "dcheck_always_on=True, debug_code=False, debugging_features=False, "
        "deopt_fuzzer=False, device_type=None, "
        "dict_property_const_tracking=False, disassembler=False, "
        "endurance_fuzzer=False, full_debug=False, gc_fuzzer=False, "
        "gc_stress=False, gdbjit=False, has_jitless=False, has_maglev=False, "
        "has_turbofan=False, has_webassembly=True, i18n=True, "
        "interrupt_fuzzer=False, is_android=False, is_ios=False, "
        "isolates=False, lite_mode=False, mode=debug, msan=True, "
        "no_harness=False, no_simd_hardware=False, novfp3=False, "
        "optimize_for_size=False, simulator_run=False, slow_dchecks=False, "
        "system=linux, target_cpu=x86, tsan=True, ubsan=True, "
        "use_sanitizer=True, v8_target_cpu=x86, verify_heap=False, "
        "verify_predictable=False")
    result.stdout_includes('>>> Running tests for ia32.release')
    result.has_returncode(0)
    # TODO(machenbach): Test some more implications of the auto-detected
    # options, e.g. that the right env variables are set.

  def testLimitedPreloading(self):
    result = self.run_tests('--progress=verbose', '--variants=default', '-j1',
                            'sweet/*')
    result.stdout_includes("sweet/mangoes default: PASS")
    result.stdout_includes("sweet/cherries default: FAIL")
    result.stdout_includes("sweet/apples default: FAIL")
    result.stdout_includes("sweet/strawberries default: FAIL")
    result.stdout_includes("sweet/bananas default: PASS")
    result.stdout_includes("sweet/blackberries default: FAIL")
    result.stdout_includes("sweet/raspberries default: PASS")

  def testWithFakeContext(self):
    with patch(
        'testrunner.local.context.find_os_context_factory',
        return_value=FakeOSContext):
      result = self.run_tests(
          '--progress=verbose',
          'sweet',
      )
      result.stdout_includes('===>Starting stuff')
      result.stdout_includes('>>> Running tests for x64.release\n'
                             '>>> Running with test processors\n')
      result.stdout_includes('--- stdout ---\nfake stdout 1')
      result.stdout_includes('--- stderr ---\nfake stderr 1')
      result.stdout_includes('=== sweet/raspberries ===')
      result.stdout_includes('=== sweet/cherries ===')
      result.stdout_includes('=== sweet/apples ===')
      result.stdout_includes('Command: fake_wrapper ')
      result.stdout_includes(
          '===\n'
          '=== 4 tests failed\n'
          '===\n'
          '>>> 7 base tests produced 7 (100%) non-filtered tests\n'
          '>>> 7 tests ran\n'
          '<===Stopping stuff\n')

  def testSkips(self):
    """Test skipping tests in status file for a specific variant."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        'sweet/strawberries',
        infra_staging=False,
    )
    result.stdout_includes('0 tests ran')
    result.has_returncode(2)

  def testRunSkips(self):
    """Inverse the above. Test parameter to keep running skipped tests."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=nooptimization',
        '--run-skipped',
        'sweet/strawberries',
    )
    result.stdout_includes('1 tests failed')
    result.stdout_includes('1 tests ran')
    result.has_returncode(1)

  def testDefault(self):
    """Test using default test suites, though no tests are run since they don't
    exist in a test setting.
    """
    result = self.run_tests(
        infra_staging=False,
    )
    result.stdout_includes('0 tests ran')
    result.has_returncode(2)

  def testNoBuildConfig(self):
    """Test failing run when build config is not found."""
    result = self.run_tests(baseroot='wrong_path', with_build_config=False)
    result.stdout_includes('Failed to load build config')
    result.has_returncode(5)

  def testInconsistentArch(self):
    """Test failing run when attempting to wrongly override the arch."""
    result = self.run_tests('--arch=ia32')
    result.stdout_includes(
        '--arch value (ia32) inconsistent with build config (x64).')
    result.has_returncode(5)

  def testWrongVariant(self):
    """Test using a bogus variant."""
    result = self.run_tests('--variants=meh')
    result.has_returncode(5)

  def testModeFromBuildConfig(self):
    """Test auto-detection of mode from build config."""
    result = self.run_tests('--outdir=out/build', 'sweet/bananas')
    result.stdout_includes('Running tests for x64.release')
    result.has_returncode(0)

  def testPredictable(self):
    """Test running a test in verify-predictable mode.

    The test will fail because of missing allocation output. We verify that and
    that the predictable flags are passed and printed after failure.
    """
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        infra_staging=False,
        config_overrides=dict(verify_predictable=True),
    )
    result.stdout_includes('1 tests ran')
    result.stdout_includes('sweet/bananas default: FAIL')
    result.stdout_includes('Test had no allocation output')
    result.stdout_includes('--predictable --verify-predictable')
    result.has_returncode(1)

  def testSlowArch(self):
    """Test timeout factor manipulation on slow architecture."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        'sweet/bananas',
        config_overrides=dict(v8_target_cpu='arm64'),
    )
    # TODO(machenbach): We don't have a way for testing if the correct
    # timeout was used.
    result.has_returncode(0)

  def testRandomSeedStressWithDefault(self):
    """Test using random-seed-stress feature has the right number of tests."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed-stress-count=2',
        'sweet/bananas',
        infra_staging=False,
    )
    result.stdout_includes('2 tests ran')
    result.has_returncode(0)

  def testRandomSeedStressWithSeed(self):
    """Test using random-seed-stress feature passing a random seed."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed-stress-count=2',
        '--random-seed=123',
        'sweet/strawberries',
    )
    result.stdout_includes('2 tests ran')
    # We use a failing test so that the command is printed and we can verify
    # that the right random seed was passed.
    result.stdout_includes('--random-seed=123')
    result.has_returncode(1)

  def testRandomSeedStressWithNumfuzz(self):
    """Test using random-seed-stress feature with numfuzz flavor as used by
    flake bisect for flakes on numfuzz.
    """
    result = self.run_tests(
        '--progress=verbose',
        '--framework=num_fuzzer',
        '--variants=default',
        '--random-seed-stress-count=2',
        'sweet/bananas',
        'sweet/apples',
        infra_staging=False,
        baseroot='testroot7'
    )

    # The bananas test is expected to pass when --fuzzing, one of the numfuzz
    # default flags is present. The apples test is expected to fail with this
    # flag.
    result.stdout_includes('sweet/bananas default: PASS')
    result.stdout_includes('sweet/apples default: FAIL')

    # We get everything twice due to the stress count above set to 2.
    result.stdout_includes('2 tests failed')
    result.stdout_includes('4 tests ran')
    result.has_returncode(1)

  def testSpecificVariants(self):
    """Test using NO_VARIANTS modifiers in status files skips the desire tests.

    The test runner cmd line configures 4 tests to run (2 tests * 2 variants).
    But the status file applies a modifier to each skipping one of the
    variants.
    """
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default,stress',
        'sweet/bananas',
        'sweet/raspberries',
        config_overrides=dict(asan=True),
    )
    # Both tests are either marked as running in only default or only
    # slow variant.
    result.stdout_includes('2 tests ran')
    result.has_returncode(0)

  def testDotsProgress(self):
    result = self.run_tests(
        '--progress=dots',
        'sweet/cherries',
        'sweet/bananas',
        '--no-sorting', '-j1', # make results order deterministic
        infra_staging=False,
    )
    result.stdout_includes('2 tests ran')
    result.stdout_includes('F.')
    result.has_returncode(1)

  def testMonoProgress(self):
    self._testCompactProgress('mono')

  def testColorProgress(self):
    self._testCompactProgress('color')

  def _testCompactProgress(self, name):
    result = self.run_tests(
        '--progress=%s' % name,
        'sweet/cherries',
        'sweet/bananas',
        infra_staging=False,
    )
    if name == 'color':
      expected = ('\033[34m%  28\033[0m|'
                  '\033[32m+   1\033[0m|'
                  '\033[31m-   1\033[0m]: Done')
    else:
      expected = '%  28|+   1|-   1]: Done'
    result.stdout_includes(expected)
    result.stdout_includes('sweet/cherries')
    result.stdout_includes('sweet/bananas')
    result.has_returncode(1)

  def testCompactErrorDetails(self):
    result = self.run_tests(
        '--progress=mono',
        'sweet/cherries',
        'sweet/strawberries',
        infra_staging=False,
    )
    result.stdout_includes('sweet/cherries')
    result.stdout_includes('sweet/strawberries')
    result.stdout_includes('+Mock diff')
    result.has_returncode(1)

  def testExitAfterNFailures(self):
    result = self.run_tests(
        '--progress=verbose',
        '--exit-after-n-failures=2',
        '-j1',
        'sweet/mangoes',       # PASS
        'sweet/strawberries',  # FAIL
        'sweet/blackberries',  # FAIL
        'sweet/raspberries',   # should not run
    )
    result.stdout_includes('sweet/mangoes default: PASS')
    result.stdout_includes('sweet/strawberries default: FAIL')
    result.stdout_includes('Too many failures, exiting...')
    result.stdout_includes('sweet/blackberries default: FAIL')
    result.stdout_excludes('sweet/raspberries')
    result.stdout_includes('2 tests failed')
    result.stdout_includes('3 tests ran')
    result.has_returncode(1)

  def testHeavySequence(self):
    """Test a configuration with 2 heavy tests.
    One heavy test will get buffered before being run.
    """
    appended = 0
    popped = 0

    def mock_deque():

      class MockQ():

        def __init__(self):
          self.buffer = deque()

        def append(self, test):
          nonlocal appended
          self.buffer.append(test)
          appended += 1

        def popleft(self):
          nonlocal popped
          popped += 1
          return self.buffer.popleft()

        def __bool__(self):
          return bool(self.buffer)

      return MockQ()

    # Swarming option will trigger a cleanup routine on the bot
    def mock_kill():
      pass

    with patch('testrunner.testproc.sequence.deque', side_effect=mock_deque), \
         patch('testrunner.testproc.util.kill_processes_linux', side_effect=mock_kill):
      result = self.run_tests(
          '--variants=default', '--swarming', 'fat', baseroot="testroot6")

      result.has_returncode(1)
      self.assertEqual(1, appended)
      self.assertEqual(1, popped)

  def testRunnerFlags(self):
    """Test that runner-specific flags are passed to tests."""
    result = self.run_tests(
        '--progress=verbose',
        '--variants=default',
        '--random-seed=42',
        'sweet/bananas',
        '-v',
    )

    result.stdout_includes(
        '--test bananas --random-seed=42 --nohard-abort --testing-d8-test-runner')
    result.has_returncode(0)


class FakeTimeoutProc(base.TestProcObserver):
  """Fake of the total-timeout observer that just stops after counting
  "count" number of test or result events.
  """
  def __init__(self, count):
    super(FakeTimeoutProc, self).__init__()
    self._n = 0
    self._count = count

  def _on_next_test(self, test):
    self.__on_event()

  def _on_result_for(self, test, result):
    self.__on_event()

  def __on_event(self):
    if self._n >= self._count:
      self.stop()
    self._n += 1


class NumFuzzerTest(TestRunnerTest):
  def get_runner_class(self):
    return num_fuzzer.NumFuzzer

  def testNumFuzzer(self):
    fuzz_flags = [
      f'{flag}=1' for flag in self.get_runner_options()
      if flag.startswith('--stress-')
    ]
    self.assertEqual(len(fuzz_flags), len(fuzzer.FUZZERS))
    for fuzz_flag in fuzz_flags:
      # The fake timeout observer above will stop after proessing the 10th
      # test. This still executes an 11th. Each test causes a test- and a
      # result event internally. We test both paths here.
      for event_count in (19, 20):
        with self.subTest(f'fuzz_flag={fuzz_flag} event_count={event_count}'):
          with patch(
              'testrunner.testproc.timeout.TimeoutProc.create',
              lambda x: FakeTimeoutProc(event_count)):
            result = self.run_tests(
              '--command-prefix', sys.executable,
              '--outdir', 'out/build',
              '--variants=default',
              '--fuzzer-random-seed=12345',
              '--total-timeout-sec=60',
              fuzz_flag,
              '--progress=verbose',
              'sweet/bananas',
            )
            result.has_returncode(0)
            result.stdout_includes('>>> Statusfile variables:')
            result.stdout_includes('11 tests ran')


class OtherTest(TestRunnerTest):
  def testStatusFilePresubmit(self):
    """Test that the fake status file is well-formed."""
    with temp_base() as basedir:
      from testrunner.local import statusfile
      self.assertTrue(statusfile.PresubmitCheck(
          basedir / 'test' / 'sweet' / 'sweet.status'))


if __name__ == '__main__':
  unittest.main()
