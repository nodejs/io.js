#!/usr/bin/env python3
#
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


# for py2/py3 compatibility
from __future__ import print_function

import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))

import bisect
import collections
import ctypes
import disasm
import mmap
import optparse
import os
import re
import subprocess
import sys
import time


USAGE="""usage: %prog [OPTION]...

Analyses V8 and perf logs to produce profiles.

Perf logs can be collected using a command like:
  $ perf record -R -e cycles -c 10000 -f -i ./d8 bench.js --ll-prof
  # -R: collect all data
  # -e cycles: use cpu-cycles event (run "perf list" for details)
  # -c 10000: write a sample after each 10000 events
  # -f: force output file overwrite
  # -i: limit profiling to our process and the kernel
  # --ll-prof shell flag enables the right V8 logs
This will produce a binary trace file (perf.data) that %prog can analyse.

IMPORTANT:
  The kernel has an internal maximum for events per second, it is 100K by
  default. That's not enough for "-c 10000". Set it to some higher value:
  $ echo 10000000 | sudo tee /proc/sys/kernel/perf_event_max_sample_rate
  You can also make the warning about kernel address maps go away:
  $ echo 0 | sudo tee /proc/sys/kernel/kptr_restrict

We have a convenience script that handles all of the above for you:
  $ tools/run-llprof.sh ./d8 bench.js

Examples:
  # Print flat profile with annotated disassembly for the 10 top
  # symbols. Use default log names.
  $ %prog --disasm-top=10

  # Print flat profile with annotated disassembly for all used symbols.
  # Use default log names and include kernel symbols into analysis.
  $ %prog --disasm-all --kernel

  # Print flat profile. Use custom log names.
  $ %prog --log=foo.log --trace=foo.data
"""


JS_ORIGIN = "js"


class Code(object):
  """Code object."""

  _id = 0
  UNKNOWN = 0
  V8INTERNAL = 1
  FULL_CODEGEN = 2
  OPTIMIZED = 3

  def __init__(self, name, start_address, end_address, origin, origin_offset):
    self.id = Code._id
    Code._id += 1
    self.name = name
    self.other_names = None
    self.start_address = start_address
    self.end_address = end_address
    self.origin = origin
    self.origin_offset = origin_offset
    self.self_ticks = 0
    self.self_ticks_map = None
    self.callee_ticks = None
    if name.startswith("LazyCompile:*"):
      self.codetype = Code.OPTIMIZED
    elif name.startswith("LazyCompile:"):
      self.codetype = Code.FULL_CODEGEN
    elif name.startswith("v8::internal::"):
      self.codetype = Code.V8INTERNAL
    else:
      self.codetype = Code.UNKNOWN

  def AddName(self, name):
    assert self.name != name
    if self.other_names is None:
      self.other_names = [name]
      return
    if not name in self.other_names:
      self.other_names.append(name)

  def FullName(self):
    if self.other_names is None:
      return self.name
    self.other_names.sort()
    return "%s (aka %s)" % (self.name, ", ".join(self.other_names))

  def IsUsed(self):
    return self.self_ticks > 0 or self.callee_ticks is not None

  def Tick(self, pc):
    self.self_ticks += 1
    if self.self_ticks_map is None:
      self.self_ticks_map = collections.defaultdict(lambda: 0)
    offset = pc - self.start_address
    self.self_ticks_map[offset] += 1

  def CalleeTick(self, callee):
    if self.callee_ticks is None:
      self.callee_ticks = collections.defaultdict(lambda: 0)
    self.callee_ticks[callee] += 1

  def PrintAnnotated(self, arch, options):
    if self.self_ticks_map is None:
      ticks_map = []
    else:
      ticks_map = self.self_ticks_map.items()
    # Convert the ticks map to offsets and counts arrays so that later
    # we can do binary search in the offsets array.
    ticks_map.sort(key=lambda t: t[0])
    ticks_offsets = [t[0] for t in ticks_map]
    ticks_counts = [t[1] for t in ticks_map]
    # Get a list of disassembled lines and their addresses.
    lines = self._GetDisasmLines(arch, options)
    if len(lines) == 0:
      return
    # Print annotated lines.
    address = lines[0][0]
    total_count = 0
    for i in range(len(lines)):
      start_offset = lines[i][0] - address
      if i == len(lines) - 1:
        end_offset = self.end_address - self.start_address
      else:
        end_offset = lines[i + 1][0] - address
      # Ticks (reported pc values) are not always precise, i.e. not
      # necessarily point at instruction starts. So we have to search
      # for ticks that touch the current instruction line.
      j = bisect.bisect_left(ticks_offsets, end_offset)
      count = 0
      for offset, cnt in reversed(zip(ticks_offsets[:j], ticks_counts[:j])):
        if offset < start_offset:
          break
        count += cnt
      total_count += count
      percent = 100.0 * count / self.self_ticks
      offset = lines[i][0]
      if percent >= 0.01:
        # 5 spaces for tick count
        # 1 space following
        # 1 for '|'
        # 1 space following
        # 6 for the percentage number, incl. the '.'
        # 1 for the '%' sign
        # => 15
        print("%5d | %6.2f%% %x(%d): %s" % (count, percent, offset, offset, lines[i][1]))
      else:
        print("%s %x(%d): %s" % (" " * 15, offset, offset, lines[i][1]))
    print()
    assert total_count == self.self_ticks, \
        "Lost ticks (%d != %d) in %s" % (total_count, self.self_ticks, self)

  def __str__(self):
    return "%s [0x%x, 0x%x) size: %d origin: %s" % (
      self.name,
      self.start_address,
      self.end_address,
      self.end_address - self.start_address,
      self.origin)

  def _GetDisasmLines(self, arch, options):
    if self.origin == JS_ORIGIN:
      inplace = False
      filename = options.log + ".ll"
    else:
      inplace = True
      filename = self.origin
    return disasm.GetDisasmLines(filename,
                                 self.origin_offset,
                                 self.end_address - self.start_address,
                                 arch,
                                 inplace)


class CodePage(object):
  """Group of adjacent code objects."""

  SHIFT = 20  # 1M pages
  SIZE = (1 << SHIFT)
  MASK = ~(SIZE - 1)

  @staticmethod
  def PageAddress(address):
    return address & CodePage.MASK

  @staticmethod
  def PageId(address):
    return address >> CodePage.SHIFT

  @staticmethod
  def PageAddressFromId(id):
    return id << CodePage.SHIFT

  def __init__(self, address):
    self.address = address
    self.code_objects = []

  def Add(self, code):
    self.code_objects.append(code)

  def Remove(self, code):
    self.code_objects.remove(code)

  def Find(self, pc):
    code_objects = self.code_objects
    for i, code in enumerate(code_objects):
      if code.start_address <= pc < code.end_address:
        code_objects[0], code_objects[i] = code, code_objects[0]
        return code
    return None

  def __iter__(self):
    return self.code_objects.__iter__()


class CodeMap(object):
  """Code object map."""

  def __init__(self):
    self.pages = {}
    self.min_address = 1 << 64
    self.max_address = -1

  def Add(self, code, max_pages=-1):
    page_id = CodePage.PageId(code.start_address)
    limit_id = CodePage.PageId(code.end_address + CodePage.SIZE - 1)
    pages = 0
    while page_id < limit_id:
      if max_pages >= 0 and pages > max_pages:
        print("Warning: page limit (%d) reached for %s [%s]" % (
            max_pages, code.name, code.origin), file=sys.stderr)
        break
      if page_id in self.pages:
        page = self.pages[page_id]
      else:
        page = CodePage(CodePage.PageAddressFromId(page_id))
        self.pages[page_id] = page
      page.Add(code)
      page_id += 1
      pages += 1
    self.min_address = min(self.min_address, code.start_address)
    self.max_address = max(self.max_address, code.end_address)

  def Remove(self, code):
    page_id = CodePage.PageId(code.start_address)
    limit_id = CodePage.PageId(code.end_address + CodePage.SIZE - 1)
    removed = False
    while page_id < limit_id:
      if page_id not in self.pages:
        page_id += 1
        continue
      page = self.pages[page_id]
      page.Remove(code)
      removed = True
      page_id += 1
    return removed

  def AllCode(self):
    for page in self.pages.values():
      for code in page:
        if CodePage.PageAddress(code.start_address) == page.address:
          yield code

  def UsedCode(self):
    for code in self.AllCode():
      if code.IsUsed():
        yield code

  def Print(self):
    for code in self.AllCode():
      print(code)

  def Find(self, pc):
    if pc < self.min_address or pc >= self.max_address:
      return None
    page_id = CodePage.PageId(pc)
    if page_id not in self.pages:
      return None
    return self.pages[page_id].Find(pc)


class CodeInfo(object):
  """Generic info about generated code objects."""

  def __init__(self, arch, header_size):
    self.arch = arch
    self.header_size = header_size


class LogReader(object):
  """V8 low-level (binary) log reader."""

  _ARCH_TO_POINTER_TYPE_MAP = {
    "ia32": ctypes.c_uint32,
    "arm": ctypes.c_uint32,
    "x64": ctypes.c_uint64,
    "arm64": ctypes.c_uint64
  }

  _CODE_CREATE_TAG = b"C"
  _CODE_MOVE_TAG = b"M"
  _CODE_MOVING_GC_TAG = b"G"

  def __init__(self, log_name, code_map):
    self.log_file = open(log_name, "r")
    self.log = mmap.mmap(self.log_file.fileno(), 0, mmap.MAP_PRIVATE)
    self.log_pos = 0
    self.code_map = code_map

    self.arch = self.log[:self.log.find(b"\0")].decode()
    self.log_pos += len(self.arch) + 1
    assert self.arch in LogReader._ARCH_TO_POINTER_TYPE_MAP, \
        "Unsupported architecture %s" % self.arch
    pointer_type = LogReader._ARCH_TO_POINTER_TYPE_MAP[self.arch]

    self.code_create_struct = LogReader._DefineStruct([
        ("name_size", ctypes.c_int32),
        ("code_address", pointer_type),
        ("code_size", ctypes.c_int32)])

    self.code_move_struct = LogReader._DefineStruct([
        ("from_address", pointer_type),
        ("to_address", pointer_type)])

    self.code_delete_struct = LogReader._DefineStruct([
        ("address", pointer_type)])

  def ReadUpToGC(self):
    while self.log_pos < self.log.size():
      tag = self.log[self.log_pos:self.log_pos + 1]
      self.log_pos += 1

      if tag == LogReader._CODE_MOVING_GC_TAG:
        return

      if tag == LogReader._CODE_CREATE_TAG:
        event = self.code_create_struct.from_buffer(self.log, self.log_pos)
        self.log_pos += ctypes.sizeof(event)
        start_address = event.code_address
        end_address = start_address + event.code_size
        name = self.log[self.log_pos:self.log_pos + event.name_size].decode()
        origin = JS_ORIGIN
        self.log_pos += event.name_size
        origin_offset = self.log_pos
        self.log_pos += event.code_size
        code = Code(name, start_address, end_address, origin, origin_offset)
        conficting_code = self.code_map.Find(start_address)
        if conficting_code:
          if not (conficting_code.start_address == code.start_address and
            conficting_code.end_address == code.end_address):
            self.code_map.Remove(conficting_code)
          else:
            LogReader._HandleCodeConflict(conficting_code, code)
            # TODO(vitalyr): this warning is too noisy because of our
            # attempts to reconstruct code log from the snapshot.
            # print >>sys.stderr, \
            #     "Warning: Skipping duplicate code log entry %s" % code
            continue
        self.code_map.Add(code)
        continue

      if tag == LogReader._CODE_MOVE_TAG:
        event = self.code_move_struct.from_buffer(self.log, self.log_pos)
        self.log_pos += ctypes.sizeof(event)
        old_start_address = event.from_address
        new_start_address = event.to_address
        if old_start_address == new_start_address:
          # Skip useless code move entries.
          continue
        code = self.code_map.Find(old_start_address)
        if not code:
          print("Warning: Not found %x" % old_start_address, file=sys.stderr)
          continue
        assert code.start_address == old_start_address, \
            "Inexact move address %x for %s" % (old_start_address, code)
        self.code_map.Remove(code)
        size = code.end_address - code.start_address
        code.start_address = new_start_address
        code.end_address = new_start_address + size
        self.code_map.Add(code)
        continue

      assert False, "Unknown tag %s" % tag

  def Dispose(self):
    self.log.close()
    self.log_file.close()

  @staticmethod
  def _DefineStruct(fields):
    class Struct(ctypes.Structure):
      _fields_ = fields
    return Struct

  @staticmethod
  def _HandleCodeConflict(old_code, new_code):
    assert (old_code.start_address == new_code.start_address and
            old_code.end_address == new_code.end_address), \
        "Conficting code log entries %s and %s" % (old_code, new_code)
    if old_code.name == new_code.name:
      return
    # Code object may be shared by a few functions. Collect the full
    # set of names.
    old_code.AddName(new_code.name)


class Descriptor(object):
  """Descriptor of a structure in the binary trace log."""

  CTYPE_MAP = {
    "u16": ctypes.c_uint16,
    "u32": ctypes.c_uint32,
    "u64": ctypes.c_uint64
  }

  def __init__(self, fields):
    class TraceItem(ctypes.Structure):
      _fields_ = Descriptor.CtypesFields(fields)

      def __str__(self):
        return ", ".join("%s: %s" % (field, self.__getattribute__(field))
                         for field, _ in TraceItem._fields_)

    self.ctype = TraceItem

  def Read(self, trace, offset):
    return self.ctype.from_buffer(trace, offset)

  @staticmethod
  def CtypesFields(fields):
    return [(field, Descriptor.CTYPE_MAP[format]) for (field, format) in fields]


# Please see http://git.kernel.org/?p=linux/kernel/git/torvalds/linux-2.6.git;a=tree;f=tools/perf
# for the gory details.


# Reference: struct perf_file_header in kernel/tools/perf/util/header.h
TRACE_HEADER_DESC = Descriptor([
  ("magic", "u64"),
  ("size", "u64"),
  ("attr_size", "u64"),
  ("attrs_offset", "u64"),
  ("attrs_size", "u64"),
  ("data_offset", "u64"),
  ("data_size", "u64"),
  ("event_types_offset", "u64"),
  ("event_types_size", "u64")
])


# Reference: /usr/include/linux/perf_event.h
PERF_EVENT_ATTR_DESC = Descriptor([
  ("type", "u32"),
  ("size", "u32"),
  ("config", "u64"),
  ("sample_period_or_freq", "u64"),
  ("sample_type", "u64"),
  ("read_format", "u64"),
  ("flags", "u64"),
  ("wakeup_events_or_watermark", "u32"),
  ("bp_type", "u32"),
  ("bp_addr", "u64"),
  ("bp_len", "u64")
])


# Reference: /usr/include/linux/perf_event.h
PERF_EVENT_HEADER_DESC = Descriptor([
  ("type", "u32"),
  ("misc", "u16"),
  ("size", "u16")
])


# Reference: kernel/tools/perf/util/event.h
PERF_MMAP_EVENT_BODY_DESC = Descriptor([
  ("pid", "u32"),
  ("tid", "u32"),
  ("addr", "u64"),
  ("len", "u64"),
  ("pgoff", "u64")
])

# Reference: kernel/tools/perf/util/event.h
PERF_MMAP2_EVENT_BODY_DESC = Descriptor([
  ("pid", "u32"),
  ("tid", "u32"),
  ("addr", "u64"),
  ("len", "u64"),
  ("pgoff", "u64"),
  ("maj", "u32"),
  ("min", "u32"),
  ("ino", "u64"),
  ("ino_generation", "u64"),
  ("prot", "u32"),
  ("flags","u32")
])

# perf_event_attr.sample_type bits control the set of
# perf_sample_event fields.
PERF_SAMPLE_IP = 1 << 0
PERF_SAMPLE_TID = 1 << 1
PERF_SAMPLE_TIME = 1 << 2
PERF_SAMPLE_ADDR = 1 << 3
PERF_SAMPLE_READ = 1 << 4
PERF_SAMPLE_CALLCHAIN = 1 << 5
PERF_SAMPLE_ID = 1 << 6
PERF_SAMPLE_CPU = 1 << 7
PERF_SAMPLE_PERIOD = 1 << 8
PERF_SAMPLE_STREAM_ID = 1 << 9
PERF_SAMPLE_RAW = 1 << 10


# Reference: /usr/include/perf_event.h, the comment for PERF_RECORD_SAMPLE.
PERF_SAMPLE_EVENT_BODY_FIELDS = [
  ("ip", "u64", PERF_SAMPLE_IP),
  ("pid", "u32", PERF_SAMPLE_TID),
  ("tid", "u32", PERF_SAMPLE_TID),
  ("time", "u64", PERF_SAMPLE_TIME),
  ("addr", "u64", PERF_SAMPLE_ADDR),
  ("id", "u64", PERF_SAMPLE_ID),
  ("stream_id", "u64", PERF_SAMPLE_STREAM_ID),
  ("cpu", "u32", PERF_SAMPLE_CPU),
  ("res", "u32", PERF_SAMPLE_CPU),
  ("period", "u64", PERF_SAMPLE_PERIOD),
  # Don't want to handle read format that comes after the period and
  # before the callchain and has variable size.
  ("nr", "u64", PERF_SAMPLE_CALLCHAIN)
  # Raw data follows the callchain and is ignored.
]


PERF_SAMPLE_EVENT_IP_FORMAT = "u64"


PERF_RECORD_MMAP = 1
PERF_RECORD_MMAP2 = 10
PERF_RECORD_SAMPLE = 9


class TraceReader(object):
  """Perf (linux-2.6/tools/perf) trace file reader."""

  _TRACE_HEADER_MAGIC = int.from_bytes(b"PERFFILE", byteorder="little")
  assert _TRACE_HEADER_MAGIC == 4993446653023372624
  _TRACE_HEADER_MAGIC_2 = int.from_bytes(b"PERFILE2", byteorder="little")
  assert _TRACE_HEADER_MAGIC_2 == 3622385352885552464

  def __init__(self, trace_name):
    self.trace_file = open(trace_name, "r")
    self.trace = mmap.mmap(self.trace_file.fileno(), 0, mmap.MAP_PRIVATE)
    self.trace_header = TRACE_HEADER_DESC.Read(self.trace, 0)
    if self.trace_header.magic not in (self._TRACE_HEADER_MAGIC,
                                       self._TRACE_HEADER_MAGIC_2):
      print("Warning: unsupported trace header magic", file=sys.stderr)
    self.offset = self.trace_header.data_offset
    self.limit = self.trace_header.data_offset + self.trace_header.data_size
    assert self.limit <= self.trace.size(), \
        "Trace data limit exceeds trace file size"
    self.header_size = ctypes.sizeof(PERF_EVENT_HEADER_DESC.ctype)
    assert self.trace_header.attrs_size != 0, \
        "No perf event attributes found in the trace"
    perf_event_attr = PERF_EVENT_ATTR_DESC.Read(self.trace,
                                                self.trace_header.attrs_offset)
    self.sample_event_body_desc = self._SampleEventBodyDesc(
        perf_event_attr.sample_type)
    self.callchain_supported = \
        (perf_event_attr.sample_type & PERF_SAMPLE_CALLCHAIN) != 0
    self.ip_struct = None
    if self.callchain_supported:
      self.ip_struct = Descriptor.CTYPE_MAP[PERF_SAMPLE_EVENT_IP_FORMAT]
      self.ip_size = ctypes.sizeof(self.ip_struct)

  def ReadEventHeader(self):
    if self.offset >= self.limit:
      return None, 0
    offset = self.offset
    header = PERF_EVENT_HEADER_DESC.Read(self.trace, self.offset)
    self.offset += header.size
    return header, offset

  def ReadMmap(self, header, offset):
    mmap_info = PERF_MMAP_EVENT_BODY_DESC.Read(self.trace,
                                               offset + self.header_size)
    # Read null-terminated filename.
    filename = self.trace[offset + self.header_size + ctypes.sizeof(mmap_info):
                          offset + header.size]
    mmap_info.filename = HOST_ROOT + filename[:filename.find(b"\0")].decode()
    return mmap_info

  def ReadMmap2(self, header, offset):
    mmap_info = PERF_MMAP2_EVENT_BODY_DESC.Read(self.trace,
                                                offset + self.header_size)
    # Read null-terminated filename.
    filename = self.trace[offset + self.header_size + ctypes.sizeof(mmap_info):
                          offset + header.size]
    mmap_info.filename = HOST_ROOT + filename[:filename.find(b"\0")].decode()
    return mmap_info

  def ReadSample(self, header, offset):
    sample = self.sample_event_body_desc.Read(self.trace,
                                              offset + self.header_size)
    if not self.callchain_supported:
      return sample
    sample.ips = []
    offset += self.header_size + ctypes.sizeof(sample)
    for _ in range(sample.nr):
      sample.ips.append(
        self.ip_struct.from_buffer(self.trace, offset).value)
      offset += self.ip_size
    return sample

  def Dispose(self):
    if self.ip_struct:
      del self.ip_struct
    del self.trace_header
    del self.sample_event_body_desc
    self.trace_file.close()
    # TODO: fix this, we leak some data somewhere.
    # self.trace.close()

  def _SampleEventBodyDesc(self, sample_type):
    assert (sample_type & PERF_SAMPLE_READ) == 0, \
           "Can't hande read format in samples"
    fields = [(field, format)
              for (field, format, bit) in PERF_SAMPLE_EVENT_BODY_FIELDS
              if (bit & sample_type) != 0]
    return Descriptor(fields)


OBJDUMP_SECTION_HEADER_RE = re.compile(
  r"^\s*\d+\s(\.\S+)\s+[a-f0-9]")
OBJDUMP_SYMBOL_LINE_RE = re.compile(
  r"^([a-f0-9]+)\s(.{7})\s(\S+)\s+([a-f0-9]+)\s+(?:\.hidden\s+)?(.*)$")
OBJDUMP_DYNAMIC_SYMBOLS_START_RE = re.compile(
  r"^DYNAMIC SYMBOL TABLE")
OBJDUMP_SKIP_RE = re.compile(
  r"^.*ld\.so\.cache$")
KERNEL_ALLSYMS_FILE = "/proc/kallsyms"
PERF_KERNEL_ALLSYMS_RE = re.compile(
  r".*kallsyms.*")
KERNEL_ALLSYMS_LINE_RE = re.compile(
  r"^([a-f0-9]+)\s(?:t|T)\s(\S+)$")


class LibraryRepo(object):
  def __init__(self):
    self.infos = []
    self.names = set()
    self.ticks = {}


  def HasDynamicSymbols(self, filename):
    if filename.endswith(".ko"): return False
    process = subprocess.Popen(
      "%s -h %s" % (OBJDUMP_BIN, filename),
      shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pipe = process.stdout
    try:
      for line in pipe:
        match = OBJDUMP_SECTION_HEADER_RE.match(line.decode())
        if match and match.group(1) == 'dynsym':
          return True
    finally:
      pipe.close()
    assert process.wait() == 0, "Failed to objdump -h %s" % filename
    return False


  def Load(self, mmap_info, code_map, options):
    # Skip kernel mmaps when requested using the fact that their tid
    # is 0.
    if mmap_info.tid == 0 and not options.kernel:
      return True
    if OBJDUMP_SKIP_RE.match(mmap_info.filename):
      return True
    if PERF_KERNEL_ALLSYMS_RE.match(mmap_info.filename):
      return self._LoadKernelSymbols(code_map)
    self.infos.append(mmap_info)
    mmap_info.ticks = 0
    mmap_info.unique_name = self._UniqueMmapName(mmap_info)
    if not os.path.exists(mmap_info.filename):
      return True
    # Request section headers (-h), symbols (-t), and dynamic symbols
    # (-T) from objdump.
    # Unfortunately, section headers span two lines, so we have to
    # keep the just seen section name (from the first line in each
    # section header) in the after_section variable.
    if self.HasDynamicSymbols(mmap_info.filename):
      dynamic_symbols = "-T"
    else:
      dynamic_symbols = ""
    process = subprocess.Popen(
      "%s -h -t %s -C %s" % (OBJDUMP_BIN, dynamic_symbols, mmap_info.filename),
      shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pipe = process.stdout
    after_section = None
    code_sections = set()
    reloc_sections = set()
    dynamic = False
    try:
      for line in pipe:
        line = line.decode()
        if after_section:
          if line.find("CODE") != -1:
            code_sections.add(after_section)
          if line.find("RELOC") != -1:
            reloc_sections.add(after_section)
          after_section = None
          continue

        match = OBJDUMP_SECTION_HEADER_RE.match(line)
        if match:
          after_section = match.group(1)
          continue

        if OBJDUMP_DYNAMIC_SYMBOLS_START_RE.match(line):
          dynamic = True
          continue

        match = OBJDUMP_SYMBOL_LINE_RE.match(line)
        if match:
          start_address = int(match.group(1), 16)
          origin_offset = start_address
          flags = match.group(2)
          section = match.group(3)
          if section in code_sections:
            if dynamic or section in reloc_sections:
              start_address += mmap_info.addr
            size = int(match.group(4), 16)
            name = match.group(5)
            origin = mmap_info.filename
            code_map.Add(Code(name, start_address, start_address + size,
                              origin, origin_offset))
    finally:
      pipe.close()
    assert process.wait() == 0, "Failed to objdump %s" % mmap_info.filename

  def Tick(self, pc):
    for i, mmap_info in enumerate(self.infos):
      if mmap_info.addr <= pc < (mmap_info.addr + mmap_info.len):
        mmap_info.ticks += 1
        self.infos[0], self.infos[i] = mmap_info, self.infos[0]
        return True
    return False

  def _UniqueMmapName(self, mmap_info):
    name = mmap_info.filename
    index = 1
    while name in self.names:
      name = "%s-%d" % (mmap_info.filename, index)
      index += 1
    self.names.add(name)
    return name

  def _LoadKernelSymbols(self, code_map):
    if not os.path.exists(KERNEL_ALLSYMS_FILE):
      print("Warning: %s not found" % KERNEL_ALLSYMS_FILE, file=sys.stderr)
      return False
    kallsyms = open(KERNEL_ALLSYMS_FILE, "r")
    code = None
    for line in kallsyms:
      match = KERNEL_ALLSYMS_LINE_RE.match(line)
      if match:
        start_address = int(match.group(1), 16)
        end_address = start_address
        name = match.group(2)
        if code:
          code.end_address = start_address
          code_map.Add(code, 16)
        code = Code(name, start_address, end_address, "kernel", 0)
    return True


def PrintReport(code_map, library_repo, arch, ticks, options):
  print("Ticks per symbol:")
  used_code = [code for code in code_map.UsedCode()]
  used_code.sort(key=lambda x: x.self_ticks, reverse=True)
  for i, code in enumerate(used_code):
    code_ticks = code.self_ticks
    print("%10d %5.1f%% %s [%s]" % (code_ticks, 100. * code_ticks / ticks,
                                    code.FullName(), code.origin))
    if options.disasm_all or i < options.disasm_top:
      code.PrintAnnotated(arch, options)
  print()
  print("Ticks per library:")
  mmap_infos = [m for m in library_repo.infos if m.ticks > 0]
  mmap_infos.sort(key=lambda m: m.ticks, reverse=True)
  for mmap_info in mmap_infos:
    mmap_ticks = mmap_info.ticks
    print("%10d %5.1f%% %s" % (mmap_ticks, 100. * mmap_ticks / ticks,
                               mmap_info.unique_name))


def PrintDot(code_map, options):
  print("digraph G {")
  for code in code_map.UsedCode():
    if code.self_ticks < 10:
      continue
    print("n%d [shape=box,label=\"%s\"];" % (code.id, code.name))
    if code.callee_ticks:
      for callee, ticks in code.callee_ticks.iteritems():
        print("n%d -> n%d [label=\"%d\"];" % (code.id, callee.id, ticks))
  print("}")


if __name__ == "__main__":
  parser = optparse.OptionParser(USAGE)
  parser.add_option("--log",
                    default="v8.log",
                    help="V8 log file name [default: %default]")
  parser.add_option("--trace",
                    default="perf.data",
                    help="perf trace file name [default: %default]")
  parser.add_option("--kernel",
                    default=False,
                    action="store_true",
                    help="process kernel entries [default: %default]")
  parser.add_option("--disasm-top",
                    default=0,
                    type="int",
                    help=("number of top symbols to disassemble and annotate "
                          "[default: %default]"))
  parser.add_option("--disasm-all",
                    default=False,
                    action="store_true",
                    help=("disassemble and annotate all used symbols "
                          "[default: %default]"))
  parser.add_option("--dot",
                    default=False,
                    action="store_true",
                    help="produce dot output (WIP) [default: %default]")
  parser.add_option("--quiet", "-q",
                    default=False,
                    action="store_true",
                    help="no auxiliary messages [default: %default]")
  parser.add_option("--gc-fake-mmap",
                    default="/tmp/__v8_gc__",
                    help="gc fake mmap file [default: %default]")
  parser.add_option("--objdump",
                    default="/usr/bin/objdump",
                    help="objdump tool to use [default: %default]")
  parser.add_option("--host-root",
                    default="",
                    help="Path to the host root [default: %default]")
  options, args = parser.parse_args()

  if not options.quiet:
    print("V8 log: %s, %s.ll" % (options.log, options.log))
    print("Perf trace file: %s" % options.trace)

  V8_GC_FAKE_MMAP = options.gc_fake_mmap
  HOST_ROOT = options.host_root
  if os.path.exists(options.objdump):
    disasm.OBJDUMP_BIN = options.objdump
    OBJDUMP_BIN = options.objdump
  else:
    print("Cannot find %s, falling back to default objdump" % options.objdump)

  # Stats.
  events = 0
  ticks = 0
  missed_ticks = 0
  really_missed_ticks = 0
  optimized_ticks = 0
  generated_ticks = 0
  v8_internal_ticks = 0
  mmap_time = 0
  sample_time = 0

  # Initialize the log reader.
  code_map = CodeMap()
  log_reader = LogReader(log_name=options.log + ".ll",
                         code_map=code_map)
  if not options.quiet:
    print("Generated code architecture: %s" % log_reader.arch)
    print()
    sys.stdout.flush()

  # Process the code and trace logs.
  library_repo = LibraryRepo()
  log_reader.ReadUpToGC()
  trace_reader = TraceReader(options.trace)
  while True:
    header, offset = trace_reader.ReadEventHeader()
    if not header:
      break
    events += 1
    if header.type == PERF_RECORD_MMAP:
      start = time.time()
      mmap_info = trace_reader.ReadMmap(header, offset)
      if mmap_info.filename == HOST_ROOT + V8_GC_FAKE_MMAP:
        log_reader.ReadUpToGC()
      else:
        library_repo.Load(mmap_info, code_map, options)
      mmap_time += time.time() - start
    elif header.type == PERF_RECORD_MMAP2:
      start = time.time()
      mmap_info = trace_reader.ReadMmap2(header, offset)
      if mmap_info.filename == HOST_ROOT + V8_GC_FAKE_MMAP:
        log_reader.ReadUpToGC()
      else:
        library_repo.Load(mmap_info, code_map, options)
      mmap_time += time.time() - start
    elif header.type == PERF_RECORD_SAMPLE:
      ticks += 1
      start = time.time()
      sample = trace_reader.ReadSample(header, offset)
      code = code_map.Find(sample.ip)
      if code:
        code.Tick(sample.ip)
        if code.codetype == Code.OPTIMIZED:
          optimized_ticks += 1
        elif code.codetype == Code.FULL_CODEGEN:
          generated_ticks += 1
        elif code.codetype == Code.V8INTERNAL:
          v8_internal_ticks += 1
      else:
        missed_ticks += 1
      if not library_repo.Tick(sample.ip) and not code:
        really_missed_ticks += 1
      if trace_reader.callchain_supported:
        for ip in sample.ips:
          caller_code = code_map.Find(ip)
          if caller_code:
            if code:
              caller_code.CalleeTick(code)
            code = caller_code
      sample_time += time.time() - start

  if options.dot:
    PrintDot(code_map, options)
  else:
    PrintReport(code_map, library_repo, log_reader.arch, ticks, options)

    if not options.quiet:
      def PrintTicks(number, total, description):
        print("%10d %5.1f%% ticks in %s" %
              (number, 100.0*number/total, description))
      print()
      print("Stats:")
      print("%10d total trace events" % events)
      print("%10d total ticks" % ticks)
      print("%10d ticks not in symbols" % missed_ticks)
      unaccounted = "unaccounted ticks"
      if really_missed_ticks > 0:
        unaccounted += " (probably in the kernel, try --kernel)"
      PrintTicks(really_missed_ticks, ticks, unaccounted)
      PrintTicks(optimized_ticks, ticks, "ticks in optimized code")
      PrintTicks(generated_ticks, ticks, "ticks in other lazily compiled code")
      PrintTicks(v8_internal_ticks, ticks, "ticks in v8::internal::*")
      print("%10d total symbols" % len([c for c in code_map.AllCode()]))
      print("%10d used symbols" % len([c for c in code_map.UsedCode()]))
      print("%9.2fs library processing time" % mmap_time)
      print("%9.2fs tick processing time" % sample_time)

  log_reader.Dispose()
  trace_reader.Dispose()
