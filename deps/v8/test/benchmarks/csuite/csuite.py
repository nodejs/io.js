#!/usr/bin/python3
# Copyright 2018 the V8 project authors. All rights reserved.
'''
C S u i t e                                         because who can remember?
-----------------------------------------------------------------------------
python csuite.py [options] <benchmark> <mode> <d8 path>

Arguments
  benchmark: one of octane, sunspider or kraken.
  mode: baseline or compare.
  d8 path: a valid path to the d8 executable you want to use.

CSuite is a wrapper around benchmark.py and compare-baseline.py, old
friends in the d8 benchmarking world. Unlike those tools, it can be
run in any directory. It's also opinionated about which benchmarks it
will run, currently SunSpider, Octane and Kraken. Furthermore, it
runs the versions we pull into ./test/benchmarks/data.

Examples:

Say you want to see how much optimization buys you:
  ./csuite.py kraken baseline ~/src/v8/out/d8 -x="--noturbofan"
  ./csuite.py kraken compare ~/src/v8/out/d8

Suppose you are comparing two binaries, quick n' dirty style:
  ./csuite.py -r 3 octane baseline ~/src/v8/out-master/d8
  ./csuite.py -r 3 octane compare ~/src/v8/out-mine/d8

You can run from any place:
  ../../somewhere-strange/csuite.py sunspider baseline ./d8
  ../../somewhere-strange/csuite.py sunspider compare ./d8-better
'''

# for py2/py3 compatibility
from __future__ import print_function

import os
from optparse import OptionParser
import subprocess
import sys

if __name__ == '__main__':
  parser = OptionParser(usage=__doc__)
  parser.add_option("-r", "--runs", dest="runs",
      help="Override the default number of runs for the benchmark.")
  parser.add_option("-x", "--extra-arguments", dest="extra_args",
      help="Pass these extra arguments to d8.")
  parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
      help="See more output about what magic csuite is doing.")
  parser.add_option(
      "-m",
      "--memory",
      action="store_true",
      dest="memory",
      help="Measure system memory usage.")
  (opts, args) = parser.parse_args()

  if len(args) < 3:
    print('not enough arguments')
    sys.exit(1)

  suite = args[0]
  mode = args[1]

  if suite not in ['octane', 'sunspider', 'kraken']:
    print('Suite must be octane, sunspider or kraken. Aborting.')
    sys.exit(1)

  if mode != 'baseline' and mode != 'compare':
    print('mode must be baseline or compare. Aborting.')
    sys.exit(1)

  # Set up paths.
  d8_path = os.path.abspath(args[2])
  if not os.path.exists(d8_path):
    print(d8_path + " is not valid.")
    sys.exit(1)

  csuite_path = os.path.dirname(os.path.abspath(__file__))
  if not os.path.exists(csuite_path):
    print("The csuite directory is invalid.")
    sys.exit(1)

  benchmark_py_path = os.path.join(csuite_path, "benchmark.py")
  if not os.path.exists(benchmark_py_path):
    print("Unable to find benchmark.py in " + csuite_path \
        + ". Aborting.")
    sys.exit(1)

  compare_baseline_py_path = os.path.join(csuite_path,
      "compare-baseline.py")

  if not os.path.exists(compare_baseline_py_path):
    print("Unable to find compare-baseline.py in " + csuite_path \
        + ". Aborting.")
    sys.exit(1)

  benchmark_path = os.path.abspath(os.path.join(csuite_path, "../data"))
  if not os.path.exists(benchmark_path):
    print("I can't find the benchmark data directory. Aborting.")
    sys.exit(1)

  # Gather the remaining arguments into a string of extra args for d8.
  extra_args = ""
  if opts.extra_args:
    extra_args = opts.extra_args
  if opts.memory:
    extra_args += " --dump-system-memory-stats"

  if suite == "octane":
    runs = 10
    suite_path = os.path.join(benchmark_path, "octane")
    cmd = "run.js"
  elif suite == "kraken":
    runs = 80
    suite_path = os.path.join(benchmark_path, "kraken")
    cmd = os.path.join(csuite_path, "run-kraken.js")
  else:
    runs = 100
    suite_path = os.path.join(benchmark_path, "sunspider")
    cmd = os.path.join(csuite_path, "sunspider-standalone-driver.js")

  if opts.runs:
    if (float(opts.runs) / runs) < 0.6:
      print("Normally, %s requires %d runs to get stable results." \
          % (suite, runs))
    runs = int(opts.runs)

  if opts.verbose:
    print("Running and averaging %s %d times." % (suite, runs))

  # Ensure output directory is setup
  output_path_base = os.path.abspath(os.getcwd())
  output_path = os.path.join(output_path_base, "_results")
  output_file = os.path.join(output_path, "master_" + suite)
  if not os.path.exists(output_path):
    if opts.verbose:
      print("Creating directory %s." % output_path)
    os.mkdir(output_path)

  if opts.verbose:
    print("Working directory for runs is %s." % suite_path)

  inner_command = " -c \"%s --expose-gc %s %s \"" \
      % (d8_path, extra_args, cmd)
  if opts.verbose:
    print("calling d8 like so: %s." % inner_command)

  cmdline_base = "python %s %s -fv -r %d -d %s" \
      % (benchmark_py_path, inner_command, runs, output_path_base)

  if mode == "baseline":
    cmdline = "%s > %s" % (cmdline_base, output_file)
  else:
    output_file_compare = output_file + "_compare"
    cmdline = "%s > %s" % (cmdline_base, output_file_compare)

  if opts.verbose:
    print("Spawning subprocess: %s." % cmdline)
  return_code = subprocess.call(cmdline, shell=True, cwd=suite_path)
  if return_code < 0:
    print("Error return code: %d." % return_code)

  if mode == "baseline":
    print("Wrote %s." % output_file)
    print("Run %s again with compare mode to see results." % suite)
  else:
    print("Wrote %s." % output_file_compare)
    cmdline = "python %s  %s -f %s" % (compare_baseline_py_path, output_file, output_file_compare)
    if opts.verbose:
      print("Spawning subprocess: %s." % cmdline)
    return_code = subprocess.call(cmdline, shell=True, cwd=suite_path)
    if return_code < 0:
      print("Error return code: %d." % return_code)
