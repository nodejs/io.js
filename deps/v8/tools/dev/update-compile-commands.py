#!/usr/bin/env python3
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""\
Creates a "compile_commands.json" file for V8, for the needs of clangd and
similar code indexers. Also updates generated C++ sources, and compiles the
Torque Language Server, for a complete code indexing experience.
"""

import json
import os
import subprocess
import sys
import platform

DEFAULT_ARCH = "x64"
if platform.machine() == "arm64":
  DEFAULT_ARCH = "arm64"

PYLIB_PATH = 'tools/clang/pylib'
GM_PATH = 'tools/dev'
PYLIB_CHECK = os.path.join(PYLIB_PATH, 'clang', 'compile_db.py')
GM_CHECK = os.path.join(GM_PATH, 'gm.py')
def CheckRelativeImport(path):
  if not os.path.exists(path):
    print(f"Error: Please run this script from the root of a V8 checkout. "
          "{path} must be a valid relative path.")
    sys.exit(1)
CheckRelativeImport(PYLIB_CHECK)
CheckRelativeImport(GM_CHECK)

sys.path.insert(0, PYLIB_PATH)
from clang import compile_db

sys.path.insert(0, GM_PATH)
import gm

def _Call(cmd, silent=False):
  if not silent:
    print(f"# {cmd}")
  return subprocess.call(cmd, shell=True)

def _Write(filename, content):
  with open(filename, "w") as f:
    f.write(content)

def PrepareBuildDir(arch, mode):
  build_dir = os.path.join("out", f"{arch}.{mode}")
  if not os.path.exists(build_dir):
    print(f"# mkdir -p {build_dir}")
    os.makedirs(build_dir)
  args_gn = os.path.join(build_dir, "args.gn")
  if not os.path.exists(args_gn):
    conf = gm.ManagedConfig(arch, mode, [])
    _Write(args_gn, conf.get_gn_args())
  build_ninja = os.path.join(build_dir, "build.ninja")
  if not os.path.exists(build_ninja):
    code = _Call(f"gn gen {build_dir}")
    if code != 0: raise Exception("gn gen failed")
  else:
    _Call(f"autoninja -C {build_dir} build.ninja")
  return build_dir

def AddTargetsForArch(arch, combined):
  build_dir = PrepareBuildDir(arch, "debug")
  commands = compile_db.ProcessCompileDatabase(
                compile_db.GenerateWithNinja(build_dir, ["all"]), [])
  added = 0
  for c in commands:
    key = c["file"]
    if key not in combined:
      combined[key] = c
      added += 1
  print(f"{arch}: added {added} compile commands")

def UpdateCompileCommands():
  print(">>> Updating compile_commands.json...")
  combined = {}
  AddTargetsForArch("x64", combined)
  AddTargetsForArch("arm64", combined)
  if DEFAULT_ARCH != "arm64":
    # Mac arm64 doesn't like 32bit platforms:
    AddTargetsForArch("ia32", combined)
    AddTargetsForArch("arm", combined)
  commands = []
  for key in combined:
    commands.append(combined[key])
  _Write("compile_commands.json", json.dumps(commands, indent=2))

def CompileLanguageServer():
  print(">>> Compiling Torque Language Server...")
  PrepareBuildDir(DEFAULT_ARCH, "release")
  _Call(f"autoninja -C out/{DEFAULT_ARCH}.release torque-language-server")


def GenerateCCFiles():
  print(">>> Generating generated C++ source files...")
  # This must be called after UpdateCompileCommands().
  assert os.path.exists(f"out/{DEFAULT_ARCH}.debug/build.ninja")
  _Call(f"autoninja -C out/{DEFAULT_ARCH}.debug v8_generated_cc_files")


def PrepareReclient():
  reclient_mode = gm.detect_reclient()
  if reclient_mode == gm.Reclient.GOOGLE and not gm.detect_reclient_cert():
    print("# gcert")
    subprocess.check_call("gcert", shell=True)


if __name__ == "__main__":
  PrepareReclient()
  CompileLanguageServer()
  UpdateCompileCommands()
  GenerateCCFiles()
