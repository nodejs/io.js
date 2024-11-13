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

import importlib.machinery
import sys

from functools import cached_property
from pathlib import Path

from testrunner.local import statusfile
from testrunner.local import testsuite
from testrunner.objects import testcase
from testrunner.outproc import base as outproc
from testrunner.outproc import test262


# TODO(littledan): move the flag mapping into the status file
#
# Multiple flags are allowed, separated by space.
FEATURE_FLAGS = {
    'Intl.DurationFormat': '--harmony-intl-duration-format',
    'Intl.Locale-info': '--harmony-intl-locale-info-func',
    'FinalizationRegistry': '--harmony-weak-refs-with-cleanup-some',
    'WeakRef': '--harmony-weak-refs-with-cleanup-some',
    'host-gc-required': '--expose-gc-as=v8GC',
    'IsHTMLDDA': '--allow-natives-syntax',
    'import-assertions': '--harmony-import-assertions',
    'Temporal': '--harmony-temporal',
    'array-find-from-last': '--harmony-array-find-last',
    'ShadowRealm': '--harmony-shadow-realm',
    'regexp-v-flag': '--harmony-regexp-unicode-sets',
    'String.prototype.isWellFormed': '--harmony-string-is-well-formed',
    'String.prototype.toWellFormed': '--harmony-string-is-well-formed',
    'json-parse-with-source': '--harmony-json-parse-with-source',
    'iterator-helpers': '--harmony-iterator-helpers',
    'set-methods': '--harmony-set-methods',
    'import-attributes': '--harmony-import-attributes',
    'regexp-duplicate-named-groups': '--js-regexp-duplicate-named-groups',
    'regexp-modifiers': '--js-regexp-modifiers',
    'Float16Array': '--js-float16array',
    'explicit-resource-management': '--js_explicit_resource_management',
    'decorators': '--js-decorators',
    'promise-try': '--js-promise-try',
    'Atomics.pause': '--js-atomics-pause',
    'source-phase-imports': '--js-source-phase-imports --allow-natives-syntax',
}

SKIPPED_FEATURES = set([])

TEST262_DIR = Path(__file__).resolve().parent
BASE_DIR = TEST262_DIR.parents[1]

TEST_262_HARNESS_FILES = ["sta.js", "assert.js"]
TEST_262_NATIVE_FILES = ["detachArrayBuffer.js"]

TEST_262_SUITE_PATH = Path("data") / "test"
TEST_262_HARNESS_PATH = Path("data") / "harness"
TEST_262_TOOLS_ABS_PATH = BASE_DIR / "third_party" / "test262-harness" / "src"
TEST_262_LOCAL_TESTS_PATH = Path("local-tests") / "test"

sys.path.append(str(TEST_262_TOOLS_ABS_PATH))


class VariantsGenerator(testsuite.VariantsGenerator):
  def gen(self, test):
    flags_set = self._get_flags_set(test)
    test_record = test.test_record

    # Add a reverse test ensuring that FAIL_PHASE_ONLY is only used for tests
    # that actually fail to throw an exception at wrong phase.
    phase_variants = ['']
    if test.fail_phase_only:
      phase_variants.append('-fail-phase-reverse')

    for phase_var in phase_variants:
      for n, variant in enumerate(self._get_variants(test)):
        flags = flags_set[variant][0]
        if 'noStrict' in test_record:
          yield (variant, flags, str(n) + phase_var)
        elif 'onlyStrict' in test_record:
          yield (variant, flags + ['--use-strict'], 'strict-%d' % n + phase_var)
        else:
          yield (variant, flags, str(n))
          yield (variant, flags + ['--use-strict'], 'strict-%d' % n + phase_var)


class TestLoader(testsuite.JSTestLoader):
  @cached_property
  def local_staging_implementations(self):
    return set()

  @property
  def test_dirs(self):
    self.reset_local_implementations_filtering()
    return [
      self.test_root,
      self.suite.root / TEST_262_LOCAL_TESTS_PATH,
    ]

  def reset_local_implementations_filtering(self):
    self.local_staging_implementations.clear()

  @property
  def excluded_suffixes(self):
    return {"_FIXTURE.js"}

  @property
  def excluded_dirs(self):
    return {"intl402", "Intl402"} if self.test_config.noi18n else set()

  def _should_filter_by_test(self, test):
    if test.has_local_staging_implementation:
      if test.path_js in self.local_staging_implementations:
        print(f"Skipping test {test.path_js} as it has a local implementation")
        return True
      self.local_staging_implementations.add(test.path_js)
    features = test.test_record.get("features", [])
    return SKIPPED_FEATURES.intersection(features)


class TestSuite(testsuite.TestSuite):

  def __init__(self, ctx, *args, **kwargs):
    super(TestSuite, self).__init__(ctx, *args, **kwargs)
    self.test_root = self.root / TEST_262_SUITE_PATH
    # TODO: this makes the TestLoader mutable, refactor it.
    self._test_loader.test_root = self.test_root
    self.harnesspath = self.root / TEST_262_HARNESS_PATH
    self.harness = [self.harnesspath / f for f in TEST_262_HARNESS_FILES]
    self.harness += [self.root / "harness-adapt.js"]
    self.local_test_root = self.root / TEST_262_LOCAL_TESTS_PATH
    self.parse_test_record = self._load_parse_test_record()

  def _load_parse_test_record(self):
    root = TEST_262_TOOLS_ABS_PATH
    f = None
    try:
      loader = importlib.machinery.SourceFileLoader(
          "parseTestRecord", f"{root}/parseTestRecord.py")
      module = loader.load_module()
      return module.parseTestRecord
    except Exception as e:
      print(f'Cannot load parseTestRecord: {e}')
      raise
    finally:
      if f:
        f.close()

  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase

  def _variants_gen_class(self):
    return VariantsGenerator


class TestCase(testcase.D8TestCase):
  def __init__(self, *args, **kwargs):
    super(TestCase, self).__init__(*args, **kwargs)

    source = self.get_source()
    self.test_record = self.suite.parse_test_record(source, self.path)
    self._expected_exception = (
        self.test_record
          .get('negative', {})
          .get('type', None)
    )
    self._async = 'async' in self.test_record.get('flags', [])

    # We disallow combining FAIL_PHASE_ONLY with any other fail outcome types.
    # Outcome parsing logic in the base class converts all outcomes specified in
    # the status file into either FAIL, CRASH or PASS, thus we do not need to
    # handle FAIL_OK, FAIL_SLOPPY and various other outcomes.
    if self.fail_phase_only:
      assert (
          statusfile.FAIL not in self.expected_outcomes and
          statusfile.CRASH not in self.expected_outcomes), self.name

  @property
  def fail_phase_only(self):
    # The FAIL_PHASE_ONLY is defined in tools/testrunner/local/statusfile.py and
    # can be used in status files to mark tests that throw an exception at wrong
    # phase, e.g. SyntaxError is thrown at execution phase instead of parsing
    # phase. See https://crbug.com/v8/8467 for more details.
    return statusfile.FAIL_PHASE_ONLY in self._statusfile_outcomes

  @property
  def _fail_phase_reverse(self):
    return 'fail-phase-reverse' in self.procid

  def __needs_harness_agent(self):
    return self.path.parts[:2] == ("built-ins", "Atomics")

  def _get_files_params(self):
    harness_args = []
    if "raw" not in self.test_record.get("flags", []):
      harness_args = list(self.suite.harness)
    return (harness_args +
            ([self.suite.root /
              "harness-agent.js"] if self.__needs_harness_agent() else []) +
            ([self.suite.root / "harness-ishtmldda.js"]
             if "IsHTMLDDA" in self.test_record.get("features", []) else []) +
            ([self.suite.root /
              "harness-abstractmodulesource.js"] if "source-phase-imports"
             in self.test_record.get("features", []) else []) +
            ([self.suite.root / "harness-adapt-donotevaluate.js"]
             if self.fail_phase_only and not self._fail_phase_reverse else []) +
            ([self.suite.root / "harness-done.js"]
             if "async" in self.test_record.get("flags", []) else []) +
            self._get_includes() +
            (["--module"] if "module" in self.test_record else []) +
            [self._get_source_path()])

  def _get_suite_flags(self):
    feature_flags = []
    for (feature, flags_string) in FEATURE_FLAGS.items():
      if feature in self.test_record.get("features", []):
        for flag in flags_string.split(" "):
          feature_flags.append(flag)
    return (["--ignore-unhandled-promises"] +
            (["--throws"] if "negative" in self.test_record else []) +
            (["--allow-natives-syntax"] if "detachArrayBuffer.js"
             in self.test_record.get("includes", []) else []) + feature_flags +
            ["--no-arguments"]  # disable top-level arguments in d8
           )

  def _get_includes(self):
    return [self._base_path(filename) / filename
            for filename in self.test_record.get("includes", [])]

  def _base_path(self, filename):
    if filename in TEST_262_NATIVE_FILES:
      return self.suite.root
    else:
      return self.suite.harnesspath

  def _get_source_path(self):
    path = self.suite.local_test_root / self.path_js
    if path.exists():
      return path
    return self.suite.test_root / self.path_js

  @cached_property
  def has_local_staging_implementation(self):
    return  (str(self.path_js).startswith("staging")
        and (self.suite.local_test_root / self.path_js).exists()
    )

  @property
  def output_proc(self):
    if self._expected_exception is not None:
      return test262.ExceptionOutProc(self.expected_outcomes,
                                      self._expected_exception,
                                      self._fail_phase_reverse, self._async)
    else:
      # We only support fail phase reverse on tests that expect an exception.
      assert not self._fail_phase_reverse

    if self.expected_outcomes == outproc.OUTCOMES_PASS:
      return test262.PassNoExceptionOutProc(self._async)
    return test262.NoExceptionOutProc(self.expected_outcomes, self._async)

  def skip_rdb(self, result):
    return not result.has_unexpected_output
