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

from os.path import isdir
from os.path import join
import os
import platform
import re


### Exit codes and their meaning.
# Normal execution.
EXIT_CODE_PASS = 0
# Execution with test failures.
EXIT_CODE_FAILURES = 1
# Execution with no tests executed.
EXIT_CODE_NO_TESTS = 2
# Execution aborted with SIGINT (Ctrl-C).
EXIT_CODE_INTERRUPTED = 3
# Execution aborted with SIGTERM.
EXIT_CODE_TERMINATED = 4
# Internal error.
EXIT_CODE_INTERNAL_ERROR = 5


def GetSuitePaths(test_root):
  return [ f for f in os.listdir(test_root) if isdir(join(test_root, f)) ]


# Reads a file into an array of strings
def ReadLinesFrom(name):
  lines = []
  with open(name) as f:
    for line in f:
      if line.startswith('#'): continue
      if '#' in line:
        line = line[:line.find('#')]
      line = line.strip()
      if not line: continue
      lines.append(line)
  return lines


def GuessOS():
  system = platform.system()
  if system == 'Linux':
    return 'linux'
  elif system == 'Darwin':
    return 'macos'
  elif system.find('CYGWIN') >= 0:
    return 'cygwin'
  elif system == 'Windows' or system == 'Microsoft':
    # On Windows Vista platform.system() can return 'Microsoft' with some
    # versions of Python, see http://bugs.python.org/issue1082
    return 'windows'
  elif system == 'FreeBSD':
    return 'freebsd'
  elif system == 'OpenBSD':
    return 'openbsd'
  elif system == 'SunOS':
    return 'solaris'
  elif system == 'NetBSD':
    return 'netbsd'
  elif system in ['AIX', 'OS400']:
    # OS400 runs an AIX emulator called PASE
    return 'aix'
  elif system == "OS/390":
    return 'zos'
  else:
    return None


# Check if Vector Enhancement Facility 1 is available on the
# host S390 machine. This facility is required for supporting Simd on V8.
def IsS390SimdSupported():
  if GuessOS() == 'zos':
    from ctypes import CDLL
    libname = os.environ.get('ZOSLIB_LIBPATH') + '/libzoslib.so'
    clib = CDLL(libname)
    return clib.__is_vef1_available()

  import subprocess
  cpuinfo = subprocess.check_output("cat /proc/cpuinfo", shell=True)
  cpuinfo_list = cpuinfo.strip().decode("utf-8").splitlines()
  facilities = "".join(x for x in cpuinfo_list if x.startswith("facilities"))
  facilities_list = facilities.split(" ")
  # Having bit 135 set indicates VEF1 is available.
  return "135" in facilities_list


# Returns power processor version, taking compatibility mode into account.
# (Power9 running in Power8 compatibility mode returns 8)
# Only useful if arch is ppc64
def GuessPowerProcessorVersion():
  import ctypes, ctypes.util
  os = GuessOS()
  if os == 'linux':
    AT_PLATFORM = 15 # from linux/auxvec.h
    _LIBC = ctypes.CDLL(ctypes.util.find_library('c'))
    _LIBC.getauxval.argtypes = [ctypes.c_ulong]
    _LIBC.getauxval.restype = ctypes.c_char_p
    at_platform = _LIBC.getauxval(AT_PLATFORM).decode('utf-8').lower()
    if at_platform.startswith('power6'):
      return 6
    elif at_platform.startswith('power7'):
      return 7
    elif at_platform.startswith('power8'):
      return 8
    elif at_platform.startswith('power9'):
      return 9
    elif at_platform.startswith('power10'):
      return 10
    else:
      raise Exception('Unable to guess power processor version')
  elif os == 'aix':
    # covers aix and os400
    RTLD_MEMBER = 0x00040000
    _LIBC = ctypes.CDLL(ctypes.util.find_library('c'),
                        ctypes.DEFAULT_MODE | RTLD_MEMBER)
    class _system_configuration(ctypes.Structure):
      _fields_ = [
        ('architecture', ctypes.c_int),
        ('implementation', ctypes.c_int),
      ]
    cfg = _system_configuration.in_dll(_LIBC, '_system_configuration')
    # Values found in sys/systemcfg.h
    if cfg.implementation == 0x4000:
      return 6
    elif cfg.implementation == 0x8000:
      return 7
    elif cfg.implementation == 0x10000:
      return 8
    elif cfg.implementation == 0x20000:
      return 9
    elif cfg.implementation == 0x40000:
      return 10
    else:
      raise Exception('Unable to guess power processor version')
  else:
    raise Exception('Unable to guess power processor version')


def UseSimulator(arch):
  machine = platform.machine()
  return (machine and (arch == "arm" or arch == "arm64") and
          not arch.startswith(machine))


# This will default to building the 32 bit VM even on machines that are
# capable of running the 64 bit VM.
def DefaultArch():
  machine = platform.machine()
  machine = machine.lower()  # Windows 7 capitalizes 'AMD64'.
  if machine.startswith('arm'):
    return 'arm'
  elif (not machine) or (not re.match('(x|i[3-6])86$', machine) is None):
    return 'ia32'
  elif machine == 'i86pc':
    return 'ia32'
  elif machine == 'x86_64':
    return 'ia32'
  elif machine == 'amd64':
    return 'ia32'
  elif machine == 's390x':
    return 's390'
  elif machine == 'ppc64':
    return 'ppc'
  else:
    return None


def GuessWordsize():
  if '64' in platform.machine():
    return '64'
  else:
    return '32'


def IsWindows():
  return GuessOS() == 'windows'


class FrozenDict(dict):
  def __setitem__(self, *args, **kwargs):
    raise Exception('Tried to mutate a frozen dict')

  def update(self, *args, **kwargs):
    raise Exception('Tried to mutate a frozen dict')


def Freeze(obj):
  if isinstance(obj, dict):
    return FrozenDict((k, Freeze(v)) for k, v in list(obj.items()))
  elif isinstance(obj, set):
    return frozenset(obj)
  elif isinstance(obj, list):
    return tuple(Freeze(item) for item in obj)
  else:
    # Make sure object is hashable.
    hash(obj)
    return obj
