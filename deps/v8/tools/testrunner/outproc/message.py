# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re

from itertools import zip_longest

from . import base


class OutProc(base.ExpectedOutProc):
  def __init__(self, expected_outcomes, basepath, expected_fail,
               expected_filename, regenerate_expected_files):
    super(OutProc, self).__init__(expected_outcomes, expected_filename,
                                  regenerate_expected_files)
    self._basepath = basepath
    self._expected_fail = expected_fail

  def _is_failure_output(self, output):
    fail = output.exit_code != 0
    if fail != self._expected_fail:
      return True

    expected_lines = []
    # Can't use utils.ReadLinesFrom() here because it strips whitespace.
    with open(self._basepath.with_suffix('.out')) as f:
      for line in f:
        if line.startswith("#") or not line.strip():
          continue
        expected_lines.append(line)
    raw_lines = output.stdout.splitlines()
    actual_lines = [ s for s in raw_lines if not self._ignore_line(s) ]
    if len(expected_lines) != len(actual_lines):
      return True

    # Try .js first, and fall back to .mjs.
    # TODO(v8:9406): clean this up by never separating the path from
    # the extension in the first place.
    base_path = self._basepath.with_suffix('.js')
    if not base_path.exists():
      base_path = self._basepath.with_suffix('.mjs')

    env = {
      'basename': base_path.name,
    }
    for (expected, actual) in zip_longest(
        expected_lines, actual_lines, fillvalue=''):
      pattern = re.escape(expected.rstrip() % env)
      pattern = pattern.replace('\\*', '.*')
      pattern = pattern.replace('\\{NUMBER\\}', r'\d+(?:\.\d*)?')
      # Note: The character sequence for printing an address in C++ is
      # implementation defined.
      pattern = pattern.replace('\\{ADDRESS\\}', r'(0x)?[0-9A-Fa-f]+')
      pattern = '^%s$' % pattern
      if not re.match(pattern, actual):
        return True
    return False

  def _ignore_line(self, string):
    """Ignore empty lines, valgrind output, Android output."""
    return (
        not string or  #
        not string.strip() or  #
        string.startswith("==") or  #
        string.startswith("**") or  #
        string.startswith("ANDROID") or  #
        # Android linker warning.
        string.startswith('WARNING: linker:') or  #
        # Testing on Android devices mixes stderr into stdout.
        string ==
        "V8 is running with experimental features enabled. Stability and security will suffer."
        or  #
        string == "Concurrent maglev has been disabled for tracing.")
