# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from .utils import random_utils


class TestConfig(object):
  def __init__(self,
               command_prefix,
               extra_flags,
               extra_d8_flags,
               framework_name,
               isolates,
               log_process_stats,
               mode_flags,
               no_harness,
               noi18n,
               random_seed,
               run_skipped,
               shard_count,
               shard_id,
               shell_dir,
               target_os,
               timeout,
               verbose,
               regenerate_expected_files=False):
    self.command_prefix = command_prefix
    self.extra_flags = extra_flags
    self.extra_d8_flags = extra_d8_flags
    self.framework_name = framework_name
    self.isolates = isolates
    self.log_process_stats = log_process_stats
    self.mode_flags = mode_flags
    self.no_harness = no_harness
    self.noi18n = noi18n
    # random_seed is always not None.
    self.random_seed = random_seed or random_utils.random_seed()
    self.run_skipped = run_skipped
    self.shard_count = shard_count
    self.shard_id = shard_id
    self.shell_dir = shell_dir
    self.target_os = target_os
    self.timeout = timeout
    self.verbose = verbose
    self.regenerate_expected_files = regenerate_expected_files
