#!/usr/bin/env python3
#
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from functools import reduce
from pathlib import Path

import datetime
import json
import os
import sys
import tempfile
from testrunner.testproc.rerun import RerunProc
from testrunner.testproc.timeout import TimeoutProc
from testrunner.testproc.progress import ResultsTracker, ProgressProc
from testrunner.testproc.shard import ShardProc

import testrunner.base_runner as base_runner

from testrunner.local.variants import ALL_VARIANTS
from testrunner.objects import predictable
from testrunner.testproc.execution import ExecutionProc
from testrunner.testproc.filter import StatusFileFilterProc, NameFilterProc
from testrunner.testproc.loader import LoadProc
from testrunner.testproc.seed import SeedProc
from testrunner.testproc.sequence import SequenceProc
from testrunner.testproc.variant import VariantProc


VARIANTS = ['default']

MORE_VARIANTS = [
  'stress',
  'stress_js_bg_compile_wasm_code_gc',
  'stress_incremental_marking',
  'future',
]

VARIANT_ALIASES = {
    # The default for developer workstations.
    'dev':
        VARIANTS,
    # Additional variants, run on all bots.
    'more':
        MORE_VARIANTS,
    # Shortcut for the two above ('more' first - it has the longer running tests)
    'exhaustive':
        MORE_VARIANTS + VARIANTS,
    # Additional variants, run on a subset of bots.
    'extra': [
        'jitless', 'nooptimization', 'no_wasm_traps', 'instruction_scheduling',
        'always_sparkplug_and_stress_regexp_jit', 'turboshaft'
    ],
}

# Extra flags passed to all tests using the standard test runner.
EXTRA_DEFAULT_FLAGS = ['--testing-d8-test-runner']

GC_STRESS_FLAGS = ['--gc-interval=500', '--stress-compaction',
                   '--concurrent-recompilation-queue-length=64',
                   '--concurrent-recompilation-delay=500',
                   '--concurrent-recompilation',
                   '--stress-flush-code', '--flush-bytecode',
                   '--wasm-code-gc', '--stress-wasm-code-gc']

RANDOM_GC_STRESS_FLAGS = ['--random-gc-interval=5000',
                          '--stress-compaction-random']

class StandardTestRunner(base_runner.BaseTestRunner):
  def __init__(self, *args, **kwargs):
    super(StandardTestRunner, self).__init__(*args, **kwargs)

    self.sancov_dir = None
    self._variants = None

  @property
  def framework_name(self):
    return 'standard_runner'

  def _get_default_suite_names(self):
    return ['default']

  def _add_parser_options(self, parser):
    parser.add_option('--novfp3',
                      help='Indicates that V8 was compiled without VFP3'
                      ' support',
                      default=False, action='store_true')

    # Variants
    parser.add_option('--no-variants', '--novariants',
                      help='Deprecated. '
                           'Equivalent to passing --variants=default',
                      default=False, dest='no_variants', action='store_true')
    parser.add_option(
        '--variants',
        help='Comma-separated list of testing variants;'
        ' default: "%s"' % ','.join(ALL_VARIANTS))
    parser.add_option('--exhaustive-variants',
                      default=False, action='store_true',
                      help='Deprecated. '
                           'Equivalent to passing --variants=exhaustive')

    # Filters
    parser.add_option('--slow-tests', default='dontcare',
                      help='Regard slow tests (run|skip|dontcare)')
    parser.add_option('--pass-fail-tests', default='dontcare',
                      help='Regard pass|fail tests (run|skip|dontcare)')
    parser.add_option('--quickcheck', default=False, action='store_true',
                      help=('Quick check mode (skip slow tests)'))

    # Stress modes
    parser.add_option('--gc-stress',
                      help='Switch on GC stress mode',
                      default=False, action='store_true')
    parser.add_option('--random-gc-stress',
                      help='Switch on random GC stress mode',
                      default=False, action='store_true')
    parser.add_option('--random-seed-stress-count', default=1, type='int',
                      dest='random_seed_stress_count',
                      help='Number of runs with different random seeds. Only '
                           'with test processors: 0 means infinite '
                           'generation.')

    # Extra features.
    parser.add_option('--max-heavy-tests', default=1, type='int',
                      help='Maximum number of heavy tests run in parallel')
    parser.add_option('--time', help='Print timing information after running',
                      default=False, action='store_true')

    # Noop
    parser.add_option('--cfi-vptr',
                      help='Run tests with UBSAN cfi_vptr option.',
                      default=False, action='store_true')
    parser.add_option('--no-sorting', '--nosorting',
                      help='Don\'t sort tests according to duration of last'
                      ' run.',
                      default=False, dest='no_sorting', action='store_true')
    parser.add_option('--no-presubmit', '--nopresubmit',
                      help='Skip presubmit checks (deprecated)',
                      default=False, dest='no_presubmit', action='store_true')

    # Unimplemented for test processors
    parser.add_option('--sancov-dir',
                      help='Directory where to collect coverage data')
    parser.add_option('--flakiness-results',
                      help='Path to a file for storing flakiness json.')

  def _predictable_wrapper(self):
    return self.v8_root / 'tools' / 'predictable_wrapper.py'

  def _process_options(self):
    if self.options.sancov_dir:
      self.sancov_dir = Path(self.options.sancov_dir)
      if not self.sancov_dir.exists():
        print('sancov-dir %s doesn\'t exist' % self.sancov_dir)
        raise base_runner.TestRunnerError()

    if self.options.gc_stress:
      self.options.extra_flags += GC_STRESS_FLAGS

    if self.options.random_gc_stress:
      self.options.extra_flags += RANDOM_GC_STRESS_FLAGS

    if self.build_config.asan:
      self.options.extra_d8_flags.append('--invoke-weak-callbacks')

    if self.options.novfp3:
      self.options.extra_flags.append('--noenable-vfp3')

    if self.options.no_variants:  # pragma: no cover
      print ('Option --no-variants is deprecated. '
             'Pass --variants=default instead.')
      assert not self.options.variants
      self.options.variants = 'default'

    if self.options.exhaustive_variants:  # pragma: no cover
      # TODO(machenbach): Switch infra to --variants=exhaustive after M65.
      print ('Option --exhaustive-variants is deprecated. '
             'Pass --variants=exhaustive instead.')
      # This is used on many bots. It includes a larger set of default
      # variants.
      # Other options for manipulating variants still apply afterwards.
      assert not self.options.variants
      self.options.variants = 'exhaustive'

    if self.options.quickcheck:
      assert not self.options.variants
      self.options.variants = 'stress,default'
      self.options.slow_tests = 'skip'
      self.options.pass_fail_tests = 'skip'

    if self.build_config.verify_predictable:
      self.options.variants = 'default'
      self.options.extra_flags.append('--predictable')
      self.options.extra_flags.append('--verify-predictable')
      self.options.extra_flags.append('--no-inline-new')
      self.options.extra_flags.append('--omit-quit')
      # Add predictable wrapper to command prefix.
      self.options.command_prefix = (
          [sys.executable, self._predictable_wrapper()] + self.options.command_prefix)

    # TODO(machenbach): Figure out how to test a bigger subset of variants on
    # msan.
    if self.build_config.msan:
      self.options.variants = 'default'

    if self.options.variants == 'infra_staging':
      self.options.variants = 'exhaustive'

    self._variants = self._parse_variants(self.options.variants)

    def CheckTestMode(name, option):  # pragma: no cover
      if option not in ['run', 'skip', 'dontcare']:
        print('Unknown %s mode %s' % (name, option))
        raise base_runner.TestRunnerError()
    CheckTestMode('slow test', self.options.slow_tests)
    CheckTestMode('pass|fail test', self.options.pass_fail_tests)
    if not self.build_config.i18n:
      base_runner.TEST_MAP['bot_default'].remove('intl')
      base_runner.TEST_MAP['default'].remove('intl')
      # TODO(machenbach): uncomment after infra side lands.
      # base_runner.TEST_MAP['d8_default'].remove('intl')

    if self.options.time and not self.options.json_test_results:
      # We retrieve the slowest tests from the JSON output file, so create
      # a temporary output file (which will automatically get deleted on exit)
      # if the user didn't specify one.
      self._temporary_json_output_file = tempfile.NamedTemporaryFile(
          prefix="v8-test-runner-")
      self.options.json_test_results = self._temporary_json_output_file.name

  def _runner_flags(self):
    return EXTRA_DEFAULT_FLAGS

  def _parse_variants(self, aliases_str):
    # Use developer defaults if no variant was specified.
    aliases_str = aliases_str or 'dev'
    aliases = aliases_str.split(',')
    user_variants = set(reduce(
        list.__add__, [VARIANT_ALIASES.get(a, [a]) for a in aliases]))

    result = [v for v in ALL_VARIANTS if v in user_variants]
    if len(result) == len(user_variants):
      return result

    for v in user_variants:
      if v not in ALL_VARIANTS:
        print('Unknown variant: %s' % v)
        print('    Available variants: %s' % ALL_VARIANTS)
        print('    Available variant aliases: %s' % VARIANT_ALIASES.keys());
        raise base_runner.TestRunnerError()
    assert False, 'Unreachable' # pragma: no cover

  def _setup_env(self):
    super(StandardTestRunner, self)._setup_env()

    symbolizer_option = self._get_external_symbolizer_option()

    if self.sancov_dir:
      os.environ['ASAN_OPTIONS'] = ':'.join([
        'coverage=1',
        'coverage_dir=%s' % self.sancov_dir,
        symbolizer_option,
        'allow_user_segv_handler=1',
      ])

  def _get_statusfile_variables(self):
    variables = super(StandardTestRunner, self)._get_statusfile_variables()
    variables.update({
      'gc_stress': self.options.gc_stress or self.options.random_gc_stress,
      'gc_fuzzer': self.options.random_gc_stress,
      'novfp3': self.options.novfp3,
    })
    return variables

  def _create_sequence_proc(self):
    """Create processor for sequencing heavy tests on swarming."""
    return SequenceProc(self.options.max_heavy_tests) if self.options.swarming else None

  def _do_execute(self, tests, args, ctx):
    jobs = self.options.j

    print('>>> Running with test processors')
    loader = LoadProc(tests, initial_batch_size=self.options.j * 2)
    results = ResultsTracker.create(self.options)
    outproc_factory = None
    if self.build_config.verify_predictable:
      outproc_factory = predictable.get_outproc
    execproc = ExecutionProc(ctx, jobs, outproc_factory)
    sigproc = self._create_signal_proc()
    progress = ProgressProc(ctx, self.options, tests.test_count_estimate)
    procs = [
        loader,
        NameFilterProc(args) if args else None,
        VariantProc(self._variants),
        StatusFileFilterProc(self.options.slow_tests,
                             self.options.pass_fail_tests),
        self._create_predictable_filter(),
        ShardProc.create(self.options),
        self._create_seed_proc(),
        self._create_sequence_proc(),
        sigproc,
        progress,
        results,
        TimeoutProc.create(self.options),
        RerunProc.create(self.options),
        execproc,
    ]
    procs = [p for p in procs if p]

    self._prepare_procs(procs)
    loader.load_initial_tests()

    # This starts up worker processes and blocks until all tests are
    # processed.
    requirement = max(p._requirement for p in procs)
    execproc.run(requirement)

    progress.finished()

    results.standard_show(tests)


    if self.options.time:
      self._print_durations()

    return sigproc.worst_exit_code(results)

  def _print_durations(self):

    def format_duration(duration_in_seconds):
      duration = datetime.timedelta(seconds=duration_in_seconds)
      time = (datetime.datetime.min + duration).time()
      return time.strftime('%M:%S:') + '%03i' % int(time.microsecond / 1000)

    def _duration_results_text(test):
      return [
        'Test: %s' % test['name'],
        'Flags: %s' % ' '.join(test['flags']),
        'Command: %s' % test['command'],
        'Duration: %s' % format_duration(test['duration']),
      ]

    assert Path(self.options.json_test_results).exists()
    with open(self.options.json_test_results, "r") as f:
      output = json.load(f)
    lines = []
    for test in output['slowest_tests']:
      suffix = ''
      if test.get('marked_slow') is False:
        suffix = ' *'
      lines.append(
          '%s %s%s' % (format_duration(test['duration']),
                       test['name'], suffix))

    # Slowest tests duration details.
    lines.extend(['', 'Details:', ''])
    for test in output['slowest_tests']:
      lines.extend(_duration_results_text(test))
    print("\n".join(lines))

  def _create_predictable_filter(self):
    if not self.build_config.verify_predictable:
      return None
    return predictable.PredictableFilterProc()

  def _create_seed_proc(self):
    if self.options.random_seed_stress_count == 1:
      return None
    return SeedProc(self.options.random_seed_stress_count, self.options.random_seed,
                    self.options.j * 4)


if __name__ == '__main__':
  sys.exit(StandardTestRunner().execute()) # pragma: no cover
