#!/usr/bin/env python3
# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""\
Convenience wrapper for compiling V8 with gn/ninja and running tests.
Sets up build output directories if they don't exist.
Produces simulator builds for non-Intel target architectures.
Uses reclient by default if it is detected (at output directory setup time).
Expects to be run from the root of a V8 checkout.

Usage:
    gm.py [<arch>].[<mode>[-<suffix>]].[<target>] [testname...] [--flag]

All arguments are optional. Most combinations should work, e.g.:
    gm.py ia32.debug x64.release x64.release-my-custom-opts d8
    gm.py android_arm.release.check --progress=verbose
    gm.py x64 mjsunit/foo cctest/test-bar/*

For a less automated experience, pass an existing output directory (which
must contain an existing args.gn), e.g.:
    gm.py out/foo unittests

Flags are passed unchanged to the test runner. They must start with -- and must
not contain spaces.
"""
# See HELP below for additional documentation.

from __future__ import print_function
import errno
import os
import platform
import re
import subprocess
import sys
import shutil
import time

from enum import IntEnum
from pathlib import Path

USE_PTY = "linux" in sys.platform
if USE_PTY:
  import pty

BUILD_TARGETS_TEST = [
    "d8", "bigint_shell", "cctest", "inspector-test", "v8_unittests",
    "wasm_api_tests"
]
BUILD_TARGETS_ALL = ["all"]

# All arches that this script understands.
ARCHES = [
    "ia32", "x64", "arm", "arm64", "mips64el", "ppc", "ppc64", "riscv32",
    "riscv64", "s390", "s390x", "android_arm", "android_arm64", "loong64",
    "fuchsia_x64", "fuchsia_arm64", "android_riscv64"
]
# Arches that get built/run when you don't specify any.
DEFAULT_ARCHES = ["ia32", "x64", "arm", "arm64"]
SANDBOX_SUPPORTED_ARCHES = ["x64", "arm64"]
# Modes that this script understands.
MODES = {
    "release": "release",
    "rel": "release",
    "debug": "debug",
    "dbg": "debug",
    "optdebug": "optdebug",
    "opt": "optdebug"
}
# Modes that get built/run when you don't specify any.
DEFAULT_MODES = ["release", "debug"]
# Build targets that can be manually specified.
TARGETS = [
    "d8", "cctest", "v8_unittests", "v8_fuzzers", "wasm_api_tests", "wee8",
    "mkgrokdump", "mksnapshot", "generate-bytecode-expectations",
    "inspector-test", "bigint_shell", "wami", "gn_args"
]
# Build targets that get built when you don't specify any (and specified tests
# don't imply any other targets).
DEFAULT_TARGETS = ["d8"]
# Tests that run-tests.py would run by default that can be run with
# BUILD_TARGETS_TESTS.
DEFAULT_TESTS = ["cctest", "debugger", "intl", "message", "mjsunit",
                 "unittests"]
# These can be suffixed to any <arch>.<mode> combo, or used standalone,
# or used as global modifiers (affecting all <arch>.<mode> combos).
ACTIONS = {
    "all": {
        "targets": BUILD_TARGETS_ALL,
        "tests": [],
        "clean": False
    },
    "tests": {
        "targets": BUILD_TARGETS_TEST,
        "tests": [],
        "clean": False
    },
    "check": {
        "targets": BUILD_TARGETS_TEST,
        "tests": DEFAULT_TESTS,
        "clean": False
    },
    "checkall": {
        "targets": BUILD_TARGETS_ALL,
        "tests": ["ALL"],
        "clean": False
    },
    "clean": {
        "targets": [],
        "tests": [],
        "clean": True
    },
}

HELP = """<arch> can be any of: %(arches)s
<mode> can be any of: %(modes)s
<target> can be any of:
 - %(targets)s (build respective binary)
 - all (build all binaries)
 - tests (build test binaries)
 - check (build test binaries, run most tests)
 - checkall (build all binaries, run more tests)
""" % {
    "arches": " ".join(ARCHES),
    "modes": " ".join(MODES.keys()),
    "targets": ", ".join(TARGETS)
}

TESTSUITES_TARGETS = {
    "benchmarks": "d8",
    "bigint": "bigint_shell",
    "cctest": "cctest",
    "debugger": "d8",
    "fuzzer": "v8_fuzzers",
    "inspector": "inspector-test",
    "intl": "d8",
    "message": "d8",
    "mjsunit": "d8",
    "mozilla": "d8",
    "test262": "d8",
    "unittests": "v8_unittests",
    "wasm-api-tests": "wasm_api_tests",
    "wasm-js": "d8",
    "wasm-spec-tests": "d8",
    "webkit": "d8"
}

out_dir_override = os.getenv("V8_GM_OUTDIR")
if out_dir_override and Path(out_dir_override).is_file:
  OUTDIR = Path(out_dir_override)
else:
  OUTDIR = Path("out")

V8_DIR = Path(__file__).resolve().parent.parent.parent
GCLIENT_FILE_PATH = V8_DIR.parent / ".gclient"
RECLIENT_CERT_CACHE = V8_DIR / ".#gm_reclient_cert_cache"

BUILD_DISTRIBUTION_RE = re.compile(r"\nuse_(remoteexec|goma) = (false|true)")
GOMA_DIR_LINE = re.compile(r"\ngoma_dir = \"[^\"]+\"")
DEPRECATED_RBE_CFG_RE = re.compile(r"\nrbe_cfg_dir = \"[^\"]+\"")
RECLIENT_CFG_RE = re.compile(r"\nreclient_cfg_dir = \"[^\"]+\"")

class Reclient(IntEnum):
  NONE = 0
  GOOGLE = 1
  CUSTOM = 2


def get_v8_solution(solutions):
  for solution in solutions:
    if (solution["name"] == "v8" or
        solution["url"] == "https://chromium.googlesource.com/v8/v8.git"):
      return solution
  return None


# Note: this function is reused by update-compile-commands.py. When renaming
# this, please update that file too!
def detect_reclient():
  if not GCLIENT_FILE_PATH.exists():
    return Reclient.NONE
  with open(GCLIENT_FILE_PATH) as f:
    content = f.read()
  try:
    config_dict = {}
    exec(content, config_dict)
  except SyntaxError as e:
    print("# Can't detect reclient due to .gclient syntax errors.")
    return Reclient.NONE
  v8_solution = get_v8_solution(config_dict["solutions"])
  if not v8_solution:
    print("# Can't detect reclient due to missing v8 gclient solution.")
    return Reclient.NONE
  custom_vars = v8_solution.get("custom_vars", {})
  if custom_vars.get("rbe_instance"):
    return Reclient.CUSTOM
  if custom_vars.get("download_remoteexec_cfg"):
    return Reclient.GOOGLE
  return Reclient.NONE


# Note: this function is reused by update-compile-commands.py. When renaming
# this, please update that file too!
def detect_reclient_cert():
  now = int(time.time())
  # We cache the cert expiration time in a file, because that's much faster
  # to read than invoking `gcertstatus`.
  if RECLIENT_CERT_CACHE.exists():
    with open(RECLIENT_CERT_CACHE, 'r') as f:
      cached_time = int(f.read())
    if now < cached_time:
      return True
  cmd = ["gcertstatus", "-nocheck_ssh", "-format=simple"]
  ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  if ret.returncode != 0:
    return False
  MARGIN = 300  # Request fresh cert if less than 5 mins remain.
  lifetime = int(ret.stdout.decode("utf-8").strip().split(':')[1]) - MARGIN
  if lifetime < 0:
    return False
  with open(RECLIENT_CERT_CACHE, 'w') as f:
    f.write(str(now + lifetime))
  return True


RECLIENT_MODE = detect_reclient()

if platform.system() == "Linux":
  RECLIENT_CFG_REL = "../../buildtools/reclient_cfgs/linux"
else:
  RECLIENT_CFG_REL = "../../buildtools/reclient_cfgs"

BUILD_DISTRIBUTION_LINE = ""
if RECLIENT_MODE:
  BUILD_DISTRIBUTION_LINE = "\nuse_remoteexec = true"
  if RECLIENT_MODE == Reclient.CUSTOM:
    BUILD_DISTRIBUTION_LINE += f"\nreclient_cfg_dir = \"{RECLIENT_CFG_REL}\""

RELEASE_ARGS_TEMPLATE = f"""\
is_component_build = false
is_debug = false
%s{BUILD_DISTRIBUTION_LINE}
v8_enable_backtrace = true
v8_enable_disassembler = true
v8_enable_object_print = true
v8_enable_verify_heap = true
dcheck_always_on = false
"""

DEBUG_ARGS_TEMPLATE = f"""\
is_component_build = true
is_debug = true
symbol_level = 2
%s{BUILD_DISTRIBUTION_LINE}
v8_enable_backtrace = true
v8_enable_fast_mksnapshot = true
v8_enable_slow_dchecks = true
v8_optimized_debug = false
"""

OPTDEBUG_ARGS_TEMPLATE = f"""\
is_component_build = true
is_debug = true
symbol_level = 1
%s{BUILD_DISTRIBUTION_LINE}
v8_enable_backtrace = true
v8_enable_fast_mksnapshot = true
v8_enable_verify_heap = true
v8_optimized_debug = true
"""

ARGS_TEMPLATES = {
  "release": RELEASE_ARGS_TEMPLATE,
  "debug": DEBUG_ARGS_TEMPLATE,
  "optdebug": OPTDEBUG_ARGS_TEMPLATE
}


def print_help_and_exit():
  print(__doc__)
  print(HELP)
  sys.exit(0)


# Used by `tools/bash-completion.sh`
def print_completions_and_exit():
  for a in ARCHES:
    print(str(a))
    for m in set(MODES.values()):
      print(f"{a}.{m}")
      for t in TARGETS:
        print(f"{a}.{m}.{t}")
      for k in ACTIONS.keys():
        print(f"{a}.{m}.{k}")
  for t in TARGETS:
    print(str(t))
  for m in set(MODES.values()):
    print(str(m))
  sys.exit(0)


def _call(cmd, silent=False):
  if not silent:
    print(f"# {cmd}")
  return subprocess.call(cmd, shell=True)


def _call_with_output_no_terminal(cmd):
  return subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)


def _call_with_output(cmd):
  print(f"# {cmd}")
  # The following trickery is required so that the 'cmd' thinks it's running
  # in a real terminal, while this script gets to intercept its output.
  parent, child = pty.openpty()
  p = subprocess.Popen(cmd, shell=True, stdin=child, stdout=child, stderr=child)
  os.close(child)
  output = []
  try:
    while True:
      try:
        data = os.read(parent, 512).decode('utf-8')
      except OSError as e:
        if e.errno != errno.EIO: raise
        break # EIO means EOF on some systems
      else:
        if not data: # EOF
          break
        print(data, end="")
        sys.stdout.flush()
        output.append(data)
  finally:
    os.close(parent)
    p.wait()
  return p.returncode, "".join(output)


def _write(filename, content, log=True):
  if log:
    print(f"# echo > {filename} << EOF\n{content}EOF")
  with filename.open("w") as f:
    f.write(content)


def _notify(summary, body):
  if (shutil.which('notify-send') is not None and
      os.environ.get("DISPLAY") is not None):
    _call(f"notify-send '{summary}' '{body}'", silent=True)
  else:
    print(f"{summary} - {body}")


def _get_machine():
  return platform.machine()


def get_path(arch, mode):
  return OUTDIR / f"{arch}.{mode}"


def prepare_mksnapshot_cmdline(orig_cmdline, path):
  mksnapshot_bin = path / "mksnapshot"
  result = f"gdb --args {mksnapshot_bin} "
  for w in orig_cmdline.split(" "):
    if w.startswith("gen/") or w.startswith("snapshot_blob"):
      result += f"{str(path / w)} "
    elif w.startswith("../../"):
      result += f"{w[6:]} "
    else:
      result += f"{w} "
  return result


def prepare_torque_cmdline(orig_cmdline: str, path):
  torque_bin = path / "torque"
  args = orig_cmdline.replace("-v8-root ../..", "-v8-root .")
  args = args.replace("gen/torque-generated", f"{path}/gen/torque-generated")
  return f"gdb --args {torque_bin} {args}"

# Only has a path, assumes that the path (and args.gn in it) already exists.
class RawConfig:

  def __init__(self, path, targets, tests=[], clean=False, testrunner_args=[]):
    self.path = path
    self.targets = set(targets)
    self.tests = set(tests)
    self.testrunner_args = testrunner_args
    self.clean = clean

  def extend(self, targets, tests=[], clean=False):
    self.targets.update(targets)
    self.tests.update(tests)
    self.clean |= clean

  def update_build_distribution_args(self):
    args_gn = self.path / "args.gn"
    assert args_gn.exists()
    with open(args_gn) as f:
      gn_args = f.read()
    # Remove custom reclient config path (it will be added again as part of
    # the config line below if needed).
    new_gn_args = DEPRECATED_RBE_CFG_RE.sub("", gn_args)
    new_gn_args = RECLIENT_CFG_RE.sub("", new_gn_args)
    new_gn_args = BUILD_DISTRIBUTION_RE.sub(BUILD_DISTRIBUTION_LINE,
                                            new_gn_args)
    # Remove stale goma_dir to silence GN warnings about unused options.
    new_gn_args = GOMA_DIR_LINE.sub("", new_gn_args)
    if gn_args != new_gn_args:
      print(f"# Updated gn args:{BUILD_DISTRIBUTION_LINE}")
      _write(args_gn, new_gn_args, log=False)

  def build(self):
    self.update_build_distribution_args()
    # If the target is to just build args.gn then we are done here; otherwise
    # drop that target because it's not something ninja can build.
    if 'gn_args' in self.targets:
      self.targets.remove('gn_args')
    if len(self.targets) == 0:
      return 0
    build_ninja = self.path / "build.ninja"
    if not build_ninja.exists():
      code = _call(f"gn gen {self.path}")
      if code != 0:
        return code
    elif self.clean:
      code = _call(f"gn clean {self.path}")
      if code != 0:
        return code
    targets = " ".join(self.targets)
    # The implementation of mksnapshot failure detection relies on
    # the "pty" module and GDB presence, so skip it on non-Linux.
    if not USE_PTY:
      return _call(f"autoninja -C {self.path} {targets}")

    return_code, output = _call_with_output(
        f"autoninja -C {self.path} {targets}")
    if return_code != 0 and "FAILED:" in output:
      if "snapshot_blob" in output:
        if "gen-static-roots.py" in output:
          _notify("V8 build requires your attention",
                  "Please re-generate static roots...")
          return return_code
        csa_trap = re.compile("Specify option( --csa-trap-on-node=[^ ]*)")
        match = csa_trap.search(output)
        extra_opt = match.group(1) if match else ""
        cmdline = re.compile("python3 ../../tools/run.py ./mksnapshot (.*)")
        orig_cmdline = cmdline.search(output).group(1).strip()
        cmdline = (
            prepare_mksnapshot_cmdline(orig_cmdline, self.path) + extra_opt)
        _notify("V8 build requires your attention",
                "Detected mksnapshot failure, re-running in GDB...")
        _call(cmdline)
      elif "run.py ./torque" in output and not ": Torque Error: " in output:
        # Torque failed/crashed without printing an error message.
        cmdline = re.compile("python3 ../../tools/run.py ./torque (.*)")
        orig_cmdline = cmdline.search(output).group(1).strip()
        cmdline = f"gdb --args "
        cmdline = prepare_torque_cmdline(orig_cmdline, self.path)
        _notify("V8 build requires your attention",
                "Detecting torque failure, re-running in GDB...")
        _call(cmdline)
    return return_code

  def run_tests(self):
    if not self.tests:
      return 0
    if "ALL" in self.tests:
      tests = ""
    else:
      tests = " ".join(self.tests)
    run_tests = Path("tools") / "run-tests.py"
    test_runner_args = " ".join(self.testrunner_args)
    return _call(
        f'"{sys.executable }" {run_tests} --outdir={self.path} {tests} {test_runner_args}'
    )


# Contrary to RawConfig, takes arch and mode, and sets everything up
# automatically.
# Note: This class is imported by update-compile-commands.py. When renaming
# anything here, please update that script too!
class ManagedConfig(RawConfig):

  def __init__(self,
               arch,
               mode,
               targets,
               tests=[],
               clean=False,
               testrunner_args=[]):
    super().__init__(
        get_path(arch, mode), targets, tests, clean, testrunner_args)
    self.arch = arch
    self.mode = mode

  def get_target_cpu(self):
    cpu = "x86"
    if self.arch == "android_arm":
      cpu = "arm"
    elif self.arch == "android_arm64" or self.arch == "fuchsia_arm64":
      cpu = "arm64"
    elif self.arch == "android_riscv64":
      cpu = "riscv64"
    elif self.arch == "arm64" and _get_machine() in ("aarch64", "arm64"):
      # arm64 build host:
      cpu = "arm64"
    elif self.arch == "arm" and _get_machine() in ("aarch64", "arm64"):
      cpu = "arm"
    elif self.arch == "loong64" and _get_machine() == "loongarch64":
      cpu = "loong64"
    elif self.arch == "mips64el" and _get_machine() == "mips64":
      cpu = "mips64el"
    elif "64" in self.arch or self.arch == "s390x":
      # Native x64 or simulator build.
      cpu = "x64"
    return [f"target_cpu = \"{cpu}\""]

  def get_v8_target_cpu(self):
    if self.arch == "android_arm":
      v8_cpu = "arm"
    elif self.arch == "android_arm64" or self.arch == "fuchsia_arm64":
      v8_cpu = "arm64"
    elif self.arch == "android_riscv64":
      v8_cpu = "riscv64"
    elif self.arch in ("arm", "arm64", "mips64el", "ppc", "ppc64", "riscv64",
                       "riscv32", "s390", "s390x", "loong64"):
      v8_cpu = self.arch
    else:
      return []
    return [f"v8_target_cpu = \"{v8_cpu}\""]

  def get_target_os(self):
    if self.arch in ("android_arm", "android_arm64", "android_riscv64"):
      return ["target_os = \"android\""]
    elif self.arch in ("fuchsia_x64", "fuchsia_arm64"):
      return ["target_os = \"fuchsia\""]
    return []

  def get_specialized_compiler(self):
    if _get_machine() in ("aarch64", "mips64", "loongarch64"):
      # We have no prebuilt Clang for arm64, mips64 or loongarch64 on Linux,
      # so use the system Clang instead.
      return ["clang_base_path = \"/usr\"", "clang_use_chrome_plugins = false"]
    return []

  def get_sandbox_flag(self):
    if self.arch in SANDBOX_SUPPORTED_ARCHES:
      return ["v8_enable_sandbox = true"]
    return []

  def get_gn_args(self):
    # Use only substring before first '-' as the actual mode
    mode = re.match("([^-]+)", self.mode).group(1)
    template = ARGS_TEMPLATES[mode]
    arch_specific = (
        self.get_target_cpu() + self.get_v8_target_cpu() +
        self.get_target_os() + self.get_specialized_compiler() +
        self.get_sandbox_flag())
    return template % "\n".join(arch_specific)

  def build(self):
    path = self.path
    args_gn = path / "args.gn"
    if not path.exists():
      print(f"# mkdir -p {path}")
      path.mkdir(parents=True)
    if not args_gn.exists():
      _write(args_gn, self.get_gn_args())
    return super().build()

  def run_tests(self):
    host_arch = _get_machine()
    if host_arch == "arm64":
      if platform.system() == "Darwin" and self.arch != "arm64":
        # MacOS-arm64 doesn't provide a good experience when users
        # accidentally try to run x64 tests.
        print(f"Running {self.arch} tests on Mac-arm64 isn't going to work.")
        return 1
      if platform.system() == "Linux" and "arm" not in self.arch:
        # Assume that an arm64 Linux machine may have been set up to run
        # arm32 binaries and Android on-device tests; refuse everything else.
        print(f"Running {self.arch} tests on Linux-arm64 isn't going to work.")
        return 1
    # Special handling for "mkgrokdump": if it was built, run it.
    if ("mkgrokdump" in self.targets and self.mode == "release" and
        (host_arch == "x86_64" and self.arch == "x64") or
        (host_arch == "arm64" and self.arch == "arm64")):
      mkgrokdump_bin = self.path / "mkgrokdump"
      _call(f"{mkgrokdump_bin} > tools/v8heapconst.py")
    return super().run_tests()


def get_test_binary(argstring):
  for suite in TESTSUITES_TARGETS:
    if argstring.startswith(suite): return TESTSUITES_TARGETS[suite]
  return None

class ArgumentParser(object):
  def __init__(self):
    self.global_targets = set()
    self.global_tests = set()
    self.global_actions = set()
    self.configs = {}
    self.testrunner_args = []

  def populate_configs(self, arches, modes, targets, tests, clean):
    for a in arches:
      for m in modes:
        path = get_path(a, m)
        if path not in self.configs:
          self.configs[path] = ManagedConfig(a, m, targets, tests, clean,
                                             self.testrunner_args)
        else:
          self.configs[path].extend(targets, tests)

  def process_global_actions(self):
    have_configs = len(self.configs) > 0
    for action in self.global_actions:
      impact = ACTIONS[action]
      if (have_configs):
        for c in self.configs:
          self.configs[c].extend(**impact)
      else:
        self.populate_configs(DEFAULT_ARCHES, DEFAULT_MODES, **impact)

  def maybe_parse_builddir(self, argstring):
    outdir_prefix = str(OUTDIR) + os.path.sep
    # {argstring} must have the shape "out/x", and the 'x' part must be
    # at least one character.
    if not argstring.startswith(outdir_prefix):
      return False
    if len(argstring) <= len(outdir_prefix):
      return False
    # "out/foo.d8" -> path="out/foo", targets=["d8"]
    # "out/d8.cctest" -> path="out/d8", targets=["cctest"]
    # "out/x.y.d8.cctest" -> path="out/x.y", targets=["d8", "cctest"]
    words = argstring.split('.')
    path_end = len(words)
    targets = []
    tests = []
    clean = False
    while path_end > 1:
      w = words[path_end - 1]
      maybe_target = get_test_binary(w)
      if w in TARGETS:
        targets.append(w)
      elif maybe_target is not None:
        targets.append(maybe_target)
        tests.append(w)
      elif w == 'clean':
        clean = True
      else:
        break
      path_end -= 1
    targets = targets or DEFAULT_TARGETS
    path = Path('.'.join(words[:path_end]))
    args_gn = path / "args.gn"
    # Only accept existing build output directories, otherwise fall back
    # to regular parsing.
    if not args_gn.is_file():
      return False
    if path not in self.configs:
      self.configs[path] = RawConfig(path, targets, tests, clean,
                                     self.testrunner_args)
    else:
      self.configs[path].extend(targets, tests, clean)
    return True

  def parse_arg(self, argstring):
    if argstring in ("-h", "--help", "help"):
      print_help_and_exit()
    if argstring == "--print-completions":
      print_completions_and_exit()
    arches = []
    modes = []
    targets = []
    actions = []
    tests = []
    clean = False
    # Special handling for "mkgrokdump": build it for (arm64|x64).release.
    if argstring == "mkgrokdump":
      arch = "x64"
      if _get_machine() in ("aarch64", "arm64"):
        arch = "arm64"
      self.populate_configs([arch], ["release"], ["mkgrokdump"], [], False)
      return
    if argstring.startswith("--"):
      # Pass all other flags to test runner.
      self.testrunner_args.append(argstring)
      return
    # Specifying a directory like "out/foo" enters "manual mode".
    if self.maybe_parse_builddir(argstring):
      return
    # Specifying a single unit test looks like "unittests/Foo.Bar", test262
    # tests have names like "S15.4.4.7_A4_T1", don't split these.
    if (argstring.startswith("unittests/") or
        argstring.startswith("test262/") or
        argstring.startswith("wasm-api-tests/")):
      words = [argstring]
    else:
      # Assume it's a word like "x64.release" -> split at the dot.
      words = argstring.split('.')
    if len(words) == 1:
      word = words[0]
      if word in ACTIONS:
        self.global_actions.add(word)
        return
      if word in TARGETS:
        self.global_targets.add(word)
        return
      maybe_target = get_test_binary(word)
      if maybe_target is not None:
        self.global_tests.add(word)
        self.global_targets.add(maybe_target)
        return
    for word in words:
      if word in ARCHES:
        arches.append(word)
      elif word in MODES:
        modes.append(MODES[word])
      elif word in TARGETS:
        targets.append(word)
      elif word in ACTIONS:
        actions.append(word)
      else:
        for mode in MODES.keys():
          if word.startswith(mode + "-"):
            prefix = word[:len(mode)]
            suffix = word[len(mode) + 1:]
            modes.append(MODES[prefix] + "-" + suffix)
            break
        else:
          print(f"Didn't understand: {word}")
          sys.exit(1)
    # Process actions.
    for action in actions:
      impact = ACTIONS[action]
      targets += impact["targets"]
      tests += impact["tests"]
      clean |= impact["clean"]
    # Fill in defaults for things that weren't specified.
    arches = arches or DEFAULT_ARCHES
    modes = modes or DEFAULT_MODES
    targets = targets or DEFAULT_TARGETS
    # Produce configs.
    self.populate_configs(arches, modes, targets, tests, clean)

  def parse_arguments(self, argv):
    if len(argv) == 0:
      print_help_and_exit()
    for argstring in argv:
      self.parse_arg(argstring)
    self.process_global_actions()
    for c in self.configs:
      self.configs[c].extend(self.global_targets, self.global_tests)
    return self.configs


def main(argv):
  parser = ArgumentParser()
  configs = parser.parse_arguments(argv[1:])
  return_code = 0
  # If we have Reclient with the Google configuration, check for current
  # certificate.
  if (RECLIENT_MODE == Reclient.GOOGLE and not detect_reclient_cert()):
    print("# gcert")
    subprocess.check_call("gcert", shell=True)
  for c in configs:
    return_code += configs[c].build()
  if return_code == 0:
    for c in configs:
      return_code += configs[c].run_tests()
  if return_code == 0:
    _notify('Done!', 'V8 compilation finished successfully.')
  else:
    _notify('Error!', 'V8 compilation finished with errors.')
  return return_code

if __name__ == "__main__":
  sys.exit(main(sys.argv))
