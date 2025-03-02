# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Calculate arguments for the gcmole plugin based on flags passed to the
compiler for a typical target in V8.
"""

from pathlib import Path

import os
import re
import sys

DEFINES_RE = re.compile(r'^defines = (.*)$', re.M)
INCLUDES_RE = re.compile(r'^include_dirs = (.*)$', re.M)
CFLAGS_CC_RE = re.compile(r'^cflags_cc = (.*)$', re.M)

BASE_DIR = Path(__file__).resolve().parents[2].absolute()

# This script is always called relative to the build directory root
# by ninja.
BUILD_DIR_ABS = Path.cwd()
BUILD_DIR_REL = BUILD_DIR_ABS.relative_to(BASE_DIR)


def search_flags(regexp, ninja_config):
  match = regexp.search(ninja_config)
  assert match
  result = match.group(1)
  assert result
  return result

def main():
  assert len(sys.argv) == 2, 'Expecting sysroot arg'
  gn_sysroot_var = sys.argv[1]
  assert gn_sysroot_var.startswith('//'), 'Expecting root-dir gn path'
  rel_sysroot = gn_sysroot_var[len('//'):]

  assert BUILD_DIR_ABS.exists()

  ninja_file = BUILD_DIR_ABS / 'obj' / 'v8_base_without_compiler.ninja'
  assert ninja_file.exists()

  with ninja_file.open() as f:
    ninja_config = f.read()

  defines = search_flags(DEFINES_RE, ninja_config)
  # ninja files escape special characters (e.g. '"', '\', '(', etc).
  defines = defines.replace('\\', '')
  includes = search_flags(INCLUDES_RE, ninja_config)
  cflags_cc = search_flags(CFLAGS_CC_RE, ninja_config)

  flags = []
  flags += defines.strip().split()

  # Include flags are relative to the build root. Make them relative to the
  # base directory for gcmole.
  # E.g. BUILD_DIR_REL = out/build and -I../../include gives -Iinclude.
  for flag in includes.strip().split():
    prefix, suffix = flag[:2], flag[2:]
    assert prefix == '-I'
    flags.append(prefix + os.path.normpath(BUILD_DIR_REL / suffix))

  # System include flags are in the ccflags
  for flag in cflags_cc.strip().split():
    if flag == "-nostdinc++":
      flags.append(flag)
    elif flag.startswith('-isystem'):
      prefix, suffix = flag[:len('-isystem')], flag[len('-isystem'):]
      assert prefix == '-isystem'
      flags.append(prefix + os.path.normpath(BUILD_DIR_REL / suffix))

  flags.append(f'--sysroot={rel_sysroot}')

  with open('v8_gcmole.args', 'w') as f:
    f.write(' '.join(flags))


if __name__ == '__main__':
  main()
