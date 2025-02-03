// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-import-wrapper-cache.h"

#include <vector>

#include "src/codegen/assembler-inl.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/common/code-memory-access-inl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

WasmCode* WasmImportWrapperCache::ModificationScope::operator[](
    const CacheKey& key) {
  return cache_->entry_map_[key];
}

// The wrapper cache is shared per-process; but it is initialized on demand, and
// this action is triggered by some isolate; so we use this isolate for error
// reporting and running GCs if required.
void WasmImportWrapperCache::LazyInitialize(Isolate* triggering_isolate) {
  base::MutexGuard lock(&mutex_);
  if (code_allocator_.get() != nullptr) return;  // Already initialized.
  // Most wrappers are small (200-300 bytes), most modules don't need many.
  // 32K is enough for ~100 wrappers.
  static constexpr size_t kInitialReservationSize = 1 << 15;
  // See {NewNativeModule} for reasoning.
  static constexpr int kAllocationRetries = 2;
  VirtualMemory code_space;
  for (int retries = 0;; ++retries) {
    code_space = GetWasmCodeManager()->TryAllocate(kInitialReservationSize);
    if (code_space.IsReserved()) break;
    if (retries == kAllocationRetries) {
      V8::FatalProcessOutOfMemory(
          triggering_isolate,
          "Failed to allocate code space for import wrappers");
      UNREACHABLE();
    }
    triggering_isolate->heap()->MemoryPressureNotification(
        MemoryPressureLevel::kCritical, true);
  }
  code_allocator_.reset(
      new WasmCodeAllocator(triggering_isolate->async_counters()));
  base::AddressRegion initial_region = code_space.region();
  code_allocator_->Init(std::move(code_space));
  code_allocator_->InitializeCodeRange(nullptr, initial_region);
}

WasmCode* WasmImportWrapperCache::ModificationScope::AddWrapper(
    const CacheKey& key, WasmCompilationResult result, WasmCode::Kind kind) {
  cache_->mutex_.AssertHeld();
  // Equivalent of NativeModule::AddCode().
  const CodeDesc& desc = result.code_desc;
  base::Vector<uint8_t> code_space =
      cache_->code_allocator_->AllocateForWrapper(desc.instr_size);

  // Equivalent of NativeModule::AddCodeWithCodeSpace().
  base::Vector<uint8_t> reloc_info{
      desc.buffer + desc.buffer_size - desc.reloc_size,
      static_cast<size_t>(desc.reloc_size)};
  // Contrary to the NativeModule, we don't track code size here, because we
  // have no source to attribute it to.
  const int safepoint_table_offset =
      desc.safepoint_table_size == 0 ? 0 : desc.safepoint_table_offset;
  const int handler_table_offset = desc.handler_table_offset;
  const int constant_pool_offset = desc.constant_pool_offset;
  const int code_comments_offset = desc.code_comments_offset;
  const int instr_size = desc.instr_size;
  {
    WritableJitAllocation jit_allocation =
        ThreadIsolation::RegisterJitAllocation(
            reinterpret_cast<Address>(code_space.begin()), code_space.size(),
            ThreadIsolation::JitAllocationType::kWasmCode);
    jit_allocation.CopyCode(0, desc.buffer, desc.instr_size);

    intptr_t delta = code_space.begin() - desc.buffer;
    Address code_start = reinterpret_cast<Address>(code_space.begin());
    Address constant_pool_start = code_start + constant_pool_offset;
    for (WritableRelocIterator it(jit_allocation, code_space, reloc_info,
                                  constant_pool_start, RelocInfo::kApplyMask);
         !it.done(); it.next()) {
      // Wrappers should contain no direct calls to Wasm functions.
      DCHECK(!RelocInfo::IsWasmCall(it.rinfo()->rmode()));
      // Wrappers should not call builtins via a Wasm jump table.
      DCHECK(!RelocInfo::IsWasmStubCall(it.rinfo()->rmode()));
      it.rinfo()->apply(delta);
    }
  }
  FlushInstructionCache(code_space.begin(), code_space.size());
  const int frame_slot_count = result.frame_slot_count;
  const int ool_spill_count = result.ool_spill_count;
  constexpr bool frame_has_feedback_slot = false;
  WasmCode* code = new WasmCode{nullptr /* no NativeModule */,
                                kAnonymousFuncIndex,
                                code_space,
                                frame_slot_count,
                                ool_spill_count,
                                result.tagged_parameter_slots,
                                safepoint_table_offset,
                                handler_table_offset,
                                constant_pool_offset,
                                code_comments_offset,
                                instr_size,
                                result.protected_instructions_data.as_vector(),
                                reloc_info,
                                result.source_positions.as_vector(),
                                result.inlining_positions.as_vector(),
                                result.deopt_data.as_vector(),
                                kind,
                                ExecutionTier::kNone,
                                wasm::kNotForDebugging,
                                frame_has_feedback_slot};
  // The refcount of a WasmCode is initialized to 1. For wrappers, we track
  // all refcounts explicitly, i.e. there will be a call to {IncRef()} that
  // doesn't distinguish between newly compiled and older cached wrappers.
  // So at this point, we lower the refcount to zero (reflecting the fact that
  // there are no references yet), while using a WasmCodeRefScope to make sure
  // that this doesn't cause the WasmCode to be freed immediately.
  WasmCodeRefScope::AddRef(code);
  code->DecRefOnLiveCode();

  code->Validate();
  cache_->entry_map_[key] = code;
  // As an optimization, we assume that wrappers are allocated in increasing
  // memory locations.
  std::map<Address, WasmCode*>& codes = cache_->codes_;
  codes.emplace_hint(codes.end(), code->instruction_start(), code);
  return code;
}

void WasmImportWrapperCache::LogForIsolate(Isolate* isolate) {
  for (const auto& entry : codes_) {
    entry.second->LogCode(isolate, "", -1);  // No source URL, no ScriptId.
  }
}

void WasmImportWrapperCache::Free(std::vector<WasmCode*>& wrappers) {
  base::MutexGuard lock(&mutex_);
  if (entry_map_.empty() || wrappers.empty()) return;
  // {WasmCodeAllocator::FreeCode()} wants code objects to be sorted.
  std::sort(wrappers.begin(), wrappers.end(), [](WasmCode* a, WasmCode* b) {
    return a->instruction_start() < b->instruction_start();
  });
  // Possible future optimization: if the size of {wrappers} is very small,
  // don't allocate the set, use linear scan instead.
  std::unordered_set<WasmCode*> fastset;
  for (WasmCode* wrapper : wrappers) {
    fastset.insert(wrapper);
    codes_.erase(wrapper->instruction_start());
  }
  for (auto it = entry_map_.begin(); it != entry_map_.end();) {
    if (fastset.contains(it->second)) {
      it = entry_map_.erase(it);
    } else {
      it++;
    }
  }
  code_allocator_->FreeCode(base::VectorOf(wrappers));
  for (WasmCode* wrapper : wrappers) {
    delete wrapper;
  }
  // Make sure nobody tries to access stale pointers.
  wrappers.clear();
}

WasmCode* WasmImportWrapperCache::MaybeGet(ImportCallKind kind,
                                           uint32_t canonical_type_index,
                                           int expected_arity,
                                           Suspend suspend) const {
  base::MutexGuard lock(&mutex_);

  auto it =
      entry_map_.find({kind, canonical_type_index, expected_arity, suspend});
  if (it == entry_map_.end()) return nullptr;
  WasmCodeRefScope::AddRef(it->second);
  return it->second;
}

WasmCode* WasmImportWrapperCache::Lookup(Address pc) const {
  // This can be called from the disassembler via `code->MaybePrint()` in
  // `AddWrapper()` above, so we need a recursive mutex.
  base::MutexGuard lock(&mutex_);
  auto iter = codes_.upper_bound(pc);
  if (iter == codes_.begin()) return nullptr;
  --iter;
  WasmCode* candidate = iter->second;
  DCHECK_EQ(candidate->instruction_start(), iter->first);
  if (!candidate->contains(pc)) return nullptr;
  WasmCodeRefScope::AddRef(candidate);
  return candidate;
}

size_t WasmImportWrapperCache::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmImportWrapperCache, 120);
  base::MutexGuard lock(&mutex_);
  return sizeof(WasmImportWrapperCache) + ContentSize(entry_map_) +
         ContentSize(codes_);
}

}  // namespace v8::internal::wasm
