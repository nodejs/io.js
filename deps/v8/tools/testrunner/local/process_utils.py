# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Tools for tracking process statistics like memory consumption.
"""

import platform
import time

from contextlib import contextmanager
from datetime import datetime
from threading import Thread, Event


PROBING_INTERVAL_SEC = 0.2
FLUSH_LOG_BUFFER_SEC = 2


class ProcessStats:
  """Storage class for process statistics indicating if data is available."""
  def __init__(self):
    self._max_rss = 0
    self._max_vms = 0
    self._available = False

  @property
  def max_rss(self):
    return self._max_rss

  @property
  def max_vms(self):
    return self._max_vms

  @property
  def available(self):
    return self._available

  def update(self, memory_info):
    self._max_rss = max(self._max_rss, memory_info.rss)
    self._max_vms = max(self._max_vms, memory_info.vms)
    self._available = True


class EmptyProcessLogger:
  @contextmanager
  def log_stats(self, process):
    """When wrapped, logs memory statistics of the Popen process argument.

    This base-class version can be used as a null object to turn off the
    feature, yielding null-object stats.
    """
    yield ProcessStats()

  @contextmanager
  def log_system_memory(self, log_path):
    """When wrapped, logs system memory statistics to 'log_path'.

    This base-class version keeps logging off.
    """
    yield


class PSUtilProcessLogger(EmptyProcessLogger):
  def __init__(
      self, probing_interval_sec=PROBING_INTERVAL_SEC,
      flush_log_buffer_sec=FLUSH_LOG_BUFFER_SEC):
    self.probing_interval_sec = probing_interval_sec
    self.log_buffer_max = int(flush_log_buffer_sec / probing_interval_sec)

  def get_pid(self, pid):
    return pid

  @contextmanager
  def log_stats(self, process):
    try:
      process_handle = psutil.Process(self.get_pid(process.pid))
    except (psutil.AccessDenied, psutil.NoSuchProcess):
      # Fetching process stats has an expected race condition with the
      # running process, which might just have ended already.
      yield ProcessStats()
      return

    stats = ProcessStats()
    finished = Event()
    def run_logger():
      try:
        while True:
          stats.update(process_handle.memory_info())
          if finished.wait(self.probing_interval_sec):
            break
      except (psutil.AccessDenied, psutil.NoSuchProcess):
        pass

    logger = Thread(target=run_logger)
    logger.start()
    try:
      yield stats
    finally:
      finished.set()

    # Until we have joined the logger thread, we can't access the stats
    # without a race condition.
    logger.join()

  @contextmanager
  def log_system_memory(self, log_path):
    with open(log_path, 'w') as handle:
      finished = Event()
      buffer = []

      def flush_buffer():
        time_str = datetime.utcfromtimestamp(time.time())
        values = ', '.join(map(lambda s: f'{s}%', buffer))
        print(f'{time_str} - {values}', file=handle)
        buffer.clear()

      def run_logger():
        while True:
          buffer.append(psutil.virtual_memory().percent)
          if len(buffer) >= self.log_buffer_max:
            flush_buffer()
          if finished.wait(self.probing_interval_sec):
            if buffer:
              flush_buffer()
            break

      logger = Thread(target=run_logger)
      logger.start()
      try:
        yield
      finally:
        finished.set()
        logger.join()


class LinuxPSUtilProcessLogger(PSUtilProcessLogger):

  def get_pid(self, pid):
    """Try to get the correct PID on Linux.

    On Linux, we call subprocesses using shell, which on some systems (Debian)
    has an optimization using exec and reusing the parent PID, while others
    (Ubuntu) create a child process with its own PID. We don't want to log
    memory stats of the shell parent.
    """
    try:
      with open(f'/proc/{pid}/task/{pid}/children') as f:
        children = f.read().strip().split(' ')
        if children and children[0]:
          # On Debian, we don't have child processes here.
          return int(children[0])
    except FileNotFoundError:
      # A quick process might already have finished.
      pass
    return pid


EMPTY_PROCESS_LOGGER = EmptyProcessLogger()
try:
  # Process utils are only supported when we use vpython or when psutil is
  # installed.
  import psutil
  if platform.system() == 'Linux':
    PROCESS_LOGGER = LinuxPSUtilProcessLogger()
  else:
    PROCESS_LOGGER = PSUtilProcessLogger()
except:
  PROCESS_LOGGER = EMPTY_PROCESS_LOGGER
