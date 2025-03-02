#!/usr/bin/env python3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from datetime import datetime
from datetime import timedelta
import optparse
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

import psutil

# ==============================================================================

usage = """Usage: %prog [OPTION]... $D8_BIN [D8_OPTION]... [FILE]

This script runs linux-perf with custom V8 logging to get support to resolve
JS function names.

The perf data is written to OUT_DIR separate by renderer process.

See https://v8.dev/docs/linux-perf for more detailed instructions.
See $D8_BIN --help for more flags/options
"""
parser = optparse.OptionParser(usage=usage)
# Stop parsing options after D8_BIN
parser.disable_interspersed_args()

parser.add_option(
    '--perf-data-dir',
    default=None,
    metavar="OUT_DIR",
    help=("Output directory for linux perf profile files "
          "Defaults to './perf_profile_d8_%Y-%m-%d_%H%M%S'"))
parser.add_option("--timeout", type=float, help="Stop d8 after N seconds")
parser.add_option(
    "--scope-to-mark-measure",
    action="store_true",
    default=False,
    help="Scope perf recording to start at performance.mark events and stop at performance.measure events"
)
parser.add_option(
    "--skip-pprof",
    action="store_true",
    default=False,
    help="Skip pprof upload (relevant for Googlers only)")

d8_options = optparse.OptionGroup(
    parser, "d8-forwarded Options",
    "THese options are for a better script experience that are forward directly"
    "to d8. Any other d8 option can be passed after the '--' arguments"
    "separator.")
d8_options.add_option(
    "--perf-prof-annotate-wasm",
    help="Load wasm source map and provide annotate wasm code.",
    action="store_true",
    default=False)
d8_options.add_option(
    "--no-interpreted-frames-native-stack",
    help="For profiling v8 copies the interpreter entry trampoline for every "
    "interpreted function. This makes interpreted functions identifiable on the "
    "native stack at cost of a slight performance and memory overhead.",
    action="store_true",
    default=False)
parser.add_option_group(d8_options)

perf_options = optparse.OptionGroup(
    parser, "perf-forward options", """
These options are forward directly to the `perf record` command and can be
used to manually tweak how profiling works.

See `perf record --help for more` informationn
""")

perf_options.add_option(
    "--freq",
    default="10000",
    help="Sampling frequency, either 'max' or a number in herz. "
    "Might be reduced depending on the platform. "
    "Default is 10000.")
perf_options.add_option(
    "--count", default=None, help="Event period to sample. Not set by default.")
perf_options.add_option("--call-graph", default="fp", help="Defaults tp 'fp'")
perf_options.add_option("--clockid", default="mono", help="Defaults to 'mono'")
perf_options.add_option("--event", default=None, help="Not set by default.")
perf_options.add_option(
    "--raw-samples",
    default=None,
    help="Collect raw samples. Not set by default")

perf_options.add_option(
    "--no-inherit",
    action="store_true",
    default=False,
    help=" Child tasks do not inherit counters.")
parser.add_option_group(perf_options)


# ==============================================================================
def log(*args):
  print("")
  print("=" * 80)
  print(*args)
  print("=" * 80)


def main():
  cleanup = []
  try:
    # ==========================================================================
    (options, args) = parser.parse_args()
    if options.freq and options.count:
      parser.error("--freq and --count are mutually exclusive. "
                   "See `perf record --help' for more details.")

    if len(args) == 0:
      parser.error("No d8 binary provided")

    raw_perf_args = []
    d8_bin = None
    while args:
      additional_arg = args.pop(0)
      maybe_d8_bin = Path(additional_arg).absolute()
      if maybe_d8_bin.exists():
        d8_bin = maybe_d8_bin
        break
      else:
        raw_perf_args.append(additional_arg)

    if not d8_bin:
      parser.error(f"D8 '{d8_bin}' does not exist")

    if options.perf_data_dir is None:
      options.perf_data_dir = Path.cwd()
    else:
      options.perf_data_dir = Path(options.perf_data_dir).absolute()
    options.perf_data_dir.mkdir(parents=True, exist_ok=True)
    if not options.perf_data_dir.is_dir():
      parser.error(f"--perf-data-dir={options.perf_data_dir} "
                   "is not an directory or does not exist.")

    if options.timeout and options.timeout < 0:
      parser.error("--timeout should be a positive number")

    # ==========================================================================
    def make_path_absolute(maybe_path, is_d8_js_argument=False):
      if maybe_path.startswith("-"):
        return maybe_path
      path = Path(maybe_path)
      if not path.exists():
        return maybe_path
      if is_d8_js_argument:
        # Be slightly more strict with JS arguments as they might accidentally
        # point to files that exist (e.h. JetStream workloads).
        if "/" not in maybe_path:
          return maybe_path
      return str(path.absolute())

    def make_args_paths_absolute(args):
      args_absolute_paths = []
      is_d8_js_argument = False
      for arg in args:
        if arg == "--":
          is_d8_js_argument = True
        args_absolute_paths.append(make_path_absolute(arg, is_d8_js_argument))
      return args_absolute_paths

    # Preprocess args if we change CWD to get cleaner output
    if options.perf_data_dir != Path.cwd():
      args = make_args_paths_absolute(args)

    # ==========================================================================
    old_cwd = Path.cwd()
    os.chdir(options.perf_data_dir)

    # ==========================================================================

    cmd = [str(d8_bin), "--perf-prof"]

    if not options.no_interpreted_frames_native_stack:
      cmd += ["--interpreted-frames-native-stack"]
    if options.perf_prof_annotate_wasm:
      cmd += ["--perf-prof-annotate-wasm"]

    if options.scope_to_mark_measure:
      tmp_fifo_dir = tempfile.mkdtemp()
      cleanup.append(lambda: shutil.rmtree(tmp_fifo_dir))

      perf_ctl_fifo_path = os.path.join(tmp_fifo_dir, "perf_ctl.fifo")
      perf_ack_fifo_path = os.path.join(tmp_fifo_dir, "perf_ack.fifo")
      os.mkfifo(perf_ctl_fifo_path)
      os.mkfifo(perf_ack_fifo_path)

      perf_ctl_fd = os.open(perf_ctl_fifo_path, os.O_RDWR)
      cleanup.append(lambda: os.close(perf_ctl_fd))
      perf_ack_fd = os.open(perf_ack_fifo_path, os.O_RDWR)
      cleanup.append(lambda: os.close(perf_ack_fd))

      cmd += [
          f"--perf-ctl-fd={perf_ctl_fd}", f"--perf-ack-fd={perf_ack_fd}",
          "--scope-linux-perf-to-mark-measure"
      ]
      pass_fds = [perf_ctl_fd, perf_ack_fd]
    else:
      pass_fds = []

    cmd += args
    log("D8 CMD: ", shlex.join(cmd))

    datetime_str = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    perf_data_file = Path.cwd() / f"d8_{datetime_str}.perf.data"
    perf_cmd = [
        "perf", "record", f"--call-graph={options.call_graph}",
        f"--clockid={options.clockid}", f"--output={perf_data_file}"
    ]
    if options.freq:
      perf_cmd += [f"--freq={options.freq}"]
    if options.count:
      perf_cmd += [f"--count={options.count}"]
    if options.raw_samples:
      perf_cmd += [f"--raw_samples={options.raw_samples}"]
    if options.event:
      perf_cmd += [f"--event={options.event}"]
    if options.no_inherit:
      perf_cmd += [f"--no-inherit"]
    if raw_perf_args:
      perf_cmd.extend(raw_perf_args)

    if options.scope_to_mark_measure:
      perf_cmd += [f"--control=fd:{perf_ctl_fd},{perf_ack_fd}", "--delay=-1"]

    cmd = perf_cmd + ["--"] + cmd
    log("LINUX PERF CMD: ", shlex.join(cmd))

    def wait_for_process_timeout(process):
      delta = timedelta(seconds=options.timeout)
      start_time = datetime.now()
      while True:
        if (datetime.now() - start_time) >= delta:
          return False
        processHasStopped = process.poll() is not None
        if processHasStopped:
          return True
        time.sleep(0.1)
      return False

    if options.timeout is None:
      try:
        subprocess.check_call(cmd, pass_fds=pass_fds)
        log("Waiting for linux-perf to flush all perf data")
        time.sleep(1)
      except Exception as e:
        log("ERROR running perf record: ", e)
    else:
      process = subprocess.Popen(cmd, pass_fds=pass_fds)
      if not wait_for_process_timeout(process):
        log(f"QUITING d8 processes after {options.timeout}s timeout")
      current_process = psutil.Process()
      children = current_process.children(recursive=True)
      for child in children:
        if "d8" in child.name():
          print(f"  quitting PID={child.pid}")
          child.send_signal(signal.SIGQUIT)
      log("Waiting for linux-perf to flush all perf data")
      time.sleep(1)
      return_status = process.poll()
      if return_status is None:
        log("Force quitting linux-perf")
        process.send_signal(signal.SIGQUIT)
        process.wait()
      elif return_status != 0:
        log("ERROR running perf record")

    # ==========================================================================
    log("POST PROCESSING: Injecting JS symbols")

    def inject_v8_symbols(perf_dat_file):
      output_file = perf_dat_file.with_suffix(".data.jitted")
      cmd = [
          "perf", "inject", "--jit", f"--input={perf_dat_file.absolute()}",
          f"--output={output_file.absolute()}"
      ]
      try:
        subprocess.check_call(cmd)
        print(f"Processed: {output_file}")
      except:
        print(shlex.join(cmd))
        return None
      return output_file

    result = inject_v8_symbols(perf_data_file)
    if result is None:
      print(
          "No perf files were successfully processed"
          f" Check for errors or partial results in '{options.perf_data_dir}'")
      return 1
    log(f"RESULTS in '{options.perf_data_dir}'")
    BYTES_TO_MIB = 1 / 1024 / 1024
    print(f"{result.name:67}{(result.stat().st_size*BYTES_TO_MIB):10.2f}MiB")

    # ==========================================================================
    if not shutil.which('gcertstatus') or options.skip_pprof:
      log("ANALYSIS")
      print(f"perf report --input='{result}'")
      print(f"pprof '{result}'")
      return 0

    log("PPROF")
    has_gcert = False
    try:
      print("# Checking gcert status for googlers")
      subprocess.check_call("gcertstatus >&/dev/null || gcert", shell=True)
      has_gcert = True

      cmd = [
          "pprof", "-symbolize=local", "-flame",
          f"-add_comment={shlex.join(sys.argv)}",
          str(result.absolute())
      ]
      print("# Processing and uploading to pprofresult")
      url = subprocess.check_output(cmd).decode('utf-8').strip()
      print(url)
    except subprocess.CalledProcessError as e:
      if has_gcert:
        raise Exception("Could not generate pprof results") from e
      print("# Please run `gcert` for generating pprof results")
      print(f"pprof -symbolize=local -flame {result}")
    except KeyboardInterrupt:
      return 1

  finally:
    for cleanup_func in reversed(cleanup):
      cleanup_func()

  return 0


sys.exit(main())
