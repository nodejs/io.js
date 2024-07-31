// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/instruction-stream.h"

#include "src/codegen/assembler-inl.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/codegen/reloc-info.h"
#include "src/objects/instruction-stream-inl.h"

namespace v8 {
namespace internal {

void InstructionStream::Relocate(WritableJitAllocation& jit_allocation,
                                 intptr_t delta) {
  Tagged<Code> code;
  if (!TryGetCodeUnchecked(&code, kAcquireLoad)) return;
  // This is called during evacuation and code.instruction_stream() will point
  // to the old object. So pass *this directly to the RelocIterator.
  for (WritableRelocIterator it(jit_allocation, *this, constant_pool(),
                                RelocInfo::kApplyMask);
       !it.done(); it.next()) {
    it.rinfo()->apply(delta);
  }
  FlushInstructionCache(instruction_start(), body_size());
}

// This function performs the relocations but doesn't trigger any write barriers
// yet. We skip the write barriers here with UNSAFE_SKIP_WRITE_BARRIER but the
// caller needs to call RelocateFromDescWriteBarriers afterwards.
InstructionStream::WriteBarrierPromise InstructionStream::RelocateFromDesc(
    WritableJitAllocation& jit_allocation, Heap* heap, const CodeDesc& desc,
    Address constant_pool, const DisallowGarbageCollection& no_gc) {
  WriteBarrierPromise write_barrier_promise;
  Assembler* origin = desc.origin;
  const int mode_mask = RelocInfo::PostCodegenRelocationMask();
  for (WritableRelocIterator it(jit_allocation, *this, constant_pool,
                                mode_mask);
       !it.done(); it.next()) {
    // IMPORTANT:
    // this code needs be stay in sync with RelocateFromDescWriteBarriers below.

    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsEmbeddedObjectMode(mode)) {
      Handle<HeapObject> p = it.rinfo()->target_object_handle(origin);
      it.rinfo()->set_target_object(*this, *p, UNSAFE_SKIP_WRITE_BARRIER,
                                    SKIP_ICACHE_FLUSH);
      write_barrier_promise.RegisterAddress(it.rinfo()->pc());
    } else if (RelocInfo::IsCodeTargetMode(mode)) {
      // Rewrite code handles to direct pointers to the first instruction in the
      // code object.
      Handle<HeapObject> p = it.rinfo()->target_object_handle(origin);
      DCHECK(IsCode(*p));
      Tagged<InstructionStream> target_istream =
          Code::cast(*p)->instruction_stream();
      it.rinfo()->set_target_address(*this, target_istream->instruction_start(),
                                     UNSAFE_SKIP_WRITE_BARRIER,
                                     SKIP_ICACHE_FLUSH);
      write_barrier_promise.RegisterAddress(it.rinfo()->pc());
    } else if (RelocInfo::IsNearBuiltinEntry(mode)) {
      // Rewrite builtin IDs to PC-relative offset to the builtin entry point.
      Builtin builtin = it.rinfo()->target_builtin_at(origin);
      Address p =
          heap->isolate()->builtin_entry_table()[Builtins::ToInt(builtin)];
      // This won't trigger a write barrier, but setting mode to
      // UPDATE_WRITE_BARRIER to make it clear that we didn't forget about it
      // below.
      it.rinfo()->set_target_address(*this, p, UPDATE_WRITE_BARRIER,
                                     SKIP_ICACHE_FLUSH);
      DCHECK_EQ(p, it.rinfo()->target_address());
    } else if (RelocInfo::IsWasmStubCall(mode)) {
#if V8_ENABLE_WEBASSEMBLY
      // Map wasm stub id to builtin.
      uint32_t stub_call_tag = it.rinfo()->wasm_call_tag();
      DCHECK_LT(stub_call_tag,
                static_cast<uint32_t>(Builtin::kFirstBytecodeHandler));
      Builtin builtin = static_cast<Builtin>(stub_call_tag);
      // Store the builtin address in relocation info.
      Address entry =
          heap->isolate()->builtin_entry_table()[Builtins::ToInt(builtin)];
      it.rinfo()->set_wasm_stub_call_address(entry, SKIP_ICACHE_FLUSH);
#else
      UNREACHABLE();
#endif
    } else {
      intptr_t delta =
          instruction_start() - reinterpret_cast<Address>(desc.buffer);
      it.rinfo()->apply(delta);
    }
  }
  return write_barrier_promise;
}

void InstructionStream::RelocateFromDescWriteBarriers(
    Heap* heap, const CodeDesc& desc, Address constant_pool,
    WriteBarrierPromise& write_barrier_promise,
    const DisallowGarbageCollection& no_gc) {
  const int mode_mask = RelocInfo::PostCodegenRelocationMask();
  for (RelocIterator it(code(kAcquireLoad), mode_mask); !it.done(); it.next()) {
    // IMPORTANT:
    // this code needs be stay in sync with RelocateFromDesc above.

    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsEmbeddedObjectMode(mode)) {
      Tagged<HeapObject> p = it.rinfo()->target_object(heap->isolate());
      WriteBarrierForCode(*this, it.rinfo(), p, UPDATE_WRITE_BARRIER);
      write_barrier_promise.ResolveAddress(it.rinfo()->pc());
    } else if (RelocInfo::IsCodeTargetMode(mode)) {
      Tagged<InstructionStream> target_istream =
          InstructionStream::FromTargetAddress(it.rinfo()->target_address());
      WriteBarrierForCode(*this, it.rinfo(), target_istream,
                          UPDATE_WRITE_BARRIER);
      write_barrier_promise.ResolveAddress(it.rinfo()->pc());
    }
  }
}

#ifdef DEBUG
void InstructionStream::WriteBarrierPromise::RegisterAddress(Address address) {
  DCHECK(delayed_write_barriers_.insert(address).second);
}

void InstructionStream::WriteBarrierPromise::ResolveAddress(Address address) {
  DCHECK_EQ(delayed_write_barriers_.erase(address), 1);
}
InstructionStream::WriteBarrierPromise::~WriteBarrierPromise() {
  DCHECK(delayed_write_barriers_.empty());
}
#endif

}  // namespace internal
}  // namespace v8
