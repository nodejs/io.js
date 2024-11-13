#!/usr/bin/env python3

# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.

import subprocess
import argparse
import os
import filecmp
import tempfile
import shutil
import platform
import re
import sys
from pathlib import Path

# List of all supported build configurations for static roots

STATIC_ROOT_CONFIGURATIONS = {
    "ptr-cmpr-wasm-intl": {
        "target": "src/roots/static-roots.h",
        "gn_args": {
            "v8_enable_static_roots": "false",
            "v8_enable_static_roots_generation": "true",
            "v8_enable_pointer_compression": "true",
            "v8_enable_shared_ro_heap": "true",
            "v8_enable_pointer_compression_shared_cage": "true",
            "v8_enable_webassembly": "true",
            "v8_enable_i18n_support": "true",
            "dcheck_always_on": "false"
        }
    },
}

# Parse args

parser = argparse.ArgumentParser(description='Generates static-roots.h.')
parser.add_argument(
    '--configuration',
    choices=STATIC_ROOT_CONFIGURATIONS.keys(),
    action='extend',
    default='ptr-cmpr-wasm-intl',
    nargs='*',
    help="""Build configuration. Refers to a set of configurations with
identical static-roots.h. Currently there is only one supported configuration.
Future configurations will need to generate multiple target files.""")
parser.add_argument(
    '--out',
    default=Path('out/static-roots'),
    required=False,
    type=Path,
    help='target build directory')

args = parser.parse_args()

# Some helpers
def run(cmd, **kwargs):
  print(f"# CMD: {cmd}")
  return subprocess.run(cmd, **kwargs, check=True)


def machine_target():
  raw = platform.machine()
  raw_lower = raw.lower()
  if raw_lower == "x86_64" or raw_lower == "amd64":
    return "x64"
  if raw_lower == "aarch64":
    return "arm64"
  return raw


V8_PATH = Path(__file__).parents[2]
GM = V8_PATH / 'tools' / 'dev' / 'gm.py'

changed = False
for target in [args.configuration]:
  out_dir = args.out / f"{target}"
  if not out_dir.exists():
    out_dir.mkdir(parents=True, exist_ok=True)
  build_dir = out_dir / f"{machine_target()}.release"
  config = STATIC_ROOT_CONFIGURATIONS[target]

  # Let gm create the default config
  e = dict(os.environ)
  e['V8_GM_OUTDIR'] = f"{out_dir}"
  run([f"{sys.executable}", f"{GM}", f"{machine_target()}.release.gn_args"],
      env=e)

  # Patch the default config according to our needs
  print("# Updating gn args")
  gn_args_template = config["gn_args"].copy()
  gn_args = (build_dir / "args.gn").open("r").read().splitlines()
  with (build_dir / "args.gn").open("w") as f:
    for line in filter(bool, gn_args):
      result = re.search(r"^([^ ]+) = (.+)$", line)
      if not result or len(result.groups()) != 2:
        print(f"Error parsing {build_dir / 'args.gn'} at line '{line}'")
        exit(255)
      if result.group(1) in gn_args_template:
        line = f"{result.group(1)} = {gn_args_template[result.group(1)]}"
        gn_args_template.pop(result.group(1))
      f.write(f"{line}\n")
    for extra, val in gn_args_template.items():
      f.write(f"{extra} = {val}\n")

  # Build mksnapshot
  run([f"{sys.executable}", f"{GM}", f"{machine_target()}.release.mksnapshot"],
      env=e)

  # Generate static roots file and check if it changed
  out_file = Path(tempfile.gettempdir()) / f"static-roots-{target}.h"
  run([f"{build_dir / 'mksnapshot'}", "--static-roots-src", f"{out_file}"])
  target_file = V8_PATH / config["target"]
  if not filecmp.cmp(out_file, target_file):
    shutil.move(out_file, target_file)
    changed = True

if changed:
  exit(1)
