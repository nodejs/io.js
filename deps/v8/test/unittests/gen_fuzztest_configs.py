#!/usr/bin/env python3
#
# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""List all available fuzz tests and create wrapper scripts for each.

Invoked by GN depending on the unit-tests target. Expected to be called
from the root out dir of GN.

Writes the wrappers and 'fuzztests.stamp' into the ./fuzztests/ directory.
"""

import os
import re
import shutil
import stat
import subprocess
import sys

from pathlib import Path, PurePath

# Set up path to be able to import action_helpers
BASE_DIR = Path(__file__).absolute().parent.parent.parent
sys.path.append(str(BASE_DIR / 'build'))
import action_helpers

CENTIPEDE = 'centipede'
EXECUTABLE = 'v8_unittests'
FUZZ_TEST_DIR = 'fuzztests'
FUZZ_TEST_STAMP = 'fuzztests.stamp'

# When this script is enabled, we expect to find at least the demo tests.
MIN_FUZZTESTS = 2

# This is an arbitrary safeguard. If we run into it and it's legit,
# just double it.
MAX_FUZZTESTS = 100

WRAPPER_HEADER = """
#!/bin/sh
BINARY_DIR="$(cd "${{0%/*}}"/..; pwd)"
cd $BINARY_DIR
""".strip()

CENTIPEDE_WRAPPER = WRAPPER_HEADER + """
exec $BINARY_DIR/centipede $@
"""

FUZZTEST_WRAPPER = WRAPPER_HEADER + """
# Normal fuzzing.
if [ "$#" -eq  "0" ]; then
   exec $BINARY_DIR/v8_unittests --fuzz={test} --corpus_database=""
fi
# Fuzztest replay.
if [ "$#" -eq  "1" ]; then
   unset CENTIPEDE_RUNNER_FLAGS
   FUZZTEST_REPLAY=$1 exec $BINARY_DIR/v8_unittests --fuzz={test} --corpus_database=""
fi
"""

FUZZER_NAME_RE = re.compile(r'^\w+\.\w+$')


def list_fuzz_tests(executable):
  env = os.environ
  env['ASAN_OPTIONS'] = 'detect_odr_violation=0'
  env['CENTIPEDE_RUNNER_FLAGS'] = 'stack_limit_kb=0:'
  test_list = subprocess.check_output(
      [executable, '--list_fuzz_tests=true'],
      env=env,
      cwd='.',
  ).decode('utf-8')
  return sorted(set(re.findall('Fuzz test: (.*)', test_list)))


def fuzz_test_to_file_name(test):
  assert FUZZER_NAME_RE.match(test)
  fuzztest_name = re.sub(r'((f|F)uzz(t|T)est|(t|T)est)', '', test)
  fuzztest_name = re.sub(r'\.', ' ', fuzztest_name)
  fuzztest_name = re.sub('([A-Z]+)', r' \1', fuzztest_name)
  fuzztest_name = re.sub('([A-Z][a-z]+)', r' \1', fuzztest_name)
  splitted = fuzztest_name.split()
  splitted = map(str.lower, splitted)
  splitted = filter(bool, splitted)
  return 'v8_' + '_'.join(splitted) + '_fuzztest'


def create_wrapper(file_name, template, test=''):
  with action_helpers.atomic_output(file_name) as f:
    wrapper = template.format(test=test)
    f.write(wrapper.encode('utf-8'))

  # Make the wrapper world-executable.
  st = os.stat(file_name)
  m = st.st_mode
  os.chmod(file_name, m | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def setup_fuzztests_dir(cwd):
  # This script owns the fuzztests subdirectory. Purge everything
  # to enable consistent incremental builds.
  fuzz_test_dir = cwd / FUZZ_TEST_DIR
  shutil.rmtree(fuzz_test_dir, ignore_errors=True)
  fuzz_test_dir.mkdir(exist_ok=True)

  # Centipede is later expected to be side-by-side with the fuzz-test
  # targets. Create a bash redirect so that shared libraries in cwd
  # keep loading.
  create_wrapper(fuzz_test_dir / CENTIPEDE, CENTIPEDE_WRAPPER)

  return fuzz_test_dir


def create_fuzztest_wrapper(fuzz_test_dir, test_name):
  fuzztest_path = fuzz_test_dir / fuzz_test_to_file_name(test_name)
  create_wrapper(fuzztest_path, FUZZTEST_WRAPPER, test_name)


def main():
  # The CWD is expected to be the root out dir of GN.
  cwd = Path(os.getcwd())

  # We expect the unit-test executable present in the root dir.
  executable = cwd / EXECUTABLE
  assert executable.exists()

  fuzz_test_dir = setup_fuzztests_dir(cwd)
  fuzz_tests = list_fuzz_tests(executable)
  assert MIN_FUZZTESTS <= len(fuzz_tests) <= MAX_FUZZTESTS

  for test_name in fuzz_tests:
    create_fuzztest_wrapper(fuzz_test_dir, test_name)

  # This is a place holder telling GN that we're done.
  with action_helpers.atomic_output(fuzz_test_dir / FUZZ_TEST_STAMP) as f:
    f.write('\n'.join(fuzz_tests).encode('utf-8'))


if __name__ == '__main__':
  main()
