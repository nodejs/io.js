#!/usr/bin/env python3
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pathlib import Path

import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

import coverage
import mock

# Requires python-coverage and python-mock. Native python coverage
# version >= 3.7.1 should be installed to get the best speed.

TOOLS_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS_ROOT)
import run_perf
from testrunner.local import command
from testrunner.objects.output import Output, NULL_OUTPUT


RUN_PERF = os.path.join(TOOLS_ROOT, 'run_perf.py')
TEST_DATA = os.path.join(TOOLS_ROOT, 'unittests', 'testdata')

TEST_WORKSPACE = Path(tempfile.gettempdir()) / 'test-v8-run-perf'

SORT_KEY = lambda x: x['graphs']

V8_JSON = {
  'path': ['.'],
  'owners': ['username@chromium.org'],
  'binary': 'd7',
  'timeout': 60,
  'flags': ['--flag'],
  'main': 'run.js',
  'run_count': 1,
  'results_regexp': '^%s: (.+)$',
  'tests': [
    {'name': 'Richards'},
    {'name': 'DeltaBlue'},
  ]
}

V8_VARIANTS_JSON = {
    'path': ['.'],
    'owners': ['username@chromium.org'],
    'binary': 'd7',
    'timeout': 60,
    'flags': ['--flag'],
    'main': 'run.js',
    'run_count': 1,
    'results_regexp': '%s: (.+)$',
    'variants': [{
        'name': 'default',
        'flags': [],
    }, {
        'name': 'VariantA',
        'flags': ['--variant-a-flag'],
    }, {
        'name': 'VariantB',
        'flags': ['--variant-b-flag'],
    }],
    'tests': [
        {
            'name': 'Richards',
        },
        {
            'name': 'DeltaBlue',
        },
    ]
}

V8_NESTED_SUITES_JSON = {
  'path': ['.'],
  'owners': ['username@chromium.org'],
  'flags': ['--flag'],
  'run_count': 1,
  'units': 'score',
  'tests': [
    {'name': 'Richards',
     'path': ['richards'],
     'binary': 'd7',
     'main': 'run.js',
     'resources': ['file1.js', 'file2.js'],
     'run_count': 2,
     'results_regexp': '^Richards: (.+)$'},
    {'name': 'Sub',
     'path': ['sub'],
     'tests': [
       {'name': 'Leaf',
        'path': ['leaf'],
        'run_count_x64': 3,
        'units': 'ms',
        'main': 'run.js',
        'results_regexp': '^Simple: (.+) ms.$'},
     ]
    },
    {'name': 'DeltaBlue',
     'path': ['delta_blue'],
     'main': 'run.js',
     'flags': ['--flag2'],
     'results_regexp': '^DeltaBlue: (.+)$'},
    {'name': 'ShouldntRun',
     'path': ['.'],
     'archs': ['arm'],
     'main': 'run.js'},
  ]
}

V8_GENERIC_JSON = {
  'path': ['.'],
  'owners': ['username@chromium.org'],
  'binary': 'cc',
  'flags': ['--flag'],
  'generic': True,
  'run_count': 1,
  'units': 'ms',
}


class UnitTest(unittest.TestCase):
  def testBuildDirectory(self):
    base_path = os.path.join(TEST_DATA, 'builddirs', 'dir1', 'out')
    expected_path = os.path.join(base_path, 'build')
    self.assertEqual(expected_path,
                     run_perf.find_build_directory(base_path, 'x64'))


class PerfTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    cls._cov = coverage.coverage(
        include=([os.path.join(TOOLS_ROOT, 'run_perf.py')]))
    cls._cov.start()

  @classmethod
  def tearDownClass(cls):
    cls._cov.stop()
    print('')
    print(cls._cov.report())

  def setUp(self):
    self.maxDiff = None
    if os.path.exists(TEST_WORKSPACE):
      shutil.rmtree(TEST_WORKSPACE)
    os.makedirs(TEST_WORKSPACE)

  def tearDown(self):
    mock.patch.stopall()
    if os.path.exists(TEST_WORKSPACE):
      shutil.rmtree(TEST_WORKSPACE)

  def _WriteTestInput(self, json_content):
    self._test_input = os.path.join(TEST_WORKSPACE, 'test.json')
    with open(self._test_input, 'w') as f:
      f.write(json.dumps(json_content))

  def _MockCommand(self, raw_dirs, raw_outputs, *args, **kwargs):
    on_bots = kwargs.pop('on_bots', False)
    # Fake output for each test run.
    test_outputs = [
        Output(
            stdout=output,
            timed_out=kwargs.get('timed_out', False),
            exit_code=kwargs.get('exit_code', 0),
            start_time=0,
            end_time=42) for output in raw_outputs
    ]

    def create_cmd(*args, **kwargs):
      cmd = mock.MagicMock()
      def execute(*args, **kwargs):
        return test_outputs.pop()
      cmd.execute = mock.MagicMock(side_effect=execute)
      return cmd

    mock.patch.object(
        run_perf.command, 'PosixCommand',
        mock.MagicMock(side_effect=create_cmd)).start()

    build_dir = 'Release' if on_bots else 'x64.release'
    out_dirs = ['out', 'out-secondary']
    return_values = [
      os.path.join(os.path.dirname(TOOLS_ROOT), out, build_dir)
      for out in out_dirs
    ]
    mock.patch.object(
        run_perf, 'find_build_directory',
        mock.MagicMock(side_effect=return_values)).start()

    # Check that d8 is called from the correct cwd for each test run.
    dirs = [os.path.join(TEST_WORKSPACE, dir) for dir in raw_dirs]

    def chdir(dir, *args, **kwargs):
      if not dirs:
        raise Exception("Missing test chdir '%s'" % dir)
      expected_dir = dirs.pop()
      self.assertEqual(
          expected_dir, dir,
          "Unexpected chdir: expected='%s' got='%s'" % (expected_dir, dir))

    os.chdir = mock.MagicMock(side_effect=chdir)

    subprocess.check_call = mock.MagicMock()
    platform.system = mock.MagicMock(return_value='Linux')

  def _CallMain(self, *args):
    self._test_output = os.path.join(TEST_WORKSPACE, 'results.json')
    all_args=[
      '--json-test-results',
      self._test_output,
      self._test_input,
    ]
    all_args += args
    return run_perf.Main(all_args)

  def _LoadResults(self, file_name=None):
    with open(file_name or self._test_output) as f:
      return json.load(f)

  def _VerifyResultTraces(self, traces, file_name=None):
    sorted_expected = sorted(traces, key=SORT_KEY)
    sorted_results = sorted(
        self._LoadResults(file_name)['traces'], key=SORT_KEY)
    self.assertListEqual(sorted_expected, sorted_results)

  def _VerifyResults(self, suite, units, traces, file_name=None):
    self.assertListEqual(sorted([
      {'units': units,
       'graphs': [suite, trace['name']],
       'results': trace['results'],
       'stddev': trace['stddev']} for trace in traces], key=SORT_KEY),
      sorted(self._LoadResults(file_name)['traces'], key=SORT_KEY))

  def _VerifyRunnableDurations(self, runs, timeout, file_name=None):
    self.assertListEqual([
      {
        'graphs': ['test'],
        'durations': [42] * runs,
        'timeout': timeout,
      },
    ], self._LoadResults(file_name)['runnables'])

  def _VerifyErrors(self, errors):
    self.assertListEqual(errors, self._LoadResults()['errors'])

  def _VerifyMock(self, binary, *args, **kwargs):
    shell = os.path.join(os.path.dirname(TOOLS_ROOT), binary)
    command.Command.assert_called_with(
        cmd_prefix=[],
        shell=shell,
        args=list(args),
        timeout=kwargs.get('timeout', 60),
        handle_sigterm=True)

  def _VerifyMockMultiple(self, *args, **kwargs):
    self.assertEqual(len(args), len(command.Command.call_args_list))
    for arg, actual in zip(args, command.Command.call_args_list):
      expected = {
        'cmd_prefix': [],
        'shell': os.path.join(os.path.dirname(TOOLS_ROOT), arg[0]),
        'args': list(arg[1:]),
        'timeout': kwargs.get('timeout', 60),
        'handle_sigterm': True,
      }
      self.assertTupleEqual((expected, ), actual)

  def testOneRun(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyRunnableDurations(1, 60)
    self._VerifyErrors([])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testWarmup_OnEmpty(self):
    warmup_cache = TEST_WORKSPACE / 'test_cache.json'
    mock.patch('run_perf.WARMUP_CACHE_FILE', warmup_cache).start()
    mock.patch('psutil.boot_time', return_value=42).start()
    mock.patch('time.time', return_value=123).start()
    cache_handler = run_perf.CacheHandler(warmup_cache)
    self.assertFalse(cache_handler.cache_file.exists())
    self._WriteTestInput(V8_JSON)

    # Require 2 runs. One for the warm-up.
    self._MockCommand(2 * ['.'], 2 * ['Richards: 1\nDeltaBlue: 2\n'])
    self.assertEqual(0, self._CallMain('--checked-warmup'))

    # The warm-up ran at the current time and cache is up to date.
    self.assertDictEqual({'test': 123}, cache_handler.read_cache())

  def testWarmup_NotNeeded(self):
    warmup_cache = TEST_WORKSPACE / 'test_cache.json'
    mock.patch('run_perf.WARMUP_CACHE_FILE', warmup_cache).start()
    mock.patch('psutil.boot_time', return_value=42).start()
    mock.patch('time.time', return_value=123).start()
    cache_handler = run_perf.CacheHandler(warmup_cache)

    # Test that the old entry is trimmed.
    cache_handler.write_cache({'test': 87, 'some_old_entry': 23})
    self._WriteTestInput(V8_JSON)

    # One run only since no warm-up is required.
    self._MockCommand(1 * ['.'], 1 * ['Richards: 1\nDeltaBlue: 2\n'])
    self.assertEqual(0, self._CallMain('--checked-warmup'))

    # No warm-up ran, cache is only trimmed.
    self.assertDictEqual({'test': 87}, cache_handler.read_cache())

  def testWarmup_Needed(self):
    warmup_cache = TEST_WORKSPACE / 'test_cache.json'
    mock.patch('run_perf.WARMUP_CACHE_FILE', warmup_cache).start()
    mock.patch('psutil.boot_time', return_value=42).start()
    mock.patch('time.time', return_value=123).start()
    cache_handler = run_perf.CacheHandler(warmup_cache)
    cache_handler.write_cache({'test': 23})
    self._WriteTestInput(V8_JSON)

    # Require 2 runs. One for the warm-up.
    self._MockCommand(2 * ['.'], 2 * ['Richards: 1\nDeltaBlue: 2\n'])
    self.assertEqual(0, self._CallMain('--checked-warmup'))

    # The warm-up ran at the current time and cache is up to date.
    self.assertDictEqual({'test': 123}, cache_handler.read_cache())

  def testOneRunVariants(self):
    self._WriteTestInput(V8_VARIANTS_JSON)
    self._MockCommand(['.', '.', '.'], [
        'x\nRichards: 3.3\nDeltaBlue: 3000\ny\n',
        'x\nRichards: 2.2\nDeltaBlue: 2000\ny\n',
        'x\nRichards: 1.1\nDeltaBlue: 1000\ny\n'
    ])
    self.assertEqual(0, self._CallMain())
    self._VerifyResultTraces([
        {
            'units': 'score',
            'graphs': ['test', 'default', 'Richards'],
            'results': [1.1],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'default', 'DeltaBlue'],
            'results': [1000],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantA', 'Richards'],
            'results': [2.2],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantA', 'DeltaBlue'],
            'results': [2000],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantB', 'Richards'],
            'results': [3.3],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantB', 'DeltaBlue'],
            'results': [3000],
            'stddev': ''
        },
    ])
    self._VerifyErrors([])
    self._VerifyMockMultiple(
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release',
                      'd7'), '--flag', '--variant-a-flag', 'run.js'),
        (os.path.join('out', 'x64.release',
                      'd7'), '--flag', '--variant-b-flag', 'run.js'))

  def testOneRunVariantsWithDefault(self):
    config = dict(V8_VARIANTS_JSON)

    # Default value for DeltaBlue
    config['tests'][1]['results_default'] = 42

    self._WriteTestInput(config)
    self._MockCommand(['.', '.', '.'], [
        'x\nRichards: 3.3\nDeltaBlue: 3000\ny\n',
        'x\nRichards: 2.2\nDeltaBlue: 2000\ny\n',
        'x\nRichards: 1.1\ny\n',  # One variant lacks DeltaBlue.
    ])
    self.assertEqual(0, self._CallMain())
    self._VerifyResultTraces([
        {
            'units': 'score',
            'graphs': ['test', 'default', 'Richards'],
            'results': [1.1],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'default', 'DeltaBlue'],
            'results': [42],  # Expected default value.
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantA', 'Richards'],
            'results': [2.2],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantA', 'DeltaBlue'],
            'results': [2000],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantB', 'Richards'],
            'results': [3.3],
            'stddev': ''
        },
        {
            'units': 'score',
            'graphs': ['test', 'VariantB', 'DeltaBlue'],
            'results': [3000],
            'stddev': ''
        },
    ])
    self._VerifyErrors([])
    self._VerifyMockMultiple(
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release',
                      'd7'), '--flag', '--variant-a-flag', 'run.js'),
        (os.path.join('out', 'x64.release',
                      'd7'), '--flag', '--variant-b-flag', 'run.js'))

  def testOneRunWithTestFlags(self):
    test_input = dict(V8_JSON)
    test_input['test_flags'] = ['2', 'test_name']
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['Richards: 1.234\nDeltaBlue: 10657567'])
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join(
      'out', 'x64.release', 'd7'), '--flag', 'run.js', '--', '2', 'test_name')

  def testTwoRuns_Units_SuiteName(self):
    test_input = dict(V8_JSON)
    test_input['run_count'] = 2
    test_input['name'] = 'v8'
    test_input['units'] = 'ms'
    self._WriteTestInput(test_input)
    self._MockCommand(['.', '.'],
                      ['Richards: 100\nDeltaBlue: 200\n',
                       'Richards: 50\nDeltaBlue: 300\n'])
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('v8', 'ms', [
      {'name': 'Richards', 'results': [50.0, 100.0], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [300.0, 200.0], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join(
      'out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testTwoRuns_SubRegexp(self):
    test_input = dict(V8_JSON)
    test_input['run_count'] = 2
    del test_input['results_regexp']
    test_input['tests'][0]['results_regexp'] = '^Richards: (.+)$'
    test_input['tests'][1]['results_regexp'] = '^DeltaBlue: (.+)$'
    self._WriteTestInput(test_input)
    self._MockCommand(['.', '.'],
                      ['Richards: 100\nDeltaBlue: 200\n',
                       'Richards: 50\nDeltaBlue: 300\n'])
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [50.0, 100.0], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [300.0, 200.0], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join(
      'out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testPerfectConfidenceRuns(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(
        ['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'] * 10)
    self.assertEqual(0, self._CallMain('--confidence-level', '1'))
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234] * 10, 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0] * 10, 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join(
      'out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testNoisyConfidenceRuns(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(
        ['.'],
        reversed([
          # First 10 runs are mandatory. DeltaBlue is slightly noisy.
          'x\nRichards: 1.234\nDeltaBlue: 10757567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10557567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          # Need 4 more runs for confidence in DeltaBlue results.
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
          'x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n',
        ]),
    )
    self.assertEqual(0, self._CallMain('--confidence-level', '1'))
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234] * 14, 'stddev': ''},
      {
        'name': 'DeltaBlue',
        'results': [10757567.0, 10557567.0] + [10657567.0] * 12,
        'stddev': '',
      },
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join(
      'out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testNestedSuite(self):
    self._WriteTestInput(V8_NESTED_SUITES_JSON)
    self._MockCommand(['delta_blue', 'sub/leaf', 'richards'],
                      ['DeltaBlue: 200\n',
                       'Simple: 1 ms.\n',
                       'Simple: 2 ms.\n',
                       'Simple: 3 ms.\n',
                       'Richards: 100\n',
                       'Richards: 50\n'])
    self.assertEqual(0, self._CallMain())
    self.assertListEqual(sorted([
      {'units': 'score',
       'graphs': ['test', 'Richards'],
       'results': [50.0, 100.0],
       'stddev': ''},
      {'units': 'ms',
       'graphs': ['test', 'Sub', 'Leaf'],
       'results': [3.0, 2.0, 1.0],
       'stddev': ''},
      {'units': 'score',
       'graphs': ['test', 'DeltaBlue'],
       'results': [200.0],
       'stddev': ''},
      ], key=SORT_KEY), sorted(self._LoadResults()['traces'], key=SORT_KEY))
    self._VerifyErrors([])
    self._VerifyMockMultiple(
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd8'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd8'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd8'), '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd8'),
         '--flag', '--flag2', 'run.js'))


  def testOneRunStdDevRegExp(self):
    test_input = dict(V8_JSON)
    test_input['stddev_regexp'] = r'^%s-stddev: (.+)$'
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['Richards: 1.234\nRichards-stddev: 0.23\n'
                              'DeltaBlue: 10657567\nDeltaBlue-stddev: 106\n'])
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': '0.23'},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': '106'},
    ])
    self._VerifyErrors([])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testTwoRunsStdDevRegExp(self):
    test_input = dict(V8_JSON)
    test_input['stddev_regexp'] = r'^%s-stddev: (.+)$'
    test_input['run_count'] = 2
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['Richards: 3\nRichards-stddev: 0.7\n'
                              'DeltaBlue: 6\nDeltaBlue-boom: 0.9\n',
                              'Richards: 2\nRichards-stddev: 0.5\n'
                              'DeltaBlue: 5\nDeltaBlue-stddev: 0.8\n'])
    self.assertEqual(1, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [2.0, 3.0], 'stddev': '0.7'},
      {'name': 'DeltaBlue', 'results': [5.0, 6.0], 'stddev': '0.8'},
    ])
    self._VerifyErrors([
        'Test test/Richards should only run once since a stddev is provided '
        'by the test.',
        'Test test/DeltaBlue should only run once since a stddev is provided '
        'by the test.',
        r'Regexp "^DeltaBlue-stddev: (.+)$" did not match for test '
        r'test/DeltaBlue.'
    ])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testBuildbot(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['Richards: 1.234\nDeltaBlue: 10657567\n'],
                      on_bots=True)
    mock.patch.object(
        run_perf.Platform, 'ReadBuildConfig',
        mock.MagicMock(return_value={'is_android': False})).start()
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join('out', 'Release', 'd7'), '--flag', 'run.js')

  def testBuildbotWithTotal(self):
    test_input = dict(V8_JSON)
    test_input['total'] = True
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['Richards: 1.234\nDeltaBlue: 10657567\n'],
                      on_bots=True)
    mock.patch.object(
        run_perf.Platform, 'ReadBuildConfig',
        mock.MagicMock(return_value={'is_android': False})).start()
    self.assertEqual(0, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
      {'name': 'Total', 'results': [3626.491097190233], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join('out', 'Release', 'd7'), '--flag', 'run.js')

  def testBuildbotWithTotalAndErrors(self):
    test_input = dict(V8_JSON)
    test_input['total'] = True
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['x\nRichards: bla\nDeltaBlue: 10657567\ny\n'],
                      on_bots=True)
    mock.patch.object(
        run_perf.Platform, 'ReadBuildConfig',
        mock.MagicMock(return_value={'is_android': False})).start()
    self.assertEqual(1, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyErrors(
        ['Regexp "^Richards: (.+)$" '
         'returned a non-numeric for test test/Richards.',
         'Not all traces have produced results. Can not compute total for '
         'test.'])
    self._VerifyMock(os.path.join('out', 'Release', 'd7'), '--flag', 'run.js')

  def testRegexpNoMatch(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['x\nRichaards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertEqual(1, self._CallMain())
    self._VerifyResults('test', 'score', [
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyErrors(
        ['Regexp "^Richards: (.+)$" did not match for test test/Richards.'])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testFilterInvalidRegexp(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertNotEqual(0, self._CallMain("--filter=((("))
    self._VerifyMock(os.path.join('out', 'Release', 'd7'), '--flag', 'run.js')

  def testFilterRegexpMatchAll(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertEqual(0, self._CallMain("--filter=test"))
    self._VerifyResults('test', 'score', [
        {
            'name': 'Richards',
            'results': [1.234],
            'stddev': ''
        },
        {
            'name': 'DeltaBlue',
            'results': [10657567.0],
            'stddev': ''
        },
    ])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testFilterRegexpSkipAll(self):
    self._WriteTestInput(V8_JSON)
    self._MockCommand(['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertEqual(0, self._CallMain("--filter=NonExistingName"))
    self._VerifyResults('test', 'score', [])

  def testOneRunCrashed(self):
    test_input = dict(V8_JSON)
    test_input['retry_count'] = 1
    self._WriteTestInput(test_input)
    self._MockCommand(
        ['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n', ''],
        exit_code=-1)
    self.assertEqual(1, self._CallMain())
    self._VerifyResults('test', 'score', [])
    self._VerifyErrors([])
    self._VerifyMock(
        os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js')

  def testOneRunTimingOut(self):
    test_input = dict(V8_JSON)
    test_input['timeout'] = 70
    test_input['retry_count'] = 0
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], [''], timed_out=True)
    self.assertEqual(1, self._CallMain())
    self._VerifyResults('test', 'score', [])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join('out', 'x64.release', 'd7'),
                     '--flag', 'run.js', timeout=70)

  def testAndroid(self):
    self._WriteTestInput(V8_JSON)
    mock.patch('run_perf.AndroidPlatform.PreExecution').start()
    mock.patch('run_perf.AndroidPlatform.PostExecution').start()
    mock.patch('run_perf.AndroidPlatform.PreTests').start()
    mock.patch('run_perf.find_build_directory').start()
    mock.patch(
        'run_perf.AndroidPlatform.Run',
        return_value=(Output(stdout='Richards: 1.234\nDeltaBlue: 10657567\n'),
                      NULL_OUTPUT)).start()
    mock.patch('testrunner.local.android.Driver', autospec=True).start()
    mock.patch(
        'run_perf.Platform.ReadBuildConfig',
        return_value={'is_android': True}).start()
    self.assertEqual(0, self._CallMain('--arch', 'arm'))
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])

  def testTwoRuns_Trybot(self):
    test_input = dict(V8_JSON)
    test_input['run_count'] = 2
    self._WriteTestInput(test_input)
    self._MockCommand(['.', '.', '.', '.'],
                      ['Richards: 100\nDeltaBlue: 200\n',
                       'Richards: 200\nDeltaBlue: 20\n',
                       'Richards: 50\nDeltaBlue: 200\n',
                       'Richards: 100\nDeltaBlue: 20\n'])
    test_output_secondary = os.path.join(
        TEST_WORKSPACE, 'results_secondary.json')
    self.assertEqual(0, self._CallMain(
        '--outdir-secondary', 'out-secondary',
        '--json-test-results-secondary', test_output_secondary,
    ))
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [100.0, 200.0], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [20.0, 20.0], 'stddev': ''},
    ])
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [50.0, 100.0], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [200.0, 200.0], 'stddev': ''},
    ], test_output_secondary)
    self._VerifyRunnableDurations(2, 60, test_output_secondary)
    self._VerifyErrors([])
    self._VerifyMockMultiple(
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out-secondary', 'x64.release', 'd7'),
         '--flag', 'run.js'),
        (os.path.join('out', 'x64.release', 'd7'), '--flag', 'run.js'),
        (os.path.join('out-secondary', 'x64.release', 'd7'),
         '--flag', 'run.js'),
    )

  def testWrongBinaryWithProf(self):
    test_input = dict(V8_JSON)
    self._WriteTestInput(test_input)
    self._MockCommand(['.'], ['x\nRichards: 1.234\nDeltaBlue: 10657567\ny\n'])
    self.assertEqual(0, self._CallMain('--extra-flags=--prof'))
    self._VerifyResults('test', 'score', [
      {'name': 'Richards', 'results': [1.234], 'stddev': ''},
      {'name': 'DeltaBlue', 'results': [10657567.0], 'stddev': ''},
    ])
    self._VerifyErrors([])
    self._VerifyMock(os.path.join('out', 'x64.release', 'd7'),
                     '--flag', '--prof', 'run.js')

  #############################################################################
  ### System tests

  def _RunPerf(self, mocked_d8, test_json):
    output_json = os.path.join(TEST_WORKSPACE, 'output.json')
    args = [
      os.sys.executable, RUN_PERF,
      '--binary-override-path', os.path.join(TEST_DATA, mocked_d8),
      '--json-test-results', output_json,
      os.path.join(TEST_DATA, test_json),
    ]
    subprocess.check_output(args)
    return self._LoadResults(output_json)

  def testNormal(self):
    results = self._RunPerf('d8_mocked1.py', 'test1.json')
    self.assertListEqual([], results['errors'])
    self.assertListEqual(sorted([
      {
        'units': 'score',
        'graphs': ['test1', 'Richards'],
        'results': [1.2, 1.2],
        'stddev': '',
      },
      {
        'units': 'score',
        'graphs': ['test1', 'DeltaBlue'],
        'results': [2.1, 2.1],
        'stddev': '',
      },
    ], key=SORT_KEY), sorted(results['traces'], key=SORT_KEY))

  def testResultsProcessor(self):
    results = self._RunPerf('d8_mocked2.py', 'test2.json')
    self.assertListEqual([], results['errors'])
    self.assertListEqual([
      {
        'units': 'score',
        'graphs': ['test2', 'Richards'],
        'results': [1.2, 1.2],
        'stddev': '',
      },
      {
        'units': 'score',
        'graphs': ['test2', 'DeltaBlue'],
        'results': [2.1, 2.1],
        'stddev': '',
      },
    ], results['traces'])

  def testResultsProcessorNested(self):
    results = self._RunPerf('d8_mocked2.py', 'test3.json')
    self.assertListEqual([], results['errors'])
    self.assertListEqual([
      {
        'units': 'score',
        'graphs': ['test3', 'Octane', 'Richards'],
        'results': [1.2],
        'stddev': '',
      },
      {
        'units': 'score',
        'graphs': ['test3', 'Octane', 'DeltaBlue'],
        'results': [2.1],
        'stddev': '',
      },
    ], results['traces'])


if __name__ == '__main__':
  unittest.main()
