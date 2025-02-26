#!/usr/bin/env python3
# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
V8 correctness fuzzer launcher script.
"""

import argparse
import hashlib
import json
import re
import sys
import traceback

from collections import namedtuple
from pathlib import Path

from v8_commands import Command, FailException, PassException
import v8_suppressions

CONFIGS = dict(
    default=[],
    ignition=[
        '--turbo-filter=~',
        '--no-turbofan',
        '--no-sparkplug',
        '--liftoff',
        '--no-wasm-tier-up',
        '--no-maglev',
    ],
    ignition_asm=[
        '--turbo-filter=~',
        '--no-turbofan',
        '--no-sparkplug',
        '--validate-asm',
        '--stress-validate-asm',
        '--no-maglev',
    ],
    ignition_eager=[
        '--turbo-filter=~',
        '--no-turbofan',
        '--no-sparkplug',
        '--no-lazy',
        '--no-lazy-inner-functions',
        '--no-maglev',
    ],
    ignition_no_ic=[
        '--turbo-filter=~',
        '--no-turbofan',
        '--no-sparkplug',
        '--liftoff',
        '--no-wasm-tier-up',
        '--no-use-ic',
        '--no-lazy-feedback-allocation',
        '--no-maglev',
    ],
    ignition_turbo=[],
    ignition_turbo_no_ic=[
        '--no-use-ic',
    ],
    ignition_turbo_opt=[
        '--always-turbofan',
        '--no-liftoff',
    ],
    ignition_turbo_opt_eager=[
        '--always-turbofan',
        '--no-lazy',
        '--no-lazy-inner-functions',
    ],
    ignition_maglev=[
        '--maglev',
        '--turbo-filter=~',
        '--no-turbofan',
    ],
    jitless=[
        '--jitless',
    ],
    slow_path=[
        '--force-slow-path',
    ],
    slow_path_opt=[
        '--always-turbofan',
        '--force-slow-path',
    ],
)

BASELINE_CONFIG = 'ignition'
DEFAULT_CONFIG = 'default'
DEFAULT_D8 = 'd8'

# Return codes.
RETURN_PASS = 0
RETURN_FAIL = 2

BASE_PATH = Path(__file__).parent.resolve()

# Timeout for one d8 run.
TEST_TIMEOUT_SEC = 3

SUPPORTED_ARCHS = ['ia32', 'x64', 'arm', 'arm64']

FAILURE_HEADER_TEMPLATE = """#
# V8 correctness failure
# V8 correctness sources: %(source_key)s
# V8 correctness suppression: %(suppression)s
"""

# Extended output for failure case. The 'CHECK' is for the minimizer.
DETAILS_TEMPLATE = """#
# CHECK
#
# Compared %(first_config_arch)s with %(second_config_arch)s
#
# Common flags:
%(common_flags)s
# Baseline flags:
%(first_config_flags)s
# Comparison flags:
%(second_config_flags)s
#
# Difference:
%(difference)s%(source_file_text)s
#
### Start of baseline output:
%(first_config_output)s
### End of baseline output
#
### Start of comparison output:
%(second_config_output)s
### End of comparison output
"""

FAILURE_TEMPLATE = FAILURE_HEADER_TEMPLATE + DETAILS_TEMPLATE

SOURCE_FILE_TEMPLATE = """
#
# Source file:
%s"""


FUZZ_TEST_RE = re.compile(r'.*fuzz(-\d+\.js)')

# The number of hex digits used from the hash of the original source file path.
# Keep the number small to avoid duplicate explosion.
SOURCE_HASH_LENGTH = 2

# Placeholder string if no original source file could be determined.
ORIGINAL_SOURCE_DEFAULT = 'none'

# Placeholder string for failures from crash tests. If a failure is found with
# this signature, the matching sources should be moved to the mapping below.
ORIGINAL_SOURCE_CRASHTESTS = 'placeholder for CrashTests'

# Mapping from relative original source path (e.g. CrashTests/path/to/file.js)
# to a string key. Map to the same key for duplicate issues. The key should
# have more than 3 characters to not collide with other existing hashes.
# If a symptom from a particular original source file is known to map to a
# known failure, it can be added to this mapping. This should be done for all
# failures from CrashTests, as those by default map to the placeholder above.
KNOWN_FAILURES = {
  # Foo.caller with asm.js: https://crbug.com/1042556
  'CrashTests/4782147262545920/494.js': '.caller',
  'CrashTests/5637524389167104/01457.js': '.caller',
  'CrashTests/5703451898085376/02176.js': '.caller',
  'CrashTests/4846282433495040/04342.js': '.caller',
  'CrashTests/5712410200899584/04483.js': '.caller',
  'v8/test/mjsunit/regress/regress-105.js': '.caller',
  # Flaky issue that almost never repros.
  'CrashTests/5694376231632896/1033966.js': 'flaky',
}

# Flags that crash smoke tests or produce non-deterministic output should
# not be used.
DISALLOWED_FLAGS = [
    # Bails out when sorting, leading to differences in sorted output.
    '--multi-mapped-mock-allocator',

    # TODO(https://crbug.com/1393020): Changes the global object.
    '--harmony-struct',

    # Cuts off stdout earlier in some situations and might alter the behavior
    # of neutered runtime functions.
    '--sandbox-testing',

    # Various non-deterministic stats.
    '--dump-counters',
    '--expose-statistics',
    '--log',
    '--rcs',
]

# The same as above, but prefixes that either match multiple flags or flags
# that assign values.
DISALLOWED_FLAG_PREFIXES = [
    # Already passed as a default flag. That default value should not be
    # overwritten in one of the runs.
    '--wasm-max-mem-pages=',
]

# List pairs of flags that lead to contradictory cycles, i.e.:
# A -> no-C and B -> C makes (A, B) contradictory.
# No need to list other contradictions, they are omitted by the
# --fuzzing flag).
CONTRADICTORY_FLAGS = [
  ('--always-turbofan', '--jitless'),
  ('--assert-types', '--stress-concurrent-inlining'),
  ('--assert-types', '--stress-concurrent-inlining-attach-code'),
  ('--jitless', '--stress-concurrent-inlining'),
  ('--jitless', '--stress-concurrent-inlining-attach-code'),
]


def is_disallowed_flag_prefix(flag):
  return any(flag.startswith(prefix) for prefix in DISALLOWED_FLAG_PREFIXES)

def filter_flags(flags):
  """Drop disallowed and contradictory flags.

  The precedence for contradictions is right to left, similar to the V8 test
  framework.
  """
  result = []
  flags_to_drop = set(DISALLOWED_FLAGS)
  for flag in reversed(flags):
    if flag in flags_to_drop or is_disallowed_flag_prefix(flag):
      continue
    result.append(flag)
    for contradicting_pair in CONTRADICTORY_FLAGS:
      if contradicting_pair[0] == flag:
        flags_to_drop.add(contradicting_pair[1])
      if contradicting_pair[1] == flag:
        flags_to_drop.add(contradicting_pair[0])
  return list(reversed(result))


def infer_arch(d8):
  """Infer the V8 architecture from the build configuration next to the
  executable.
  """
  with (d8.parent / 'v8_build_config.json').open() as f:
    arch = json.load(f)['v8_current_cpu']
  arch = 'ia32' if arch == 'x86' else arch
  assert arch in SUPPORTED_ARCHS
  return arch


class ExecutionArgumentsConfig(object):
  def __init__(self, label):
    self.label = label

  def add_arguments(self, parser, default_config):
    def add_argument(flag_template, help_template, **kwargs):
      parser.add_argument(
          flag_template % self.label,
          help=help_template % self.label,
          **kwargs)

    add_argument(
        '--%s-config',
        '%s configuration',
        default=default_config)
    add_argument(
        '--%s-config-extra-flags',
        'additional flags passed to the %s run',
        action='append',
        default=[])
    add_argument(
        '--%s-d8',
        'optional path to %s d8 executable, '
        'default: bundled in the directory of this script',
        default=DEFAULT_D8)

  def make_options(self, options, default_config=None, default_d8=None):
    def get(name):
      return getattr(options, f'{self.label}_{name}')

    config = default_config or get('config')
    assert config in CONFIGS

    d8 = Path(default_d8 or get('d8'))
    if not d8.is_absolute():
      d8 = BASE_PATH / d8
    assert d8.exists()

    flags = filter_flags(CONFIGS[config] + get('config_extra_flags'))

    RunOptions = namedtuple('RunOptions', ['arch', 'd8', 'flags'])
    return RunOptions(infer_arch(d8), d8, flags)


class ExecutionConfig(object):
  def __init__(self, options, label):
    self.options = options
    self.label = label
    self.arch = getattr(options, label).arch
    d8 = getattr(options, label).d8
    flags = getattr(options, label).flags
    self.command = Command(options, label, d8, flags)

    # Options for a fallback configuration only exist when comparing
    # different architectures.
    fallback_label = label + '_fallback'
    self.fallback = None
    if getattr(options, fallback_label, None):
      self.fallback = ExecutionConfig(options, fallback_label)

  @property
  def common_flags(self):
    return self.command.common_flags

  @property
  def config_flags(self):
    return self.command.config_flags

  @property
  def flags(self):
    return self.command.flags

  def has_config_flag(self, flag):
    return (flag in self.config_flags or
            self.fallback and self.fallback.has_config_flag(flag))

  def remove_config_flag(self, flag):
    self.command.remove_config_flag(flag)
    if self.fallback:
      self.fallback.remove_config_flag(flag)

  @property
  def is_error_simulation(self):
    return '--simulate-errors' in self.flags


def parse_args(args):
  first_config_arguments = ExecutionArgumentsConfig('first')
  second_config_arguments = ExecutionArgumentsConfig('second')

  parser = argparse.ArgumentParser()
  parser.add_argument(
    '--random-seed', type=int, required=True,
    help='random seed passed to both runs')
  parser.add_argument(
      '--skip-suppressions', default=False, action='store_true',
      help='skip suppressions to reproduce known issues')
  parser.add_argument(
      '--compact', default=False, action='store_true',
      help='deprecated: compact error reporting is the default')

  # Add arguments for each run configuration.
  first_config_arguments.add_arguments(parser, BASELINE_CONFIG)
  second_config_arguments.add_arguments(parser, DEFAULT_CONFIG)

  parser.add_argument('testcase', help='path to test case')
  options = parser.parse_args(args)

  # Ensure we have a test case.
  options.testcase = Path(options.testcase)
  assert options.testcase.is_file(), (
      f"Test case {options.testcase} doesn't exist")

  options.first = first_config_arguments.make_options(options)
  options.second = second_config_arguments.make_options(options)
  options.default = second_config_arguments.make_options(
      options, default_config=DEFAULT_CONFIG)

  # Use fallback configurations only on different architectures. In this
  # case we are going to re-test against the first architecture.
  if options.first.arch != options.second.arch:
    options.second_fallback = second_config_arguments.make_options(
        options, default_d8=options.first.d8)
    options.default_fallback = second_config_arguments.make_options(
        options, default_config=DEFAULT_CONFIG, default_d8=options.first.d8)

  # Ensure we make a valid comparison.
  if (options.first.d8 == options.second.d8 and
      options.first.flags == options.second.flags):
    parser.error('Need either executable or flag difference.')

  return options


def create_execution_configs(options):
  """Prepare the baseline, default and secondary configuration to compare to.

  The default (turbofan) takes precedence as many of the secondary configs
  are based on the turbofan config with additional parameters.
  """
  return [
      ExecutionConfig(options, 'first'),
      ExecutionConfig(options, 'default'),
      ExecutionConfig(options, 'second'),
  ]


def fail_bailout(output, ignore_by_output_fun):
  """Print failure state and return if ignore_by_output_fun matches output."""
  bug = (ignore_by_output_fun(output.stdout) or '').strip()
  if bug:
    raise FailException(FAILURE_HEADER_TEMPLATE % dict(
        source_key='', suppression=bug))


def format_difference(
    first_config, second_config,
    first_config_output, second_config_output,
    difference, source=None):
  # The first three entries will be parsed by clusterfuzz. Format changes
  # will require changes on the clusterfuzz side.
  source_file_text = SOURCE_FILE_TEMPLATE % source if source else ''

  assert first_config.common_flags == second_config.common_flags

  return FAILURE_TEMPLATE % dict(
      source_file_text=source_file_text,
      source_key=cluster_failures(source),
      suppression='',  # We can't tie bugs to differences.
      first_config_arch=first_config.arch,
      second_config_arch=second_config.arch,
      common_flags=' '.join(first_config.common_flags),
      first_config_flags=' '.join(first_config.config_flags),
      second_config_flags=' '.join(second_config.config_flags),
      first_config_output=first_config_output,
      second_config_output=second_config_output,
      source=source,
      difference=difference,
  )


def cluster_failures(source, known_failures=None):
  """Returns a string key for clustering duplicate failures.

  Args:
    source: The original source path where the failure happened.
    known_failures: Mapping from original source path to failure key.
  """
  known_failures = known_failures or KNOWN_FAILURES
  # No source known. Typical for manually uploaded issues. This
  # requires also manual issue creation.
  if not source:
    return ORIGINAL_SOURCE_DEFAULT
  # Source is known to produce a particular failure.
  if source in known_failures:
    return known_failures[source]
  # Subsume all other sources from CrashTests under one key. Otherwise
  # failures lead to new crash tests which in turn lead to new failures.
  if source.startswith('CrashTests'):
    return ORIGINAL_SOURCE_CRASHTESTS
  # Report all deviations from the smoke test as one to avoid a bug flood.
  if source == v8_suppressions.SMOKE_TEST_SOURCE:
    return 'smoke test failed'

  # We map all remaining failures to a short hash of the original source.
  long_key = hashlib.sha1(source.encode('utf-8')).hexdigest()
  return long_key[:SOURCE_HASH_LENGTH]


class RepeatedRuns(object):
  """Helper class for storing statistical data from repeated runs."""
  def __init__(self, test_case, timeout):
    self.test_case = test_case
    self.timeout = timeout

    # Stores if any run has crashed or was simulated.
    self.has_crashed = False
    self.simulated = False

  def run(self, config):
    comparison_output = config.command.run(
        self.test_case, timeout=self.timeout)
    self.has_crashed = self.has_crashed or comparison_output.HasCrashed()
    self.simulated = self.simulated or config.is_error_simulation
    return comparison_output

  @property
  def crash_state(self):
    return '_simulated_crash_' if self.simulated else '_unexpected_crash_'


def run_comparisons(suppress, execution_configs, test_case, timeout):
  """Runs different configurations and bails out on output difference.

  Args:
    suppress: The helper object for textual suppressions.
    execution_configs: Two or more configurations to run. The first one will be
        used as baseline to compare all others to.
    test_case: The test case to run.
    timeout: Timeout in seconds for one run.
  """
  runner = RepeatedRuns(test_case, timeout)

  # Run the baseline configuration.
  baseline_config = execution_configs[0]
  baseline_output = runner.run(baseline_config)

  # The output of the baseline run might change the shape of the remaining
  # configurations.
  remaining_configs = execution_configs[1:]
  changes = suppress.adjust_configs_by_output(
      remaining_configs, baseline_output.stdout)
  if changes:
    print('# Adjusted experiments based on baseline output:')
    print('\n'.join(changes))

  # Iterate over the remaining configurations, run and compare.
  for comparison_config in remaining_configs:
    comparison_output = runner.run(comparison_config)
    difference, source = suppress.diff(baseline_output, comparison_output)

    if difference:
      # Only bail out due to suppressed output if there was a difference. If a
      # suppression doesn't show up anymore in the statistics, we might want to
      # remove it.
      fail_bailout(baseline_output, suppress.ignore_by_output)
      fail_bailout(comparison_output, suppress.ignore_by_output)

      # Check if a difference also occurs with the fallback configuration and
      # give it precedence. E.g. we always prefer x64 differences.
      if comparison_config.fallback:
        fallback_output = runner.run(comparison_config.fallback)
        fallback_difference, fallback_source = suppress.diff(
            baseline_output, fallback_output)
        # Ensure also that the source doesn't differ, e.g. in case we have
        # a real failure in the cross-arch comparison, but find the smoke
        # tests failing in the fallback.
        if fallback_difference and source == fallback_source:
          fail_bailout(fallback_output, suppress.ignore_by_output)
          comparison_config = comparison_config.fallback
          comparison_output = fallback_output
          difference = fallback_difference

      # We print only output relevant to the source of the failure. E.g.
      # only print smoke-test output or real failure output.
      reduced_baseline_output = suppress.reduced_output(
          baseline_output.stdout, source)
      reduced_comparison_output = suppress.reduced_output(
          comparison_output.stdout, source)

      raise FailException(format_difference(
          baseline_config, comparison_config, reduced_baseline_output,
          reduced_comparison_output, difference, source))

    # Bail out if we crash during smoke tests already or if the crash is
    # expected based on a simulation flag.
    if (runner.has_crashed and (
        source == v8_suppressions.SMOKE_TEST_SOURCE or runner.simulated)):
      # Subsume such crashes with one failure state.
      raise FailException(FAILURE_HEADER_TEMPLATE % dict(
          source_key='', suppression=runner.crash_state))

  if runner.has_crashed:
    # Show if a crash has happened in one of the runs, but no difference was
    # detected. This is only for the statistics during experiments.
    raise PassException('# V8 correctness - C-R-A-S-H')


def main():
  options = parse_args(sys.argv[1:])
  suppress = v8_suppressions.get_suppression(options.skip_suppressions)

  # Static bailout based on test case content or metadata.
  with options.testcase.open(encoding='utf-8') as f:
    content = f.read()

  execution_configs = create_execution_configs(options)

  # Some configs will be dropped or altered based on the testcase content.
  changes = suppress.adjust_configs_by_content(execution_configs, content)
  if changes:
    print('# Adjusted flags and experiments based on the test case:')
    print('\n'.join(changes))

  # Run all configs against the fuzz test case.
  run_comparisons(
      suppress, execution_configs,
      test_case=options.testcase,
      timeout=TEST_TIMEOUT_SEC,
  )

  # TODO(machenbach): Figure out if we could also return a bug in case
  # there's no difference, but one of the line suppressions has matched -
  # and without the match there would be a difference.
  print('# V8 correctness - pass')
  return RETURN_PASS


if __name__ == "__main__":
  try:
    result = main()
  except FailException as e:
    print(e.message)
    result = RETURN_FAIL
  except PassException as e:
    print(e.message)
    result = RETURN_PASS
  except SystemExit:
    # Make sure clusterfuzz reports internal errors and wrong usage.
    # Use one label for all internal and usage errors.
    print(FAILURE_HEADER_TEMPLATE % dict(
        source_key='', suppression='wrong_usage'))
    result = RETURN_FAIL
  except MemoryError:
    # Running out of memory happens occasionally but is not actionable.
    print('# V8 correctness - pass')
    result = RETURN_PASS
  except Exception as e:
    print(FAILURE_HEADER_TEMPLATE % dict(
        source_key='', suppression='internal_error'))
    print(f'# Internal error: {e}')
    traceback.print_exc(file=sys.stdout)
    result = RETURN_FAIL

  sys.exit(result)
