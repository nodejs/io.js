# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import OrderedDict, namedtuple
from functools import reduce
from pathlib import Path

import json
import logging
import multiprocessing
import optparse
import os
import shlex
import sys
import traceback

from testrunner.build_config import BuildConfig
from testrunner.local import testsuite
from testrunner.local import utils
from testrunner.local.variants import REQUIRED_BUILD_VARIABLES
from testrunner.local.context import os_context
from testrunner.test_config import TestConfig
from testrunner.testproc import util
from testrunner.testproc.indicators import PROGRESS_INDICATORS
from testrunner.testproc.sigproc import SignalProc
from testrunner.utils.augmented_options import AugmentedOptions


DEFAULT_OUT_GN = Path('out.gn')

# Map of test name synonyms to lists of test suites. Should be ordered by
# expected runtimes (suites with slow test cases first). These groups are
# invoked in separate steps on the bots.
# The mapping from names used here to GN targets (which must stay in sync)
# is defined in infra/mb/gn_isolate_map.pyl.
TEST_MAP = {
  # This needs to stay in sync with group("v8_bot_default") in test/BUILD.gn.
  "bot_default": [
    "debugger",
    "mjsunit",
    "cctest",
    "wasm-spec-tests",
    "inspector",
    "webkit",
    "mkgrokdump",
    "wasm-js",
    "fuzzer",
    "message",
    "intl",
    "unittests",
    "wasm-api-tests",
  ],
  # This needs to stay in sync with group("v8_default") in test/BUILD.gn.
  "default": [
    "debugger",
    "mjsunit",
    "cctest",
    "wasm-spec-tests",
    "inspector",
    "mkgrokdump",
    "wasm-js",
    "fuzzer",
    "message",
    "intl",
    "unittests",
    "wasm-api-tests",
  ],
  # This needs to stay in sync with group("v8_d8_default") in test/BUILD.gn.
  "d8_default": [
    "debugger",
    "mjsunit",
    "webkit",
    "message",
    "intl",
  ],
  # This needs to stay in sync with "v8_optimize_for_size" in test/BUILD.gn.
  "optimize_for_size": [
    "debugger",
    "mjsunit",
    "cctest",
    "inspector",
    "webkit",
    "intl",
  ],
  "unittests": [
    "unittests",
  ],
}

ModeConfig = namedtuple(
    'ModeConfig', 'label flags timeout_scalefactor status_mode')

RELEASE_FLAGS = ["--nohard-abort"]

RELEASE_MODE = ModeConfig(
    label='release',
    flags=RELEASE_FLAGS,
    timeout_scalefactor=1,
    status_mode="release",
)

# Normal trybot release configuration. There, dchecks are always on which
# implies debug is set. Hence, the status file needs to assume debug-like
# behavior/timeouts.
TRY_RELEASE_MODE = ModeConfig(
    label='release+dchecks',
    flags=RELEASE_FLAGS,
    timeout_scalefactor=4,
    status_mode="debug",
)

# Set up logging. No need to log a date in timestamps as we can get that from
# test run start times.
logging.basicConfig(
    format='%(asctime)s %(message)s',
    datefmt='%H:%M:%S',
    level=logging.WARNING,
)

class TestRunnerError(Exception):
  pass

class BaseTestRunner(object):
  def __init__(self, basedir=None):
    self.v8_root = Path(__file__).absolute().parent.parent.parent
    self.basedir = Path(basedir or self.v8_root)
    self.outdir = None
    self.build_config = None
    self.mode_options = None
    self.target_os = None
    self.infra_staging = False
    self.options = None

  @property
  def framework_name(self):
    """String name of the base-runner subclass, used in test results."""
    raise NotImplementedError() # pragma: no cover

  def execute(self, sys_args=None):
    if sys_args is None:  # pragma: no cover
      sys_args = sys.argv[1:]
    parser = self._create_parser()
    self.options, args = self._parse_args(parser, sys_args)
    self.infra_staging = self.options.infra_staging
    if self.options.swarming:
      logging.getLogger().setLevel(logging.INFO)

      # Swarming doesn't print how isolated commands are called. Lets make
      # this less cryptic by printing it ourselves.
      print(' '.join(sys.argv))

      # TODO(machenbach): Print used Python version until we have switched to
      # Python3 everywhere.
      print('Running with:')
      print(sys.version)

      # Kill stray processes from previous tasks on swarming.
      util.kill_processes_linux()

    try:
      self._load_build_config()
      try:
        self._process_default_options()
        self._process_options()
      except TestRunnerError:
        parser.print_help()
        raise

      args = self._parse_test_args(args)

      with os_context(self.target_os, self.options) as ctx:
        self._setup_env()
        names = self._args_to_suite_names(args)
        tests = self._load_testsuite_generators(ctx, names)
        print(">>> Running tests for %s.%s" % (self.build_config.arch,
                                               self.mode_options.label))
        return self._do_execute(tests, args, ctx)
    except TestRunnerError:
      traceback.print_exc()
      return utils.EXIT_CODE_INTERNAL_ERROR
    except KeyboardInterrupt:
      return utils.EXIT_CODE_INTERRUPTED
    except Exception:
      traceback.print_exc()
      return utils.EXIT_CODE_INTERNAL_ERROR


  def _create_parser(self):
    parser = optparse.OptionParser()
    parser.usage = '%prog [options] [tests]'
    parser.description = """TESTS: %s""" % (TEST_MAP["default"])
    self._add_parser_default_options(parser)
    self._add_parser_options(parser)
    return parser

  def _add_parser_default_options(self, parser):
    parser.add_option("--gn", help="Scan out.gn for the last built"
                      " configuration",
                      default=False, action="store_true")
    parser.add_option("--outdir", help="Base directory with compile output",
                      default="out")
    parser.add_option("--arch",
                      help="The architecture to run tests for")
    parser.add_option("--shell-dir", help="DEPRECATED! Executables from build "
                      "directory will be used")
    parser.add_option("--test-root", help="Root directory of the test suites",
                      default=self.basedir / 'test')
    parser.add_option("--total-timeout-sec", default=0, type="int",
                      help="How long should fuzzer run")
    parser.add_option("--swarming", default=False, action="store_true",
                      help="Indicates running test driver on swarming.")
    parser.add_option('--infra-staging', help='Use new test runner features',
                      dest='infra_staging', default=None,
                      action='store_true')
    parser.add_option('--no-infra-staging',
                      help='Opt out of new test runner features',
                      dest='infra_staging', default=None,
                      action='store_false')

    parser.add_option("-j", help="The number of parallel tasks to run",
                      default=0, type=int)
    parser.add_option("-d", "--device",
                      help="The device ID to run Android tests on. If not "
                           "given it will be autodetected.")

    # Shard
    parser.add_option("--shard-count", default=1, type=int,
                      help="Split tests into this number of shards")
    parser.add_option("--shard-run", default=1, type=int,
                      help="Run this shard from the split up tests.")

    # Progress
    parser.add_option("-p", "--progress",
                      choices=list(PROGRESS_INDICATORS.keys()), default="mono",
                      help="The style of progress indicator (verbose, dots, "
                           "color, mono)")
    parser.add_option("--json-test-results",
                      help="Path to a file for storing json results.")
    parser.add_option("--log-system-memory",
                      help="Path to a file for storing system memory stats.")
    parser.add_option("--log-test-schedule",
                      help="Path to a file for streaming the test schedule to.")
    parser.add_option('--slow-tests-cutoff', type="int", default=100,
                      help='Collect N slowest tests')
    parser.add_option("--exit-after-n-failures", type="int", default=100,
                      help="Exit after the first N failures instead of "
                           "running all tests. Pass 0 to disable this feature.")
    parser.add_option("--ci-test-completion",
                      help="Path to a file for logging test completion in the "
                           "context of CI progress indicator. Ignored if "
                           "progress indicator is other than 'ci'.")

    # Rerun
    parser.add_option("--rerun-failures-count", default=0, type=int,
                      help="Number of times to rerun each failing test case. "
                           "Very slow tests will be rerun only once.")
    parser.add_option("--rerun-failures-max", default=100, type=int,
                      help="Maximum number of failing test cases to rerun")

    # Test config
    parser.add_option("--command-prefix", default="",
                      help="Prepended to each shell command used to run a test")
    parser.add_option('--dont-skip-slow-simulator-tests',
                      help='Don\'t skip more slow tests when using a'
                      ' simulator.', default=False, action='store_true',
                      dest='dont_skip_simulator_slow_tests')
    parser.add_option("--extra-flags", action="append", default=[],
                      help="Additional flags to pass to each test command")
    parser.add_option("--isolates", action="store_true", default=False,
                      help="Whether to test isolates")
    parser.add_option("--no-harness", "--noharness",
                      default=False, action="store_true",
                      help="Run without test harness of a given suite")
    parser.add_option("--random-seed", default=0, type=int,
                      help="Default seed for initializing random generator")
    parser.add_option("--run-skipped", help="Also run skipped tests.",
                      default=False, action="store_true")
    parser.add_option("-t", "--timeout", default=60, type=int,
                      help="Timeout for single test in seconds")
    parser.add_option("-v", "--verbose", default=False, action="store_true",
                      help="Verbose output")
    parser.add_option('--regenerate-expected-files', default=False, action='store_true',
                      help='Regenerate expected files')

    # TODO(machenbach): Temporary options for rolling out new test runner
    # features.
    parser.add_option("--mastername", default='',
                      help="Mastername property from infrastructure. Not "
                           "setting this option indicates manual usage.")
    parser.add_option("--buildername", default='',
                      help="Buildername property from infrastructure. Not "
                           "setting this option indicates manual usage.")

  def _add_parser_options(self, parser):
    pass # pragma: no cover

  def _parse_args(self, parser, sys_args):
    options, args = parser.parse_args(sys_args)

    options.test_root = Path(options.test_root)
    options.outdir = Path(options.outdir)

    if options.arch and ',' in options.arch:  # pragma: no cover
      print('Multiple architectures are deprecated')
      raise TestRunnerError()

    # We write a test schedule and the system memory stats by default
    # alongside json test results on bots.
    if options.json_test_results:
      result_dir = Path(options.json_test_results).parent
      if not options.log_test_schedule:
        options.log_test_schedule = result_dir / 'test_schedule.log'
      if not options.log_system_memory:
        options.log_system_memory = result_dir / 'memory_stats.log'

    return AugmentedOptions.augment(options), args

  def _load_build_config(self):
    for outdir in self._possible_outdirs():
      try:
        self.build_config = self._do_load_build_config(outdir)

        # In auto-detect mode the outdir is always where we found the build config.
        # This ensures that we'll also take the build products from there.
        self.outdir = outdir
        break
      except TestRunnerError:
        pass

    if not self.build_config:  # pragma: no cover
      print('Failed to load build config')
      raise TestRunnerError

    print('Build found: %s' % self.outdir)
    if str(self.build_config):
      print('>>> Autodetected:')
      print(self.build_config)

    # Represents the OS where tests are run on. Same as host OS except for
    # Android and iOS, which are determined by build output.
    if self.build_config.is_android:
      self.target_os = 'android'
    elif self.build_config.is_ios:
      self.target_os = 'ios'
    else:
      self.target_os = utils.GuessOS()

    # Verify integrity between build variables and variant configs.
    self.build_config.ensure_vars(REQUIRED_BUILD_VARIABLES)

  def _do_load_build_config(self, outdir):
    build_config_path = outdir / "v8_build_config.json"
    if not build_config_path.exists():
      if self.options.verbose:
        print("Didn't find build config: %s" % build_config_path)
      raise TestRunnerError()

    with open(build_config_path) as f:
      try:
        build_config_json = json.load(f)
      except Exception:  # pragma: no cover
        print("%s exists but contains invalid json. Is your build up-to-date?"
              % build_config_path)
        raise TestRunnerError()

    return BuildConfig(build_config_json)

  # Returns possible build paths in order:
  # gn
  # outdir
  # outdir on bots
  def _possible_outdirs(self):
    def outdirs():
      if self.options.gn:
        yield self._get_gn_outdir()
        return

      yield self.options.outdir

      if self.options.outdir.name != 'build':
        yield self.options.outdir / 'build'

    for outdir in outdirs():
      yield self.basedir / outdir

  def _get_gn_outdir(self):
    gn_out_dir = self.basedir / DEFAULT_OUT_GN
    latest_timestamp = -1
    latest_config = None
    for gn_config_dir in gn_out_dir.iterdir():
      if not gn_config_dir.is_dir():
        continue
      if gn_config_dir.stat().st_mtime > latest_timestamp:
        latest_timestamp = gn_config_dir.stat().st_mtime
        latest_config = gn_config_dir.name
    if latest_config:
      print(">>> Latest GN build found: %s" % latest_config)
      return DEFAULT_OUT_GN / latest_config

  def _custom_debug_mode(self):
    custom_debug_flags = ["--nohard-abort"]
    if self.build_config.verify_heap:
      custom_debug_flags += ["--verify-heap"]
    if self.build_config.slow_dchecks:
      custom_debug_flags += ["--enable-slow-asserts"]
    return ModeConfig(
        label='debug',
        flags=custom_debug_flags,
        timeout_scalefactor=4,
        status_mode="debug",
    )

  def _process_default_options(self):
    if self.build_config.debugging_features:
      self.mode_options = self._custom_debug_mode()
    elif self.build_config.dcheck_always_on:
      self.mode_options = TRY_RELEASE_MODE
    else:
      self.mode_options = RELEASE_MODE

    if self.options.arch and self.options.arch != self.build_config.arch:
      print('--arch value (%s) inconsistent with build config (%s).' % (
        self.options.arch, self.build_config.arch))
      raise TestRunnerError()

    if self.options.shell_dir:  # pragma: no cover
      print('Warning: --shell-dir is deprecated. Searching for executables in '
            'build directory (%s) instead.' % self.outdir)

    if self.options.j == 0:
      if self.build_config.is_android:
        # Adb isn't happy about multi-processed file pushing.
        self.options.j = 1
      else:
        self.options.j = multiprocessing.cpu_count()

    self.options.command_prefix = shlex.split(self.options.command_prefix)
    self.options.extra_flags = sum(list(map(shlex.split, self.options.extra_flags)), [])
    self.options.extra_d8_flags = []

  def _process_options(self):
    pass # pragma: no cover

  def _setup_env(self):
    # Use the v8 root as cwd as some test cases use "load" with relative paths.
    os.chdir(self.basedir)

    # Many tests assume an English interface.
    os.environ['LANG'] = 'en_US.UTF-8'

    symbolizer_option = self._get_external_symbolizer_option()

    if self.build_config.asan:
      asan_options = [
          symbolizer_option,
          'allow_user_segv_handler=1',
          'allocator_may_return_null=1',
      ]
      if self.build_config.component_build:
        # Some abseil symbols are observed as defined more than once in
        # component builds.
        asan_options += ['detect_odr_violation=0']
      if not utils.GuessOS() in ['macos', 'windows']:
        # LSAN is not available on mac and windows.
        asan_options.append('detect_leaks=1')
      else:
        asan_options.append('detect_leaks=0')
      if utils.GuessOS() == 'windows':
        # https://crbug.com/967663
        asan_options.append('detect_stack_use_after_return=0')
      os.environ['ASAN_OPTIONS'] = ":".join(asan_options)

    if self.build_config.cfi:
      os.environ['UBSAN_OPTIONS'] = ":".join([
        'print_stacktrace=1',
        'print_summary=1',
        'symbolize=1',
        symbolizer_option,
      ])

    if self.build_config.ubsan:
      os.environ['UBSAN_OPTIONS'] = ":".join([
        'print_stacktrace=1',
        symbolizer_option,
      ])

    if self.build_config.msan:
      os.environ['MSAN_OPTIONS'] = symbolizer_option

    if self.build_config.tsan:
      suppressions_file = (
          self.basedir / 'tools' / 'sanitizers' / 'tsan_suppressions.txt')
      os.environ['TSAN_OPTIONS'] = " ".join([
        symbolizer_option,
        'suppressions=%s' % suppressions_file,
        'exit_code=0',
        'report_thread_leaks=0',
        'history_size=7',
        'report_destroy_locked=0',
      ])

  def _get_external_symbolizer_option(self):
    external_symbolizer_path = (
        self.basedir / 'third_party' / 'llvm-build' / 'Release+Asserts' /
        'bin' / 'llvm-symbolizer')

    if utils.IsWindows():
      external_symbolizer_path = external_symbolizer_path.with_suffix('.exe')

      # Quote, because sanitizers might confuse colon as option separator.
      external_symbolizer_path = f'"{external_symbolizer_path}"'

    return f'external_symbolizer_path={external_symbolizer_path}'

  def _parse_test_args(self, args):
    if not args:
      args = self._get_default_suite_names()

    # Expand arguments with grouped tests. The args should reflect the list
    # of suites as otherwise filters would break.
    def expand_test_group(name):
      return TEST_MAP.get(name, [name])

    return reduce(list.__add__, list(map(expand_test_group, args)), [])

  def _args_to_suite_names(self, args):
    # Use default tests if no test configuration was provided at the cmd line.
    all_names = set(utils.GetSuitePaths(self.options.test_root))
    args_names = OrderedDict([(arg.split('/')[0], None) for arg in args]) # set
    return [name for name in args_names if name in all_names]

  def _get_default_suite_names(self):
    return [] # pragma: no cover

  def _load_testsuite_generators(self, ctx, names):
    test_config = self._create_test_config()
    variables = self._get_statusfile_variables()

    # Head generator with no elements
    test_chain = testsuite.TestGenerator(0, [], [], [])
    for name in names:
      if self.options.verbose:
        print('>>> Loading test suite: %s' % name)
      suite = testsuite.TestSuite.Load(
          ctx, self.options.test_root / name, test_config)

      if self._is_testsuite_supported(suite):
        tests = suite.load_tests_from_disk(variables)
        test_chain.merge(tests)

    return test_chain

  def _is_testsuite_supported(self, suite):
    """A predicate that can be overridden to filter out unsupported TestSuite
    instances (see NumFuzzer for usage)."""
    return True

  @property
  def _no_simd_hardware(self):
    # TODO(liviurau): Add some tests.
    # We try to find all the reasons why we have no_simd.
    if any(i in self.options.extra_flags for i in [
        '--noenable-sse3', '--no-enable-sse3', '--noenable-ssse3',
        '--no-enable-ssse3', '--noenable-sse4-1', '--no-enable-sse4_1'
    ]):
      return True

    # Set no_simd_hardware on architectures without Simd enabled.
    if (self.build_config.arch == 'mips64el' and
        not self.build_config.simd_mips):
      return True

    if self.build_config.arch == 'loong64':
      return True

    # S390 hosts without VEF1 do not support Simd.
    if (self.build_config.arch == 's390x' and
        not self.build_config.simulator_run and
        not utils.IsS390SimdSupported()):
      return True

    # Ppc64 processors earlier than POWER9 do not support Simd instructions
    if (self.build_config.arch == 'ppc64' and
        not self.build_config.simulator_run and
        utils.GuessPowerProcessorVersion() < 9):
      return True

    return False

  def _get_statusfile_variables(self):
    """Returns all attributes accessible in status files.

    All build-time flags from V8's BUILD.gn file as defined by the action
    v8_dump_build_config can be accessed in status files.
    """
    variables = dict(self.build_config.items())
    variables.update({
        "byteorder": sys.byteorder,
        "deopt_fuzzer": False,
        "endurance_fuzzer": False,
        "gc_fuzzer": False,
        "gc_stress": False,
        "isolates": self.options.isolates,
        "interrupt_fuzzer": False,
        "mode": self.mode_options.status_mode,
        "no_harness": self.options.no_harness,
        "no_simd_hardware": self._no_simd_hardware,
        "novfp3": False,
        "optimize_for_size": "--optimize-for-size" in self.options.extra_flags,
        "simulator_run": variables["simulator_run"]
                         and not self.options.dont_skip_simulator_slow_tests,
        "system": self.target_os,
    })
    return variables

  def _runner_flags(self):
    """Extra default flags specific to the test runner implementation."""
    return [] # pragma: no cover

  def _create_test_config(self):
    shard_id, shard_count = self.options.shard_info
    timeout = self.build_config.timeout_scalefactor(
        self.options.timeout * self.mode_options.timeout_scalefactor)
    return TestConfig(
        command_prefix=self.options.command_prefix,
        extra_flags=self.options.extra_flags,
        extra_d8_flags=self.options.extra_d8_flags,
        framework_name=self.framework_name,
        isolates=self.options.isolates,
        log_process_stats=self.options.json_test_results,
        mode_flags=self.mode_options.flags + self._runner_flags(),
        no_harness=self.options.no_harness,
        noi18n=not self.build_config.i18n,
        random_seed=self.options.random_seed,
        run_skipped=self.options.run_skipped,
        shard_count=shard_count,
        shard_id=shard_id,
        shell_dir=self.outdir,
        target_os=self.target_os,
        timeout=timeout,
        verbose=self.options.verbose,
        regenerate_expected_files=self.options.regenerate_expected_files,
    )

  # TODO(majeski): remove options & args parameters
  def _do_execute(self, suites, args):
    raise NotImplementedError() # pragma: no coverage

  def _prepare_procs(self, procs):
    for i in range(0, len(procs) - 1):
      procs[i].connect_to(procs[i + 1])

  def _create_signal_proc(self):
    return SignalProc()
