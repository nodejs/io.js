# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from testrunner.local import testsuite
from testrunner.objects import testcase

SUB_TESTS = [
    'inspector',
    'json',
    'parser',
    'regexp',
    'regexp_builtins',
    'multi_return',
    'wasm',
    'wasm_async',
    'wasm_code',
    'wasm_compile',
    'wasm_compile_all',
    'wasm_compile_simd',
    'wasm_compile_wasmgc',
    'wasm_init_expr',
    'wasm_streaming',
]

class VariantsGenerator(testsuite.VariantsGenerator):
  def _get_variants(self, test):
    return self._standard_variant


class TestLoader(testsuite.GenericTestLoader):
  @property
  def test_dirs(self):
    return SUB_TESTS

  def _to_relpath(self, abspath, _):
    return abspath.relative_to(self.suite.root)

  def _should_filter_by_name(self, _):
    return False

class TestSuite(testsuite.TestSuite):
  def _test_loader_class(self):
    return TestLoader

  def _test_class(self):
    return TestCase

  def _variants_gen_class(self):
    return VariantsGenerator


class TestCase(testcase.TestCase):
  def _get_files_params(self):
    return [self.suite.root / self.path]

  def _get_variant_flags(self):
    return []

  def _get_statusfile_flags(self):
    return []

  def _get_mode_flags(self):
    return []

  def get_shell(self):
    return f'v8_simple_{self.path.parts[0]}_fuzzer'
