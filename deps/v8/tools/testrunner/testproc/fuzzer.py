# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


from . import base


# Extra flags randomly added to all fuzz tests with numfuzz. List of tuples
# (probability, flag).
EXTRA_FLAGS = [
    (0.1, '--always-turbofan'),
    (0.1, '--assert-types'),
    (0.1, '--cache=code'),
    (0.1, '--force-slow-path'),
    (0.2, '--future'),
    # TODO(v8:13524): Enable when issue is fixed
    # TODO(v8:13528): Enable when issue is fixed
    # (0.1, '--harmony-struct'),
    (0.1, '--jit-fuzzing'),
    (0.1, '--liftoff'),
    (0.1, '--maglev'),
    (0.1, '--maglev-future'),
    (0.25, '--minor-ms'),
    (0.2, '--no-analyze-environment-liveness'),
    # TODO(machenbach): Enable when it doesn't collide with crashing on missing
    # simd features.
    #(0.1, '--no-enable-sse3'),
    #(0.1, '--no-enable-ssse3'),
    #(0.1, '--no-enable-sse4_1'),
    (0.1, '--no-enable-sse4_2'),
    (0.1, '--no-enable-sahf'),
    (0.1, '--no-enable-avx'),
    (0.1, '--no-enable-fma3'),
    (0.1, '--no-enable-bmi1'),
    (0.1, '--no-enable-bmi2'),
    (0.1, '--no-enable-lzcnt'),
    (0.1, '--no-enable-popcnt'),
    (0.3, '--no-lazy-feedback-allocation'),
    (0.1, '--no-liftoff'),
    (0.1, '--no-turbofan'),
    (0.1, '--no-wasm-tier-up'),
    (0.1, '--optimize-on-next-call-optimizes-to-maglev'),
    (0.1, '--regexp-interpret-all'),
    (0.1, '--regexp-tier-up-ticks=0'),
    (0.1, '--regexp-tier-up-ticks=10'),
    (0.1, '--regexp-tier-up-ticks=100'),
    (0.1, '--shared-string-table'),
    (0.1, '--stress-background-compile'),
    (0.1, '--stress-flush-code'),
    (0.1, '--stress-lazy-source-positions'),
    (0.1, '--stress-maglev'),
    (0.1, '--stress-wasm-code-gc'),
    (0.2, '--turboshaft'),
    (0.1, '--turboshaft-from-maglev'),
    (0.1, '--turbo-instruction-scheduling'),
    (0.1, '--turbo-stress-instruction-scheduling'),
    (0.1, '--stress-wasm-memory-moving'),
]

MIN_DEOPT = 1
MAX_DEOPT = 10**9
ANALYSIS_SUFFIX = 'analysis'


def random_extra_flags(rng):
  """Returns a random list of flags chosen from the configurations in
  EXTRA_FLAGS.
  """
  return [flag for prob, flag in EXTRA_FLAGS if rng.random() < prob]


def _flag_prefix(flag):
  """Returns the flag part before an equal sign."""
  if '=' not in flag:
    return flag
  else:
    return flag[0:flag.index('=')]


def _invert_flag(flag):
  """Flips a --flag and its --no-flag counterpart."""
  assert flag.startswith('--')
  if flag.startswith('--no-'):
    return '--' + flag[len('--no-'):]
  else:
    return '--no-' + flag[2:]


def _drop_contradictory_flags(new_flags, existing_flags):
  """Drops flags that have a simple contradiction with an existing flag.

  Contradictions checked for:
  - Repetition: --flag --flag
  - Repetition with param: --flag=foo --flag=bar
  - Negation: --flag --no-flag
  - Inverse negation: --no-flag --flag
  - For simplicity also drops combinations of negation and param, which don't
    occur in practice.

  Args:
    new_flags: new flags to filter from
    existing_flags: existing flags checked against
  Returns: A list of flags without contradictions.
  """
  existing_flag_prefixes = set(_flag_prefix(flag) for flag in existing_flags)

  def contradictory_flag(flag):
    flag_prefix = _flag_prefix(flag)
    return (flag_prefix in existing_flag_prefixes or
            _invert_flag(flag_prefix) in existing_flag_prefixes)

  return [flag for flag in new_flags if not contradictory_flag(flag)]


class FuzzerConfig(object):
  def __init__(self, probability, analyzer, fuzzer):
    """
    Args:
      probability: of choosing this fuzzer (0; 10]
      analyzer: instance of Analyzer class, can be None if no analysis is needed
      fuzzer: instance of Fuzzer class
    """
    assert probability > 0 and probability <= 10

    self.probability = probability
    self.analyzer = analyzer
    self.fuzzer = fuzzer


class Analyzer(object):
  def get_analysis_flags(self):
    raise NotImplementedError()

  def do_analysis(self, result):
    raise NotImplementedError()


class Fuzzer(object):
  def create_flags_generator(self, rng, test, analysis_value):
    """
    Args:
      rng: random number generator
      test: test for which to create flags
      analysis_value: value returned by the analyzer. None if there is no
        corresponding analyzer to this fuzzer or the analysis phase is disabled
    """
    raise NotImplementedError()


# TODO(majeski): Allow multiple subtests to run at once.
class FuzzerProc(base.TestProcProducer):
  @staticmethod
  def create(options):
    return FuzzerProc(
        options.fuzzer_rng(),
        options.fuzzer_tests_count(),
        options.fuzzer_configs(),
        options.combine_tests,
    )

  def __init__(self, rng, count, fuzzers, disable_analysis=False):
    """
    Args:
      rng: random number generator used to select flags and values for them
      count: number of tests to generate based on each base test
      fuzzers: list of FuzzerConfig instances
      disable_analysis: disable analysis phase and filtering base on it. When
        set, processor passes None as analysis result to fuzzers
    """
    super(FuzzerProc, self).__init__('Fuzzer')
    self._rng = rng
    self._count = count
    self._fuzzer_configs = fuzzers
    self._disable_analysis = disable_analysis
    self._gens = {}

  def test_suffix(self, test):
    return test.subtest_id

  def _next_test(self, test):
    if self.is_stopped:
      return False

    analysis_subtest = self._create_analysis_subtest(test)
    if analysis_subtest:
      return self._send_test(analysis_subtest)

    self._gens[test.procid] = self._create_gen(test)
    return self._try_send_next_test(test)

  def _create_analysis_subtest(self, test):
    if self._disable_analysis:
      return None

    analysis_flags = []
    for fuzzer_config in self._fuzzer_configs:
      if fuzzer_config.analyzer:
        analysis_flags += fuzzer_config.analyzer.get_analysis_flags()

    if analysis_flags:
      analysis_flags = list(set(analysis_flags))
      return test.create_subtest(
          self, ANALYSIS_SUFFIX, flags=analysis_flags, keep_output=True)

  def _result_for(self, test, subtest, result):
    if not self._disable_analysis:
      if result is not None and subtest.procid.endswith(
          f'{self.name}-{ANALYSIS_SUFFIX}'):
        # Analysis phase, for fuzzing we drop the result.
        if result.has_unexpected_output:
          self._send_result(test, None)
          return

        self._gens[test.procid] = self._create_gen(test, result)

    self._try_send_next_test(test)

  def _create_gen(self, test, analysis_result=None):
    # It will be called with analysis_result==None only when there is no
    # analysis phase at all, so no fuzzer has it's own analyzer.
    gens = []
    indexes = []
    for fuzzer_config in self._fuzzer_configs:
      analysis_value = None
      if analysis_result and fuzzer_config.analyzer:
        analysis_value = fuzzer_config.analyzer.do_analysis(analysis_result)
        if not analysis_value:
          # Skip fuzzer for this test since it doesn't have analysis data
          continue
      p = fuzzer_config.probability
      flag_gen = fuzzer_config.fuzzer.create_flags_generator(self._rng, test,
                                                             analysis_value)
      indexes += [len(gens)] * p
      gens.append((p, flag_gen))

    if not gens:
      # No fuzzers for this test, skip it
      return

    i = 0
    while not self._count or i < self._count:
      main_index = self._rng.choice(indexes)
      _, main_gen = gens[main_index]

      flags = random_extra_flags(self._rng) + next(main_gen)
      for index, (p, gen) in enumerate(gens):
        if index == main_index:
          continue
        if self._rng.randint(1, 10) <= p:
          flags += next(gen)

      flags.append('--fuzzer-random-seed=%s' % self._next_seed())

      flags = _drop_contradictory_flags(flags, test.get_flags())
      yield test.create_subtest(self, str(i), flags=flags)

      i += 1

  def _try_send_next_test(self, test):
    for subtest in self._gens[test.procid]:
      if self._send_test(subtest):
        return True
      elif self.is_stopped:
        return False

    del self._gens[test.procid]
    return False

  def _next_seed(self):
    seed = None
    while not seed:
      seed = self._rng.randint(-2147483648, 2147483647)
    return seed


class ScavengeAnalyzer(Analyzer):
  def get_analysis_flags(self):
    return ['--fuzzer-gc-analysis']

  def do_analysis(self, result):
    for line in reversed(result.output.stdout.splitlines()):
      if line.startswith('### Maximum new space size reached = '):
        return int(float(line.split()[7]))


class ScavengeFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--stress-scavenge=%d' % (analysis_value or 100)]


class MarkingAnalyzer(Analyzer):
  def get_analysis_flags(self):
    return ['--fuzzer-gc-analysis']

  def do_analysis(self, result):
    for line in reversed(result.output.stdout.splitlines()):
      if line.startswith('### Maximum marking limit reached = '):
        return int(float(line.split()[6]))


class MarkingFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--stress-marking=%d' % (analysis_value or 100)]


class GcIntervalAnalyzer(Analyzer):
  def get_analysis_flags(self):
    return ['--fuzzer-gc-analysis']

  def do_analysis(self, result):
    for line in reversed(result.output.stdout.splitlines()):
      if line.startswith('### Allocations = '):
        return int(float(line.split()[3][:-1]))


class GcIntervalFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    if analysis_value:
      value = analysis_value // 10
    else:
      value = 10000
    while True:
      yield ['--random-gc-interval=%d' % value]


class CompactionFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--stress-compaction-random']


class InterruptBudgetFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      # Half with, half without lazy feedback allocation. The first flag
      # overwrites potential flag negations from the extra flags list.
      flag1 = rng.choice(
          ['--lazy-feedback-allocation', '--no-lazy-feedback-allocation'])
      flag2 = '--invocation-count-for-turbofan=%d' % rng.randint(0, 240)
      flag3 = '--invocation-count-for-maglev=%d' % rng.randint(0, 120)
      flag4 = '--invocation-count-for-feedback-allocation=%d' % rng.randint(
          0, 8)

      yield [flag1, flag2, flag3, flag4]


class StackSizeFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--stack-size=%d' % rng.randint(54, 983)]


class TaskDelayFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--stress-delay-tasks']


class ThreadPoolSizeFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      yield ['--thread-pool-size=%d' % rng.randint(1, 8)]


class DeoptAnalyzer(Analyzer):
  def get_analysis_flags(self):
    return ['--deopt-every-n-times=%d' % MAX_DEOPT,
            '--print-deopt-stress']

  def do_analysis(self, result):
    for line in reversed(result.output.stdout.splitlines()):
      if line.startswith('=== Stress deopt counter: '):
        counter = MAX_DEOPT - int(line.split(' ')[-1])
        if counter < MIN_DEOPT:
          # Skip this test since we won't generate any meaningful interval with
          # given minimum.
          return None
        return counter


class DeoptFuzzer(Fuzzer):
  def create_flags_generator(self, rng, test, analysis_value):
    while True:
      if analysis_value:
        value = analysis_value // 2
      else:
        value = 10000
      interval = rng.randint(MIN_DEOPT, max(value, MIN_DEOPT))
      yield ['--deopt-every-n-times=%d' % interval]


FUZZERS = {
    'compaction': (None, CompactionFuzzer),
    'delay': (None, TaskDelayFuzzer),
    'deopt': (DeoptAnalyzer, DeoptFuzzer),
    'gc_interval': (GcIntervalAnalyzer, GcIntervalFuzzer),
    'interrupt': (None, InterruptBudgetFuzzer),
    'marking': (MarkingAnalyzer, MarkingFuzzer),
    'scavenge': (ScavengeAnalyzer, ScavengeFuzzer),
    'stack': (None, StackSizeFuzzer),
    'threads': (None, ThreadPoolSizeFuzzer),
}


def create_fuzzer_config(name, probability, *args, **kwargs):
  analyzer_class, fuzzer_class = FUZZERS[name]
  return FuzzerConfig(
      probability,
      analyzer_class(*args, **kwargs) if analyzer_class else None,
      fuzzer_class(*args, **kwargs),
  )
