# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Fork from commands.py and output.py in v8 test driver.

import signal
import subprocess
import sys

from pathlib import Path
from threading import Event, Timer

# List of default flags passed to each d8 run.
DEFAULT_FLAGS = [
    '--correctness-fuzzer-suppressions',
    '--expose-gc',
    '--fuzzing',
    '--allow-natives-for-differential-fuzzing',
    '--invoke-weak-callbacks',
    '--omit-quit',
    '--harmony',
    '--js-staging',
    '--wasm-staging',
    '--no-wasm-async-compilation',
    # Limit wasm memory to just below 2GiB, to avoid differences between 32-bit
    # and 64-bit builds.
    '--wasm-max-mem-pages=32767',
    '--suppress-asm-messages',
]

BASE_PATH = Path(__file__).parent.resolve()

# List of files passed to each d8 run before the testcase.
DEFAULT_MOCK = BASE_PATH / 'v8_mock.js'
SMOKE_TESTS = BASE_PATH / 'v8_smoke_tests.js'

# Suppressions on JavaScript level for known issues.
JS_SUPPRESSIONS = BASE_PATH / 'v8_suppressions.js'

# Config-specific mock files.
ARCH_MOCKS = BASE_PATH / 'v8_mock_archs.js'
WEBASSEMBLY_MOCKS = BASE_PATH / 'v8_mock_webassembly.js'

# Non-standard exit code only used to simulate crashes in tests.
CRASH_CODE_FOR_TESTING = 73


def _startup_files(options):
  """Default files and optional config-specific mock files."""
  files = [DEFAULT_MOCK]
  if not options.skip_suppressions:
    files.append(JS_SUPPRESSIONS)
  if options.first.arch != options.second.arch:
    files.append(ARCH_MOCKS)
  # Mock out WebAssembly when comparing with jitless mode.
  if '--jitless' in options.first.flags + options.second.flags:
    files.append(WEBASSEMBLY_MOCKS)
  # Keep the smoke tests last, right before the actual test case.
  return files + [SMOKE_TESTS]


class BaseException(Exception):
  """Used to abort the comparison workflow and print the given message."""
  def __init__(self, message):
    self.message = message


class PassException(BaseException):
  """Represents an early abort making the overall run pass."""
  pass


class FailException(BaseException):
  """Represents an early abort making the overall run fail."""
  pass


class Command(object):
  """Represents a configuration for running V8 multiple times with certain
  flags and files.
  """
  def __init__(self, options, label, executable, config_flags):
    self.label = label
    self.executable = executable
    self.config_flags = config_flags
    self.common_flags =  DEFAULT_FLAGS[:]
    self.common_flags.extend(['--random-seed', str(options.random_seed)])

    self.files = _startup_files(options)

  def run(self, testcase, timeout):
    """Run the executable with a specific testcase."""
    raw_args = [self.executable] + self.flags + self.files + [testcase]
    args = list(map(str, raw_args))
    print(f'# Command line for {self.label} comparison:')
    print(' '.join(args))
    if self.executable.suffix == '.py':
      # Wrap with python in tests.
      args = [sys.executable] + args
    return Execute(
        args,
        cwd=testcase.parent,
        timeout=timeout,
    )

  @property
  def flags(self):
    return self.common_flags + self.config_flags

  def remove_config_flag(self, remove_flag):
    """Removes all occurences of `remove_flag` from the config flags."""
    self.config_flags = [
        flag for flag in self.config_flags if flag != remove_flag
    ]


class Output(object):
  def __init__(self, exit_code, stdout_bytes, pid):
    self.exit_code = exit_code
    self.stdout_bytes = stdout_bytes
    self.pid = pid

  @property
  def stdout(self):
    try:
      return self.stdout_bytes.decode('utf-8')
    except UnicodeDecodeError:
      return self.stdout_bytes.decode('latin-1')

  def HasCrashed(self):
    return self.exit_code < 0 or self.exit_code == CRASH_CODE_FOR_TESTING


def Execute(args, cwd, timeout=None):
  popen_args = [c for c in args if c != ""]
  try:
    process = subprocess.Popen(
      args=popen_args,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      cwd=cwd,
    )
  except Exception as e:
    sys.stderr.write(f'Error executing: {popen_args}\n')
    raise e

  timeout_event = Event()

  def kill_process():
    timeout_event.set()
    try:
      process.kill()
    except OSError:
      sys.stderr.write(f'Error: Process {process.pid} already ended.\n')

  timer = Timer(timeout, kill_process)
  timer.start()
  stdout_bytes, _ = process.communicate()
  timer.cancel()

  if timeout_event.is_set():
    raise PassException('# V8 correctness - T-I-M-E-O-U-T')

  return Output(
      process.returncode,
      stdout_bytes,
      process.pid,
  )
