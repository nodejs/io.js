// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
#define V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_

#include "src/builtins/builtins-inl.h"
#include "src/common/code-memory-access-inl.h"
#include "src/objects/objects-inl.h"
#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/js-dispatch-table.h"
#include "src/snapshot/embedded/embedded-data.h"

#ifdef V8_ENABLE_LEAPTIERING

namespace v8 {
namespace internal {

void JSDispatchEntry::MakeJSDispatchEntry(Address object, Address entrypoint,
                                          uint16_t parameter_count,
                                          bool mark_as_alive) {
  DCHECK_EQ(object & kHeapObjectTag, 0);
  DCHECK_EQ((object << kObjectPointerShift) >> kObjectPointerShift, object);

  Address payload =
      (object << kObjectPointerShift) | (parameter_count & kParameterCountMask);
  DCHECK(!(payload & kMarkingBit));
  if (mark_as_alive) payload |= kMarkingBit;
#ifdef V8_TARGET_ARCH_32_BIT
  parameter_count_.store(parameter_count, std::memory_order_relaxed);
  next_free_entry_.store(0, std::memory_order_relaxed);
#endif
  encoded_word_.store(payload, std::memory_order_relaxed);
  entrypoint_.store(entrypoint, std::memory_order_relaxed);
  DCHECK(!IsFreelistEntry());
}

Address JSDispatchEntry::GetEntrypoint() const {
  CHECK(!IsFreelistEntry());
  return entrypoint_.load(std::memory_order_relaxed);
}

Address JSDispatchEntry::GetCodePointer() const {
  CHECK(!IsFreelistEntry());
  // The pointer tag bit (LSB) of the object pointer is used as marking bit,
  // and so may be 0 or 1 here. As the return value is a tagged pointer, the
  // bit must be 1 when returned, so we need to set it here.
  Address payload = encoded_word_.load(std::memory_order_relaxed);
  return (payload >> kObjectPointerShift) | kHeapObjectTag;
}

Tagged<Code> JSDispatchEntry::GetCode() const {
  return Cast<Code>(Tagged<Object>(GetCodePointer()));
}

uint16_t JSDispatchEntry::GetParameterCount() const {
  // Loading a pointer out of a freed entry will always result in an invalid
  // pointer (e.g. upper bits set or nullptr). However, here we're just loading
  // an integer (the parameter count), so we probably want to make sure that
  // we're not getting that from a freed entry.
  CHECK(!IsFreelistEntry());
#ifdef V8_TARGET_ARCH_32_BIT
  return parameter_count_.load(std::memory_order_relaxed);
#else
  static_assert(kParameterCountMask != 0);
  Address payload = encoded_word_.load(std::memory_order_relaxed);
  return payload & kParameterCountMask;
#endif
}

Tagged<Code> JSDispatchTable::GetCode(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetCode();
}

void JSDispatchTable::SetCodeNoWriteBarrier(JSDispatchHandle handle,
                                            Tagged<Code> new_code) {
  SetCodeAndEntrypointNoWriteBarrier(handle, new_code,
                                     new_code->instruction_start());
}

void JSDispatchTable::SetCodeKeepTieringRequestNoWriteBarrier(
    JSDispatchHandle handle, Tagged<Code> new_code) {
  if (IsTieringRequested(handle)) {
    SetCodeAndEntrypointNoWriteBarrier(handle, new_code, GetEntrypoint(handle));
  } else {
    SetCodeAndEntrypointNoWriteBarrier(handle, new_code,
                                       new_code->instruction_start());
  }
}

void JSDispatchTable::SetCodeAndEntrypointNoWriteBarrier(
    JSDispatchHandle handle, Tagged<Code> new_code, Address new_entrypoint) {
  SBXCHECK(IsCompatibleCode(new_code, GetParameterCount(handle)));

  // The object should be in old space to avoid creating old-to-new references.
  DCHECK(!HeapLayout::InYoungGeneration(new_code));

  uint32_t index = HandleToIndex(handle);
  DCHECK_GE(index, kEndOfInternalReadOnlySegment);
  CFIMetadataWriteScope write_scope("JSDispatchTable update");
  at(index).SetCodeAndEntrypointPointer(new_code.ptr(), new_entrypoint);
}

void JSDispatchTable::SetTieringRequest(JSDispatchHandle handle,
                                        TieringBuiltin builtin,
                                        Isolate* isolate) {
  DCHECK(IsValidTieringBuiltin(builtin));
  uint32_t index = HandleToIndex(handle);
  DCHECK_GE(index, kEndOfInternalReadOnlySegment);
  CFIMetadataWriteScope write_scope("JSDispatchTable update");
  at(index).SetEntrypointPointer(
      isolate->builtin_entry_table()[static_cast<uint32_t>(builtin)]);
}

bool JSDispatchTable::IsTieringRequested(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  DCHECK_GE(index, kEndOfInternalReadOnlySegment);
  Address entrypoint = at(index).GetEntrypoint();
  Address code_entrypoint = at(index).GetCode()->instruction_start();
  return code_entrypoint != entrypoint;
}

bool JSDispatchTable::IsTieringRequested(JSDispatchHandle handle,
                                         TieringBuiltin builtin,
                                         Isolate* isolate) {
  uint32_t index = HandleToIndex(handle);
  DCHECK_GE(index, kEndOfInternalReadOnlySegment);
  Address entrypoint = at(index).GetEntrypoint();
  Address code_entrypoint = at(index).GetCode()->instruction_start();
  if (entrypoint == code_entrypoint) return false;
  return entrypoint == EmbeddedData::FromBlob(isolate).InstructionStartOf(
                           static_cast<Builtin>(builtin));
}

void JSDispatchTable::ResetTieringRequest(JSDispatchHandle handle,
                                          Isolate* isolate) {
  uint32_t index = HandleToIndex(handle);
  DCHECK_GE(index, kEndOfInternalReadOnlySegment);
  CFIMetadataWriteScope write_scope("JSDispatchTable update");
  at(index).SetEntrypointPointer(at(index).GetCode()->instruction_start());
}

JSDispatchHandle JSDispatchTable::AllocateAndInitializeEntry(
    Space* space, uint16_t parameter_count) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  CFIMetadataWriteScope write_scope("JSDispatchTable initialize");
  at(index).MakeJSDispatchEntry(kNullAddress, kNullAddress, parameter_count,
                                space->allocate_black());
  return IndexToHandle(index);
}

JSDispatchHandle JSDispatchTable::AllocateAndInitializeEntry(
    Space* space, uint16_t parameter_count, Tagged<Code> new_code) {
  DCHECK(space->BelongsTo(this));
  SBXCHECK(IsCompatibleCode(new_code, parameter_count));

  uint32_t index = AllocateEntry(space);
  JSDispatchEntry& entry = at(index);
  CFIMetadataWriteScope write_scope("JSDispatchTable initialize");
  entry.MakeJSDispatchEntry(new_code.address(), new_code->instruction_start(),
                            parameter_count, space->allocate_black());
  return IndexToHandle(index);
}

void JSDispatchEntry::SetCodeAndEntrypointPointer(Address new_object,
                                                  Address new_entrypoint) {
  Address old_payload = encoded_word_.load(std::memory_order_relaxed);
  Address marking_bit = old_payload & kMarkingBit;
  Address parameter_count = old_payload & kParameterCountMask;
  // We want to preserve the marking bit of the entry. Since that happens to
  // be the tag bit of the pointer, we need to explicitly clear it here.
  Address object = (new_object << kObjectPointerShift) & ~kMarkingBit;
  Address new_payload = object | marking_bit | parameter_count;
  encoded_word_.store(new_payload, std::memory_order_relaxed);
  entrypoint_.store(new_entrypoint, std::memory_order_relaxed);
  DCHECK(!IsFreelistEntry());
}

void JSDispatchEntry::SetEntrypointPointer(Address new_entrypoint) {
  entrypoint_.store(new_entrypoint, std::memory_order_relaxed);
}

void JSDispatchEntry::MakeFreelistEntry(uint32_t next_entry_index) {
#ifdef V8_TARGET_ARCH_64_BIT
  Address payload = kFreeEntryTag | next_entry_index;
  entrypoint_.store(payload, std::memory_order_relaxed);
#else
  // Store index + 1 such that we can use 0 for non-free entries.
  next_free_entry_.store(next_entry_index + 1, std::memory_order_relaxed);
  entrypoint_.store(kNullAddress, std::memory_order_relaxed);
#endif
  encoded_word_.store(kNullAddress, std::memory_order_relaxed);
  DCHECK(IsFreelistEntry());
}

bool JSDispatchEntry::IsFreelistEntry() const {
#ifdef V8_TARGET_ARCH_64_BIT
  auto entrypoint = entrypoint_.load(std::memory_order_relaxed);
  return (entrypoint & kFreeEntryTag) == kFreeEntryTag;
#else
  return next_free_entry_.load(std::memory_order_relaxed) != 0;
#endif
}

uint32_t JSDispatchEntry::GetNextFreelistEntryIndex() const {
  DCHECK(IsFreelistEntry());
#ifdef V8_TARGET_ARCH_64_BIT
  return static_cast<uint32_t>(entrypoint_.load(std::memory_order_relaxed));
#else
  return next_free_entry_.load(std::memory_order_relaxed) - 1;
#endif
}

void JSDispatchEntry::Mark() {
  Address old_value = encoded_word_.load(std::memory_order_relaxed);
  Address new_value = old_value | kMarkingBit;
  // We don't need this cas to succeed. If marking races with
  // `SetCodeAndEntrypointPointer`, then we are bound to re-set the mark bit in
  // the write barrier.
  static_assert(JSDispatchTable::kWriteBarrierSetsEntryMarkBit);
  encoded_word_.compare_exchange_strong(old_value, new_value,
                                        std::memory_order_relaxed);
}

void JSDispatchEntry::Unmark() {
  Address value = encoded_word_.load(std::memory_order_relaxed);
  value &= ~kMarkingBit;
  encoded_word_.store(value, std::memory_order_relaxed);
}

bool JSDispatchEntry::IsMarked() const {
  Address value = encoded_word_.load(std::memory_order_relaxed);
  return value & kMarkingBit;
}

Address JSDispatchTable::GetEntrypoint(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetEntrypoint();
}

Address JSDispatchTable::GetCodeAddress(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  Address ptr = at(index).GetCodePointer();
  DCHECK(Internals::HasHeapObjectTag(ptr));
  return ptr;
}

uint16_t JSDispatchTable::GetParameterCount(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetParameterCount();
}

void JSDispatchTable::Mark(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);

  // The read-only space is immortal and cannot be written to.
  if (index < kEndOfInternalReadOnlySegment) return;

  CFIMetadataWriteScope write_scope("JSDispatchTable write");
  at(index).Mark();
}

#if defined(DEBUG) || defined(VERIFY_HEAP)
void JSDispatchTable::VerifyEntry(JSDispatchHandle handle, Space* space,
                                  Space* ro_space) {
  DCHECK(space->BelongsTo(this));
  DCHECK(ro_space->BelongsTo(this));
  if (handle == kNullJSDispatchHandle) {
    return;
  }
  uint32_t index = HandleToIndex(handle);
  if (ro_space->Contains(index)) {
    CHECK(at(index).IsMarked());
  } else {
    CHECK(space->Contains(index));
  }
}
#endif  // defined(DEBUG) || defined(VERIFY_HEAP)

template <typename Callback>
void JSDispatchTable::IterateActiveEntriesIn(Space* space, Callback callback) {
  IterateEntriesIn(space, [&](uint32_t index) {
    if (!at(index).IsFreelistEntry()) {
      callback(IndexToHandle(index));
    }
  });
}

template <typename Callback>
void JSDispatchTable::IterateMarkedEntriesIn(Space* space, Callback callback) {
  IterateEntriesIn(space, [&](uint32_t index) {
    if (at(index).IsMarked()) {
      callback(IndexToHandle(index));
    }
  });
}

template <typename Callback>
uint32_t JSDispatchTable::Sweep(Space* space, Counters* counters,
                                Callback callback) {
  uint32_t num_live_entries = GenericSweep(space, callback);
  counters->js_dispatch_table_entries_count()->AddSample(num_live_entries);
  return num_live_entries;
}

// static
bool JSDispatchTable::IsCompatibleCode(Tagged<Code> code,
                                       uint16_t parameter_count) {
  if (code->entrypoint_tag() != kJSEntrypointTag) {
    // Target code doesn't use JS linkage. This cannot be valid.
    return false;
  }
  if (code->parameter_count() == parameter_count) {
    DCHECK_IMPLIES(code->is_builtin(),
                   parameter_count ==
                       Builtins::GetFormalParameterCount(code->builtin_id()));
    // Dispatch entry and code have the same signature. This is correct.
    return true;
  }

  // Signature mismatch. This is mostly not safe, except for certain varargs
  // builtins which are able to correctly handle such a mismatch. Examples
  // include builtins like the InterpreterEntryTrampoline or the JSToWasm and
  // JSToJS wrappers which determine their actual parameter count at runtime
  // (see CodeStubAssembler::SetSupportsDynamicParameterCount()), or internal
  // builtins that end up tailcalling into other code such as CompileLazy.
  //
  // Currently, we also allow this for testing code (from our test suites).
  // TODO(saelo): maybe we should also forbid this just to be sure.
  if (code->kind() == CodeKind::FOR_TESTING) {
    return true;
  }
  DCHECK(code->is_builtin());
  DCHECK_EQ(code->parameter_count(), kDontAdaptArgumentsSentinel);
  switch (code->builtin_id()) {
    case Builtin::kIllegal:
    case Builtin::kCompileLazy:
    case Builtin::kInterpreterEntryTrampoline:
    case Builtin::kInstantiateAsmJs:
    case Builtin::kDebugBreakTrampoline:
#ifdef V8_ENABLE_WEBASSEMBLY
    case Builtin::kJSToWasmWrapper:
    case Builtin::kJSToJSWrapper:
    case Builtin::kJSToJSWrapperInvalidSig:
    case Builtin::kWasmPromising:
#if V8_ENABLE_DRUMBRAKE
    case Builtin::kGenericJSToWasmInterpreterWrapper:
#endif
    case Builtin::kWasmStressSwitch:
#endif
      return true;
    default:
      return false;
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_LEAPTIERING

#endif  // V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
