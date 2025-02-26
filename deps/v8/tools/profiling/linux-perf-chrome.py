#!/usr/bin/env python3
# Copyright 2022 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from datetime import datetime
from datetime import timedelta
import multiprocessing
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

renderer_cmd_file = (Path(__file__).parent /
                     'linux-perf-chrome-renderer-cmd.sh').resolve()
assert renderer_cmd_file.is_file()
renderer_cmd_prefix = f"{renderer_cmd_file} --perf-data-prefix=chrome_renderer"

# ==============================================================================

usage = """Usage: %prog $CHROME_BIN [OPTION]... -- [CHROME_OPTION]... [URL]

This script runs linux-perf on all render process with custom V8 logging to get
support to resolve JS function names.

The perf data is written to OUT_DIR separate by renderer process.

See https://v8.dev/docs/linux-perf for more detailed instructions.
"""
parser = optparse.OptionParser(usage=usage)

parser.add_option(
    "--perf-data-dir",
    default=None,
    metavar="OUT_DIR",
    help=("Output directory for linux perf profile files."
          "Defaults to './perf_profile_chrome_%Y-%m-%d_%H%M%S'"))

linux_perf_group = optparse.OptionGroup(
    parser,
    "Linux perf options",
)
linux_perf_group.add_option(
    "--freq",
    metavar="FREQUENCY",
    help=("Sampling frequency in herz. Either use int or 'max'. "
          "Sets `perf record --freq=FREQUENCY'. Defaults to 10000"))
linux_perf_group.add_option(
    "--count",
    type=int,
    metavar="COUNT",
    help=("Sample trigger after count events. "
          "Sets `perf record --count=COUNT'. Not used by default."))
linux_perf_group.add_option(
    "--call-graph",
    metavar="TYPE",
    type=str,
    help="Sets `perf record --call-graph=TYPE`. Defaults to `fp`.")
linux_perf_group.add_option(
    "--clockid",
    metavar="TYPE",
    type=str,
    help="Sets `perf record --clockid=TYPE`. Defaults to 'mono'")
parser.add_option_group(linux_perf_group)

parser.add_option(
    "--profile-browser-process",
    action="store_true",
    default=False,
    help="Also start linux-perf for the browser process. "
    "By default only renderer processes are sampled. "
    "Outputs 'browser_*.perf.data' in the CDW")
parser.add_option("--timeout", type=float, help="Stop chrome after N seconds")

chrome_options = optparse.OptionGroup(
    parser, "Chrome-forwarded Options",
    "These convenience for a better script experience that are forward directly"
    "to chrome. Any other chrome option can be passed after the '--' arguments"
    "separator.")
chrome_options.add_option(
    "--user-data-dir",
    dest="user_data_dir",
    default=None,
    help="Chrome's profile location. "
    "By default a temp directory is used.")
chrome_options.add_option(
    "--js-flags",
    dest="js_flags",
    help="Comma-separated list of flags passed to V8.")
chrome_options.add_option(
    "--renderer-cmd-prefix",
    default=None,
    help=f"Set command prefix, used for each new chrome renderer process."
    "Default: {renderer_cmd_prefix}")
FEATURES_DOC = "See chrome's base/feature_list.h source file for more dertails"
chrome_options.add_option(
    "--enable-features",
    help="Comma-separated list of enabled chrome features. " + FEATURES_DOC)
chrome_options.add_option(
    "--disable-features",
    help="Command-separated list of disabled chrome features. " + FEATURES_DOC)
parser.add_option_group(chrome_options)


# ==============================================================================
def log(*args):
  print("")
  print("=" * 80)
  print(*args)
  print("=" * 80)

# ==============================================================================

(options, args) = parser.parse_args()

if len(args) == 0:
  parser.error("No chrome binary provided")

chrome_bin = Path(args.pop(0)).absolute()
if not chrome_bin.exists():
  parser.error(f"Chrome '{chrome_bin}' does not exist")

CUSTOM_PERF_OPTIONS = ("clockid", "call_graph", "freq", "count")

if options.freq and options.count:
  parser.error("--freq and --count are mutually exclusive. "
               "See `perf record --help' for more details.")

if options.renderer_cmd_prefix:
  for perf_option in CUSTOM_PERF_OPTIONS + ("perf_data_dir",):
    if getattr(options, perf_option):
      flag_name = perf_option.replace("_", "-")
      parser.error(f"Cannot specify --{flag_name} "
                   "if a custom --renderer-cmd-prefix is provided")

if options.perf_data_dir:
  perf_data_dir = Path(options.perf_data_dir).absolute()
else:
  date = datetime.now().strftime("%Y-%m-%d_%H%M%S")
  perf_data_dir = Path.cwd() / f"perf_profile_chrome_{date}"

perf_data_dir.mkdir(parents=True, exist_ok=True)
if not perf_data_dir.is_dir():
  parser.error(f"--perf-data-dir={options.perf_data_dir} "
               "is not an directory or does not exist.")

if not options.renderer_cmd_prefix:
  options.perf_data_dir = perf_data_dir
  renderer_cmd_prefix = [str(renderer_cmd_file)]
  for perf_option in CUSTOM_PERF_OPTIONS:
    flag_value = getattr(options, perf_option)
    if flag_value:
      flag_name = perf_option.replace("_", "-")
      renderer_cmd_prefix.append(f"--{flag_name}={flag_value}")
  renderer_cmd_prefix.append(f"--perf-data-dir={options.perf_data_dir}")

  options.renderer_cmd_prefix = shlex.join(renderer_cmd_prefix)

if options.timeout and options.timeout < 2:
  parser.error("--timeout should be more than 2 seconds")

# ==============================================================================
old_cwd = Path.cwd()
os.chdir(perf_data_dir)

# ==============================================================================
JS_FLAGS_PERF = ("--perf-prof", "--interpreted-frames-native-stack")


def wait_for_process_timeout(process):
  delta = timedelta(seconds=options.timeout)
  start_time = datetime.now()
  while True:
    if (datetime.now() - start_time) >= delta:
      return False
    processHasStopped = process.poll() is not None
    if processHasStopped:
      return True
    time.sleep(0.5)
  return False


with tempfile.TemporaryDirectory(prefix="chrome-") as tmp_dir_path:
  tempdir = Path(tmp_dir_path)
  cmd = [
      str(chrome_bin),
  ]
  if options.user_data_dir is None:
    options.user_data_dir = tempdir
  cmd.append(f"--user-data-dir={options.user_data_dir}")
  cmd += [
      "--no-sandbox",
      "--enable-benchmarking",
      "--no-first-run",
      "--no-default-browser-check",
      f"--renderer-cmd-prefix={options.renderer_cmd_prefix}",
  ]

  # Do the magic js-flag concatenation to properly forward them to the
  # renderer command
  js_flags = set(JS_FLAGS_PERF)
  if options.js_flags:
    js_flags.update(shlex.split(options.js_flags))
  cmd += [f"--js-flags={','.join(list(js_flags))}"]

  if options.enable_features:
    cmd += [f"--enable-features={options.enable_features}"]
  if options.disable_features:
    cmd += [f"--disable-features={options.disable_features}"]
  cmd += args
  log("CHROME CMD: ", shlex.join(cmd))

  if options.profile_browser_process:
    perf_data_file = f"{tempdir.name}_browser.perf.data"
    perf_cmd = [
        "perf",
        "record",
        f"--call-graph={options.call_graph or 'fp'}",
        f"--freq={options.freq or 10000}",
        f"--clockid={options.clockid or 'mono'}",
        f"--output={perf_data_file}",
        "--",
    ]
    cmd = perf_cmd + cmd
    log("LINUX PERF CMD: ", shlex.join(cmd))

  if options.timeout is None:
    try:
      subprocess.check_call(cmd, start_new_session=True)
      log("Waiting for linux-perf to flush all perf data")
      time.sleep(3)
    except:
      log("ERROR running perf record")
  else:
    process = subprocess.Popen(cmd)
    if not wait_for_process_timeout(process):
      log(f"QUITING chrome child processes after {options.timeout}s timeout")
    current_process = psutil.Process()
    children = current_process.children(recursive=True)
    for child in children:
      if "chrome" in child.name() or "content_shell" in child.name():
        print(f"  quitting PID={child.pid}")
        child.send_signal(signal.SIGQUIT)
    log("Waiting for linux-perf to flush all perf data")
    time.sleep(3)
    return_status = process.poll()
    if return_status is None:
      log("Force quitting linux-perf")
      process.send_signal(signal.SIGQUIT)
      process.wait()
    elif return_status != 0:
      log("ERROR running perf record")

# ==============================================================================
log("PARALLEL POST PROCESSING: Injecting JS symbols")


def inject_v8_symbols(perf_dat_file):
  output_file = perf_dat_file.with_suffix(".data.jitted")
  cmd = [
      "perf", "inject", "--jit", f"--input={perf_dat_file.absolute()}",
      f"--output={output_file.absolute()}"
  ]
  try:
    subprocess.check_call(cmd)
    print(f"Processed: {output_file.name}")
  except:
    print(shlex.join(cmd))
    return None
  return output_file


results = []
with multiprocessing.Pool() as pool:
  results = list(
      pool.imap_unordered(inject_v8_symbols, perf_data_dir.glob("*perf.data")))

results = list(filter(lambda x: x is not None, results))
if len(results) == 0:
  print("No perf files were successfully processed"
        f" Check for errors or partial results in '{perf_data_dir}'")
  exit(1)

log(f"RESULTS in '{perf_data_dir}'")
results.sort(key=lambda x: x.stat().st_size)
BYTES_TO_MIB = 1 / 1024 / 1024
for output_file in reversed(results):
  print(f"{output_file.name:67}"
        f"{(output_file.stat().st_size*BYTES_TO_MIB):10.2f}MiB")

# ==============================================================================
rel_path_strings = [str(path.relative_to(old_cwd)) for path in results]
abs_path_strings = [str(path.absolute()) for path in results]
largest_result = abs_path_strings[-1]

if not shutil.which('gcertstatus'):
  log("ANALYSIS")
  print(f"perf report --input='{largest_result}'")
  print(f"pprof {rel_path_strings}")
  exit(0)

log("PPROF")
has_gcert = False
try:
  print("# Checking gcert status for googlers")
  subprocess.check_call("gcertstatus >&/dev/null || gcert", shell=True)
  has_gcert = True

  cmd = [
      "pprof", "-symbolize=local", "-flame",
      f"-add_comment={shlex.join(sys.argv)}"
  ]
  print("# Processing and uploading largest pprof result")
  url = subprocess.check_output(cmd + [largest_result]).decode('utf-8').strip()
  print("# PPROF RESULT")
  print(url)

  print("# Processing and uploading combined pprof result")
  url = subprocess.check_output(cmd + abs_path_strings).decode('utf-8').strip()
  print("# PPROF RESULT")
  print(url)
except subprocess.CalledProcessError as e:
  if has_gcert:
    raise Exception("Could not generate pprof results") from e
  print("# Please run `gcert` for generating pprof results")
  print(f"pprof -symbolize=local -flame {' '.join(rel_path_strings)}")
except KeyboardInterrupt:
  exit(1)
