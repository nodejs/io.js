// Copyright 2016 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_DIAGNOSTICS_PERF_JIT_H_
#define V8_DIAGNOSTICS_PERF_JIT_H_

#include "include/v8config.h"

// {LinuxPerfJitLogger} is only implemented on Linux.
#if V8_OS_LINUX

#include "src/logging/log.h"

namespace v8 {
namespace internal {

// Linux perf tool logging support.
class LinuxPerfJitLogger : public CodeEventLogger {
 public:
  explicit LinuxPerfJitLogger(Isolate* isolate);
  ~LinuxPerfJitLogger() override;

  void CodeMoveEvent(Tagged<InstructionStream> from,
                     Tagged<InstructionStream> to) override {
    UNREACHABLE();  // Unsupported.
  }
  void BytecodeMoveEvent(Tagged<BytecodeArray> from,
                         Tagged<BytecodeArray> to) override {}
  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) override {}

 private:
  void OpenJitDumpFile();
  void CloseJitDumpFile();
  void* OpenMarkerFile(int fd);
  void CloseMarkerFile(void* marker_address);

  uint64_t GetTimestamp();
  void LogRecordedBuffer(Tagged<AbstractCode> code,
                         MaybeHandle<SharedFunctionInfo> maybe_shared,
                         const char* name, size_t length) override;
#if V8_ENABLE_WEBASSEMBLY
  void LogRecordedBuffer(const wasm::WasmCode* code, const char* name,
                         size_t length) override;
#endif  // V8_ENABLE_WEBASSEMBLY

  // Extension added to V8 log file name to get the low-level log name.
  static const char kFilenameFormatString[];
  static const int kFilenameBufferPadding;

  // File buffer size of the low-level log. We don't use the default to
  // minimize the associated overhead.
  static const int kLogBufferSize = 2 * MB;

  void WriteJitCodeLoadEntry(const uint8_t* code_pointer, uint32_t code_size,
                             const char* name, size_t name_length);

  void LogWriteBytes(const char* bytes, size_t size);
  void LogWriteHeader();
  void LogWriteDebugInfo(Tagged<Code> code, Handle<SharedFunctionInfo> shared);
#if V8_ENABLE_WEBASSEMBLY
  void LogWriteDebugInfo(const wasm::WasmCode* code);
#endif  // V8_ENABLE_WEBASSEMBLY
  void LogWriteUnwindingInfo(Tagged<Code> code);

  static const uint32_t kElfMachIA32 = 3;
  static const uint32_t kElfMachX64 = 62;
  static const uint32_t kElfMachARM = 40;
  static const uint32_t kElfMachMIPS64 = 8;
  static const uint32_t kElfMachLOONG64 = 258;
  static const uint32_t kElfMachARM64 = 183;
  static const uint32_t kElfMachS390x = 22;
  static const uint32_t kElfMachPPC64 = 21;
  static const uint32_t kElfMachRISCV = 243;

  uint32_t GetElfMach() {
#if V8_TARGET_ARCH_IA32
    return kElfMachIA32;
#elif V8_TARGET_ARCH_X64
    return kElfMachX64;
#elif V8_TARGET_ARCH_ARM
    return kElfMachARM;
#elif V8_TARGET_ARCH_MIPS64
    return kElfMachMIPS64;
#elif V8_TARGET_ARCH_LOONG64
    return kElfMachLOONG64;
#elif V8_TARGET_ARCH_ARM64
    return kElfMachARM64;
#elif V8_TARGET_ARCH_S390X
    return kElfMachS390x;
#elif V8_TARGET_ARCH_PPC64
    return kElfMachPPC64;
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
    return kElfMachRISCV;
#else
    UNIMPLEMENTED();
    return 0;
#endif
  }

#if V8_TARGET_ARCH_32_BIT
  static const int kElfHeaderSize = 0x34;
#elif V8_TARGET_ARCH_64_BIT
  static const int kElfHeaderSize = 0x40;
#else
#error Unknown target architecture pointer size
#endif

  // Per-process singleton file. We assume that there is one main isolate;
  // to determine when it goes away, we keep reference count.
  static FILE* perf_output_handle_;
  static uint64_t reference_count_;
  static void* marker_address_;
  static uint64_t code_index_;
  static int process_id_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OS_LINUX

#endif  // V8_DIAGNOSTICS_PERF_JIT_H_
