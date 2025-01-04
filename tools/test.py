#!/usr/bin/env python3
#
# Copyright 2008 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


from __future__ import print_function
from typing import Dict
import logging
import optparse
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
import threading
import utils
import multiprocessing
import errno
import copy
import io

if sys.version_info >= (3, 5):
  from importlib import machinery, util
  def get_module(name, path):
    loader_details = (machinery.SourceFileLoader, machinery.SOURCE_SUFFIXES)
    spec = machinery.FileFinder(path, loader_details).find_spec(name)
    module = util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
else:
  import imp
  def get_module(name, path):
    file = None
    try:
      (file, pathname, description) = imp.find_module(name, [path])
      return imp.load_module(name, file, pathname, description)
    finally:
      if file:
        file.close()


from io import open
from os.path import join, dirname, abspath, basename, isdir, exists
from datetime import datetime, timedelta
try:
    from queue import Queue, Empty  # Python 3
except ImportError:
    from Queue import Queue, Empty  # Python 2

from functools import reduce

try:
  from urllib.parse import unquote    # Python 3
except ImportError:
  from urllib import unquote          # Python 2


logger = logging.getLogger('testrunner')
skip_regex = re.compile(r'(?:\d+\.\.\d+|ok|not ok).*# SKIP\S*\s+(.*)', re.IGNORECASE)

VERBOSE = False

os.umask(0o022)
os.environ.pop('NODE_OPTIONS', None)

# ---------------------------------------------
# --- P r o g r e s s   I n d i c a t o r s ---
# ---------------------------------------------


class ProgressIndicator(object):

  def __init__(self, cases, flaky_tests_mode, measure_flakiness):
    self.cases = cases
    self.serial_id = 0
    self.flaky_tests_mode = flaky_tests_mode
    self.measure_flakiness = measure_flakiness
    self.parallel_queue = Queue(len(cases))
    self.sequential_queue = Queue(len(cases))
    for case in cases:
      if case.parallel:
        self.parallel_queue.put_nowait(case)
      else:
        self.sequential_queue.put_nowait(case)
    self.succeeded = 0
    self.remaining = len(cases)
    self.total = len(cases)
    self.failed = [ ]
    self.flaky_failed = [ ]
    self.crashed = 0
    self.lock = threading.Lock()
    self.shutdown_event = threading.Event()

  def GetFailureOutput(self, failure):
    output = []
    if failure.output.stderr:
      output += ["--- stderr ---" ]
      output += [failure.output.stderr.strip()]
    if failure.output.stdout:
      output += ["--- stdout ---"]
      output += [failure.output.stdout.strip()]
    output += ["Command: %s" % EscapeCommand(failure.command)]
    if failure.HasCrashed():
      output += ["--- %s ---" % PrintCrashed(failure.output.exit_code)]
    if failure.HasTimedOut():
      output += ["--- TIMEOUT ---"]
    output = "\n".join(output)
    return output

  def PrintFailureOutput(self, failure):
    print(self.GetFailureOutput(failure))

  def PrintFailureHeader(self, test):
    if test.IsNegative():
      negative_marker = '[negative] '
    else:
      negative_marker = ''
    print("=== %(label)s %(negative)s===" % {
      'label': test.GetLabel(),
      'negative': negative_marker
    })
    print("Path: %s" % "/".join(test.path))

  def Run(self, tasks) -> Dict:
    self.Starting()
    threads = []
    # Spawn N-1 threads and then use this thread as the last one.
    # That way -j1 avoids threading altogether which is a nice fallback
    # in case of threading problems.
    for i in range(tasks - 1):
      thread = threading.Thread(target=self.RunSingle, args=[True, i + 1])
      threads.append(thread)
      thread.start()
    try:
      self.RunSingle(False, 0)
      # Wait for the remaining threads
      for thread in threads:
        # Use a timeout so that signals (ctrl-c) will be processed.
        thread.join(timeout=1000000)
    except (KeyboardInterrupt, SystemExit):
      self.shutdown_event.set()
    except Exception:
      # If there's an exception we schedule an interruption for any
      # remaining threads.
      self.shutdown_event.set()
      # ...and then reraise the exception to bail out
      raise
    self.Done()
    return {
      'allPassed': not self.failed,
      'failed': self.failed,
    }

  def RunSingle(self, parallel, thread_id):
    while not self.shutdown_event.is_set():
      try:
        test = self.parallel_queue.get_nowait()
      except Empty:
        if parallel:
          return
        try:
          test = self.sequential_queue.get_nowait()
        except Empty:
          return
      case = test
      case.thread_id = thread_id
      self.lock.acquire()
      case.serial_id = self.serial_id
      self.serial_id += 1
      self.AboutToRun(case)
      self.lock.release()
      try:
        start = datetime.now()
        output = case.Run()
        # SmartOS has a bug that causes unexpected ECONNREFUSED errors.
        # See https://smartos.org/bugview/OS-2767
        # If ECONNREFUSED on SmartOS, retry the test one time.
        if (output.UnexpectedOutput() and
          sys.platform == 'sunos5' and
          'ECONNREFUSED' in output.output.stderr):
            output = case.Run()
            output.diagnostic.append('ECONNREFUSED received, test retried')
        case.duration = (datetime.now() - start)
      except IOError:
        return
      if self.shutdown_event.is_set():
        return
      self.lock.acquire()
      if output.UnexpectedOutput():
        if FLAKY in output.test.outcomes and self.flaky_tests_mode == DONTCARE:
          self.flaky_failed.append(output)
        elif FLAKY in output.test.outcomes and self.flaky_tests_mode == KEEP_RETRYING:
          for _ in range(99):
            if not case.Run().UnexpectedOutput():
              self.flaky_failed.append(output)
              break
          else:
            # If after 100 tries, the test is not passing, it's not flaky.
            self.failed.append(output)
        else:
          self.failed.append(output)
          if output.HasCrashed():
            self.crashed += 1
          if self.measure_flakiness:
            outputs = [case.Run() for _ in range(self.measure_flakiness)]
            # +1s are there because the test already failed once at this point.
            print(" failed %d out of %d" % (len([i for i in outputs if i.UnexpectedOutput()]) + 1, self.measure_flakiness + 1))
      else:
        self.succeeded += 1
      self.remaining -= 1
      self.HasRun(output)
      self.lock.release()


def EscapeCommand(command):
  parts = []
  for part in command:
    if ' ' in part:
      # Escape spaces.  We may need to escape more characters for this
      # to work properly.
      parts.append('"%s"' % part)
    else:
      parts.append(part)
  return " ".join(parts)


class SimpleProgressIndicator(ProgressIndicator):

  def Starting(self):
    print('Running %i tests' % len(self.cases))

  def Done(self):
    print()
    for failed in self.failed:
      self.PrintFailureHeader(failed.test)
      self.PrintFailureOutput(failed)
    if len(self.failed) == 0:
      print("===")
      print("=== All tests succeeded")
      print("===")
    else:
      print()
      print("===")
      print("=== %i tests failed" % len(self.failed))
      if self.crashed > 0:
        print("=== %i tests CRASHED" % self.crashed)
      print("===")


class VerboseProgressIndicator(SimpleProgressIndicator):

  def AboutToRun(self, case):
    print('Starting %s...' % case.GetLabel())
    sys.stdout.flush()

  def HasRun(self, output):
    if output.UnexpectedOutput():
      if output.HasCrashed():
        outcome = 'CRASH'
      else:
        outcome = 'FAIL'
    else:
      outcome = 'pass'
    print('Done running %s: %s' % (output.test.GetLabel(), outcome))


class DotsProgressIndicator(SimpleProgressIndicator):

  def AboutToRun(self, case):
    pass

  def HasRun(self, output):
    total = self.succeeded + len(self.failed)
    if (total > 1) and (total % 50 == 1):
      sys.stdout.write('\n')
    if output.UnexpectedOutput():
      if output.HasCrashed():
        sys.stdout.write('C')
        sys.stdout.flush()
      elif output.HasTimedOut():
        sys.stdout.write('T')
        sys.stdout.flush()
      else:
        sys.stdout.write('F')
        sys.stdout.flush()
    else:
      sys.stdout.write('.')
      sys.stdout.flush()

class ActionsAnnotationProgressIndicator(DotsProgressIndicator):
  def AboutToRun(self, case):
    case.additional_flags = case.additional_flags.copy() if hasattr(case, 'additional_flags') else []
    case.additional_flags.append('--test-reporter=./test/common/test-error-reporter.js')
    case.additional_flags.append('--test-reporter-destination=stdout')

  def GetAnnotationInfo(self, test, output):
    traceback = output.stdout + output.stderr
    find_full_path = re.search(r' +at .*\(.*%s:([0-9]+):([0-9]+)' % test.file, traceback)
    col = line = 0
    if find_full_path:
        line, col = map(int, find_full_path.groups())
    root_path = abspath(join(dirname(__file__), '../')) + os.sep
    filename = test.file.replace(root_path, "")
    return filename, line, col

  def PrintFailureOutput(self, failure):
    output = self.GetFailureOutput(failure)
    filename, line, column = self.GetAnnotationInfo(failure.test, failure.output)
    print("::error file=%s,line=%d,col=%d::%s" % (filename, line, column, output.replace('\n', '%0A')))

class TapProgressIndicator(SimpleProgressIndicator):

  def _printDiagnostic(self):
    logger.info('  severity: %s', self.severity)
    self.exitcode and logger.info('  exitcode: %s', self.exitcode)
    logger.info('  stack: |-')

    for l in self.traceback.splitlines():
      logger.info('    ' + l)

  def Starting(self):
    logger.info('TAP version 13')
    logger.info('1..%i' % len(self.cases))
    self._done = 0

  def AboutToRun(self, case):
    pass

  def HasRun(self, output):
    self._done += 1
    self.traceback = ''
    self.severity = 'ok'
    self.exitcode = ''

    # Print test name as (for example) "parallel/test-assert".  Tests that are
    # scraped from the addons documentation are all named test.js, making it
    # hard to decipher what test is running when only the filename is printed.
    prefix = abspath(join(dirname(__file__), '../test')) + os.sep
    command = output.command[-1]
    command = NormalizePath(command, prefix)

    if output.UnexpectedOutput():
      status_line = 'not ok %i %s' % (self._done, command)
      self.severity = 'fail'
      self.exitcode = output.output.exit_code
      self.traceback = output.output.stdout + output.output.stderr

      if FLAKY in output.test.outcomes and self.flaky_tests_mode == DONTCARE:
        status_line = status_line + ' # TODO : Fix flaky test'
        self.severity = 'flaky'

      logger.info(status_line)

      if output.HasCrashed():
        self.severity = 'crashed'

      elif output.HasTimedOut():
        self.severity = 'fail'

    else:
      skip = skip_regex.search(output.output.stdout)
      if skip:
        logger.info(
          'ok %i %s # skip %s' % (self._done, command, skip.group(1)))
      else:
        status_line = 'ok %i %s' % (self._done, command)
        if FLAKY in output.test.outcomes:
          status_line = status_line + ' # TODO : Fix flaky test'
        logger.info(status_line)

      if output.diagnostic:
        self.severity = 'ok'
        if isinstance(output.diagnostic, list):
          self.traceback = '\n'.join(output.diagnostic)
        else:
          self.traceback = output.diagnostic


    duration = output.test.duration
    logger.info('  ---')
    logger.info('  duration_ms: %.5f' % (duration  / timedelta(milliseconds=1)))
    if self.severity != 'ok' or self.traceback != '':
      if output.HasTimedOut():
        self.traceback = 'timeout\n' + output.output.stdout + output.output.stderr
      self._printDiagnostic()
    logger.info('  ...')

  def Done(self):
    pass

class DeoptsCheckProgressIndicator(SimpleProgressIndicator):

  def Starting(self):
    pass

  def AboutToRun(self, case):
    pass

  def HasRun(self, output):
    # Print test name as (for example) "parallel/test-assert".  Tests that are
    # scraped from the addons documentation are all named test.js, making it
    # hard to decipher what test is running when only the filename is printed.
    prefix = abspath(join(dirname(__file__), '../test')) + os.sep
    command = output.command[-1]
    command = NormalizePath(command, prefix)

    stdout = output.output.stdout.strip()
    printed_file = False
    for line in stdout.splitlines():
      if (
        (line.startswith("[aborted optimiz") or line.startswith("[disabled optimiz")) and
        ("because:" in line or "reason:" in line)
      ):
        if not printed_file:
          printed_file = True
          print('==== %s ====' % command)
          self.failed.append(output)
        print('  %s' % line)

  def Done(self):
    pass


class CompactProgressIndicator(ProgressIndicator):

  def __init__(self, cases, flaky_tests_mode, measure_flakiness, templates):
    super(CompactProgressIndicator, self).__init__(cases, flaky_tests_mode, measure_flakiness)
    self.templates = templates
    self.last_status_length = 0
    self.start_time = time.time()

  def Starting(self):
    pass

  def Done(self):
    self.PrintProgress('Done\n')

  def AboutToRun(self, case):
    self.PrintProgress(case.GetLabel())

  def HasRun(self, output):
    if output.UnexpectedOutput():
      self.ClearLine(self.last_status_length)
      self.PrintFailureHeader(output.test)
      stdout = output.output.stdout.strip()
      if len(stdout):
        print(self.templates['stdout'] % stdout)
      stderr = output.output.stderr.strip()
      if len(stderr):
        print(self.templates['stderr'] % stderr)
      print("Command: %s" % EscapeCommand(output.command))
      if output.HasCrashed():
        print("--- %s ---" % PrintCrashed(output.output.exit_code))
      if output.HasTimedOut():
        print("--- TIMEOUT ---")
      print("\n") # Two blank lines between failures, for visual separation

  def Truncate(self, str, length):
    if length and (len(str) > (length - 3)):
      return str[:(length-3)] + "..."
    else:
      return str

  def PrintProgress(self, name):
    self.ClearLine(self.last_status_length)
    elapsed = time.time() - self.start_time
    status = self.templates['status_line'] % {
      'passed': self.succeeded,
      'remaining': (((self.total - self.remaining) * 100) // self.total),
      'failed': len(self.failed),
      'test': name,
      'mins': int(elapsed) / 60,
      'secs': int(elapsed) % 60
    }
    status = self.Truncate(status, 78)
    self.last_status_length = len(status)
    print(status, end='')
    sys.stdout.flush()


class ColorProgressIndicator(CompactProgressIndicator):

  def __init__(self, cases, flaky_tests_mode, measure_flakiness):
    templates = {
      'status_line': "[%(mins)02i:%(secs)02i|\033[34m%%%(remaining) 4d\033[0m|\033[32m+%(passed) 4d\033[0m|\033[31m-%(failed) 4d\033[0m]: %(test)s",
      'stdout': "\033[1m%s\033[0m",
      'stderr': "\033[31m%s\033[0m",
    }
    super(ColorProgressIndicator, self).__init__(cases, flaky_tests_mode, measure_flakiness, templates)

  def ClearLine(self, last_line_length):
    print("\033[1K\r", end='')


class MonochromeProgressIndicator(CompactProgressIndicator):

  def __init__(self, cases, flaky_tests_mode, measure_flakiness):
    templates = {
      'status_line': "[%(mins)02i:%(secs)02i|%%%(remaining) 4d|+%(passed) 4d|-%(failed) 4d]: %(test)s",
      'stdout': '%s',
      'stderr': '%s',
      'clear': lambda last_line_length: ("\r" + (" " * last_line_length) + "\r"),
      'max_length': 78
    }
    super(MonochromeProgressIndicator, self).__init__(cases, flaky_tests_mode, measure_flakiness, templates)

  def ClearLine(self, last_line_length):
    print(("\r" + (" " * last_line_length) + "\r"), end='')


PROGRESS_INDICATORS = {
  'verbose': VerboseProgressIndicator,
  'dots': DotsProgressIndicator,
  'actions': ActionsAnnotationProgressIndicator,
  'color': ColorProgressIndicator,
  'tap': TapProgressIndicator,
  'mono': MonochromeProgressIndicator,
  'deopts': DeoptsCheckProgressIndicator
}


# -------------------------
# --- F r a m e w o r k ---
# -------------------------


class CommandOutput(object):

  def __init__(self, exit_code, timed_out, stdout, stderr):
    self.exit_code = exit_code
    self.timed_out = timed_out
    self.stdout = stdout
    self.stderr = stderr
    self.failed = None


class TestCase(object):

  def __init__(self, context, path, arch, mode):
    self.path = path
    self.context = context
    self.duration = None
    self.arch = arch
    self.mode = mode
    self.parallel = False
    self.disable_core_files = False
    self.max_virtual_memory = None
    self.serial_id = 0
    self.thread_id = 0

  def IsNegative(self):
    return self.context.expect_fail

  def DidFail(self, output):
    if output.failed is None:
      output.failed = self.IsFailureOutput(output)
    return output.failed

  def IsFailureOutput(self, output):
    return output.exit_code != 0

  def GetSource(self):
    return "(no source available)"

  def RunCommand(self, command, env):
    full_command = self.context.processor(command)
    output = Execute(full_command,
                     self.context,
                     self.context.GetTimeout(self.mode, self.config.section),
                     env,
                     disable_core_files = self.disable_core_files,
                     max_virtual_memory = self.max_virtual_memory)
    return TestOutput(self,
                      full_command,
                      output,
                      self.context.store_unexpected_output)

  def Run(self):
    try:
      result = self.RunCommand(self.GetCommand(), {
        "TEST_SERIAL_ID": "%d" % self.serial_id,
        "TEST_THREAD_ID": "%d" % self.thread_id,
        "TEST_PARALLEL" : "%d" % self.parallel,
        "GITHUB_STEP_SUMMARY": "",
      })
    finally:
      # Tests can leave the tty in non-blocking mode. If the test runner
      # tries to print to stdout/stderr after that and the tty buffer is
      # full, it'll die with a EAGAIN OSError. Ergo, put the tty back in
      # blocking mode before proceeding.
      if sys.platform != 'win32':
        from fcntl import fcntl, F_GETFL, F_SETFL
        from os import O_NONBLOCK
        for fd in 0,1,2: fcntl(fd, F_SETFL, ~O_NONBLOCK & fcntl(fd, F_GETFL))

    return result


class TestOutput(object):

  def __init__(self, test, command, output, store_unexpected_output):
    self.test = test
    self.command = command
    self.output = output
    self.store_unexpected_output = store_unexpected_output
    self.diagnostic = []

  def UnexpectedOutput(self):
    if self.HasCrashed():
      outcome = CRASH
    elif self.HasTimedOut():
      outcome = TIMEOUT
    elif self.HasFailed():
      outcome = FAIL
    else:
      outcome = PASS
    return not outcome in self.test.outcomes

  def HasCrashed(self):
    if utils.IsWindows():
      return 0x80000000 & self.output.exit_code and not (0x3FFFFF00 & self.output.exit_code)
    else:
      # Timed out tests will have exit_code -signal.SIGTERM.
      if self.output.timed_out:
        return False
      return self.output.exit_code < 0

  def HasTimedOut(self):
    return self.output.timed_out

  def HasFailed(self):
    execution_failed = self.test.DidFail(self.output)
    if self.test.IsNegative():
      return not execution_failed
    else:
      return execution_failed


def KillProcessWithID(pid, signal_to_send=signal.SIGTERM):
  if utils.IsWindows():
    os.popen('taskkill /T /F /PID %d' % pid)
  else:
    os.kill(pid, signal_to_send)


MAX_SLEEP_TIME = 0.1
INITIAL_SLEEP_TIME = 0.0001
SLEEP_TIME_FACTOR = 1.25

SEM_INVALID_VALUE = -1
SEM_NOGPFAULTERRORBOX = 0x0002 # Microsoft Platform SDK WinBase.h

def Win32SetErrorMode(mode):
  prev_error_mode = SEM_INVALID_VALUE
  try:
    import ctypes
    prev_error_mode = ctypes.windll.kernel32.SetErrorMode(mode)
  except ImportError:
    pass
  return prev_error_mode


def KillTimedOutProcess(context, pid):
  signal_to_send = signal.SIGTERM
  if context.abort_on_timeout:
    # Using SIGABRT here allows the OS to generate a core dump that can be
    # looked at post-mortem, which helps for investigating failures that are
    # difficult to reproduce.
    signal_to_send = signal.SIGABRT
  KillProcessWithID(pid, signal_to_send)


def RunProcess(context, timeout, args, **rest):
  if context.verbose: print("#", " ".join(args))
  popen_args = args
  prev_error_mode = SEM_INVALID_VALUE
  if utils.IsWindows():
    if context.suppress_dialogs:
      # Try to change the error mode to avoid dialogs on fatal errors. Don't
      # touch any existing error mode flags by merging the existing error mode.
      # See http://blogs.msdn.com/oldnewthing/archive/2004/07/27/198410.aspx.
      error_mode = SEM_NOGPFAULTERRORBOX
      prev_error_mode = Win32SetErrorMode(error_mode)
      Win32SetErrorMode(error_mode | prev_error_mode)

  process = subprocess.Popen(
    args = popen_args,
    **rest
  )
  if utils.IsWindows() and context.suppress_dialogs and prev_error_mode != SEM_INVALID_VALUE:
    Win32SetErrorMode(prev_error_mode)
  # Compute the end time - if the process crosses this limit we
  # consider it timed out.
  if timeout is None: end_time = None
  else: end_time = time.time() + timeout
  timed_out = False
  # Repeatedly check the exit code from the process in a
  # loop and keep track of whether or not it times out.
  exit_code = None
  sleep_time = INITIAL_SLEEP_TIME

  while exit_code is None:
    if (not end_time is None) and (time.time() >= end_time):
      # Kill the process and wait for it to exit.
      KillTimedOutProcess(context, process.pid)
      exit_code = process.wait()
      timed_out = True
    else:
      exit_code = process.poll()
      time.sleep(sleep_time)
      sleep_time = sleep_time * SLEEP_TIME_FACTOR
      if sleep_time > MAX_SLEEP_TIME:
        sleep_time = MAX_SLEEP_TIME
  return (process, exit_code, timed_out)


def PrintError(str):
  sys.stderr.write(str)
  sys.stderr.write('\n')


def CheckedUnlink(name):
  while True:
    try:
      os.unlink(name)
    except OSError as e:
      # On Windows unlink() fails if another process (typically a virus scanner
      # or the indexing service) has the file open. Those processes keep a
      # file open for a short time only, so yield and try again; it'll succeed.
      if sys.platform == 'win32' and e.errno == errno.EACCES:
        time.sleep(0)
        continue
      PrintError("os.unlink() " + str(e))
    break

def Execute(args, context, timeout=None, env=None, disable_core_files=False,
            stdin=None, max_virtual_memory=None):
  (fd_out, outname) = tempfile.mkstemp()
  (fd_err, errname) = tempfile.mkstemp()

  if env is None:
    env = {}
  env_copy = os.environ.copy()

  # Remove NODE_PATH
  if "NODE_PATH" in env_copy:
    del env_copy["NODE_PATH"]

  # Remove NODE_REPL_EXTERNAL_MODULE
  if "NODE_REPL_EXTERNAL_MODULE" in env_copy:
    del env_copy["NODE_REPL_EXTERNAL_MODULE"]

  # Extend environment
  for key, value in env.items():
    env_copy[key] = value

  preexec_fn = None

  def disableCoreFiles():
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))

  if disable_core_files and not utils.IsWindows():
    preexec_fn = disableCoreFiles

  if max_virtual_memory is not None and utils.GuessOS() == 'linux':
    def setMaxVirtualMemory():
      import resource
      resource.setrlimit(resource.RLIMIT_CORE, (0,0))
      resource.setrlimit(resource.RLIMIT_AS, (max_virtual_memory,max_virtual_memory + 1))

    if preexec_fn is not None:
      prev_preexec_fn = preexec_fn
      def setResourceLimits():
        setMaxVirtualMemory()
        prev_preexec_fn()
      preexec_fn = setResourceLimits
    else:
      preexec_fn = setMaxVirtualMemory

  (process, exit_code, timed_out) = RunProcess(
    context,
    timeout,
    args = args,
    stdin = stdin,
    stdout = fd_out,
    stderr = fd_err,
    env = env_copy,
    preexec_fn = preexec_fn
  )
  os.close(fd_out)
  os.close(fd_err)
  output = open(outname, encoding='utf8').read()
  errors = open(errname, encoding='utf8').read()
  CheckedUnlink(outname)
  CheckedUnlink(errname)

  return CommandOutput(exit_code, timed_out, output, errors)


def CarCdr(path):
  if len(path) == 0:
    return (None, [ ])
  else:
    return (path[0], path[1:])


class TestConfiguration(object):
  def __init__(self, context, root, section):
    self.context = context
    self.root = root
    self.section = section

  def Contains(self, path, file):
    if len(path) > len(file):
      return False
    for i in range(len(path)):
      if not path[i].match(NormalizePath(file[i])):
        return False
    return True

  def GetTestStatus(self, sections, defs):
    status_file = join(self.root, '%s.status' % self.section)
    if exists(status_file):
      ReadConfigurationInto(status_file, sections, defs)


class TestSuite(object):

  def __init__(self, name):
    self.name = name

  def GetName(self):
    return self.name


class TestRepository(TestSuite):

  def __init__(self, path):
    normalized_path = abspath(path)
    super(TestRepository, self).__init__(basename(normalized_path))
    self.path = normalized_path
    self.is_loaded = False
    self.config = None

  def GetConfiguration(self, context):
    if self.is_loaded:
      return self.config
    self.is_loaded = True

    module = get_module('testcfg', self.path)
    self.config = module.GetConfiguration(context, self.path)
    if hasattr(self.config, 'additional_flags'):
      self.config.additional_flags += context.node_args
    else:
      self.config.additional_flags = context.node_args
    return self.config

  def GetBuildRequirements(self, path, context):
    return self.GetConfiguration(context).GetBuildRequirements()

  def AddTestsToList(self, result, current_path, path, context, arch, mode):
    tests = self.GetConfiguration(context).ListTests(current_path, path,
                                                     arch, mode)
    result += tests
    for i in range(1, context.repeat):
      result += copy.deepcopy(tests)

  def GetTestStatus(self, context, sections, defs):
    self.GetConfiguration(context).GetTestStatus(sections, defs)


class LiteralTestSuite(TestSuite):
  def __init__(self, tests_repos, test_root):
    super(LiteralTestSuite, self).__init__('root')
    self.tests_repos = tests_repos
    self.test_root = test_root

  def GetBuildRequirements(self, path, context):
    (name, rest) = CarCdr(path)
    result = [ ]
    for test in self.tests_repos:
      if not name or name.match(test.GetName()):
        result += test.GetBuildRequirements(rest, context)
    return result

  def ListTests(self, current_path, path, context, arch, mode):
    (name, rest) = CarCdr(path)
    result = [ ]
    for test in self.tests_repos:
      test_name = test.GetName()
      if not name or name.match(test_name):
        full_path = current_path + [test_name]
        test.AddTestsToList(result, full_path, path, context, arch, mode)
    result.sort(key=lambda x: x.GetName())
    return result

  def GetTestStatus(self, context, sections, defs):
    # Just read the test configuration from root_path/root.status.
    root = TestConfiguration(context, self.test_root, 'root')
    root.GetTestStatus(sections, defs)
    for tests_repos in self.tests_repos:
      tests_repos.GetTestStatus(context, sections, defs)


TIMEOUT_SCALEFACTOR = {
    'arm'       : { 'debug' :  8, 'release' : 3 }, # The ARM buildbots are slow.
    'riscv64'   : { 'debug' :  8, 'release' : 3 }, # The riscv devices are slow.
    'loong64'   : { 'debug' :  4, 'release' : 1 },
    'ia32'      : { 'debug' :  4, 'release' : 1 },
    'ppc'       : { 'debug' :  4, 'release' : 1 },
    's390'      : { 'debug' :  4, 'release' : 1 } }


class Context(object):

  def __init__(self, workspace, verbose, vm, args, expect_fail,
               timeout, processor, suppress_dialogs,
               store_unexpected_output, repeat, abort_on_timeout):
    self.workspace = workspace
    self.verbose = verbose
    self.vm = vm
    self.node_args = args
    self.expect_fail = expect_fail
    self.timeout = timeout
    self.processor = processor
    self.suppress_dialogs = suppress_dialogs
    self.store_unexpected_output = store_unexpected_output
    self.repeat = repeat
    self.abort_on_timeout = abort_on_timeout
    self.v8_enable_inspector = True
    self.node_has_crypto = True

  def GetVm(self, arch, mode):
    if self.vm is not None:
      return self.vm
    if arch == 'none':
      name = 'out/Debug/node' if mode == 'debug' else 'out/Release/node'
    else:
      name = 'out/%s.%s/node' % (arch, mode)

    # Currently GYP does not support output_dir for MSVS.
    # http://code.google.com/p/gyp/issues/detail?id=40
    # It will put the builds into Release/node.exe or Debug/node.exe
    if utils.IsWindows():
      if not exists(name + '.exe'):
        name = name.replace('out/', '')
      name = os.path.abspath(name + '.exe')

    if not exists(name):
      raise ValueError('Could not find executable. Should be ' + name)

    return name

  def GetTimeout(self, mode, section=''):
    timeout = self.timeout * TIMEOUT_SCALEFACTOR[ARCH_GUESS or 'ia32'][mode]
    if section == 'pummel' or section == 'benchmark':
      timeout = timeout * 6
    # We run all WPT from one subset in the same process using workers.
    # As the number of the tests grow, it can take longer to run some of the
    # subsets, but it's still overall faster than running them in different
    # processes.
    elif section == 'wpt':
      timeout = timeout * 12
    return timeout

def RunTestCases(cases_to_run, progress, tasks, flaky_tests_mode, measure_flakiness):
  progress = PROGRESS_INDICATORS[progress](cases_to_run, flaky_tests_mode, measure_flakiness)
  return progress.Run(tasks)

# -------------------------------------------
# --- T e s t   C o n f i g u r a t i o n ---
# -------------------------------------------


RUN = 'run'
SKIP = 'skip'
FAIL = 'fail'
PASS = 'pass'
OKAY = 'okay'
TIMEOUT = 'timeout'
CRASH = 'crash'
SLOW = 'slow'
FLAKY = 'flaky'
DONTCARE = 'dontcare'
KEEP_RETRYING = 'keep_retrying'

class Expression(object):
  pass


class Constant(Expression):

  def __init__(self, value):
    self.value = value

  def Evaluate(self, env, defs):
    return self.value


class Variable(Expression):

  def __init__(self, name):
    self.name = name

  def GetOutcomes(self, env, defs):
    if self.name in env: return set([env[self.name]])
    else: return set()


class Outcome(Expression):

  def __init__(self, name):
    self.name = name

  def GetOutcomes(self, env, defs):
    if self.name in defs:
      return defs[self.name].GetOutcomes(env, defs)
    else:
      return set([self.name])


class Operation(Expression):

  def __init__(self, left, op, right):
    self.left = left
    self.op = op
    self.right = right

  def Evaluate(self, env, defs):
    if self.op == '||' or self.op == ',':
      return self.left.Evaluate(env, defs) or self.right.Evaluate(env, defs)
    elif self.op == 'if':
      return False
    elif self.op == '!=':
      inter = self.left.GetOutcomes(env, defs) != self.right.GetOutcomes(env, defs)
      return bool(inter)
    elif self.op == '==':
      inter = self.left.GetOutcomes(env, defs) & self.right.GetOutcomes(env, defs)
      return bool(inter)
    else:
      assert self.op == '&&'
      return self.left.Evaluate(env, defs) and self.right.Evaluate(env, defs)

  def GetOutcomes(self, env, defs):
    if self.op == '||' or self.op == ',':
      return self.left.GetOutcomes(env, defs) | self.right.GetOutcomes(env, defs)
    elif self.op == 'if':
      if self.right.Evaluate(env, defs):
        return self.left.GetOutcomes(env, defs)
      else:
        return set()
    else:
      assert self.op == '&&'
      return self.left.GetOutcomes(env, defs) & self.right.GetOutcomes(env, defs)


def IsAlpha(str):
  for char in str:
    if not (char.isalpha() or char.isdigit() or char == '_'):
      return False
  return True


class Tokenizer(object):
  """A simple string tokenizer that chops expressions into variables,
  parens and operators"""

  def __init__(self, expr):
    self.index = 0
    self.expr = expr
    self.length = len(expr)
    self.tokens = None

  def Current(self, length = 1):
    if not self.HasMore(length): return ""
    return self.expr[self.index:self.index+length]

  def HasMore(self, length = 1):
    return self.index < self.length + (length - 1)

  def Advance(self, count = 1):
    self.index = self.index + count

  def AddToken(self, token):
    self.tokens.append(token)

  def SkipSpaces(self):
    while self.HasMore() and self.Current().isspace():
      self.Advance()

  def Tokenize(self):
    self.tokens = [ ]
    while self.HasMore():
      self.SkipSpaces()
      if not self.HasMore():
        return None
      if self.Current() == '(':
        self.AddToken('(')
        self.Advance()
      elif self.Current() == ')':
        self.AddToken(')')
        self.Advance()
      elif self.Current() == '$':
        self.AddToken('$')
        self.Advance()
      elif self.Current() == ',':
        self.AddToken(',')
        self.Advance()
      elif IsAlpha(self.Current()):
        buf = ""
        while self.HasMore() and IsAlpha(self.Current()):
          buf += self.Current()
          self.Advance()
        self.AddToken(buf)
      elif self.Current(2) == '&&':
        self.AddToken('&&')
        self.Advance(2)
      elif self.Current(2) == '||':
        self.AddToken('||')
        self.Advance(2)
      elif self.Current(2) == '==':
        self.AddToken('==')
        self.Advance(2)
      elif self.Current(2) == '!=':
        self.AddToken('!=')
        self.Advance(2)
      else:
        return None
    return self.tokens


class Scanner(object):
  """A simple scanner that can serve out tokens from a given list"""

  def __init__(self, tokens):
    self.tokens = tokens
    self.length = len(tokens)
    self.index = 0

  def HasMore(self):
    return self.index < self.length

  def Current(self):
    return self.tokens[self.index]

  def Advance(self):
    self.index = self.index + 1


def ParseAtomicExpression(scan):
  if scan.Current() == "true":
    scan.Advance()
    return Constant(True)
  elif scan.Current() == "false":
    scan.Advance()
    return Constant(False)
  elif IsAlpha(scan.Current()):
    name = scan.Current()
    scan.Advance()
    return Outcome(name.lower())
  elif scan.Current() == '$':
    scan.Advance()
    if not IsAlpha(scan.Current()):
      return None
    name = scan.Current()
    scan.Advance()
    return Variable(name.lower())
  elif scan.Current() == '(':
    scan.Advance()
    result = ParseLogicalExpression(scan)
    if (not result) or (scan.Current() != ')'):
      return None
    scan.Advance()
    return result
  else:
    return None


BINARIES = ['==', '!=']
def ParseOperatorExpression(scan):
  left = ParseAtomicExpression(scan)
  if not left: return None
  while scan.HasMore() and (scan.Current() in BINARIES):
    op = scan.Current()
    scan.Advance()
    right = ParseOperatorExpression(scan)
    if not right:
      return None
    left = Operation(left, op, right)
  return left


def ParseConditionalExpression(scan):
  left = ParseOperatorExpression(scan)
  if not left: return None
  while scan.HasMore() and (scan.Current() == 'if'):
    scan.Advance()
    right = ParseOperatorExpression(scan)
    if not right:
      return None
    left=  Operation(left, 'if', right)
  return left


LOGICALS = ["&&", "||", ","]
def ParseLogicalExpression(scan):
  left = ParseConditionalExpression(scan)
  if not left: return None
  while scan.HasMore() and (scan.Current() in LOGICALS):
    op = scan.Current()
    scan.Advance()
    right = ParseConditionalExpression(scan)
    if not right:
      return None
    left = Operation(left, op, right)
  return left


def ParseCondition(expr):
  """Parses a logical expression into an Expression object"""
  tokens = Tokenizer(expr).Tokenize()
  if not tokens:
    print("Malformed expression: '%s'" % expr)
    return None
  scan = Scanner(tokens)
  ast = ParseLogicalExpression(scan)
  if not ast:
    print("Malformed expression: '%s'" % expr)
    return None
  if scan.HasMore():
    print("Malformed expression: '%s'" % expr)
    return None
  return ast


class Configuration(object):
  """The parsed contents of a configuration file"""

  def __init__(self, sections, defs):
    self.sections = sections
    self.defs = defs

  def ClassifyTests(self, cases, env):
    sections = [ s for s in self.sections if s.condition.Evaluate(env, self.defs) ]
    all_rules = reduce(list.__add__, [s.rules for s in sections], [])
    unused_rules = set(all_rules)
    result = []
    for case in cases:
      matches = [ r for r in all_rules if r.Contains(case.path) ]
      outcomes_list = [ r.GetOutcomes(env, self.defs) for r in matches ]
      outcomes = reduce(set.union, outcomes_list, set())
      unused_rules.difference_update(matches)
      case.outcomes = set(outcomes) or set([PASS])
      # slow tests may also just pass.
      if SLOW in case.outcomes:
        case.outcomes.add(PASS)
      result.append(case)
    return result, unused_rules


class Section(object):
  """A section of the configuration file.  Sections are enabled or
  disabled prior to running the tests, based on their conditions"""

  def __init__(self, condition):
    self.condition = condition
    self.rules = [ ]

  def AddRule(self, rule):
    self.rules.append(rule)


class Rule(object):
  """A single rule that specifies the expected outcome for a single
  test."""

  def __init__(self, raw_path, path, value):
    self.raw_path = raw_path
    self.path = path
    self.value = value

  def GetOutcomes(self, env, defs):
    return self.value.GetOutcomes(env, defs)

  def Contains(self, path):
    if len(self.path) > len(path):
      return False
    for i in range(len(self.path)):
      if not self.path[i].match(path[i]):
        return False
    return True


HEADER_PATTERN = re.compile(r'\[([^]]+)\]')
RULE_PATTERN = re.compile(r'\s*([^: ]*)\s*:(.*)')
DEF_PATTERN = re.compile(r'^def\s*(\w+)\s*=(.*)$')
PREFIX_PATTERN = re.compile(r'^\s*prefix\s+([\w_.\-/]+)$')


def ReadConfigurationInto(path, sections, defs):
  current_section = Section(Constant(True))
  sections.append(current_section)
  prefix = []
  for line in utils.ReadLinesFrom(path):
    header_match = HEADER_PATTERN.match(line)
    if header_match:
      condition_str = header_match.group(1).strip()
      condition = ParseCondition(condition_str)
      new_section = Section(condition)
      sections.append(new_section)
      current_section = new_section
      continue
    rule_match = RULE_PATTERN.match(line)
    if rule_match:
      path = prefix + SplitPath(rule_match.group(1).strip())
      value_str = rule_match.group(2).strip()
      value = ParseCondition(value_str)
      if not value:
        return False
      current_section.AddRule(Rule(rule_match.group(1), path, value))
      continue
    def_match = DEF_PATTERN.match(line)
    if def_match:
      name = def_match.group(1).lower()
      value = ParseCondition(def_match.group(2).strip())
      if not value:
        return False
      defs[name] = value
      continue
    prefix_match = PREFIX_PATTERN.match(line)
    if prefix_match:
      prefix = SplitPath(prefix_match.group(1).strip())
      continue
    raise Exception("Malformed line: '%s'." % line)


# ---------------
# --- M a i n ---
# ---------------


ARCH_GUESS = utils.GuessArchitecture()


def BuildOptions():
  result = optparse.OptionParser()
  result.add_option("-m", "--mode", help="The test modes in which to run (comma-separated)",
      default='release')
  result.add_option("-v", "--verbose", help="Verbose output",
      default=False, action="store_true")
  result.add_option('--logfile', dest='logfile',
      help='write test output to file. NOTE: this only applies the tap progress indicator')
  result.add_option("-p", "--progress",
      help="The style of progress indicator (%s)" % ", ".join(PROGRESS_INDICATORS.keys()),
      choices=list(PROGRESS_INDICATORS.keys()), default="mono")
  result.add_option("--report", help="Print a summary of the tests to be run",
      default=False, action="store_true")
  result.add_option("-s", "--suite", help="A test suite",
      default=[], action="append")
  result.add_option("-t", "--timeout", help="Timeout in seconds",
      default=120, type="int")
  result.add_option("--arch", help='The architecture to run tests for',
      default='none')
  result.add_option("--snapshot", help="Run the tests with snapshot turned on",
      default=False, action="store_true")
  result.add_option("--special-command", default=None)
  result.add_option("--node-args", dest="node_args", help="Args to pass through to Node",
      default=[], action="append")
  result.add_option("--expect-fail", dest="expect_fail",
      help="Expect test cases to fail", default=False, action="store_true")
  result.add_option("--valgrind", help="Run tests through valgrind",
      default=False, action="store_true")
  result.add_option("--worker", help="Run parallel tests inside a worker context",
      default=False, action="store_true")
  result.add_option("--check-deopts", help="Check tests for permanent deoptimizations",
      default=False, action="store_true")
  result.add_option("--cat", help="Print the source of the tests",
      default=False, action="store_true")
  result.add_option("--flaky-tests",
      help="Regard tests marked as flaky (run|skip|dontcare|keep_retrying)",
      default="run")
  result.add_option("--measure-flakiness",
      help="When a test fails, re-run it x number of times",
      default=0, type="int")
  result.add_option("--skip-tests",
      help="Tests that should not be executed (comma-separated)",
      default="")
  result.add_option("--warn-unused", help="Report unused rules",
      default=False, action="store_true")
  result.add_option("-j", help="The number of parallel tasks to run, 0=use number of cores",
      default=0, type="int")
  result.add_option("-J", help="For legacy compatibility, has no effect",
      default=False, action="store_true")
  result.add_option("--time", help="Print timing information after running",
      default=False, action="store_true")
  result.add_option("--suppress-dialogs", help="Suppress Windows dialogs for crashing tests",
        dest="suppress_dialogs", default=True, action="store_true")
  result.add_option("--no-suppress-dialogs", help="Display Windows dialogs for crashing tests",
        dest="suppress_dialogs", action="store_false")
  result.add_option("--shell", help="Path to node executable", default=None)
  result.add_option("--store-unexpected-output",
      help="Store the temporary JS files from tests that fails",
      dest="store_unexpected_output", default=True, action="store_true")
  result.add_option("--no-store-unexpected-output",
      help="Deletes the temporary JS files from tests that fails",
      dest="store_unexpected_output", action="store_false")
  result.add_option("-r", "--run",
      help="Divide the tests in m groups (interleaved) and run tests from group n (--run=n,m with n < m)",
      default="")
  result.add_option('--temp-dir',
      help='Optional path to change directory used for tests', default=False)
  result.add_option('--test-root',
      help='Optional path to change test directory', dest='test_root', default=None)
  result.add_option('--repeat',
      help='Number of times to repeat given tests',
      default=1, type="int")
  result.add_option('--abort-on-timeout',
      help='Send SIGABRT instead of SIGTERM to kill processes that time out',
      default=False, action="store_true", dest="abort_on_timeout")
  result.add_option("--type",
      help="Type of build (simple, fips, coverage)",
      default=None)
  return result


def ProcessOptions(options):
  global VERBOSE
  VERBOSE = options.verbose
  options.arch = options.arch.split(',')
  options.mode = options.mode.split(',')
  options.run = options.run.split(',')
  # Split at commas and filter out all the empty strings.
  options.skip_tests = [test for test in options.skip_tests.split(',') if test]
  if options.run == [""]:
    options.run = None
  elif len(options.run) != 2:
    print("The run argument must be two comma-separated integers.")
    return False
  else:
    try:
      options.run = [int(level) for level in options.run]
    except ValueError:
      print("Could not parse the integers from the run argument.")
      return False
    if options.run[0] < 0 or options.run[1] < 0:
      print("The run argument cannot have negative integers.")
      return False
    if options.run[0] >= options.run[1]:
      print("The test group to run (n) must be smaller than number of groups (m).")
      return False
  if options.j == 0:
    # inherit JOBS from environment if provided. some virtualised systems
    # tends to exaggerate the number of available cpus/cores.
    cores = os.environ.get('JOBS')
    options.j = int(cores) if cores is not None else multiprocessing.cpu_count()
  elif options.J:
    # If someone uses -j and legacy -J, let them know that we will be respecting
    # -j and ignoring -J, which is the opposite of what we used to do before -J
    # became a legacy no-op.
    print('Warning: Legacy -J option is ignored. Using the -j option.')
  if options.flaky_tests not in [RUN, SKIP, DONTCARE, KEEP_RETRYING]:
    print("Unknown flaky-tests mode %s" % options.flaky_tests)
    return False
  return True


REPORT_TEMPLATE = """\
Total: %(total)i tests
 * %(skipped)4d tests will be skipped
 * %(pass)4d tests are expected to pass
 * %(fail_ok)4d tests are expected to fail that we won't fix
 * %(fail)4d tests are expected to fail that we should fix\
"""


class Pattern(object):

  def __init__(self, pattern):
    self.pattern = pattern
    self.compiled = None

  def match(self, str):
    if not self.compiled:
      pattern = "^" + self.pattern.replace('*', '.*') + "$"
      self.compiled = re.compile(pattern)
    return self.compiled.match(str)

  def __str__(self):
    return self.pattern


def SplitPath(path_arg):
  stripped = [c.strip() for c in path_arg.split('/')]
  return [Pattern(s) for s in stripped if len(s) > 0]

def NormalizePath(path, prefix='test/'):
  # strip the extra path information of the specified test
  prefix = prefix.replace('\\', '/')
  path = path.replace('\\', '/')
  if path.startswith(prefix):
    path = path[len(prefix):]
  if path.endswith('.js'):
    path = path[:-3]
  elif path.endswith('.mjs'):
    path = path[:-4]
  return path

def GetSpecialCommandProcessor(value):
  if (not value) or (value.find('@') == -1):
    def ExpandCommand(args):
      return args
    return ExpandCommand
  else:
    prefix, _, suffix = value.partition('@')
    prefix = unquote(prefix).split()
    suffix = unquote(suffix).split()
    def ExpandCommand(args):
      return prefix + args + suffix
    return ExpandCommand

def GetSuites(test_root):
  def IsSuite(path):
    return isdir(path) and exists(join(path, 'testcfg.py'))
  return [ f for f in os.listdir(test_root) if IsSuite(join(test_root, f)) ]


def FormatTime(d):
  millis = round(d * 1000) % 1000
  return time.strftime("%M:%S.", time.gmtime(d)) + ("%03i" % millis)


def FormatTimedelta(td):
  if hasattr(td, 'total_seconds'):
    d = td.total_seconds()
  else: # python2.6 compat
    d =  td.seconds + (td.microseconds / 10.0**6)
  return FormatTime(d)


def PrintCrashed(code):
  if utils.IsWindows():
    return "CRASHED"
  else:
    return "CRASHED (Signal: %d)" % -code


# these suites represent special cases that should not be run as part of the
# default JavaScript test-run, e.g., internet/ requires a network connection,
# addons/ requires compilation.
IGNORED_SUITES = [
  'addons',
  'benchmark',
  'doctool',
  'embedding',
  'internet',
  'js-native-api',
  'node-api',
  'pummel',
  'sqlite',
  'tick-processor',
  'v8-updates'
]


def ArgsToTestPaths(test_root, args, suites):
  if len(args) == 0 or 'default' in args:
    def_suites = [s for s in suites if s not in IGNORED_SUITES]
    args = [a for a in args if a != 'default'] + def_suites
  subsystem_regex = re.compile(r'^[a-zA-Z-]*$')
  check = lambda arg: subsystem_regex.match(arg) and (arg not in suites)
  mapped_args = ["*/test*-%s-*" % arg if check(arg) else arg for arg in args]
  paths = [SplitPath(NormalizePath(a)) for a in mapped_args]
  return paths


def get_env_type(vm, options_type, context):
  if options_type is not None:
    env_type = options_type
  else:
    # 'simple' is the default value for 'env_type'.
    env_type = 'simple'
    ssl_ver = Execute([vm, '-p', 'process.versions.openssl'], context).stdout
    if 'fips' in ssl_ver:
      env_type = 'fips'
  return env_type


def get_asan_state(vm, context):
  asan = Execute([vm, '-p', 'process.config.variables.asan'], context).stdout.strip()
  return "on" if asan == "1" else "off"


def Main():
  parser = BuildOptions()
  (options, args) = parser.parse_args()
  if not ProcessOptions(options):
    parser.print_help()
    return 1

  stream = sys.stdout
  try:
    sys.stdout.reconfigure(encoding='utf8')
  except AttributeError:
    # Python < 3.7 does not have reconfigure
    stream = io.TextIOWrapper(sys.stdout.buffer,encoding='utf8')
  ch = logging.StreamHandler(stream)
  logger.addHandler(ch)
  logger.setLevel(logging.INFO)
  if options.logfile:
    fh = logging.FileHandler(options.logfile, encoding='utf-8', mode='w')
    logger.addHandler(fh)

  workspace = abspath(join(dirname(sys.argv[0]), '..'))
  test_root = join(workspace, 'test')
  if options.test_root is not None:
    test_root = options.test_root
  suites = GetSuites(test_root)
  repositories = [TestRepository(join(test_root, name)) for name in suites]
  repositories += [TestRepository(a) for a in options.suite]

  root = LiteralTestSuite(repositories, test_root)
  paths = ArgsToTestPaths(test_root, args, suites)

  # Check for --valgrind option. If enabled, we overwrite the special
  # command flag with a command that uses the run-valgrind.py script.
  if options.valgrind:
    run_valgrind = join(workspace, "tools", "run-valgrind.py")
    options.special_command = "python -u " + run_valgrind + " @"

  if options.check_deopts:
    options.node_args.append("--trace-opt")
    options.node_args.append("--trace-file-names")
    # --always-turbofan is needed because many tests do not run long enough for
    # the optimizer to kick in, so this flag will force it to run.
    options.node_args.append("--always-turbofan")
    options.progress = "deopts"

  if options.worker:
    run_worker = join(workspace, "tools", "run-worker.js")
    options.node_args.append(run_worker)

  processor = GetSpecialCommandProcessor(options.special_command)

  context = Context(workspace,
                    VERBOSE,
                    options.shell,
                    options.node_args,
                    options.expect_fail,
                    options.timeout,
                    processor,
                    options.suppress_dialogs,
                    options.store_unexpected_output,
                    options.repeat,
                    options.abort_on_timeout)

  # Get status for tests
  sections = [ ]
  defs = { }
  root.GetTestStatus(context, sections, defs)
  config = Configuration(sections, defs)

  # List the tests
  all_cases = [ ]
  all_unused = [ ]
  unclassified_tests = [ ]
  globally_unused_rules = None
  for path in paths:
    for arch in options.arch:
      for mode in options.mode:
        vm = context.GetVm(arch, mode)
        if not exists(vm):
          print("Can't find shell executable: '%s'" % vm)
          continue
        archEngineContext = Execute([vm, "-p", "process.arch"], context)
        vmArch = archEngineContext.stdout.rstrip()
        if archEngineContext.exit_code != 0 or vmArch == "undefined":
          print("Can't determine the arch of: '%s'" % vm)
          print(archEngineContext.stderr.rstrip())
          continue
        env = {
          'mode': mode,
          'system': utils.GuessOS(),
          'arch': vmArch,
          'type': get_env_type(vm, options.type, context),
          'asan': get_asan_state(vm, context),
        }
        test_list = root.ListTests([], path, context, arch, mode)
        unclassified_tests += test_list
        cases, unused_rules = config.ClassifyTests(test_list, env)
        if globally_unused_rules is None:
          globally_unused_rules = set(unused_rules)
        else:
          globally_unused_rules = (
              globally_unused_rules.intersection(unused_rules))
        all_cases += cases
        all_unused.append(unused_rules)

  # We want to skip the inspector tests if node was built without the inspector.
  has_inspector = Execute([vm,
      '-p', 'process.features.inspector'], context)
  if has_inspector.stdout.rstrip() == 'false':
    context.v8_enable_inspector = False

  has_crypto = Execute([vm,
      '-p', 'process.versions.openssl'], context)
  if has_crypto.stdout.rstrip() == 'undefined':
    context.node_has_crypto = False

  if options.cat:
    visited = set()
    for test in unclassified_tests:
      key = tuple(test.path)
      if key in visited:
        continue
      visited.add(key)
      print("--- begin source: %s ---" % test.GetLabel())
      source = test.GetSource().strip()
      print(source)
      print("--- end source: %s ---" % test.GetLabel())
    return 0

  if options.warn_unused:
    for rule in globally_unused_rules:
      print("Rule for '%s' was not used." % '/'.join([str(s) for s in rule.path]))

  tempdir = os.environ.get('NODE_TEST_DIR') or options.temp_dir
  if tempdir:
    os.environ['NODE_TEST_DIR'] = tempdir
    try:
      os.makedirs(tempdir)
    except OSError as exception:
      if exception.errno != errno.EEXIST:
        print("Could not create the temporary directory", options.temp_dir)
        sys.exit(1)

  def should_keep(case):
    if any((s in case.file) for s in options.skip_tests):
      return False
    elif SKIP in case.outcomes:
      return False
    elif (options.flaky_tests == SKIP) and (set([SLOW, FLAKY]) & case.outcomes):
      return False
    else:
      return True

  cases_to_run = [
    test_case for test_case in all_cases if should_keep(test_case)
  ]

  if options.report:
    print(REPORT_TEMPLATE % {
      'total': len(all_cases),
      'skipped': len(all_cases) - len(cases_to_run),
      'pass': len([t for t in cases_to_run if PASS in t.outcomes]),
      'fail_ok': len([t for t in cases_to_run if t.outcomes == set([FAIL, OKAY])]),
      'fail': len([t for t in cases_to_run if t.outcomes == set([FAIL])])
    })

  if options.run is not None:
    # Must ensure the list of tests is sorted before selecting, to avoid
    # silent errors if this file is changed to list the tests in a way that
    # can be different in different machines
    cases_to_run.sort(key=lambda c: (c.arch, c.mode, c.file))
    cases_to_run = [ cases_to_run[i] for i
                     in range(options.run[0],
                               len(cases_to_run),
                               options.run[1]) ]
  if len(cases_to_run) == 0:
    print("No tests to run.")
    return 1
  else:
    try:
      start = time.time()
      result = RunTestCases(cases_to_run, options.progress, options.j, options.flaky_tests, options.measure_flakiness)
      exitcode = 0 if result['allPassed'] else 1
      duration = time.time() - start
    except KeyboardInterrupt:
      print("Interrupted")
      return 1

  if options.time:
    # Write the times to stderr to make it easy to separate from the
    # test output.
    print()
    sys.stderr.write("--- Total time: %s ---\n" % FormatTime(duration))
    timed_tests = [ t for t in cases_to_run if not t.duration is None ]
    timed_tests.sort(key=lambda x: x.duration)
    for i, entry in enumerate(timed_tests[:20], start=1):
      t = FormatTimedelta(entry.duration)
      sys.stderr.write("%4i (%s) %s\n" % (i, t, entry.GetLabel()))

  if result['allPassed']:
    print("\nAll tests passed.")
  else:
    print("\nFailed tests:")
    for failure in result['failed']:
      print(EscapeCommand(failure.command))

  return exitcode


if __name__ == '__main__':
  sys.exit(Main())
