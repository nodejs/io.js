#!/usr/bin/env python3
#
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

# Adds testrunner to the path hence it has to be imported at the beggining.
from testrunner import base_runner

from testrunner.local import utils

from testrunner.testproc import fuzzer
from testrunner.testproc.combiner import CombinerProc
from testrunner.testproc.execution import ExecutionProc
from testrunner.testproc.expectation import ExpectationProc
from testrunner.testproc.filter import StatusFileFilterProc, NameFilterProc
from testrunner.testproc.loader import LoadProc
from testrunner.utils import random_utils
from testrunner.testproc.rerun import RerunProc
from testrunner.testproc.timeout import TimeoutProc
from testrunner.testproc.progress import ResultsTracker, ProgressProc
from testrunner.testproc.shard import ShardProc


DEFAULT_SUITES = ["mjsunit", "webkit", "benchmarks"]


class NumFuzzer(base_runner.BaseTestRunner):
  def __init__(self, *args, **kwargs):
    super(NumFuzzer, self).__init__(*args, **kwargs)

  @property
  def default_framework_name(self):
    return 'num_fuzzer'

  def _add_parser_options(self, parser):
    parser.add_option("--fuzzer-random-seed", default=0,
                      help="Default seed for initializing fuzzer random "
                      "generator")
    parser.add_option("--tests-count", default=5, type="int",
                      help="Number of tests to generate from each base test. "
                           "Can be combined with --total-timeout-sec with "
                           "value 0 to provide infinite number of subtests. "
                           "When --combine-tests is set it indicates how many "
                           "tests to create in total")

    # Stress gc
    parser.add_option("--stress-marking", default=0, type="int",
                      help="probability [0-10] of adding --stress-marking "
                           "flag to the test")
    parser.add_option("--stress-scavenge", default=0, type="int",
                      help="probability [0-10] of adding --stress-scavenge "
                           "flag to the test")
    parser.add_option("--stress-compaction", default=0, type="int",
                      help="probability [0-10] of adding --stress-compaction "
                           "flag to the test")
    parser.add_option("--stress-gc", default=0, type="int",
                      help="probability [0-10] of adding --random-gc-interval "
                           "flag to the test")

    # Stress stack size
    parser.add_option("--stress-stack-size", default=0, type="int",
                      help="probability [0-10] of adding --stack-size "
                           "flag to the test")

    # Stress tasks
    parser.add_option("--stress-delay-tasks", default=0, type="int",
                      help="probability [0-10] of adding --stress-delay-tasks "
                           "flag to the test")
    parser.add_option("--stress-thread-pool-size", default=0, type="int",
                      help="probability [0-10] of adding --thread-pool-size "
                           "flag to the test")

    # Stress compiler
    parser.add_option("--stress-deopt", default=0, type="int",
                      help="probability [0-10] of adding --deopt-every-n-times "
                           "flag to the test")
    parser.add_option("--stress-interrupt-budget", default=0, type="int",
                      help="probability [0-10] of adding the --interrupt-budget "
                           "flag to the test")

    # Combine multiple tests
    parser.add_option("--combine-tests", default=False, action="store_true",
                      help="Combine multiple tests as one and run with "
                           "try-catch wrapper")
    parser.add_option("--combine-max", default=100, type="int",
                      help="Maximum number of tests to combine")
    parser.add_option("--combine-min", default=2, type="int",
                      help="Minimum number of tests to combine")

    # Miscellaneous
    parser.add_option("--variants", default='default',
                      help="Comma-separated list of testing variants")

    return parser


  def _process_options(self):
    if not self.options.fuzzer_random_seed:
      self.options.fuzzer_random_seed = random_utils.random_seed()

    if self.options.total_timeout_sec:
      self.options.tests_count = 0

    if self.options.combine_tests:
      if self.options.combine_min > self.options.combine_max:
        print(('min_group_size (%d) cannot be larger than max_group_size (%d)' %
               self.options.min_group_size, self.options.max_group_size))
        raise base_runner.TestRunnerError()

    if self.options.variants != 'default':
      print ('Only default testing variant is supported with numfuzz')
      raise base_runner.TestRunnerError()

    return True

  def _get_default_suite_names(self):
    return DEFAULT_SUITES

  def _get_statusfile_variables(self, context):
    variables = (
        super(NumFuzzer, self)._get_statusfile_variables(context))
    variables.update({
        'deopt_fuzzer':
            bool(self.options.stress_deopt),
        'interrupt_fuzzer':
            bool(self.options.stress_interrupt_budget),
        'endurance_fuzzer':
            bool(self.options.combine_tests),
        'gc_stress':
            bool(self.options.stress_gc),
        'gc_fuzzer':
            bool(
                max([
                    self.options.stress_marking, self.options.stress_scavenge,
                    self.options.stress_compaction, self.options.stress_gc,
                    self.options.stress_delay_tasks,
                    self.options.stress_stack_size,
                    self.options.stress_thread_pool_size
                ])),
    })
    return variables

  def _do_execute(self, tests, args, ctx):
    loader = LoadProc(tests)
    combiner = CombinerProc.create(self.options)
    results = ResultsTracker.create(self.options)
    execproc = ExecutionProc(ctx, self.options.j)
    sigproc = self._create_signal_proc()
    progress = ProgressProc(ctx, self.options, tests.test_count_estimate)
    procs = [
        loader,
        NameFilterProc(args) if args else None,
        StatusFileFilterProc(None, None),
        # TODO(majeski): Improve sharding when combiner is present. Maybe select
        # different random seeds for shards instead of splitting tests.
        ShardProc.create(self.options),
        ExpectationProc(),
        combiner,
        fuzzer.FuzzerProc.create(self.options),
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

    # TODO(majeski): maybe some notification from loader would be better?
    if combiner:
      combiner.generate_initial_tests(self.options.j * 4)

    # This starts up worker processes and blocks until all tests are
    # processed.
    execproc.run()

    progress.finished()

    print('>>> %d tests ran' % results.total)
    if results.failed:
      return utils.EXIT_CODE_FAILURES

    # Indicate if a SIGINT or SIGTERM happened.
    return sigproc.exit_code

  def _is_testsuite_supported(self, suite):
    return not self.options.combine_tests or suite.test_combiner_available()


if __name__ == '__main__':
  sys.exit(NumFuzzer().execute()) # pragma: no cover
