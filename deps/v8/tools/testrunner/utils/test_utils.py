# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import contextlib
import json
import os
import shutil
import sys
import tempfile
import unittest

from contextlib import contextmanager
from dataclasses import dataclass
from io import StringIO
from mock import patch
from pathlib import Path

from testrunner.local.command import BaseCommand
from testrunner.objects import output
from testrunner.local.context import DefaultOSContext
from testrunner.local.pool import SingleThreadedExecutionPool
from testrunner.local.variants import REQUIRED_BUILD_VARIABLES

TOOLS_ROOT = Path(__file__).resolve().parent.parent.parent

TEST_DATA_ROOT = TOOLS_ROOT / 'testrunner' / 'testdata'
BUILD_CONFIG_BASE = TEST_DATA_ROOT / 'v8_build_config.json'

from testrunner.local import command
from testrunner.local import pool

@contextlib.contextmanager
def temp_dir():
  """Wrapper making a temporary directory available."""
  path = None
  try:
    path = Path(tempfile.mkdtemp('_v8_test'))
    yield path
  finally:
    if path:
      shutil.rmtree(path)


@contextlib.contextmanager
def temp_base(baseroot='testroot1'):
  """Wrapper that sets up a temporary V8 test root.

  Args:
    baseroot: The folder with the test root blueprint. All files will be
        copied to the temporary test root, to guarantee a fresh setup with no
        dirty state.
  """
  basedir = TEST_DATA_ROOT / baseroot
  with temp_dir() as tempbase:
    if basedir.exists():
      shutil.copytree(basedir, tempbase, dirs_exist_ok=True)
    yield tempbase


@contextlib.contextmanager
def capture():
  """Wrapper that replaces system stdout/stderr an provides the streams."""
  oldout = sys.stdout
  olderr = sys.stderr
  try:
    stdout=StringIO()
    stderr=StringIO()
    sys.stdout = stdout
    sys.stderr = stderr
    yield stdout, stderr
  finally:
    sys.stdout = oldout
    sys.stderr = olderr


def with_json_output(basedir):
  """ Function used as a placeholder where we need to resolve the value in the
  context of a temporary test configuration"""
  return basedir / 'out.json'

def clean_json_output(json_path, basedir):
  # Extract relevant properties of the json output.
  if not json_path:
    return None
  if not json_path.exists():
    return '--file-does-not-exists--'
  with open(json_path) as f:
    json_output = json.load(f)

  # Replace duration in actual output as it's non-deterministic. Also
  # replace the python executable prefix as it has a different absolute
  # path dependent on where this runs.
  def replace_variable_data(data):
    data['duration'] = 1
    data['max_rss'] = 1
    data['max_vms'] = 1
    data['command'] = ' '.join(
        ['/usr/bin/python'] + data['command'].split()[1:])
    data['command'] = data['command'].replace(f'{basedir}/', '')
  for container in [
      'max_rss_tests', 'max_vms_tests','slowest_tests', 'results']:
    for data in json_output[container]:
      replace_variable_data(data)
  json_output['duration_mean'] = 1
  # We need lexicographic sorting here to avoid non-deterministic behaviour
  # The original sorting key is duration or memory, but in our fake test we
  # have non-deterministic values before we reset them to 1.
  def sort_key(x):
    return str(sorted(x.items()))
  for container in [
      'max_rss_tests', 'max_vms_tests','slowest_tests']:
    json_output[container].sort(key=sort_key)
  return json_output


def test_schedule_log(json_path):
  if not json_path:
    return None
  with open(json_path.parent / 'test_schedule.log') as f:
    return f.read()


def setup_build_config(basedir, outdir):
  """Ensure a build config file exists - default or from test root."""
  path = basedir / outdir / 'build' / 'v8_build_config.json'
  if path.exists():
    return

  # Use default build-config blueprint.
  with open(BUILD_CONFIG_BASE) as f:
    config = json.load(f)

  # Add defaults for all variables used in variant configs.
  for key in REQUIRED_BUILD_VARIABLES:
    config[key] = False

  os.makedirs(path.parent, exist_ok=True)
  with open(path, 'w') as f:
    json.dump(config, f)

def override_build_config(basedir, **kwargs):
  """Override the build config with new values provided as kwargs."""
  if not kwargs:
    return
  path = basedir / 'out' / 'build' / 'v8_build_config.json'
  with open(path) as f:
    config = json.load(f)
    config.update(kwargs)
  with open(path, 'w') as f:
    json.dump(config, f)

@dataclass
class TestResult():
  stdout: str
  stderr: str
  returncode: int
  json: str
  test_schedule: str
  current_test_case: unittest.TestCase

  def __str__(self):
    return f'\nReturncode: {self.returncode}\nStdout:\n{self.stdout}\nStderr:\n{self.stderr}\n'

  def has_returncode(self, code):
    self.current_test_case.assertEqual(code, self.returncode, self)

  def stdout_includes(self, content):
    self.current_test_case.assertIn(content, self.stdout, self)

  def stdout_excludes(self, content):
    self.current_test_case.assertNotIn(content, self.stdout, self)

  def stderr_includes(self, content):
    self.current_test_case.assertIn(content, self.stderr, self)

  def stderr_excludes(self, content):
    self.current_test_case.assertNotIn(content, self.stderr, self)

  def json_content_equals(self, expected_results_file):
    with open(TEST_DATA_ROOT / expected_results_file) as f:
      expected_test_results = json.load(f)

    pretty_json = json.dumps(self.json, indent=2, sort_keys=True)
    msg = None  # Set to pretty_json for bootstrapping.
    self.current_test_case.assertDictEqual(self.json, expected_test_results, msg)


class TestRunnerTest(unittest.TestCase):
  @classmethod
  def setUpClass(cls):
    command.setup_testing()
    pool.setup_testing()

  def run_tests(
      self, *args, baseroot='testroot1', config_overrides=None,
      with_build_config=True, outdir='out', **kwargs):
    """Executes the test runner with captured output."""
    with temp_base(baseroot=baseroot) as basedir:
      if with_build_config:
        setup_build_config(basedir, outdir)
      override_build_config(basedir, **(config_overrides or {}))
      json_out_path = None
      def resolve_arg(arg):
        """Some arguments come as function objects to be called (resolved)
        in the context of a temporary test configuration"""
        nonlocal json_out_path
        if arg == with_json_output:
          json_out_path = with_json_output(basedir)
          return json_out_path
        return arg
      resolved_args = [resolve_arg(arg) for arg in args]
      with capture() as (stdout, stderr):
        sys_args = ['--command-prefix', sys.executable] + resolved_args
        if kwargs.get('infra_staging', False):
          sys_args.append('--infra-staging')
        else:
          sys_args.append('--no-infra-staging')
        runner = self.get_runner_class()(basedir=basedir)
        code = runner.execute(sys_args)
        json_out = clean_json_output(json_out_path, basedir)
        test_schedule = test_schedule_log(json_out_path)
        return TestResult(
            stdout.getvalue(), stderr.getvalue(), code, json_out, test_schedule, self)

  def get_runner_options(self, baseroot='testroot1'):
    """Returns a list of all flags parsed by the test runner."""
    with temp_base(baseroot=baseroot) as basedir:
      runner = self.get_runner_class()(basedir=basedir)
      parser = runner._create_parser()
      return [i.get_opt_string() for i in parser.option_list]

  def get_runner_class():
    """Implement to return the runner class"""
    return None

  @contextmanager
  def with_fake_rdb(self):
    records = []

    def fake_sink():
      return True

    class Fake_RPC:

      def __init__(self, sink):
        pass

      def send(self, r):
        records.append(r)

    with patch('testrunner.testproc.progress.rdb_sink', fake_sink), \
        patch('testrunner.testproc.resultdb.ResultDB_RPC', Fake_RPC):
      yield records


class FakeOSContext(DefaultOSContext):

  def __init__(self):
    super(FakeOSContext, self).__init__(FakeCommand,
                                        SingleThreadedExecutionPool())

  @contextmanager
  def handle_context(self, options):
    print("===>Starting stuff")
    yield
    print("<===Stopping stuff")

  def on_load(self):
    print("<==>Loading stuff")


class FakeCommand(BaseCommand):
  counter = 0

  def __init__(self,
               shell,
               args=None,
               cmd_prefix=None,
               timeout=60,
               env=None,
               verbose=False,
               test_case=None,
               handle_sigterm=False,
               log_process_stats=False):
    f_prefix = ['fake_wrapper'] + cmd_prefix
    super(FakeCommand, self).__init__(
        shell,
        args=args,
        cmd_prefix=f_prefix,
        timeout=timeout,
        env=env,
        verbose=verbose,
        handle_sigterm=handle_sigterm,
        log_process_stats=log_process_stats)

  def execute(self):
    FakeCommand.counter += 1
    return output.Output(
        0,  #return_code,
        False,  # TODO: Figure out timeouts.
        f'fake stdout {FakeCommand.counter}',
        f'fake stderr {FakeCommand.counter}',
        -1,  # No pid available.
        start_time=1,
        end_time=100,
    )
