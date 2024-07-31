# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
from subprocess import STDOUT, PIPE, Popen

# This line is 'magic' in that git-cl looks for it to decide whether to
# use Python3 instead of Python2 when running the code in this file.
USE_PYTHON3 = True


def _CheckLint(input_api, output_api):
  root = pathlib.Path(input_api.PresubmitLocalPath())
  vpython_spec = root / '.lint-vpython3'
  workdir = root / 'data'
  lint_exe = workdir / 'tools' / 'lint' / 'lint.py'
  test_path = workdir / '..' / 'local-tests' / 'test'
  staging_path = (test_path / 'staging').relative_to(workdir)
  lint_exceptions = root / 'lint.exceptions'
  command = [
      'vpython3',
      '-vpython-spec',
      vpython_spec,
      lint_exe,
      '--exceptions',
      lint_exceptions,
      '--features',
      staging_path / 'features.txt',
      staging_path,
  ]
  proc = Popen(command, cwd=workdir, stderr=STDOUT, stdout=PIPE)
  output, exit_code = proc.communicate()[0], proc.returncode
  if exit_code == 0:
    return []
  return [
      output_api.PresubmitError(
          "Test262 lint failed.", long_text=output.decode())
  ]


def _PyUnitTest(input_api, output_api):
  return input_api.RunTests(
      input_api.canned_checks.GetUnitTestsRecursively(
          input_api,
          output_api,
          input_api.os_path.join(input_api.PresubmitLocalPath()),
          files_to_check=[r'.+_test\.py$'],
          files_to_skip=[],
          run_on_python2=False,
      ))

def _CommonChecks(input_api, output_api):
  checks = [
      _CheckLint,
      _PyUnitTest,
  ]
  return sum([check(input_api, output_api) for check in checks], [])


def CheckChangeOnCommit(input_api, output_api):
  return _CommonChecks(input_api, output_api)


def CheckChangeOnUpload(input_api, output_api):
  return _CommonChecks(input_api, output_api)
