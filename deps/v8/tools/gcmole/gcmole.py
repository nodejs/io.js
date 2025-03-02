#!/usr/bin/env python3
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is main driver for gcmole tool. See README for more details.
# Usage: CLANG_BIN=clang-bin-dir python tools/gcmole/gcmole.py [arm|arm64|ia32|x64]

from contextlib import contextmanager
from contextlib import redirect_stderr
from multiprocessing import cpu_count
from pathlib import Path

import argparse
import collections
import difflib
import io
import json
import os
import pickle
import re
import subprocess
import sys
import threading
import queue


ArchCfg = collections.namedtuple("ArchCfg",
                                 ["name", "cpu", "triple", "arch_options"])

ARCHITECTURES = {
    "ia32":
        ArchCfg(
            name="ia32",
            cpu="x86",
            triple="i586-unknown-linux",
            arch_options=["-m32"],
        ),
    "arm":
        ArchCfg(
            name="arm",
            cpu="arm",
            triple="i586-unknown-linux",
            arch_options=["-m32"],
        ),
    "x64":
        ArchCfg(
            name="x64",
            cpu="x64",
            triple="x86_64-unknown-linux",
            arch_options=[]),
    "arm64":
        ArchCfg(
            name="arm64",
            cpu="arm64",
            triple="x86_64-unknown-linux",
            arch_options=[],
        ),
}
ARCHITECTURES['x86'] = ARCHITECTURES['ia32']


def log(format, *args, **kwargs):
  mark = ("#", "=", "-", ".")[kwargs.get("level", 0)]
  print(mark * 2, str(format).format(*list(map(str, args))))


def fatal(format):
  log(format)
  sys.exit(1)


# -----------------------------------------------------------------------------
# Clang invocation


def make_clang_command_line(plugin, plugin_args, options):
  with open(options.v8_build_dir / 'v8_gcmole.args') as f:
    generated_args = f.read().strip().split()

  arch_cfg = ARCHITECTURES[options.v8_target_cpu]
  prefixed_plugin_args = []
  if plugin_args:
    for arg in plugin_args:
      prefixed_plugin_args += [
          "-Xclang",
          "-plugin-arg-" + plugin,
          "-Xclang",
          arg,
      ]
  log("Using generated files in {}", options.v8_build_dir / 'gen')
  return ([
      options.clang_bin_dir / "clang++",
      "-std=c++20",
      "-c",
      "-Xclang",
      "-load",
      "-Xclang",
      options.clang_plugins_dir / "libgcmole.so",
      "-Xclang",
      "-plugin",
      "-Xclang",
      plugin,
  ] + prefixed_plugin_args + [
      "-Xclang",
      "-triple",
      "-Xclang",
      arch_cfg.triple,
      "-fno-exceptions",
      "-Wno-everything",
      "-DV8_GC_MOLE",
  ] + generated_args + arch_cfg.arch_options)


def invoke_clang_plugin_for_file(filename, cmd_line, verbose):
  args = cmd_line + [filename]
  args = list(map(str, args))
  if verbose:
    print("popen ", " ".join(args))
  p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  stdout, stderr = p.communicate()
  return p.returncode, stdout.decode("utf-8"), stderr.decode("utf-8")


def invoke_clang_plugin_for_files_in_queue(i, input_queue, output_queue,
                                           cancel_event, cmd_line, verbose):
  success = False
  try:
    while not cancel_event.is_set():
      filename = input_queue.get_nowait()
      ret, stdout, stderr = invoke_clang_plugin_for_file(
          filename, cmd_line, verbose)
      output_queue.put_nowait((filename, ret, stdout, stderr))
      if ret != 0:
        break
  except KeyboardInterrupt:
    log("[{}] Interrupting", i, level=1)
  except queue.Empty:
    success = True
  finally:
    # Emit a success bool so that the reader knows that there was either an
    # error or all files were processed.
    output_queue.put_nowait(success)


def invoke_clang_plugin_for_each_file(filenames, plugin, plugin_args, options):
  cmd_line = make_clang_command_line(plugin, plugin_args, options)
  verbose = options.verbose
  if options.sequential:
    log("Sequential execution.")
    for filename in filenames:
      log(filename, level=1)
      returncode, stdout, stderr = invoke_clang_plugin_for_file(
          filename, cmd_line, verbose)
      if returncode != 0:
        sys.stderr.write(stderr)
        sys.exit(returncode)
      yield filename, stdout, stderr
  else:
    log("Parallel execution.")
    cpus = cpu_count()
    input_queue = queue.Queue()
    output_queue = queue.Queue()
    threads = []
    try:
      for filename in filenames:
        input_queue.put(filename)

      cancel_event = threading.Event()

      for i in range(min(len(filenames), cpus)):
        threads.append(
            threading.Thread(
                target=invoke_clang_plugin_for_files_in_queue,
                args=(i, input_queue, output_queue, cancel_event, cmd_line,
                      verbose)))

      for t in threads:
        t.start()

      num_finished = 0
      while num_finished < len(threads):
        output = output_queue.get()
        if type(output) == bool:
          if output:
            num_finished += 1
            continue
          else:
            break
        filename, returncode, stdout, stderr = output
        log(filename, level=2)
        if returncode != 0:
          sys.stderr.write(stderr)
          sys.exit(returncode)
        yield filename, stdout, stderr

    finally:
      cancel_event.set()
      for t in threads:
        t.join()


# -----------------------------------------------------------------------------


def build_file_list(options):
  """Calculates the list of source files to be checked with gcmole.

  The list comprises all files from marked source sections in the
  listed BUILD.gn files. All files preceeded by the following comment and
  until the end of the source section are used:
  ### gcmole(arch) ###
  Where arch can either be all (all architectures) or one of the supported V8
  architectures.

  The structure of these directives is also checked by presubmit via:
  tools/v8_presubmit.py::GCMoleProcessor.

  Returns: List of file paths (of type Path).
  """
  if options.test_run:
    return [options.v8_root_dir / "tools/gcmole/gcmole-test.cc"]
  result = []
  gn_files = [
      ("BUILD.gn", re.compile('"([^"]*?\.cc)"'), ""),
      ("test/cctest/BUILD.gn", re.compile('"(test-[^"]*?\.cc)"'),
       Path("test/cctest/")),
  ]
  gn_re = re.compile(f"### gcmole\((all|{options.v8_target_cpu})\) ###(.*?)\]",
                     re.MULTILINE | re.DOTALL)
  for filename, file_pattern, prefix in gn_files:
    path = options.v8_root_dir / filename
    with open(path) as gn_file:
      gn = gn_file.read()
      for _, sources in gn_re.findall(gn):
        for file in file_pattern.findall(sources):
          result.append(options.v8_root_dir / prefix / file)

  # Filter files of current shard if running on multiple hosts.
  def is_in_shard(index):
    return (index % options.shard_count) == options.shard_index

  return [f for i, f in enumerate(result) if is_in_shard(i)]


# -----------------------------------------------------------------------------
# GCSuspects Generation

# Note that the gcsuspects file lists functions in the form:
#  mangled_name,unmangled_function_name
#
# This means that we can match just the function name by matching only
# after a comma.
ALLOWLIST = [
    # The following functions call CEntryStub which is always present.
    "MacroAssembler.*,CallRuntime",
    "CompileCallLoadPropertyWithInterceptor",
    "CallIC.*,GenerateMiss",
    # DirectCEntryStub is a special stub used on ARM.
    # It is pinned and always present.
    "DirectCEntryStub.*,GenerateCall",
    # TODO GCMole currently is sensitive enough to understand that certain
    #    functions only cause GC and return Failure simulataneously.
    #    Callsites of such functions are safe as long as they are properly
    #    check return value and propagate the Failure to the caller.
    #    It should be possible to extend GCMole to understand this.
    "Heap.*,TryEvacuateObject",
    # Ignore all StateTag methods.
    "StateTag",
    # Ignore printing of elements transition.
    "PrintElementsTransition",
    # CodeCreateEvent receives AbstractCode (a raw ptr) as an argument.
    "CodeCreateEvent",
    "WriteField",
]

GC_PATTERN = ",.*Collect.*Garbage"
SAFEPOINT_PATTERN = ",SafepointSlowPath"
ALLOWLIST_PATTERN = "|".join("(?:{})".format(p) for p in ALLOWLIST)


def merge_regexp(pattern_dict):
  return re.compile("|".join("(?P<{}>{})".format(key, value)
                             for (key, value) in list(pattern_dict.items())))


IS_SPECIAL_WITHOUT_ALLOW_LIST = merge_regexp({
    "gc": GC_PATTERN,
    "safepoint": SAFEPOINT_PATTERN
})
IS_SPECIAL_WITH_ALLOW_LIST = merge_regexp({
    "gc": GC_PATTERN,
    "safepoint": SAFEPOINT_PATTERN,
    "allow": ALLOWLIST_PATTERN
})


class CallGraph:

  def __init__(self):
    self.funcs = collections.defaultdict(set)
    self.current_caller = None

  def parse(self, lines):
    for funcname in lines:
      if not funcname:
        continue

      if funcname[0] != "\t":
        # Always inserting the current caller makes the serialized version
        # more compact.
        self.funcs[funcname]
        self.current_caller = funcname
      else:
        self.funcs[funcname[1:]].add(self.current_caller)

  def to_file(self, file_name):
    """Store call graph in file 'file_name'."""
    log(f"Writing serialized callgraph to {file_name}")
    with open(file_name, 'wb') as f:
      pickle.dump(self, f)

  @staticmethod
  def from_file(file_name):
    """Restore call graph from file 'file_name'."""
    log(f"Reading serialized callgraph from {file_name}")
    with open(file_name, 'rb') as f:
      return pickle.load(f)

  @staticmethod
  def from_files(*file_names):
    """Merge multiple call graphs from a list of files."""
    callgraph = CallGraph()
    for file_name in file_names:
      funcs = CallGraph.from_file(file_name).funcs
      for callee, callers in funcs.items():
        callgraph.funcs[callee].update(callers)
    return callgraph


class GCSuspectsCollector:

  def __init__(self, options, funcs):
    self.gc = {}
    self.gc_caused = collections.defaultdict(set)
    self.funcs = funcs
    if options.allowlist:
      self.is_special = IS_SPECIAL_WITH_ALLOW_LIST
    else:
      self.is_special = IS_SPECIAL_WITHOUT_ALLOW_LIST

  def add_cause(self, name, cause):
    self.gc_caused[name].add(cause)

  def resolve(self, name):
    m = self.is_special.search(name)
    if not m:
      return

    if m.group("gc"):
      self.gc[name] = True
      self.add_cause(name, "<GC>")
    elif m.group("safepoint"):
      self.gc[name] = True
      self.add_cause(name, "<Safepoint>")
    elif m.group("allow"):
      self.gc[name] = False

  def propagate(self):
    log("Propagating GC information")

    def mark(funcname):
      for caller in self.funcs[funcname]:
        if caller not in self.gc:
          self.gc[caller] = True
          mark(caller)
        self.add_cause(caller, funcname)

    for funcname in self.funcs:
      self.resolve(funcname)

    for funcname in self.funcs:
      if self.gc.get(funcname, False):
        mark(funcname)


def generate_callgraph(files, options):
  """Construct a (potentially partial) call graph from a subset of
  source files.
  """
  callgraph = CallGraph()

  log(f"Building call graph for {options.v8_target_cpu}")
  for _, stdout, _ in invoke_clang_plugin_for_each_file(
      files, "dump-callees", [], options):
    callgraph.parse(stdout.splitlines())

  return callgraph


def generate_gc_suspects_from_callgraph(callgraph, options):
  """Calculate and store gc-suspect information from a given call graph."""
  collector = GCSuspectsCollector(options, callgraph.funcs)
  collector.propagate()
  # TODO(cbruni): remove once gcmole.cc is migrated
  write_gcmole_results(collector, options, options.v8_root_dir)
  write_gcmole_results(collector, options, options.out_dir)


def generate_gc_suspects_from_files(options):
  """Generate file list and corresponding gc-suspect information."""
  files = build_file_list(options)
  call_graph = generate_callgraph(files, options)
  generate_gc_suspects_from_callgraph(call_graph, options)
  return files


def write_gcmole_results(collector, options, dst):
  # gcsuspects contains a list("mangled_full_name,name") of all functions that
  # could cause a gc (directly or indirectly).
  #
  # EXAMPLE
  # _ZN2v88internal4Heap16CreateApiObjectsEv,CreateApiObjects
  # _ZN2v88internal4Heap17CreateInitialMapsEv,CreateInitialMaps
  # ...
  with open(dst / "gcsuspects", "w") as out:
    for name, value in list(collector.gc.items()):
      if value:
        out.write(name + "\n")
  # gccauses contains a map["mangled_full_name,name"] => list(inner gcsuspects)
  # Where the inner gcsuspects are functions directly called in the outer
  # function that can cause a gc. The format is encoded for simplified
  # deserialization in gcmole.cc.
  #
  # EXAMPLE:
  # _ZN2v88internal4Heap17CreateHeapObjectsEv,CreateHeapObjects
  # start,nested
  # _ZN2v88internal4Heap16CreateApiObjectsEv,CreateApiObjects
  # _ZN2v88internal4Heap17CreateInitialMapsEv,CreateInitialMaps
  # ...
  # end,nested
  # ...
  with open(dst / "gccauses", "w") as out:
    for name, causes in list(collector.gc_caused.items()):
      out.write("{}\n".format(name))
      out.write("start,nested\n")
      for cause in causes:
        out.write("{}\n".format(cause))
      out.write("end,nested\n")
  log("GCSuspects and gccauses generated for {} in '{}'", options.v8_target_cpu,
      dst)


# ------------------------------------------------------------------------------
# Analysis


def check_correctness_for_arch(files, options):
  processed_files = 0
  errors_found = False

  log("Searching for evaluation order problems " +
      ("and dead variables " if options.dead_vars else "") + "for " +
      options.v8_target_cpu)
  plugin_args = []
  if options.dead_vars:
    plugin_args.append("--dead-vars")
  if options.verbose:
    plugin_args.append("--verbose")
  if options.verbose_trace:
    plugin_args.append("--verbose-trace")
  for _, _, stderr in invoke_clang_plugin_for_each_file(files, "find-problems",
                                                        plugin_args, options):
    processed_files = processed_files + 1
    if not errors_found:
      errors_found = re.search("^[^:]+:\d+:\d+: (warning|error)", stderr,
                               re.MULTILINE) is not None
    sys.stderr.write(stderr)

  log("Done processing {} files.", processed_files)
  log("Errors found" if errors_found else "No errors found")

  return errors_found


def clean_test_output(output):
  """Substitute line number patterns for files except gcmole-test.cc, as
  otherwise unrelated code changes require a rebaseline of test expectations.
  """

  def clean_block(m):
    # For blocks whose filename is gcmole-test.cc, leave the block unchanged.
    if m.group("file").endswith("gcmole-test.cc"):
      return m.group(0)

    # Otherwise, clear the line numbers both from the main message, and from the
    # individually printed line numbers from the printed source. That is, change
    #
    #   ./src/heap/heap.h:934:21: note: GC call here.
    #      934 |   V8_EXPORT_PRIVATE void CollectGarbage(
    #          |                     ^
    #
    # into
    #
    #   ./src/heap/heap.h:<number>:<number>: note: GC call here.
    #          |   V8_EXPORT_PRIVATE void CollectGarbage(
    #          |                     ^
    block = m.group("block")

    def clear_line_number(m):
      return f"{m.group(1)}{' ' * len(m.group(2))} |{m.group(3)}"

    block = re.sub(r'(\s*)(\d+) \|(.*)', clear_line_number, block)

    return f"{m.group('file')}:<number>:<number>:{m.group('msg')}\n{block}"

  # Substitute within blocks of the form:
  #   ./src/heap/heap.h:934:21: note: GC call here.
  #      934 |   V8_EXPORT_PRIVATE void CollectGarbage(
  #          |           ^
  return re.sub(
      r'(?P<file>\S+):(?P<pos>\d+:\d+):(?P<msg>.*)\n(?P<block>(\s*\d* \|.*\n)*)',
      clean_block, output)

def has_unexpected_errors(options, errors_found, file_io):
  """Returns True if error state isn't as expected, False otherwise.

  In test-run mode, we expect certain errors and return False if expectations
  are met.
  """
  if not options.test_run:
    return errors_found

  log("Test Run")
  output = clean_test_output(file_io.getvalue())
  if not errors_found:
    log("Test file should produce errors, but none were found. Output:")
    print(output)
    return True

  new_file = options.out_dir / "test-expectations-gen.txt"
  with open(new_file, "w") as f:
    f.write(output)
  log("Wrote test-results: {}", new_file)

  expected_file = options.v8_root_dir / "tools/gcmole/test-expectations.txt"
  with open(expected_file) as exp_file:
    expectations = exp_file.read()

  if output != expectations:
    diff_file = options.out_dir / "test_output.diff"
    print("#" * 79)
    log("Output mismatch from running tests.")
    log("Please run gcmole manually with --test-run --verbose.")
    log(f"Expected: {expected_file}")
    log(f"New:      {new_file}")
    log(f"*Diff:*   {diff_file}")
    print("#" * 79)
    for line in difflib.unified_diff(
        expectations.splitlines(),
        output.splitlines(),
        fromfile=str(new_file),
        tofile=str(diff_file),
        lineterm="",
    ):
      print(line)

    print("#" * 79)
    log("Full output")
    log(f"Expected: {expected_file}")
    log(f"Diff:     {diff_file}")
    log(f"*New*:    {new_file}")
    print("#" * 79)
    print(output)
    print("#" * 79)

    return True

  log("Tests ran successfully")
  return False


# =============================================================================
def relative_parents(path, level=0):
  return Path(os.path.relpath(str(path.resolve().parents[level])))


def main(argv):
  # Get a clean parent path relative to PWD
  default_root_dir = relative_parents(Path(__file__), level=2)
  if len(argv) >= 1:
    default_gcmole_dir = relative_parents(Path(argv[0]))
  if default_gcmole_dir or not default_gcmole_dir.exists():
    default_gcmole_dir = default_root_dir / 'tools' / 'gcmole'
  default_clang_bin_dir = default_gcmole_dir / 'gcmole-tools/bin'

  def add_common_args(parser):
    archs = list(ARCHITECTURES.keys())
    parser.add_argument(
        "--v8-root-dir",
        metavar="DIR",
        default=default_root_dir,
        help="V8 checkout directory. Default: '{}'".format(
            default_root_dir.absolute()))
    parser.add_argument(
        "--v8-target-cpu",
        default="x64",
        choices=archs,
        help="Tested CPU architecture. Choices: {}".format(archs),
        metavar="CPU")
    parser.add_argument(
        "--clang-bin-dir",
        metavar="DIR",
        help="Build dir of the custom clang version for gcmole." + \
        "Default: env['CLANG_DIR'] or '{}'".format(default_clang_bin_dir))
    parser.add_argument(
        "--clang-plugins-dir",
        metavar="DIR",
        help="Containing dir for libgcmole.so."
        "Default: env['CLANG_PLUGINS'] or '{}'".format(default_gcmole_dir))
    parser.add_argument(
        "--v8-build-dir",
        metavar="BUILD_DIR",
        help="GN build dir for v8. Default: 'out/CPU.Release'. "
        "Config must match cpu specified by --v8-target-cpu")
    parser.add_argument(
        "--out-dir",
        metavar="DIR",
        help="Output location for the gcsuspect and gcauses file."
        "Default: BUILD_DIR/gen/tools/gcmole")
    parser.add_argument(
        "--is-bot",
        action="store_true",
        default=False,
        help="Flag for setting build bot specific settings.")
    parser.add_argument(
        "--shard-count",
        default=1,
        type=int,
        help="Number of tasks the current action (e.g. collect or check) "
             "is distributed to.")
    parser.add_argument(
        "--shard-index",
        default=0,
        type=int,
        help="Index of the current task (in [0..shard-count-1]) if the "
             "overall action is distributed (shard-count > 1).")

    group = parser.add_argument_group("GCMOLE options")
    group.add_argument(
        "--sequential",
        action="store_true",
        default=False,
        help="Don't use parallel python runner.")
    group.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Print commands to console before executing them.")
    group.add_argument(
        "--no-dead-vars",
        action="store_false",
        dest="dead_vars",
        default=True,
        help="Don't perform dead variable analysis.")
    group.add_argument(
        "--verbose-trace",
        action="store_true",
        default=False,
        help="Enable verbose tracing from the plugin itself."
        "This can be useful to debug finding dead variable.")
    group.add_argument(
        "--no-allowlist",
        action="store_true",
        default=True,
        dest="allowlist",
        help="When building gcsuspects allowlist certain functions as if they can be "
        "causing GC. Currently used to reduce number of false positives in dead "
        "variables analysis. See TODO for ALLOWLIST in gcmole.py")
    group.add_argument(
        "--test-run",
        action="store_true",
        default=False,
        help="Test gcmole on tools/gcmole/gcmole-test.cc")

  parser = argparse.ArgumentParser()
  subps = parser.add_subparsers()

  subp = subps.add_parser(
      "full", description="Run both gcmole analysis passes.")
  add_common_args(subp)
  subp.set_defaults(func=full_run)

  subp = subps.add_parser(
      "collect",
      description="Construct call graph from source files. "
                  "The action can be distributed using --shard-count and "
                  "--shard-index.")
  add_common_args(subp)
  subp.set_defaults(func=collect_run)
  subp.add_argument(
      "--output",
      required=True,
      help="Path to a file where to store the constructed call graph")

  subp = subps.add_parser(
      "merge",
      description="Merge partial call graphs and propagate gc suspects.")
  add_common_args(subp)
  subp.set_defaults(func=merge_run)
  subp.add_argument(
      "--input",
      action='append',
      required=True,
      help="Path to a file containing a partial call graph stored by "
           "'collect'. Repeat for multiple files.")

  subp = subps.add_parser(
      "check",
      description="Check for problems using previously collected gc-suspect "
                  "information. The action can be distributed using "
                  "--shard-count and --shard-index.")
  add_common_args(subp)
  subp.set_defaults(func=check_run)

  options = parser.parse_args(argv[1:])

  verify_and_convert_dirs(parser, options, default_gcmole_dir,
                          default_clang_bin_dir)
  verify_clang_plugin(parser, options)
  prepare_gcmole_files(options)
  verify_build_config(parser, options)
  override_env_options(options)

  options.func(options)


@contextmanager
def maybe_redirect_stderr(options):
  file_io = io.StringIO() if options.test_run else sys.stderr
  with redirect_stderr(file_io) as f:
    yield f


def check_files(options, files):
  with maybe_redirect_stderr(options) as file_io:
    errors_found = check_correctness_for_arch(files, options)
  sys.exit(has_unexpected_errors(options, errors_found, file_io))


def full_run(options):
  check_files(options, generate_gc_suspects_from_files(options))


def collect_run(options):
  files = build_file_list(options)
  callgraph = generate_callgraph(files, options)
  callgraph.to_file(options.output)


def merge_run(options):
  generate_gc_suspects_from_callgraph(
      CallGraph.from_files(*options.input), options)


def check_run(options):
  check_files(options, build_file_list(options))


def verify_and_convert_dirs(parser, options, default_tools_gcmole_dir,
                            default_clang_bin_dir):
  # Verify options for setting directors and convert the input strings to Path
  # objects.
  options.v8_root_dir = Path(options.v8_root_dir)

  if not options.clang_bin_dir:
    # Backwards compatibility
    if os.getenv("CLANG_BIN"):
      options.clang_bin_dir = Path(os.getenv("CLANG_BIN"))
      options.is_bot = True
    else:
      options.clang_bin_dir = default_clang_bin_dir
      if not (options.clang_bin_dir / 'clang++').exists():
        options.clang_bin_dir = Path(options.v8_root_dir,
                                     "tools/gcmole/bootstrap/build/bin")
    log("Using --clang-bin-dir={}", options.clang_bin_dir)
  else:
    options.clang_bin_dir = Path(options.clang_bin_dir)

  if not options.clang_plugins_dir:
    # Backwards compatibility
    if os.getenv("CLANG_PLUGINS"):
      options.clang_plugins_dir = Path(os.getenv("CLANG_PLUGINS"))
    else:
      options.clang_plugins_dir = default_tools_gcmole_dir.resolve()
    log("Using --clang-plugins-dir={}", options.clang_plugins_dir)
  else:
    options.clang_plugins_dir = Path(options.clang_plugins_dir)

  if not options.v8_build_dir:
    config = ARCHITECTURES[options.v8_target_cpu]
    options.v8_build_dir = options.v8_root_dir / ('out/%s.release' %
                                                  config.name)
    # Fallback for build bots.
    if not options.v8_build_dir.exists() and os.getenv("CLANG_BIN"):
      options.v8_build_dir = options.v8_root_dir / 'out/build'
    log("Using --v8-build-dir={}", options.v8_build_dir)
  else:
    options.v8_build_dir = Path(options.v8_build_dir)

  if not options.out_dir:
    options.out_dir = options.v8_build_dir / 'gen/tools/gcmole'
    if options.v8_build_dir.exists():
      options.out_dir.mkdir(parents=True, exist_ok=True)
  else:
    options.out_dir = Path(options.out_dir)

  for flag, path in [
      ("--v8-root-dir", options.v8_root_dir),
      ("--v8-build-dir", options.v8_build_dir),
      ("--clang-bin-dir", options.clang_bin_dir),
      ("--clang-plugins-dir", options.clang_plugins_dir),
      ("--out-dir", options.out_dir),
  ]:
    if not path.is_dir():
      parser.error(f"{flag}='{path}' does not exist!")


def verify_clang_plugin(parser, options):
  libgcmole_path = options.clang_plugins_dir / "libgcmole.so"
  if not libgcmole_path.is_file():
    parser.error("'{}' does not exist. Please build gcmole first.".format(
        libgcmole_path))
  clang_path = options.clang_bin_dir / "clang++"
  if not clang_path.is_file():
    parser.error(
        "'{}' does not exist. Please build gcmole first.".format(clang_path))


def prepare_gcmole_files(options):
  cmd = [
      "autoninja", "-C", options.v8_build_dir, "v8_gcmole_files",
      "v8_dump_build_config"
  ]
  cmd = list(map(str, cmd))
  log("Preparing files: {}", " ".join(cmd))
  try:
    subprocess.check_call(cmd)
  except:
    # Ignore ninja task errors on the bots
    if options.is_bot:
      log("Ninja command failed, ignoring errors.")
    else:
      raise


def verify_build_config(parser, options):
  if options.is_bot:
    #TODO(cbruni): Fix, currently not supported on the bots
    return
  config_file = options.v8_build_dir / 'v8_build_config.json'
  with open(config_file) as f:
    config = json.load(f)
  found_cpu = None
  for key in ('v8_target_cpu', 'target_cpu', 'current_cpu'):
    found_cpu = config.get('v8_target_cpu')
    if found_cpu == options.v8_target_cpu:
      return
  parser.error("Build dir '{}' config doesn't match request cpu. {}: {}".format(
      options.v8_build_dir, options.v8_target_cpu, found_cpu))


def override_env_options(options):
  """Set shard options if passed as gtest environment vars on bots."""
  options.shard_count = int(
    os.environ.get('GTEST_TOTAL_SHARDS', options.shard_count))
  options.shard_index = int(
    os.environ.get('GTEST_SHARD_INDEX', options.shard_index))


if __name__ == "__main__":
  main(sys.argv)
