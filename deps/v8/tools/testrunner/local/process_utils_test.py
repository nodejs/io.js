#!/usr/bin/env python3
# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import psutil
import threading
import unittest

from collections import namedtuple
from mock import patch
from pathlib import Path
from pyfakefs import fake_filesystem_unittest

import process_utils


Process = namedtuple('Process', 'pid')
MemoryInfo = namedtuple('MemoryInfo', 'rss, vms')
SystemMemory = namedtuple('SystemMemory', 'percent')


class TestStats(unittest.TestCase):
  def test_update(self):
    stats = process_utils.ProcessStats()
    self.assertFalse(stats.available)

    stats.update(MemoryInfo(17, 42))
    self.assertTrue(stats.available)
    self.assertEqual(17, stats.max_rss)
    self.assertEqual(42, stats.max_vms)

    stats.update(MemoryInfo(23, 23))
    self.assertTrue(stats.available)
    self.assertEqual(23, stats.max_rss)
    self.assertEqual(42, stats.max_vms)


class TestProcessLogger(unittest.TestCase):
  def test_null(self):
    with process_utils.EmptyProcessLogger().log_stats(None) as stats:
      pass
    self.assertFalse(stats.available)
    self.assertEqual(0, stats.max_rss)
    self.assertEqual(0, stats.max_vms)

  def test_basic(self):
    """Test three iterations of probing with mocked psutil."""
    done = threading.Event()
    results = [MemoryInfo(17, 101), MemoryInfo(2, 11), MemoryInfo(23, 42)]

    class FakeProcess:
      def __init__(self, process):
        self.iter = iter(results)

      def memory_info(self):
        try:
          return self.iter.__next__()
        except StopIteration:
          done.set()
          raise psutil.NoSuchProcess(123)

    logger = process_utils.PSUtilProcessLogger(0.01)
    with patch('psutil.Process', FakeProcess):
      with logger.log_stats(Process(123)) as stats:
        done.wait()

    self.assertTrue(stats.available)
    self.assertEqual(23, stats.max_rss)
    self.assertEqual(101, stats.max_vms)

  def test_fast_process(self):
    """Test a process that finished fast."""
    class FakeProcess:
      def __init__(self, process):
        raise psutil.NoSuchProcess(123)

    logger = process_utils.PSUtilProcessLogger(0.01)
    with patch('psutil.Process', FakeProcess):
      with logger.log_stats(Process(123)) as stats:
        pass

    self.assertFalse(stats.available)
    self.assertEqual(0, stats.max_rss)
    self.assertEqual(0, stats.max_vms)


class TestLinuxProcessLogger(fake_filesystem_unittest.TestCase):
  def test_child_process(self):
    self.setUpPyfakefs(allow_root_user=True)
    os.makedirs('/proc/123/task/123')
    with open('/proc/123/task/123/children', 'w') as f:
      f.write('42')
    logger = process_utils.LinuxPSUtilProcessLogger()
    self.assertEqual(42, logger.get_pid(123))

  def test_no_child_process(self):
    self.setUpPyfakefs(allow_root_user=True)
    os.makedirs('/proc/123/task/123')
    with open('/proc/123/task/123/children', 'w') as f:
      f.write('')
    logger = process_utils.LinuxPSUtilProcessLogger()
    self.assertEqual(123, logger.get_pid(123))

  def test_process_ended(self):
    self.setUpPyfakefs(allow_root_user=True)
    logger = process_utils.LinuxPSUtilProcessLogger()
    self.assertEqual(123, logger.get_pid(123))


class TestSystemMemoryLogger(fake_filesystem_unittest.TestCase):
  def test_log(self):
    """Test logging memory stats from a background thread.

    Simulate deterministic stats and timestamps. The main thread waits
    for at least two iterations.
    """
    done = threading.Event()
    results = 3 * [SystemMemory(1.7)]
    it = iter(results)

    def side_effect():
      try:
        return it.__next__()
      except StopIteration:
        done.set()

        # If the main thread is slow, we might keep running into this.
        return results[0]

    output_file = Path('dir') / 'memory.log'
    self.setUpPyfakefs()
    self.fs.create_dir(output_file.parent)
    logger = process_utils.PSUtilProcessLogger(0.01, 0.02)

    with patch('psutil.virtual_memory', side_effect=side_effect):
      with patch('time.time', return_value=13123212323):
        with logger.log_system_memory(output_file):
          done.wait()

    # We get at least two lines of deterministic log. We might get more
    # due to unpredictable delay after done.wait() on the main thread.
    with open(output_file) as f:
      lines = f.readlines()
      expected = '2385-11-10 00:45:23 - 1.7%, 1.7%\n'
      self.assertLess(1, len(lines))
      self.assertEqual(expected, lines[0])
      self.assertEqual(expected, lines[1])


if __name__ == '__main__':
  unittest.main()
