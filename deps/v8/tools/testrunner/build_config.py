# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Increase the timeout for these:
SLOW_ARCHS = [
    "arm", "arm64", "mips64", "mips64el", "s390x", "riscv32", "riscv64",
    "loong64"
]

# Timeout scale factor per build flag.
SCALE_FACTOR = dict(
    full_debug=4,
    lite_mode=2,
    tsan=2,
    use_sanitizer=1.5,
    verify_predictable=8,
)

INITIALIZATION_ERROR = f"""
Missing property '%s'. If you see this error in testing, you might need
to add the property to tools/testrunner/testdata/v8_build_config.json. If
you see this error in production, ensure to add the property to the
v8_dump_build_config action in V8's top-level BUILD.gn file.
"""


class BuildConfig(object):
  """Enables accessing all build-time flags as set in V8's BUILD.gn file.

  All flags are auto-generated based on the output of V8's
  v8_dump_build_config action.
  """

  def __init__(self, build_config):
    for key, value in build_config.items():
      setattr(self, key, value)

    self.keys = list(build_config.keys())

    bool_options = [key for key, value in self.items() if value is True]
    string_options = [
      f'{key}="{value}"'
      for key, value in self.items() if value and isinstance(value, str)]
    self._str_rep = ', '.join(sorted(bool_options + string_options))

  def items(self):
    for key in self.keys:
      yield key, getattr(self, key)

  def ensure_vars(self, build_vars):
    for var in build_vars:
      if var not in self.keys:
        raise Exception(INITIALIZATION_ERROR % var)

  def timeout_scalefactor(self, initial_factor):
    """Increases timeout for slow build configurations."""
    result = initial_factor
    for key, value in SCALE_FACTOR.items():
      try:
        if getattr(self, key):
          result *= value
      except AttributeError:
        raise Exception(INITIALIZATION_ERROR % key)
    if self.arch in SLOW_ARCHS:
      result *= 4.5
    return result

  def __str__(self):
    return self._str_rep
