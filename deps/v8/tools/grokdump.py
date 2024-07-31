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

# flake8: noqa  # https://bugs.chromium.org/p/v8/issues/detail?id=8784


import http.server as http_server
import bisect
import html
import cmd
import codecs
import ctypes
import datetime
import disasm
import inspect
import mmap
import optparse
import os
import re
import io
import sys
import types
import urllib.parse
import webbrowser

try:
  import v8heapconst
except ImportError:
  print("Importing v8heapconst.py failed. "
        "Run `tools/dev/gm.py mkgrokdump` to regenerate it.")
  sys.exit(1)

PORT_NUMBER = 8081


USAGE="""usage: %prog [OPTIONS] [DUMP-FILE]

Minidump analyzer.

Shows the processor state at the point of exception including the
stack of the active thread and the referenced objects in the V8
heap. Code objects are disassembled and the addresses linked from the
stack (e.g. pushed return addresses) are marked with "=>".

Examples:
  $ %prog 12345678-1234-1234-1234-123456789abcd-full.dmp"""


DEBUG=False


def DebugPrint(s):
  if not DEBUG: return
  print(s)


class Descriptor(object):
  """Descriptor of a structure in a memory."""

  def __init__(self, fields):
    self.fields = fields
    self.is_flexible = False
    for _, type_or_func in fields:
      if isinstance(type_or_func, types.FunctionType):
        self.is_flexible = True
        break
    if not self.is_flexible:
      self.ctype = Descriptor._GetCtype(fields)
      self.size = ctypes.sizeof(self.ctype)

  def Read(self, memory, offset):
    if self.is_flexible:
      fields_copy = self.fields[:]
      last = 0
      for name, type_or_func in fields_copy:
        if isinstance(type_or_func, types.FunctionType):
          partial_ctype = Descriptor._GetCtype(fields_copy[:last])
          partial_object = partial_ctype.from_buffer(memory, offset)
          type = type_or_func(partial_object)
          if type is not None:
            fields_copy[last] = (name, type)
            last += 1
        else:
          last += 1
      complete_ctype = Descriptor._GetCtype(fields_copy[:last])
    else:
      complete_ctype = self.ctype
    return complete_ctype.from_buffer(memory, offset)

  @staticmethod
  def _GetCtype(fields):
    class Raw(ctypes.Structure):
      _fields_ = fields
      _pack_ = 1

      def __str__(self):
        return "{" + ", ".join("%s: %s" % (field, self.__getattribute__(field))
                               for field, _ in Raw._fields_) + "}"
    return Raw


def FullDump(reader, heap):
  """Dump all available memory regions."""
  def dump_region(reader, start, size, location):
    print()
    while start & 3 != 0:
      start += 1
      size -= 1
      location += 1
    is_executable = reader.IsProbableExecutableRegion(location, size)
    is_ascii = reader.IsProbableASCIIRegion(location, size)

    if is_executable is not False:
      lines = reader.GetDisasmLines(start, size)
      for line in lines:
        print(FormatDisasmLine(start, heap, line))
      print()

    if is_ascii is not False:
      # Output in the same format as the Unix hd command
      addr = start
      for i in range(0, size, 16):
        slot = i + location
        hex_line = ""
        asc_line = ""
        for i in range(16):
          if slot + i < location + size:
            byte = ctypes.c_uint8.from_buffer(reader.minidump, slot + i).value
            if byte >= 0x20 and byte < 0x7f:
              asc_line += chr(byte)
            else:
              asc_line += "."
            hex_line += " %02x" % (byte)
          else:
            hex_line += "   "
          if i == 7:
            hex_line += " "
        print("%s  %s |%s|" % (reader.FormatIntPtr(addr),
                               hex_line,
                               asc_line))
        addr += 16

    if is_executable is not True and is_ascii is not True:
      print("%s - %s" % (reader.FormatIntPtr(start),
                         reader.FormatIntPtr(start + size)))
      print(start + size + 1);
      for i in range(0, size, reader.MachinePointerSize()):
        slot = start + i
        maybe_address = reader.ReadUIntPtr(slot)
        heap_object = heap.FindObject(maybe_address)
        print("%s: %s" % (reader.FormatIntPtr(slot),
                          reader.FormatIntPtr(maybe_address)))
        if heap_object:
          heap_object.Print(Printer())
          print()

  reader.ForEachMemoryRegion(dump_region)

# Heap constants generated by 'make grokdump' in v8heapconst module.
INSTANCE_TYPES = v8heapconst.INSTANCE_TYPES
KNOWN_MAPS = v8heapconst.KNOWN_MAPS
KNOWN_OBJECTS = v8heapconst.KNOWN_OBJECTS
FRAME_MARKERS = v8heapconst.FRAME_MARKERS

# Markers pushed on the stack by PushStackTraceAndDie
MAGIC_MARKER_PAIRS = (
    (0xbbbbbbbb, 0xbbbbbbbb),
    (0xfefefefe, 0xfefefeff),
)
# See StackTraceFailureMessage in isolate.h
STACK_TRACE_MARKER = 0xdecade30
# See FailureMessage in logging.cc
ERROR_MESSAGE_MARKER = 0xdecade10

# Set of structures and constants that describe the layout of minidump
# files. Based on MSDN and Google Breakpad.

MINIDUMP_HEADER = Descriptor([
  ("signature", ctypes.c_uint32),
  ("version", ctypes.c_uint32),
  ("stream_count", ctypes.c_uint32),
  ("stream_directories_rva", ctypes.c_uint32),
  ("checksum", ctypes.c_uint32),
  ("time_date_stampt", ctypes.c_uint32),
  ("flags", ctypes.c_uint64)
])

MINIDUMP_LOCATION_DESCRIPTOR = Descriptor([
  ("data_size", ctypes.c_uint32),
  ("rva", ctypes.c_uint32)
])

MINIDUMP_STRING = Descriptor([
  ("length", ctypes.c_uint32),
  ("buffer", lambda t: ctypes.c_uint8 * (t.length + 2))
])

MINIDUMP_DIRECTORY = Descriptor([
  ("stream_type", ctypes.c_uint32),
  ("location", MINIDUMP_LOCATION_DESCRIPTOR.ctype)
])

MD_EXCEPTION_MAXIMUM_PARAMETERS = 15

MINIDUMP_EXCEPTION = Descriptor([
  ("code", ctypes.c_uint32),
  ("flags", ctypes.c_uint32),
  ("record", ctypes.c_uint64),
  ("address", ctypes.c_uint64),
  ("parameter_count", ctypes.c_uint32),
  ("unused_alignment", ctypes.c_uint32),
  ("information", ctypes.c_uint64 * MD_EXCEPTION_MAXIMUM_PARAMETERS)
])

MINIDUMP_EXCEPTION_STREAM = Descriptor([
  ("thread_id", ctypes.c_uint32),
  ("unused_alignment", ctypes.c_uint32),
  ("exception", MINIDUMP_EXCEPTION.ctype),
  ("thread_context", MINIDUMP_LOCATION_DESCRIPTOR.ctype)
])

# Stream types.
MD_UNUSED_STREAM = 0
MD_RESERVED_STREAM_0 = 1
MD_RESERVED_STREAM_1 = 2
MD_THREAD_LIST_STREAM = 3
MD_MODULE_LIST_STREAM = 4
MD_MEMORY_LIST_STREAM = 5
MD_EXCEPTION_STREAM = 6
MD_SYSTEM_INFO_STREAM = 7
MD_THREAD_EX_LIST_STREAM = 8
MD_MEMORY_64_LIST_STREAM = 9
MD_COMMENT_STREAM_A = 10
MD_COMMENT_STREAM_W = 11
MD_HANDLE_DATA_STREAM = 12
MD_FUNCTION_TABLE_STREAM = 13
MD_UNLOADED_MODULE_LIST_STREAM = 14
MD_MISC_INFO_STREAM = 15
MD_MEMORY_INFO_LIST_STREAM = 16
MD_THREAD_INFO_LIST_STREAM = 17
MD_HANDLE_OPERATION_LIST_STREAM = 18

MD_FLOATINGSAVEAREA_X86_REGISTERAREA_SIZE = 80

MINIDUMP_FLOATING_SAVE_AREA_X86 = Descriptor([
  ("control_word", ctypes.c_uint32),
  ("status_word", ctypes.c_uint32),
  ("tag_word", ctypes.c_uint32),
  ("error_offset", ctypes.c_uint32),
  ("error_selector", ctypes.c_uint32),
  ("data_offset", ctypes.c_uint32),
  ("data_selector", ctypes.c_uint32),
  ("register_area", ctypes.c_uint8 * MD_FLOATINGSAVEAREA_X86_REGISTERAREA_SIZE),
  ("cr0_npx_state", ctypes.c_uint32)
])

MD_CONTEXT_X86_EXTENDED_REGISTERS_SIZE = 512

# Context flags.
MD_CONTEXT_X86 = 0x00010000
MD_CONTEXT_X86_CONTROL = (MD_CONTEXT_X86 | 0x00000001)
MD_CONTEXT_X86_INTEGER = (MD_CONTEXT_X86 | 0x00000002)
MD_CONTEXT_X86_SEGMENTS = (MD_CONTEXT_X86 | 0x00000004)
MD_CONTEXT_X86_FLOATING_POINT = (MD_CONTEXT_X86 | 0x00000008)
MD_CONTEXT_X86_DEBUG_REGISTERS = (MD_CONTEXT_X86 | 0x00000010)
MD_CONTEXT_X86_EXTENDED_REGISTERS = (MD_CONTEXT_X86 | 0x00000020)

def EnableOnFlag(type, flag):
  return lambda o: [None, type][int((o.context_flags & flag) != 0)]

MINIDUMP_CONTEXT_X86 = Descriptor([
  ("context_flags", ctypes.c_uint32),
  # MD_CONTEXT_X86_DEBUG_REGISTERS.
  ("dr0", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  ("dr1", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  ("dr2", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  ("dr3", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  ("dr6", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  ("dr7", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_DEBUG_REGISTERS)),
  # MD_CONTEXT_X86_FLOATING_POINT.
  ("float_save", EnableOnFlag(MINIDUMP_FLOATING_SAVE_AREA_X86.ctype,
                              MD_CONTEXT_X86_FLOATING_POINT)),
  # MD_CONTEXT_X86_SEGMENTS.
  ("gs", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_SEGMENTS)),
  ("fs", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_SEGMENTS)),
  ("es", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_SEGMENTS)),
  ("ds", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_SEGMENTS)),
  # MD_CONTEXT_X86_INTEGER.
  ("edi", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  ("esi", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  ("ebx", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  ("edx", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  ("ecx", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  ("eax", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_INTEGER)),
  # MD_CONTEXT_X86_CONTROL.
  ("ebp", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  ("eip", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  ("cs", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  ("eflags", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  ("esp", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  ("ss", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_X86_CONTROL)),
  # MD_CONTEXT_X86_EXTENDED_REGISTERS.
  ("extended_registers",
   EnableOnFlag(ctypes.c_uint8 * MD_CONTEXT_X86_EXTENDED_REGISTERS_SIZE,
                MD_CONTEXT_X86_EXTENDED_REGISTERS))
])

MD_CONTEXT_ARM = 0x40000000
MD_CONTEXT_ARM_INTEGER = (MD_CONTEXT_ARM | 0x00000002)
MD_CONTEXT_ARM_FLOATING_POINT = (MD_CONTEXT_ARM | 0x00000004)
MD_FLOATINGSAVEAREA_ARM_FPR_COUNT = 32
MD_FLOATINGSAVEAREA_ARM_FPEXTRA_COUNT = 8

MINIDUMP_FLOATING_SAVE_AREA_ARM = Descriptor([
  ("fpscr", ctypes.c_uint64),
  ("regs", ctypes.c_uint64 * MD_FLOATINGSAVEAREA_ARM_FPR_COUNT),
  ("extra", ctypes.c_uint64 * MD_FLOATINGSAVEAREA_ARM_FPEXTRA_COUNT)
])

MINIDUMP_CONTEXT_ARM = Descriptor([
  ("context_flags", ctypes.c_uint32),
  # MD_CONTEXT_ARM_INTEGER.
  ("r0", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r1", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r2", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r3", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r4", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r5", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r6", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r7", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r8", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r9", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r10", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r11", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("r12", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("sp", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("lr", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("pc", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_ARM_INTEGER)),
  ("cpsr", ctypes.c_uint32),
  ("float_save", EnableOnFlag(MINIDUMP_FLOATING_SAVE_AREA_ARM.ctype,
                              MD_CONTEXT_ARM_FLOATING_POINT))
])


MD_CONTEXT_ARM64 =  0x80000000
MD_CONTEXT_ARM64_INTEGER = (MD_CONTEXT_ARM64 | 0x00000002)
MD_CONTEXT_ARM64_FLOATING_POINT = (MD_CONTEXT_ARM64 | 0x00000004)
MD_FLOATINGSAVEAREA_ARM64_FPR_COUNT = 64

MINIDUMP_FLOATING_SAVE_AREA_ARM = Descriptor([
  ("fpscr", ctypes.c_uint64),
  ("regs", ctypes.c_uint64 * MD_FLOATINGSAVEAREA_ARM64_FPR_COUNT),
])

MINIDUMP_CONTEXT_ARM64 = Descriptor([
  ("context_flags", ctypes.c_uint64),
  # MD_CONTEXT_ARM64_INTEGER.
  ("r0", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r1", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r2", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r3", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r4", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r5", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r6", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r7", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r8", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r9", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r10", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r11", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r12", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r13", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r14", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r15", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r16", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r17", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r18", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r19", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r20", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r21", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r22", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r23", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r24", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r25", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r26", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r27", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("r28", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("fp", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("lr", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("sp", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("pc", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_ARM64_INTEGER)),
  ("cpsr", ctypes.c_uint32),
  ("float_save", EnableOnFlag(MINIDUMP_FLOATING_SAVE_AREA_ARM.ctype,
                              MD_CONTEXT_ARM64_FLOATING_POINT))
])


MD_CONTEXT_AMD64 = 0x00100000
MD_CONTEXT_AMD64_CONTROL = (MD_CONTEXT_AMD64 | 0x00000001)
MD_CONTEXT_AMD64_INTEGER = (MD_CONTEXT_AMD64 | 0x00000002)
MD_CONTEXT_AMD64_SEGMENTS = (MD_CONTEXT_AMD64 | 0x00000004)
MD_CONTEXT_AMD64_FLOATING_POINT = (MD_CONTEXT_AMD64 | 0x00000008)
MD_CONTEXT_AMD64_DEBUG_REGISTERS = (MD_CONTEXT_AMD64 | 0x00000010)

MINIDUMP_CONTEXT_AMD64 = Descriptor([
  ("p1_home", ctypes.c_uint64),
  ("p2_home", ctypes.c_uint64),
  ("p3_home", ctypes.c_uint64),
  ("p4_home", ctypes.c_uint64),
  ("p5_home", ctypes.c_uint64),
  ("p6_home", ctypes.c_uint64),
  ("context_flags", ctypes.c_uint32),
  ("mx_csr", ctypes.c_uint32),
  # MD_CONTEXT_AMD64_CONTROL.
  ("cs", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_CONTROL)),
  # MD_CONTEXT_AMD64_SEGMENTS
  ("ds", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_SEGMENTS)),
  ("es", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_SEGMENTS)),
  ("fs", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_SEGMENTS)),
  ("gs", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_SEGMENTS)),
  # MD_CONTEXT_AMD64_CONTROL.
  ("ss", EnableOnFlag(ctypes.c_uint16, MD_CONTEXT_AMD64_CONTROL)),
  ("eflags", EnableOnFlag(ctypes.c_uint32, MD_CONTEXT_AMD64_CONTROL)),
  # MD_CONTEXT_AMD64_DEBUG_REGISTERS.
  ("dr0", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("dr1", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("dr2", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("dr3", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("dr6", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("dr7", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  # MD_CONTEXT_AMD64_INTEGER.
  ("rax", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("rcx", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("rdx", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("rbx", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  # MD_CONTEXT_AMD64_CONTROL.
  ("rsp", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_CONTROL)),
  # MD_CONTEXT_AMD64_INTEGER.
  ("rbp", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("rsi", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("rdi", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r8", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r9", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r10", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r11", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r12", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r13", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r14", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  ("r15", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_INTEGER)),
  # MD_CONTEXT_AMD64_CONTROL.
  ("rip", EnableOnFlag(ctypes.c_uint64, MD_CONTEXT_AMD64_CONTROL)),
  # MD_CONTEXT_AMD64_FLOATING_POINT
  ("sse_registers", EnableOnFlag(ctypes.c_uint8 * (16 * 26),
                                 MD_CONTEXT_AMD64_FLOATING_POINT)),
  ("vector_registers", EnableOnFlag(ctypes.c_uint8 * (16 * 26),
                                    MD_CONTEXT_AMD64_FLOATING_POINT)),
  ("vector_control", EnableOnFlag(ctypes.c_uint64,
                                  MD_CONTEXT_AMD64_FLOATING_POINT)),
  # MD_CONTEXT_AMD64_DEBUG_REGISTERS.
  ("debug_control", EnableOnFlag(ctypes.c_uint64,
                                 MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("last_branch_to_rip", EnableOnFlag(ctypes.c_uint64,
                                      MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("last_branch_from_rip", EnableOnFlag(ctypes.c_uint64,
                                        MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("last_exception_to_rip", EnableOnFlag(ctypes.c_uint64,
                                         MD_CONTEXT_AMD64_DEBUG_REGISTERS)),
  ("last_exception_from_rip", EnableOnFlag(ctypes.c_uint64,
                                           MD_CONTEXT_AMD64_DEBUG_REGISTERS))
])

MINIDUMP_MEMORY_DESCRIPTOR = Descriptor([
  ("start", ctypes.c_uint64),
  ("memory", MINIDUMP_LOCATION_DESCRIPTOR.ctype)
])

MINIDUMP_MEMORY_DESCRIPTOR64 = Descriptor([
  ("start", ctypes.c_uint64),
  ("size", ctypes.c_uint64)
])

MINIDUMP_MEMORY_LIST = Descriptor([
  ("range_count", ctypes.c_uint32),
  ("ranges", lambda m: MINIDUMP_MEMORY_DESCRIPTOR.ctype * m.range_count)
])

MINIDUMP_MEMORY_LIST_Mac = Descriptor([
  ("range_count", ctypes.c_uint32),
  ("junk", ctypes.c_uint32),
  ("ranges", lambda m: MINIDUMP_MEMORY_DESCRIPTOR.ctype * m.range_count)
])

MINIDUMP_MEMORY_LIST64 = Descriptor([
  ("range_count", ctypes.c_uint64),
  ("base_rva", ctypes.c_uint64),
  ("ranges", lambda m: MINIDUMP_MEMORY_DESCRIPTOR64.ctype * m.range_count)
])

MINIDUMP_THREAD = Descriptor([
  ("id", ctypes.c_uint32),
  ("suspend_count", ctypes.c_uint32),
  ("priority_class", ctypes.c_uint32),
  ("priority", ctypes.c_uint32),
  ("ted", ctypes.c_uint64),
  ("stack", MINIDUMP_MEMORY_DESCRIPTOR.ctype),
  ("context", MINIDUMP_LOCATION_DESCRIPTOR.ctype)
])

MINIDUMP_THREAD_LIST = Descriptor([
  ("thread_count", ctypes.c_uint32),
  ("threads", lambda t: MINIDUMP_THREAD.ctype * t.thread_count)
])

MINIDUMP_THREAD_LIST_Mac = Descriptor([
  ("thread_count", ctypes.c_uint32),
  ("junk", ctypes.c_uint32),
  ("threads", lambda t: MINIDUMP_THREAD.ctype * t.thread_count)
])

MINIDUMP_VS_FIXEDFILEINFO = Descriptor([
  ("dwSignature", ctypes.c_uint32),
  ("dwStrucVersion", ctypes.c_uint32),
  ("dwFileVersionMS", ctypes.c_uint32),
  ("dwFileVersionLS", ctypes.c_uint32),
  ("dwProductVersionMS", ctypes.c_uint32),
  ("dwProductVersionLS", ctypes.c_uint32),
  ("dwFileFlagsMask", ctypes.c_uint32),
  ("dwFileFlags", ctypes.c_uint32),
  ("dwFileOS", ctypes.c_uint32),
  ("dwFileType", ctypes.c_uint32),
  ("dwFileSubtype", ctypes.c_uint32),
  ("dwFileDateMS", ctypes.c_uint32),
  ("dwFileDateLS", ctypes.c_uint32)
])

MINIDUMP_RAW_MODULE = Descriptor([
  ("base_of_image", ctypes.c_uint64),
  ("size_of_image", ctypes.c_uint32),
  ("checksum", ctypes.c_uint32),
  ("time_date_stamp", ctypes.c_uint32),
  ("module_name_rva", ctypes.c_uint32),
  ("version_info", MINIDUMP_VS_FIXEDFILEINFO.ctype),
  ("cv_record", MINIDUMP_LOCATION_DESCRIPTOR.ctype),
  ("misc_record", MINIDUMP_LOCATION_DESCRIPTOR.ctype),
  ("reserved0", ctypes.c_uint32 * 2),
  ("reserved1", ctypes.c_uint32 * 2)
])

MINIDUMP_MODULE_LIST = Descriptor([
  ("number_of_modules", ctypes.c_uint32),
  ("modules", lambda t: MINIDUMP_RAW_MODULE.ctype * t.number_of_modules)
])

MINIDUMP_MODULE_LIST_Mac = Descriptor([
  ("number_of_modules", ctypes.c_uint32),
  ("junk", ctypes.c_uint32),
  ("modules", lambda t: MINIDUMP_RAW_MODULE.ctype * t.number_of_modules)
])

MINIDUMP_RAW_SYSTEM_INFO = Descriptor([
  ("processor_architecture", ctypes.c_uint16)
])

MD_CPU_ARCHITECTURE_X86 = 0
MD_CPU_ARCHITECTURE_ARM = 5
# Breakpad used a custom value of 0x8003 here; Crashpad uses the new
# standardized value 12.
MD_CPU_ARCHITECTURE_ARM64 = 12
MD_CPU_ARCHITECTURE_ARM64_BREAKPAD_LEGACY = 0x8003
MD_CPU_ARCHITECTURE_AMD64 = 9

OBJDUMP_BIN = None
DEFAULT_OBJDUMP_BIN = '/usr/bin/objdump'

class FuncSymbol:
  def __init__(self, start, size, name):
    self.start = start
    self.end = self.start + size
    self.name = name

  def __cmp__(self, other):
    if isinstance(other, FuncSymbol):
      return self.start - other.start
    return self.start - other

  def Covers(self, addr):
    return (self.start <= addr) and (addr < self.end)


class MinidumpReader(object):
  """Minidump (.dmp) reader."""

  _HEADER_MAGIC = 0x504d444d

  def __init__(self, options, minidump_name):
    self._reset()
    self.minidump_name = minidump_name
    if sys.platform == 'win32':
      self.minidump_file = open(minidump_name, "a+")
      self.minidump = mmap.mmap(self.minidump_file.fileno(), 0)
    else:
      self.minidump_file = open(minidump_name, "r")
      self.minidump = mmap.mmap(self.minidump_file.fileno(), 0, mmap.MAP_PRIVATE)
    self.header = MINIDUMP_HEADER.Read(self.minidump, 0)
    if self.header.signature != MinidumpReader._HEADER_MAGIC:
      print("Warning: Unsupported minidump header magic!", file=sys.stderr)
    DebugPrint(self.header)
    offset = self.header.stream_directories_rva
    directories = []
    for _ in range(self.header.stream_count):
      directories.append(MINIDUMP_DIRECTORY.Read(self.minidump, offset))
      offset += MINIDUMP_DIRECTORY.size

    self.symdir = options.symdir
    self._ReadArchitecture(directories)
    self._ReadDirectories(directories)
    self._FindObjdump(options)

  def _reset(self):
    self.header = None
    self.arch = None
    self.exception = None
    self.exception_context = None
    self.memory_list = None
    self.memory_list64 = None
    self.module_list = None
    self.thread_map = {}

    self.modules_with_symbols = []
    self.symbols = []


  def _ReadArchitecture(self, directories):
    # Find MDRawSystemInfo stream and determine arch.
    for d in directories:
      if d.stream_type == MD_SYSTEM_INFO_STREAM:
        system_info = MINIDUMP_RAW_SYSTEM_INFO.Read(
            self.minidump, d.location.rva)
        self.arch = system_info.processor_architecture
        if self.arch == MD_CPU_ARCHITECTURE_ARM64_BREAKPAD_LEGACY:
          self.arch = MD_CPU_ARCHITECTURE_ARM64
        assert self.arch in [MD_CPU_ARCHITECTURE_AMD64,
                             MD_CPU_ARCHITECTURE_ARM,
                             MD_CPU_ARCHITECTURE_ARM64,
                             MD_CPU_ARCHITECTURE_X86]
    assert not self.arch is None

  def _ReadDirectories(self, directories):
    for d in directories:
      DebugPrint(d)
      if d.stream_type == MD_EXCEPTION_STREAM:
        self.exception = MINIDUMP_EXCEPTION_STREAM.Read(
          self.minidump, d.location.rva)
        DebugPrint(self.exception)
        self.exception_context = self.ContextDescriptor().Read(
            self.minidump, self.exception.thread_context.rva)
        DebugPrint(self.exception_context)
      elif d.stream_type == MD_THREAD_LIST_STREAM:
        thread_list = MINIDUMP_THREAD_LIST.Read(self.minidump, d.location.rva)
        if ctypes.sizeof(thread_list) + 4 == d.location.data_size:
          thread_list = MINIDUMP_THREAD_LIST_Mac.Read(
              self.minidump, d.location.rva)
        assert ctypes.sizeof(thread_list) == d.location.data_size
        DebugPrint(thread_list)
        for thread in thread_list.threads:
          DebugPrint(thread)
          self.thread_map[thread.id] = thread
      elif d.stream_type == MD_MODULE_LIST_STREAM:
        assert self.module_list is None
        self.module_list = MINIDUMP_MODULE_LIST.Read(
          self.minidump, d.location.rva)
        if ctypes.sizeof(self.module_list) + 4 == d.location.data_size:
          self.module_list = MINIDUMP_MODULE_LIST_Mac.Read(
              self.minidump, d.location.rva)
        assert ctypes.sizeof(self.module_list) == d.location.data_size
        DebugPrint(self.module_list)
      elif d.stream_type == MD_MEMORY_LIST_STREAM:
        print("Warning: This is not a full minidump!", file=sys.stderr)
        assert self.memory_list is None
        self.memory_list = MINIDUMP_MEMORY_LIST.Read(
          self.minidump, d.location.rva)
        if ctypes.sizeof(self.memory_list) + 4 == d.location.data_size:
          self.memory_list = MINIDUMP_MEMORY_LIST_Mac.Read(
              self.minidump, d.location.rva)
        assert ctypes.sizeof(self.memory_list) == d.location.data_size
        DebugPrint(self.memory_list)
      elif d.stream_type == MD_MEMORY_64_LIST_STREAM:
        assert self.memory_list64 is None
        self.memory_list64 = MINIDUMP_MEMORY_LIST64.Read(
          self.minidump, d.location.rva)
        assert ctypes.sizeof(self.memory_list64) == d.location.data_size
        DebugPrint(self.memory_list64)

  def _FindObjdump(self, options):
    if options.objdump:
      objdump_bin = options.objdump
    else:
      objdump_bin = self._FindThirdPartyObjdump()
    if not objdump_bin or not os.path.exists(objdump_bin):
      print("# Cannot find '%s', falling back to default objdump '%s'" % (
          objdump_bin, DEFAULT_OBJDUMP_BIN))
      objdump_bin  = DEFAULT_OBJDUMP_BIN
    global OBJDUMP_BIN
    OBJDUMP_BIN = objdump_bin
    disasm.OBJDUMP_BIN = objdump_bin

  def _FindThirdPartyObjdump(self):
    # Try to find the platform specific objdump.
    if self.arch == MD_CPU_ARCHITECTURE_ARM:
      platform_filter = 'arm-linux'
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      platform_filter = 'aarch64'
    else:
      # Use default otherwise.
      return None
    print(("# Looking for platform specific (%s) objdump in "
           "third_party directory.") % platform_filter)
    third_party_dir = os.path.join(
        os.path.dirname(os.path.dirname(__file__)), 'third_party')
    for root, dirs, files in os.walk(third_party_dir):
      for file in files:
        if file.endswith("objdump") and platform_filter in file:
          return os.path.join(root, file)
    print("# Could not find platform specific objdump in third_party.")
    print("# Make sure you installed the correct SDK.")
    return None

  def ContextDescriptor(self):
    if self.arch == MD_CPU_ARCHITECTURE_X86:
      return MINIDUMP_CONTEXT_X86
    elif self.arch == MD_CPU_ARCHITECTURE_AMD64:
      return MINIDUMP_CONTEXT_AMD64
    elif self.arch == MD_CPU_ARCHITECTURE_ARM:
      return MINIDUMP_CONTEXT_ARM
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      return MINIDUMP_CONTEXT_ARM64
    else:
      return None

  def IsValidAlignedAddress(self, address):
    return self.IsAlignedAddress(address) and self.IsValidAddress(address)

  def IsValidAddress(self, address):
    return self.FindLocation(address) is not None

  def IsAlignedAddress(self, address):
    return (address % self.MachinePointerSize()) == 0

  def IsExceptionStackAddress(self, address):
    if not self.IsAlignedAddress(address): return False
    return self.IsAnyExceptionStackAddress(address)

  def IsAnyExceptionStackAddress(self, address):
    return self.StackTop() <= address <= self.StackBottom()

  def IsValidExceptionStackAddress(self, address):
    if not self.IsValidAddress(address): return False
    return self.IsExceptionStackAddress(address)

  def IsModuleAddress(self, address):
    return self.GetModuleForAddress(address) is not None

  def GetModuleForAddress(self, address):
    for module in self.module_list.modules:
      start = module.base_of_image
      end = start + module.size_of_image
      if start <= address < end: return module
    return None

  def ReadU8(self, address):
    location = self.FindLocation(address)
    return ctypes.c_uint8.from_buffer(self.minidump, location).value

  def ReadU32(self, address):
    location = self.FindLocation(address)
    return ctypes.c_uint32.from_buffer(self.minidump, location).value

  def ReadU64(self, address):
    location = self.FindLocation(address)
    return ctypes.c_uint64.from_buffer(self.minidump, location).value

  def Is64(self):
    return (self.arch == MD_CPU_ARCHITECTURE_ARM64 or
            self.arch == MD_CPU_ARCHITECTURE_AMD64)

  def IsPointerCompressed(self):
    # Assume all 64-bit builds are pointer compressed.
    return self.Is64()

  def Is32BitTagged(self):
    return not self.Is64() or self.IsPointerCompressed()

  def ReadTagged(self, address):
    if self.Is32BitTagged():
      return self.ReadU32(address)
    return self.ReadU64(address)

  def ReadUIntPtr(self, address):
    if self.Is64():
      return self.ReadU64(address)
    return self.ReadU32(address)

  def ReadSized(self, address, size):
    if size == 8:
      return self.ReadU64(address)
    assert (size == 4)
    return self.ReadU32(address)

  def ReadBytes(self, address, size):
    location = self.FindLocation(address)
    return self.minidump[location:location + size]

  def _ReadWord(self, location):
    if self.Is64():
      return ctypes.c_uint64.from_buffer(self.minidump, location).value
    return ctypes.c_uint32.from_buffer(self.minidump, location).value

  def ReadAsciiPtr(self, address):
    ascii_content = [
        chr(c) if c >= 0x20 and c < 0x7f else '.'
        for c in self.ReadBytes(address, self.MachinePointerSize())
    ]
    return ''.join(ascii_content)

  def ReadAsciiString(self, address):
    string = ""
    while self.IsValidAddress(address):
      code = self.ReadU8(address)
      if 0 < code < 128:
        string += chr(code)
      else:
        break
      address += 1
    return string

  def IsProbableASCIIRegion(self, location, length):
    ascii_bytes = 0
    non_ascii_bytes = 0
    for i in range(length):
      loc = location + i
      byte = ctypes.c_uint8.from_buffer(self.minidump, loc).value
      if byte >= 0x7f:
        non_ascii_bytes += 1
      if byte < 0x20 and byte != 0:
        non_ascii_bytes += 1
      if byte < 0x7f and byte >= 0x20:
        ascii_bytes += 1
      if byte == 0xa:  # newline
        ascii_bytes += 1
    if ascii_bytes * 10 <= length:
      return False
    if length > 0 and ascii_bytes > non_ascii_bytes * 7:
      return True
    if ascii_bytes > non_ascii_bytes * 3:
      return None  # Maybe
    return False

  def IsProbableExecutableRegion(self, location, length):
    opcode_bytes = 0
    sixty_four = self.Is64()
    for i in range(length):
      loc = location + i
      byte = ctypes.c_uint8.from_buffer(self.minidump, loc).value
      if (byte == 0x8b or           # mov
          byte == 0x89 or           # mov reg-reg
          (byte & 0xf0) == 0x50 or  # push/pop
          (sixty_four and (byte & 0xf0) == 0x40) or  # rex prefix
          byte == 0xc3 or           # return
          byte == 0x74 or           # jeq
          byte == 0x84 or           # jeq far
          byte == 0x75 or           # jne
          byte == 0x85 or           # jne far
          byte == 0xe8 or           # call
          byte == 0xe9 or           # jmp far
          byte == 0xeb):            # jmp near
        opcode_bytes += 1
    opcode_percent = (opcode_bytes * 100) / length
    threshold = 20
    if opcode_percent > threshold + 2:
      return True
    if opcode_percent > threshold - 2:
      return None  # Maybe
    return False

  def FindRegion(self, addr):
    answer = [-1, -1]
    def is_in(reader, start, size, location):
      if addr >= start and addr < start + size:
        answer[0] = start
        answer[1] = size
    self.ForEachMemoryRegion(is_in)
    if answer[0] == -1:
      return None
    return answer

  def ForEachMemoryRegion(self, cb):
    if self.memory_list64 is not None:
      for r in self.memory_list64.ranges:
        location = self.memory_list64.base_rva + offset
        cb(self, r.start, r.size, location)
        offset += r.size

    if self.memory_list is not None:
      for r in self.memory_list.ranges:
        cb(self, r.start, r.memory.data_size, r.memory.rva)

  def FindWord(self, word, alignment=0):
    def search_inside_region(reader, start, size, location):
      location = (location + alignment) & ~alignment
      for i in range(size - self.MachinePointerSize()):
        loc = location + i
        if reader._ReadWord(loc) == word:
          slot = start + (loc - location)
          print("%s: %s" % (reader.FormatIntPtr(slot),
                            reader.FormatIntPtr(word)))
    self.ForEachMemoryRegion(search_inside_region)

  def FindPtr(self, expected_value, start, end):
    ptr_size = self.MachinePointerSize()
    for slot in range(start, end, ptr_size):
      if not self.IsValidAddress(slot):
        return None
      value = self.ReadUIntPtr(slot)
      if value == expected_value:
        return slot
    return None

  def FindWordList(self, word):
    aligned_res = []
    unaligned_res = []
    def search_inside_region(reader, start, size, location):
      for i in range(size - self.MachinePointerSize()):
        loc = location + i
        if reader._ReadWord(loc) == word:
          slot = start + (loc - location)
          if self.IsAlignedAddress(slot):
            aligned_res.append(slot)
          else:
            unaligned_res.append(slot)
    self.ForEachMemoryRegion(search_inside_region)
    return (aligned_res, unaligned_res)

  def FindLocation(self, address):
    offset = 0
    if self.memory_list64 is not None:
      for r in self.memory_list64.ranges:
        if r.start <= address < r.start + r.size:
          return self.memory_list64.base_rva + offset + address - r.start
        offset += r.size
    if self.memory_list is not None:
      for r in self.memory_list.ranges:
        if r.start <= address < r.start + r.memory.data_size:
          return r.memory.rva + address - r.start
    return None

  def GetDisasmLines(self, address, size):
    def CountUndefinedInstructions(lines):
      pattern = "<UNDEFINED>"
      return sum([line.count(pattern) for (ignore, line) in lines])

    location = self.FindLocation(address)
    if location is None: return []
    arch = None
    possible_objdump_flags = [""]
    if self.arch == MD_CPU_ARCHITECTURE_X86:
      arch = "ia32"
    elif self.arch == MD_CPU_ARCHITECTURE_ARM:
      arch = "arm"
      possible_objdump_flags = ["", "--disassembler-options=force-thumb"]
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      arch = "arm64"
      possible_objdump_flags = ["", "--disassembler-options=force-thumb"]
    elif self.arch == MD_CPU_ARCHITECTURE_AMD64:
      arch = "x64"
    results = [ disasm.GetDisasmLines(self.minidump_name,
                                     location,
                                     size,
                                     arch,
                                     False,
                                     objdump_flags)
                for objdump_flags in possible_objdump_flags ]
    return min(results, key=CountUndefinedInstructions)


  def Dispose(self):
    self._reset()
    self.minidump.close()
    self.minidump_file.close()

  def ExceptionIP(self):
    if self.arch == MD_CPU_ARCHITECTURE_AMD64:
      return self.exception_context.rip
    elif self.arch == MD_CPU_ARCHITECTURE_ARM:
      return self.exception_context.pc
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      return self.exception_context.pc
    elif self.arch == MD_CPU_ARCHITECTURE_X86:
      return self.exception_context.eip

  def ExceptionSP(self):
    if self.arch == MD_CPU_ARCHITECTURE_AMD64:
      return self.exception_context.rsp
    elif self.arch == MD_CPU_ARCHITECTURE_ARM:
      return self.exception_context.sp
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      return self.exception_context.sp
    elif self.arch == MD_CPU_ARCHITECTURE_X86:
      return self.exception_context.esp

  def ExceptionFP(self):
    if self.arch == MD_CPU_ARCHITECTURE_AMD64:
      return self.exception_context.rbp
    elif self.arch == MD_CPU_ARCHITECTURE_ARM:
      return None
    elif self.arch == MD_CPU_ARCHITECTURE_ARM64:
      return self.exception_context.fp
    elif self.arch == MD_CPU_ARCHITECTURE_X86:
      return self.exception_context.ebp

  def ExceptionThread(self):
    return self.thread_map[self.exception.thread_id]

  def StackTop(self):
    return self.ExceptionSP()

  def StackBottom(self):
    exception_thread = self.ExceptionThread()
    return exception_thread.stack.start + \
        exception_thread.stack.memory.data_size

  def FormatIntPtr(self, value):
    if self.Is64():
      return "%016x" % value
    return "%08x" % value

  def FormatTagged(self, value):
    if self.Is64() and not self.IsPointerCompressed():
      return "%016x" % value
    return "%08x" % value

  def MachinePointerSize(self):
    if self.Is64():
      return 8
    return 4

  def TaggedPointerSize(self):
    if self.IsPointerCompressed():
      return 4
    return self.MachinePointerSize()

  def Register(self, name):
    return self.exception_context.__getattribute__(name)

  def ReadMinidumpString(self, rva):
    string = bytearray(MINIDUMP_STRING.Read(self.minidump, rva).buffer)
    string = string.decode("utf16")
    return string[0:len(string) - 1]

  # Load FUNC records from a BreakPad symbol file
  #
  #    http://code.google.com/p/google-breakpad/wiki/SymbolFiles
  #
  def _LoadSymbolsFrom(self, symfile, baseaddr):
    print("Loading symbols from %s" % (symfile))
    with open(symfile) as f:
      for line in f:
        result = re.match(
            r"^FUNC ([a-f0-9]+) ([a-f0-9]+) ([a-f0-9]+) (.*)$", line)
        if result is not None:
          start = int(result.group(1), 16)
          size = int(result.group(2), 16)
          name = result.group(4).rstrip()
          bisect.insort_left(self.symbols,
                             FuncSymbol(baseaddr + start, size, name))
    print(" ... done")

  def TryLoadSymbolsFor(self, modulename, module):
    try:
      symfile = os.path.join(self.symdir,
                             modulename.replace('.', '_') + ".pdb.sym")
      if os.path.isfile(symfile):
        self._LoadSymbolsFrom(symfile, module.base_of_image)
        self.modules_with_symbols.append(module)
    except Exception as e:
      print("  ... failure (%s)" % (e))

  # Returns true if address is covered by some module that has loaded symbols.
  def _IsInModuleWithSymbols(self, addr):
    for module in self.modules_with_symbols:
      start = module.base_of_image
      end = start + module.size_of_image
      if (start <= addr) and (addr < end):
        return True
    return False

  # Find symbol covering the given address and return its name in format
  #     <symbol name>+<offset from the start>
  def FindSymbol(self, addr):
    if not self._IsInModuleWithSymbols(addr):
      return None

    i = bisect.bisect_left(self.symbols, addr)
    symbol = None
    if (0 < i) and self.symbols[i - 1].Covers(addr):
      symbol = self.symbols[i - 1]
    elif (i < len(self.symbols)) and self.symbols[i].Covers(addr):
      symbol = self.symbols[i]
    else:
      return None
    diff = addr - symbol.start
    return "%s+0x%x" % (symbol.name, diff)


class Printer(object):
  """Printer with indentation support."""

  def __init__(self):
    self.indent = 0

  def Indent(self):
    self.indent += 2

  def Dedent(self):
    self.indent -= 2

  def Print(self, string):
    print("%s%s" % (self._IndentString(), string))

  def PrintLines(self, lines):
    indent = self._IndentString()
    print("\n".join("%s%s" % (indent, line) for line in lines))

  def _IndentString(self):
    return self.indent * " "


ADDRESS_RE = re.compile(r"0x[0-9a-fA-F]+")


def FormatDisasmLine(start, heap, line):
  line_address = start + line[0]
  stack_slot = heap.stack_map.get(line_address)
  marker = "  "
  if stack_slot:
    marker = "=>"
  code = AnnotateAddresses(heap, line[1])

  # Compute the actual call target which the disassembler is too stupid
  # to figure out (it adds the call offset to the disassembly offset rather
  # than the absolute instruction address).
  if heap.reader.arch == MD_CPU_ARCHITECTURE_X86:
    if code.startswith("e8"):
      words = code.split()
      if len(words) > 6 and words[5] == "call":
        offset = int(words[4] + words[3] + words[2] + words[1], 16)
        target = (line_address + offset + 5) & 0xFFFFFFFF
        code = code.replace(words[6], "0x%08x" % target)
  # TODO(jkummerow): port this hack to ARM and x64.

  return "%s%08x %08x: %s" % (marker, line_address, line[0], code)


def AnnotateAddresses(heap, line):
  extra = []
  for m in ADDRESS_RE.finditer(line):
    maybe_address = int(m.group(0), 16)
    object = heap.FindObject(maybe_address)
    if not object: continue
    extra.append(str(object))
  if len(extra) == 0: return line
  return "%s  ;; %s" % (line, ", ".join(extra))


class HeapObject(object):
  def __init__(self, heap, map, address):
    self.heap = heap
    self.map = map
    self.address = address

  def Is(self, cls):
    return isinstance(self, cls)

  def Print(self, p):
    p.Print(str(self))

  def __str__(self):
    instance_type = "???"
    if self.map is not None:
      instance_type = INSTANCE_TYPES[self.map.instance_type]
    return "%s(%s, %s)" % (self.__class__.__name__,
                           self.heap.reader.FormatIntPtr(self.address),
                           instance_type)

  def ObjectField(self, offset):
    field_value = self.heap.reader.ReadTagged(self.address + offset)
    return self.heap.FindObjectOrSmi(field_value)

  def SmiField(self, offset):
    field_value = self.heap.reader.ReadTagged(self.address + offset)
    if self.heap.IsSmi(field_value):
      return self.heap.SmiUntag(field_value)
    return None


class Map(HeapObject):
  def Decode(self, offset, size, value):
    return (value >> offset) & ((1 << size) - 1)

  # Instance Sizes
  def InstanceSizesOffset(self):
    return self.heap.TaggedPointerSize()

  def InstanceSizeOffset(self):
    return self.InstanceSizesOffset()

  def InObjectProperties(self):
    return self.InstanceSizeOffset() + 1

  def UnusedByte(self):
    return self.InObjectProperties() + 1

  def VisitorId(self):
    return self.UnusedByte() + 1

  # Instance Attributes
  def InstanceAttributesOffset(self):
    return self.InstanceSizesOffset() + self.heap.IntSize()

  def InstanceTypeOffset(self):
    return self.InstanceAttributesOffset()

  def BitFieldOffset(self):
    return self.InstanceTypeOffset() + 1

  def BitField2Offset(self):
    return self.BitFieldOffset() + 1

  def UnusedPropertyFieldsOffset(self):
    return self.BitField2Offset() + 1

  # Other fields
  def BitField3Offset(self):
    return self.InstanceAttributesOffset() + self.heap.IntSize()

  def PrototypeOffset(self):
    return self.BitField3Offset() + self.heap.TaggedPointerSize()

  def ConstructorOrBackPointerOffset(self):
    return self.PrototypeOffset() + self.heap.TaggedPointerSize()

  def TransitionsOrPrototypeInfoOffset(self):
    return self.ConstructorOrBackPointerOffset() + self.heap.TaggedPointerSize()

  def DescriptorsOffset(self):
    return (self.TransitionsOrPrototypeInfoOffset() +
            self.heap.TaggedPointerSize())

  def CodeCacheOffset(self):
    return self.DescriptorsOffset() + self.heap.TaggedPointerSize()

  def DependentCodeOffset(self):
    return self.CodeCacheOffset() + self.heap.TaggedPointerSize()

  def ReadByte(self, offset):
    return self.heap.reader.ReadU8(self.address + offset)

  def ReadSlot(self, offset):
    return self.heap.reader.ReadTagged(self.address + offset)

  def Print(self, p):
    p.Print("Map(%08x)" % (self.address))
    p.Print("  - size: %d, inobject: %d, (unused: %d), visitor: %d" % (
        self.ReadByte(self.InstanceSizeOffset()),
        self.ReadByte(self.InObjectProperties()),
        self.ReadByte(self.UnusedByte()),
        self.VisitorId()))

    instance_type = INSTANCE_TYPES[self.ReadByte(self.InstanceTypeOffset())]
    bitfield = self.ReadByte(self.BitFieldOffset())
    bitfield2 = self.ReadByte(self.BitField2Offset())
    unused = self.ReadByte(self.UnusedPropertyFieldsOffset())
    p.Print("  - %s, bf: %d, bf2: %d, unused: %d" % (
        instance_type, bitfield, bitfield2, unused))

    p.Print("  - kind: %s" % (self.Decode(3, 5, bitfield2)))

    bitfield3 = self.ReadSlot(self.BitField3Offset())

    p.Print(
        "  - EnumLength: %d NumberOfOwnDescriptors: %d OwnsDescriptors: %s" % (
            self.Decode(0, 10, bitfield3),
            self.Decode(10, 10, bitfield3),
            self.Decode(21, 1, bitfield3)))
    p.Print("  - DictionaryMap: %s" % (self.Decode(20, 1, bitfield3)))
    p.Print("  - Deprecated: %s" % (self.Decode(23, 1, bitfield3)))
    p.Print("  - IsUnstable: %s" % (self.Decode(24, 1, bitfield3)))
    p.Print("  - NewTargetIsBase: %s" % (self.Decode(27, 1, bitfield3)))

    descriptors = self.ObjectField(self.DescriptorsOffset())
    if descriptors.__class__ == FixedArray:
      DescriptorArray(descriptors).Print(p)
    else:
      p.Print("  - Descriptors: %s" % (descriptors))

    transitions = self.ObjectField(self.TransitionsOrPrototypeInfoOffset())
    if transitions.__class__ == FixedArray:
      TransitionArray(transitions).Print(p)
    else:
      p.Print("  - TransitionsOrPrototypeInfo: %s" % (transitions))

    p.Print("  - Prototype: %s" % self.ObjectField(self.PrototypeOffset()))

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.instance_type = \
        heap.reader.ReadU8(self.address + self.InstanceTypeOffset())


class String(HeapObject):
  def LengthOffset(self):
    # First word after the map is the hash, the second is the length.
    return self.heap.TaggedPointerSize() * 2

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.length = self.SmiField(self.LengthOffset())

  def GetChars(self):
    return "?string?"

  def Print(self, p):
    p.Print(str(self))

  def __str__(self):
    return "\"%s\"" % self.GetChars()


class SeqString(String):
  def CharsOffset(self):
    return self.heap.TaggedPointerSize() * 3

  def __init__(self, heap, map, address):
    String.__init__(self, heap, map, address)
    self.chars = heap.reader.ReadBytes(self.address + self.CharsOffset(),
                                       self.length)

  def GetChars(self):
    return self.chars


class ExternalString(String):
  # TODO(vegorov) fix ExternalString for X64 architecture
  RESOURCE_OFFSET = 12

  WEBKIT_RESOUCE_STRING_IMPL_OFFSET = 4
  WEBKIT_STRING_IMPL_CHARS_OFFSET = 8

  def __init__(self, heap, map, address):
    String.__init__(self, heap, map, address)
    reader = heap.reader
    self.resource = \
        reader.ReadU32(self.address + ExternalString.RESOURCE_OFFSET)
    self.chars = "?external string?"
    if not reader.IsValidAddress(self.resource): return
    string_impl_address = self.resource + \
        ExternalString.WEBKIT_RESOUCE_STRING_IMPL_OFFSET
    if not reader.IsValidAddress(string_impl_address): return
    string_impl = reader.ReadU32(string_impl_address)
    chars_ptr_address = string_impl + \
        ExternalString.WEBKIT_STRING_IMPL_CHARS_OFFSET
    if not reader.IsValidAddress(chars_ptr_address): return
    chars_ptr = reader.ReadU32(chars_ptr_address)
    if not reader.IsValidAddress(chars_ptr): return
    raw_chars = reader.ReadBytes(chars_ptr, 2 * self.length)
    self.chars = codecs.getdecoder("utf16")(raw_chars)[0]

  def GetChars(self):
    return self.chars


class ConsString(String):
  def LeftOffset(self):
    return self.heap.TaggedPointerSize() * 3

  def RightOffset(self):
    return self.heap.TaggedPointerSize() * 4

  def __init__(self, heap, map, address):
    String.__init__(self, heap, map, address)
    self.left = self.ObjectField(self.LeftOffset())
    self.right = self.ObjectField(self.RightOffset())

  def GetChars(self):
    try:
      return self.left.GetChars() + self.right.GetChars()
    except:
      return "***CAUGHT EXCEPTION IN GROKDUMP***"


class Oddball(HeapObject):
  #Should match declarations in objects.h
  KINDS = [
    "False",
    "True",
    "TheHole",
    "Null",
    "ArgumentMarker",
    "Undefined",
    "Other"
  ]

  def ToStringOffset(self):
    return self.heap.TaggedPointerSize()

  def ToNumberOffset(self):
    return self.ToStringOffset() + self.heap.TaggedPointerSize()

  def KindOffset(self):
    return self.ToNumberOffset() + self.heap.TaggedPointerSize()

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.to_string = self.ObjectField(self.ToStringOffset())
    self.kind = self.SmiField(self.KindOffset())

  def Print(self, p):
    p.Print(str(self))

  def __str__(self):
    if self.to_string:
      return "Oddball(%08x, <%s>)" % (self.address, str(self.to_string))
    else:
      kind = "???"
      if 0 <= self.kind < len(Oddball.KINDS):
        kind = Oddball.KINDS[self.kind]
      return "Oddball(%08x, kind=%s)" % (self.address, kind)


class FixedArray(HeapObject):
  def LengthOffset(self):
    return self.heap.TaggedPointerSize()

  def ElementsOffset(self):
    return self.heap.TaggedPointerSize() * 2

  def MemberOffset(self, i):
    return self.ElementsOffset() + self.heap.TaggedPointerSize() * i

  def Get(self, i):
    return self.ObjectField(self.MemberOffset(i))

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.length = self.SmiField(self.LengthOffset())

  def Print(self, p):
    p.Print("FixedArray(%s) {" % self.heap.reader.FormatIntPtr(self.address))
    p.Indent()
    p.Print("length: %d" % self.length)
    base_offset = self.ElementsOffset()
    for i in range(self.length):
      offset = base_offset + 4 * i
      try:
        p.Print("[%08d] = %s" % (i, self.ObjectField(offset)))
      except TypeError:
        p.Dedent()
        p.Print("...")
        p.Print("}")
        return
    p.Dedent()
    p.Print("}")

  def __str__(self):
    return "FixedArray(%08x, length=%d)" % (self.address, self.length)


class DescriptorArray(object):
  def __init__(self, array):
    self.array = array

  def Length(self):
    return self.array.Get(0)

  def Decode(self, offset, size, value):
    return (value >> offset) & ((1 << size) - 1)

  TYPES = [
      "normal",
      "field",
      "function",
      "callbacks"
  ]

  def Type(self, value):
    return DescriptorArray.TYPES[self.Decode(0, 3, value)]

  def Attributes(self, value):
    attributes = self.Decode(3, 3, value)
    result = []
    if (attributes & 0): result += ["ReadOnly"]
    if (attributes & 1): result += ["DontEnum"]
    if (attributes & 2): result += ["DontDelete"]
    return "[" + (",".join(result)) + "]"

  def Deleted(self, value):
    return self.Decode(6, 1, value) == 1

  def FieldIndex(self, value):
    return self.Decode(20, 11, value)

  def Pointer(self, value):
    return self.Decode(6, 11, value)

  def Details(self, di, value):
    return (
        di,
        self.Type(value),
        self.Attributes(value),
        self.FieldIndex(value),
        self.Pointer(value)
    )


  def Print(self, p):
    length = self.Length()
    array = self.array

    p.Print("Descriptors(%08x, length=%d)" % (array.address, length))
    p.Print("[et] %s" % (array.Get(1)))

    for di in range(length):
      i = 2 + di * 3
      p.Print("0x%x" % (array.address + array.MemberOffset(i)))
      p.Print("[%i] name:    %s" % (di, array.Get(i + 0)))
      p.Print("[%i] details: %s %s field-index %i pointer %i" % \
              self.Details(di, array.Get(i + 1)))
      p.Print("[%i] value:   %s" % (di, array.Get(i + 2)))

    end = self.array.length // 3
    if length != end:
      p.Print("[%i-%i] slack descriptors" % (length, end))


class TransitionArray(object):
  def __init__(self, array):
    self.array = array

  def IsSimpleTransition(self):
    return self.array.length <= 2

  def Length(self):
    # SimpleTransition cases
    if self.IsSimpleTransition():
      return self.array.length - 1
    return (self.array.length - 3) // 2

  def Print(self, p):
    length = self.Length()
    array = self.array

    p.Print("Transitions(%08x, length=%d)" % (array.address, length))
    p.Print("[backpointer] %s" % (array.Get(0)))
    if self.IsSimpleTransition():
      if length == 1:
        p.Print("[simple target] %s" % (array.Get(1)))
      return

    elements = array.Get(1)
    if elements is not None:
      p.Print("[elements   ] %s" % (elements))

    prototype = array.Get(2)
    if prototype is not None:
      p.Print("[prototype  ] %s" % (prototype))

    for di in range(length):
      i = 3 + di * 2
      p.Print("[%i] symbol: %s" % (di, array.Get(i + 0)))
      p.Print("[%i] target: %s" % (di, array.Get(i + 1)))


class JSFunction(HeapObject):
  def CodeEntryOffset(self):
    return 3 * self.heap.TaggedPointerSize()

  def SharedOffset(self):
    return 5 * self.heap.TaggedPointerSize()

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    code_entry = \
        heap.reader.ReadU32(self.address + self.CodeEntryOffset())
    self.code = heap.FindObject(code_entry - Code.HeaderSize(heap) + 1)
    self.shared = self.ObjectField(self.SharedOffset())

  def Print(self, p):
    source = "\n".join("  %s" % line for line in self._GetSource().split("\n"))
    p.Print("JSFunction(%s) {" % self.heap.reader.FormatIntPtr(self.address))
    p.Indent()
    p.Print("inferred name: %s" % self.shared.inferred_name)
    if self.shared.script.Is(Script) and self.shared.script.name.Is(String):
      p.Print("script name: %s" % self.shared.script.name)
    p.Print("source:")
    p.Print(source)
    p.Print("code:")
    self.code.Print(p)
    if self.code != self.shared.code:
      p.Print("unoptimized code:")
      self.shared.code.Print(p)
    p.Dedent()
    p.Print("}")

  def __str__(self):
    inferred_name = ""
    if self.shared is not None and self.shared.Is(SharedFunctionInfo):
      inferred_name = self.shared.inferred_name
    return "JSFunction(%s, %s) " % \
          (self.heap.reader.FormatIntPtr(self.address), inferred_name)

  def _GetSource(self):
    source = "?source?"
    start = self.shared.start_position
    end = self.shared.end_position
    if not self.shared.script.Is(Script): return source
    script_source = self.shared.script.source
    if not script_source.Is(String): return source
    if start and end:
      source = script_source.GetChars()[start:end]
    return source


class SharedFunctionInfo(HeapObject):
  def CodeOffset(self):
    return 2 * self.heap.TaggedPointerSize()

  def ScriptOffset(self):
    return 7 * self.heap.TaggedPointerSize()

  def InferredNameOffset(self):
    return 9 * self.heap.TaggedPointerSize()

  def EndPositionOffset(self):
    return 12 * self.heap.TaggedPointerSize() + 4 * self.heap.IntSize()

  def StartPositionAndTypeOffset(self):
    return 12 * self.heap.TaggedPointerSize() + 5 * self.heap.IntSize()

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    try:
      self.code = self.ObjectField(self.CodeOffset())
      self.script = self.ObjectField(self.ScriptOffset())
      self.inferred_name = self.ObjectField(self.InferredNameOffset())
      if heap.TaggedPointerSize() == 8:
        start_position_and_type = \
            heap.reader.ReadU32(self.StartPositionAndTypeOffset())
        self.start_position = start_position_and_type >> 2
        pseudo_smi_end_position = \
            heap.reader.ReadU32(self.EndPositionOffset())
        self.end_position = pseudo_smi_end_position >> 2
      else:
        start_position_and_type = \
            self.SmiField(self.StartPositionAndTypeOffset())
        if start_position_and_type:
          self.start_position = start_position_and_type >> 2
        else:
          self.start_position = None
        self.end_position = \
            self.SmiField(self.EndPositionOffset())
    except:
      print("*** Error while reading SharedFunctionInfo")


class Script(HeapObject):
  def SourceOffset(self):
    return self.heap.TaggedPointerSize()

  def NameOffset(self):
    return self.SourceOffset() + self.heap.TaggedPointerSize()

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.source = self.ObjectField(self.SourceOffset())
    self.name = self.ObjectField(self.NameOffset())


class CodeCache(HeapObject):
  def DefaultCacheOffset(self):
    return self.heap.TaggedPointerSize()

  def NormalTypeCacheOffset(self):
    return self.DefaultCacheOffset() + self.heap.TaggedPointerSize()

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.default_cache = self.ObjectField(self.DefaultCacheOffset())
    self.normal_type_cache = self.ObjectField(self.NormalTypeCacheOffset())

  def Print(self, p):
    p.Print("CodeCache(%s) {" % self.heap.reader.FormatIntPtr(self.address))
    p.Indent()
    p.Print("default cache: %s" % self.default_cache)
    p.Print("normal type cache: %s" % self.normal_type_cache)
    p.Dedent()
    p.Print("}")


class Code(HeapObject):
  CODE_ALIGNMENT_MASK = (1 << 5) - 1

  def InstructionSizeOffset(self):
    return self.heap.TaggedPointerSize()

  @staticmethod
  def HeaderSize(heap):
    return (heap.TaggedPointerSize() + heap.IntSize() + \
        4 * heap.TaggedPointerSize() + 3 * heap.IntSize() + \
        Code.CODE_ALIGNMENT_MASK) & ~Code.CODE_ALIGNMENT_MASK

  def __init__(self, heap, map, address):
    HeapObject.__init__(self, heap, map, address)
    self.entry = self.address + Code.HeaderSize(heap)
    self.instruction_size = \
        heap.reader.ReadU32(self.address + self.InstructionSizeOffset())

  def Print(self, p):
    lines = self.heap.reader.GetDisasmLines(self.entry, self.instruction_size)
    p.Print("Code(%s) {" % self.heap.reader.FormatIntPtr(self.address))
    p.Indent()
    p.Print("instruction_size: %d" % self.instruction_size)
    p.PrintLines(self._FormatLine(line) for line in lines)
    p.Dedent()
    p.Print("}")

  def _FormatLine(self, line):
    return FormatDisasmLine(self.entry, self.heap, line)


class V8Heap(object):
  CLASS_MAP = {
    "SYMBOL_TYPE": SeqString,
    "ONE_BYTE_SYMBOL_TYPE": SeqString,
    "CONS_SYMBOL_TYPE": ConsString,
    "CONS_ONE_BYTE_SYMBOL_TYPE": ConsString,
    "EXTERNAL_SYMBOL_TYPE": ExternalString,
    "EXTERNAL_SYMBOL_WITH_ONE_BYTE_DATA_TYPE": ExternalString,
    "EXTERNAL_ONE_BYTE_SYMBOL_TYPE": ExternalString,
    "UNCACHED_EXTERNAL_SYMBOL_TYPE": ExternalString,
    "UNCACHED_EXTERNAL_SYMBOL_WITH_ONE_BYTE_DATA_TYPE": ExternalString,
    "UNCACHED_EXTERNAL_ONE_BYTE_SYMBOL_TYPE": ExternalString,
    "STRING_TYPE": SeqString,
    "ONE_BYTE_STRING_TYPE": SeqString,
    "CONS_STRING_TYPE": ConsString,
    "CONS_ONE_BYTE_STRING_TYPE": ConsString,
    "EXTERNAL_STRING_TYPE": ExternalString,
    "EXTERNAL_STRING_WITH_ONE_BYTE_DATA_TYPE": ExternalString,
    "EXTERNAL_ONE_BYTE_STRING_TYPE": ExternalString,
    "MAP_TYPE": Map,
    "ODDBALL_TYPE": Oddball,
    "FIXED_ARRAY_TYPE": FixedArray,
    "HASH_TABLE_TYPE": FixedArray,
    "OBJECT_BOILERPLATE_DESCRIPTION_TYPE": FixedArray,
    "SCOPE_INFO_TYPE": FixedArray,
    "JS_FUNCTION_TYPE": JSFunction,
    "SHARED_FUNCTION_INFO_TYPE": SharedFunctionInfo,
    "SCRIPT_TYPE": Script,
    "CODE_CACHE_TYPE": CodeCache,
    "CODE_TYPE": Code,
  }

  def __init__(self, reader, stack_map):
    self.reader = reader
    self.stack_map = stack_map
    self.objects = {}

  def FindObjectOrSmi(self, tagged_address):
    if self.IsSmi(tagged_address): return self.SmiUntag(tagged_address)
    return self.FindObject(tagged_address)

  def FindObject(self, tagged_address):
    if tagged_address in self.objects:
      return self.objects[tagged_address]
    if not self.IsTaggedObjectAddress(tagged_address): return None
    address = tagged_address - 1
    if not self.reader.IsValidAddress(address): return None
    map_tagged_address = self.reader.ReadTagged(address)
    if tagged_address == map_tagged_address:
      # Meta map?
      meta_map = Map(self, None, address)
      instance_type_name = INSTANCE_TYPES.get(meta_map.instance_type)
      if instance_type_name != "MAP_TYPE": return None
      meta_map.map = meta_map
      object = meta_map
    else:
      map = self.FindMap(map_tagged_address)
      if map is None: return None
      instance_type_name = INSTANCE_TYPES.get(map.instance_type)
      if instance_type_name is None: return None
      cls = V8Heap.CLASS_MAP.get(instance_type_name, HeapObject)
      object = cls(self, map, address)
    self.objects[tagged_address] = object
    return object

  def FindMap(self, tagged_address):
    address = self.FindMapAddress(tagged_address)
    if not address: return None
    object = Map(self, None, address)
    return object

  def FindMapAddress(self, tagged_address):
    if not self.IsTaggedMapAddress(tagged_address): return None
    address = tagged_address - 1
    if not self.reader.IsValidAddress(address): return None
    return address

  def IntSize(self):
    return 4

  def MachinePointerSize(self):
    return self.reader.MachinePointerSize()

  def TaggedPointerSize(self):
    return self.reader.TaggedPointerSize()

  def IsPointerCompressed(self):
    return self.reader.IsPointerCompressed()

  def ObjectAlignmentMask(self):
    return self.TaggedPointerSize() - 1

  def IsTaggedObjectAddress(self, address):
    return (address & self.ObjectAlignmentMask()) == 1

  def IsValidTaggedObjectAddress(self, address):
    if not self.IsTaggedObjectAddress(address): return False
    return self.reader.IsValidAddress(address)

  def IsTaggedMapAddress(self, address):
    return (address & self.MapAlignmentMask()) == 1

  def MapAlignmentMask(self):
    if self.reader.arch == MD_CPU_ARCHITECTURE_AMD64:
      return (1 << 4) - 1
    elif self.reader.arch == MD_CPU_ARCHITECTURE_ARM:
      return (1 << 4) - 1
    elif self.reader.arch == MD_CPU_ARCHITECTURE_ARM64:
      return (1 << 4) - 1
    elif self.reader.arch == MD_CPU_ARCHITECTURE_X86:
      return (1 << 5) - 1

  def PageAlignmentMask(self):
    return (1 << 19) - 1

  def IsTaggedAddress(self, address):
    return (address & self.ObjectAlignmentMask()) == 1

  def IsSmi(self, tagged_address):
    if self.reader.Is64() and not self.reader.IsPointerCompressed():
      return (tagged_address & 0xFFFFFFFF) == 0
    return not self.IsTaggedAddress(tagged_address)

  def SmiUntag(self, tagged_address):
    if self.reader.Is64() and not self.reader.IsPointerCompressed():
      return tagged_address >> 32
    return (tagged_address >> 1) & 0xFFFFFFFF

  def AddressTypeMarker(self, address):
    if not self.reader.IsValidAddress(address): return " "
    if self.reader.IsExceptionStackAddress(address): return "S"
    if self.reader.IsModuleAddress(address): return "C"
    if self.IsTaggedAddress(address):
      # Cannot have an tagged pointer into the stack
      if self.reader.IsAnyExceptionStackAddress(address): return "s"
      return "T"
    return "*"

  def FormatIntPtr(self, address):
    marker = self.AddressTypeMarker(address)
    address = self.reader.FormatIntPtr(address)
    if marker == " ": return address
    return "%s %s" % (address, marker)

  def RelativeOffset(self, slot, address):
    if not self.reader.IsValidAlignedAddress(slot): return None
    if self.IsTaggedObjectAddress(address):
      address -= 1
    if not self.reader.IsValidAlignedAddress(address): return None
    offset = (address - slot) / self.MachinePointerSize()

    lower_limit = -32
    upper_limit = 128
    if self.reader.IsExceptionStackAddress(address):
      upper_limit = 0xFFFFFF

    if offset < lower_limit or upper_limit < offset: return None
    target_address = self.reader.ReadUIntPtr(address)
    return "[%+02d]=%s %s" % (offset, self.reader.FormatIntPtr(target_address),
                             self.AddressTypeMarker(target_address))

  def FindObjectPointers(self, start=0, end=0):
    objects = set()
    def find_object_in_region(reader, start, size, location):
      for slot in range(start, start + size, self.reader.TaggedPointerSize()):
        if not self.reader.IsValidAddress(slot): break
        # Collect only tagged pointers (object) to tagged pointers (map)
        tagged_address = self.reader.ReadTagged(slot)
        if not self.IsValidTaggedObjectAddress(tagged_address): continue
        map_address = self.reader.ReadTagged(tagged_address - 1)
        if not self.IsTaggedMapAddress(map_address): continue
        objects.add(tagged_address)

    if not start and not end:
      self.reader.ForEachMemoryRegion(find_object_in_region)
    else:
      find_object_in_region(self.reader, start, end-start, None)

    return objects

class KnownObject(HeapObject):
  def __init__(self, heap, known_name):
    HeapObject.__init__(self, heap, None, None)
    self.known_name = known_name

  def __str__(self):
    return "<%s>" % self.known_name


class KnownMap(HeapObject):
  def __init__(self, heap, known_name, instance_type):
    HeapObject.__init__(self, heap, None, None)
    self.instance_type = instance_type
    self.known_name = known_name

  def __str__(self):
    return "<%s>" % self.known_name


COMMENT_RE = re.compile(r"^C (0x[0-9a-fA-F]+) (.*)$")
PAGEADDRESS_RE = re.compile(
    r"^P (mappage|oldpage) (0x[0-9a-fA-F]+)$")


class InspectionInfo(object):
  def __init__(self, minidump_name, reader):
    self.comment_file = minidump_name + ".comments"
    self.address_comments = {}
    self.page_address = {}
    if os.path.exists(self.comment_file):
      with open(self.comment_file, "r") as f:
        lines = f.readlines()
        f.close()

        for l in lines:
          m = COMMENT_RE.match(l)
          if m:
            self.address_comments[int(m.group(1), 0)] = m.group(2)
          m = PAGEADDRESS_RE.match(l)
          if m:
            self.page_address[m.group(1)] = int(m.group(2), 0)
    self.reader = reader
    self.styles = {}
    self.color_addresses()
    return

  def get_page_address(self, page_kind):
    return self.page_address.get(page_kind, 0)

  def save_page_address(self, page_kind, address):
    with open(self.comment_file, "a") as f:
      f.write("P %s 0x%x\n" % (page_kind, address))
      f.close()

  def color_addresses(self):
    # Color all stack addresses.
    exception_thread = self.reader.thread_map[self.reader.exception.thread_id]
    stack_top = self.reader.ExceptionSP()
    stack_bottom = exception_thread.stack.start + \
        exception_thread.stack.memory.data_size
    frame_pointer = self.reader.ExceptionFP()
    self.styles[frame_pointer] = "frame"
    for slot in range(stack_top, stack_bottom,
                      self.reader.MachinePointerSize()):
      # stack address
      self.styles[slot] = "sa"
    for slot in range(stack_top, stack_bottom,
                      self.reader.MachinePointerSize()):
      maybe_address = self.reader.ReadUIntPtr(slot)
      # stack value
      self.styles[maybe_address] = "sv"
      if slot == frame_pointer:
        self.styles[slot] = "frame"
        frame_pointer = maybe_address
    self.styles[self.reader.ExceptionIP()] = "pc"

  def get_style_class(self, address):
    return self.styles.get(address, None)

  def get_style_class_string(self, address):
    style = self.get_style_class(address)
    if style is not None:
      return " class=%s " % style
    else:
      return ""

  def set_comment(self, address, comment):
    self.address_comments[address] = comment
    with open(self.comment_file, "a") as f:
      f.write("C 0x%x %s\n" % (address, comment))
      f.close()

  def get_comment(self, address):
    return self.address_comments.get(address, "")


class InspectionPadawan(object):
  """The padawan can improve annotations by sensing well-known objects."""
  def __init__(self, reader, heap):
    self.reader = reader
    self.heap = heap
    self.known_first_map_page = 0
    self.known_first_old_page = 0
    self.context = None

  def __getattr__(self, name):
    """An InspectionPadawan can be used instead of V8Heap, even though
       it does not inherit from V8Heap (aka. mixin)."""
    return getattr(self.heap, name)

  def GetPageAddress(self, tagged_address):
    return tagged_address & ~self.heap.PageAlignmentMask()

  def GetPageOffset(self, tagged_address):
    return tagged_address & self.heap.PageAlignmentMask()

  def IsInKnownMapSpace(self, tagged_address):
    page_address = tagged_address & ~self.heap.PageAlignmentMask()
    return page_address == self.known_first_map_page

  def IsInKnownOldSpace(self, tagged_address):
    page_address = tagged_address & ~self.heap.PageAlignmentMask()
    return page_address == self.known_first_old_page

  def ContainingKnownOldSpaceName(self, tagged_address):
    page_address = tagged_address & ~self.heap.PageAlignmentMask()
    if page_address == self.known_first_old_page: return "OLD_SPACE"
    return None

  def FrameMarkerName(self, value):
    # The frame marker is Smi-tagged but not Smi encoded and 0 is not a valid
    # frame type.
    value = (value >> 1) - 1
    if 0 <= value < len(FRAME_MARKERS):
      return "Possibly %s frame marker" % FRAME_MARKERS[value]
    return None

  def IsFrameMarker(self, slot, address):
    if not slot: return False
    # Frame markers only occur directly after a frame pointer and only on the
    # stack.
    if not self.reader.IsExceptionStackAddress(slot): return False
    next_slot = slot + self.reader.MachinePointerSize()
    if not self.reader.IsValidAddress(next_slot): return False
    next_address = self.reader.ReadUIntPtr(next_slot)
    return self.reader.IsExceptionStackAddress(next_address)

  def FormatSmi(self, address):
    value = self.heap.SmiUntag(address)
    # On 32-bit systems almost everything looks like a Smi.
    if not self.reader.Is64() or value == 0: return None
    return "Tagged<Smi>(%d)" % value

  def SenseObject(self, address, slot=None):
    if self.IsFrameMarker(slot, address):
      return self.FrameMarkerName(address)
    if self.heap.IsSmi(address):
      return self.FormatSmi(address)
    if not self.heap.IsTaggedAddress(address): return None
    tagged_address = address
    if self.heap.IsPointerCompressed():
      # Interpret the first page as the read-only space in pointer-comrpession.
      page = self.GetPageAddress(tagged_address)
      tagged_page = page & 0xFFFFFFFF
      if tagged_page == 0:
        offset = self.GetPageOffset(tagged_address)
        lookup_key = ("read_only_space", offset)
        known_obj_name = KNOWN_OBJECTS.get(lookup_key)
        if known_obj_name:
          return KnownObject(self, known_obj_name)

    if self.IsInKnownOldSpace(tagged_address):
      offset = self.GetPageOffset(tagged_address)
      lookup_key = (self.ContainingKnownOldSpaceName(tagged_address), offset)
      known_obj_name = KNOWN_OBJECTS.get(lookup_key)
      if known_obj_name:
        return KnownObject(self, known_obj_name)
    if self.IsInKnownMapSpace(tagged_address):
      known_map = self.SenseMap(tagged_address)
      if known_map:
        return known_map
    found_obj = self.heap.FindObject(tagged_address)
    if found_obj: return found_obj
    address = tagged_address - 1
    if self.reader.IsValidAddress(address):
      map_tagged_address = self.reader.ReadTagged(address)
      map = self.SenseMap(map_tagged_address)
      if map is None: return None
      instance_type_name = INSTANCE_TYPES.get(map.instance_type)
      if instance_type_name is None: return None
      cls = V8Heap.CLASS_MAP.get(instance_type_name, HeapObject)
      return cls(self, map, address)
    return None

  def SenseMap(self, tagged_address):
    if self.IsInKnownMapSpace(tagged_address):
      offset = self.GetPageOffset(tagged_address)
      lookup_key = ("MAP_SPACE", offset)
      known_map_info = KNOWN_MAPS.get(lookup_key)
      if known_map_info:
        known_map_type, known_map_name = known_map_info
        return KnownMap(self, known_map_name, known_map_type)
    found_map = self.heap.FindMap(tagged_address)
    if found_map: return found_map
    return None

  def FindObjectOrSmi(self, tagged_address):
    """When used as a mixin in place of V8Heap."""
    found_obj = self.SenseObject(tagged_address)
    if found_obj: return found_obj
    if self.IsSmi(tagged_address):
      return self.FormatSmi(tagged_address)
    else:
      return "Unknown(%s)" % self.reader.FormatIntPtr(tagged_address)

  def FindObject(self, tagged_address):
    """When used as a mixin in place of V8Heap."""
    raise NotImplementedError

  def FindMap(self, tagged_address):
    """When used as a mixin in place of V8Heap."""
    raise NotImplementedError

  def PrintKnowledge(self):
    print("  known_first_map_page = %s\n"\
          "  known_first_old_page = %s" % (
          self.reader.FormatIntPtr(self.known_first_map_page),
          self.reader.FormatIntPtr(self.known_first_old_page)))

  def FindFirstAsciiString(self, start, end=None, min_length=32):
    """ Walk the memory until we find a large string """
    if not end: end = start + 64
    for slot in range(start, end):
      if not self.reader.IsValidAddress(slot): break
      message = self.reader.ReadAsciiString(slot)
      if len(message) > min_length:
        return (slot, message)
    return (None,None)

  def PrintStackTraceMessage(self, start=None, print_message=True):
    """
    Try to print a possible message from PushStackTraceAndDie.
    Returns the first address where the normal stack starts again.
    """
    # Only look at the first 1k words on the stack
    ptr_size = self.reader.MachinePointerSize()
    if start is None: start = self.reader.ExceptionSP()
    if not self.reader.IsValidAddress(start): return start
    end = start + ptr_size * 1024 * 4
    magic1 = None
    for slot in range(start, end, ptr_size):
      if not self.reader.IsValidAddress(slot + ptr_size): break
      magic1 = self.reader.ReadUIntPtr(slot)
      magic2 = self.reader.ReadUIntPtr(slot + ptr_size)
      pair = (magic1 & 0xFFFFFFFF, magic2 & 0xFFFFFFFF)
      if pair in MAGIC_MARKER_PAIRS:
        return self.TryExtractOldStyleStackTrace(slot, start, end,
                                                 print_message)
      if pair[0] == STACK_TRACE_MARKER:
        return self.TryExtractStackTrace(slot, start, end, print_message)
      elif pair[0] == ERROR_MESSAGE_MARKER:
        return self.TryExtractErrorMessage(slot, start, end, print_message)
    # Simple fallback in case not stack trace object was found
    return self.TryExtractOldStyleStackTrace(0, start, end,
                                             print_message)

  def TryExtractStackTrace(self, slot, start, end, print_message):
    ptr_size = self.reader.MachinePointerSize()
    assert self.reader.ReadUIntPtr(slot) & 0xFFFFFFFF == STACK_TRACE_MARKER
    end_marker = STACK_TRACE_MARKER + 1;
    header_size = 10
    # Look for the end marker after the fields and the message buffer.
    end_search = start + (32 * 1024) + (header_size * ptr_size);
    end_slot = self.reader.FindPtr(end_marker, end_search,
                                   end_search + ptr_size * 512)
    if not end_slot: return start
    print("Stack Message (start=%s):" % self.heap.FormatIntPtr(slot))
    slot += ptr_size
    for name in ("isolate","ptr1", "ptr2", "ptr3", "ptr4", "codeObject1",
                 "codeObject2", "codeObject3", "codeObject4"):
      value = self.reader.ReadUIntPtr(slot)
      print(" %s: %s" % (name.rjust(14), self.heap.FormatIntPtr(value)))
      slot += ptr_size
    print("  message start: %s" % self.heap.FormatIntPtr(slot))
    stack_start = end_slot + ptr_size
    print("  stack_start:   %s" % self.heap.FormatIntPtr(stack_start))
    (message_start, message) = self.FindFirstAsciiString(slot)
    self.FormatStackTrace(message, print_message)
    return stack_start

  def TryExtractErrorMessage(self, slot, start, end, print_message):
    ptr_size = self.reader.MachinePointerSize()
    end_marker = ERROR_MESSAGE_MARKER + 1;
    header_size = 1
    end_search = start + 1024 + (header_size * ptr_size);
    end_slot = self.reader.FindPtr(end_marker, end_search,
                                   end_search + ptr_size * 512)
    if not end_slot: return start
    print("Error Message (start=%s):" % self.heap.FormatIntPtr(slot))
    slot += ptr_size
    (message_start, message) = self.FindFirstAsciiString(slot)
    self.FormatStackTrace(message, print_message)
    stack_start = end_slot + ptr_size
    return stack_start

  def TryExtractOldStyleStackTrace(self, message_slot, start, end,
                                   print_message):
    ptr_size = self.reader.MachinePointerSize()
    if message_slot == 0:
      """
      On Mac we don't always get proper magic markers, so just try printing
      the first long ascii string found on the stack.
      """
      magic1 = None
      magic2 = None
      message_start, message = self.FindFirstAsciiString(start, end, 128)
      if message_start is None: return start
    else:
      message_start = self.reader.ReadUIntPtr(message_slot + ptr_size * 4)
      message = self.reader.ReadAsciiString(message_start)
    stack_start = message_start + len(message) + 1
    # Make sure the address is word aligned
    stack_start =  stack_start - (stack_start % ptr_size)
    if magic1 is None:
      print("Stack Message:")
      print("  message start: %s" % self.heap.FormatIntPtr(message_start))
      print("  stack_start:   %s" % self.heap.FormatIntPtr(stack_start ))
    else:
      ptr1 = self.reader.ReadUIntPtr(slot + ptr_size * 2)
      ptr2 = self.reader.ReadUIntPtr(slot + ptr_size * 3)
      print("Stack Message:")
      print("  magic1:        %s" % self.heap.FormatIntPtr(magic1))
      print("  magic2:        %s" % self.heap.FormatIntPtr(magic2))
      print("  ptr1:          %s" % self.heap.FormatIntPtr(ptr1))
      print("  ptr2:          %s" % self.heap.FormatIntPtr(ptr2))
      print("  message start: %s" % self.heap.FormatIntPtr(message_start))
      print("  stack_start:   %s" % self.heap.FormatIntPtr(stack_start ))
      print("")
    self.FormatStackTrace(message, print_message)
    return stack_start

  def FormatStackTrace(self, message, print_message):
    if not print_message:
      print("  Use `dsa` to print the message with annotated addresses.")
      print("")
      return
    ptr_size = self.reader.MachinePointerSize()
    # Annotate all addresses in the dumped message
    prog = re.compile("[0-9a-fA-F]{%s}" % ptr_size*2)
    addresses = list(set(prog.findall(message)))
    for i in range(len(addresses)):
      address_org = addresses[i]
      address = self.heap.FormatIntPtr(int(address_org, 16))
      if address_org != address:
        message = message.replace(address_org, address)
    print("Message:")
    print("="*80)
    print(message)
    print("="*80)
    print("")


  def TryInferFramePointer(self, slot, address):
    """ Assume we have a framepointer if we find 4 consecutive links """
    for i in range(0, 4):
      if not self.reader.IsExceptionStackAddress(address): return 0
      next_address = self.reader.ReadUIntPtr(address)
      if next_address == address: return 0
      address = next_address
    return slot

  def TryInferContext(self, address):
    if self.context: return
    ptr_size = self.reader.MachinePointerSize()
    possible_context = dict()
    count = 0
    while self.reader.IsExceptionStackAddress(address):
      prev_addr = self.reader.ReadUIntPtr(address-ptr_size)
      if self.heap.IsTaggedObjectAddress(prev_addr):
        if prev_addr in possible_context:
          possible_context[prev_addr] += 1
        else:
          possible_context[prev_addr] = 1
      address = self.reader.ReadUIntPtr(address)
      count += 1
    if count <= 5 or len(possible_context) == 0: return
    # Find entry with highest count
    possible_context = list(possible_context.items())
    possible_context.sort(key=lambda pair: pair[1])
    address,count = possible_context[-1]
    if count <= 4: return
    self.context = address

  def InterpretMemory(self, start, end):
    # On 64 bit we omit frame pointers, so we have to do some more guesswork.
    frame_pointer = 0
    if not self.reader.Is64():
      frame_pointer = self.reader.ExceptionFP()
      # Follow the framepointer into the address range
      while frame_pointer and frame_pointer < start:
        frame_pointer = self.reader.ReadUIntPtr(frame_pointer)
        if not self.reader.IsExceptionStackAddress(frame_pointer) or \
            not frame_pointer:
          frame_pointer = 0
          break
    in_oom_dump_area  = False
    is_stack = self.reader.IsExceptionStackAddress(start)
    free_space_end = 0
    ptr_size = self.reader.TaggedPointerSize()

    for slot in range(start, end, ptr_size):
      if not self.reader.IsValidAddress(slot):
        print("%s: Address is not contained within the minidump!" % slot)
        return
      maybe_address = self.reader.ReadUIntPtr(slot)
      address_info = []
      # Mark continuous free space objects
      if slot == free_space_end:
        address_info.append("+")
      elif slot <= free_space_end:
        address_info.append("|")
      else:
        free_space_end = 0

      heap_object = self.SenseObject(maybe_address, slot)
      if heap_object:
        # Detect Free-space ranges
        if isinstance(heap_object, KnownMap) and \
            heap_object.known_name == "FreeSpaceMap":
          # The free-space length is is stored as a Smi in the next slot.
          length = self.reader.ReadTagged(slot + ptr_size)
          if self.heap.IsSmi(length):
            length = self.heap.SmiUntag(length)
            free_space_end = slot + length - ptr_size
        address_info.append(str(heap_object))
      relative_offset = self.heap.RelativeOffset(slot, maybe_address)
      if relative_offset:
        address_info.append(relative_offset)
      if maybe_address == self.context:
        address_info.append("CONTEXT")

      maybe_address_contents = None
      if is_stack:
        if self.reader.IsExceptionStackAddress(maybe_address):
          maybe_address_contents = \
              self.reader.ReadUIntPtr(maybe_address) & 0xFFFFFFFF
          if maybe_address_contents == 0xdecade00:
            in_oom_dump_area = True
          if frame_pointer == 0:
            frame_pointer = self.TryInferFramePointer(slot, maybe_address)
            if frame_pointer != 0:
              self.TryInferContext(slot)
        maybe_symbol = self.reader.FindSymbol(maybe_address)
        if in_oom_dump_area:
          if maybe_address_contents == 0xdecade00:
            address_info = ["<==== HeapStats start marker"]
          elif maybe_address_contents == 0xdecade01:
            address_info = ["<==== HeapStats end marker"]
          elif maybe_address_contents is not None:
            address_info = [" %d (%d Mbytes)" % (maybe_address_contents,
                                                 maybe_address_contents >> 20)]
        if slot == frame_pointer:
          if not self.reader.IsExceptionStackAddress(maybe_address):
            address_info.append("<==== BAD frame pointer")
            frame_pointer = 0
          else:
            address_info.append("<==== Frame pointer")
          frame_pointer = maybe_address
      address_type_marker = self.heap.AddressTypeMarker(maybe_address)
      string_value = self.reader.ReadAsciiPtr(slot)
      print("%s: %s %s %s %s" % (self.reader.FormatIntPtr(slot),
                           self.reader.FormatIntPtr(maybe_address),
                           address_type_marker,
                           string_value,
                           ' | '.join(address_info)))
      if maybe_address_contents == 0xdecade01:
        in_oom_dump_area = False
      heap_object = self.heap.FindObject(maybe_address)
      if heap_object:
        heap_object.Print(Printer())
        print("")

WEB_HEADER = """
<!DOCTYPE html>
<html>
<head>
<meta content="text/html; charset=utf-8" http-equiv="content-type">
<style media="screen" type="text/css">

.code {
  font-family: monospace;
}

.dmptable {
  border-collapse : collapse;
  border-spacing : 0px;
  table-layout: fixed;
}

.codedump {
  border-collapse : collapse;
  border-spacing : 0px;
  table-layout: fixed;
}

.addrcomments {
  border : 0px;
}

.register {
  padding-right : 1em;
}

.header {
  clear : both;
}

.header .navigation {
  float : left;
}

.header .dumpname {
  float : right;
}

tr.highlight-line {
  background-color : yellow;
}

.highlight {
  background-color : magenta;
}

tr.inexact-highlight-line {
  background-color : pink;
}

input {
  background-color: inherit;
  border: 1px solid LightGray;
}

.dumpcomments {
  border : 1px solid LightGray;
  width : 32em;
}

.regions td {
  padding:0 15px 0 15px;
}

.stackframe td {
  background-color : cyan;
}

.stackaddress, .sa {
  background-color : LightGray;
}

.stackval, .sv {
  background-color : LightCyan;
}

.frame {
  background-color : cyan;
}

.commentinput, .ci {
  width : 20em;
}

/* a.nodump */
a.nd:visited {
  color : black;
  text-decoration : none;
}

a.nd:link {
  color : black;
  text-decoration : none;
}

a:visited {
  color : blueviolet;
}

a:link {
  color : blue;
}

.disasmcomment {
  color : DarkGreen;
}

</style>

<script type="application/javascript">

var address_str = "address-";
var address_len = address_str.length;

function comment() {
  var s = event.srcElement.id;
  var index = s.indexOf(address_str);
  if (index >= 0) {
    send_comment(s.substring(index + address_len), event.srcElement.value);
  }
}
var c = comment;

function send_comment(address, comment) {
  xmlhttp = new XMLHttpRequest();
  address = encodeURIComponent(address)
  comment = encodeURIComponent(comment)
  xmlhttp.open("GET",
      "setcomment?%(query_dump)s&address=" + address +
      "&comment=" + comment, true);
  xmlhttp.send();
}

var dump_str = "dump-";
var dump_len = dump_str.length;

function dump_comment() {
  var s = event.srcElement.id;
  var index = s.indexOf(dump_str);
  if (index >= 0) {
    send_dump_desc(s.substring(index + dump_len), event.srcElement.value);
  }
}

function send_dump_desc(name, desc) {
  xmlhttp = new XMLHttpRequest();
  name = encodeURIComponent(name)
  desc = encodeURIComponent(desc)
  xmlhttp.open("GET",
      "setdumpdesc?dump=" + name +
      "&description=" + desc, true);
  xmlhttp.send();
}

function onpage(kind, address) {
  xmlhttp = new XMLHttpRequest();
  kind = encodeURIComponent(kind)
  address = encodeURIComponent(address)
  xmlhttp.onreadystatechange = function() {
    if (xmlhttp.readyState==4 && xmlhttp.status==200) {
      location.reload(true)
    }
  };
  xmlhttp.open("GET",
      "setpageaddress?%(query_dump)s&kind=" + kind +
      "&address=" + address);
  xmlhttp.send();
}

</script>

<title>Dump %(dump_name)s</title>
</head>

<body>
  <div class="header">
    <form class="navigation" action="search.html">
      <a href="summary.html?%(query_dump)s">Context info</a>&nbsp;&nbsp;&nbsp;
      <a href="info.html?%(query_dump)s">Dump info</a>&nbsp;&nbsp;&nbsp;
      <a href="modules.html?%(query_dump)s">Modules</a>&nbsp;&nbsp;&nbsp;
      &nbsp;
      <input type="search" name="val">
      <input type="submit" name="search" value="Search">
      <input type="hidden" name="dump" value="%(dump_name)s">
    </form>
    <form class="navigation" action="disasm.html#highlight">
      &nbsp;
      &nbsp;
      &nbsp;
      <input type="search" name="val">
      <input type="submit" name="disasm" value="Disasm">
      &nbsp;
      &nbsp;
      &nbsp;
      <a href="dumps.html">Dumps...</a>
    </form>
  </div>
  <br>
  <hr>
"""


WEB_FOOTER = """
</body>
</html>
"""


class WebParameterError(Exception):
  pass


class InspectionWebHandler(http_server.BaseHTTPRequestHandler):

  def formatter(self, query_components):
    name = query_components.get("dump", [None])[0]
    return self.server.get_dump_formatter(name)

  def send_success_html_headers(self):
    self.send_response(200)
    self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
    self.send_header("Pragma", "no-cache")
    self.send_header("Expires", "0")
    self.send_header('Content-type','text/html')
    self.end_headers()
    return

  def write(self, string):
    self.wfile.write(string.encode('utf-8'))

  def do_GET(self):
    try:
      parsedurl = urllib.parse.urlparse(self.path)
      query_components = urllib.parse.parse_qs(parsedurl.query)
      out_buffer = io.StringIO()
      if parsedurl.path == "/dumps.html":
        self.send_success_html_headers()
        self.server.output_dumps(out_buffer)
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/summary.html":
        self.send_success_html_headers()
        self.formatter(query_components).output_summary(out_buffer)
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/info.html":
        self.send_success_html_headers()
        self.formatter(query_components).output_info(out_buffer)
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/modules.html":
        self.send_success_html_headers()
        self.formatter(query_components).output_modules(out_buffer)
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/search.html" or parsedurl.path == "/s":
        address = query_components.get("val", [])
        if len(address) != 1:
          self.send_error(404, "Invalid params")
          return
        self.send_success_html_headers()
        self.formatter(query_components).output_search_res(
            out_buffer, address[0])
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/disasm.html":
        address = query_components.get("val", [])
        exact = query_components.get("exact", ["on"])
        if len(address) != 1:
          self.send_error(404, "Invalid params")
          return
        self.send_success_html_headers()
        self.formatter(query_components).output_disasm(
            out_buffer, address[0], exact[0])
        self.write(out_buffer.getvalue())
      elif parsedurl.path == "/data.html":
        address = query_components.get("val", [])
        datakind = query_components.get("type", ["address"])
        if len(address) == 1 and len(datakind) == 1:
          self.send_success_html_headers()
          self.formatter(query_components).output_data(
              out_buffer, address[0], datakind[0])
          self.write(out_buffer.getvalue())
        else:
          self.send_error(404,'Invalid params')
      elif parsedurl.path == "/setdumpdesc":
        name = query_components.get("dump", [""])
        description = query_components.get("description", [""])
        if len(name) == 1 and len(description) == 1:
          name = name[0]
          description = description[0]
          if self.server.set_dump_desc(name, description):
            self.send_success_html_headers()
            self.write("OK")
            return
        self.send_error(404,'Invalid params')
      elif parsedurl.path == "/setcomment":
        address = query_components.get("address", [])
        comment = query_components.get("comment", [""])
        if len(address) == 1 and len(comment) == 1:
          address = address[0]
          comment = comment[0]
          self.formatter(query_components).set_comment(address, comment)
          self.send_success_html_headers()
          self.write("OK")
        else:
          self.send_error(404,'Invalid params')
      elif parsedurl.path == "/setpageaddress":
        kind = query_components.get("kind", [])
        address = query_components.get("address", [""])
        if len(kind) == 1 and len(address) == 1:
          kind = kind[0]
          address = address[0]
          self.formatter(query_components).set_page_address(kind, address)
          self.send_success_html_headers()
          self.write("OK")
        else:
          self.send_error(404,'Invalid params')
      else:
        self.send_error(404,'File Not Found: %s' % self.path)

    except IOError:
      self.send_error(404,'File Not Found: %s' % self.path)

    except WebParameterError as e:
      self.send_error(404, 'Web parameter error: %s' % e.message)


HTML_REG_FORMAT = "<span class=\"register\"><b>%s</b>:&nbsp;%s</span><br/>\n"


class InspectionWebFormatter(object):
  CONTEXT_FULL = 0
  CONTEXT_SHORT = 1

  def __init__(self, switches, minidump_name, http_server):
    self.dumpfilename = os.path.split(minidump_name)[1]
    self.encfilename = urllib.parse.urlencode({'dump': self.dumpfilename})
    self.reader = MinidumpReader(switches, minidump_name)
    self.server = http_server

    # Set up the heap
    exception_thread = self.reader.thread_map[self.reader.exception.thread_id]
    stack_top = self.reader.ExceptionSP()
    stack_bottom = exception_thread.stack.start + \
        exception_thread.stack.memory.data_size
    stack_map = {self.reader.ExceptionIP(): -1}
    for slot in range(stack_top, stack_bottom,
                      self.reader.MachinePointerSize()):
      maybe_address = self.reader.ReadUIntPtr(slot)
      if not maybe_address in stack_map:
        stack_map[maybe_address] = slot
    self.heap = V8Heap(self.reader, stack_map)

    self.padawan = InspectionPadawan(self.reader, self.heap)
    self.comments = InspectionInfo(minidump_name, self.reader)
    self.padawan.known_first_old_page = (
        self.comments.get_page_address("oldpage"))
    self.padawan.known_first_map_page = (
        self.comments.get_page_address("mappage"))

  def set_comment(self, straddress, comment):
    try:
      address = int(straddress, 0)
      self.comments.set_comment(address, comment)
    except ValueError:
      print("Invalid address")

  def set_page_address(self, kind, straddress):
    try:
      address = int(straddress, 0)
      if kind == "oldpage":
        self.padawan.known_first_old_page = address
      elif kind == "mappage":
        self.padawan.known_first_map_page = address
      self.comments.save_page_address(kind, address)
    except ValueError:
      print("Invalid address")

  def td_from_address(self, f, address):
    f.write("<td %s>" % self.comments.get_style_class_string(address))

  def format_address(self, maybeaddress, straddress = None):
    if maybeaddress is None:
      return "not in dump"
    else:
      if straddress is None:
        straddress = "0x" + self.reader.FormatIntPtr(maybeaddress)
      style_class = ""
      if not self.reader.IsValidAddress(maybeaddress):
        style_class = "class=nd"
      return ("<a %s href=s?%s&amp;val=%s>%s</a>" %
              (style_class, self.encfilename, straddress, straddress))

  def format_onheap_address(self, size, maybeaddress, uncompressed):
    if maybeaddress is None:
      return "not in dump"
    else:
      straddress = "0x" + self.reader.FormatTagged(maybeaddress)
      struncompressed = "0x" + self.reader.FormatIntPtr(uncompressed)
      style_class = ""
      if not self.reader.IsValidAddress(uncompressed):
        style_class = "class=nd"
      return ("<a %s href=s?%s&amp;val=%s>%s</a>" %
              (style_class, self.encfilename, struncompressed, straddress))

  def output_header(self, f):
    f.write(WEB_HEADER % {
        "query_dump": self.encfilename,
        "dump_name": html.escape(self.dumpfilename)
    })

  def output_footer(self, f):
    f.write(WEB_FOOTER)

  MAX_CONTEXT_STACK = 2048

  def output_summary(self, f):
    self.output_header(f)
    f.write('<div class="code">')
    self.output_context(f, InspectionWebFormatter.CONTEXT_SHORT)
    self.output_disasm_pc(f)

    # Output stack, trying to also output the stack trace if dumped.
    stack_top = self.try_output_stack_trace(f)

    exception_thread = self.reader.thread_map[self.reader.exception.thread_id]
    stack_bottom = min(exception_thread.stack.start + \
        exception_thread.stack.memory.data_size,
        stack_top + self.MAX_CONTEXT_STACK)

    self.output_words(f, stack_top - 16, stack_bottom, stack_top, "Stack",
                      self.heap.MachinePointerSize())

    f.write('</div>')
    self.output_footer(f)
    return

  def try_output_stack_trace(self, f, start=None, print_message=True):
    """
    Try to print a possible message from PushStackTraceAndDie.
    Returns the first address where the normal stack starts again.
    """
    # Only look at the first 1k words on the stack
    ptr_size = self.reader.MachinePointerSize()
    if start is None:
      start = self.reader.ExceptionSP()
    if not self.reader.IsValidAddress(start):
      return start
    end = start + ptr_size * 1024 * 4
    for slot in range(start, end, ptr_size):
      if not self.reader.IsValidAddress(slot + ptr_size):
        break
      magic = self.reader.ReadUIntPtr(slot)
      if magic == STACK_TRACE_MARKER:
        return self.output_stack_trace(f, slot, start, end, print_message)
    return start

  def output_stack_trace(self, f, slot, start, end, print_message):
    ptr_size = self.reader.MachinePointerSize()
    assert self.reader.ReadUIntPtr(slot) & 0xFFFFFFFF == STACK_TRACE_MARKER
    end_marker = STACK_TRACE_MARKER + 1
    header_size = 10
    # Look for the end marker after the fields and the message buffer.
    end_search = start + (32 * 1024) + (header_size * ptr_size)
    end_slot = self.reader.FindPtr(end_marker, end_search,
                                   end_search + ptr_size * 512)
    if not end_slot:
      return start
    f.write("<h3>PushStackTraceAndDie Stack Message (start=%s):</h3>" %
            self.format_address(slot))
    slot += ptr_size
    for name in ("isolate", "ptr1", "ptr2", "ptr3", "ptr4", "codeObject1",
                 "codeObject2", "codeObject3", "codeObject4"):
      value = self.reader.ReadUIntPtr(slot)
      f.write("<b>%s</b>: %s %s<br>" %
              (name.rjust(14), self.format_address(value),
               "" if name == "isolate" else self.format_object(value)))
      slot += ptr_size
    f.write("<b>message start</b>: %s<br>" % self.format_address(slot))
    stack_start = end_slot + ptr_size
    f.write("<b>stack_start</b>:   %s<br>" % self.format_address(stack_start))
    (message_start, message) = self.padawan.FindFirstAsciiString(slot)
    if print_message:
      f.write("<a href='#eom'>Scroll to end of message...</a><br>")
      self.output_stack_trace_message(f, message)
      f.write("<span id=eom></span>")
    return stack_start

  def output_stack_trace_message(self, f, message):
    ptr_size = self.reader.MachinePointerSize()
    # Annotate all addresses in the dumped message
    prog = re.compile("0x[0-9a-fA-F]+")

    message = html.escape(message)
    message = message.replace("\n", "<br>")

    addresses = list(set(prog.findall(message)))
    for i in range(len(addresses)):
      address_org = addresses[i]
      address = int(address_org, 16)
      formatted_address = self.format_address(address, address_org)
      message = message.replace(address_org, formatted_address)

    f.write(message)

  def output_info(self, f):
    self.output_header(f)
    f.write("<h3>Dump info</h3>")
    f.write("Description: ")
    self.server.output_dump_desc_field(f, self.dumpfilename)
    f.write("<br>")
    f.write("Filename: ")
    f.write("<span class=\"code\">%s</span><br>" % (self.dumpfilename))
    dt = datetime.datetime.fromtimestamp(self.reader.header.time_date_stampt)
    f.write("Timestamp: %s<br>" % dt.strftime('%Y-%m-%d %H:%M:%S'))
    self.output_context(f, InspectionWebFormatter.CONTEXT_FULL)
    self.output_address_ranges(f)
    self.output_footer(f)
    return

  def output_address_ranges(self, f):
    regions = {}
    def print_region(_reader, start, size, _location):
      regions[start] = size
    self.reader.ForEachMemoryRegion(print_region)
    f.write("<h3>Available memory regions</h3>")
    f.write('<div class="code">')
    f.write("<table class=\"regions\">")
    f.write("<thead><tr>")
    f.write("<th>Start address</th>")
    f.write("<th>End address</th>")
    f.write("<th>Number of bytes</th>")
    f.write("</tr></thead>")
    for start in sorted(regions):
      size = regions[start]
      f.write("<tr>")
      f.write("<td>%s</td>" % self.format_address(start))
      f.write("<td>&nbsp;%s</td>" % self.format_address(start + size))
      f.write("<td>&nbsp;%d</td>" % size)
      f.write("</tr>")
    f.write("</table>")
    f.write('</div>')
    return

  def output_module_details(self, f, module):
    f.write("<b>%s</b>" % GetModuleName(self.reader, module))
    file_version = GetVersionString(module.version_info.dwFileVersionMS,
                                    module.version_info.dwFileVersionLS)
    product_version = GetVersionString(module.version_info.dwProductVersionMS,
                                       module.version_info.dwProductVersionLS)
    f.write("<br>&nbsp;&nbsp;")
    f.write("base: %s" % self.reader.FormatIntPtr(module.base_of_image))
    f.write("<br>&nbsp;&nbsp;")
    f.write("  end: %s" % self.reader.FormatIntPtr(module.base_of_image +
                                            module.size_of_image))
    f.write("<br>&nbsp;&nbsp;")
    f.write("  file version: %s" % file_version)
    f.write("<br>&nbsp;&nbsp;")
    f.write("  product version: %s" % product_version)
    f.write("<br>&nbsp;&nbsp;")
    time_date_stamp = datetime.datetime.fromtimestamp(module.time_date_stamp)
    f.write("  timestamp: %s" % time_date_stamp)
    f.write("<br>");

  def output_modules(self, f):
    self.output_header(f)
    f.write('<div class="code">')
    for module in self.reader.module_list.modules:
      self.output_module_details(f, module)
    f.write("</div>")
    self.output_footer(f)
    return

  def output_context(self, f, details):
    exception_thread = self.reader.thread_map[self.reader.exception.thread_id]
    f.write("<h3>Exception context</h3>")
    f.write('<div class="code">')
    f.write("Thread id: %d" % exception_thread.id)
    f.write("&nbsp;&nbsp; Exception code: %08X<br/>" %
            self.reader.exception.exception.code)
    if details == InspectionWebFormatter.CONTEXT_FULL:
      if self.reader.exception.exception.parameter_count > 0:
        f.write("&nbsp;&nbsp; Exception parameters: ")
        for i in range(0, self.reader.exception.exception.parameter_count):
          f.write("%08x" % self.reader.exception.exception.information[i])
        f.write("<br><br>")

    for r in CONTEXT_FOR_ARCH[self.reader.arch]:
      f.write(HTML_REG_FORMAT %
              (r, self.format_address(self.reader.Register(r))))
    # TODO(vitalyr): decode eflags.
    if self.reader.arch in [MD_CPU_ARCHITECTURE_ARM, MD_CPU_ARCHITECTURE_ARM64]:
      f.write("<b>cpsr</b>: %s" % bin(self.reader.exception_context.cpsr)[2:])
    else:
      f.write("<b>eflags</b>: %s" %
              bin(self.reader.exception_context.eflags)[2:])
    f.write('</div>')
    return

  def align_down(self, a, size):
    alignment_correction = a % size
    return a - alignment_correction

  def align_up(self, a, size):
    alignment_correction = (size - 1) - ((a + size - 1) % size)
    return a + alignment_correction

  def format_object(self, address):
    heap_object = self.padawan.SenseObject(address)
    return html.escape(str(heap_object or ""))

  def output_data(self, f, straddress, datakind):
    try:
      self.output_header(f)
      address = int(straddress, 0)
      if not self.reader.IsValidAddress(address):
        f.write("<h3>Address 0x%x not found in the dump.</h3>" % address)
        return
      region = self.reader.FindRegion(address)
      if datakind == "address":
        self.output_words(f, region[0], region[0] + region[1], address, "Dump",
                          self.heap.MachinePointerSize())
      if datakind == "tagged":
        self.output_words(f, region[0], region[0] + region[1], address,
                          "Tagged Dump", self.heap.TaggedPointerSize())
      elif datakind == "ascii":
        self.output_ascii(f, region[0], region[0] + region[1], address)
      self.output_footer(f)

    except ValueError:
      f.write("<h3>Unrecognized address format \"%s\".</h3>" % straddress)
    return

  def output_words(self, f, start_address, end_address, highlight_address, desc,
                   size):
    region = self.reader.FindRegion(highlight_address)
    if region is None:
      f.write("<h3>Address 0x%x not found in the dump.</h3>" %
              (highlight_address))
      return
    start_address = self.align_down(start_address, size)
    low = self.align_down(region[0], size)
    high = self.align_up(region[0] + region[1], size)
    if start_address < low:
      start_address = low
    end_address = self.align_up(end_address, size)
    if end_address > high:
      end_address = high

    expand = ""
    if start_address != low or end_address != high:
      additional_query = ""
      if self.heap.IsPointerCompressed(
      ) and size == self.reader.TaggedPointerSize():
        additional_query = "&amp;type=tagged"

      expand = ("(<a href=\"data.html?%s&amp;val=0x%x%s#highlight\">"
                " more..."
                " </a>)" %
                (self.encfilename, highlight_address, additional_query))

    f.write("<h3>%s 0x%x - 0x%x, "
            "highlighting <a href=\"#highlight\">0x%x</a> %s</h3>" %
            (desc, start_address, end_address, highlight_address, expand))
    f.write('<div class="code">')
    f.write("<table class=codedump>")

    for j in range(0, end_address - start_address, size):
      slot = start_address + j
      heap_object = ""
      maybe_address = None
      maybe_uncompressed_address = None
      end_region = region[0] + region[1]
      if slot < region[0] or slot + size > end_region:
        straddress = "0x"
        for i in range(end_region, slot + size):
          straddress += "??"
        for i in reversed(
            range(max(slot, region[0]), min(slot + size, end_region))):
          straddress += "%02x" % self.reader.ReadU8(i)
        for i in range(slot, region[0]):
          straddress += "??"
      else:
        maybe_address = self.reader.ReadSized(slot, size)
        if size == self.reader.MachinePointerSize():
          maybe_uncompressed_address = maybe_address
        else:
          maybe_uncompressed_address = (slot & (0xFFFFFFFF << 32)) | (
              maybe_address & 0xFFFFFFFF)

        if size == self.reader.TaggedPointerSize():
          straddress = self.format_onheap_address(size, maybe_address,
                                                  maybe_uncompressed_address)
          if maybe_address:
            heap_object = self.format_object(maybe_uncompressed_address)
        else:
          straddress = self.format_address(maybe_address)

      address_fmt = "%s&nbsp;</td>"
      if slot == highlight_address:
        f.write("<tr class=highlight-line>")
        address_fmt = "<a id=highlight></a>%s&nbsp;</td>"
      elif slot < highlight_address and highlight_address < slot + size:
        f.write("<tr class=inexact-highlight-line>")
        address_fmt = "<a id=highlight></a>%s&nbsp;</td>"
      else:
        f.write("<tr>")

      f.write("<td>")
      self.output_comment_box(f, "da-", slot)
      f.write("</td>")
      self.td_from_address(f, slot)
      f.write(address_fmt % self.format_address(slot))
      self.td_from_address(f, maybe_uncompressed_address)
      f.write(":&nbsp;%s&nbsp;</td>" % straddress)
      f.write("<td>")
      if maybe_uncompressed_address is not None:
        self.output_comment_box(f, "sv-" + self.reader.FormatIntPtr(slot),
                                maybe_uncompressed_address)
      f.write("</td>")
      f.write("<td>%s</td>" % (heap_object or ''))
      f.write("</tr>")
    f.write("</table>")
    f.write("</div>")
    return

  def output_ascii(self, f, start_address, end_address, highlight_address):
    region = self.reader.FindRegion(highlight_address)
    if region is None:
      f.write("<h3>Address %x not found in the dump.</h3>" %
          highlight_address)
      return
    if start_address < region[0]:
      start_address = region[0]
    if end_address > region[0] + region[1]:
      end_address = region[0] + region[1]

    expand = ""
    if start_address != region[0] or end_address != region[0] + region[1]:
      link = ("data.html?%s&amp;val=0x%x&amp;type=ascii#highlight" %
              (self.encfilename, highlight_address))
      expand = "(<a href=\"%s\">more...</a>)" % link

    f.write("<h3>ASCII dump 0x%x - 0x%x, highlighting 0x%x %s</h3>" %
            (start_address, end_address, highlight_address, expand))

    line_width = 64

    f.write('<div class="code">')

    start = self.align_down(start_address, line_width)

    for i in range(end_address - start):
      address = start + i
      if address % 64 == 0:
        if address != start:
          f.write("<br>")
        f.write("0x%08x:&nbsp;" % address)
      if address < start_address:
        f.write("&nbsp;")
      else:
        if address == highlight_address:
          f.write("<span class=\"highlight\">")
        code = self.reader.ReadU8(address)
        if code < 127 and code >= 32:
          f.write("&#")
          f.write(str(code))
          f.write(";")
        else:
          f.write("&middot;")
        if address == highlight_address:
          f.write("</span>")
    f.write("</div>")
    return

  def output_disasm(self, f, straddress, strexact):
    try:
      self.output_header(f)
      address = int(straddress, 0)
      if not self.reader.IsValidAddress(address):
        f.write("<h3>Address 0x%x not found in the dump.</h3>" % address)
        return
      region = self.reader.FindRegion(address)
      self.output_disasm_range(
          f, region[0], region[0] + region[1], address, strexact == "on")
      self.output_footer(f)
    except ValueError:
      f.write("<h3>Unrecognized address format \"%s\".</h3>" % straddress)
    return

  def output_disasm_range(
      self, f, start_address, end_address, highlight_address, exact):
    region = self.reader.FindRegion(highlight_address)
    if start_address < region[0]:
      start_address = region[0]
    if end_address > region[0] + region[1]:
      end_address = region[0] + region[1]
    count = end_address - start_address
    lines = self.reader.GetDisasmLines(start_address, count)
    found = False
    if exact:
      for line in lines:
        if line[0] + start_address == highlight_address:
          found = True
          break
      if not found:
        start_address = highlight_address
        count = end_address - start_address
        lines = self.reader.GetDisasmLines(highlight_address, count)
    expand = ""
    if start_address != region[0] or end_address != region[0] + region[1]:
      exactness = ""
      if exact and not found and end_address == region[0] + region[1]:
        exactness = "&amp;exact=off"
      expand = ("(<a href=\"disasm.html?%s%s"
                "&amp;val=0x%x#highlight\">more...</a>)" %
                (self.encfilename, exactness, highlight_address))

    f.write("<h3>Disassembling 0x%x - 0x%x, highlighting 0x%x %s</h3>" %
            (start_address, end_address, highlight_address, expand))
    f.write('<div class="code">')
    f.write("<table class=\"codedump\">");
    for i in range(len(lines)):
      line = lines[i]
      next_address = count
      if i + 1 < len(lines):
        next_line = lines[i + 1]
        next_address = next_line[0]
      self.format_disasm_line(
          f, start_address, line, next_address, highlight_address)
    f.write("</table>")
    f.write("</div>")
    return

  def annotate_disasm_addresses(self, line):
    extra = []
    for m in ADDRESS_RE.finditer(line):
      maybe_address = int(m.group(0), 16)
      formatted_address = self.format_address(maybe_address, m.group(0))
      line = line.replace(m.group(0), formatted_address)
      object_info = self.padawan.SenseObject(maybe_address)
      if not object_info:
        continue
        extra.append(html.escape(str(object_info)))
    if len(extra) == 0:
      return line
    return ("%s <span class=disasmcomment>;; %s</span>" %
            (line, ", ".join(extra)))

  def format_disasm_line(
      self, f, start, line, next_address, highlight_address):
    line_address = start + line[0]
    address_fmt = "  <td>%s</td>"
    if line_address == highlight_address:
      f.write("<tr class=highlight-line>")
      address_fmt = "  <td><a id=highlight>%s</a></td>"
    elif (line_address < highlight_address and
          highlight_address < next_address + start):
      f.write("<tr class=inexact-highlight-line>")
      address_fmt = "  <td><a id=highlight>%s</a></td>"
    else:
      f.write("<tr>")
    num_bytes = next_address - line[0]
    stack_slot = self.heap.stack_map.get(line_address)
    marker = ""
    if stack_slot:
      marker = "=>"

    code = line[1]

    # Some disassemblers insert spaces between each byte,
    # while some do not.
    if code[2] == " ":
      op_offset = 3 * num_bytes - 1
    else:
      op_offset = 2 * num_bytes

    # Compute the actual call target which the disassembler is too stupid
    # to figure out (it adds the call offset to the disassembly offset rather
    # than the absolute instruction address).
    if self.heap.reader.arch == MD_CPU_ARCHITECTURE_X86:
      if code.startswith("e8"):
        words = code.split()
        if len(words) > 6 and words[5] == "call":
          offset = int(words[4] + words[3] + words[2] + words[1], 16)
          target = (line_address + offset + 5) & 0xFFFFFFFF
          code = code.replace(words[6], "0x%08x" % target)
    # TODO(jkummerow): port this hack to ARM and x64.

    opcodes = code[:op_offset]
    code = self.annotate_disasm_addresses(code[op_offset:])
    f.write("  <td>")
    self.output_comment_box(f, "codel-", line_address)
    f.write("</td>")
    f.write(address_fmt % marker)
    f.write("  ")
    self.td_from_address(f, line_address)
    f.write(self.format_address(line_address))
    f.write(" (+0x%x)</td>" % line[0])
    f.write("<td>:&nbsp;%s&nbsp;</td>" % opcodes)
    f.write("<td>%s</td>" % code)
    f.write("</tr>")

  def output_comment_box(self, f, prefix, address):
    comment = self.comments.get_comment(address)
    value = ""
    if comment:
      value = " value=\"%s\"" % html.escape(comment)
    f.write("<input type=text class=ci "
            "id=%s-address-0x%s onchange=c()%s>" %
            (prefix,
             self.reader.FormatIntPtr(address),
             value))

  MAX_FOUND_RESULTS = 100

  def output_find_results(self, f, results):
    f.write("Addresses")
    toomany = len(results) > self.MAX_FOUND_RESULTS
    if toomany:
      f.write("(found %i results, displaying only first %i)" %
              (len(results), self.MAX_FOUND_RESULTS))
    f.write(": ")
    results = sorted(results)
    results = results[:min(len(results), self.MAX_FOUND_RESULTS)]
    for address in results:
      f.write("<span %s>%s</span>" %
              (self.comments.get_style_class_string(address),
               self.format_address(address)))
    if toomany:
      f.write("...")


  def output_page_info(self, f, page_kind, page_address, my_page_address):
    if my_page_address == page_address and page_address != 0:
      f.write("Marked first %s page." % page_kind)
    else:
      f.write("<span id=\"%spage\" style=\"display:none\">" % page_kind)
      f.write("Marked first %s page." % page_kind)
      f.write("</span>\n")
      f.write("<button onclick=\"onpage('%spage', '0x%x')\">" %
              (page_kind, my_page_address))
      f.write("Mark as first %s page</button>" % page_kind)
    return

  def output_search_res(self, f, straddress):
    try:
      self.output_header(f)
      f.write("<h3>Search results for %s</h3>" % straddress)

      address = int(straddress, 0)

      f.write("Comment: ")
      self.output_comment_box(f, "search-", address)
      f.write("<br>")

      page_address = address & ~self.heap.PageAlignmentMask()

      f.write("Page info: ")
      self.output_page_info(f, "old", self.padawan.known_first_old_page, \
                            page_address)
      self.output_page_info(f, "map", self.padawan.known_first_map_page, \
                            page_address)

      if not self.reader.IsValidAddress(address):
        f.write("<h3>The contents at address %s not found in the dump.</h3>" % \
                straddress)
      else:
        # Print as words
        self.output_words(f, address - 8, address + 32, address, "Dump",
                          self.heap.MachinePointerSize())

        if self.heap.IsPointerCompressed():
          self.output_words(f, address - 8, address + 32, address,
                            "Tagged Dump", self.heap.TaggedPointerSize())

        # Print as ASCII
        f.write("<hr>")
        self.output_ascii(f, address, address + 256, address)

        # Print as code
        f.write("<hr>")
        self.output_disasm_range(f, address - 16, address + 16, address, True)

      aligned_res, unaligned_res = self.reader.FindWordList(address)

      if len(aligned_res) > 0:
        f.write("<h3>Occurrences of 0x%x at aligned addresses</h3>" %
                address)
        self.output_find_results(f, aligned_res)

      if len(unaligned_res) > 0:
        f.write("<h3>Occurrences of 0x%x at unaligned addresses</h3>" % \
                address)
        self.output_find_results(f, unaligned_res)

      if len(aligned_res) + len(unaligned_res) == 0:
        f.write("<h3>No occurrences of 0x%x found in the dump</h3>" % address)

      self.output_footer(f)

    except ValueError:
      f.write("<h3>Unrecognized address format \"%s\".</h3>" % straddress)
    return

  def output_disasm_pc(self, f):
    address = self.reader.ExceptionIP()
    if not self.reader.IsValidAddress(address):
      return
    self.output_disasm_range(f, address - 16, address + 16, address, True)


WEB_DUMPS_HEADER = """
<!DOCTYPE html>
<html>
<head>
<meta content="text/html; charset=utf-8" http-equiv="content-type">
<style media="screen" type="text/css">

.dumplist {
  border-collapse : collapse;
  border-spacing : 0px;
  font-family: monospace;
}

.dumpcomments {
  border : 1px solid LightGray;
  width : 32em;
}

</style>

<script type="application/javascript">

var dump_str = "dump-";
var dump_len = dump_str.length;

function dump_comment() {
  var s = event.srcElement.id;
  var index = s.indexOf(dump_str);
  if (index >= 0) {
    send_dump_desc(s.substring(index + dump_len), event.srcElement.value);
  }
}

function send_dump_desc(name, desc) {
  xmlhttp = new XMLHttpRequest();
  name = encodeURIComponent(name)
  desc = encodeURIComponent(desc)
  xmlhttp.open("GET",
      "setdumpdesc?dump=" + name +
      "&description=" + desc, true);
  xmlhttp.send();
}

</script>

<title>Dump list</title>
</head>

<body>
"""

WEB_DUMPS_FOOTER = """
</body>
</html>
"""

DUMP_FILE_RE = re.compile(r"[-_0-9a-zA-Z][-\._0-9a-zA-Z]*\.dmp$")


class InspectionWebServer(http_server.HTTPServer):

  def __init__(self, port_number, switches, minidump_name):
    super().__init__(('localhost', port_number), InspectionWebHandler)
    splitpath = os.path.split(minidump_name)
    self.dumppath = splitpath[0]
    self.dumpfilename = splitpath[1]
    self.default_formatter = InspectionWebFormatter(
        switches, minidump_name, self)
    self.formatters = { self.dumpfilename : self.default_formatter }
    self.switches = switches

  def output_dump_desc_field(self, f, name):
    try:
      descfile = open(os.path.join(self.dumppath, name + ".desc"), "r")
      desc = descfile.readline()
      descfile.close()
    except IOError:
      desc = ""
    f.write("<input type=\"text\" class=\"dumpcomments\" "
            "id=\"dump-%s\" onchange=\"dump_comment()\" value=\"%s\">\n" %
            (html.escape(name), desc))

  def set_dump_desc(self, name, description):
    if not DUMP_FILE_RE.match(name):
      return False
    fname = os.path.join(self.dumppath, name)
    if not os.path.isfile(fname):
      return False
    fname = fname + ".desc"
    descfile = open(fname, "w")
    descfile.write(description)
    descfile.close()
    return True

  def get_dump_formatter(self, name):
    if name is None:
      return self.default_formatter
    else:
      if not DUMP_FILE_RE.match(name):
        raise WebParameterError("Invalid name '%s'" % name)
      formatter = self.formatters.get(name, None)
      if formatter is None:
        try:
          formatter = InspectionWebFormatter(
              self.switches, os.path.join(self.dumppath, name), self)
          self.formatters[name] = formatter
        except IOError:
          raise WebParameterError("Could not open dump '%s'" % name)
      return formatter

  def output_dumps(self, f):
    f.write(WEB_DUMPS_HEADER)
    f.write("<h3>List of available dumps</h3>")
    f.write("<table class=\"dumplist\">\n")
    f.write("<thead><tr>")
    f.write("<th>Name</th>")
    f.write("<th>File time</th>")
    f.write("<th>Comment</th>")
    f.write("</tr></thead>")
    dumps_by_time = {}
    for fname in os.listdir(self.dumppath):
      if DUMP_FILE_RE.match(fname):
        mtime = os.stat(os.path.join(self.dumppath, fname)).st_mtime
        fnames = dumps_by_time.get(mtime, [])
        fnames.append(fname)
        dumps_by_time[mtime] = fnames

    for mtime in sorted(dumps_by_time, reverse=True):
      fnames = dumps_by_time[mtime]
      for fname in fnames:
        f.write("<tr>\n")
        f.write("<td><a href=\"summary.html?%s\">%s</a></td>\n" %
                ((urllib.parse.urlencode({'dump': fname}), fname)))
        f.write("<td>&nbsp;&nbsp;&nbsp;")
        f.write(datetime.datetime.fromtimestamp(mtime))
        f.write("</td>")
        f.write("<td>&nbsp;&nbsp;&nbsp;")
        self.output_dump_desc_field(f, fname)
        f.write("</td>")
        f.write("</tr>\n")
    f.write("</table>\n")
    f.write(WEB_DUMPS_FOOTER)
    return

class InspectionShell(cmd.Cmd):
  def __init__(self, reader, heap):
    cmd.Cmd.__init__(self)
    self.reader = reader
    self.heap = heap
    self.padawan = InspectionPadawan(reader, heap)
    self.prompt = "(grok) "

    self.dd_start = 0
    self.dd_num = 0x10
    self.u_start = 0
    self.u_num = 0

  def EvalExpression(self, expr):
    # Auto convert hex numbers to a python compatible format
    if expr[:2] == "00":
      expr = "0x"+expr
    result = None
    try:
      # Ugly hack to patch in register values.
      registers = [register
                   for register,value in self.reader.ContextDescriptor().fields]
      registers.sort(key=lambda r: len(r))
      registers.reverse()
      for register in registers:
        expr = expr.replace("$"+register, str(self.reader.Register(register)))
      result = eval(expr)
    except Exception as e:
      print("**** Could not evaluate '%s': %s" % (expr, e))
      raise e
    return result

  def ParseAddressExpr(self, expr):
    address = 0;
    try:
      result = self.EvalExpression(expr)
    except:
      return 0
    try:
      address = int(result)
    except Exception as e:
      print("**** Could not convert '%s' => %s to valid address: %s" % (
          expr, result , e))
    return address

  def do_help(self, cmd=None):
    if len(cmd) == 0:
      print("Available commands")
      print("=" * 79)
      prefix = "do_"
      methods = inspect.getmembers(InspectionShell, predicate=inspect.ismethod)
      for name,method in methods:
        if not name.startswith(prefix): continue
        doc = inspect.getdoc(method)
        if not doc: continue
        name = prefix.join(name.split(prefix)[1:])
        description = doc.splitlines()[0]
        print((name + ": ").ljust(16) + description)
      print("=" * 79)
    else:
      return super(InspectionShell, self).do_help(cmd)

  def do_p(self, cmd):
    """ see print """
    return self.do_print(cmd)

  def do_print(self, cmd):
    """
    Evaluate an arbitrary python command.
    """
    try:
      print(self.EvalExpression(cmd))
    except:
      pass

  def do_da(self, address):
    """ see display_ascii"""
    return self.do_display_ascii(address)

  def do_display_ascii(self, address):
    """
     Print ASCII string starting at specified address.
    """
    address = self.ParseAddressExpr(address)
    string = self.reader.ReadAsciiString(address)
    if string == "":
      print("Not an ASCII string at %s" % self.reader.FormatIntPtr(address))
    else:
      print("%s\n" % string)

  def do_dsa(self, address):
    """ see display_stack_ascii"""
    return self.do_display_stack_ascii(address)

  def do_display_stack_ascii(self, address):
    """
    Print ASCII stack error message.
    """
    if self.reader.exception is None:
      print("Minidump has no exception info")
      return
    if len(address) == 0:
      address = None
    else:
      address = self.ParseAddressExpr(address)
    self.padawan.PrintStackTraceMessage(address)

  def do_dd(self, args):
    """
     Interpret memory in the given region [address, address + num * word_size)

     (if available) as a sequence of words. Automatic alignment is not performed.
     If the num is not specified, a default value of 16 words is usif not self.Is
     If no address is given, dd continues printing at the next word.

     Synopsis: dd 0x<address>|$register [0x<num>]
    """
    if len(args) != 0:
      args = args.split(' ')
      self.dd_start = self.ParseAddressExpr(args[0])
      self.dd_num = int(args[1], 16) if len(args) > 1 else 0x10
    else:
      self.dd_start += self.dd_num * self.reader.MachinePointerSize()
    if not self.reader.IsAlignedAddress(self.dd_start):
      print("Warning: Dumping un-aligned memory, is this what you had in mind?")
    end = self.dd_start + self.reader.MachinePointerSize() * self.dd_num
    self.padawan.InterpretMemory(self.dd_start, end)

  def do_do(self, address):
    """ see display_object """
    return self.do_display_object(address)

  def do_display_object(self, address):
    """
     Interpret memory at the given address as a V8 object.

     Automatic alignment makes sure that you can pass tagged as well as
     un-tagged addresses.
    """
    address = self.ParseAddressExpr(address)
    if self.reader.IsAlignedAddress(address):
      address = address + 1
    elif not self.heap.IsTaggedObjectAddress(address):
      print("Address doesn't look like a valid pointer!")
      return
    heap_object = self.padawan.SenseObject(address)
    if heap_object:
      heap_object.Print(Printer())
    else:
      print("Address cannot be interpreted as object!")

  def do_dso(self, args):
    """ see display_stack_objects """
    return self.do_display_stack_objects(args)

  def do_display_stack_objects(self, args):
    """
    Find and Print object pointers in the given range.

    Print all possible object pointers that are on the stack or in the given
    address range.

    Usage: dso [START_ADDR,[END_ADDR]]
    """
    start = self.reader.StackTop()
    end = self.reader.StackBottom()
    if len(args) != 0:
      args = args.split(' ')
      start = self.ParseAddressExpr(args[0])
      end = self.ParseAddressExpr(args[1]) if len(args) > 1 else end
    objects = self.heap.FindObjectPointers(start, end)
    for address in objects:
      heap_object = self.padawan.SenseObject(address)
      info = ""
      if heap_object:
        info = str(heap_object)
      print("%s %s" % (self.padawan.FormatIntPtr(address), info))

  def do_do_desc(self, address):
    """
      Print a descriptor array in a readable format.
    """
    start = self.ParseAddressExpr(address)
    if ((start & 1) == 1): start = start - 1
    DescriptorArray(FixedArray(self.heap, None, start)).Print(Printer())

  def do_do_map(self, address):
    """
      Print a Map in a readable format.
    """
    start = self.ParseAddressExpr(address)
    if ((start & 1) == 1): start = start - 1
    Map(self.heap, None, start).Print(Printer())

  def do_do_trans(self, address):
    """
      Print a transition array in a readable format.
    """
    start = self.ParseAddressExpr(address)
    if ((start & 1) == 1): start = start - 1
    TransitionArray(FixedArray(self.heap, None, start)).Print(Printer())

  def do_dp(self, address):
    """ see display_page """
    return self.do_display_page(address)

  def do_display_page(self, address):
    """
     Prints details about the V8 heap page of the given address.

     Interpret memory at the given address as being on a V8 heap page
     and print information about the page header (if available).
    """
    address = self.ParseAddressExpr(address)
    page_address = address & ~self.heap.PageAlignmentMask()
    if self.reader.IsValidAddress(page_address):
      print("**** Not Implemented")
      return
    else:
      print("Page header is not available!")

  def do_k(self, arguments):
    """
     Teach V8 heap layout information to the inspector.

     This increases the amount of annotations the inspector can produce while
     dumping data. The first page of each heap space is of particular interest
     because it contains known objects that do not move.
    """
    self.padawan.PrintKnowledge()

  def do_ko(self, address):
    """ see known_oldspace """
    return self.do_known_oldspace(address)

  def do_known_oldspace(self, address):
    """
     Teach V8 heap layout information to the inspector.

     Set the first old space page by passing any pointer into that page.
    """
    address = self.ParseAddressExpr(address)
    page_address = address & ~self.heap.PageAlignmentMask()
    self.padawan.known_first_old_page = page_address

  def do_km(self, address):
    """ see known_map """
    return self.do_known_map(address)

  def do_known_map(self, address):
    """
     Teach V8 heap layout information to the inspector.

     Set the first map-space page by passing any pointer into that page.
    """
    address = self.ParseAddressExpr(address)
    page_address = address & ~self.heap.PageAlignmentMask()
    self.padawan.known_first_map_page = page_address

  def do_list(self, smth):
    """
     List all available memory regions.
    """
    def print_region(reader, start, size, location):
      print("  %s - %s (%d bytes)" % (reader.FormatIntPtr(start),
                                      reader.FormatIntPtr(start + size),
                                      size))
    print("Available memory regions:")
    self.reader.ForEachMemoryRegion(print_region)

  def do_lm(self, arg):
    """ see list_modules """
    return self.do_list_modules(arg)

  def do_list_modules(self, arg):
    """
     List details for all loaded modules in the minidump.

     An argument can be passed to limit the output to only those modules that
     contain the argument as a substring (case insensitive match).
    """
    for module in self.reader.module_list.modules:
      if arg:
        name = GetModuleName(self.reader, module).lower()
        if name.find(arg.lower()) >= 0:
          PrintModuleDetails(self.reader, module)
      else:
        PrintModuleDetails(self.reader, module)
    print()

  def do_s(self, word):
    """ see search """
    return self.do_search(word)

  def do_search(self, word):
    """
     Search for a given word in available memory regions.

     The given word is expanded to full pointer size and searched at aligned
     as well as un-aligned memory locations. Use 'sa' to search aligned locations
     only.
    """
    try:
      word = self.ParseAddressExpr(word)
    except ValueError:
      print("Malformed word, prefix with '0x' to use hexadecimal format.")
      return
    print(
      "Searching for word %d/0x%s:" % (word, self.reader.FormatIntPtr(word)))
    self.reader.FindWord(word)

  def do_sh(self, none):
    """
     Search for the V8 Heap object in all available memory regions.

     You might get lucky and find this rare treasure full of invaluable
     information.
    """
    print("**** Not Implemented")

  def do_u(self, args):
    """ see disassemble """
    return self.do_disassemble(args)

  def do_disassemble(self, args):
    """
     Unassemble memory in the region [address, address + size).

     If the size is not specified, a default value of 32 bytes is used.
     Synopsis: u 0x<address> 0x<size>
    """
    if len(args) != 0:
      args = args.split(' ')
      self.u_start = self.ParseAddressExpr(args[0])
      self.u_size = self.ParseAddressExpr(args[1]) if len(args) > 1 else 0x20
      skip = False
    else:
      # Skip the first instruction if we reuse the last address.
      skip = True

    if not self.reader.IsValidAddress(self.u_start):
      print("Address %s is not contained within the minidump!" % (
          self.reader.FormatIntPtr(self.u_start)))
      return
    lines = self.reader.GetDisasmLines(self.u_start, self.u_size)
    if len(lines) == 0:
      print("Address %s could not be disassembled!" % (
          self.reader.FormatIntPtr(self.u_start)))
      print("    Could not disassemble using %s." % OBJDUMP_BIN)
      print("    Pass path to architecture specific objdump via --objdump?")
      return
    for line in lines:
      if skip:
        skip = False
        continue
      print(FormatDisasmLine(self.u_start, self.heap, line))
    # Set the next start address = last line
    self.u_start += lines[-1][0]
    print()

  def do_EOF(self, none):
    raise KeyboardInterrupt

EIP_PROXIMITY = 64

CONTEXT_FOR_ARCH = {
    MD_CPU_ARCHITECTURE_AMD64:
      ['rax', 'rbx', 'rcx', 'rdx', 'rdi', 'rsi', 'rbp', 'rsp', 'rip',
       'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'],
    MD_CPU_ARCHITECTURE_ARM:
      ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9',
       'r10', 'r11', 'r12', 'sp', 'lr', 'pc'],
    MD_CPU_ARCHITECTURE_ARM64:
      ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9',
       'r10', 'r11', 'r12', 'r13', 'r14', 'r15', 'r16', 'r17', 'r18', 'r19',
       'r20', 'r21', 'r22', 'r23', 'r24', 'r25', 'r26', 'r27', 'r28',
       'fp', 'lr', 'sp', 'pc'],
    MD_CPU_ARCHITECTURE_X86:
      ['eax', 'ebx', 'ecx', 'edx', 'edi', 'esi', 'ebp', 'esp', 'eip']
}

KNOWN_MODULES = {'chrome.exe', 'chrome.dll'}

def GetVersionString(ms, ls):
  return "%d.%d.%d.%d" % (ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff)


def GetModuleName(reader, module):
  name = reader.ReadMinidumpString(module.module_name_rva)
  # simplify for path manipulation
  name = name.encode('utf-8')
  return str(os.path.basename(str(name).replace("\\", "/")))


def PrintModuleDetails(reader, module):
  print("%s" % GetModuleName(reader, module))
  file_version = GetVersionString(module.version_info.dwFileVersionMS,
                                  module.version_info.dwFileVersionLS);
  product_version = GetVersionString(module.version_info.dwProductVersionMS,
                                     module.version_info.dwProductVersionLS)
  print("  base: %s" % reader.FormatIntPtr(module.base_of_image))
  print("  end: %s" % reader.FormatIntPtr(module.base_of_image +
                                          module.size_of_image))
  print("  file version: %s" % file_version)
  print("  product version: %s" % product_version)
  time_date_stamp = datetime.datetime.fromtimestamp(module.time_date_stamp)
  print("  timestamp: %s" % time_date_stamp)


def AnalyzeMinidump(options, minidump_name):
  reader = MinidumpReader(options, minidump_name)
  # Use a separate function to prevent leaking the minidump buffer through
  # ctypes in local variables.
  _AnalyzeMinidump(options, reader)
  reader.Dispose()


def _AnalyzeMinidump(options, reader):
  heap = None

  stack_top = reader.ExceptionSP()
  stack_bottom = reader.StackBottom()
  stack_map = {reader.ExceptionIP(): -1}
  for slot in range(stack_top, stack_bottom, reader.MachinePointerSize()):
    maybe_address = reader.ReadUIntPtr(slot)
    if not maybe_address in stack_map:
      stack_map[maybe_address] = slot

  heap = V8Heap(reader, stack_map)
  padawan = InspectionPadawan(reader, heap)

  DebugPrint("========================================")
  if reader.exception is None:
    print("Minidump has no exception info")
  else:
    print("Address markers:")
    print("  T = valid tagged pointer in the minidump")
    print("  S = address on the exception stack")
    print("  C = address in loaded C/C++ module")
    print("  * = address in the minidump")
    print("")
    print("Exception info:")
    exception_thread = reader.ExceptionThread()
    print("  thread id: %d" % exception_thread.id)
    print("  code:      %08X" % reader.exception.exception.code)
    print("  context:")
    context = CONTEXT_FOR_ARCH[reader.arch]
    maxWidth = max(map(lambda s: len(s), context))
    for r in context:
      register_value = reader.Register(r)
      print("    %s: %s" % (r.rjust(maxWidth),
                            heap.FormatIntPtr(register_value)))
    # TODO(vitalyr): decode eflags.
    if reader.arch in [MD_CPU_ARCHITECTURE_ARM, MD_CPU_ARCHITECTURE_ARM64]:
      print("    cpsr: %s" % bin(reader.exception_context.cpsr)[2:])
    else:
      print("    eflags: %s" % bin(reader.exception_context.eflags)[2:])

    print()
    print("  modules:")
    for module in reader.module_list.modules:
      name = GetModuleName(reader, module)
      if name in KNOWN_MODULES:
        print("    %s at %08X" % (name, module.base_of_image))
        reader.TryLoadSymbolsFor(name, module)
    print()

    print("  stack-top:    %s" % heap.FormatIntPtr(reader.StackTop()))
    print("  stack-bottom: %s" % heap.FormatIntPtr(reader.StackBottom()))
    print("")

    if options.shell:
      padawan.PrintStackTraceMessage(print_message=False)

    print("Disassembly around exception.eip:")
    eip_symbol = reader.FindSymbol(reader.ExceptionIP())
    if eip_symbol is not None:
      print(eip_symbol)
    disasm_start = reader.ExceptionIP() - EIP_PROXIMITY
    disasm_bytes = 2 * EIP_PROXIMITY
    if (options.full):
      full_range = reader.FindRegion(reader.ExceptionIP())
      if full_range is not None:
        disasm_start = full_range[0]
        disasm_bytes = full_range[1]

    lines = reader.GetDisasmLines(disasm_start, disasm_bytes)

    if not lines:
      print("Could not disassemble using %s." % OBJDUMP_BIN)
      print("Pass path to architecture specific objdump via --objdump?")

    for line in lines:
      print(FormatDisasmLine(disasm_start, heap, line))
    print()

  if heap is None:
    heap = V8Heap(reader, None)

  if options.full:
    FullDump(reader, heap)

  if options.command:
    InspectionShell(reader, heap).onecmd(options.command)

  if options.shell:
    try:
      InspectionShell(reader, heap).cmdloop("type help to get help")
    except KeyboardInterrupt:
      print("Kthxbye.")
  elif not options.command:
    if reader.exception is not None:
      print("Annotated stack (from exception.esp to bottom):")
      stack_start = padawan.PrintStackTraceMessage()
      padawan.InterpretMemory(stack_start, stack_bottom)


if __name__ == "__main__":
  parser = optparse.OptionParser(USAGE)
  parser.add_option("-s", "--shell", dest="shell", action="store_true",
                    help="start an interactive inspector shell")
  parser.add_option("-w", "--web", dest="web", action="store_true",
                    help="start a web server on localhost:%i" % PORT_NUMBER)
  parser.add_option("-c", "--command", dest="command", default="",
                    help="run an interactive inspector shell command and exit")
  parser.add_option("-f", "--full", dest="full", action="store_true",
                    help="dump all information contained in the minidump")
  parser.add_option("--symdir", dest="symdir", default=".",
                    help="directory containing *.pdb.sym file with symbols")
  parser.add_option("--objdump", default="",
                    help="objdump tool to use [default: %s]" % (
                        DEFAULT_OBJDUMP_BIN))
  options, args = parser.parse_args()
  if len(args) != 1:
    parser.print_help()
    sys.exit(1)
  if options.web:
    try:
      server = InspectionWebServer(PORT_NUMBER, options, args[0])
      print('Started httpserver on port ' , PORT_NUMBER)
      webbrowser.open('http://localhost:%i/summary.html' % PORT_NUMBER)
      server.serve_forever()
    except KeyboardInterrupt:
      print('^C received, shutting down the web server')
      server.socket.close()
  else:
    AnalyzeMinidump(options, args[0])
