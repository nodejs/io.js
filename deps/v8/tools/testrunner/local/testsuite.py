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
import itertools
import os

from contextlib import contextmanager
from pathlib import Path

from . import statusfile
from .variants import ALL_VARIANTS, ALL_VARIANT_FLAGS


STANDARD_VARIANT = set(["default"])


class VariantsGenerator(object):
  def __init__(self, variants):
    self._all_variants = [v for v in variants if v in ALL_VARIANTS]
    self._standard_variant = [v for v in variants if v in STANDARD_VARIANT]

  def gen(self, test):
    """Generator producing (variant, flags, procid suffix) tuples."""
    flags_set = self._get_flags_set(test)
    for n, variant in enumerate(self._get_variants(test)):
      yield (variant, flags_set[variant][0], n)

  def _get_flags_set(self, test):
    return ALL_VARIANT_FLAGS

  def _get_variants(self, test):
    if test.only_standard_variant:
      return self._standard_variant
    return self._all_variants


class TestCombiner(object):
  def get_group_key(self, test):
    """To indicate what tests can be combined with each other we define a group
    key for each test. Tests with the same group key can be combined. Test
    without a group key (None) is not combinable with any other test.
    """
    raise NotImplementedError()

  def combine(self, name, tests):
    """Returns test combined from `tests`. Since we identify tests by their
    suite and name, `name` parameter should be unique within one suite.
    """
    return self._combined_test_class()(name, tests)

  def _combined_test_class(self):
    raise NotImplementedError()


class TestLoader(object):
  """Base class for loading TestSuite tests after applying test suite
  transformations."""

  def __init__(self, ctx, suite, test_class, test_config, test_root):
    self.ctx = ctx
    self.suite = suite
    self.test_class = test_class
    self.test_config = test_config
    self.test_root = test_root
    self.test_count_estimation = len(list(self._list_test_filenames()))

  def _list_test_filenames(self):
    """Implemented by the subclassed TestLoaders to list filenames.

    Filenames are expected to be sorted and are deterministic."""
    raise NotImplementedError

  def _should_filter_by_name(self, name):
    return False

  def _should_filter_by_test(self, test):
    return False

  def _filename_to_testname(self, filename):
    """Hook for subclasses to write their own filename transformation
    logic before the test creation."""
    return filename

  def _create_test(self, path, suite, **kwargs):
    """Converts paths into test objects using the given options"""
    return self.test_class(suite, path, path.as_posix(), **kwargs)

  def list_tests(self):
    """Loads and returns the test objects for a TestSuite"""
    # TODO: detect duplicate tests.
    for filename in map(Path, self._list_test_filenames()):
      if self._should_filter_by_name(filename):
        continue

      testname = self._filename_to_testname(filename)
      case = self._create_test(testname, self.suite)
      if self._should_filter_by_test(case):
        continue

      yield case


class GenericTestLoader(TestLoader):
  """Generic TestLoader implementing the logic for listing filenames"""
  @property
  def excluded_files(self):
    return set()

  @property
  def excluded_dirs(self):
    return set()

  @property
  def excluded_suffixes(self):
    return set()

  @property
  def test_dirs(self):
    return [self.test_root]

  @property
  def extensions(self):
    return []

  def __find_extension(self, filename):
    for extension in self.extensions:
      if filename.name.endswith(extension):
        return extension

    return False

  def _should_filter_by_name(self, filename):
    if not self.__find_extension(filename):
      return True

    for suffix in self.excluded_suffixes:
      if filename.name.endswith(suffix):
        return True

    if filename.name in self.excluded_files:
      return True

    return False

  def _filename_to_testname(self, filename):
    extension = self.__find_extension(filename)
    if not extension:
      return filename

    return filename.parent / filename.name[:-len(extension)]

  def _to_relpath(self, abspath, test_root):
    return abspath.relative_to(test_root)

  def _list_test_filenames(self):
    for test_dir in sorted(self.test_dirs):
      test_root = self.test_root / test_dir
      for dirname, dirs, files in os.walk(test_root, followlinks=True):
        dirs.sort()
        for dir in dirs:
          if dir in self.excluded_dirs or dir.startswith('.'):
            dirs.remove(dir)

        files.sort()
        for filename in files:
          abspath = Path(dirname) / filename
          yield self._to_relpath(abspath, test_root)


class JSTestLoader(GenericTestLoader):
  @property
  def extensions(self):
    return [".js", ".mjs"]


class TestGenerator(object):
  def __init__(
      self, test_count_estimate, heavy_tests, slow_tests, remaining_tests):
    self.test_count_estimate = test_count_estimate
    self.heavy_tests = heavy_tests
    self.slow_tests = slow_tests
    self.remaining_tests = remaining_tests
    self._rebuild_iterator()

  def _rebuild_iterator(self):
    self._iterator = itertools.chain(
        self.heavy_tests, self.slow_tests, self.remaining_tests)

  def __iter__(self):
    return self

  def __next__(self):
    return self.next()

  def next(self):
    return next(self._iterator)

  def merge(self, test_generator):
    self.test_count_estimate += test_generator.test_count_estimate
    self.heavy_tests = itertools.chain(
      self.heavy_tests, test_generator.heavy_tests)
    self.slow_tests = itertools.chain(
      self.slow_tests, test_generator.slow_tests)
    self.remaining_tests = itertools.chain(
      self.remaining_tests, test_generator.remaining_tests)
    self._rebuild_iterator()


@contextmanager
def _load_testsuite_module(name, root):
  f = None
  try:
    yield importlib.machinery.SourceFileLoader(
        name + "_testcfg", f"{root}/testcfg.py").load_module()
  finally:
    if f:
      f.close()


class TestSuite(object):
  @staticmethod
  def Load(ctx, root, test_config):
    name = root.name
    with _load_testsuite_module(name, root) as module:
      return module.TestSuite(ctx, name, root, test_config)

  def __init__(self, ctx, name, root, test_config):
    self.name = name  # string
    self.root = root  # pathlib path
    self.test_config = test_config
    self.tests = None  # list of TestCase objects
    self.statusfile = None

    self._test_loader = self._test_loader_class()(ctx, self, self._test_class(),
                                                  self.test_config, self.root)

  @property
  def framework_name(self):
    return self.test_config.framework_name

  def status_file(self):
    return "%s/%s.status" % (self.root, self.name)

  def statusfile_outcomes(self, test_name, variant):
    return self.statusfile.get_outcomes(test_name, variant)

  @property
  def _test_loader_class(self):
    raise NotImplementedError

  def ListTests(self):
    return self._test_loader.list_tests()

  def __initialize_test_count_estimation(self):
    # Retrieves a single test to initialize the test generator.
    next(iter(self.ListTests()), None)

  def __calculate_test_count(self):
    self.__initialize_test_count_estimation()
    return self._test_loader.test_count_estimation

  def load_tests_from_disk(self, statusfile_variables):
    self.statusfile = statusfile.StatusFile(
      self.status_file(), statusfile_variables)

    test_count = self.__calculate_test_count()
    heavy_tests = (
        test for test in self.ListTests()
        if test.is_heavy)
    slow_tests = (
        test for test in self.ListTests()
        if not test.is_heavy and test.is_slow)
    remaining_tests = (
        test for test in self.ListTests()
        if not test.is_heavy and not test.is_slow)
    return TestGenerator(test_count, heavy_tests, slow_tests, remaining_tests)

  def get_variants_gen(self, variants):
    return self._variants_gen_class()(variants)

  def _variants_gen_class(self):
    return VariantsGenerator

  def test_combiner_available(self):
    return bool(self._test_combiner_class())

  def get_test_combiner(self):
    cls = self._test_combiner_class()
    if cls:
      return cls()
    return None

  def _test_combiner_class(self):
    """Returns Combiner subclass. None if suite doesn't support combining
    tests.
    """
    return None

  def _test_class(self):
    raise NotImplementedError
