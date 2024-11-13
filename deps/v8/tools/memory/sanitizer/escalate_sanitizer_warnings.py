#!/usr/bin/env python3

# Copyright 2024 the V8 project authors. All rights reserved.
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""This script searches for sanitizer warnings in stdout that do not trigger
test failures in process, and when it finds one, it updates the test status
to FAILURE for that test case.

This script is added in test_env.py after the test process runs, but before
the results are returned to result_adpater."""

# TODO(349962576): This is a temporary fork of Chromium's:
# tools/memory/sanitizer/escalate_sanitizer_warnings.py
# We should rather share the Chromium script or avoid the necessity to
# include it.

import argparse
import json
import re
import sys
"""
We are looking for various types of sanitizer warnings. They have different
formats but all end in a line 'SUMMARY: (...)Sanitizer:'.
asan example:
=================================================================
==244157==ERROR: AddressSanitizer: heap-use-after-free on address 0x60b0000000f0
...
SUMMARY: AddressSanitizer:

lsan example:
=================================================================
==23646==ERROR: LeakSanitizer: detected memory leaks
...
SUMMARY: AddressSanitizer: 7 byte(s) leaked in 1 allocation(s)

msan example:
==51936==WARNING: MemorySanitizer: use-of-uninitialized-value
...
SUMMARY: MemorySanitizer: use-of-uninitialized-value (/tmp/build/a.out+0x9fd9a)

tsan example:
==================
WARNING: ThreadSanitizer: data race (pid=14909)
...
SUMMARY: ThreadSanitizer: data race base/.../typed_macros_internal.cc:135:8...

ubsan example:
../../content/browser/.../render_widget_host_impl.cc:603:35: runtime error:
member call on address 0x28dc001c8a00 which does not point to an object of type
...
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior
"""
#pylint: disable=line-too-long
_SUMMARY_MESSAGE_STR = r'\nSUMMARY: (Address|Leak|Memory|Thread|UndefinedBehavior)Sanitizer:'
_SUMMARY_MESSAGE_REGEX = re.compile(_SUMMARY_MESSAGE_STR)


def escalate_test_status(test_name, test_run):
  original_status = test_run['status']
  # If test was not a SUCCESS, do not change it.
  if original_status != 'SUCCESS':
    return False

  regex_result = _SUMMARY_MESSAGE_REGEX.search(test_run['output_snippet'])
  if regex_result:
    sanitizer_type = regex_result.groups()[0]
    print('Found %sSanitizer Issue in test "%s"' % (sanitizer_type, test_name))
    test_run['original_status'] = test_run['status']
    test_run['status'] = 'FAILURE'
    test_run['status_processed_by'] = 'escalate_sanitizer_warnings.py'
    return True
  return False


def escalate_sanitizer_warnings(filename):
  with open(filename, 'r') as f:
    json_data = json.load(f)

  failed_test_names = []

  for iteration_data in json_data['per_iteration_data']:
    for test_name, test_runs in iteration_data.items():
      for test_run in test_runs:
        if escalate_test_status(test_name, test_run):
          failed_test_names.append(test_name)
  if not failed_test_names:
    return False
  print_sanitizer_results(failed_test_names)
  with open(filename, 'w') as f:
    json.dump(json_data, f, indent=3, sort_keys=True)
  return True


def print_sanitizer_results(failed_test_names):
  failure_count = len(failed_test_names)
  print('%d test%s failed via sanitizer warnings:' %
        (failure_count, ('s' if failure_count != 1 else '')))
  for failed_test_name in failed_test_names:
    print('    %s' % failed_test_name)


def main():
  parser = argparse.ArgumentParser(description='Escalate sanitizer warnings.')
  parser.add_argument(
      '--test-summary-json-file',
      required=True,
      help='Path to a JSON file produced by the test launcher. The script will '
      'parse output snippets to find sanitizer warnings that are shown as '
      'WARNINGS but should cause build failures in sanitizer versions. The '
      'status will be FAILED when found. The result will be written back '
      'to the JSON file.')
  args = parser.parse_args()

  if escalate_sanitizer_warnings(args.test_summary_json_file):
    # If tests failed due to sanitizer warnings, exit with 1 returncode to
    # influence task state.
    return 1


if __name__ == '__main__':
  sys.exit(main())
