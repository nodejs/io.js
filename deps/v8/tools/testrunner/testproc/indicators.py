# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import datetime
import json
import logging
import platform
import sys
import time

from . import base
from . import util
from .stack_utils import stack_analyzer_util


TRACK_N_HEAVY_TESTS = 20


def print_failure_header(test, is_flaky=False):
  text = [test.full_name]
  if test.output_proc.negative:
    text.append('[negative]')
  if is_flaky:
    text.append('(flaky)')
  output = '=== %s ===' % ' '.join(text)
  encoding = sys.stdout.encoding or 'utf-8'
  print(output.encode(encoding, errors='replace').decode(encoding))


def formatted_result_output(result, relative=False):
  lines = []
  if result.output.stderr:
    lines.append("--- stderr ---")
    lines.append(result.output.stderr.strip())
  if result.output.stdout:
    lines.append("--- stdout ---")
    lines.append(result.output.stdout.strip())
  lines.append("Command: %s" % result.cmd.to_string(relative))
  if result.output.HasCrashed():
    lines.append("exit code: %s" % result.output.exit_code_string)
    lines.append("--- CRASHED ---")
  if result.output.HasTimedOut():
    lines.append("--- TIMEOUT ---")
  return '\n'.join(lines)


class ProgressIndicator():

  def __init__(self, context, options, test_count):
    self.options = None
    self.options = options
    self._total = test_count
    self.context = context

  def on_test_result(self, test, result):
    pass

  def finished(self):
    pass

  def on_heartbeat(self):
    pass

  def on_event(self, event):
    pass


class SimpleProgressIndicator(ProgressIndicator):

  def __init__(self, context, options, test_count):
    super(SimpleProgressIndicator, self).__init__(context, options, test_count)
    self._requirement = base.DROP_PASS_OUTPUT

    self._failed = []

  def on_test_result(self, test, result):
    # TODO(majeski): Support for dummy/grouped results
    if result.has_unexpected_output:
      self._failed.append((test, result, False))
    elif result.is_rerun:
      # Print only the first result of a flaky failure that was rerun.
      self._failed.append((test, result.results[0], True))

  def finished(self):
    crashed = 0
    flaky = 0
    print()
    for test, result, is_flaky in self._failed:
      flaky += int(is_flaky)
      print_failure_header(test, is_flaky=is_flaky)
      print(formatted_result_output(result))
    if len(self._failed) == 0:
      print("===")
      print("=== All tests succeeded")
      print("===")
    else:
      print()
      print("===")
      print("=== %d tests failed" % len(self._failed))
      if flaky > 0:
        print("=== %d tests were flaky" % flaky)
      if crashed > 0:
        print("=== %d tests CRASHED" % crashed)
      print("===")


class StreamProgressIndicator(ProgressIndicator):

  def __init__(self, context, options, test_count):
    super(StreamProgressIndicator, self).__init__(context, options, test_count)
    self._requirement = base.DROP_PASS_OUTPUT

  def on_test_result(self, test, result):
    if not result.has_unexpected_output:
      self.print('PASS', test)
    elif result.output.HasCrashed():
      self.print("CRASH", test)
    elif result.output.HasTimedOut():
      self.print("TIMEOUT", test)
    else:
      if test.is_fail:
        self.print("UNEXPECTED PASS", test)
      else:
        self.print("FAIL", test)

  def print(self, prefix, test):
    print('%s: %ss' % (prefix, test))
    sys.stdout.flush()


class VerboseProgressIndicator(SimpleProgressIndicator):

  def __init__(self, context, options, test_count):
    super(VerboseProgressIndicator, self).__init__(context, options, test_count)
    self._last_printed_time = time.time()

  def _print(self, text):
    encoding = sys.stdout.encoding or 'utf-8'
    print(text.encode(encoding, errors='replace').decode(encoding))
    sys.stdout.flush()
    self._last_printed_time = time.time()

  def _message(self, test, result):
    return '%s %s: %s' % (test, test.variant or 'default', result.status())

  def on_test_result(self, test, result):
    super(VerboseProgressIndicator, self).on_test_result(test, result)
    self._print(self._message(test, result))

  # TODO(machenbach): Remove this platform specific hack and implement a proper
  # feedback channel from the workers, providing which tests are currently run.
  def _log_processes(self):
    procs = self.context.list_processes()
    if procs:
      logging.info('List of processes:')
      for pid, cmd in self.context.list_processes():
        # Show command with pid, but other process info cut off.
        logging.info('pid: %d cmd: %s', pid, cmd)

  def _ensure_delay(self, delay):
    return time.time() - self._last_printed_time > delay

  def on_heartbeat(self):
    if self._ensure_delay(30):
      # Print something every 30 seconds to not get killed by an output
      # timeout.
      self._print('Still working...')
      self._log_processes()

  def on_event(self, event):
    logging.info(event)
    self._log_processes()


class CIProgressIndicator(VerboseProgressIndicator):

  def on_test_result(self, context, test, result):
    super(VerboseProgressIndicator, self).on_test_result(context, test, result)
    if self.options.ci_test_completion:
      with open(self.options.ci_test_completion, "a") as f:
        f.write(self._message(test, result) + "\n")
    self._output_feedback()

  def _output_feedback(self):
    """Reduced the verbosity leads to getting killed by an ouput timeout.
    We ensure output every minute.
    """
    if self._ensure_delay(60):
      dt = time.time()
      st = datetime.datetime.fromtimestamp(dt).strftime('%Y-%m-%d %H:%M:%S')
      self._print(st)


class DotsProgressIndicator(SimpleProgressIndicator):

  def __init__(self, context, options, test_count):
    super(DotsProgressIndicator, self).__init__(context, options, test_count)
    self._count = 0

  def on_test_result(self, test, result):
    super(DotsProgressIndicator, self).on_test_result(test, result)
    # TODO(majeski): Support for dummy/grouped results
    self._count += 1
    if self._count > 1 and self._count % 50 == 1:
      sys.stdout.write('\n')
    if result.has_unexpected_output:
      if result.output.HasCrashed():
        sys.stdout.write('C')
        sys.stdout.flush()
      elif result.output.HasTimedOut():
        sys.stdout.write('T')
        sys.stdout.flush()
      else:
        sys.stdout.write('F')
        sys.stdout.flush()
    else:
      sys.stdout.write('.')
      sys.stdout.flush()


class CompactProgressIndicator(ProgressIndicator):

  def __init__(self, context, options, test_count, templates):
    super(CompactProgressIndicator, self).__init__(context, options, test_count)
    self._requirement = base.DROP_PASS_OUTPUT

    self._templates = templates
    self._last_status_length = 0
    self._start_time = time.time()

    self._passed = 0
    self._failed = 0

  def on_test_result(self, test, result):
    # TODO(majeski): Support for dummy/grouped results
    if result.has_unexpected_output:
      self._failed += 1
    else:
      self._passed += 1

    self._print_progress(test.full_name)
    if result.has_unexpected_output:
      output = result.output
      stdout = output.stdout.strip()
      stderr = output.stderr.strip()

      self._clear_line(self._last_status_length)
      print_failure_header(test)
      if len(stdout):
        self.printFormatted('stdout', stdout)
      if len(stderr):
        self.printFormatted('stderr', stderr)
      if result.error_details:
        self.printFormatted('failure', result.error_details)
      self.printFormatted('command',
                          "Command: %s" % result.cmd.to_string(relative=True))
      if output.HasCrashed():
        self.printFormatted('failure',
                            "exit code: %s" % output.exit_code_string)
        self.printFormatted('failure', "--- CRASHED ---")
      elif output.HasTimedOut():
        self.printFormatted('failure', "--- TIMEOUT ---")
      else:
        if test.is_fail:
          self.printFormatted('failure', "--- UNEXPECTED PASS ---")
          if test.expected_failure_reason is not None:
            self.printFormatted('failure', test.expected_failure_reason)
        else:
          self.printFormatted('failure', "--- FAILED ---")

  def finished(self):
    self._print_progress('Done')
    print()

  def _print_progress(self, name):
    self._clear_line(self._last_status_length)
    elapsed = time.time() - self._start_time
    if self._total:
      progress = (self._passed + self._failed) * 100 // self._total
    else:
      progress = 0
    status = self._templates['status_line'] % {
        'passed': self._passed,
        'progress': progress,
        'failed': self._failed,
        'test': name,
        'mins': int(elapsed) // 60,
        'secs': int(elapsed) % 60
    }
    status = self._truncateStatusLine(status, 78)
    self._last_status_length = len(status)
    print(status, end='')
    sys.stdout.flush()

  def _truncateStatusLine(self, string, length):
    if length and len(string) > (length - 3):
      return string[:(length - 3)] + "..."
    else:
      return string

  def _clear_line(self, last_length):
    raise NotImplementedError()


class ColorProgressIndicator(CompactProgressIndicator):

  def __init__(self, context, options, test_count):
    templates = {
        'status_line': ("[%(mins)02i:%(secs)02i|"
                        "\033[34m%%%(progress) 4d\033[0m|"
                        "\033[32m+%(passed) 4d\033[0m|"
                        "\033[31m-%(failed) 4d\033[0m]: %(test)s"),
        'stdout': "\033[1m%s\033[0m",
        'stderr': "\033[31m%s\033[0m",
        'failure': "\033[1;31m%s\033[0m",
        'command': "\033[33m%s\033[0m",
    }
    super(ColorProgressIndicator, self).__init__(context, options, test_count,
                                                 templates)

  def printFormatted(self, format, string):
    print(self._templates[format] % string)

  def _truncateStatusLine(self, string, length):
    # Add some slack for the color control chars
    return super(ColorProgressIndicator,
                 self)._truncateStatusLine(string, length + 3 * 9)

  def _clear_line(self, last_length):
    print("\033[1K\r", end='')


class MonochromeProgressIndicator(CompactProgressIndicator):

  def __init__(self, context, options, test_count):
    templates = {
        'status_line': ("[%(mins)02i:%(secs)02i|%%%(progress) 4d|"
                        "+%(passed) 4d|-%(failed) 4d]: %(test)s"),
    }
    super(MonochromeProgressIndicator, self).__init__(context, options,
                                                      test_count, templates)

  def printFormatted(self, format, string):
    print(string)

  def _clear_line(self, last_length):
    print(("\r" + (" " * last_length) + "\r"), end='')


class JsonTestProgressIndicator(ProgressIndicator):

  def __init__(self, context, options, test_count):
    super(JsonTestProgressIndicator, self).__init__(context, options,
                                                    test_count)
    self.slowest_tests = util.FixedSizeTopList(
        self.options.slow_tests_cutoff, key=lambda rec: rec['duration'])
    self.max_rss_tests = util.FixedSizeTopList(
        TRACK_N_HEAVY_TESTS, key=lambda rec: rec['max_rss'])
    self.max_vms_tests = util.FixedSizeTopList(
        TRACK_N_HEAVY_TESTS, key=lambda rec: rec['max_vms'])

    # We want to drop stdout/err for all passed tests on the first try, but we
    # need to get outputs for all runs after the first one. To accommodate that,
    # reruns are set to keep the result no matter what requirement says, i.e.
    # keep_output set to True in the RerunProc.
    self._requirement = base.DROP_PASS_STDOUT

    self.results = []
    self.duration_sum = 0
    self.test_count = 0
    self.stack_parser = stack_analyzer_util.create_stack_parser()

  def on_test_result(self, test, result):
    self.process_results(test, result.as_list)

  def process_results(self, test, results):
    for run, result in enumerate(results):
      # TODO(majeski): Support for dummy/grouped results
      output = result.output

      self._buffer_top_tests(test, result, output, run)

      # Omit tests that run as expected on the first try.
      # Everything that happens after the first run is included in the output
      # even if it flakily passes.
      if not result.has_unexpected_output and run == 0:
        continue

      record = self._test_record(test, result, run)
      record.update({
          "result": test.output_proc.get_outcome(output),
          "stdout": output.stdout,
          "stderr": output.stderr,
          "error_details": result.error_details,
      })

      record.update(self.stack_parser.analyze_crash(output.stderr))

      self.results.append(record)

  def _buffer_top_tests(self, test, result, output, run):

    def result_value(test, result, output):
      if not result.has_unexpected_output:
        return ""
      return test.output_proc.get_outcome(output)

    record = self._test_record(test, result, run)
    record.update(
        result=result_value(test, result, output),
        marked_slow=test.is_slow,
        marked_heavy=test.is_heavy,
    )
    self.slowest_tests.add(record)
    self.max_rss_tests.add(record)
    self.max_vms_tests.add(record)
    self.duration_sum += record['duration']
    self.test_count += 1

  def _test_record(self, test, result, run):
    record = util.base_test_record(test, result, run)
    record.update(
        command=result.cmd.to_string(relative=True),
    )
    return record

  def finished(self):
    duration_mean = None
    if self.test_count:
      duration_mean = self.duration_sum / self.test_count

    result = {
        'results': self.results,
        'max_rss_tests': self.max_rss_tests.as_list(),
        'max_vms_tests': self.max_vms_tests.as_list(),
        'slowest_tests': self.slowest_tests.as_list(),
        'duration_mean': duration_mean,
        'test_total': self.test_count,
    }

    with open(self.options.json_test_results, "w") as f:
      json.dump(result, f)


class TestScheduleIndicator(ProgressIndicator):
  """Indicator the logs the start:end interval as timestamps for each
  executed test. Reruns and variants are accounted for separately.
  """

  def __init__(self, context, options, test_count):
    super(TestScheduleIndicator, self).__init__(
        context, options, test_count)
    self._requirement = base.DROP_PASS_STDOUT
    self.handle = open(self.options.log_test_schedule, 'w')

  def on_test_result(self, test, result):
    encoding = sys.stdout.encoding or 'utf-8'
    for r in result.as_list:
      # TODO(v8-infra): Ideally the full_name should already be encoded
      # correctly. This takes care of one test with unicode characters in
      # the file name.
      name = test.full_name.encode(encoding, errors='replace').decode(encoding)
      print(f'{name} {test.variant or "default"}: {r.status()} '
            f'({r.output.start_time}:{r.output.end_time})',
            file=self.handle)

  def finished(self):
    self.handle.close()


PROGRESS_INDICATORS = {
    'verbose': VerboseProgressIndicator,
    'ci': CIProgressIndicator,
    'dots': DotsProgressIndicator,
    'color': ColorProgressIndicator,
    'mono': MonochromeProgressIndicator,
    'stream': StreamProgressIndicator,
}
