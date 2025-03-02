#!/usr/bin/env python3
# vim:fenc=utf-8:shiftwidth=2

# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Check that each header can be included in isolation.

For each header we generate one .cc file which only includes this one header.
All these .cc files are then added to a sources.gni file which is included in
BUILD.gn. Just compile to check whether there are any violations to the rule
that each header must be includable in isolation.
"""

# for py2/py3 compatibility
from __future__ import print_function

import argparse
import os
import os.path
import re
import sys

# TODO(clemensb): Extend to tests.
DEFAULT_INPUT = ['include', 'src']
DEFAULT_GN_FILE = 'BUILD.gn'
MY_DIR = os.path.dirname(os.path.realpath(__file__))
V8_DIR = os.path.dirname(MY_DIR)
OUT_DIR = os.path.join(V8_DIR, 'check-header-includes')
AUTO_EXCLUDE = [
    # Platform specific for iOS.
    'src/base/ios-headers.h',
    # flag-definitions.h needs a mode set for being included.
    'src/flags/flag-definitions.h',
    # recorder.h should only be included conditionally.
    'src/libplatform/tracing/recorder.h',
    # trap-handler-simulator.h can only be included in simulator builds.
    'src/trap-handler/trap-handler-simulator.h',
    # The src/wasm/*-impl.h coding pattern is generally at odds with the rule
    # being enforced here: they are meant to provide possibly-incomplete
    # templates, and their users must provide their prerequisites.
    'src/wasm/function-body-decoder-impl.h',
    'src/wasm/module-decoder-impl.h',
    # wasm-interpreter-runtime-inl.h should only be included when building
    # with V8_ENABLE_DRUMBRAKE which is not the case for the default build.
    'src/wasm/interpreter/wasm-interpreter-runtime-inl.h',
    # TODO(carlscab): Enable once Perfetto is built by default.
    'src/tracing/code-data-source.h',
    'src/tracing/code-trace-context.h',
    'src/tracing/perfetto-logger.h',
    'src/tracing/perfetto-utils.h',
]
AUTO_EXCLUDE_PATTERNS = [
    'src/base/atomicops_internals_.*',
    # TODO(petermarshall): Enable once Perfetto is built by default.
    'src/libplatform/tracing/perfetto*',
    # TODO(v8:7700): Enable once Maglev is built by default.
    'src/maglev/.*',
] + [
    # platform-specific headers
    '\\b{}\\b'.format(p)
    for p in ('win', 'win32', 'ia32', 'x64', 'arm', 'arm64', 'mips64', 's390',
              'ppc', 'riscv', 'riscv64', 'riscv32', 'loong64')
]

args = None
def parse_args():
  global args
  parser = argparse.ArgumentParser()
  parser.add_argument('-i', '--input', type=str, action='append',
                      help='Headers or directories to check (directories '
                           'are scanned for headers recursively); default: ' +
                           ','.join(DEFAULT_INPUT))
  parser.add_argument('-x', '--exclude', type=str, action='append',
                      help='Add an exclude pattern (regex)')
  parser.add_argument('-v', '--verbose', action='store_true',
                      help='Be verbose')
  args = parser.parse_args()
  args.exclude = (args.exclude or []) + AUTO_EXCLUDE_PATTERNS
  args.exclude += ['^' + re.escape(x) + '$' for x in AUTO_EXCLUDE]
  if not args.input:
    args.input=DEFAULT_INPUT


def printv(line):
  if args.verbose:
    print(line)


def find_all_headers():
  printv('Searching for headers...')
  header_files = []
  exclude_patterns = [re.compile(x) for x in args.exclude]
  def add_recursively(filename):
    full_name = os.path.join(V8_DIR, filename)
    if not os.path.exists(full_name):
      sys.exit('File does not exist: {}'.format(full_name))
    if os.path.isdir(full_name):
      for subfile in os.listdir(full_name):
        full_name = os.path.join(filename, subfile)
        printv('Scanning {}'.format(full_name))
        add_recursively(full_name)
    elif filename.endswith('.h'):
      printv('--> Found header file {}'.format(filename))
      for p in exclude_patterns:
        if p.search(filename):
          printv('--> EXCLUDED (matches {})'.format(p.pattern))
          return
      header_files.append(filename)

  for filename in args.input:
    add_recursively(filename)

  return header_files


def get_cc_file_name(header):
  split = os.path.split(header)
  header_dir = os.path.relpath(split[0], V8_DIR)
  # Prefix with the directory name, to avoid collisions in the object files.
  prefix = header_dir.replace(os.path.sep, '-')
  cc_file_name = 'test-include-' + prefix + '-' + split[1][:-1] + 'cc'
  return os.path.join(OUT_DIR, cc_file_name)


def create_including_cc_files(header_files):
  comment = 'check including this header in isolation'
  for header in header_files:
    cc_file_name = get_cc_file_name(header)
    rel_cc_file_name = os.path.relpath(cc_file_name, V8_DIR)
    content = '#include "{}"  // {}\n'.format(header, comment)
    if os.path.exists(cc_file_name):
      with open(cc_file_name) as cc_file:
        if cc_file.read() == content:
          printv('File {} is up to date'.format(rel_cc_file_name))
          continue
    printv('Creating file {}'.format(rel_cc_file_name))
    with open(cc_file_name, 'w') as cc_file:
      cc_file.write(content)


def generate_gni(header_files):
  gni_file = os.path.join(OUT_DIR, 'sources.gni')
  printv('Generating file "{}"'.format(os.path.relpath(gni_file, V8_DIR)))
  with open(gni_file, 'w') as gn:
    gn.write("""\
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This list is filled automatically by tools/check_header_includes.py.
check_header_includes_sources = [
""");
    for header in header_files:
      cc_file_name = get_cc_file_name(header)
      gn.write('    "{}",\n'.format(os.path.relpath(cc_file_name, V8_DIR)))
    gn.write(']\n')


def main():
  parse_args()
  header_files = find_all_headers()
  if not os.path.exists(OUT_DIR):
    os.mkdir(OUT_DIR)
  create_including_cc_files(header_files)
  generate_gni(header_files)

if __name__ == '__main__':
  main()
