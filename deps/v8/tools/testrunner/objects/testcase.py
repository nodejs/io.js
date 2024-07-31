# Copyright 2012 the V8 project authors. All rights reserved.
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

import copy
import os
import re
import shlex

from pathlib import Path

from testrunner.outproc import base as outproc
from testrunner.local import command
from testrunner.local import statusfile
from testrunner.local import utils
from testrunner.local.variants import ALL_VARIANT_FLAGS
from testrunner.local.variants import INCOMPATIBLE_FLAGS_PER_VARIANT
from testrunner.local.variants import INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE
from testrunner.local.variants import INCOMPATIBLE_FLAGS_PER_EXTRA_FLAG


FLAGS_PATTERN = re.compile(r"//\s+Flags:(.*)")

# Patterns for additional resource files on Android. Files that are not covered
# by one of the other patterns below will be specified in the resources section.
RESOURCES_PATTERN = re.compile(r"//\s+Resources:(.*)")
# Pattern to auto-detect files to push on Android for statements like:
# load("path/to/file.js")
# d8.file.execute("path/to/file.js")
LOAD_PATTERN = re.compile(
    r"(?:execute|load|readbuffer|read)\((?:'|\")([^'\"]+)(?:'|\")\)")
# Pattern to auto-detect files to push on Android for statements like:
# import foobar from "path/to/file.js"
# import {foo, bar} from "path/to/file.js"
# export {"foo" as "bar"} from "path/to/file.js"
MODULE_FROM_RESOURCES_PATTERN = re.compile(
    r"(?:import|export).*?from\s*\(?['\"]([^'\"]+)['\"]",
    re.MULTILINE | re.DOTALL)
# Pattern to detect files to push on Android for statements like:
# import "path/to/file.js"
# import("module.mjs").catch()...
# Require the matched path in one line. Note this might include some
# false matches, which is safe, since files are tested for existence.
MODULE_IMPORT_RESOURCES_PATTERN = re.compile(
    r"import\s*\(?['\"]([^'\"\n]+)['\"]",
    re.MULTILINE)
# Pattern to detect files to push on Android for expressions like:
# shadowRealm.importValue("path/to/file.js", "obj")
SHADOWREALM_IMPORTVALUE_RESOURCES_PATTERN = re.compile(
    r"(?:importValue)\((?:'|\")([^'\"]+)(?:'|\")", re.MULTILINE | re.DOTALL)
# Pattern to detect and strip test262 frontmatter from tests to prevent false
# positives for MODULE_RESOURCES_PATTERN above.
TEST262_FRONTMATTER_PATTERN = re.compile(r"/\*---.*?---\*/", re.DOTALL)

TIMEOUT_LONG = "long"

def read_file(file):
  with open(file, encoding='ISO-8859-1') as f:
    return f.read()

class TestCase(object):

  def __init__(self, suite, path, name):
    self.suite = suite

    # Path (pathlib) with the relative test path, e.g. 'test-api/foo'.
    self.path = Path(path)

    # Sting with a posix path to identify test in the status file and
    # at the command line.
    self.name = name

    # String that identifies subtests.
    self.subtest_id = None

    # Name of the used testing variant.
    self.variant = None

    # List of strings, flags specific to this test.
    self.variant_flags = []

    # Fields used by the test processors.
    self.origin = None # Test that this test is subtest of.
    # Processor that created this subtest, initialised to a default value
    self.processor = DuckProcessor()
    self.procid = '%s/%s' % (self.suite.name, self.name) # unique id
    self.keep_output = False # Can output of this test be dropped
    self._random_seed = None # Overrides test config value if not None

    # Outcomes
    self._statusfile_outcomes = None
    self._expected_outcomes = None
    self._checked_flag_contradictions = False
    self._statusfile_flags = None
    self.expected_failure_reason = None

    self._prepare_outcomes()

  def create_subtest(self, processor, subtest_id, variant=None, flags=None,
                     keep_output=False, random_seed=None):
    subtest = copy.copy(self)
    subtest.origin = self
    subtest.processor = processor
    subtest.subtest_id = subtest_id
    subtest.procid += f'.{subtest.processor_name}-{subtest_id}'
    subtest.keep_output |= keep_output
    if random_seed:
      subtest._random_seed = random_seed
    if flags:
      subtest.variant_flags = subtest.variant_flags + flags
    if variant is not None:
      assert self.variant is None
      subtest.variant = variant
      subtest._prepare_outcomes()
    return subtest

  def _prepare_outcomes(self, force_update=True):
    if force_update or self._statusfile_outcomes is None:
      def is_flag(outcome):
        return outcome.startswith('--')
      def not_flag(outcome):
        return not is_flag(outcome)

      outcomes = self.suite.statusfile_outcomes(self.name, self.variant)
      self._statusfile_outcomes = list(filter(not_flag, outcomes))
      self._statusfile_flags = list(filter(is_flag, outcomes))
    self._expected_outcomes = (
      self._parse_status_file_outcomes(self._statusfile_outcomes))

  def _parse_status_file_outcomes(self, outcomes):
    if (statusfile.FAIL_SLOPPY in outcomes and
        '--use-strict' not in self.variant_flags):
      return outproc.OUTCOMES_FAIL

    expected_outcomes = []
    if (statusfile.FAIL in outcomes or
        statusfile.FAIL_OK in outcomes):
      expected_outcomes.append(statusfile.FAIL)
    if statusfile.CRASH in outcomes:
      expected_outcomes.append(statusfile.CRASH)

    # Do not add PASS if there is nothing else. Empty outcomes are converted to
    # the global [PASS].
    if expected_outcomes and statusfile.PASS in outcomes:
      expected_outcomes.append(statusfile.PASS)

    # Avoid creating multiple instances of a list with a single FAIL.
    if expected_outcomes == outproc.OUTCOMES_FAIL:
      return outproc.OUTCOMES_FAIL
    return expected_outcomes or outproc.OUTCOMES_PASS

  def allow_timeouts(self):
    if self.expected_outcomes == outproc.OUTCOMES_PASS:
      self._expected_outcomes = outproc.OUTCOMES_PASS_OR_TIMEOUT
    elif self.expected_outcomes == outproc.OUTCOMES_FAIL:
      self._expected_outcomes = outproc.OUTCOMES_FAIL_OR_TIMEOUT
    elif statusfile.TIMEOUT not in self.expected_outcomes:
      self._expected_outcomes = (
          self.expected_outcomes + [statusfile.TIMEOUT])

  def allow_pass(self):
    if self.expected_outcomes == outproc.OUTCOMES_TIMEOUT:
      self._expected_outcomes = outproc.OUTCOMES_PASS_OR_TIMEOUT
    elif self.expected_outcomes == outproc.OUTCOMES_FAIL:
      self._expected_outcomes = outproc.OUTCOMES_FAIL_OR_PASS
    elif statusfile.PASS not in self.expected_outcomes:
      self._expected_outcomes = (
          self.expected_outcomes + [statusfile.PASS])

  @property
  def expected_outcomes(self):
    def is_flag(maybe_flag):
      return maybe_flag.startswith("--")  # Best-effort heuristic.

    # Filter to flags, e.g.: ["--foo", "3", "--bar"] -> ["--foo", "--bar"].
    def filter_flags(normalized_flags):
      return [f for f in normalized_flags if is_flag(f)];

    def normalize_flag(flag):
      return flag.replace("_", "-").replace("--no-", "--no")

    def normalize_flags(flags):
      return [normalize_flag(flag) for flag in filter_flags(flags)]

    # Note this can get it wrong if the flag name starts with the characters
    # "--no" where "no" is part of the flag name, e.g. "--nobodys-perfect".
    # In that case the negation "--bodys-perfect" would be returned. This is
    # a weakness we accept and hope to never run into.
    def negate_flag(normalized_flag):
      return ("--" + normalized_flag[4:] if normalized_flag.startswith("--no")
              else "--no" + normalized_flag[2:])

    def negate_flags(normalized_flags):
      return [negate_flag(flag) for flag in normalized_flags]

    def has_flag(conflicting_flag, flags):
      conflicting_flag = normalize_flag(conflicting_flag)
      if conflicting_flag in flags:
        return True
      if conflicting_flag.endswith("*"):
        return any(flag.startswith(conflicting_flag[:-1]) for flag in flags)
      return False

    def check_flags(incompatible_flags, actual_flags, rule):
      for incompatible_flag in incompatible_flags:
        if has_flag(incompatible_flag, actual_flags):
          self._statusfile_outcomes = outproc.OUTCOMES_FAIL
          self._expected_outcomes = outproc.OUTCOMES_FAIL
          self.expected_failure_reason = (
              "Rule " + rule + " in " +
              "tools/testrunner/local/variants.py expected a flag " +
              "contradiction error with " + incompatible_flag + ".")

    if not self._checked_flag_contradictions:
      self._checked_flag_contradictions = True

      file_specific_flags = (self._get_source_flags() + self._get_suite_flags()
                             + self._get_statusfile_flags())
      file_specific_flags = normalize_flags(file_specific_flags)
      extra_flags = normalize_flags(self._get_extra_flags())

      # Contradiction: flags contains both a flag --foo and its negation
      # --no-foo.
      if self.variant in ALL_VARIANT_FLAGS:
        for flags in ALL_VARIANT_FLAGS[self.variant]:
          all_flags = (file_specific_flags + extra_flags
                       + normalize_flags(flags))
          check_flags(negate_flags(all_flags), all_flags, "Flag negations")

      # Contradiction: flags specified through the "Flags:" annotation are
      # incompatible with the variant.
      if self.variant in INCOMPATIBLE_FLAGS_PER_VARIANT:
        check_flags(INCOMPATIBLE_FLAGS_PER_VARIANT[self.variant], file_specific_flags,
                    "INCOMPATIBLE_FLAGS_PER_VARIANT[\""+self.variant+"\"]")

      # Contradiction: flags specified through the "Flags:" annotation are
      # incompatible with the build.
      for variable, incompatible_flags in INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE.items():
        if variable.startswith("!"):
          # `variable` is negated, apply the rule if the build variable is NOT set.
          if not self.suite.statusfile.variables[variable[1:]]:
            check_flags(
                incompatible_flags, file_specific_flags,
                "INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE[\"" + variable + "\"]")
        else:
          if self.suite.statusfile.variables[variable]:
            check_flags(
                incompatible_flags, file_specific_flags,
                "INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE[\"" + variable + "\"]")

      # Contradiction: flags passed through --extra-flags are incompatible.
      for extra_flag, incompatible_flags in INCOMPATIBLE_FLAGS_PER_EXTRA_FLAG.items():
        if has_flag(extra_flag, extra_flags):
          check_flags(
              incompatible_flags, file_specific_flags,
              "INCOMPATIBLE_FLAGS_PER_EXTRA_FLAG[\"" + extra_flag + "\"]")
    return self._expected_outcomes

  @property
  def test_config(self):
    return self.suite.test_config

  @property
  def framework_name(self):
    return self.test_config.framework_name

  @property
  def shard_id(self):
    return self.test_config.shard_id

  @property
  def shard_count(self):
    return self.test_config.shard_count

  @property
  def do_skip(self):
    return (statusfile.SKIP in self._statusfile_outcomes and
            not self.test_config.run_skipped)

  @property
  def is_heavy(self):
    return statusfile.HEAVY in self._statusfile_outcomes

  @property
  def is_slow(self):
    return statusfile.SLOW in self._statusfile_outcomes

  @property
  def is_fail_ok(self):
    return statusfile.FAIL_OK in self._statusfile_outcomes

  @property
  def is_pass_or_fail(self):
    return (statusfile.PASS in self._statusfile_outcomes and
            statusfile.FAIL in self._statusfile_outcomes and
            statusfile.CRASH not in self._statusfile_outcomes)

  @property
  def is_fail(self):
    return (statusfile.FAIL in self._statusfile_outcomes and
            statusfile.PASS not in self._statusfile_outcomes)

  @property
  def only_standard_variant(self):
    return statusfile.NO_VARIANTS in self._statusfile_outcomes

  @property
  def shell(self):
    return self.get_shell()

  def skip_rdb(self, result):
    return False

  def get_command(self, ctx):
    params = self._get_cmd_params()
    env = self._get_cmd_env()
    shell_flags = self._get_shell_flags()
    timeout = self._get_timeout(params)
    return self._create_cmd(ctx, shell_flags + params, env, timeout)

  def _get_cmd_params(self):
    """Gets all command parameters and combines them in the following order:
      - files [empty by default]
      - all flags
    """
    return (self._get_files_params() + self.get_flags())

  def get_flags(self):
    """Gets all flags and combines them in the following order:
      - random seed
      - mode flags (based on chosen mode)
      - extra flags (from command line)
      - user flags (variant/fuzzer flags)
      - source flags (from source code) [empty by default]
      - test-suite flags
      - statusfile flags

    The best way to modify how parameters are created is to only override
    methods for getting partial parameters.
    """
    return (
        self._get_random_seed_flags() +
        self._get_mode_flags() +
        self._get_extra_flags() +
        self._get_variant_flags() +
        self._get_source_flags() +
        self._get_suite_flags() +
        self._get_statusfile_flags()
    )

  def _get_cmd_env(self):
    return {}

  def _get_files_params(self):
    return []

  def _get_timeout_param(self):
    return None

  def _get_random_seed_flags(self):
    return ['--random-seed=%d' % self.random_seed]

  @property
  def random_seed(self):
    return self._random_seed or self.test_config.random_seed

  def _get_extra_flags(self):
    return self.test_config.extra_flags

  def _get_variant_flags(self):
    return self.variant_flags

  def _get_statusfile_flags(self):
    """Gets runtime flags from a status file.

    Every outcome that starts with "--" is a flag.
    """
    return self._statusfile_flags

  def _get_mode_flags(self):
    return self.test_config.mode_flags

  def _get_source_flags(self):
    return []

  def _get_suite_flags(self):
    return []

  def _get_shell_flags(self):
    return []

  def _get_timeout(self, params):
    timeout = self.test_config.timeout
    if "--jitless" in params:
      timeout *= 2
    if "--no-turbofan" in params:
      timeout *= 2
    if "--noenable-vfp3" in params:
      timeout *= 2
    if self._get_timeout_param() == TIMEOUT_LONG:
      timeout *= 10
    if self.is_slow:
      timeout *= 4
    return timeout

  def get_shell(self):
    raise NotImplementedError()

  def path_and_suffix(self, suffix):
    return self.path.with_name(self.path.name + suffix)

  @property
  def path_js(self):
    return self.path_and_suffix('.js')

  @property
  def path_mjs(self):
    return self.path_and_suffix('.mjs')

  def _create_cmd(self, ctx, params, env, timeout):
    return ctx.command(
        cmd_prefix=self.test_config.command_prefix,
        shell=ctx.platform_shell(self.get_shell(), params,
                                 self.test_config.shell_dir),
        args=params,
        env=env,
        timeout=timeout,
        verbose=self.test_config.verbose,
        test_case=self,
        handle_sigterm=True,
        log_process_stats=self.test_config.log_process_stats,
    )

  def _parse_source_flags(self, source=None):
    source = source or self.get_source()
    flags = []
    for match in re.findall(FLAGS_PATTERN, source):
      flags += shlex.split(match.strip())
    return flags

  def is_source_available(self):
    return self._get_source_path() is not None

  def get_source(self):
    return read_file(self._get_source_path())

  def _get_source_path(self):
    return None

  def skip_predictable(self):
    """Returns True if the test case is not suitable for predictable testing."""
    return True

  @property
  def output_proc(self):
    if self.expected_outcomes is outproc.OUTCOMES_PASS:
      return outproc.DEFAULT
    return outproc.OutProc(self.expected_outcomes)

  def __cmp__(self, other):
    # Make sure that test cases are sorted correctly if sorted without
    # key function. But using a key function is preferred for speed.
    def cmp(x, y):
      return (x > y) - (x < y)
    return cmp(
        (self.suite.name, self.name, self.variant),
        (other.suite.name, other.name, other.variant)
    )

  @property
  def full_name(self):
    return self.suite.name + '/' + self.name

  def __str__(self):
    return self.full_name

  def test_suffixes(self):
    suffixes = self.origin.test_suffixes() if self.origin else []
    current_suffix = self.processor.test_suffix(self)
    if current_suffix:
      suffixes.append(str(current_suffix))
    return suffixes

  @property
  def rdb_test_id(self):
    suffixes = '/'.join(self.test_suffixes())
    full_suffix = ('//' + suffixes) if suffixes else ''
    return self.full_name + full_suffix

  @property
  def processor_name(self):
    return self.processor.name

  def _get_resources_for_file(self, file):
    """Returns for a given file a list of absolute paths of files needed by the
    given file.
    """
    source = read_file(file)
    result = []
    def add_path(path):
      result.append(Path(path).resolve())
    def add_import_path(import_path):
      add_path(file.parent / import_path)
    def strip_test262_frontmatter(input):
      return TEST262_FRONTMATTER_PATTERN.sub('', input)
    for match in RESOURCES_PATTERN.finditer(source):
      # There are several resources per line. Relative to base dir.
      for path in match.group(1).strip().split():
        add_path(path)
    # Strip test262 frontmatter before looking for load() and import/export
    # statements.
    source = strip_test262_frontmatter(source)
    for match in LOAD_PATTERN.finditer(source):
      # Files in load statements are relative to base dir.
      add_path(match.group(1))
    # Imported files are relative to the file importing them.
    for match in MODULE_FROM_RESOURCES_PATTERN.finditer(source):
      add_import_path(match.group(1))
    for match in MODULE_IMPORT_RESOURCES_PATTERN.finditer(source):
      add_import_path(match.group(1))
    for match in SHADOWREALM_IMPORTVALUE_RESOURCES_PATTERN.finditer(source):
      add_import_path(match.group(1))
    return result

  def get_android_resources(self):
    """Returns a list of absolute paths with additional files needed by the
    test case.

    Used to push additional files to Android devices.
    """
    if not self._get_source_path():
      return []
    result = set()
    to_check = [self._get_source_path()]
    # Recurse over all files until reaching a fixpoint.
    while to_check:
      next_resource = to_check.pop()
      result.add(next_resource)
      for resource in self._get_resources_for_file(next_resource):
        # Only add files that exist on disc. The pattens we check for give some
        # false positives otherwise.
        if resource not in result and resource.exists():
          to_check.append(resource)
    return sorted(list(result))


class DuckProcessor:
  """Dummy default processor for original tests implemented by duck-typing."""

  def test_suffix(self, test):
    return None

  @property
  def name(self):
    return None


class D8TestCase(TestCase):
  def get_shell(self):
    return "d8"

  def _get_shell_flags(self):
    return ['--test']

  def _get_extra_flags(self):
    return self.test_config.extra_flags + self.test_config.extra_d8_flags

  def skip_predictable(self):
    """Returns True if the test case is not suitable for predictable testing."""
    return (statusfile.FAIL in self.expected_outcomes or
            self.output_proc.negative)
