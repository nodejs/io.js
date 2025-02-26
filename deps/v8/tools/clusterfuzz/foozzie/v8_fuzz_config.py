# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import random

from pathlib import Path

THIS_DIR = Path(__file__).parent.resolve()

# List of configuration experiments for correctness fuzzing.
# List of <probability>, <1st config name>, <2nd config name>, <2nd d8>.
# Probabilities must add up to 100.
with (THIS_DIR / 'v8_fuzz_experiments.json').open() as f:
  FOOZZIE_EXPERIMENTS = json.load(f)

# Additional flag experiments. List of tuples like
# (<likelihood to use flags in [0,1)>, <flag>).
with (THIS_DIR / 'v8_fuzz_flags.json').open() as f:
  ADDITIONAL_FLAGS = json.load(f)


class Config(object):
  def __init__(self, name, rng=None):
    """
    Args:
      name: Name of the used fuzzer.
      rng: Random number generator for generating experiments.
      random_seed: Random-seed used for d8 throughout one fuzz session.
    """
    self.name = name
    self.rng = rng or random.Random()

  def choose_foozzie_flags(self, foozzie_experiments=None, additional_flags=None):
    """Randomly chooses a configuration from FOOZZIE_EXPERIMENTS.

    Args:
      foozzie_experiments: Override experiment config for testing.
      additional_flags: Override additional flags for testing.

    Returns: List of flags to pass to v8_foozzie.py fuzz harness.
    """
    foozzie_experiments = foozzie_experiments or FOOZZIE_EXPERIMENTS
    additional_flags = additional_flags or ADDITIONAL_FLAGS

    # Add additional flags to second config based on experiment percentages.
    extra_flags = []
    for p, flags in additional_flags:
      if self.rng.random() < p:
        for flag in flags.split():
          extra_flags.append(f'--second-config-extra-flags={flag}')

    # Calculate flags determining the experiment.
    acc = 0
    threshold = self.rng.random() * 100
    for prob, first_config, second_config, second_d8 in foozzie_experiments:
      acc += prob
      if acc > threshold:
        return [
            f'--first-config={first_config}',
            f'--second-config={second_config}',
            f'--second-d8={second_d8}',
        ] + extra_flags
    assert False
