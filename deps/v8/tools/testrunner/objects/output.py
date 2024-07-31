# Copyright 2012 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


import signal
import copy

from ..local.process_utils import ProcessStats
from ..local import utils


class Output(object):

  def __init__(self, exit_code=0, timed_out=False, stdout=None, stderr=None,
               pid=None, start_time=0, end_time=0, stats=None):
    self.exit_code = exit_code
    self.timed_out = timed_out
    self.stdout = stdout
    self.stderr = stderr
    self.pid = pid
    self.start_time = start_time
    self.end_time = end_time
    self.stats = stats or ProcessStats()

  @property
  def duration(self):
    return self.end_time - self.start_time

  def without_text(self):
    """Returns copy of the output without stdout and stderr."""
    other = copy.copy(self)
    other.stdout = None
    other.stderr = None
    return other

  def HasCrashed(self):
    if utils.IsWindows():
      return 0x80000000 & self.exit_code and not (0x3FFFFF00 & self.exit_code)
    else:
      # Timed out tests will have exit_code -signal.SIGTERM.
      if self.timed_out:
        return False
      return (self.exit_code < 0 and
              self.exit_code != -signal.SIGABRT)

  def HasTimedOut(self):
    return self.timed_out

  def IsSuccess(self):
    return not self.HasCrashed() and not self.HasTimedOut()

  @property
  def exit_code_string(self):
    return "%d [%02X]" % (self.exit_code, self.exit_code & 0xffffffff)


class _NullOutput(Output):
  """Useful to signal that the binary has not been run."""
  def __init__(self):
    super(_NullOutput, self).__init__()


# Default instance of the _NullOutput class above.
NULL_OUTPUT = _NullOutput()
