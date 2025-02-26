// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRAP_HANDLER_TRAP_HANDLER_INTERNAL_H_
#define V8_TRAP_HANDLER_TRAP_HANDLER_INTERNAL_H_

// This file should not be included (even transitively) by files outside of
// src/trap-handler.

#include "src/trap-handler/trap-handler.h"

#include <atomic>

namespace v8 {
namespace internal {
namespace trap_handler {

// This describes a chunk of code that the trap handler will be able to handle
// faults in. {base} points to the beginning of the chunk, and {size} is the
// number of bytes in the code chunk. The remainder of the struct is a list of
// protected memory access instructions and an offset to a landing pad to handle
// faults on that instruction.
struct CodeProtectionInfo {
  uintptr_t base;
  size_t size;
  size_t num_protected_instructions;
  ProtectedInstructionData instructions[1];
};

class MetadataLock {
  static std::atomic_flag spinlock_;

 public:
  MetadataLock();
  ~MetadataLock();

  MetadataLock(const MetadataLock&) = delete;
  void operator=(const MetadataLock&) = delete;
};

// To enable constant time registration of handler data, we keep a free list of
// entries in the gCodeObjects table. Each entry contains a {next_free} field,
// which can be used to figure out where the next entry should be inserted.
// In order to avoid having to initialize all the links to start with, we use
// 0 to indicate that this is a fresh, never-used list entry and that therefore
// the next entry is known to be free. If {next_entry} is greater than zero,
// then {next_entry - 1} is the index that we should insert into next.
struct CodeProtectionInfoListEntry {
  CodeProtectionInfo* code_info;
  size_t next_free;
};

extern size_t gNumCodeObjects;
extern CodeProtectionInfoListEntry* gCodeObjects;

// This list describes sandboxes as bases and sizes.
struct SandboxRecord {
  uintptr_t base;
  size_t size;
  SandboxRecord* next;
};

class SandboxRecordsLock {
  static std::atomic_flag spinlock_;

 public:
  SandboxRecordsLock();
  ~SandboxRecordsLock();

  SandboxRecordsLock(const SandboxRecordsLock&) = delete;
  void operator=(const SandboxRecordsLock&) = delete;
};

extern SandboxRecord* gSandboxRecordsHead;

extern std::atomic_size_t gRecoveredTrapCount;

extern std::atomic<uintptr_t> gLandingPad;

// Searches the fault location table for an entry matching fault_addr. If found,
// returns true, otherwise, returns false.
bool IsFaultAddressCovered(uintptr_t fault_addr);

// Checks whether the accessed memory is covered by the trap handler. In
// particular, when the V8 sandbox is enabled, only faulting accesses to memory
// inside the sandbox are handled by the trap handler since all Wasm memory
// objects are inside the sandbox.
bool IsAccessedMemoryCovered(uintptr_t accessed_addr);

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8

#endif  // V8_TRAP_HANDLER_TRAP_HANDLER_INTERNAL_H_
