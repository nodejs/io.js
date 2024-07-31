# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from . import base
from testrunner.local import utils
from testrunner.testproc.indicators import (
    JsonTestProgressIndicator, TestScheduleIndicator, PROGRESS_INDICATORS)
from testrunner.testproc.resultdb import rdb_sink, ResultDBIndicator


class ResultsTracker(base.TestProcObserver):
  @staticmethod
  def create(options):
    return ResultsTracker(options.exit_after_n_failures)

  """Tracks number of results and stops to run tests if max_failures reached."""
  def __init__(self, max_failures):
    super(ResultsTracker, self).__init__()
    self._requirement = base.DROP_OUTPUT

    self.failed = 0
    self.remaining = 0
    self.total = 0
    self.max_failures = max_failures

  def _on_next_test(self, test):
    self.total += 1
    self.remaining += 1

  def _on_result_for(self, test, result):
    self.remaining -= 1
    # Count failures - treat flakes as failures, too.
    if result.has_unexpected_output or result.is_rerun:
      self.failed += 1
      if self.max_failures and self.failed >= self.max_failures:
        print('>>> Too many failures, exiting...')
        self.stop()

  def standard_show(self, tests):
    if tests.test_count_estimate:
      percentage = float(self.total) / tests.test_count_estimate * 100
    else:
      percentage = 0
    print(('>>> %d base tests produced %d (%d%s)'
           ' non-filtered tests') %
          (tests.test_count_estimate, self.total, percentage, '%'))
    print('>>> %d tests ran' % (self.total - self.remaining))

  def exit_code(self):
    exit_code = utils.EXIT_CODE_PASS
    if self.failed:
      exit_code = utils.EXIT_CODE_FAILURES
    if not self.total:
      exit_code = utils.EXIT_CODE_NO_TESTS
    return exit_code


class ProgressProc(base.TestProcObserver):

  def __init__(self, context, options, test_count):
    super(ProgressProc, self).__init__()
    self.procs = [
        PROGRESS_INDICATORS[options.progress](context, options, test_count)
    ]
    if options.log_test_schedule:
      self.procs.insert(
          0,
          TestScheduleIndicator(context, options, test_count))
    if options.json_test_results:
      self.procs.insert(
          0,
          JsonTestProgressIndicator(context, options, test_count))
    sink = rdb_sink()
    if sink:
      self.procs.append(ResultDBIndicator(context, options, test_count, sink))
    self._requirement = max(proc._requirement for proc in self.procs)

  def _on_result_for(self, test, result):
    for proc in self.procs:
      proc.on_test_result(test, result)

  def finished(self):
    for proc in self.procs:
      proc.finished()

  def _on_heartbeat(self):
    for proc in self.procs:
      proc.on_heartbeat()

  def _on_event(self, event):
    for proc in self.procs:
      proc.on_event(event)
