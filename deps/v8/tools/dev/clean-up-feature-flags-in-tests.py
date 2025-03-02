#!/usr/bin/env python3

# Copyright 2025 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

TEST_ROOT = Path(__file__).absolute().parent.parent.parent / 'test'
TEST_SUITES = [
    'mjsunit', 'debugger', 'inspector', 'message', 'js-perf-test', 'webkit'
]
TEST_EXTENSIONS = ('.js', '.mjs')
HAS_FLAGS_RE = re.compile("//\s*Flags:.*\n")
EXTRACT_FLAGS_RE = re.compile("//\s*Flags:\s+((--[A-z0-9-_=]+\s*)+).*")
FEATURE_FLAG_PREFIXES = {
    'js': ('--js-', '--js_'),
    'harmony': ('--harmony-', '--harmony_'),
    'wasm': ('--experimental-wasm-', '--experimental_wasm_')
}
NEG_FEATURE_FLAG_PREFIXES = {
    'js': ('--no-js-', '--no_js_'),
    'harmony': ('--no-harmony-', '--no_harmony_'),
    'wasm': ('--no-experimental-wasm-', '--no_experimental_wasm_')
}
STAGING_FLAGS = {
    'js': '--js-staging',
    'harmony': '--harmony',
    'wasm': '--wasm-staging'
}
SHIPPING_FLAGS = {
    'js': '--js-shipping',
    'harmony': '--harmony-shipping',
    'wasm': ''  # Wasm doesn't have a shipping flag
}

parser = argparse.ArgumentParser()
parser.add_argument(
    '--outdir', required=True, type=Path, help='build directory with d8')
args = parser.parse_args()

D8 = os.path.join(args.outdir, 'd8')


def raw_flags_to_cli_flags(raw_flags_for_lang):
  return {
      kind:
          set([f'--{flag}' for flag in raw_flags] +
              [f"--{flag.replace('_', '-')}" for flag in raw_flags])
      for kind, raw_flags in raw_flags_for_lang.items()
  }


def get_feature_flags():
  p = subprocess.run([D8, '--print-feature-flags-json'],
                     stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE)
  raw_flags = json.loads(p.stdout)
  return {key: raw_flags_to_cli_flags(val) for key, val in raw_flags.items()}


def clean_up_feature_flags(contents, all_flags_by_lang):
  # Returns a (bool, string) pair, where the bool says whether the contents
  # changed due to a flag being cleaned up.

  if not re.search(HAS_FLAGS_RE, contents):
    return (False, contents)

  dirty = False
  cleaned_contents = []
  emitted_staging_flags = set()
  for line in contents.splitlines():
    match = re.search(EXTRACT_FLAGS_RE, line)
    if match:
      inprogress_flags = set(match.group(1).split())
      original_flags = inprogress_flags.copy()
      for (lang, lang_flags) in all_flags_by_lang.items():
        # Remove staged flags.
        inprogress_flags_no_staged = inprogress_flags - lang_flags['staged']
        # If any staged flags were removed, emit the staging flag instead. Only
        # do so once per file.
        if (inprogress_flags_no_staged
            != inprogress_flags) and (lang not in emitted_staging_flags):
          inprogress_flags_no_staged.add(STAGING_FLAGS[lang])
          emitted_staging_flags.add(lang)
        inprogress_flags = inprogress_flags_no_staged
        # Remove flags that are shipping.
        inprogress_flags -= lang_flags['shipping']
        # Heuristically remove flags that look like feature flags (prefixed by
        # one of the known prefixes) that don't exist anymore.
        for flag in inprogress_flags.copy():
          if flag == STAGING_FLAGS[lang] or flag == SHIPPING_FLAGS[lang]:
            continue
          if flag.startswith(FEATURE_FLAG_PREFIXES[lang]) and (
              flag not in lang_flags['in-progress']):
            inprogress_flags.remove(flag)
          elif flag.startswith(NEG_FEATURE_FLAG_PREFIXES[lang]) and (
              f"--{flag[5:]}" not in lang_flags['in-progress']):
            inprogress_flags.remove(flag)

      if inprogress_flags != original_flags:
        dirty = True
        if len(inprogress_flags) > 0:
          cleaned_contents.append(f"// Flags: {' '.join(inprogress_flags)}")
      else:
        cleaned_contents.append(line)
    else:
      cleaned_contents.append(line)

  if not dirty:
    return (False, contents)
  else:
    return (True, "\n".join(cleaned_contents) + "\n")


if __name__ == '__main__':
  flags = get_feature_flags()

  # Walk test files.
  for suite_dir in TEST_SUITES:
    suite_path = TEST_ROOT / suite_dir
    for dirname, dirs, files in os.walk(suite_path, followlinks=True):
      for filename in files:
        if not filename.endswith(TEST_EXTENSIONS):
          continue
        abspath = Path(dirname) / filename
        encoding = 'utf-8'
        with open(abspath, 'rb') as fh:
          # We still have some test files that aren't valid UTF-8. But we also
          # can't unconditionally use ISO-8859-1, because we also have tests
          # that can only be encoded in UTF-8.
          try:
            contents = fh.read().decode(encoding)
          except:
            encoding = 'ISO-8859-1'
            contents = fh.read().decode(encoding)
          contents_changed, contents = clean_up_feature_flags(contents, flags)
        if contents_changed:
          with open(abspath, 'w', encoding=encoding) as fh:
            fh.write(contents)
          print(f"Changed flags in {abspath}")

  sys.exit(0)
