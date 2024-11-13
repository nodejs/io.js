#!/usr/bin/env python3

# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.

import subprocess
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(
    description='Generate builtin PGO profiles. ' +
    'The script has to be run from the root of a V8 checkout and updates the profiles in `tools/builtins-pgo/profiles`.'
)
parser.add_argument(
    'v8_target_cpu', help='target cpu to build the profile for: x64 or arm64')
parser.add_argument(
    '--target-cpu',
    default=None,
    help='target cpu for V8 binary (for simulator builds), by default it\'s equal to `v8_target_cpu`'
)
parser.add_argument(
    '--clang',
    default=True,
    help='Use clang for building V8 binaries. Using other compiler helps to get profiles for Windows/gcc. See crbug.com/v8/13647.',
    action=argparse.BooleanOptionalAction)
parser.add_argument(
    '--use-qemu',
    default=False,
    help='Use qemu for running cross-compiled V8 binary.',
    action=argparse.BooleanOptionalAction)
parser.add_argument(
    'benchmark_path',
    help='path to benchmark runner .js file, usually JetStream2\'s `cli.js`',
    type=Path)
parser.add_argument(
    '--out-path',
    default=Path("out"),
    help='directory to be used for building V8, by default `./out`',
    type=Path)

args = parser.parse_args()

if args.target_cpu is None:
  args.target_cpu = args.v8_target_cpu


def run(cmd, **kwargs):
  print(f"# CMD: {cmd} {kwargs}")
  return subprocess.run(cmd, **kwargs, check=True)


def build_d8(path, gn_args):
  if not path.exists():
    path.mkdir(parents=True, exist_ok=True)
  with (path / "args.gn").open("w") as f:
    f.write(gn_args)
  run(["gn", "gen", path])
  run(["autoninja", "-C", path, "d8"])
  return (path / "d8").absolute()


tools_pgo_dir = Path(__file__).parent
v8_path = tools_pgo_dir.parent.parent

if not args.benchmark_path.is_file() or args.benchmark_path.suffix != ".js":
  print(f"Invalid benchmark argument: {args.benchmark_path}")
  exit(1)

cmd_prefix = []
if args.use_qemu:
  if args.v8_target_cpu == "arm":
    cmd_prefix = ["qemu-arm", "-L", "/usr/arm-linux-gnueabihf/"]
  elif args.v8_target_cpu == "arm64":
    cmd_prefix = ["qemu-aarch64", "-L", "/usr/aarch64-linux-gnu/"]
  else:
    print(f"{args.v8_target_cpu} binaries can't be run with qemu")
    exit(1)

GN_ARGS_TEMPLATE_CLANG = f"""\
is_debug = false
is_clang = true
target_cpu = "{args.target_cpu}"
v8_target_cpu = "{args.v8_target_cpu}"
v8_enable_builtins_profiling = true
"""

GN_ARGS_TEMPLATE_NO_CLANG = f"""\
is_debug = false
is_clang = false
use_custom_libcxx = false
target_cpu = "{args.target_cpu}"
v8_target_cpu = "{args.v8_target_cpu}"
v8_enable_builtins_profiling = true
"""

GN_ARGS_TEMPLATE = GN_ARGS_TEMPLATE_CLANG if args.clang else GN_ARGS_TEMPLATE_NO_CLANG

for arch, gn_args in [(args.v8_target_cpu, GN_ARGS_TEMPLATE)]:
  # TODO(crbug.com/v8/13647): remove profile suffixes once CSA is fixed.
  suffix = "" if args.clang else "-rl"
  build_dir = args.out_path / f"{arch}{suffix}.release.generate_builtin_pgo_profile"
  d8_path = build_d8(build_dir, gn_args)
  benchmark_dir = args.benchmark_path.parent
  benchmark_file = args.benchmark_path.name
  log_path = (build_dir / "v8.builtins.pgo").absolute()
  cmd = cmd_prefix + [
      d8_path, f"--turbo-profiling-output={log_path}", benchmark_file
  ]
  run(cmd, cwd=benchmark_dir)
  get_hints_path = tools_pgo_dir / "get_hints.py"
  profile_path = tools_pgo_dir / "profiles" / f"{arch}{suffix}.profile"
  run([get_hints_path, log_path, profile_path])
