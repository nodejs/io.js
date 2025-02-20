#!/usr/bin/env python3
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Wrapper script for verify-predictable mode. D8 is expected to be compiled with
v8_enable_verify_predictable.

The actual test command is expected to be passed to this wraper as is. E.g.:
predictable_wrapper.py path/to/d8 --test --predictable --flag1 --flag2

The command is run up to three times and the printed allocation hash is
compared. Differences are reported as errors.
"""

import sys

from testrunner.local import command
from testrunner.local import utils


MAX_TRIES = 7
TIMEOUT = 120

# Predictable mode works only when run on the host os.
command.setup(utils.GuessOS(), None)

def maybe_decode(message):
  if not isinstance(message, str):
    return message.decode()
  return message


def main(args):
  def allocation_str(stdout):
    for line in reversed((stdout or '').splitlines()):
      if maybe_decode(line).startswith('### Allocations = '):
        return line
    return None

  cmd = command.Command(
      args[0], args[1:], timeout=TIMEOUT, handle_sigterm=True)

  previous_allocations = None
  for run in range(1, MAX_TRIES + 1):
    print('### Predictable run #%d' % run)
    output = cmd.execute()
    if output.stdout:
      print('### Stdout:')
      print(output.stdout)
    if output.stderr:
      print('### Stderr:')
      print(output.stderr)
    print('### Return code: %s' % output.exit_code)
    if output.HasTimedOut():
      # If we get a timeout in any run, we are in an unpredictable state. Just
      # report it as a failure and don't rerun.
      print('### Test timed out')
      return 1
    allocations = allocation_str(output.stdout)
    if not allocations:
      print ('### Test had no allocation output. Ensure this is built '
             'with v8_enable_verify_predictable and that '
             '--verify-predictable is passed at the cmd line.')
      return 2
    if previous_allocations and previous_allocations != allocations:
      print('### Allocations differ')
      return 3
    if run >= MAX_TRIES:
      # No difference on the last run -> report a success.
      return 0
    previous_allocations = allocations
  # Unreachable.
  assert False


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
