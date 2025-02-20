#!/usr/bin/env python3
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""\
Convenience script for generating arch-specific ctags file.
This script MUST be executed at the top directory.

Usage:
    $ tools/dev/gen-tags.py [<arch>...]

The example usage is as follows:
    $ tools/dev/gen-tags.py x64

If no <arch> is given, it generates tags file for all arches:
    $ tools/dev/gen-tags.py
"""

# for py2/py3 compatibility
from __future__ import print_function

import os
import subprocess
import sys

# All arches that this script understands.
ARCHES = ["ia32", "x64", "arm", "arm64", "mips64"]


def PrintHelpAndExit():
  print(__doc__)
  sys.exit(0)


def _Call(cmd, silent=False):
  if not silent: print("# %s" % cmd)
  return subprocess.call(cmd, shell=True)


def ParseArguments(argv):
  if not "tools/dev" in argv[0]:
    PrintHelpAndExit()
  argv = argv[1:]

  # If no argument is given, then generate ctags for all arches.
  if len(argv) == 0:
    return ARCHES

  user_arches = []
  for argstring in argv:
    if argstring in ("-h", "--help", "help"):
      PrintHelpAndExit()
    if argstring not in ARCHES:
      print("Invalid argument: %s" % argstring)
      sys.exit(1)
    user_arches.append(argstring)

  return user_arches


def Exclude(fullpath, exclude_arches):
  for arch in exclude_arches:
    if ("/%s/" % arch) in fullpath: return True
  return False


def Main(argv):
  user_arches = []

  user_arches = ParseArguments(argv)

  exclude_arches = list(ARCHES)
  for user_arch in user_arches:
    exclude_arches.remove(user_arch)

  paths = ["include", "src", "test"]
  exts = [".h", ".cc", ".c"]

  gtags_filename = "gtags.files"

  with open(gtags_filename, "w") as gtags:
    for path in paths:
      for root, dirs, files in os.walk(path):
        for file in files:
          if not file.endswith(tuple(exts)): continue
          fullpath = os.path.join(root, file)
          if Exclude(fullpath, exclude_arches): continue
          gtags.write(fullpath + os.linesep)

  _Call("ctags --fields=+l -L " + gtags_filename)


if __name__ == "__main__":
  sys.exit(Main(sys.argv))
