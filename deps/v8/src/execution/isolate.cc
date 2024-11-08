// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"

#include <stdlib.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "include/v8-template.h"
#include "src/api/api-arguments-inl.h"
#include "src/api/api-inl.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"
#include "src/base/hashmap.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/base/sys-info.h"
#include "src/base/utils/random-number-generator.h"
#include "src/baseline/baseline-batch-compiler.h"
#include "src/bigint/bigint.h"
#include "src/builtins/builtins-promise.h"
#include "src/builtins/builtins.h"
#include "src/builtins/constants-table-builder.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr-inl.h"
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"
#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"
#include "src/date/date.h"
#include "src/debug/debug-frames.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/deoptimizer/materialized-object-store.h"
#include "src/diagnostics/basic-block-profiler.h"
#include "src/diagnostics/compilation-statistics.h"
#include "src/execution/frames-inl.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/local-isolate.h"
#include "src/execution/messages.h"
#include "src/execution/microtask-queue.h"
#include "src/execution/protectors-inl.h"
#include "src/execution/simulator.h"
#include "src/execution/tiering-manager.h"
#include "src/execution/v8threads.h"
#include "src/execution/vm-state-inl.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/safepoint.h"
#include "src/ic/stub-cache.h"
#include "src/init/bootstrapper.h"
#include "src/init/setup-isolate.h"
#include "src/init/v8.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter.h"
#include "src/libsampler/sampler.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/logging/metrics.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/backing-store.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/elements.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-function.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/promise-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/prototype.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/source-text-module-inl.h"
#include "src/objects/string-set-inl.h"
#include "src/objects/visitors.h"
#include "src/objects/waiter-queue-node.h"
#include "src/profiler/heap-profiler.h"
#include "src/profiler/tracing-cpu-profiler.h"
#include "src/regexp/regexp-stack.h"
#include "src/roots/static-roots.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/embedded/embedded-file-writer-interface.h"
#include "src/snapshot/read-only-deserializer.h"
#include "src/snapshot/shared-heap-deserializer.h"
#include "src/snapshot/snapshot.h"
#include "src/snapshot/startup-deserializer.h"
#include "src/strings/string-builder-inl.h"
#include "src/strings/string-stream.h"
#include "src/tasks/cancelable-task.h"

#if defined(V8_USE_PERFETTO)
#include "src/tracing/perfetto-logger.h"
#endif  // defined(V8_USE_PERFETTO)

#include "src/tracing/tracing-category-observer.h"
#include "src/utils/address-map.h"
#include "src/utils/ostreams.h"
#include "src/utils/version.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/type-stats.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#include "unicode/locid.h"
#include "unicode/uobject.h"
#endif  // V8_INTL_SUPPORT

#if V8_ENABLE_MAGLEV
#include "src/maglev/maglev-concurrent-dispatcher.h"
#endif  // V8_ENABLE_MAGLEV

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/stacks.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter.h"
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
#include "src/diagnostics/etw-jit-win.h"
#endif

#if defined(V8_OS_WIN64)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN64

#if USE_SIMULATOR
#include "src/execution/simulator-base.h"
#endif

extern "C" const uint8_t v8_Default_embedded_blob_code_[];
extern "C" uint32_t v8_Default_embedded_blob_code_size_;
extern "C" const uint8_t v8_Default_embedded_blob_data_[];
extern "C" uint32_t v8_Default_embedded_blob_data_size_;

namespace v8 {
namespace internal {

#ifdef DEBUG
#define TRACE_ISOLATE(tag)                                                  \
  do {                                                                      \
    if (v8_flags.trace_isolates) {                                          \
      PrintF("Isolate %p (id %d)" #tag "\n", reinterpret_cast<void*>(this), \
             id());                                                         \
    }                                                                       \
  } while (false)
#else
#define TRACE_ISOLATE(tag)
#endif

const uint8_t* DefaultEmbeddedBlobCode() {
  return v8_Default_embedded_blob_code_;
}
uint32_t DefaultEmbeddedBlobCodeSize() {
  return v8_Default_embedded_blob_code_size_;
}
const uint8_t* DefaultEmbeddedBlobData() {
  return v8_Default_embedded_blob_data_;
}
uint32_t DefaultEmbeddedBlobDataSize() {
  return v8_Default_embedded_blob_data_size_;
}

namespace {
// These variables provide access to the current embedded blob without requiring
// an isolate instance. This is needed e.g. by
// InstructionStream::InstructionStart, which may not have access to an isolate
// but still needs to access the embedded blob. The variables are initialized by
// each isolate in Init(). Writes and reads are relaxed since we can guarantee
// that the current thread has initialized these variables before accessing
// them. Different threads may race, but this is fine since they all attempt to
// set the same values of the blob pointer and size.

std::atomic<const uint8_t*> current_embedded_blob_code_(nullptr);
std::atomic<uint32_t> current_embedded_blob_code_size_(0);
std::atomic<const uint8_t*> current_embedded_blob_data_(nullptr);
std::atomic<uint32_t> current_embedded_blob_data_size_(0);

// The various workflows around embedded snapshots are fairly complex. We need
// to support plain old snapshot builds, nosnap builds, and the requirements of
// subtly different serialization tests. There's two related knobs to twiddle:
//
// - The default embedded blob may be overridden by setting the sticky embedded
// blob. This is set automatically whenever we create a new embedded blob.
//
// - Lifecycle management can be either manual or set to refcounting.
//
// A few situations to demonstrate their use:
//
// - A plain old snapshot build neither overrides the default blob nor
// refcounts.
//
// - mksnapshot sets the sticky blob and manually frees the embedded
// blob once done.
//
// - Most serializer tests do the same.
//
// - Nosnapshot builds set the sticky blob and enable refcounting.

// This mutex protects access to the following variables:
// - sticky_embedded_blob_code_
// - sticky_embedded_blob_code_size_
// - sticky_embedded_blob_data_
// - sticky_embedded_blob_data_size_
// - enable_embedded_blob_refcounting_
// - current_embedded_blob_refs_
base::LazyMutex current_embedded_blob_refcount_mutex_ = LAZY_MUTEX_INITIALIZER;

const uint8_t* sticky_embedded_blob_code_ = nullptr;
uint32_t sticky_embedded_blob_code_size_ = 0;
const uint8_t* sticky_embedded_blob_data_ = nullptr;
uint32_t sticky_embedded_blob_data_size_ = 0;

bool enable_embedded_blob_refcounting_ = true;
int current_embedded_blob_refs_ = 0;

const uint8_t* StickyEmbeddedBlobCode() { return sticky_embedded_blob_code_; }
uint32_t StickyEmbeddedBlobCodeSize() {
  return sticky_embedded_blob_code_size_;
}
const uint8_t* StickyEmbeddedBlobData() { return sticky_embedded_blob_data_; }
uint32_t StickyEmbeddedBlobDataSize() {
  return sticky_embedded_blob_data_size_;
}

void SetStickyEmbeddedBlob(const uint8_t* code, uint32_t code_size,
                           const uint8_t* data, uint32_t data_size) {
  sticky_embedded_blob_code_ = code;
  sticky_embedded_blob_code_size_ = code_size;
  sticky_embedded_blob_data_ = data;
  sticky_embedded_blob_data_size_ = data_size;
}

}  // namespace

void DisableEmbeddedBlobRefcounting() {
  base::MutexGuard guard(current_embedded_blob_refcount_mutex_.Pointer());
  enable_embedded_blob_refcounting_ = false;
}

void FreeCurrentEmbeddedBlob() {
  CHECK(!enable_embedded_blob_refcounting_);
  base::MutexGuard guard(current_embedded_blob_refcount_mutex_.Pointer());

  if (StickyEmbeddedBlobCode() == nullptr) return;

  CHECK_EQ(StickyEmbeddedBlobCode(), Isolate::CurrentEmbeddedBlobCode());
  CHECK_EQ(StickyEmbeddedBlobData(), Isolate::CurrentEmbeddedBlobData());

  OffHeapInstructionStream::FreeOffHeapOffHeapInstructionStream(
      const_cast<uint8_t*>(Isolate::CurrentEmbeddedBlobCode()),
      Isolate::CurrentEmbeddedBlobCodeSize(),
      const_cast<uint8_t*>(Isolate::CurrentEmbeddedBlobData()),
      Isolate::CurrentEmbeddedBlobDataSize());

  current_embedded_blob_code_.store(nullptr, std::memory_order_relaxed);
  current_embedded_blob_code_size_.store(0, std::memory_order_relaxed);
  current_embedded_blob_data_.store(nullptr, std::memory_order_relaxed);
  current_embedded_blob_data_size_.store(0, std::memory_order_relaxed);
  sticky_embedded_blob_code_ = nullptr;
  sticky_embedded_blob_code_size_ = 0;
  sticky_embedded_blob_data_ = nullptr;
  sticky_embedded_blob_data_size_ = 0;
}

// static
bool Isolate::CurrentEmbeddedBlobIsBinaryEmbedded() {
  // In some situations, we must be able to rely on the embedded blob being
  // immortal immovable. This is the case if the blob is binary-embedded.
  // See blob lifecycle controls above for descriptions of when the current
  // embedded blob may change (e.g. in tests or mksnapshot). If the blob is
  // binary-embedded, it is immortal immovable.
  const uint8_t* code =
      current_embedded_blob_code_.load(std::memory_order_relaxed);
  if (code == nullptr) return false;
  return code == DefaultEmbeddedBlobCode();
}

void Isolate::SetEmbeddedBlob(const uint8_t* code, uint32_t code_size,
                              const uint8_t* data, uint32_t data_size) {
  CHECK_NOT_NULL(code);
  CHECK_NOT_NULL(data);

  embedded_blob_code_ = code;
  embedded_blob_code_size_ = code_size;
  embedded_blob_data_ = data;
  embedded_blob_data_size_ = data_size;
  current_embedded_blob_code_.store(code, std::memory_order_relaxed);
  current_embedded_blob_code_size_.store(code_size, std::memory_order_relaxed);
  current_embedded_blob_data_.store(data, std::memory_order_relaxed);
  current_embedded_blob_data_size_.store(data_size, std::memory_order_relaxed);

#ifdef DEBUG
  // Verify that the contents of the embedded blob are unchanged from
  // serialization-time, just to ensure the compiler isn't messing with us.
  EmbeddedData d = EmbeddedData::FromBlob();
  if (d.EmbeddedBlobDataHash() != d.CreateEmbeddedBlobDataHash()) {
    FATAL(
        "Embedded blob data section checksum verification failed. This "
        "indicates that the embedded blob has been modified since compilation "
        "time.");
  }
  if (v8_flags.text_is_readable) {
    if (d.EmbeddedBlobCodeHash() != d.CreateEmbeddedBlobCodeHash()) {
      FATAL(
          "Embedded blob code section checksum verification failed. This "
          "indicates that the embedded blob has been modified since "
          "compilation time. A common cause is a debugging breakpoint set "
          "within builtin code.");
    }
  }
#endif  // DEBUG
}

void Isolate::ClearEmbeddedBlob() {
  CHECK(enable_embedded_blob_refcounting_);
  CHECK_EQ(embedded_blob_code_, CurrentEmbeddedBlobCode());
  CHECK_EQ(embedded_blob_code_, StickyEmbeddedBlobCode());
  CHECK_EQ(embedded_blob_data_, CurrentEmbeddedBlobData());
  CHECK_EQ(embedded_blob_data_, StickyEmbeddedBlobData());

  embedded_blob_code_ = nullptr;
  embedded_blob_code_size_ = 0;
  embedded_blob_data_ = nullptr;
  embedded_blob_data_size_ = 0;
  current_embedded_blob_code_.store(nullptr, std::memory_order_relaxed);
  current_embedded_blob_code_size_.store(0, std::memory_order_relaxed);
  current_embedded_blob_data_.store(nullptr, std::memory_order_relaxed);
  current_embedded_blob_data_size_.store(0, std::memory_order_relaxed);
  sticky_embedded_blob_code_ = nullptr;
  sticky_embedded_blob_code_size_ = 0;
  sticky_embedded_blob_data_ = nullptr;
  sticky_embedded_blob_data_size_ = 0;
}

const uint8_t* Isolate::embedded_blob_code() const {
  return embedded_blob_code_;
}
uint32_t Isolate::embedded_blob_code_size() const {
  return embedded_blob_code_size_;
}
const uint8_t* Isolate::embedded_blob_data() const {
  return embedded_blob_data_;
}
uint32_t Isolate::embedded_blob_data_size() const {
  return embedded_blob_data_size_;
}

// static
const uint8_t* Isolate::CurrentEmbeddedBlobCode() {
  return current_embedded_blob_code_.load(std::memory_order_relaxed);
}

// static
uint32_t Isolate::CurrentEmbeddedBlobCodeSize() {
  return current_embedded_blob_code_size_.load(std::memory_order_relaxed);
}

// static
const uint8_t* Isolate::CurrentEmbeddedBlobData() {
  return current_embedded_blob_data_.load(std::memory_order_relaxed);
}

// static
uint32_t Isolate::CurrentEmbeddedBlobDataSize() {
  return current_embedded_blob_data_size_.load(std::memory_order_relaxed);
}

// static
base::AddressRegion Isolate::GetShortBuiltinsCallRegion() {
  // Update calculations below if the assert fails.
  static_assert(kMaxPCRelativeCodeRangeInMB <= 4096);
  if (kMaxPCRelativeCodeRangeInMB == 0) {
    // Return empty region if pc-relative calls/jumps are not supported.
    return base::AddressRegion(kNullAddress, 0);
  }
  constexpr size_t max_size = std::numeric_limits<size_t>::max();
  if (uint64_t{kMaxPCRelativeCodeRangeInMB} * MB > max_size) {
    // The whole addressable space is reachable with pc-relative calls/jumps.
    return base::AddressRegion(kNullAddress, max_size);
  }
  constexpr size_t radius = kMaxPCRelativeCodeRangeInMB * MB;

  DCHECK_LT(CurrentEmbeddedBlobCodeSize(), radius);
  Address embedded_blob_code_start =
      reinterpret_cast<Address>(CurrentEmbeddedBlobCode());
  if (embedded_blob_code_start == kNullAddress) {
    // Return empty region if there's no embedded blob.
    return base::AddressRegion(kNullAddress, 0);
  }
  Address embedded_blob_code_end =
      embedded_blob_code_start + CurrentEmbeddedBlobCodeSize();
  Address region_start =
      (embedded_blob_code_end > radius) ? (embedded_blob_code_end - radius) : 0;
  Address region_end = embedded_blob_code_start + radius;
  if (region_end < embedded_blob_code_start) {
    region_end = static_cast<Address>(-1);
  }
  return base::AddressRegion(region_start, region_end - region_start);
}

size_t Isolate::HashIsolateForEmbeddedBlob() {
  DCHECK(builtins_.is_initialized());
  DCHECK(Builtins::AllBuiltinsAreIsolateIndependent());

  DisallowGarbageCollection no_gc;

  static constexpr size_t kSeed = 0;
  size_t hash = kSeed;

  // Hash static entries of the roots table.
  hash = base::hash_combine(hash, V8_STATIC_ROOTS_BOOL);
#if V8_STATIC_ROOTS_BOOL
  hash = base::hash_combine(hash,
                            static_cast<int>(RootIndex::kReadOnlyRootsCount));
  RootIndex i = RootIndex::kFirstReadOnlyRoot;
  for (auto ptr : StaticReadOnlyRootsPointerTable) {
    hash = base::hash_combine(ptr, hash);
    ++i;
  }
#endif  // V8_STATIC_ROOTS_BOOL

  // Hash data sections of builtin code objects.
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    Tagged<Code> code = builtins()->code(builtin);

    DCHECK(Internals::HasHeapObjectTag(code.ptr()));
    uint8_t* const code_ptr = reinterpret_cast<uint8_t*>(code.address());

    // These static asserts ensure we don't miss relevant fields. We don't hash
    // instruction_start, but other data fields must remain the same.
    static_assert(Code::kEndOfStrongFieldsOffset ==
                  Code::kInstructionStartOffset);
#ifndef V8_ENABLE_SANDBOX
    static_assert(Code::kInstructionStartOffsetEnd + 1 == Code::kFlagsOffset);
#endif
    static_assert(Code::kFlagsOffsetEnd + 1 == Code::kInstructionSizeOffset);
    static_assert(Code::kInstructionSizeOffsetEnd + 1 ==
                  Code::kMetadataSizeOffset);
    static_assert(Code::kMetadataSizeOffsetEnd + 1 ==
                  Code::kInlinedBytecodeSizeOffset);
    static_assert(Code::kInlinedBytecodeSizeOffsetEnd + 1 ==
                  Code::kOsrOffsetOffset);
    static_assert(Code::kOsrOffsetOffsetEnd + 1 ==
                  Code::kHandlerTableOffsetOffset);
    static_assert(Code::kHandlerTableOffsetOffsetEnd + 1 ==
                  Code::kUnwindingInfoOffsetOffset);
    static_assert(Code::kUnwindingInfoOffsetOffsetEnd + 1 ==
                  Code::kConstantPoolOffsetOffset);
    static_assert(Code::kConstantPoolOffsetOffsetEnd + 1 ==
                  Code::kCodeCommentsOffsetOffset);
    static_assert(Code::kCodeCommentsOffsetOffsetEnd + 1 ==
                  Code::kParameterCountOffset);
    static_assert(Code::kParameterCountOffsetEnd + 1 == Code::kBuiltinIdOffset);
    static_assert(Code::kBuiltinIdOffsetEnd + 1 == Code::kUnalignedSize);
    static constexpr int kStartOffset = Code::kFlagsOffset;

    for (int j = kStartOffset; j < Code::kUnalignedSize; j++) {
      hash = base::hash_combine(hash, size_t{code_ptr[j]});
    }
  }

  // The builtins constants table is also tightly tied to embedded builtins.
  hash = base::hash_combine(
      hash, static_cast<size_t>(heap_.builtins_constants_table()->length()));

  return hash;
}

Isolate* Isolate::process_wide_shared_space_isolate_{nullptr};

thread_local Isolate::PerIsolateThreadData* g_current_per_isolate_thread_data_
    V8_CONSTINIT = nullptr;
thread_local Isolate* g_current_isolate_ V8_CONSTINIT = nullptr;

namespace {
// A global counter for all generated Isolates, might overflow.
std::atomic<int> isolate_counter{0};
}  // namespace

Isolate::PerIsolateThreadData*
Isolate::FindOrAllocatePerThreadDataForThisThread() {
  ThreadId thread_id = ThreadId::Current();
  PerIsolateThreadData* per_thread = nullptr;
  {
    base::MutexGuard lock_guard(&thread_data_table_mutex_);
    per_thread = thread_data_table_.Lookup(thread_id);
    if (per_thread == nullptr) {
      if (v8_flags.adjust_os_scheduling_parameters) {
        base::OS::AdjustSchedulingParams();
      }
      per_thread = new PerIsolateThreadData(this, thread_id);
      thread_data_table_.Insert(per_thread);
    }
    DCHECK(thread_data_table_.Lookup(thread_id) == per_thread);
  }
  return per_thread;
}

void Isolate::DiscardPerThreadDataForThisThread() {
  ThreadId thread_id = ThreadId::TryGetCurrent();
  if (thread_id.IsValid()) {
    DCHECK_NE(thread_manager_->mutex_owner_.load(std::memory_order_relaxed),
              thread_id);
    base::MutexGuard lock_guard(&thread_data_table_mutex_);
    PerIsolateThreadData* per_thread = thread_data_table_.Lookup(thread_id);
    if (per_thread) {
      DCHECK(!per_thread->thread_state_);
      thread_data_table_.Remove(per_thread);
    }
  }
}

Isolate::PerIsolateThreadData* Isolate::FindPerThreadDataForThisThread() {
  ThreadId thread_id = ThreadId::Current();
  return FindPerThreadDataForThread(thread_id);
}

Isolate::PerIsolateThreadData* Isolate::FindPerThreadDataForThread(
    ThreadId thread_id) {
  PerIsolateThreadData* per_thread = nullptr;
  {
    base::MutexGuard lock_guard(&thread_data_table_mutex_);
    per_thread = thread_data_table_.Lookup(thread_id);
  }
  return per_thread;
}

void Isolate::InitializeOncePerProcess() { Heap::InitializeOncePerProcess(); }

Address Isolate::get_address_from_id(IsolateAddressId id) {
  return isolate_addresses_[id];
}

char* Isolate::Iterate(RootVisitor* v, char* thread_storage) {
  ThreadLocalTop* thread = reinterpret_cast<ThreadLocalTop*>(thread_storage);
  Iterate(v, thread);
  // Normally, ThreadLocalTop::topmost_script_having_context_ is visited weakly
  // but in order to simplify handling of frozen threads we just clear it.
  // Otherwise, we'd need to traverse the thread_storage again just to find this
  // one field.
  thread->topmost_script_having_context_ = Context();
  return thread_storage + sizeof(ThreadLocalTop);
}

void Isolate::IterateThread(ThreadVisitor* v, char* t) {
  ThreadLocalTop* thread = reinterpret_cast<ThreadLocalTop*>(t);
  v->VisitThread(this, thread);
}

void Isolate::Iterate(RootVisitor* v, ThreadLocalTop* thread) {
  // Visit the roots from the top for a given thread.
  v->VisitRootPointer(Root::kStackRoots, nullptr,
                      FullObjectSlot(&thread->exception_));
  v->VisitRootPointer(Root::kStackRoots, nullptr,
                      FullObjectSlot(&thread->pending_message_));
  v->VisitRootPointer(Root::kStackRoots, nullptr,
                      FullObjectSlot(&thread->context_));

  for (v8::TryCatch* block = thread->try_catch_handler_; block != nullptr;
       block = block->next_) {
    // TODO(3770): Make TryCatch::exception_ an Address (and message_obj_ too).
    v->VisitRootPointer(
        Root::kStackRoots, nullptr,
        FullObjectSlot(reinterpret_cast<Address>(&(block->exception_))));
    v->VisitRootPointer(
        Root::kStackRoots, nullptr,
        FullObjectSlot(reinterpret_cast<Address>(&(block->message_obj_))));
  }

  v->VisitRootPointer(
      Root::kStackRoots, nullptr,
      FullObjectSlot(continuation_preserved_embedder_data_address()));

  // Iterate over pointers on native execution stack.
#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmCodeRefScope wasm_code_ref_scope;

  for (const std::unique_ptr<wasm::StackMemory>& stack : wasm_stacks_) {
    if (stack->IsActive()) {
      continue;
    }
    for (StackFrameIterator it(this, stack.get()); !it.done(); it.Advance()) {
      it.frame()->Iterate(v);
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  for (StackFrameIterator it(this, thread); !it.done(); it.Advance()) {
    it.frame()->Iterate(v);
  }
}

void Isolate::Iterate(RootVisitor* v) {
  ThreadLocalTop* current_t = thread_local_top();
  Iterate(v, current_t);
}

void Isolate::RegisterTryCatchHandler(v8::TryCatch* that) {
  thread_local_top()->try_catch_handler_ = that;
}

void Isolate::UnregisterTryCatchHandler(v8::TryCatch* that) {
  DCHECK_EQ(thread_local_top()->try_catch_handler_, that);
  thread_local_top()->try_catch_handler_ = that->next_;
  SimulatorStack::UnregisterJSStackComparableAddress(this);
}

Handle<String> Isolate::StackTraceString() {
  if (stack_trace_nesting_level_ == 0) {
    stack_trace_nesting_level_++;
    HeapStringAllocator allocator;
    StringStream::ClearMentionedObjectCache(this);
    StringStream accumulator(&allocator);
    incomplete_message_ = &accumulator;
    PrintStack(&accumulator);
    Handle<String> stack_trace = accumulator.ToString(this);
    incomplete_message_ = nullptr;
    stack_trace_nesting_level_ = 0;
    return stack_trace;
  } else if (stack_trace_nesting_level_ == 1) {
    stack_trace_nesting_level_++;
    base::OS::PrintError(
        "\n\nAttempt to print stack while printing stack (double fault)\n");
    base::OS::PrintError(
        "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message_->OutputToStdOut();
    return factory()->empty_string();
  } else {
    base::OS::Abort();
  }
}

void Isolate::PushStackTraceAndDie(void* ptr1, void* ptr2, void* ptr3,
                                   void* ptr4, void* ptr5, void* ptr6) {
  StackTraceFailureMessage message(this,
                                   StackTraceFailureMessage::kIncludeStackTrace,
                                   ptr1, ptr2, ptr3, ptr4, ptr5, ptr6);
  message.Print();
  base::OS::Abort();
}

void Isolate::PushParamsAndDie(void* ptr1, void* ptr2, void* ptr3, void* ptr4,
                               void* ptr5, void* ptr6) {
  StackTraceFailureMessage message(
      this, StackTraceFailureMessage::kDontIncludeStackTrace, ptr1, ptr2, ptr3,
      ptr4, ptr5, ptr6);
  message.Print();
  base::OS::Abort();
}

void Isolate::PushStackTraceAndContinue(void* ptr1, void* ptr2, void* ptr3,
                                        void* ptr4, void* ptr5, void* ptr6) {
  StackTraceFailureMessage message(this,
                                   StackTraceFailureMessage::kIncludeStackTrace,
                                   ptr1, ptr2, ptr3, ptr4, ptr5, ptr6);
  message.Print();
  V8::GetCurrentPlatform()->DumpWithoutCrashing();
}

void Isolate::PushParamsAndContinue(void* ptr1, void* ptr2, void* ptr3,
                                    void* ptr4, void* ptr5, void* ptr6) {
  StackTraceFailureMessage message(
      this, StackTraceFailureMessage::kDontIncludeStackTrace, ptr1, ptr2, ptr3,
      ptr4, ptr5, ptr6);
  message.Print();
  V8::GetCurrentPlatform()->DumpWithoutCrashing();
}

void StackTraceFailureMessage::Print() volatile {
  // Print the details of this failure message object, including its own address
  // to force stack allocation.
  base::OS::PrintError(
      "Stacktrace:\n    ptr1=%p\n    ptr2=%p\n    ptr3=%p\n    ptr4=%p\n    "
      "ptr5=%p\n    ptr6=%p\n    failure_message_object=%p\n%s",
      ptr1_, ptr2_, ptr3_, ptr4_, ptr5_, ptr6_, this, &js_stack_trace_[0]);
}

StackTraceFailureMessage::StackTraceFailureMessage(
    Isolate* isolate, StackTraceFailureMessage::StackTraceMode mode, void* ptr1,
    void* ptr2, void* ptr3, void* ptr4, void* ptr5, void* ptr6) {
  isolate_ = isolate;
  ptr1_ = ptr1;
  ptr2_ = ptr2;
  ptr3_ = ptr3;
  ptr4_ = ptr4;
  ptr5_ = ptr5;
  ptr6_ = ptr6;
  // Write a stracktrace into the {js_stack_trace_} buffer.
  const size_t buffer_length = arraysize(js_stack_trace_);
  memset(&js_stack_trace_, 0, buffer_length);
  memset(&code_objects_, 0, sizeof(code_objects_));
  if (mode == kIncludeStackTrace) {
    FixedStringAllocator fixed(&js_stack_trace_[0], buffer_length - 1);
    StringStream accumulator(&fixed, StringStream::kPrintObjectConcise);
    isolate->PrintStack(&accumulator, Isolate::kPrintStackVerbose);
    // Keeping a reference to the last code objects to increase likelihood that
    // they get included in the minidump.
    const size_t code_objects_length = arraysize(code_objects_);
    size_t i = 0;
    StackFrameIterator it(isolate);
    for (; !it.done() && i < code_objects_length; it.Advance()) {
      code_objects_[i++] =
          reinterpret_cast<void*>(it.frame()->unchecked_code().ptr());
    }
  }
}

bool NoExtension(const v8::FunctionCallbackInfo<v8::Value>&) { return false; }

namespace {

bool IsBuiltinFunction(Isolate* isolate, Tagged<HeapObject> object,
                       Builtin builtin) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> const function = Cast<JSFunction>(object);
  // Currently we have to use full pointer comparison here as builtin Code
  // objects are still inside the sandbox while runtime-generated Code objects
  // are in trusted space.
  static_assert(!kAllCodeObjectsLiveInTrustedSpace);
  return function->code(isolate).SafeEquals(isolate->builtins()->code(builtin));
}

// Check if the function is one of the known async function or
// async generator fulfill handlers.
bool IsBuiltinAsyncFulfillHandler(Isolate* isolate, Tagged<HeapObject> object) {
  return IsBuiltinFunction(isolate, object,
                           Builtin::kAsyncFunctionAwaitResolveClosure) ||
         IsBuiltinFunction(isolate, object,
                           Builtin::kAsyncGeneratorAwaitResolveClosure) ||
         IsBuiltinFunction(
             isolate, object,
             Builtin::kAsyncGeneratorYieldWithAwaitResolveClosure);
}

// Check if the function is one of the known async function or
// async generator fulfill handlers.
bool IsBuiltinAsyncRejectHandler(Isolate* isolate, Tagged<HeapObject> object) {
  return IsBuiltinFunction(isolate, object,
                           Builtin::kAsyncFunctionAwaitRejectClosure) ||
         IsBuiltinFunction(isolate, object,
                           Builtin::kAsyncGeneratorAwaitRejectClosure);
}

// Check if the function is one of the known builtin rejection handlers that
// rethrows the exception instead of catching it.
bool IsBuiltinForwardingRejectHandler(Isolate* isolate,
                                      Tagged<HeapObject> object) {
  return IsBuiltinFunction(isolate, object, Builtin::kPromiseCatchFinally) ||
         IsBuiltinFunction(isolate, object,
                           Builtin::kAsyncFromSyncIteratorCloseSyncAndRethrow);
}

MaybeHandle<JSGeneratorObject> TryGetAsyncGenerator(
    Isolate* isolate, DirectHandle<PromiseReaction> reaction) {
  // Check if the {reaction} has one of the known async function or
  // async generator continuations as its fulfill handler.
  if (IsBuiltinAsyncFulfillHandler(isolate, reaction->fulfill_handler())) {
    // Now peek into the handlers' AwaitContext to get to
    // the JSGeneratorObject for the async function.
    DirectHandle<Context> context(
        Cast<JSFunction>(reaction->fulfill_handler())->context(), isolate);
    Handle<JSGeneratorObject> generator_object(
        Cast<JSGeneratorObject>(context->extension()), isolate);
    return generator_object;
  }
  return MaybeHandle<JSGeneratorObject>();
}

#if V8_ENABLE_WEBASSEMBLY
MaybeHandle<WasmSuspenderObject> TryGetWasmSuspender(
    Isolate* isolate, Tagged<HeapObject> handler) {
  // Check if the {handler} is WasmResume.
  if (IsBuiltinFunction(isolate, handler, Builtin::kWasmResume)) {
    // Now peek into the handlers' AwaitContext to get to
    // the JSGeneratorObject for the async function.
    Tagged<SharedFunctionInfo> shared = Cast<JSFunction>(handler)->shared();
    if (shared->HasWasmResumeData()) {
      return handle(shared->wasm_resume_data()->suspender(), isolate);
    }
  }
  return MaybeHandle<WasmSuspenderObject>();
}
#endif  // V8_ENABLE_WEBASSEMBLY

int GetGeneratorBytecodeOffset(
    DirectHandle<JSGeneratorObject> generator_object) {
  // The stored bytecode offset is relative to a different base than what
  // is used in the source position table, hence the subtraction.
  return Smi::ToInt(generator_object->input_or_debug_pos()) -
         (BytecodeArray::kHeaderSize - kHeapObjectTag);
}

class CallSiteBuilder {
 public:
  CallSiteBuilder(Isolate* isolate, FrameSkipMode mode, int limit,
                  Handle<Object> caller)
      : isolate_(isolate),
        mode_(mode),
        limit_(limit),
        caller_(caller),
        skip_next_frame_(mode != SKIP_NONE) {
    DCHECK_IMPLIES(mode_ == SKIP_UNTIL_SEEN, IsJSFunction(*caller_));
    // Modern web applications are usually built with multiple layers of
    // framework and library code, and stack depth tends to be more than
    // a dozen frames, so we over-allocate a bit here to avoid growing
    // the elements array in the common case.
    elements_ = isolate->factory()->NewFixedArray(std::min(64, limit));
  }

  bool Visit(FrameSummary const& summary) {
    if (Full()) return false;
#if V8_ENABLE_WEBASSEMBLY
#if V8_ENABLE_DRUMBRAKE
    if (summary.IsWasmInterpreted()) {
      AppendWasmInterpretedFrame(summary.AsWasmInterpreted());
      return true;
      // FrameSummary::IsWasm() should be renamed FrameSummary::IsWasmCompiled
      // to be more precise, but we'll leave it as it is to try to reduce merge
      // churn.
    } else {
#endif  // V8_ENABLE_DRUMBRAKE
      if (summary.IsWasm()) {
        AppendWasmFrame(summary.AsWasm());
        return true;
      }
#if V8_ENABLE_DRUMBRAKE
    }
#endif  // V8_ENABLE_DRUMBRAKE
    if (summary.IsWasmInlined()) {
      AppendWasmInlinedFrame(summary.AsWasmInlined());
      return true;
    }
    if (summary.IsBuiltin()) {
      AppendBuiltinFrame(summary.AsBuiltin());
      return true;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    AppendJavaScriptFrame(summary.AsJavaScript());
    return true;
  }

  void AppendAsyncFrame(DirectHandle<JSGeneratorObject> generator_object) {
    DirectHandle<JSFunction> function(generator_object->function(), isolate_);
    if (!IsVisibleInStackTrace(function)) return;
    int flags = CallSiteInfo::kIsAsync;
    if (IsStrictFrame(function)) flags |= CallSiteInfo::kIsStrict;

    Handle<JSAny> receiver(generator_object->receiver(), isolate_);
    DirectHandle<BytecodeArray> code(
        function->shared()->GetBytecodeArray(isolate_), isolate_);
    int offset = GetGeneratorBytecodeOffset(generator_object);

    DirectHandle<FixedArray> parameters =
        isolate_->factory()->empty_fixed_array();
    if (V8_UNLIKELY(v8_flags.detailed_error_stack_trace)) {
      parameters = isolate_->factory()->CopyFixedArrayUpTo(
          handle(generator_object->parameters_and_registers(), isolate_),
          function->shared()
              ->internal_formal_parameter_count_without_receiver());
    }

    AppendFrame(receiver, function, code, offset, flags, parameters);
  }

  void AppendPromiseCombinatorFrame(DirectHandle<JSFunction> element_function,
                                    DirectHandle<JSFunction> combinator) {
    if (!IsVisibleInStackTrace(combinator)) return;
    int flags =
        CallSiteInfo::kIsAsync | CallSiteInfo::kIsSourcePositionComputed;

    Handle<JSFunction> receiver(
        combinator->native_context()->promise_function(), isolate_);
    DirectHandle<Code> code(combinator->code(isolate_), isolate_);

    // TODO(mmarchini) save Promises list from the Promise combinator
    DirectHandle<FixedArray> parameters =
        isolate_->factory()->empty_fixed_array();

    // We store the offset of the promise into the element function's
    // hash field for element callbacks.
    int promise_index = Smi::ToInt(element_function->GetIdentityHash()) - 1;

    AppendFrame(receiver, combinator, code, promise_index, flags, parameters);
  }

  void AppendJavaScriptFrame(
      FrameSummary::JavaScriptFrameSummary const& summary) {
    // Filter out internal frames that we do not want to show.
    if (!IsVisibleInStackTrace(summary.function())) return;

    int flags = 0;
    DirectHandle<JSFunction> function = summary.function();
    if (IsStrictFrame(function)) flags |= CallSiteInfo::kIsStrict;
    if (summary.is_constructor()) flags |= CallSiteInfo::kIsConstructor;

    AppendFrame(Cast<UnionOf<JSAny, Hole>>(summary.receiver()), function,
                summary.abstract_code(), summary.code_offset(), flags,
                summary.parameters());
  }

#if V8_ENABLE_WEBASSEMBLY
  void AppendWasmFrame(FrameSummary::WasmFrameSummary const& summary) {
    if (summary.code()->kind() != wasm::WasmCode::kWasmFunction) return;
    Handle<WasmInstanceObject> instance = summary.wasm_instance();
    int flags = CallSiteInfo::kIsWasm;
    if (instance->module_object()->is_asm_js()) {
      flags |= CallSiteInfo::kIsAsmJsWasm;
      if (summary.at_to_number_conversion()) {
        flags |= CallSiteInfo::kIsAsmJsAtNumberConversion;
      }
    }

    DirectHandle<HeapObject> code = isolate_->factory()->undefined_value();
    AppendFrame(instance,
                handle(Smi::FromInt(summary.function_index()), isolate_), code,
                summary.code_offset(), flags,
                isolate_->factory()->empty_fixed_array());
  }

#if V8_ENABLE_DRUMBRAKE
  void AppendWasmInterpretedFrame(
      FrameSummary::WasmInterpretedFrameSummary const& summary) {
    Handle<WasmInstanceObject> instance = summary.wasm_instance();
    int flags = CallSiteInfo::kIsWasm | CallSiteInfo::kIsWasmInterpretedFrame;
    DCHECK(!instance->module_object()->is_asm_js());
    // We don't have any code object in the interpreter, so we pass 'undefined'.
    auto code = isolate_->factory()->undefined_value();
    AppendFrame(instance,
                handle(Smi::FromInt(summary.function_index()), isolate_), code,
                summary.byte_offset(), flags,
                isolate_->factory()->empty_fixed_array());
  }
#endif  // V8_ENABLE_DRUMBRAKE

  void AppendWasmInlinedFrame(
      FrameSummary::WasmInlinedFrameSummary const& summary) {
    DirectHandle<HeapObject> code = isolate_->factory()->undefined_value();
    int flags = CallSiteInfo::kIsWasm;
    AppendFrame(summary.wasm_instance(),
                handle(Smi::FromInt(summary.function_index()), isolate_), code,
                summary.code_offset(), flags,
                isolate_->factory()->empty_fixed_array());
  }

  void AppendBuiltinFrame(FrameSummary::BuiltinFrameSummary const& summary) {
    Builtin builtin = summary.builtin();
    DirectHandle<Code> code = isolate_->builtins()->code_handle(builtin);
    DirectHandle<Smi> function(Smi::FromInt(static_cast<int>(builtin)),
                               isolate_);
    int flags = CallSiteInfo::kIsBuiltin;
    AppendFrame(Cast<UnionOf<JSAny, Hole>>(summary.receiver()), function, code,
                summary.code_offset(), flags,
                isolate_->factory()->empty_fixed_array());
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  bool Full() { return index_ >= limit_; }

  Handle<FixedArray> Build() {
    return FixedArray::RightTrimOrEmpty(isolate_, elements_, index_);
  }

 private:
  // Poison stack frames below the first strict mode frame.
  // The stack trace API should not expose receivers and function
  // objects on frames deeper than the top-most one with a strict mode
  // function.
  bool IsStrictFrame(DirectHandle<JSFunction> function) {
    if (!encountered_strict_function_) {
      encountered_strict_function_ =
          is_strict(function->shared()->language_mode());
    }
    return encountered_strict_function_;
  }

  // Determines whether the given stack frame should be displayed in a stack
  // trace.
  bool IsVisibleInStackTrace(DirectHandle<JSFunction> function) {
    return ShouldIncludeFrame(function) && IsNotHidden(function);
  }

  // This mechanism excludes a number of uninteresting frames from the stack
  // trace. This can be be the first frame (which will be a builtin-exit frame
  // for the error constructor builtin) or every frame until encountering a
  // user-specified function.
  bool ShouldIncludeFrame(DirectHandle<JSFunction> function) {
    switch (mode_) {
      case SKIP_NONE:
        return true;
      case SKIP_FIRST:
        if (!skip_next_frame_) return true;
        skip_next_frame_ = false;
        return false;
      case SKIP_UNTIL_SEEN:
        if (skip_next_frame_ && (*function == *caller_)) {
          skip_next_frame_ = false;
          return false;
        }
        return !skip_next_frame_;
    }
    UNREACHABLE();
  }

  bool IsNotHidden(DirectHandle<JSFunction> function) {
    // TODO(szuend): Remove this check once the flag is enabled
    //               by default.
    if (!v8_flags.experimental_stack_trace_frames &&
        function->shared()->IsApiFunction()) {
      return false;
    }
    // Functions defined not in user scripts are not visible unless directly
    // exposed, in which case the native flag is set.
    // The --builtins-in-stack-traces command line flag allows including
    // internal call sites in the stack trace for debugging purposes.
    if (!v8_flags.builtins_in_stack_traces &&
        !function->shared()->IsUserJavaScript()) {
      return function->shared()->native() ||
             function->shared()->IsApiFunction();
    }
    return true;
  }

  void AppendFrame(Handle<UnionOf<JSAny, Hole>> receiver_or_instance,
                   DirectHandle<UnionOf<Smi, JSFunction>> function,
                   DirectHandle<HeapObject> code, int offset, int flags,
                   DirectHandle<FixedArray> parameters) {
    if (IsTheHole(*receiver_or_instance, isolate_)) {
      // TODO(jgruber): Fix all cases in which frames give us a hole value
      // (e.g. the receiver in RegExp constructor frames).
      receiver_or_instance = isolate_->factory()->undefined_value();
    }
    auto info = isolate_->factory()->NewCallSiteInfo(
        Cast<JSAny>(receiver_or_instance), function, code, offset, flags,
        parameters);
    elements_ = FixedArray::SetAndGrow(isolate_, elements_, index_++, info);
  }

  Isolate* isolate_;
  const FrameSkipMode mode_;
  int index_ = 0;
  const int limit_;
  const Handle<Object> caller_;
  bool skip_next_frame_;
  bool encountered_strict_function_ = false;
  Handle<FixedArray> elements_;
};

void CaptureAsyncStackTrace(Isolate* isolate, DirectHandle<JSPromise> promise,
                            CallSiteBuilder* builder) {
  while (!builder->Full()) {
    // Check that the {promise} is not settled.
    if (promise->status() != Promise::kPending) return;

    // Check that we have exactly one PromiseReaction on the {promise}.
    if (!IsPromiseReaction(promise->reactions())) return;
    DirectHandle<PromiseReaction> reaction(
        Cast<PromiseReaction>(promise->reactions()), isolate);
    if (!IsSmi(reaction->next())) return;

    Handle<JSGeneratorObject> generator_object;

    if (TryGetAsyncGenerator(isolate, reaction).ToHandle(&generator_object)) {
      CHECK(generator_object->is_suspended());

      // Append async frame corresponding to the {generator_object}.
      builder->AppendAsyncFrame(generator_object);

      // Try to continue from here.
      if (IsJSAsyncFunctionObject(*generator_object)) {
        auto async_function_object =
            Cast<JSAsyncFunctionObject>(generator_object);
        promise = handle(async_function_object->promise(), isolate);
      } else {
        auto async_generator_object =
            Cast<JSAsyncGeneratorObject>(generator_object);
        if (IsUndefined(async_generator_object->queue(), isolate)) return;
        DirectHandle<AsyncGeneratorRequest> async_generator_request(
            Cast<AsyncGeneratorRequest>(async_generator_object->queue()),
            isolate);
        promise = handle(Cast<JSPromise>(async_generator_request->promise()),
                         isolate);
      }
    } else if (IsBuiltinFunction(isolate, reaction->fulfill_handler(),
                                 Builtin::kPromiseAllResolveElementClosure)) {
      DirectHandle<JSFunction> function(
          Cast<JSFunction>(reaction->fulfill_handler()), isolate);
      DirectHandle<Context> context(function->context(), isolate);
      DirectHandle<JSFunction> combinator(
          context->native_context()->promise_all(), isolate);
      builder->AppendPromiseCombinatorFrame(function, combinator);

      if (IsNativeContext(*context)) {
        // NativeContext is used as a marker that the closure was already
        // called. We can't access the reject element context any more.
        return;
      }

      // Now peek into the Promise.all() resolve element context to
      // find the promise capability that's being resolved when all
      // the concurrent promises resolve.
      int const index =
          PromiseBuiltins::kPromiseAllResolveElementCapabilitySlot;
      DirectHandle<PromiseCapability> capability(
          Cast<PromiseCapability>(context->get(index)), isolate);
      if (!IsJSPromise(capability->promise())) return;
      promise = handle(Cast<JSPromise>(capability->promise()), isolate);
    } else if (IsBuiltinFunction(
                   isolate, reaction->fulfill_handler(),
                   Builtin::kPromiseAllSettledResolveElementClosure)) {
      DirectHandle<JSFunction> function(
          Cast<JSFunction>(reaction->fulfill_handler()), isolate);
      DirectHandle<Context> context(function->context(), isolate);
      DirectHandle<JSFunction> combinator(
          context->native_context()->promise_all_settled(), isolate);
      builder->AppendPromiseCombinatorFrame(function, combinator);

      if (IsNativeContext(*context)) {
        // NativeContext is used as a marker that the closure was already
        // called. We can't access the reject element context any more.
        return;
      }

      // Now peek into the Promise.allSettled() resolve element context to
      // find the promise capability that's being resolved when all
      // the concurrent promises resolve.
      int const index =
          PromiseBuiltins::kPromiseAllResolveElementCapabilitySlot;
      DirectHandle<PromiseCapability> capability(
          Cast<PromiseCapability>(context->get(index)), isolate);
      if (!IsJSPromise(capability->promise())) return;
      promise = handle(Cast<JSPromise>(capability->promise()), isolate);
    } else if (IsBuiltinFunction(isolate, reaction->reject_handler(),
                                 Builtin::kPromiseAnyRejectElementClosure)) {
      DirectHandle<JSFunction> function(
          Cast<JSFunction>(reaction->reject_handler()), isolate);
      DirectHandle<Context> context(function->context(), isolate);
      DirectHandle<JSFunction> combinator(
          context->native_context()->promise_any(), isolate);
      builder->AppendPromiseCombinatorFrame(function, combinator);

      if (IsNativeContext(*context)) {
        // NativeContext is used as a marker that the closure was already
        // called. We can't access the reject element context any more.
        return;
      }

      // Now peek into the Promise.any() reject element context to
      // find the promise capability that's being resolved when any of
      // the concurrent promises resolve.
      int const index = PromiseBuiltins::kPromiseAnyRejectElementCapabilitySlot;
      DirectHandle<PromiseCapability> capability(
          Cast<PromiseCapability>(context->get(index)), isolate);
      if (!IsJSPromise(capability->promise())) return;
      promise = handle(Cast<JSPromise>(capability->promise()), isolate);
    } else if (IsBuiltinFunction(isolate, reaction->fulfill_handler(),
                                 Builtin::kPromiseCapabilityDefaultResolve)) {
      DirectHandle<JSFunction> function(
          Cast<JSFunction>(reaction->fulfill_handler()), isolate);
      DirectHandle<Context> context(function->context(), isolate);
      promise =
          handle(Cast<JSPromise>(context->get(PromiseBuiltins::kPromiseSlot)),
                 isolate);
    } else {
      // We have some generic promise chain here, so try to
      // continue with the chained promise on the reaction
      // (only works for native promise chains).
      Handle<HeapObject> promise_or_capability(
          reaction->promise_or_capability(), isolate);
      if (IsJSPromise(*promise_or_capability)) {
        promise = Cast<JSPromise>(promise_or_capability);
      } else if (IsPromiseCapability(*promise_or_capability)) {
        auto capability = Cast<PromiseCapability>(promise_or_capability);
        if (!IsJSPromise(capability->promise())) return;
        promise = handle(Cast<JSPromise>(capability->promise()), isolate);
      } else {
        // Otherwise the {promise_or_capability} must be undefined here.
        CHECK(IsUndefined(*promise_or_capability, isolate));
        return;
      }
    }
  }
}

MaybeHandle<JSPromise> TryGetCurrentTaskPromise(Isolate* isolate) {
  Handle<Object> current_microtask = isolate->factory()->current_microtask();
  if (IsPromiseReactionJobTask(*current_microtask)) {
    auto promise_reaction_job_task =
        Cast<PromiseReactionJobTask>(current_microtask);
    // Check if the {reaction} has one of the known async function or
    // async generator continuations as its fulfill handler.
    if (IsBuiltinAsyncFulfillHandler(isolate,
                                     promise_reaction_job_task->handler()) ||
        IsBuiltinAsyncRejectHandler(isolate,
                                    promise_reaction_job_task->handler())) {
      // Now peek into the handlers' AwaitContext to get to
      // the JSGeneratorObject for the async function.
      DirectHandle<Context> context(
          Cast<JSFunction>(promise_reaction_job_task->handler())->context(),
          isolate);
      Handle<JSGeneratorObject> generator_object(
          Cast<JSGeneratorObject>(context->extension()), isolate);
      if (generator_object->is_executing()) {
        if (IsJSAsyncFunctionObject(*generator_object)) {
          auto async_function_object =
              Cast<JSAsyncFunctionObject>(generator_object);
          Handle<JSPromise> promise(async_function_object->promise(), isolate);
          return promise;
        } else {
          auto async_generator_object =
              Cast<JSAsyncGeneratorObject>(generator_object);
          DirectHandle<Object> queue(async_generator_object->queue(), isolate);
          if (!IsUndefined(*queue, isolate)) {
            auto async_generator_request = Cast<AsyncGeneratorRequest>(queue);
            Handle<JSPromise> promise(
                Cast<JSPromise>(async_generator_request->promise()), isolate);
            return promise;
          }
        }
      }
    } else {
#if V8_ENABLE_WEBASSEMBLY
      Handle<WasmSuspenderObject> suspender;
      if (TryGetWasmSuspender(isolate, promise_reaction_job_task->handler())
              .ToHandle(&suspender)) {
        // The {promise_reaction_job_task} belongs to a suspended Wasm stack
        return handle(suspender->promise(), isolate);
      }
#endif  // V8_ENABLE_WEBASSEMBLY

      // The {promise_reaction_job_task} doesn't belong to an await (or
      // yield inside an async generator) or a suspended Wasm stack,
      // but we might still be able to find an async frame if we follow
      // along the chain of promises on the {promise_reaction_job_task}.
      Handle<HeapObject> promise_or_capability(
          promise_reaction_job_task->promise_or_capability(), isolate);
      if (IsJSPromise(*promise_or_capability)) {
        Handle<JSPromise> promise = Cast<JSPromise>(promise_or_capability);
        return promise;
      }
    }
  }
  return MaybeHandle<JSPromise>();
}

void CaptureAsyncStackTrace(Isolate* isolate, CallSiteBuilder* builder) {
  Handle<JSPromise> promise;
  if (TryGetCurrentTaskPromise(isolate).ToHandle(&promise)) {
    CaptureAsyncStackTrace(isolate, promise, builder);
  }
}

template <typename Visitor>
void VisitStack(Isolate* isolate, Visitor* visitor,
                StackTrace::StackTraceOptions options = StackTrace::kDetailed) {
  DisallowJavascriptExecution no_js(isolate);
  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* frame = it.frame();
    switch (frame->type()) {
      case StackFrame::API_CALLBACK_EXIT:
      case StackFrame::BUILTIN_EXIT:
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION:
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH:
      case StackFrame::TURBOFAN:
      case StackFrame::MAGLEV:
      case StackFrame::INTERPRETED:
      case StackFrame::BASELINE:
      case StackFrame::BUILTIN:
#if V8_ENABLE_WEBASSEMBLY
      case StackFrame::STUB:
      case StackFrame::WASM:
#if V8_ENABLE_DRUMBRAKE
      case StackFrame::WASM_INTERPRETER_ENTRY:
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY
      {
        // A standard frame may include many summarized frames (due to
        // inlining).
        std::vector<FrameSummary> summaries;
        CommonFrame::cast(frame)->Summarize(&summaries);
        for (auto rit = summaries.rbegin(); rit != summaries.rend(); ++rit) {
          FrameSummary& summary = *rit;
          // Skip frames from other origins when asked to do so.
          if (!(options & StackTrace::kExposeFramesAcrossSecurityOrigins) &&
              !summary.native_context()->HasSameSecurityTokenAs(
                  isolate->context())) {
            continue;
          }
          if (!visitor->Visit(summary)) return;
        }
        break;
      }

      default:
        break;
    }
  }
}

Handle<FixedArray> CaptureSimpleStackTrace(Isolate* isolate, int limit,
                                           FrameSkipMode mode,
                                           Handle<Object> caller) {
  TRACE_EVENT_BEGIN1(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__,
                     "maxFrameCount", limit);

#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmCodeRefScope code_ref_scope;
#endif  // V8_ENABLE_WEBASSEMBLY

  CallSiteBuilder builder(isolate, mode, limit, caller);
  VisitStack(isolate, &builder);

  // If --async-stack-traces are enabled and the "current microtask" is a
  // PromiseReactionJobTask, we try to enrich the stack trace with async
  // frames.
  if (v8_flags.async_stack_traces) {
    CaptureAsyncStackTrace(isolate, &builder);
  }

  Handle<FixedArray> stack_trace = builder.Build();
  TRACE_EVENT_END1(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__,
                   "frameCount", stack_trace->length());
  return stack_trace;
}

}  // namespace

MaybeHandle<JSObject> Isolate::CaptureAndSetErrorStack(
    Handle<JSObject> error_object, FrameSkipMode mode, Handle<Object> caller) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__);
  Handle<UnionOf<Undefined, FixedArray>> call_site_infos_or_formatted_stack =
      factory()->undefined_value();

  // Capture the "simple stack trace" for the error.stack property,
  // which can be disabled by setting Error.stackTraceLimit to a non
  // number value or simply deleting the property. If the inspector
  // is active, and requests more stack frames than the JavaScript
  // program itself, we collect up to the maximum.
  int stack_trace_limit = 0;
  if (GetStackTraceLimit(this, &stack_trace_limit)) {
    int limit = stack_trace_limit;
    if (capture_stack_trace_for_uncaught_exceptions_ &&
        !(stack_trace_for_uncaught_exceptions_options_ &
          StackTrace::kExposeFramesAcrossSecurityOrigins)) {
      // Collect up to the maximum of what the JavaScript program and
      // the inspector want. There's a special case here where the API
      // can ask the stack traces to also include cross-origin frames,
      // in which case we collect a separate trace below. Note that
      // the inspector doesn't use this option, so we could as well
      // just deprecate this in the future.
      if (limit < stack_trace_for_uncaught_exceptions_frame_limit_) {
        limit = stack_trace_for_uncaught_exceptions_frame_limit_;
      }
    }
    call_site_infos_or_formatted_stack =
        CaptureSimpleStackTrace(this, limit, mode, caller);
  }
  Handle<Object> error_stack = call_site_infos_or_formatted_stack;

  // Next is the inspector part: Depending on whether we got a "simple
  // stack trace" above and whether that's usable (meaning the API
  // didn't request to include cross-origin frames), we remember the
  // cap for the stack trace (either a positive limit indicating that
  // the Error.stackTraceLimit value was below what was requested via
  // the API, or a negative limit to indicate the opposite), or we
  // collect a "detailed stack trace" eagerly and stash that away.
  if (capture_stack_trace_for_uncaught_exceptions_) {
    DirectHandle<UnionOf<Smi, FixedArray>> limit_or_stack_frame_infos;
    if (IsUndefined(*error_stack, this) ||
        (stack_trace_for_uncaught_exceptions_options_ &
         StackTrace::kExposeFramesAcrossSecurityOrigins)) {
      limit_or_stack_frame_infos = CaptureDetailedStackTrace(
          stack_trace_for_uncaught_exceptions_frame_limit_,
          stack_trace_for_uncaught_exceptions_options_);
    } else {
      int limit =
          stack_trace_limit > stack_trace_for_uncaught_exceptions_frame_limit_
              ? -stack_trace_for_uncaught_exceptions_frame_limit_
              : stack_trace_limit;
      limit_or_stack_frame_infos = handle(Smi::FromInt(limit), this);
    }
    error_stack = factory()->NewErrorStackData(
        call_site_infos_or_formatted_stack, limit_or_stack_frame_infos);
  }

  RETURN_ON_EXCEPTION(
      this,
      Object::SetProperty(this, error_object, factory()->error_stack_symbol(),
                          error_stack, StoreOrigin::kMaybeKeyed,
                          Just(ShouldThrow::kThrowOnError)));
  return error_object;
}

Handle<FixedArray> Isolate::GetDetailedStackTrace(
    Handle<JSReceiver> maybe_error_object) {
  ErrorUtils::StackPropertyLookupResult lookup =
      ErrorUtils::GetErrorStackProperty(this, maybe_error_object);

  if (!IsErrorStackData(*lookup.error_stack)) return {};
  auto error_stack_data = Cast<ErrorStackData>(lookup.error_stack);

  ErrorStackData::EnsureStackFrameInfos(this, error_stack_data);

  if (!IsFixedArray(error_stack_data->limit_or_stack_frame_infos())) return {};
  return handle(
      Cast<FixedArray>(error_stack_data->limit_or_stack_frame_infos()), this);
}

Handle<FixedArray> Isolate::GetSimpleStackTrace(
    Handle<JSReceiver> maybe_error_object) {
  ErrorUtils::StackPropertyLookupResult lookup =
      ErrorUtils::GetErrorStackProperty(this, maybe_error_object);

  if (IsFixedArray(*lookup.error_stack)) {
    return Cast<FixedArray>(lookup.error_stack);
  }
  if (!IsErrorStackData(*lookup.error_stack)) {
    return factory()->empty_fixed_array();
  }
  auto error_stack_data = Cast<ErrorStackData>(lookup.error_stack);
  if (!error_stack_data->HasCallSiteInfos()) {
    return factory()->empty_fixed_array();
  }
  return handle(error_stack_data->call_site_infos(), this);
}

Address Isolate::GetAbstractPC(int* line, int* column) {
  JavaScriptStackFrameIterator it(this);

  if (it.done()) {
    *line = -1;
    *column = -1;
    return kNullAddress;
  }
  JavaScriptFrame* frame = it.frame();
  DCHECK(!frame->is_builtin());

  Handle<SharedFunctionInfo> shared(frame->function()->shared(), this);
  SharedFunctionInfo::EnsureSourcePositionsAvailable(this, shared);
  int position = frame->position();

  Tagged<Object> maybe_script = frame->function()->shared()->script();
  if (IsScript(maybe_script)) {
    DirectHandle<Script> script(Cast<Script>(maybe_script), this);
    Script::PositionInfo info;
    Script::GetPositionInfo(script, position, &info);
    *line = info.line + 1;
    *column = info.column + 1;
  } else {
    *line = position;
    *column = -1;
  }

  if (frame->is_unoptimized()) {
    UnoptimizedFrame* iframe = static_cast<UnoptimizedFrame*>(frame);
    Address bytecode_start =
        iframe->GetBytecodeArray()->GetFirstBytecodeAddress();
    return bytecode_start + iframe->GetBytecodeOffset();
  }

  return frame->pc();
}

namespace {

class StackFrameBuilder {
 public:
  StackFrameBuilder(Isolate* isolate, int limit)
      : isolate_(isolate),
        frames_(isolate_->factory()->empty_fixed_array()),
        index_(0),
        limit_(limit) {}

  bool Visit(FrameSummary& summary) {
    // Check if we have enough capacity left.
    if (index_ >= limit_) return false;
    // Skip frames that aren't subject to debugging.
    if (!summary.is_subject_to_debugging()) return true;
    DirectHandle<StackFrameInfo> frame = summary.CreateStackFrameInfo();
    frames_ = FixedArray::SetAndGrow(isolate_, frames_, index_++, frame);
    return true;
  }

  Handle<FixedArray> Build() {
    return FixedArray::RightTrimOrEmpty(isolate_, frames_, index_);
  }

 private:
  Isolate* isolate_;
  Handle<FixedArray> frames_;
  int index_;
  int limit_;
};

}  // namespace

Handle<FixedArray> Isolate::CaptureDetailedStackTrace(
    int limit, StackTrace::StackTraceOptions options) {
  TRACE_EVENT_BEGIN1(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__,
                     "maxFrameCount", limit);
  StackFrameBuilder builder(this, limit);
  VisitStack(this, &builder, options);
  Handle<FixedArray> stack_trace = builder.Build();
  TRACE_EVENT_END1(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__,
                   "frameCount", stack_trace->length());
  return stack_trace;
}

namespace {

class CurrentScriptNameStackVisitor {
 public:
  explicit CurrentScriptNameStackVisitor(Isolate* isolate)
      : isolate_(isolate) {}

  bool Visit(FrameSummary& summary) {
    // Skip frames that aren't subject to debugging. Keep this in sync with
    // StackFrameBuilder::Visit so both visitors visit the same frames.
    if (!summary.is_subject_to_debugging()) return true;

    // Frames that are subject to debugging always have a valid script object.
    auto script = Cast<Script>(summary.script());
    Handle<Object> name_or_url_obj(script->GetNameOrSourceURL(), isolate_);
    if (!IsString(*name_or_url_obj)) return true;

    auto name_or_url = Cast<String>(name_or_url_obj);
    if (!name_or_url->length()) return true;

    name_or_url_ = name_or_url;
    return false;
  }

  Handle<String> CurrentScriptNameOrSourceURL() const { return name_or_url_; }

 private:
  Isolate* const isolate_;
  Handle<String> name_or_url_;
};

class CurrentScriptStackVisitor {
 public:
  bool Visit(FrameSummary& summary) {
    // Skip frames that aren't subject to debugging. Keep this in sync with
    // StackFrameBuilder::Visit so both visitors visit the same frames.
    if (!summary.is_subject_to_debugging()) return true;

    // Frames that are subject to debugging always have a valid script object.
    current_script_ = Cast<Script>(summary.script());
    return false;
  }

  MaybeHandle<Script> CurrentScript() const { return current_script_; }

 private:
  MaybeHandle<Script> current_script_;
};

}  // namespace

Handle<String> Isolate::CurrentScriptNameOrSourceURL() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__);
  CurrentScriptNameStackVisitor visitor(this);
  VisitStack(this, &visitor);
  return visitor.CurrentScriptNameOrSourceURL();
}

MaybeHandle<Script> Isolate::CurrentReferrerScript() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__);
  CurrentScriptStackVisitor visitor{};
  VisitStack(this, &visitor);
  Handle<Script> script;
  if (!visitor.CurrentScript().ToHandle(&script)) {
    return MaybeHandle<Script>();
  }
  return handle(script->GetEvalOrigin(), this);
}

bool Isolate::GetStackTraceLimit(Isolate* isolate, int* result) {
  if (v8_flags.correctness_fuzzer_suppressions) return false;
  Handle<JSObject> error = isolate->error_function();

  Handle<String> key = isolate->factory()->stackTraceLimit_string();
  DirectHandle<Object> stack_trace_limit =
      JSReceiver::GetDataProperty(isolate, error, key);
  if (!IsNumber(*stack_trace_limit)) return false;

  // Ensure that limit is not negative.
  *result = std::max(
      FastD2IChecked(Object::NumberValue(Cast<Number>(*stack_trace_limit))), 0);

  if (*result != v8_flags.stack_trace_limit) {
    isolate->CountUsage(v8::Isolate::kErrorStackTraceLimit);
  }

  return true;
}

void Isolate::PrintStack(FILE* out, PrintStackMode mode) {
  if (stack_trace_nesting_level_ == 0) {
    stack_trace_nesting_level_++;
    StringStream::ClearMentionedObjectCache(this);
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    incomplete_message_ = &accumulator;
    PrintStack(&accumulator, mode);
    accumulator.OutputToFile(out);
    InitializeLoggingAndCounters();
    accumulator.Log(this);
    incomplete_message_ = nullptr;
    stack_trace_nesting_level_ = 0;
  } else if (stack_trace_nesting_level_ == 1) {
    stack_trace_nesting_level_++;
    base::OS::PrintError(
        "\n\nAttempt to print stack while printing stack (double fault)\n");
    base::OS::PrintError(
        "If you are lucky you may find a partial stack dump on stdout.\n\n");
    incomplete_message_->OutputToFile(out);
  }
}

static void PrintFrames(Isolate* isolate, StringStream* accumulator,
                        StackFrame::PrintMode mode) {
  StackFrameIterator it(isolate);
  for (int i = 0; !it.done(); it.Advance()) {
    it.frame()->Print(accumulator, mode, i++);
  }
}

void Isolate::PrintStack(StringStream* accumulator, PrintStackMode mode) {
  HandleScope scope(this);
  DCHECK(accumulator->IsMentionedObjectCacheClear(this));

  // Avoid printing anything if there are no frames.
  if (c_entry_fp(thread_local_top()) == 0) return;

  accumulator->Add(
      "\n==== JS stack trace =========================================\n\n");
  PrintFrames(this, accumulator, StackFrame::OVERVIEW);
  if (mode == kPrintStackVerbose) {
    accumulator->Add(
        "\n==== Details ================================================\n\n");
    PrintFrames(this, accumulator, StackFrame::DETAILS);
    accumulator->PrintMentionedObjectCache(this);
  }
  accumulator->Add("=====================\n\n");
}

void Isolate::SetFailedAccessCheckCallback(
    v8::FailedAccessCheckCallback callback) {
  thread_local_top()->failed_access_check_callback_ = callback;
}

MaybeHandle<Object> Isolate::ReportFailedAccessCheck(
    Handle<JSObject> receiver) {
  if (!thread_local_top()->failed_access_check_callback_) {
    THROW_NEW_ERROR(this, NewTypeError(MessageTemplate::kNoAccess));
  }

  DCHECK(IsAccessCheckNeeded(*receiver));
  DCHECK(!context().is_null());

  // Get the data object from access check info.
  HandleScope scope(this);
  Handle<Object> data;
  {
    DisallowGarbageCollection no_gc;
    Tagged<AccessCheckInfo> access_check_info =
        AccessCheckInfo::Get(this, receiver);
    if (access_check_info.is_null()) {
      no_gc.Release();
      THROW_NEW_ERROR(this, NewTypeError(MessageTemplate::kNoAccess));
    }
    data = handle(access_check_info->data(), this);
  }

  {
    // Leaving JavaScript.
    VMState<EXTERNAL> state(this);
    thread_local_top()->failed_access_check_callback_(
        v8::Utils::ToLocal(receiver), v8::ACCESS_HAS, v8::Utils::ToLocal(data));
  }
  RETURN_VALUE_IF_EXCEPTION(this, {});
  // Throw exception even the callback forgot to do so.
  THROW_NEW_ERROR(this, NewTypeError(MessageTemplate::kNoAccess));
}

bool Isolate::MayAccess(Handle<NativeContext> accessing_context,
                        Handle<JSObject> receiver) {
  DCHECK(IsJSGlobalProxy(*receiver) || IsAccessCheckNeeded(*receiver));

  // Check for compatibility between the security tokens in the
  // current lexical context and the accessed object.

  // During bootstrapping, callback functions are not enabled yet.
  if (bootstrapper()->IsActive()) return true;
  {
    DisallowGarbageCollection no_gc;

    if (IsJSGlobalProxy(*receiver)) {
      std::optional<Tagged<Object>> receiver_context =
          Cast<JSGlobalProxy>(*receiver)->GetCreationContext();
      if (!receiver_context) return false;

      if (*receiver_context == *accessing_context) return true;

      if (Cast<Context>(*receiver_context)->security_token() ==
          accessing_context->security_token())
        return true;
    }
  }

  HandleScope scope(this);
  Handle<Object> data;
  v8::AccessCheckCallback callback = nullptr;
  {
    DisallowGarbageCollection no_gc;
    Tagged<AccessCheckInfo> access_check_info =
        AccessCheckInfo::Get(this, receiver);
    if (access_check_info.is_null()) return false;
    Tagged<Object> fun_obj = access_check_info->callback();
    callback = v8::ToCData<v8::AccessCheckCallback, kApiAccessCheckCallbackTag>(
        this, fun_obj);
    data = handle(access_check_info->data(), this);
  }

  {
    // Leaving JavaScript.
    VMState<EXTERNAL> state(this);
    return callback(v8::Utils::ToLocal(accessing_context),
                    v8::Utils::ToLocal(receiver), v8::Utils::ToLocal(data));
  }
}

Tagged<Object> Isolate::StackOverflow() {
  // Whoever calls this method should not have overflown the stack limit by too
  // much. Otherwise we risk actually running out of stack space.
  // We allow for up to 8kB overflow, because we typically allow up to 4KB
  // overflow per frame in generated code, but might call through more smaller
  // frames until we reach this method.
  // If this DCHECK fails, one of the frames on the stack should be augmented by
  // an additional stack check.
#if defined(V8_USE_ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER)
  // Allow for a bit more overflow in sanitizer builds, because C++ frames take
  // significantly more space there.
  DCHECK_GE(GetCurrentStackPosition(), stack_guard()->real_climit() - 64 * KB);
#elif (defined(V8_TARGET_ARCH_RISCV64) || defined(V8_TARGET_ARCH_RISCV32)) && \
    defined(USE_SIMULATOR)
  // Allow for more overflow on riscv simulator, because C++ frames take more
  // there.
  DCHECK_GE(GetCurrentStackPosition(), stack_guard()->real_climit() - 12 * KB);
#else
  DCHECK_GE(GetCurrentStackPosition(), stack_guard()->real_climit() - 8 * KB);
#endif

  if (v8_flags.correctness_fuzzer_suppressions) {
    FATAL("Aborting on stack overflow");
  }

  DisallowJavascriptExecution no_js(this);
  HandleScope scope(this);

  Handle<JSFunction> fun = range_error_function();
  DirectHandle<Object> msg = factory()->NewStringFromAsciiChecked(
      MessageFormatter::TemplateString(MessageTemplate::kStackOverflow));
  Handle<Object> options = factory()->undefined_value();
  Handle<Object> no_caller;
  Handle<JSObject> exception;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      this, exception,
      ErrorUtils::Construct(this, fun, fun, msg, options, SKIP_NONE, no_caller,
                            ErrorUtils::StackTraceCollection::kEnabled));
  JSObject::AddProperty(this, exception, factory()->wasm_uncatchable_symbol(),
                        factory()->true_value(), NONE);

  Throw(*exception);

#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap && v8_flags.stress_compaction) {
    heap()->CollectAllGarbage(GCFlag::kNoFlags,
                              GarbageCollectionReason::kTesting);
  }
#endif  // VERIFY_HEAP

  return ReadOnlyRoots(heap()).exception();
}

Tagged<Object> Isolate::ThrowAt(Handle<JSObject> exception,
                                MessageLocation* location) {
  Handle<Name> key_start_pos = factory()->error_start_pos_symbol();
  Object::SetProperty(this, exception, key_start_pos,
                      handle(Smi::FromInt(location->start_pos()), this),
                      StoreOrigin::kMaybeKeyed,
                      Just(ShouldThrow::kThrowOnError))
      .Check();

  Handle<Name> key_end_pos = factory()->error_end_pos_symbol();
  Object::SetProperty(this, exception, key_end_pos,
                      handle(Smi::FromInt(location->end_pos()), this),
                      StoreOrigin::kMaybeKeyed,
                      Just(ShouldThrow::kThrowOnError))
      .Check();

  Handle<Name> key_script = factory()->error_script_symbol();
  Object::SetProperty(this, exception, key_script, location->script(),
                      StoreOrigin::kMaybeKeyed,
                      Just(ShouldThrow::kThrowOnError))
      .Check();

  return Throw(*exception, location);
}

Tagged<Object> Isolate::TerminateExecution() {
  return Throw(ReadOnlyRoots(this).termination_exception());
}

void Isolate::CancelTerminateExecution() {
  if (!is_execution_terminating()) return;
  clear_internal_exception();
  if (try_catch_handler()) try_catch_handler()->ResetInternal();
}

void Isolate::RequestInterrupt(InterruptCallback callback, void* data) {
  ExecutionAccess access(this);
  api_interrupts_queue_.push(InterruptEntry(callback, data));
  stack_guard()->RequestApiInterrupt();
}

void Isolate::InvokeApiInterruptCallbacks() {
  RCS_SCOPE(this, RuntimeCallCounterId::kInvokeApiInterruptCallbacks);
  // Note: callback below should be called outside of execution access lock.
  while (true) {
    InterruptEntry entry;
    {
      ExecutionAccess access(this);
      if (api_interrupts_queue_.empty()) return;
      entry = api_interrupts_queue_.front();
      api_interrupts_queue_.pop();
    }
    VMState<EXTERNAL> state(this);
    HandleScope handle_scope(this);
    entry.first(reinterpret_cast<v8::Isolate*>(this), entry.second);
  }
}

void Isolate::RequestInvalidateNoProfilingProtector() {
  // This request might be triggered from arbitrary thread but protector
  // invalidation must happen on the main thread, so use Api interrupt
  // to achieve that.
  RequestInterrupt(
      [](v8::Isolate* isolate, void*) {
        Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
        if (Protectors::IsNoProfilingIntact(i_isolate)) {
          Protectors::InvalidateNoProfiling(i_isolate);
        }
      },
      nullptr);
}

namespace {

void ReportBootstrappingException(DirectHandle<Object> exception,
                                  MessageLocation* location) {
  base::OS::PrintError("Exception thrown during bootstrapping\n");
  if (location == nullptr || location->script().is_null()) return;
  // We are bootstrapping and caught an error where the location is set
  // and we have a script for the location.
  // In this case we could have an extension (or an internal error
  // somewhere) and we print out the line number at which the error occurred
  // to the console for easier debugging.
  int line_number =
      location->script()->GetLineNumber(location->start_pos()) + 1;
  if (IsString(*exception) && IsString(location->script()->name())) {
    base::OS::PrintError(
        "Extension or internal compilation error: %s in %s at line %d.\n",
        Cast<String>(*exception)->ToCString().get(),
        Cast<String>(location->script()->name())->ToCString().get(),
        line_number);
  } else if (IsString(location->script()->name())) {
    base::OS::PrintError(
        "Extension or internal compilation error in %s at line %d.\n",
        Cast<String>(location->script()->name())->ToCString().get(),
        line_number);
  } else if (IsString(*exception)) {
    base::OS::PrintError("Extension or internal compilation error: %s.\n",
                         Cast<String>(*exception)->ToCString().get());
  } else {
    base::OS::PrintError("Extension or internal compilation error.\n");
  }
#ifdef OBJECT_PRINT
  // Since comments and empty lines have been stripped from the source of
  // builtins, print the actual source here so that line numbers match.
  if (IsString(location->script()->source())) {
    DirectHandle<String> src(Cast<String>(location->script()->source()),
                             location->script()->GetIsolate());
    PrintF("Failing script:");
    int len = src->length();
    if (len == 0) {
      PrintF(" <not available>\n");
    } else {
      PrintF("\n");
      line_number = 1;
      PrintF("%5d: ", line_number);
      for (int i = 0; i < len; i++) {
        uint16_t character = src->Get(i);
        PrintF("%c", character);
        if (character == '\n' && i < len - 2) {
          PrintF("%5d: ", ++line_number);
        }
      }
      PrintF("\n");
    }
  }
#endif
}

}  // anonymous namespace

Handle<JSMessageObject> Isolate::CreateMessageOrAbort(
    Handle<Object> exception, MessageLocation* location) {
  Handle<JSMessageObject> message_obj = CreateMessage(exception, location);

  // If the abort-on-uncaught-exception flag is specified, and if the
  // embedder didn't specify a custom uncaught exception callback,
  // or if the custom callback determined that V8 should abort, then
  // abort.
  // Cache the flag on a static so that we can modify the value looked up below
  // in the presence of read-only flags.
  static bool abort_on_uncaught_exception =
      v8_flags.abort_on_uncaught_exception;
  if (abort_on_uncaught_exception) {
    CatchType prediction = PredictExceptionCatcher();
    if ((prediction == NOT_CAUGHT || prediction == CAUGHT_BY_EXTERNAL) &&
        (!abort_on_uncaught_exception_callback_ ||
         abort_on_uncaught_exception_callback_(
             reinterpret_cast<v8::Isolate*>(this)))) {
      // Prevent endless recursion.
      abort_on_uncaught_exception = false;
      // This flag is intended for use by JavaScript developers, so
      // print a user-friendly stack trace (not an internal one).
      PrintF(stderr, "%s\n\nFROM\n",
             MessageHandler::GetLocalizedMessage(this, message_obj).get());
      std::ostringstream stack_trace_stream;
      PrintCurrentStackTrace(stack_trace_stream);
      PrintF(stderr, "%s", stack_trace_stream.str().c_str());
      base::OS::Abort();
    }
  }

  return message_obj;
}

Tagged<Object> Isolate::Throw(Tagged<Object> raw_exception,
                              MessageLocation* location) {
  DCHECK(!has_exception());
  IF_WASM(DCHECK_IMPLIES, trap_handler::IsTrapHandlerEnabled(),
          !trap_handler::IsThreadInWasm());

  HandleScope scope(this);
  Handle<Object> exception(raw_exception, this);

  if (v8_flags.print_all_exceptions) {
    PrintF("=========================================================\n");
    PrintF("Exception thrown:\n");
    if (location) {
      DirectHandle<Script> script = location->script();
      DirectHandle<Object> name(script->GetNameOrSourceURL(), this);
      PrintF("at ");
      if (IsString(*name) && Cast<String>(*name)->length() > 0)
        Cast<String>(*name)->PrintOn(stdout);
      else
        PrintF("<anonymous>");
// Script::GetLineNumber and Script::GetColumnNumber can allocate on the heap to
// initialize the line_ends array, so be careful when calling them.
#ifdef DEBUG
      if (AllowGarbageCollection::IsAllowed()) {
#else
      if ((false)) {
#endif
        Script::PositionInfo start_pos;
        Script::PositionInfo end_pos;
        Script::GetPositionInfo(script, location->start_pos(), &start_pos);
        Script::GetPositionInfo(script, location->end_pos(), &end_pos);
        PrintF(", %d:%d - %d:%d\n", start_pos.line + 1, start_pos.column + 1,
               end_pos.line + 1, end_pos.column + 1);
        // Make sure to update the raw exception pointer in case it moved.
        raw_exception = *exception;
      } else {
        PrintF(", line %d\n", script->GetLineNumber(location->start_pos()) + 1);
      }
    }
    Print(raw_exception);
    PrintF("Stack Trace:\n");
    PrintStack(stdout);
    PrintF("=========================================================\n");
  }

  // Determine whether a message needs to be created for the given exception
  // depending on the following criteria:
  // 1) External v8::TryCatch missing: Always create a message because any
  //    JavaScript handler for a finally-block might re-throw to top-level.
  // 2) External v8::TryCatch exists: Only create a message if the handler
  //    captures messages or is verbose (which reports despite the catch).
  // 3) ReThrow from v8::TryCatch: The message from a previous throw still
  //    exists and we preserve it instead of creating a new message.
  bool requires_message = try_catch_handler() == nullptr ||
                          try_catch_handler()->is_verbose_ ||
                          try_catch_handler()->capture_message_;
  bool rethrowing_message = thread_local_top()->rethrowing_message_;

  thread_local_top()->rethrowing_message_ = false;

  // Notify debugger of exception.
  if (is_catchable_by_javascript(raw_exception)) {
    std::optional<Tagged<Object>> maybe_exception = debug()->OnThrow(exception);
    if (maybe_exception.has_value()) {
      return *maybe_exception;
    }
  }

  // Generate the message if required.
  if (requires_message && !rethrowing_message) {
    MessageLocation computed_location;
    // If no location was specified we try to use a computed one instead.
    if (location == nullptr && ComputeLocation(&computed_location)) {
      location = &computed_location;
    }
    if (bootstrapper()->IsActive()) {
      // It's not safe to try to make message objects or collect stack traces
      // while the bootstrapper is active since the infrastructure may not have
      // been properly initialized.
      ReportBootstrappingException(exception, location);
    } else {
      DirectHandle<Object> message_obj =
          CreateMessageOrAbort(exception, location);
      set_pending_message(*message_obj);
    }
  }

  // Set the exception being thrown.
  set_exception(*exception);
  PropagateExceptionToExternalTryCatch(TopExceptionHandlerType(*exception));

  if (v8_flags.experimental_report_exceptions_from_callbacks &&
      exception_propagation_callback_ && !rethrowing_message &&
      !preprocessing_exception_) {
    // Don't preprocess exceptions that might happen inside
    // |exception_propagation_callback_|.
    preprocessing_exception_ = true;
    NotifyExceptionPropagationCallback();
    preprocessing_exception_ = false;
  }
  return ReadOnlyRoots(heap()).exception();
}

Tagged<Object> Isolate::ReThrow(Tagged<Object> exception) {
  DCHECK(!has_exception());

  // Set the exception being re-thrown.
  set_exception(exception);
  return ReadOnlyRoots(heap()).exception();
}

Tagged<Object> Isolate::ReThrow(Tagged<Object> exception,
                                Tagged<Object> message) {
  DCHECK(!has_exception());
  DCHECK(!has_pending_message());

  set_pending_message(message);
  return ReThrow(exception);
}

namespace {
#if V8_ENABLE_WEBASSEMBLY
// This scope will set the thread-in-wasm flag after the execution of all
// destructors. The thread-in-wasm flag is only set when the scope gets enabled.
class SetThreadInWasmFlagScope {
 public:
  SetThreadInWasmFlagScope() {
    DCHECK_IMPLIES(trap_handler::IsTrapHandlerEnabled(),
                   !trap_handler::IsThreadInWasm());
  }

  ~SetThreadInWasmFlagScope() {
    if (enabled_) trap_handler::SetThreadInWasm();
  }

  void Enable() { enabled_ = true; }

 private:
  bool enabled_ = false;
};
#endif  // V8_ENABLE_WEBASSEMBLY
}  // namespace

Tagged<Object> Isolate::UnwindAndFindHandler() {
  // TODO(v8:12676): Fix gcmole failures in this function.
  DisableGCMole no_gcmole;
  DisallowGarbageCollection no_gc;

  // The topmost_script_having_context value becomes outdated after frames
  // unwinding.
  clear_topmost_script_having_context();

#if V8_ENABLE_WEBASSEMBLY
  // Create the {SetThreadInWasmFlagScope} first in this function so that its
  // destructor gets called after all the other destructors. It is important
  // that the destructor sets the thread-in-wasm flag after all other
  // destructors. The other destructors may cause exceptions, e.g. ASan on
  // Windows, which would invalidate the thread-in-wasm flag when the wasm trap
  // handler handles such non-wasm exceptions.
  SetThreadInWasmFlagScope set_thread_in_wasm_flag_scope;
#endif  // V8_ENABLE_WEBASSEMBLY
  Tagged<Object> exception = this->exception();

  auto FoundHandler = [&](Tagged<Context> context, Address instruction_start,
                          intptr_t handler_offset,
                          Address constant_pool_address, Address handler_sp,
                          Address handler_fp, int num_frames_above_handler) {
    // Store information to be consumed by the CEntry.
    thread_local_top()->pending_handler_context_ = context;
    thread_local_top()->pending_handler_entrypoint_ =
        instruction_start + handler_offset;
    thread_local_top()->pending_handler_constant_pool_ = constant_pool_address;
    thread_local_top()->pending_handler_fp_ = handler_fp;
    thread_local_top()->pending_handler_sp_ = handler_sp;
    thread_local_top()->num_frames_above_pending_handler_ =
        num_frames_above_handler;

    // Return and clear exception. The contract is that:
    // (1) the exception is stored in one place (no duplication), and
    // (2) within generated-code land, that one place is the return register.
    // If/when we unwind back into C++ (returning to the JSEntry stub,
    // or to Execution::CallWasm), the returned exception will be sent
    // back to isolate->set_exception(...).
    clear_internal_exception();
    return exception;
  };

#if V8_ENABLE_WEBASSEMBLY
  Tagged<Object> maybe_continuation = root(RootIndex::kActiveContinuation);
  Tagged<WasmContinuationObject> continuation;
  if (!IsUndefined(maybe_continuation)) {
    continuation = Cast<WasmContinuationObject>(maybe_continuation);
  }
#endif

  // Special handling of termination exceptions, uncatchable by JavaScript and
  // Wasm code, we unwind the handlers until the top ENTRY handler is found.
  bool catchable_by_js = is_catchable_by_javascript(exception);
  if (!catchable_by_js && !context().is_null()) {
    // Because the array join stack will not pop the elements when throwing the
    // uncatchable terminate exception, we need to clear the array join stack to
    // avoid leaving the stack in an invalid state.
    // See also CycleProtectedArrayJoin.
    raw_native_context()->set_array_join_stack(
        ReadOnlyRoots(this).undefined_value());
  }

  // Compute handler and stack unwinding information by performing a full walk
  // over the stack and dispatching according to the frame type.
  int visited_frames = 0;
  for (StackFrameIterator iter(this);; iter.Advance(), visited_frames++) {
#if V8_ENABLE_WEBASSEMBLY
    if (iter.frame()->type() == StackFrame::STACK_SWITCH) {
      if (catchable_by_js) {
        Tagged<Code> code =
            builtins()->code(Builtin::kWasmReturnPromiseOnSuspendAsm);
        HandlerTable table(code);
        Address instruction_start =
            code->InstructionStart(this, iter.frame()->pc());
        int handler_offset = table.LookupReturn(0);
        return FoundHandler(Context(), instruction_start, handler_offset,
                            kNullAddress, iter.frame()->sp(),
                            iter.frame()->fp(), visited_frames);
      } else {
        // We reached the base of the wasm stack. Follow the chain of
        // continuations to find the parent stack and reset the iterator.
        DCHECK(!continuation.is_null());
        wasm::StackMemory* stack =
            reinterpret_cast<wasm::StackMemory*>(continuation->stack());
        RetireWasmStack(stack);
        continuation = Cast<WasmContinuationObject>(continuation->parent());
        wasm::StackMemory* parent =
            reinterpret_cast<wasm::StackMemory*>(continuation->stack());
        parent->jmpbuf()->state = wasm::JumpBuffer::Active;
        roots_table().slot(RootIndex::kActiveContinuation).store(continuation);
        SyncStackLimit();
        iter.Reset(thread_local_top(), parent);
      }
    }
#endif
    // Handler must exist.
    DCHECK(!iter.done());

    StackFrame* frame = iter.frame();

    // The debugger implements the "restart frame" feature by throwing a
    // terminate exception. Check and if we need to restart `frame`,
    // jump into the `RestartFrameTrampoline` builtin instead of
    // a catch handler.
    // Optimized frames take a detour via the deoptimizer before also jumping
    // to the `RestartFrameTrampoline` builtin.
    if (debug()->ShouldRestartFrame(frame->id())) {
      CancelTerminateExecution();
      CHECK(!catchable_by_js);
      CHECK(frame->is_java_script());

      if (frame->is_optimized()) {
        Tagged<Code> code = frame->LookupCode();
        // The debugger triggers lazy deopt for the "to-be-restarted" frame
        // immediately when the CDP event arrives while paused.
        CHECK(code->marked_for_deoptimization());
        set_deoptimizer_lazy_throw(true);

        // Jump directly to the optimized frames return, to immediately fall
        // into the deoptimizer.
        const int offset =
            static_cast<int>(frame->pc() - code->instruction_start());

        // Compute the stack pointer from the frame pointer. This ensures that
        // argument slots on the stack are dropped as returning would.
        // Note: Needed by the deoptimizer to rematerialize frames.
        Address return_sp = frame->fp() +
                            StandardFrameConstants::kFixedFrameSizeAboveFp -
                            code->stack_slots() * kSystemPointerSize;
        return FoundHandler(Context(), code->instruction_start(), offset,
                            code->constant_pool(), return_sp, frame->fp(),
                            visited_frames);
      }

      debug()->clear_restart_frame();
      Tagged<Code> code = *BUILTIN_CODE(this, RestartFrameTrampoline);
      return FoundHandler(Context(), code->instruction_start(), 0,
                          code->constant_pool(), kNullAddress, frame->fp(),
                          visited_frames);
    }

    switch (frame->type()) {
      case StackFrame::ENTRY:
      case StackFrame::CONSTRUCT_ENTRY: {
        // For JSEntry frames we always have a handler.
        StackHandler* handler = frame->top_handler();

        // Restore the next handler.
        thread_local_top()->handler_ = handler->next_address();

        // Gather information from the handler.
        Tagged<Code> code = frame->LookupCode();
        HandlerTable table(code);
        return FoundHandler(Context(),
                            code->InstructionStart(this, frame->pc()),
                            table.LookupReturn(0), code->constant_pool(),
                            handler->address() + StackHandlerConstants::kSize,
                            0, visited_frames);
      }

#if V8_ENABLE_WEBASSEMBLY
      case StackFrame::C_WASM_ENTRY: {
#if V8_ENABLE_DRUMBRAKE
        if (v8_flags.wasm_jitless) {
          StackHandler* handler = frame->top_handler();
          thread_local_top()->handler_ = handler->next_address();
          Tagged<Code> code =
              frame->LookupCode();  // WasmInterpreterCWasmEntry.

          HandlerTable table(code);
          Address instruction_start = code->InstructionStart(this, frame->pc());
          // Compute the stack pointer from the frame pointer. This ensures that
          // argument slots on the stack are dropped as returning would.
          Address return_sp = *reinterpret_cast<Address*>(
              frame->fp() + WasmInterpreterCWasmEntryConstants::kSPFPOffset);
          const int handler_offset = table.LookupReturn(0);
          if (trap_handler::IsThreadInWasm()) {
            trap_handler::ClearThreadInWasm();
          }
          return FoundHandler(Context(), instruction_start, handler_offset,
                              code->constant_pool(), return_sp, frame->fp(),
                              visited_frames);
        }
#endif  // V8_ENABLE_DRUMBRAKE

        StackHandler* handler = frame->top_handler();
        thread_local_top()->handler_ = handler->next_address();
        Tagged<Code> code = frame->LookupCode();
        HandlerTable table(code);
        Address instruction_start = code->instruction_start();
        int return_offset = static_cast<int>(frame->pc() - instruction_start);
        int handler_offset = table.LookupReturn(return_offset);
        DCHECK_NE(-1, handler_offset);
        // Compute the stack pointer from the frame pointer. This ensures that
        // argument slots on the stack are dropped as returning would.
        Address return_sp = frame->fp() +
                            StandardFrameConstants::kFixedFrameSizeAboveFp -
                            code->stack_slots() * kSystemPointerSize;
        return FoundHandler(Context(), instruction_start, handler_offset,
                            code->constant_pool(), return_sp, frame->fp(),
                            visited_frames);
      }

#if V8_ENABLE_DRUMBRAKE
      case StackFrame::WASM_INTERPRETER_ENTRY: {
        if (trap_handler::IsThreadInWasm()) {
          trap_handler::ClearThreadInWasm();
        }
      } break;
#endif  // V8_ENABLE_DRUMBRAKE

      case StackFrame::WASM: {
        if (!is_catchable_by_wasm(exception)) break;

        WasmFrame* wasm_frame = static_cast<WasmFrame*>(frame);
        wasm::WasmCode* wasm_code =
            wasm::GetWasmCodeManager()->LookupCode(this, frame->pc());
        int offset = wasm_frame->LookupExceptionHandlerInTable();
        if (offset < 0) break;
        // Compute the stack pointer from the frame pointer. This ensures that
        // argument slots on the stack are dropped as returning would.
        // The stack slot count needs to be adjusted for Liftoff frames. It has
        // two components: the fixed frame slots, and the maximum number of
        // registers pushed on top of the frame in out-of-line code. We know
        // that we are not currently in an OOL call, because OOL calls don't
        // have exception handlers. So we subtract the OOL spill count from the
        // total stack slot count to compute the actual frame size:
        int stack_slots = wasm_code->stack_slots() - wasm_code->ool_spills();
        Address return_sp = frame->fp() +
                            StandardFrameConstants::kFixedFrameSizeAboveFp -
                            stack_slots * kSystemPointerSize;

#if V8_ENABLE_DRUMBRAKE
        // Transitioning from JS To Wasm.
        if (v8_flags.wasm_enable_exec_time_histograms &&
            v8_flags.slow_histograms && !v8_flags.wasm_jitless) {
          // Start measuring the time spent running Wasm for jitted Wasm.
          wasm_execution_timer()->Start();
        }
#endif  // V8_ENABLE_DRUMBRAKE

        // This is going to be handled by WebAssembly, so we need to set the TLS
        // flag. The {SetThreadInWasmFlagScope} will set the flag after all
        // destructors have been executed.
        set_thread_in_wasm_flag_scope.Enable();
        return FoundHandler(Context(), wasm_code->instruction_start(), offset,
                            wasm_code->constant_pool(), return_sp, frame->fp(),
                            visited_frames);
      }

      case StackFrame::WASM_LIFTOFF_SETUP: {
        // The WasmLiftoffFrameSetup builtin doesn't throw, and doesn't call
        // out to user code that could throw.
        UNREACHABLE();
      }
      case StackFrame::WASM_TO_JS: {
        Tagged<Object> suspender_obj = root(RootIndex::kActiveSuspender);
        if (!IsUndefined(suspender_obj)) {
          Tagged<WasmSuspenderObject> suspender =
              Cast<WasmSuspenderObject>(suspender_obj);
          // If the wasm-to-js wrapper was on a secondary stack and switched
          // to the central stack, handle the implicit switch back.
          Address central_stack_sp = *reinterpret_cast<Address*>(
              frame->fp() +
              WasmImportWrapperFrameConstants::kCentralStackSPOffset);
          bool switched_stacks = central_stack_sp != kNullAddress;
          if (switched_stacks) {
            DCHECK_EQ(1, suspender->has_js_frames());
            suspender->set_has_js_frames(0);
            thread_local_top()->is_on_central_stack_flag_ = false;
            Address secondary_stack_limit = Memory<Address>(
                frame->fp() +
                WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset);
            stack_guard()->SetStackLimitForStackSwitching(
                secondary_stack_limit);
          }
        }
        break;
      }
#endif  // V8_ENABLE_WEBASSEMBLY

      case StackFrame::MAGLEV:
      case StackFrame::TURBOFAN: {
        // For optimized frames we perform a lookup in the handler table.
        if (!catchable_by_js) break;
        OptimizedFrame* opt_frame = static_cast<OptimizedFrame*>(frame);
        int offset = opt_frame->LookupExceptionHandlerInTable(nullptr, nullptr);
        if (offset < 0) break;
        // The code might be an optimized code or a turbofanned builtin.
        Tagged<Code> code = frame->LookupCode();
        // Compute the stack pointer from the frame pointer. This ensures
        // that argument slots on the stack are dropped as returning would.
        Address return_sp = frame->fp() +
                            StandardFrameConstants::kFixedFrameSizeAboveFp -
                            code->stack_slots() * kSystemPointerSize;

        // TODO(bmeurer): Turbofanned BUILTIN frames appear as TURBOFAN,
        // but do not have a code kind of TURBOFAN.
        if (CodeKindCanDeoptimize(code->kind()) &&
            code->marked_for_deoptimization()) {
          // If the target code is lazy deoptimized, we jump to the original
          // return address, but we make a note that we are throwing, so
          // that the deoptimizer can do the right thing.
          offset = static_cast<int>(frame->pc() - code->instruction_start());
          set_deoptimizer_lazy_throw(true);
        }

        return FoundHandler(
            Context(), code->InstructionStart(this, frame->pc()), offset,
            code->constant_pool(), return_sp, frame->fp(), visited_frames);
      }

      case StackFrame::STUB: {
        // Some stubs are able to handle exceptions.
        if (!catchable_by_js) break;
        StubFrame* stub_frame = static_cast<StubFrame*>(frame);
#if V8_ENABLE_WEBASSEMBLY
#if DEBUG
        DCHECK_NULL(wasm::GetWasmCodeManager()->LookupCode(this, frame->pc()));
#endif
        {
          Tagged<Code> code = stub_frame->LookupCode();
          if (code->builtin_id() == Builtin::kWasmToJsWrapperCSA) {
            // If the wasm-to-js wrapper was on a secondary stack and switched
            // to the central stack, handle the implicit switch back.
            Address central_stack_sp = *reinterpret_cast<Address*>(
                frame->fp() + WasmToJSWrapperConstants::kCentralStackSPOffset);
            bool switched_stacks = central_stack_sp != kNullAddress;
            if (switched_stacks) {
              thread_local_top()->is_on_central_stack_flag_ = false;
              Address secondary_stack_limit = Memory<Address>(
                  frame->fp() +
                  WasmToJSWrapperConstants::kSecondaryStackLimitOffset);
              stack_guard()->SetStackLimitForStackSwitching(
                  secondary_stack_limit);
            }
          }
        }
#endif  // V8_ENABLE_WEBASSEMBLY

        // The code might be a dynamically generated stub or a turbofanned
        // embedded builtin.
        Tagged<Code> code = stub_frame->LookupCode();
        if (!code->is_turbofanned() || !code->has_handler_table()) {
          break;
        }

        int offset = stub_frame->LookupExceptionHandlerInTable();
        if (offset < 0) break;

        // Compute the stack pointer from the frame pointer. This ensures
        // that argument slots on the stack are dropped as returning would.
        Address return_sp = frame->fp() +
                            StandardFrameConstants::kFixedFrameSizeAboveFp -
                            code->stack_slots() * kSystemPointerSize;

        return FoundHandler(
            Context(), code->InstructionStart(this, frame->pc()), offset,
            code->constant_pool(), return_sp, frame->fp(), visited_frames);
      }

      case StackFrame::INTERPRETED:
      case StackFrame::BASELINE: {
        // For interpreted frame we perform a range lookup in the handler table.
        if (!catchable_by_js) break;
        UnoptimizedFrame* js_frame = UnoptimizedFrame::cast(frame);
        int register_slots = UnoptimizedFrameConstants::RegisterStackSlotCount(
            js_frame->GetBytecodeArray()->register_count());
        int context_reg = 0;  // Will contain register index holding context.
        int offset =
            js_frame->LookupExceptionHandlerInTable(&context_reg, nullptr);
        if (offset < 0) break;
        // Compute the stack pointer from the frame pointer. This ensures that
        // argument slots on the stack are dropped as returning would.
        // Note: This is only needed for interpreted frames that have been
        //       materialized by the deoptimizer. If there is a handler frame
        //       in between then {frame->sp()} would already be correct.
        Address return_sp = frame->fp() -
                            InterpreterFrameConstants::kFixedFrameSizeFromFp -
                            register_slots * kSystemPointerSize;

        // Patch the bytecode offset in the interpreted frame to reflect the
        // position of the exception handler. The special builtin below will
        // take care of continuing to dispatch at that position. Also restore
        // the correct context for the handler from the interpreter register.
        Tagged<Context> context =
            Cast<Context>(js_frame->ReadInterpreterRegister(context_reg));
        DCHECK(IsContext(context));

        if (frame->is_baseline()) {
          BaselineFrame* sp_frame = BaselineFrame::cast(js_frame);
          Tagged<Code> code = sp_frame->LookupCode();
          intptr_t pc_offset = sp_frame->GetPCForBytecodeOffset(offset);
          // Patch the context register directly on the frame, so that we don't
          // need to have a context read + write in the baseline code.
          sp_frame->PatchContext(context);
          return FoundHandler(Context(), code->instruction_start(), pc_offset,
                              code->constant_pool(), return_sp, sp_frame->fp(),
                              visited_frames);
        } else {
          InterpretedFrame::cast(js_frame)->PatchBytecodeOffset(
              static_cast<int>(offset));

          Tagged<Code> code = *BUILTIN_CODE(this, InterpreterEnterAtBytecode);
          // We subtract a frame from visited_frames because otherwise the
          // shadow stack will drop the underlying interpreter entry trampoline
          // in which the handler runs.
          //
          // An interpreted frame cannot be the first frame we look at
          // because at a minimum, an exit frame into C++ has to separate
          // it and the context in which this C++ code runs.
          CHECK_GE(visited_frames, 1);
          return FoundHandler(context, code->instruction_start(), 0,
                              code->constant_pool(), return_sp, frame->fp(),
                              visited_frames - 1);
        }
      }

      case StackFrame::BUILTIN:
        // For builtin frames we are guaranteed not to find a handler.
        if (catchable_by_js) {
          CHECK_EQ(-1, BuiltinFrame::cast(frame)->LookupExceptionHandlerInTable(
                           nullptr, nullptr));
        }
        break;

      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH: {
        // Builtin continuation frames with catch can handle exceptions.
        if (!catchable_by_js) break;
        JavaScriptBuiltinContinuationWithCatchFrame* js_frame =
            JavaScriptBuiltinContinuationWithCatchFrame::cast(frame);
        js_frame->SetException(exception);

        // Reconstruct the stack pointer from the frame pointer.
        Address return_sp = js_frame->fp() - js_frame->GetSPToFPDelta();
        Tagged<Code> code = js_frame->LookupCode();
        return FoundHandler(Context(), code->instruction_start(), 0,
                            code->constant_pool(), return_sp, frame->fp(),
                            visited_frames);
      }

      default:
        // All other types can not handle exception.
        break;
    }

    if (frame->is_optimized()) {
      // Remove per-frame stored materialized objects.
      bool removed = materialized_object_store_->Remove(frame->fp());
      USE(removed);
      // If there were any materialized objects, the code should be
      // marked for deopt.
      DCHECK_IMPLIES(removed, frame->LookupCode()->marked_for_deoptimization());
    }
  }

  UNREACHABLE();
}  // namespace internal

namespace {

class StackFrameSummaryIterator {
 public:
  explicit StackFrameSummaryIterator(Isolate* isolate)
      : stack_iterator_(isolate), summaries_(), index_(0) {
    InitSummaries();
  }
  void Advance() {
    if (index_ == 0) {
      summaries_.clear();
      stack_iterator_.Advance();
      InitSummaries();
    } else {
      index_--;
    }
  }
  bool done() const { return stack_iterator_.done(); }
  StackFrame* frame() const { return stack_iterator_.frame(); }
  bool has_frame_summary() const { return index_ < summaries_.size(); }
  const FrameSummary& frame_summary() const {
    DCHECK(has_frame_summary());
    return summaries_[index_];
  }
  Isolate* isolate() const { return stack_iterator_.isolate(); }

 private:
  void InitSummaries() {
    if (!done() && frame()->is_java_script()) {
      JavaScriptFrame::cast(frame())->Summarize(&summaries_);
      DCHECK_GT(summaries_.size(), 0);
      index_ = summaries_.size() - 1;
    }
  }
  StackFrameIterator stack_iterator_;
  std::vector<FrameSummary> summaries_;
  size_t index_;
};

HandlerTable::CatchPrediction CatchPredictionFor(Builtin builtin_id) {
  switch (builtin_id) {
#define CASE(Name)       \
  case Builtin::k##Name: \
    return HandlerTable::PROMISE;
    BUILTIN_PROMISE_REJECTION_PREDICTION_LIST(CASE)
#undef CASE
    default:
      return HandlerTable::UNCAUGHT;
  }
}

HandlerTable::CatchPrediction PredictExceptionFromBytecode(
    Tagged<BytecodeArray> bytecode, int code_offset) {
  HandlerTable table(bytecode);
  int handler_index = table.LookupHandlerIndexForRange(code_offset);
  if (handler_index < 0) return HandlerTable::UNCAUGHT;
  return table.GetRangePrediction(handler_index);
}

HandlerTable::CatchPrediction PredictException(const FrameSummary& summary,
                                               Isolate* isolate) {
  if (!summary.IsJavaScript()) {
    // This can happen when WASM is inlined by TurboFan. For now we ignore
    // frames that are not JavaScript.
    // TODO(https://crbug.com/349588762): We should also check Wasm code
    // for exception handling.
    return HandlerTable::UNCAUGHT;
  }
  PtrComprCageBase cage_base(isolate);
  DirectHandle<AbstractCode> code = summary.AsJavaScript().abstract_code();
  if (code->kind(cage_base) == CodeKind::BUILTIN) {
    return CatchPredictionFor(code->GetCode()->builtin_id());
  }

  // Must have been constructed from a bytecode array.
  CHECK_EQ(CodeKind::INTERPRETED_FUNCTION, code->kind(cage_base));
  return PredictExceptionFromBytecode(code->GetBytecodeArray(),
                                      summary.code_offset());
}

HandlerTable::CatchPrediction PredictExceptionFromGenerator(
    DirectHandle<JSGeneratorObject> generator, Isolate* isolate) {
  return PredictExceptionFromBytecode(
      generator->function()->shared()->GetBytecodeArray(isolate),
      GetGeneratorBytecodeOffset(generator));
}

Isolate::CatchType ToCatchType(HandlerTable::CatchPrediction prediction) {
  switch (prediction) {
    case HandlerTable::UNCAUGHT:
      return Isolate::NOT_CAUGHT;
    case HandlerTable::CAUGHT:
      return Isolate::CAUGHT_BY_JAVASCRIPT;
    case HandlerTable::PROMISE:
      return Isolate::CAUGHT_BY_PROMISE;
    case HandlerTable::UNCAUGHT_ASYNC_AWAIT:
    case HandlerTable::ASYNC_AWAIT:
      return Isolate::CAUGHT_BY_ASYNC_AWAIT;
    default:
      UNREACHABLE();
  }
}

Isolate::CatchType PredictExceptionCatchAtFrame(
    const StackFrameSummaryIterator& iterator) {
  const StackFrame* frame = iterator.frame();
  switch (frame->type()) {
    case StackFrame::ENTRY:
    case StackFrame::CONSTRUCT_ENTRY: {
      Address external_handler =
          iterator.isolate()->thread_local_top()->try_catch_handler_address();
      Address entry_handler = frame->top_handler()->next_address();
      // The exception has been externally caught if and only if there is an
      // external handler which is on top of the top-most JS_ENTRY handler.
      if (external_handler != kNullAddress &&
          !iterator.isolate()->try_catch_handler()->IsVerbose()) {
        if (entry_handler == kNullAddress || entry_handler > external_handler) {
          return Isolate::CAUGHT_BY_EXTERNAL;
        }
      }
    } break;

    // For JavaScript frames we perform a lookup in the handler table.
    case StackFrame::INTERPRETED:
    case StackFrame::BASELINE:
    case StackFrame::TURBOFAN:
    case StackFrame::MAGLEV:
    case StackFrame::BUILTIN: {
      DCHECK(iterator.has_frame_summary());
      return ToCatchType(
          PredictException(iterator.frame_summary(), iterator.isolate()));
    }

    case StackFrame::STUB: {
      Tagged<Code> code = *frame->LookupCode();
      if (code->kind() != CodeKind::BUILTIN || !code->has_handler_table() ||
          !code->is_turbofanned()) {
        break;
      }

      return ToCatchType(CatchPredictionFor(code->builtin_id()));
    }

    case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH: {
      Tagged<Code> code = *frame->LookupCode();
      return ToCatchType(CatchPredictionFor(code->builtin_id()));
    }

    default:
      // All other types can not handle exception.
      break;
  }
  return Isolate::NOT_CAUGHT;
}
}  // anonymous namespace

Isolate::CatchType Isolate::PredictExceptionCatcher() {
  if (TopExceptionHandlerType(Tagged<Object>()) ==
      ExceptionHandlerType::kExternalTryCatch) {
    return CAUGHT_BY_EXTERNAL;
  }

  // Search for an exception handler by performing a full walk over the stack.
  for (StackFrameSummaryIterator iter(this); !iter.done(); iter.Advance()) {
    Isolate::CatchType prediction = PredictExceptionCatchAtFrame(iter);
    if (prediction != NOT_CAUGHT) return prediction;
  }

  // Handler not found.
  return NOT_CAUGHT;
}

Tagged<Object> Isolate::ThrowIllegalOperation() {
  if (v8_flags.stack_trace_on_illegal) PrintStack(stdout);
  return Throw(ReadOnlyRoots(heap()).illegal_access_string());
}

void Isolate::PrintCurrentStackTrace(std::ostream& out) {
  DirectHandle<FixedArray> frames = CaptureSimpleStackTrace(
      this, FixedArray::kMaxLength, SKIP_NONE, factory()->undefined_value());

  IncrementalStringBuilder builder(this);
  for (int i = 0; i < frames->length(); ++i) {
    DirectHandle<CallSiteInfo> frame(Cast<CallSiteInfo>(frames->get(i)), this);
    SerializeCallSiteInfo(this, frame, &builder);
    if (i != frames->length() - 1) builder.AppendCharacter('\n');
  }

  DirectHandle<String> stack_trace = builder.Finish().ToHandleChecked();
  stack_trace->PrintOn(out);
}

bool Isolate::ComputeLocation(MessageLocation* target) {
  DebuggableStackFrameIterator it(this);
  if (it.done()) return false;
  // Compute the location from the function and the relocation info of the
  // baseline code. For optimized code this will use the deoptimization
  // information to get canonical location information.
#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmCodeRefScope code_ref_scope;
#endif  // V8_ENABLE_WEBASSEMBLY
  FrameSummary summary = it.GetTopValidFrame();
  Handle<SharedFunctionInfo> shared;
  Handle<Object> script = summary.script();
  if (!IsScript(*script) ||
      IsUndefined(Cast<Script>(*script)->source(), this)) {
    return false;
  }

  if (summary.IsJavaScript()) {
    shared = handle(summary.AsJavaScript().function()->shared(), this);
  }
  if (summary.AreSourcePositionsAvailable()) {
    int pos = summary.SourcePosition();
    *target = MessageLocation(Cast<Script>(script), pos, pos + 1, shared);
  } else {
    *target =
        MessageLocation(Cast<Script>(script), shared, summary.code_offset());
  }
  return true;
}

bool Isolate::ComputeLocationFromException(MessageLocation* target,
                                           Handle<Object> exception) {
  if (!IsJSObject(*exception)) return false;

  Handle<Name> start_pos_symbol = factory()->error_start_pos_symbol();
  DirectHandle<Object> start_pos = JSReceiver::GetDataProperty(
      this, Cast<JSObject>(exception), start_pos_symbol);
  if (!IsSmi(*start_pos)) return false;
  int start_pos_value = Cast<Smi>(*start_pos).value();

  Handle<Name> end_pos_symbol = factory()->error_end_pos_symbol();
  DirectHandle<Object> end_pos = JSReceiver::GetDataProperty(
      this, Cast<JSObject>(exception), end_pos_symbol);
  if (!IsSmi(*end_pos)) return false;
  int end_pos_value = Cast<Smi>(*end_pos).value();

  Handle<Name> script_symbol = factory()->error_script_symbol();
  DirectHandle<Object> script = JSReceiver::GetDataProperty(
      this, Cast<JSObject>(exception), script_symbol);
  if (!IsScript(*script)) return false;

  Handle<Script> cast_script(Cast<Script>(*script), this);
  *target = MessageLocation(cast_script, start_pos_value, end_pos_value);
  return true;
}

bool Isolate::ComputeLocationFromSimpleStackTrace(MessageLocation* target,
                                                  Handle<Object> exception) {
  if (!IsJSReceiver(*exception)) {
    return false;
  }
  DirectHandle<FixedArray> call_site_infos =
      GetSimpleStackTrace(Cast<JSReceiver>(exception));
  for (int i = 0; i < call_site_infos->length(); ++i) {
    DirectHandle<CallSiteInfo> call_site_info(
        Cast<CallSiteInfo>(call_site_infos->get(i)), this);
    if (CallSiteInfo::ComputeLocation(call_site_info, target)) {
      return true;
    }
  }
  return false;
}

bool Isolate::ComputeLocationFromDetailedStackTrace(MessageLocation* target,
                                                    Handle<Object> exception) {
  if (!IsJSReceiver(*exception)) return false;

  Handle<FixedArray> stack_frame_infos =
      GetDetailedStackTrace(Cast<JSReceiver>(exception));
  if (stack_frame_infos.is_null() || stack_frame_infos->length() == 0) {
    return false;
  }

  DirectHandle<StackFrameInfo> info(
      Cast<StackFrameInfo>(stack_frame_infos->get(0)), this);
  const int pos = StackFrameInfo::GetSourcePosition(info);
  *target = MessageLocation(handle(info->script(), this), pos, pos + 1);
  return true;
}

Handle<JSMessageObject> Isolate::CreateMessage(Handle<Object> exception,
                                               MessageLocation* location) {
  Handle<FixedArray> stack_trace_object;
  if (capture_stack_trace_for_uncaught_exceptions_) {
    if (IsJSObject(*exception)) {
      // First, check whether a stack trace is already present on this object.
      // It maybe an Error, or the embedder may have stored a stack trace using
      // Exception::CaptureStackTrace().
      // If the lookup fails, we fall through and capture the stack trace
      // at this throw site.
      stack_trace_object = GetDetailedStackTrace(Cast<JSObject>(exception));
    }
    if (stack_trace_object.is_null()) {
      // Not an error object, we capture stack and location at throw site.
      stack_trace_object = CaptureDetailedStackTrace(
          stack_trace_for_uncaught_exceptions_frame_limit_,
          stack_trace_for_uncaught_exceptions_options_);
    }
  }
  MessageLocation computed_location;
  if (location == nullptr &&
      (ComputeLocationFromException(&computed_location, exception) ||
       ComputeLocationFromSimpleStackTrace(&computed_location, exception) ||
       ComputeLocation(&computed_location))) {
    location = &computed_location;
  }

  return MessageHandler::MakeMessageObject(
      this, MessageTemplate::kUncaughtException, location, exception,
      stack_trace_object);
}

Handle<JSMessageObject> Isolate::CreateMessageFromException(
    Handle<Object> exception) {
  DirectHandle<FixedArray> stack_trace_object;
  if (IsJSError(*exception)) {
    stack_trace_object = GetDetailedStackTrace(Cast<JSObject>(exception));
  }

  MessageLocation* location = nullptr;
  MessageLocation computed_location;
  if (ComputeLocationFromException(&computed_location, exception) ||
      ComputeLocationFromDetailedStackTrace(&computed_location, exception)) {
    location = &computed_location;
  }

  return MessageHandler::MakeMessageObject(
      this, MessageTemplate::kPlaceholderOnly, location, exception,
      stack_trace_object);
}

Isolate::ExceptionHandlerType Isolate::TopExceptionHandlerType(
    Tagged<Object> exception) {
  DCHECK_NE(ReadOnlyRoots(heap()).the_hole_value(), exception);

  Address js_handler = Isolate::handler(thread_local_top());
  Address external_handler = thread_local_top()->try_catch_handler_address();

  // A handler cannot be on top if it doesn't exist. For uncatchable exceptions,
  // the JavaScript handler cannot be on top.
  if (js_handler == kNullAddress || !is_catchable_by_javascript(exception)) {
    if (external_handler == kNullAddress) {
      return ExceptionHandlerType::kNone;
    }
    return ExceptionHandlerType::kExternalTryCatch;
  }

  if (external_handler == kNullAddress) {
    return ExceptionHandlerType::kJavaScriptHandler;
  }

  // The exception has been externally caught if and only if there is an
  // external handler which is on top of the top-most JS_ENTRY handler.
  //
  // Note, that finally clauses would re-throw an exception unless it's aborted
  // by jumps in control flow (like return, break, etc.) and we'll have another
  // chance to set proper v8::TryCatch later.
  DCHECK_NE(kNullAddress, external_handler);
  DCHECK_NE(kNullAddress, js_handler);
  if (external_handler < js_handler) {
    return ExceptionHandlerType::kExternalTryCatch;
  }
  return ExceptionHandlerType::kJavaScriptHandler;
}

std::vector<MemoryRange>* Isolate::GetCodePages() const {
  return code_pages_.load(std::memory_order_acquire);
}

void Isolate::SetCodePages(std::vector<MemoryRange>* new_code_pages) {
  code_pages_.store(new_code_pages, std::memory_order_release);
}

void Isolate::ReportPendingMessages(bool report) {
  Tagged<Object> exception_obj = exception();
  ExceptionHandlerType top_handler = TopExceptionHandlerType(exception_obj);

  // Try to propagate the exception to an external v8::TryCatch handler. If
  // propagation was unsuccessful, then we will get another chance at reporting
  // the pending message if the exception is re-thrown.
  bool has_been_propagated = PropagateExceptionToExternalTryCatch(top_handler);
  if (!has_been_propagated) return;
  if (!report) return;

  DCHECK(AllowExceptions::IsAllowed(this));

  // The embedder might run script in response to an exception.
  AllowJavascriptExecutionDebugOnly allow_script(this);

  // Clear the pending message object early to avoid endless recursion.
  Tagged<Object> message_obj = pending_message();
  clear_pending_message();

  // For uncatchable exceptions we do nothing. If needed, the exception and the
  // message have already been propagated to v8::TryCatch.
  if (!is_catchable_by_javascript(exception_obj)) return;

  // Determine whether the message needs to be reported to all message handlers
  // depending on whether the topmost external v8::TryCatch is verbose. We know
  // there's no JavaScript handler on top; if there was, we would've returned
  // early.
  DCHECK_NE(ExceptionHandlerType::kJavaScriptHandler, top_handler);

  bool should_report_exception;
  if (top_handler == ExceptionHandlerType::kExternalTryCatch) {
    should_report_exception = try_catch_handler()->is_verbose_;
  } else {
    should_report_exception = true;
  }

  // Actually report the pending message to all message handlers.
  if (!IsTheHole(message_obj, this) && should_report_exception) {
    HandleScope scope(this);
    DirectHandle<JSMessageObject> message(Cast<JSMessageObject>(message_obj),
                                          this);
    Handle<Script> script(message->script(), this);
    // Clear the exception and restore it afterwards, otherwise
    // CollectSourcePositions will abort.
    {
      ExceptionScope exception_scope(this);
      JSMessageObject::EnsureSourcePositionsAvailable(this, message);
    }
    int start_pos = message->GetStartPosition();
    int end_pos = message->GetEndPosition();
    MessageLocation location(script, start_pos, end_pos);
    MessageHandler::ReportMessage(this, &location, message);
  }
}

namespace {
bool ReceiverIsForwardingHandler(Isolate* isolate, Handle<JSReceiver> handler) {
  // Recurse to the forwarding Promise (e.g. return false) due to
  //  - await reaction forwarding to the throwaway Promise, which has
  //    a dependency edge to the outer Promise.
  //  - PromiseIdResolveHandler forwarding to the output of .then
  //  - Promise.all/Promise.race forwarding to a throwaway Promise, which
  //    has a dependency edge to the generated outer Promise.
  // Otherwise, this is a real reject handler for the Promise.
  Handle<Symbol> key = isolate->factory()->promise_forwarding_handler_symbol();
  DirectHandle<Object> forwarding_handler =
      JSReceiver::GetDataProperty(isolate, handler, key);
  return !IsUndefined(*forwarding_handler, isolate);
}

bool WalkPromiseTreeInternal(
    Isolate* isolate, Handle<JSPromise> promise,
    const std::function<void(Isolate::PromiseHandler)>& callback) {
  if (promise->status() != Promise::kPending) {
    // If a rejection reaches an exception that isn't pending, it will be
    // treated as caught.
    return true;
  }

  bool any_caught = false;
  bool any_uncaught = false;
  DirectHandle<Object> current(promise->reactions(), isolate);
  while (!IsSmi(*current)) {
    auto reaction = Cast<PromiseReaction>(current);
    Handle<HeapObject> promise_or_capability(reaction->promise_or_capability(),
                                             isolate);
    if (!IsUndefined(*promise_or_capability, isolate)) {
      if (!IsJSPromise(*promise_or_capability)) {
        promise_or_capability = handle(
            Cast<PromiseCapability>(promise_or_capability)->promise(), isolate);
      }
      if (IsJSPromise(*promise_or_capability)) {
        Handle<JSPromise> next_promise = Cast<JSPromise>(promise_or_capability);
        bool caught = false;
        Handle<JSReceiver> reject_handler;
        if (!IsUndefined(reaction->reject_handler(), isolate)) {
          reject_handler =
              handle(Cast<JSReceiver>(reaction->reject_handler()), isolate);
          if (!ReceiverIsForwardingHandler(isolate, reject_handler) &&
              !IsBuiltinForwardingRejectHandler(isolate, *reject_handler)) {
            caught = true;
          }
        }
        // Pass each handler to the callback
        Handle<JSGeneratorObject> async_function;
        if (TryGetAsyncGenerator(isolate, reaction).ToHandle(&async_function)) {
          caught = caught ||
                   PredictExceptionFromGenerator(async_function, isolate) ==
                       HandlerTable::CAUGHT;
          // Look at the async function, not the individual handlers
          callback({async_function->function()->shared(), true});
        } else {
          // Not an async function, look at individual handlers
          if (!IsUndefined(reaction->fulfill_handler(), isolate)) {
            Handle<JSReceiver> fulfill_handler(
                Cast<JSReceiver>(reaction->fulfill_handler()), isolate);
            if (!ReceiverIsForwardingHandler(isolate, fulfill_handler)) {
              if (IsBuiltinFunction(isolate, *fulfill_handler,
                                    Builtin::kPromiseThenFinally)) {
                // If this is the finally handler, get the wrapped callback
                // from the context to use instead
                DirectHandle<Context> context(
                    Cast<JSFunction>(reaction->fulfill_handler())->context(),
                    isolate);
                int const index =
                    PromiseBuiltins::PromiseFinallyContextSlot::kOnFinallySlot;
                fulfill_handler =
                    handle(Cast<JSReceiver>(context->get(index)), isolate);
              }
              if (IsJSFunction(*fulfill_handler)) {
                callback({Cast<JSFunction>(fulfill_handler)->shared(), true});
              }
            }
          }
          if (caught) {
            // We've already checked that this isn't undefined or
            // a forwarding handler
            if (IsJSFunction(*reject_handler)) {
              callback({Cast<JSFunction>(reject_handler)->shared(), true});
            }
          }
        }
        caught =
            caught || WalkPromiseTreeInternal(isolate, next_promise, callback);
        any_caught = any_caught || caught;
        any_uncaught = any_uncaught || !caught;
      }
    } else {
#if V8_ENABLE_WEBASSEMBLY
      Handle<WasmSuspenderObject> suspender;
      if (TryGetWasmSuspender(isolate, reaction->fulfill_handler())
              .ToHandle(&suspender)) {
        // If in the future we support Wasm exceptions or ignore listing in
        // Wasm, we will need to iterate through these frames. For now, we
        // only care about the resulting promise.
        Handle<JSPromise> next_promise = handle(suspender->promise(), isolate);
        bool caught = WalkPromiseTreeInternal(isolate, next_promise, callback);
        any_caught = any_caught || caught;
        any_uncaught = any_uncaught || !caught;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    current = direct_handle(reaction->next(), isolate);
  }

  bool caught = any_caught && !any_uncaught;

  if (!caught) {
    // If there is an outer promise, follow that to see if it is caught.
    Handle<Symbol> key = isolate->factory()->promise_handled_by_symbol();
    Handle<Object> outer_promise_obj =
        JSObject::GetDataProperty(isolate, promise, key);
    if (IsJSPromise(*outer_promise_obj)) {
      return WalkPromiseTreeInternal(
          isolate, Cast<JSPromise>(outer_promise_obj), callback);
    }
  }
  return caught;
}

// Helper functions to scan for calls to .catch.
using interpreter::Bytecode;
using interpreter::Bytecodes;

enum PromiseMethod { kThen, kCatch, kFinally, kInvalid };

// Requires the iterator to be on a GetNamedProperty instruction
PromiseMethod GetPromiseMethod(
    Isolate* isolate, const interpreter::BytecodeArrayIterator& iterator) {
  DirectHandle<Object> object = iterator.GetConstantForIndexOperand(1, isolate);
  if (!IsString(*object)) {
    return kInvalid;
  }
  auto str = Cast<String>(object);
  if (str->Equals(ReadOnlyRoots(isolate).then_string())) {
    return kThen;
  } else if (str->IsEqualTo(base::StaticCharVector("catch"))) {
    return kCatch;
  } else if (str->IsEqualTo(base::StaticCharVector("finally"))) {
    return kFinally;
  } else {
    return kInvalid;
  }
}

bool TouchesRegister(const interpreter::BytecodeArrayIterator& iterator,
                     int index) {
  Bytecode bytecode = iterator.current_bytecode();
  int num_operands = Bytecodes::NumberOfOperands(bytecode);
  const interpreter::OperandType* operand_types =
      Bytecodes::GetOperandTypes(bytecode);

  for (int i = 0; i < num_operands; ++i) {
    if (Bytecodes::IsRegisterOperandType(operand_types[i])) {
      int base_index = iterator.GetRegisterOperand(i).index();
      int num_registers;
      if (Bytecodes::IsRegisterListOperandType(operand_types[i])) {
        num_registers = iterator.GetRegisterCountOperand(++i);
      } else {
        num_registers =
            Bytecodes::GetNumberOfRegistersRepresentedBy(operand_types[i]);
      }

      if (base_index <= index && index < base_index + num_registers) {
        return true;
      }
    }
  }

  if (Bytecodes::WritesImplicitRegister(bytecode)) {
    return iterator.GetStarTargetRegister().index() == index;
  }

  return false;
}

bool CallsCatchMethod(Isolate* isolate, Handle<BytecodeArray> bytecode_array,
                      int offset) {
  interpreter::BytecodeArrayIterator iterator(bytecode_array, offset);

  while (!iterator.done()) {
    // We should be on a call instruction of some kind. While we could check
    // this, it may be difficult to create an exhaustive list of instructions
    // that could call, such as property getters, but at a minimum this
    // instruction should write to the accumulator.
    if (!Bytecodes::WritesAccumulator(iterator.current_bytecode())) {
      return false;
    }

    iterator.Advance();
    // While usually the next instruction is a Star, sometimes we store and
    // reload from context first.
    if (iterator.done()) {
      return false;
    }
    if (iterator.current_bytecode() == Bytecode::kStaCurrentContextSlot) {
      // Step over patterns like:
      //     StaCurrentContextSlot [x]
      //     LdaImmutableCurrentContextSlot [x]
      unsigned int slot = iterator.GetIndexOperand(0);
      iterator.Advance();
      if (!iterator.done() &&
          (iterator.current_bytecode() ==
               Bytecode::kLdaImmutableCurrentContextSlot ||
           iterator.current_bytecode() == Bytecode::kLdaCurrentContextSlot)) {
        if (iterator.GetIndexOperand(0) != slot) {
          return false;
        }
        iterator.Advance();
      }
    } else if (iterator.current_bytecode() == Bytecode::kStaContextSlot) {
      // Step over patterns like:
      //     StaContextSlot r_x [y] [z]
      //     LdaContextSlot r_x [y] [z]
      int context = iterator.GetRegisterOperand(0).index();
      unsigned int slot = iterator.GetIndexOperand(1);
      unsigned int depth = iterator.GetUnsignedImmediateOperand(2);
      iterator.Advance();
      if (!iterator.done() &&
          (iterator.current_bytecode() == Bytecode::kLdaImmutableContextSlot ||
           iterator.current_bytecode() == Bytecode::kLdaContextSlot)) {
        if (iterator.GetRegisterOperand(0).index() != context ||
            iterator.GetIndexOperand(1) != slot ||
            iterator.GetUnsignedImmediateOperand(2) != depth) {
          return false;
        }
        iterator.Advance();
      }
    } else if (iterator.current_bytecode() == Bytecode::kStaLookupSlot) {
      // Step over patterns like:
      //     StaLookupSlot [x] [_]
      //     LdaLookupSlot [x]
      unsigned int slot = iterator.GetIndexOperand(0);
      iterator.Advance();
      if (!iterator.done() &&
          (iterator.current_bytecode() == Bytecode::kLdaLookupSlot ||
           iterator.current_bytecode() ==
               Bytecode::kLdaLookupSlotInsideTypeof)) {
        if (iterator.GetIndexOperand(0) != slot) {
          return false;
        }
        iterator.Advance();
      }
    }

    // Next instruction should be a Star (store accumulator to register)
    if (iterator.done() || !Bytecodes::IsAnyStar(iterator.current_bytecode())) {
      return false;
    }
    // The register it stores to will be assumed to be our promise
    int promise_register = iterator.GetStarTargetRegister().index();

    // TODO(crbug/40283993): Should we loop over non-matching instructions here
    // to allow code like
    // `const promise = foo(); console.log(...); promise.catch(...);`?

    iterator.Advance();
    // We should be on a GetNamedProperty instruction.
    if (iterator.done() ||
        iterator.current_bytecode() != Bytecode::kGetNamedProperty ||
        iterator.GetRegisterOperand(0).index() != promise_register) {
      return false;
    }
    PromiseMethod method = GetPromiseMethod(isolate, iterator);
    if (method == kInvalid) {
      return false;
    }

    iterator.Advance();
    // Next instruction should be a Star (save immediate to register)
    if (iterator.done() || !Bytecodes::IsAnyStar(iterator.current_bytecode())) {
      return false;
    }
    // This register contains the method we will eventually invoke
    int method_register = iterator.GetStarTargetRegister().index();
    if (method_register == promise_register) {
      return false;
    }

    // Now we step over multiple instructions creating the arguments for the
    // method.
    while (true) {
      iterator.Advance();
      if (iterator.done()) {
        return false;
      }
      Bytecode bytecode = iterator.current_bytecode();
      if (bytecode == Bytecode::kCallProperty1 ||
          bytecode == Bytecode::kCallProperty2) {
        // This is a call property call of the right size, but is it a call of
        // the method and on the promise?
        if (iterator.GetRegisterOperand(0).index() == method_register &&
            iterator.GetRegisterOperand(1).index() == promise_register) {
          // This is our method call, but does it catch?
          if (method == kCatch ||
              (method == kThen && bytecode == Bytecode::kCallProperty2)) {
            return true;
          }
          // Break out of the inner loop, continuing the outer loop. We
          // will use the same procedure to check for chained method calls.
          break;
        }
      }

      // Check for some instructions that should make us give up scanning.
      if (Bytecodes::IsJump(bytecode) || Bytecodes::IsSwitch(bytecode) ||
          Bytecodes::Returns(bytecode) ||
          Bytecodes::UnconditionallyThrows(bytecode)) {
        // Stop scanning at control flow instructions that aren't calls
        return false;
      }

      if (TouchesRegister(iterator, promise_register) ||
          TouchesRegister(iterator, method_register)) {
        // Stop scanning at instruction that unexpectedly interacts with one of
        // the registers we care about.
        return false;
      }
    }
  }
  return false;
}

bool CallsCatchMethod(const StackFrameSummaryIterator& iterator) {
  if (!iterator.frame()->is_java_script()) {
    return false;
  }
  if (iterator.frame_summary().IsJavaScript()) {
    auto& js_summary = iterator.frame_summary().AsJavaScript();
    if (IsBytecodeArray(*js_summary.abstract_code())) {
      if (CallsCatchMethod(iterator.isolate(),
                           Cast<BytecodeArray>(js_summary.abstract_code()),
                           js_summary.code_offset())) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool Isolate::WalkCallStackAndPromiseTree(
    MaybeHandle<JSPromise> rejected_promise,
    const std::function<void(PromiseHandler)>& callback) {
  bool is_promise_rejection = false;

  Handle<JSPromise> promise;
  if (rejected_promise.ToHandle(&promise)) {
    is_promise_rejection = true;
    // If the promise has reactions, follow them and assume we are done. If
    // it has no reactions, assume promise is returned up the call stack and
    // trace accordingly. If the promise is not pending, it has no reactions
    // and is probably the result of a call to Promise.reject().
    if (promise->status() != Promise::kPending) {
      // Ignore this promise; set to null
      rejected_promise = MaybeHandle<JSPromise>();
    } else if (IsSmi(promise->reactions())) {
      // Also check that there is no outer promise
      Handle<Symbol> key = factory()->promise_handled_by_symbol();
      if (!IsJSPromise(*JSObject::GetDataProperty(this, promise, key))) {
        // Ignore this promise; set to null
        rejected_promise = MaybeHandle<JSPromise>();
      }
    }
  }

  if (!is_promise_rejection && TopExceptionHandlerType(Tagged<Object>()) ==
                                   ExceptionHandlerType::kExternalTryCatch) {
    return true;  // caught by external
  }

  // Search for an exception handler by performing a full walk over the stack.
  for (StackFrameSummaryIterator iter(this); !iter.done(); iter.Advance()) {
    Isolate::CatchType prediction = PredictExceptionCatchAtFrame(iter);

    bool caught;
    if (rejected_promise.is_null()) {
      switch (prediction) {
        case NOT_CAUGHT:
          // Uncaught unless this is a promise rejection and the code will call
          // .catch()
          caught = is_promise_rejection && CallsCatchMethod(iter);
          break;
        case CAUGHT_BY_ASYNC_AWAIT:
          // Uncaught unless this is a promise rejection and the code will call
          // .catch()
          caught = is_promise_rejection && CallsCatchMethod(iter);
          // Exceptions turn into promise rejections here
          is_promise_rejection = true;
          break;
        case CAUGHT_BY_PROMISE:
          // Exceptions turn into promise rejections here
          // TODO(leese): Perhaps we can handle the case where the reject method
          // is called in the promise constructor and it is still on the stack
          // by ignoring all try/catches on the stack until we get to the right
          // CAUGHT_BY_PROMISE?
          is_promise_rejection = true;
          caught = false;
          break;
        case CAUGHT_BY_EXTERNAL:
          caught = !is_promise_rejection;
          break;
        case CAUGHT_BY_JAVASCRIPT:
          caught = true;
          // Unless this is a promise rejection and the function is not async...
          DCHECK(iter.has_frame_summary());
          const FrameSummary& summary = iter.frame_summary();
          if (is_promise_rejection && summary.IsJavaScript()) {
            // If the catch happens in an async function, assume it will
            // await this promise. Alternately, if the code will call .catch,
            // assume it is on this promise.
            caught = IsAsyncFunction(iter.frame_summary()
                                         .AsJavaScript()
                                         .function()
                                         ->shared()
                                         ->kind()) ||
                     CallsCatchMethod(iter);
          }
          break;
      }
    } else {
      // The frame that calls the reject handler will not catch that promise
      // regardless of what else it does. We will trace where this rejection
      // goes according to its reaction callbacks, but we first need to handle
      // the topmost debuggable frame just to ensure there is a debuggable
      // frame and to permit ignore listing there.
      caught = false;
    }

    if (iter.frame()->is_java_script()) {
      bool debuggable = false;
      DCHECK(iter.has_frame_summary());
      const FrameSummary& summary = iter.frame_summary();
      if (summary.IsJavaScript()) {
        const auto& info = summary.AsJavaScript().function()->shared();
        if (info->IsSubjectToDebugging()) {
          callback({*info, false});
          debuggable = true;
        }
      }

      // Ignore the rest of the call stack if this is a rejection and the
      // promise has handlers; we will trace where the rejection goes instead
      // of where it came from.
      if (debuggable && !rejected_promise.is_null()) {
        break;
      }
    }

    if (caught) {
      return true;
    }
  }

  if (rejected_promise.is_null()) {
    // Now follow promises if this is a promise reaction job.
    rejected_promise = TryGetCurrentTaskPromise(this);
  }

  if (rejected_promise.ToHandle(&promise)) {
    return WalkPromiseTreeInternal(this, promise, callback);
  }
  // Nothing caught.
  return false;
}

void Isolate::SetCaptureStackTraceForUncaughtExceptions(
    bool capture, int frame_limit, StackTrace::StackTraceOptions options) {
  capture_stack_trace_for_uncaught_exceptions_ = capture;
  stack_trace_for_uncaught_exceptions_frame_limit_ = frame_limit;
  stack_trace_for_uncaught_exceptions_options_ = options;
}

bool Isolate::get_capture_stack_trace_for_uncaught_exceptions() const {
  return capture_stack_trace_for_uncaught_exceptions_;
}

void Isolate::SetAbortOnUncaughtExceptionCallback(
    v8::Isolate::AbortOnUncaughtExceptionCallback callback) {
  abort_on_uncaught_exception_callback_ = callback;
}

void Isolate::InstallConditionalFeatures(Handle<NativeContext> context) {
  Handle<JSGlobalObject> global = handle(context->global_object(), this);
  // If some fuzzer decided to make the global object non-extensible, then
  // we can't install any features (and would CHECK-fail if we tried).
  if (!global->map()->is_extensible()) return;
  Handle<String> sab_name = factory()->SharedArrayBuffer_string();
  if (IsSharedArrayBufferConstructorEnabled(context)) {
    if (!JSObject::HasRealNamedProperty(this, global, sab_name)
             .FromMaybe(true)) {
      JSObject::AddProperty(this, global, factory()->SharedArrayBuffer_string(),
                            shared_array_buffer_fun(), DONT_ENUM);
    }
  }
}

bool Isolate::IsSharedArrayBufferConstructorEnabled(
    Handle<NativeContext> context) {
  if (!v8_flags.enable_sharedarraybuffer_per_context) return true;

  if (sharedarraybuffer_constructor_enabled_callback()) {
    v8::Local<v8::Context> api_context = v8::Utils::ToLocal(context);
    return sharedarraybuffer_constructor_enabled_callback()(api_context);
  }
  return false;
}

bool Isolate::IsWasmStringRefEnabled(Handle<NativeContext> context) {
#ifdef V8_ENABLE_WEBASSEMBLY
  // If Wasm imported strings are explicitly enabled via a callback, also enable
  // stringref.
  v8::WasmImportedStringsEnabledCallback callback_imported_strings =
      wasm_imported_strings_enabled_callback();
  if (callback_imported_strings) {
    v8::Local<v8::Context> api_context = v8::Utils::ToLocal(context);
    if (callback_imported_strings(api_context)) return true;
  }
  // Otherwise use the runtime flag.
  return v8_flags.experimental_wasm_stringref;
#else
  return false;
#endif
}

bool Isolate::IsWasmJSPIRequested(Handle<NativeContext> context) {
#ifdef V8_ENABLE_WEBASSEMBLY
  v8::WasmJSPIEnabledCallback jspi_callback = wasm_jspi_enabled_callback();
  if (jspi_callback) {
    v8::Local<v8::Context> api_context = v8::Utils::ToLocal(context);
    if (jspi_callback(api_context)) return true;
  }

  // Otherwise use the runtime flag.
  return v8_flags.experimental_wasm_jspi;
#else
  return false;
#endif
}

bool Isolate::IsWasmJSPIEnabled(Handle<NativeContext> context) {
#ifdef V8_ENABLE_WEBASSEMBLY
  return IsWasmJSPIRequested(context) &&
         context->is_wasm_jspi_installed() != Smi::zero();
#else
  return false;
#endif
}

bool Isolate::IsWasmInliningEnabled(DirectHandle<NativeContext> context) {
  // If Wasm GC is explicitly enabled via a callback, also enable inlining.
#ifdef V8_ENABLE_WEBASSEMBLY
  return v8_flags.experimental_wasm_inlining;
#else
  return false;
#endif
}

bool Isolate::IsWasmImportedStringsEnabled(Handle<NativeContext> context) {
#ifdef V8_ENABLE_WEBASSEMBLY
  v8::WasmImportedStringsEnabledCallback callback =
      wasm_imported_strings_enabled_callback();
  if (callback) {
    v8::Local<v8::Context> api_context = v8::Utils::ToLocal(context);
    if (callback(api_context)) return true;
  }
  return v8_flags.experimental_wasm_imported_strings;
#else
  return false;
#endif
}

Handle<NativeContext> Isolate::GetIncumbentContextSlow() {
  JavaScriptStackFrameIterator it(this);

  // 1st candidate: most-recently-entered author function's context
  // if it's newer than the last Context::BackupIncumbentScope entry.
  //
  // NOTE: This code assumes that the stack grows downward.
  Address top_backup_incumbent =
      top_backup_incumbent_scope()
          ? top_backup_incumbent_scope()->JSStackComparableAddressPrivate()
          : 0;
  if (!it.done() &&
      (!top_backup_incumbent || it.frame()->sp() < top_backup_incumbent)) {
    Tagged<Context> context = Cast<Context>(it.frame()->context());
    // If the topmost_script_having_context is set then it must be correct.
    if (DEBUG_BOOL && !topmost_script_having_context().is_null()) {
      DCHECK_EQ(topmost_script_having_context()->native_context(),
                context->native_context());
    }
    return Handle<NativeContext>(context->native_context(), this);
  }
  DCHECK(topmost_script_having_context().is_null());

  // 2nd candidate: the last Context::Scope's incumbent context if any.
  if (top_backup_incumbent_scope()) {
    v8::Local<v8::Context> incumbent_context =
        top_backup_incumbent_scope()->backup_incumbent_context_;
    return Utils::OpenHandle(*incumbent_context);
  }

  // Last candidate: the entered context or microtask context.
  // Given that there is no other author function is running, there must be
  // no cross-context function running, then the incumbent realm must match
  // the entry realm.
  v8::Local<v8::Context> entered_context =
      reinterpret_cast<v8::Isolate*>(this)->GetEnteredOrMicrotaskContext();
  return Utils::OpenHandle(*entered_context);
}

char* Isolate::ArchiveThread(char* to) {
  MemCopy(to, reinterpret_cast<char*>(thread_local_top()),
          sizeof(ThreadLocalTop));
  return to + sizeof(ThreadLocalTop);
}

char* Isolate::RestoreThread(char* from) {
  MemCopy(reinterpret_cast<char*>(thread_local_top()), from,
          sizeof(ThreadLocalTop));
  DCHECK(context().is_null() || IsContext(context()));
  return from + sizeof(ThreadLocalTop);
}

void Isolate::ReleaseSharedPtrs() {
  base::MutexGuard lock(&managed_ptr_destructors_mutex_);
  while (managed_ptr_destructors_head_) {
    ManagedPtrDestructor* l = managed_ptr_destructors_head_;
    ManagedPtrDestructor* n = nullptr;
    managed_ptr_destructors_head_ = nullptr;
    for (; l != nullptr; l = n) {
      l->destructor_(l->shared_ptr_ptr_);
      n = l->next_;
      delete l;
    }
  }
}

bool Isolate::IsBuiltinTableHandleLocation(Address* handle_location) {
  FullObjectSlot location(handle_location);
  FullObjectSlot first_root(builtin_table());
  FullObjectSlot last_root(first_root + Builtins::kBuiltinCount);
  if (location >= last_root) return false;
  if (location < first_root) return false;
  return true;
}

void Isolate::RegisterManagedPtrDestructor(ManagedPtrDestructor* destructor) {
  base::MutexGuard lock(&managed_ptr_destructors_mutex_);
  DCHECK_NULL(destructor->prev_);
  DCHECK_NULL(destructor->next_);
  if (managed_ptr_destructors_head_) {
    managed_ptr_destructors_head_->prev_ = destructor;
  }
  destructor->next_ = managed_ptr_destructors_head_;
  managed_ptr_destructors_head_ = destructor;
}

void Isolate::UnregisterManagedPtrDestructor(ManagedPtrDestructor* destructor) {
  base::MutexGuard lock(&managed_ptr_destructors_mutex_);
  if (destructor->prev_) {
    destructor->prev_->next_ = destructor->next_;
  } else {
    DCHECK_EQ(destructor, managed_ptr_destructors_head_);
    managed_ptr_destructors_head_ = destructor->next_;
  }
  if (destructor->next_) destructor->next_->prev_ = destructor->prev_;
  destructor->prev_ = nullptr;
  destructor->next_ = nullptr;
}

#if V8_ENABLE_WEBASSEMBLY
bool Isolate::IsOnCentralStack(Address addr) {
#ifdef USE_SIMULATOR
  auto simulator_stack = Simulator::current(this)->GetCurrentStackView();
  uint8_t* addr_ptr = reinterpret_cast<uint8_t*>(addr);
  return simulator_stack.begin() < addr_ptr &&
         addr_ptr <= simulator_stack.end();
#else
  uintptr_t upper_bound = base::Stack::GetStackStart();
  uintptr_t lower_bound = upper_bound - v8_flags.stack_size * KB -
                          wasm::StackMemory::kJSLimitOffsetKB * KB;
  return lower_bound < addr && addr <= upper_bound;
#endif
}

bool Isolate::IsOnCentralStack() {
#if USE_SIMULATOR
  return IsOnCentralStack(Simulator::current(this)->get_sp());
#else
  return IsOnCentralStack(GetCurrentStackPosition());
#endif
}

void Isolate::AddSharedWasmMemory(Handle<WasmMemoryObject> memory_object) {
  Handle<WeakArrayList> shared_wasm_memories =
      factory()->shared_wasm_memories();
  shared_wasm_memories = WeakArrayList::Append(
      this, shared_wasm_memories, MaybeObjectHandle::Weak(memory_object));
  heap()->set_shared_wasm_memories(*shared_wasm_memories);
}

void Isolate::SyncStackLimit() {
  // Synchronize the stack limit with the active continuation for
  // stack-switching. This can be done before or after changing the stack
  // pointer itself, as long as we update both before the next stack check.
  // {StackGuard::SetStackLimitForStackSwitching} doesn't update the value of
  // the jslimit if it contains a sentinel value, and it is also thread-safe. So
  // if an interrupt is requested before, during or after this call, it will be
  // preserved and handled at the next stack check.

  DisallowGarbageCollection no_gc;
  auto continuation =
      Cast<WasmContinuationObject>(root(RootIndex::kActiveContinuation));
  wasm::StackMemory* stack =
      reinterpret_cast<wasm::StackMemory*>(continuation->stack());
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Switch to stack #%d\n", stack->id());
  }
  uintptr_t limit = reinterpret_cast<uintptr_t>(stack->jmpbuf()->stack_limit);
  stack_guard()->SetStackLimitForStackSwitching(limit);
  UpdateCentralStackInfo();
}

void Isolate::UpdateCentralStackInfo() {
  Tagged<Object> current = root(RootIndex::kActiveContinuation);
  DCHECK(!IsUndefined(current));
  wasm::StackMemory* wasm_stack = reinterpret_cast<wasm::StackMemory*>(
      Cast<WasmContinuationObject>(current)->stack());
  current = Cast<WasmContinuationObject>(current)->parent();
  thread_local_top()->is_on_central_stack_flag_ =
      IsOnCentralStack(wasm_stack->jmpbuf()->sp);
  // Update the central stack info on switch. Only consider the innermost stack
  bool updated_central_stack = false;
  // We don't need to add all inactive stacks. Only the ones in the active chain
  // may contain cpp heap pointers.
  while (!IsUndefined(current)) {
    auto cont = Cast<WasmContinuationObject>(current);
    auto* wasm_stack = reinterpret_cast<wasm::StackMemory*>(cont->stack());
    // On x64 and arm64 we don't need to record the stack segments for
    // conservative stack scanning. We switch to the central stack for foreign
    // calls, so secondary stacks only contain wasm frames which use the precise
    // GC.
    current = cont->parent();
    if (!updated_central_stack && IsOnCentralStack(wasm_stack->jmpbuf()->sp)) {
      // This is the most recent use of the central stack in the call chain.
      // Switch to this SP if we need to switch to the central stack in the
      // future.
      thread_local_top()->central_stack_sp_ = wasm_stack->jmpbuf()->sp;
      thread_local_top()->central_stack_limit_ =
          reinterpret_cast<Address>(wasm_stack->jmpbuf()->stack_limit);
      updated_central_stack = true;
    }
  }
}

void Isolate::RetireWasmStack(wasm::StackMemory* stack) {
  stack->jmpbuf()->state = wasm::JumpBuffer::Retired;
  size_t index = stack->index();
  // We can only return from a stack that was still in the global list.
  DCHECK_LT(index, wasm_stacks().size());
  std::unique_ptr<wasm::StackMemory> stack_ptr =
      std::move(wasm_stacks()[index]);
  DCHECK_EQ(stack_ptr.get(), stack);
  if (index != wasm_stacks().size() - 1) {
    wasm_stacks()[index] = std::move(wasm_stacks().back());
    wasm_stacks()[index]->set_index(index);
  }
  wasm_stacks().pop_back();
  for (size_t i = 0; i < wasm_stacks().size(); ++i) {
    SLOW_DCHECK(wasm_stacks()[i]->index() == i);
  }
  stack_pool().Add(std::move(stack_ptr));
}

wasm::WasmOrphanedGlobalHandle* Isolate::NewWasmOrphanedGlobalHandle() {
  return wasm::WasmEngine::NewOrphanedGlobalHandle(&wasm_orphaned_handle_);
}

#endif  // V8_ENABLE_WEBASSEMBLY

Isolate::PerIsolateThreadData::~PerIsolateThreadData() {
#if defined(USE_SIMULATOR)
  delete simulator_;
#endif
}

Isolate::PerIsolateThreadData* Isolate::ThreadDataTable::Lookup(
    ThreadId thread_id) {
  auto t = table_.find(thread_id);
  if (t == table_.end()) return nullptr;
  return t->second;
}

void Isolate::ThreadDataTable::Insert(Isolate::PerIsolateThreadData* data) {
  bool inserted = table_.insert(std::make_pair(data->thread_id_, data)).second;
  CHECK(inserted);
}

void Isolate::ThreadDataTable::Remove(PerIsolateThreadData* data) {
  table_.erase(data->thread_id_);
  delete data;
}

void Isolate::ThreadDataTable::RemoveAllThreads() {
  for (auto& x : table_) {
    delete x.second;
  }
  table_.clear();
}

class TracingAccountingAllocator : public AccountingAllocator {
 public:
  explicit TracingAccountingAllocator(Isolate* isolate) : isolate_(isolate) {}
  ~TracingAccountingAllocator() = default;

 protected:
  void TraceAllocateSegmentImpl(v8::internal::Segment* segment) override {
    base::MutexGuard lock(&mutex_);
    UpdateMemoryTrafficAndReportMemoryUsage(segment->total_size());
  }

  void TraceZoneCreationImpl(const Zone* zone) override {
    base::MutexGuard lock(&mutex_);
    active_zones_.insert(zone);
    nesting_depth_++;
  }

  void TraceZoneDestructionImpl(const Zone* zone) override {
    base::MutexGuard lock(&mutex_);
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    if (v8_flags.trace_zone_type_stats) {
      type_stats_.MergeWith(zone->type_stats());
    }
#endif
    UpdateMemoryTrafficAndReportMemoryUsage(zone->segment_bytes_allocated());
    active_zones_.erase(zone);
    nesting_depth_--;

#ifdef V8_ENABLE_PRECISE_ZONE_STATS
    if (v8_flags.trace_zone_type_stats && active_zones_.empty()) {
      type_stats_.Dump();
    }
#endif
  }

 private:
  void UpdateMemoryTrafficAndReportMemoryUsage(size_t memory_traffic_delta) {
    if (!v8_flags.trace_zone_stats &&
        !(TracingFlags::zone_stats.load(std::memory_order_relaxed) &
          v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
      // Don't print anything if the zone tracing was enabled only because of
      // v8_flags.trace_zone_type_stats.
      return;
    }

    memory_traffic_since_last_report_ += memory_traffic_delta;
    if (memory_traffic_since_last_report_ < v8_flags.zone_stats_tolerance)
      return;
    memory_traffic_since_last_report_ = 0;

    Dump(buffer_, true);

    {
      std::string trace_str = buffer_.str();

      if (v8_flags.trace_zone_stats) {
        PrintF(
            "{"
            "\"type\": \"v8-zone-trace\", "
            "\"stats\": %s"
            "}\n",
            trace_str.c_str());
      }
      if (V8_UNLIKELY(
              TracingFlags::zone_stats.load(std::memory_order_relaxed) &
              v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
        TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.zone_stats"),
                             "V8.Zone_Stats", TRACE_EVENT_SCOPE_THREAD, "stats",
                             TRACE_STR_COPY(trace_str.c_str()));
      }
    }

    // Clear the buffer.
    buffer_.str(std::string());
  }

  void Dump(std::ostringstream& out, bool dump_details) {
    // Note: Neither isolate nor zones are locked, so be careful with accesses
    // as the allocator is potentially used on a concurrent thread.
    double time = isolate_->time_millis_since_init();
    out << "{"
        << "\"isolate\": \"" << reinterpret_cast<void*>(isolate_) << "\", "
        << "\"time\": " << time << ", ";
    size_t total_segment_bytes_allocated = 0;
    size_t total_zone_allocation_size = 0;
    size_t total_zone_freed_size = 0;

    if (dump_details) {
      // Print detailed zone stats if memory usage changes direction.
      out << "\"zones\": [";
      bool first = true;
      for (const Zone* zone : active_zones_) {
        size_t zone_segment_bytes_allocated = zone->segment_bytes_allocated();
        size_t zone_allocation_size = zone->allocation_size_for_tracing();
        size_t freed_size = zone->freed_size_for_tracing();
        if (first) {
          first = false;
        } else {
          out << ", ";
        }
        out << "{"
            << "\"name\": \"" << zone->name() << "\", "
            << "\"allocated\": " << zone_segment_bytes_allocated << ", "
            << "\"used\": " << zone_allocation_size << ", "
            << "\"freed\": " << freed_size << "}";
        total_segment_bytes_allocated += zone_segment_bytes_allocated;
        total_zone_allocation_size += zone_allocation_size;
        total_zone_freed_size += freed_size;
      }
      out << "], ";
    } else {
      // Just calculate total allocated/used memory values.
      for (const Zone* zone : active_zones_) {
        total_segment_bytes_allocated += zone->segment_bytes_allocated();
        total_zone_allocation_size += zone->allocation_size_for_tracing();
        total_zone_freed_size += zone->freed_size_for_tracing();
      }
    }
    out << "\"allocated\": " << total_segment_bytes_allocated << ", "
        << "\"used\": " << total_zone_allocation_size << ", "
        << "\"freed\": " << total_zone_freed_size << "}";
  }

  Isolate* const isolate_;
  std::atomic<size_t> nesting_depth_{0};

  base::Mutex mutex_;
  std::unordered_set<const Zone*> active_zones_;
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  TypeStats type_stats_;
#endif
  std::ostringstream buffer_;
  // This value is increased on both allocations and deallocations.
  size_t memory_traffic_since_last_report_ = 0;
};

#ifdef DEBUG
std::atomic<size_t> Isolate::non_disposed_isolates_;
#endif  // DEBUG

namespace {
bool HasFlagThatRequiresSharedHeap() {
  return v8_flags.shared_string_table || v8_flags.harmony_struct;
}

IsolateGroup* AcquireGroupForNewIsolate() {
  IsolateGroup* group = IsolateGroup::AcquireGlobal();
  if (group) return group;
  return IsolateGroup::New();
}
}  // namespace

// static
Isolate* Isolate::New() { return Allocate(); }

// static
Isolate* Isolate::Allocate() {
  // v8::V8::Initialize() must be called before creating any isolates.
  DCHECK_NOT_NULL(V8::GetCurrentPlatform());
  IsolateGroup* group = AcquireGroupForNewIsolate();
  // Allocate Isolate itself on C++ heap, ensuring page alignment.
  void* isolate_ptr = base::AlignedAlloc(sizeof(Isolate), kMinimumOSPageSize);
  // IsolateAllocator manages the virtual memory resources for the Isolate.
  Isolate* isolate = new (isolate_ptr) Isolate(group);

#ifdef DEBUG
  non_disposed_isolates_++;
#endif  // DEBUG

  return isolate;
}

// static
void Isolate::Delete(Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  // v8::V8::Dispose() must only be called after deleting all isolates.
  DCHECK_NOT_NULL(V8::GetCurrentPlatform());
  // Temporarily set this isolate as current so that various parts of
  // the isolate can access it in their destructors without having a
  // direct pointer. We don't use Enter/Exit here to avoid
  // initializing the thread data.
  PerIsolateThreadData* saved_data = isolate->CurrentPerIsolateThreadData();
  Isolate* saved_isolate = isolate->TryGetCurrent();
  SetIsolateThreadLocals(isolate, nullptr);
  isolate->set_thread_id(ThreadId::Current());
  isolate->heap()->SetStackStart();

  isolate->Deinit();

#ifdef DEBUG
  non_disposed_isolates_--;
#endif  // DEBUG

  IsolateGroup* group = isolate->isolate_group();
  isolate->~Isolate();
  // Only release the group once all other Isolate members have been destroyed.
  group->Release();
  // Free the isolate itself.
  base::AlignedFree(isolate);

  // Restore the previous current isolate.
  SetIsolateThreadLocals(saved_isolate, saved_data);
}

void Isolate::SetUpFromReadOnlyArtifacts(
    std::shared_ptr<ReadOnlyArtifacts> artifacts, ReadOnlyHeap* ro_heap) {
  if (ReadOnlyHeap::IsReadOnlySpaceShared()) {
    DCHECK_NOT_NULL(artifacts);
    artifacts_ = artifacts;
    InitializeNextUniqueSfiId(artifacts->initial_next_unique_sfi_id());
  } else {
    DCHECK_NULL(artifacts);
  }
  DCHECK_NOT_NULL(ro_heap);
  DCHECK_IMPLIES(read_only_heap_ != nullptr, read_only_heap_ == ro_heap);
  read_only_heap_ = ro_heap;
  heap_.SetUpFromReadOnlyHeap(read_only_heap_);
}

v8::PageAllocator* Isolate::page_allocator() const {
  return isolate_group()->page_allocator();
}

Isolate::Isolate(IsolateGroup* isolate_group)
    : isolate_data_(this, isolate_group),
      isolate_group_(isolate_group),
      id_(isolate_counter.fetch_add(1, std::memory_order_relaxed)),
      allocator_(new TracingAccountingAllocator(this)),
      traced_handles_(this),
      builtins_(this),
#if defined(DEBUG) || defined(VERIFY_HEAP)
      num_active_deserializers_(0),
#endif
      rail_mode_(PERFORMANCE_ANIMATION),
      logger_(new Logger()),
      detailed_source_positions_for_profiling_(v8_flags.detailed_line_info),
      persistent_handles_list_(new PersistentHandlesList()),
      jitless_(v8_flags.jitless),
      next_unique_sfi_id_(0),
      next_module_async_evaluation_ordinal_(
          SourceTextModule::kFirstAsyncEvaluationOrdinal),
      cancelable_task_manager_(new CancelableTaskManager()) {
  TRACE_ISOLATE(constructor);
  CheckIsolateLayout();

  // ThreadManager is initialized early to support locking an isolate
  // before it is entered.
  thread_manager_ = new ThreadManager(this);

  handle_scope_data()->Initialize();

#define ISOLATE_INIT_EXECUTE(type, name, initial_value) \
  name##_ = (initial_value);
  ISOLATE_INIT_LIST(ISOLATE_INIT_EXECUTE)
#undef ISOLATE_INIT_EXECUTE

#define ISOLATE_INIT_ARRAY_EXECUTE(type, name, length) \
  memset(name##_, 0, sizeof(type) * length);
  ISOLATE_INIT_ARRAY_LIST(ISOLATE_INIT_ARRAY_EXECUTE)
#undef ISOLATE_INIT_ARRAY_EXECUTE

  InitializeLoggingAndCounters();
  debug_ = new Debug(this);

  InitializeDefaultEmbeddedBlob();

#if V8_ENABLE_WEBASSEMBLY
  // If we are in production V8 and not in mksnapshot we have to pass the
  // landing pad builtin to the WebAssembly TrapHandler.
  // TODO(ahaas): Isolate creation is the earliest point in time when builtins
  // are available, so we cannot set the landing pad earlier at the moment.
  // However, if builtins ever get loaded during process initialization time,
  // then the initialization of the trap handler landing pad should also go
  // there.
  // TODO(ahaas): The code of the landing pad does not have to be a builtin,
  // we could also just move it to the trap handler, and implement it e.g. with
  // inline assembly. It's not clear if that's worth it.
  if (Isolate::CurrentEmbeddedBlobCodeSize()) {
    EmbeddedData embedded_data = EmbeddedData::FromBlob();
    Address landing_pad =
        embedded_data.InstructionStartOf(Builtin::kWasmTrapHandlerLandingPad);
    i::trap_handler::SetLandingPad(landing_pad);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  MicrotaskQueue::SetUpDefaultMicrotaskQueue(this);
}

void Isolate::CheckIsolateLayout() {
#ifdef V8_ENABLE_SANDBOX
  CHECK_EQ(static_cast<int>(OFFSET_OF(ExternalPointerTable, base_)),
           Internals::kExternalPointerTableBasePointerOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(TrustedPointerTable, base_)),
           Internals::kTrustedPointerTableBasePointerOffset);
  CHECK_EQ(static_cast<int>(sizeof(ExternalPointerTable)),
           Internals::kExternalPointerTableSize);
  CHECK_EQ(static_cast<int>(sizeof(ExternalPointerTable)),
           ExternalPointerTable::kSize);
  CHECK_EQ(static_cast<int>(sizeof(TrustedPointerTable)),
           Internals::kTrustedPointerTableSize);
  CHECK_EQ(static_cast<int>(sizeof(TrustedPointerTable)),
           TrustedPointerTable::kSize);
#endif

  CHECK_EQ(OFFSET_OF(Isolate, isolate_data_), 0);
  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.stack_guard_)),
           Internals::kIsolateStackGuardOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.is_marking_flag_)),
           Internals::kVariousBooleanFlagsOffset);
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.error_message_param_)),
      Internals::kErrorMessageParamOffset);
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.builtin_tier0_entry_table_)),
           Internals::kBuiltinTier0EntryTableOffset);
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.builtin_tier0_table_)),
      Internals::kBuiltinTier0TableOffset);
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.new_allocation_info_)),
      Internals::kNewAllocationInfoOffset);
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.old_allocation_info_)),
      Internals::kOldAllocationInfoOffset);
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.fast_c_call_caller_fp_)),
           Internals::kIsolateFastCCallCallerFpOffset);
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.fast_c_call_caller_pc_)),
           Internals::kIsolateFastCCallCallerPcOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.cage_base_)),
           Internals::kIsolateCageBaseOffset);
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.long_task_stats_counter_)),
           Internals::kIsolateLongTaskStatsCounterOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.stack_guard_)),
           Internals::kIsolateStackGuardOffset);

  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.thread_local_top_)),
      Internals::kIsolateThreadLocalTopOffset);
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.handle_scope_data_)),
      Internals::kIsolateHandleScopeDataOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.embedder_data_)),
           Internals::kIsolateEmbedderDataOffset);
#ifdef V8_COMPRESS_POINTERS
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.external_pointer_table_)),
           Internals::kIsolateExternalPointerTableOffset);
#endif
#ifdef V8_ENABLE_SANDBOX
  CHECK_EQ(
      static_cast<int>(OFFSET_OF(Isolate, isolate_data_.trusted_cage_base_)),
      Internals::kIsolateTrustedCageBaseOffset);

  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.trusted_pointer_table_)),
           Internals::kIsolateTrustedPointerTableOffset);
#endif
  CHECK_EQ(static_cast<int>(
               OFFSET_OF(Isolate, isolate_data_.api_callback_thunk_argument_)),
           Internals::kIsolateApiCallbackThunkArgumentOffset);
  CHECK_EQ(static_cast<int>(OFFSET_OF(
               Isolate, isolate_data_.continuation_preserved_embedder_data_)),
           Internals::kContinuationPreservedEmbedderDataOffset);

  CHECK_EQ(static_cast<int>(OFFSET_OF(Isolate, isolate_data_.roots_table_)),
           Internals::kIsolateRootsOffset);

  CHECK(IsAligned(reinterpret_cast<Address>(&isolate_data_),
                  kIsolateDataAlignment));

  static_assert(Internals::kStackGuardSize == sizeof(StackGuard));
  static_assert(Internals::kBuiltinTier0TableSize ==
                Builtins::kBuiltinTier0Count * kSystemPointerSize);
  static_assert(Internals::kBuiltinTier0EntryTableSize ==
                Builtins::kBuiltinTier0Count * kSystemPointerSize);

  // Ensure that certain hot IsolateData fields fall into the same CPU cache
  // line.
  constexpr size_t kCacheLineSize = 64;
  static_assert(OFFSET_OF(Isolate, isolate_data_) == 0);

  // Fields written on every CEntry/CallApiCallback/CallApiGetter call.
  // See MacroAssembler::EnterExitFrame/LeaveExitFrame.
  constexpr size_t kCEntryFPCacheLine = RoundDown<kCacheLineSize>(
      OFFSET_OF(IsolateData, thread_local_top_.c_entry_fp_));
  static_assert(kCEntryFPCacheLine ==
                RoundDown<kCacheLineSize>(
                    OFFSET_OF(IsolateData, thread_local_top_.c_function_)));
  static_assert(kCEntryFPCacheLine ==
                RoundDown<kCacheLineSize>(
                    OFFSET_OF(IsolateData, thread_local_top_.context_)));
  static_assert(
      kCEntryFPCacheLine ==
      RoundDown<kCacheLineSize>(OFFSET_OF(
          IsolateData, thread_local_top_.topmost_script_having_context_)));
  static_assert(kCEntryFPCacheLine ==
                RoundDown<kCacheLineSize>(
                    OFFSET_OF(IsolateData, thread_local_top_.last_api_entry_)));

  // Fields written on every MacroAssembler::CallCFunction call.
  static_assert(RoundDown<kCacheLineSize>(
                    OFFSET_OF(IsolateData, fast_c_call_caller_fp_)) ==
                RoundDown<kCacheLineSize>(
                    OFFSET_OF(IsolateData, fast_c_call_caller_pc_)));

  // LinearAllocationArea objects must not cross cache line boundary.
  static_assert(
      RoundDown<kCacheLineSize>(OFFSET_OF(IsolateData, new_allocation_info_)) ==
      RoundDown<kCacheLineSize>(OFFSET_OF(IsolateData, new_allocation_info_) +
                                sizeof(LinearAllocationArea) - 1));
  static_assert(
      RoundDown<kCacheLineSize>(OFFSET_OF(IsolateData, old_allocation_info_)) ==
      RoundDown<kCacheLineSize>(OFFSET_OF(IsolateData, old_allocation_info_) +
                                sizeof(LinearAllocationArea) - 1));
}

void Isolate::ClearSerializerData() {
  delete external_reference_map_;
  external_reference_map_ = nullptr;
}

// When profiling status changes, call this function to update the single bool
// cache.
void Isolate::UpdateLogObjectRelocation() {
  log_object_relocation_ = v8_flags.verify_predictable ||
                           IsLoggingCodeCreation() ||
                           v8_file_logger()->is_logging() ||
                           (heap_profiler() != nullptr &&
                            heap_profiler()->is_tracking_object_moves()) ||
                           heap()->has_heap_object_allocation_tracker();
}

void Isolate::Deinit() {
  TRACE_ISOLATE(deinit);

#if defined(V8_USE_PERFETTO)
  PerfettoLogger::UnregisterIsolate(this);
#endif  // defined(V8_USE_PERFETTO)

  // All client isolates should already be detached when the shared heap isolate
  // tears down.
  if (is_shared_space_isolate()) {
    global_safepoint()->AssertNoClientsOnTearDown();
  }

  if (has_shared_space() && !is_shared_space_isolate()) {
    IgnoreLocalGCRequests ignore_gc_requests(heap());
    main_thread_local_heap()->ExecuteMainThreadWhileParked([this]() {
      shared_space_isolate()->global_safepoint()->clients_mutex_.Lock();
    });
  }

  // We start with the heap tear down so that releasing managed objects does
  // not cause a GC.
  heap_.StartTearDown();

  DisallowGarbageCollection no_gc;
  IgnoreLocalGCRequests ignore_gc_requests(heap());

#if V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    wasm::WasmInterpreter::NotifyIsolateDisposal(this);
  } else if (v8_flags.wasm_enable_exec_time_histograms &&
             v8_flags.slow_histograms) {
    wasm_execution_timer_->Terminate();
  }
#endif  // V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE

  tracing_cpu_profiler_.reset();
  if (v8_flags.stress_sampling_allocation_profiler > 0) {
    heap_profiler()->StopSamplingHeapProfiler();
  }

  metrics_recorder_->NotifyIsolateDisposal();
  recorder_context_id_map_.clear();

  FutexEmulation::IsolateDeinit(this);
  if (v8_flags.harmony_struct) {
    JSSynchronizationPrimitive::IsolateDeinit(this);
  } else {
    DCHECK(async_waiter_queue_nodes_.empty());
  }

  debug()->Unload();

#if V8_ENABLE_WEBASSEMBLY
  wasm::GetWasmEngine()->DeleteCompileJobsOnIsolate(this);

  BackingStore::RemoveSharedWasmMemoryObjects(this);
#endif  // V8_ENABLE_WEBASSEMBLY

  if (concurrent_recompilation_enabled()) {
    optimizing_compile_dispatcher_->Stop();
    delete optimizing_compile_dispatcher_;
    optimizing_compile_dispatcher_ = nullptr;
  }

  if (v8_flags.print_deopt_stress) {
    PrintF(stdout, "=== Stress deopt counter: %u\n", stress_deopt_count_);
  }

  // We must stop the logger before we tear down other components.
  sampler::Sampler* sampler = v8_file_logger_->sampler();
  if (sampler && sampler->IsActive()) sampler->Stop();
  v8_file_logger_->StopProfilerThread();

  FreeThreadResources();

  // Stop concurrent tasks before destroying resources since they might still
  // use those.
  cancelable_task_manager()->CancelAndWait();

  // Cancel all compiler tasks.
#ifdef V8_ENABLE_SPARKPLUG
  delete baseline_batch_compiler_;
  baseline_batch_compiler_ = nullptr;
#endif  // V8_ENABLE_SPARKPLUG

#ifdef V8_ENABLE_MAGLEV
  delete maglev_concurrent_dispatcher_;
  maglev_concurrent_dispatcher_ = nullptr;
#endif  // V8_ENABLE_MAGLEV

  if (lazy_compile_dispatcher_) {
    lazy_compile_dispatcher_->AbortAll();
    lazy_compile_dispatcher_.reset();
  }

  // At this point there are no more background threads left in this isolate.
  heap_.safepoint()->AssertMainThreadIsOnlyThread();

  // Tear down data that requires the shared heap before detaching.
  heap_.TearDownWithSharedHeap();
  DumpAndResetBuiltinsProfileData();

  // Detach from the shared heap isolate and then unlock the mutex.
  if (has_shared_space() && !is_shared_space_isolate()) {
    GlobalSafepoint* global_safepoint =
        this->shared_space_isolate()->global_safepoint();
    global_safepoint->RemoveClient(this);
    global_safepoint->clients_mutex_.Unlock();
  }

  shared_space_isolate_.reset();

  // Since there are no other threads left, we can lock this mutex without any
  // ceremony. This signals to the tear down code that we are in a safepoint.
  base::RecursiveMutexGuard safepoint(&heap_.safepoint()->local_heaps_mutex_);

  ReleaseSharedPtrs();

  builtins_.TearDown();
  bootstrapper_->TearDown();

  if (tiering_manager_ != nullptr) {
    delete tiering_manager_;
    tiering_manager_ = nullptr;
  }

  delete heap_profiler_;
  heap_profiler_ = nullptr;

#if USE_SIMULATOR
  delete simulator_data_;
  simulator_data_ = nullptr;
#endif

  // After all concurrent tasks are stopped, we know for sure that stats aren't
  // updated anymore.
  DumpAndResetStats();

  heap_.TearDown();

  delete inner_pointer_to_code_cache_;
  inner_pointer_to_code_cache_ = nullptr;

  main_thread_local_isolate_.reset();

  FILE* logfile = v8_file_logger_->TearDownAndGetLogFile();
  if (logfile != nullptr) base::Fclose(logfile);

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
  if (v8_flags.enable_etw_stack_walking) {
    ETWJITInterface::RemoveIsolate(this);
  }
#endif  // defined(V8_OS_WIN)

#if V8_ENABLE_WEBASSEMBLY
  wasm::GetWasmEngine()->RemoveIsolate(this);

  delete wasm_code_look_up_cache_;
  wasm_code_look_up_cache_ = nullptr;
#endif  // V8_ENABLE_WEBASSEMBLY

  TearDownEmbeddedBlob();

  delete interpreter_;
  interpreter_ = nullptr;

  delete ast_string_constants_;
  ast_string_constants_ = nullptr;

  delete logger_;
  logger_ = nullptr;

  delete root_index_map_;
  root_index_map_ = nullptr;

  delete compiler_zone_;
  compiler_zone_ = nullptr;
  compiler_cache_ = nullptr;

  SetCodePages(nullptr);

  ClearSerializerData();

  if (OwnsStringTables()) {
    string_forwarding_table()->TearDown();
  } else {
    DCHECK_NULL(string_table_.get());
    DCHECK_NULL(string_forwarding_table_.get());
  }

  if (!is_shared_space_isolate()) {
    DCHECK_NULL(shared_struct_type_registry_.get());
  }

#ifdef V8_COMPRESS_POINTERS
  external_pointer_table().TearDownSpace(
      heap()->young_external_pointer_space());
  external_pointer_table().TearDownSpace(heap()->old_external_pointer_space());
  external_pointer_table().DetachSpaceFromReadOnlySegment(
      heap()->read_only_external_pointer_space());
  external_pointer_table().TearDownSpace(
      heap()->read_only_external_pointer_space());
  external_pointer_table().TearDown();
  if (owns_shareable_data()) {
    shared_external_pointer_table().TearDownSpace(
        shared_external_pointer_space());
    shared_external_pointer_table().TearDown();
    delete isolate_data_.shared_external_pointer_table_;
    isolate_data_.shared_external_pointer_table_ = nullptr;
    delete shared_external_pointer_space_;
    shared_external_pointer_space_ = nullptr;
  }
  cpp_heap_pointer_table().TearDownSpace(heap()->cpp_heap_pointer_space());
  cpp_heap_pointer_table().TearDown();
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  trusted_pointer_table().TearDownSpace(heap()->trusted_pointer_space());
  trusted_pointer_table().TearDown();

  GetProcessWideCodePointerTable()->TearDownSpace(heap()->code_pointer_space());
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  GetProcessWideJSDispatchTable()->TearDownSpace(
      heap()->js_dispatch_table_space());
#endif  // V8_ENABLE_LEAPTIERING

  {
    base::MutexGuard lock_guard(&thread_data_table_mutex_);
    thread_data_table_.RemoveAllThreads();
  }
}

void Isolate::SetIsolateThreadLocals(Isolate* isolate,
                                     PerIsolateThreadData* data) {
  g_current_isolate_ = isolate;
  g_current_per_isolate_thread_data_ = data;

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  if (isolate) {
    V8HeapCompressionScheme::InitBase(isolate->cage_base());
#ifdef V8_EXTERNAL_CODE_SPACE
    ExternalCodeCompressionScheme::InitBase(isolate->code_cage_base());
#endif  // V8_EXTERNAL_CODE_SPACE
  } else {
    V8HeapCompressionScheme::InitBase(kNullAddress);
#ifdef V8_EXTERNAL_CODE_SPACE
    ExternalCodeCompressionScheme::InitBase(kNullAddress);
#endif  // V8_EXTERNAL_CODE_SPACE
  }
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

  if (isolate && isolate->main_thread_local_isolate()) {
    WriteBarrier::SetForThread(
        isolate->main_thread_local_heap()->marking_barrier());
  } else {
    WriteBarrier::SetForThread(nullptr);
  }
}

Isolate::~Isolate() {
  TRACE_ISOLATE(destructor);
  DCHECK_NULL(current_deoptimizer_);

  // The entry stack must be empty when we get here.
  DCHECK(entry_stack_ == nullptr ||
         entry_stack_.load()->previous_item == nullptr);

  delete entry_stack_;
  entry_stack_ = nullptr;

  delete date_cache_;
  date_cache_ = nullptr;

  delete regexp_stack_;
  regexp_stack_ = nullptr;

  delete descriptor_lookup_cache_;
  descriptor_lookup_cache_ = nullptr;

  delete load_stub_cache_;
  load_stub_cache_ = nullptr;
  delete store_stub_cache_;
  store_stub_cache_ = nullptr;
  delete define_own_stub_cache_;
  define_own_stub_cache_ = nullptr;

  delete materialized_object_store_;
  materialized_object_store_ = nullptr;

  delete v8_file_logger_;
  v8_file_logger_ = nullptr;

  delete handle_scope_implementer_;
  handle_scope_implementer_ = nullptr;

  delete code_tracer();
  set_code_tracer(nullptr);

  delete compilation_cache_;
  compilation_cache_ = nullptr;
  delete bootstrapper_;
  bootstrapper_ = nullptr;

  delete thread_manager_;
  thread_manager_ = nullptr;

  bigint_processor_->Destroy();

  delete global_handles_;
  global_handles_ = nullptr;
  delete eternal_handles_;
  eternal_handles_ = nullptr;

#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine::FreeAllOrphanedGlobalHandles(wasm_orphaned_handle_);
#endif

  delete string_stream_debug_object_cache_;
  string_stream_debug_object_cache_ = nullptr;

  delete random_number_generator_;
  random_number_generator_ = nullptr;

  delete fuzzer_rng_;
  fuzzer_rng_ = nullptr;

  delete debug_;
  debug_ = nullptr;

  delete cancelable_task_manager_;
  cancelable_task_manager_ = nullptr;

  delete allocator_;
  allocator_ = nullptr;

  // Assert that |default_microtask_queue_| is the last MicrotaskQueue instance.
  DCHECK_IMPLIES(default_microtask_queue_,
                 default_microtask_queue_ == default_microtask_queue_->next());
  delete default_microtask_queue_;
  default_microtask_queue_ = nullptr;

  // The ReadOnlyHeap should not be destroyed when sharing without pointer
  // compression as the object itself is shared.
  if (read_only_heap_->IsOwnedByIsolate()) {
    delete read_only_heap_;
    read_only_heap_ = nullptr;
  }

  // isolate_group_ released in caller, to ensure that all member destructors
  // run before potentially unmapping the isolate's VirtualMemoryArea.
}

void Isolate::InitializeThreadLocal() {
  thread_local_top()->Initialize(this);
#ifdef DEBUG
  // This method might be called on a thread that's not bound to any Isolate
  // and thus pointer compression schemes might have cage base value unset.
  // Read-only roots accessors contain type DCHECKs which require access to
  // V8 heap in order to check the object type. So, allow heap access here
  // to let the checks work.
  i::PtrComprCageAccessScope ptr_compr_cage_access_scope(this);
#endif  // DEBUG
  clear_exception();
  clear_pending_message();
}

void Isolate::SetTerminationOnExternalTryCatch() {
  DCHECK_IMPLIES(v8_flags.strict_termination_checks,
                 is_execution_terminating());
  if (try_catch_handler() == nullptr) return;
  try_catch_handler()->can_continue_ = false;
  try_catch_handler()->exception_ = reinterpret_cast<void*>(
      ReadOnlyRoots(heap()).termination_exception().ptr());
}

bool Isolate::PropagateExceptionToExternalTryCatch(
    ExceptionHandlerType top_handler) {
  Tagged<Object> exception = this->exception();

  if (top_handler == ExceptionHandlerType::kJavaScriptHandler) return false;
  if (top_handler == ExceptionHandlerType::kNone) return true;

  DCHECK_EQ(ExceptionHandlerType::kExternalTryCatch, top_handler);
  if (!is_catchable_by_javascript(exception)) {
    SetTerminationOnExternalTryCatch();
  } else {
    v8::TryCatch* handler = try_catch_handler();
    DCHECK(IsJSMessageObject(pending_message()) ||
           IsTheHole(pending_message(), this));
    handler->can_continue_ = true;
    handler->exception_ = reinterpret_cast<void*>(exception.ptr());
    // Propagate to the external try-catch only if we got an actual message.
    if (!has_pending_message()) return true;
    handler->message_obj_ = reinterpret_cast<void*>(pending_message().ptr());
  }
  return true;
}

namespace {

inline Tagged<FunctionTemplateInfo> GetTargetFunctionTemplateInfo(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  Tagged<Object> target = FunctionCallbackArguments::GetTarget(info);
  if (IsFunctionTemplateInfo(target)) {
    return Cast<FunctionTemplateInfo>(target);
  }
  CHECK(Is<JSFunction>(target));
  Tagged<SharedFunctionInfo> shared_info = Cast<JSFunction>(target)->shared();
  return shared_info->api_func_data();
}

}  // namespace

void Isolate::NotifyExceptionPropagationCallback() {
  DCHECK_NOT_NULL(exception_propagation_callback_);

  // Try to figure out whether the exception was thrown directly from an
  // Api callback and if it's the case then call the
  // |exception_propagation_callback_| with relevant data.

  ExternalCallbackScope* ext_callback_scope = external_callback_scope();
  StackFrameIterator it(this);

  if (it.done() && !ext_callback_scope) {
    // The exception was thrown directly by embedder code without crossing
    // "C++ -> JS" or "C++ -> Api callback" boundary.
    return;
  }
  if (it.done() || (ext_callback_scope &&
                    ext_callback_scope->scope_address() < it.frame()->fp())) {
    // There were no crossings of "C++ -> JS" boundary at all or they happened
    // earlier than the last crossing of the  "C++ -> Api callback" boundary.
    // In this case all the data about Api callback is available in the
    // |ext_callback_scope| object.
    DCHECK_NOT_NULL(ext_callback_scope);
    v8::ExceptionContext kind = ext_callback_scope->exception_context();
    switch (kind) {
      case v8::ExceptionContext::kConstructor:
      case v8::ExceptionContext::kOperation: {
        DCHECK_NOT_NULL(ext_callback_scope->callback_info());
        auto callback_info =
            reinterpret_cast<const v8::FunctionCallbackInfo<v8::Value>*>(
                ext_callback_scope->callback_info());

        DirectHandle<JSReceiver> receiver =
            Utils::OpenDirectHandle(*callback_info->This());
        DirectHandle<FunctionTemplateInfo> function_template_info(
            GetTargetFunctionTemplateInfo(*callback_info), this);
        ReportExceptionFunctionCallback(receiver, function_template_info, kind);
        return;
      }
      case v8::ExceptionContext::kAttributeGet:
      case v8::ExceptionContext::kAttributeSet:
      case v8::ExceptionContext::kIndexedQuery:
      case v8::ExceptionContext::kIndexedGetter:
      case v8::ExceptionContext::kIndexedDescriptor:
      case v8::ExceptionContext::kIndexedSetter:
      case v8::ExceptionContext::kIndexedDefiner:
      case v8::ExceptionContext::kIndexedDeleter:
      case v8::ExceptionContext::kNamedQuery:
      case v8::ExceptionContext::kNamedGetter:
      case v8::ExceptionContext::kNamedDescriptor:
      case v8::ExceptionContext::kNamedSetter:
      case v8::ExceptionContext::kNamedDefiner:
      case v8::ExceptionContext::kNamedDeleter:
      case v8::ExceptionContext::kNamedEnumerator: {
        DCHECK_NOT_NULL(ext_callback_scope->callback_info());
        auto callback_info =
            reinterpret_cast<const v8::PropertyCallbackInfo<v8::Value>*>(
                ext_callback_scope->callback_info());

        // Allow usages of v8::PropertyCallbackInfo<T>::Holder() for now.
        // TODO(https://crbug.com/333672197): remove.
        START_ALLOW_USE_DEPRECATED()

        Handle<Object> holder = Utils::OpenHandle(*callback_info->Holder());
        Handle<Object> maybe_name =
            PropertyCallbackArguments::GetPropertyKeyHandle(*callback_info);
        Handle<Name> name =
            IsSmi(*maybe_name)
                ? factory()->SizeToString(
                      PropertyCallbackArguments::GetPropertyIndex(
                          *callback_info))
                : Cast<Name>(maybe_name);
        DCHECK(IsJSReceiver(*holder));

        // Allow usages of v8::PropertyCallbackInfo<T>::Holder() for now.
        // TODO(https://crbug.com/333672197): remove.
        END_ALLOW_USE_DEPRECATED()

        // Currently we call only ApiGetters from JS code.
        ReportExceptionPropertyCallback(Cast<JSReceiver>(holder), name, kind);
        return;
      }

      case v8::ExceptionContext::kUnknown:
        DCHECK_WITH_MSG(kind != v8::ExceptionContext::kUnknown,
                        "ExternalCallbackScope should not use "
                        "v8::ExceptionContext::kUnknown exception context");
        return;
    }
    UNREACHABLE();
  }

  // There were no crossings of "C++ -> Api callback" bondary or they
  // happened before crossing the "C++ -> JS" boundary.
  // In this case all the data about Api callback is available in the
  // topmost "JS -> Api callback" frame (ApiCallbackExitFrame or
  // ApiAccessorExitFrame).
  DCHECK(!it.done());
  StackFrame::Type frame_type = it.frame()->type();
  switch (frame_type) {
    case StackFrame::API_CALLBACK_EXIT: {
      ApiCallbackExitFrame* frame = ApiCallbackExitFrame::cast(it.frame());
      DirectHandle<JSReceiver> receiver(Cast<JSReceiver>(frame->receiver()),
                                        this);
      DirectHandle<FunctionTemplateInfo> function_template_info =
          frame->GetFunctionTemplateInfo();

      v8::ExceptionContext callback_kind =
          frame->IsConstructor() ? v8::ExceptionContext::kConstructor
                                 : v8::ExceptionContext::kOperation;
      ReportExceptionFunctionCallback(receiver, function_template_info,
                                      callback_kind);
      return;
    }
    case StackFrame::API_ACCESSOR_EXIT: {
      ApiAccessorExitFrame* frame = ApiAccessorExitFrame::cast(it.frame());

      Handle<Object> holder(frame->holder(), this);
      Handle<Name> name(frame->property_name(), this);
      DCHECK(IsJSReceiver(*holder));

      // Currently we call only ApiGetters from JS code.
      ReportExceptionPropertyCallback(Cast<JSReceiver>(holder), name,
                                      v8::ExceptionContext::kAttributeGet);
      return;
    }
    case StackFrame::TURBOFAN:
      // This must be a fast Api call.
      CHECK(it.frame()->InFastCCall());
      // TODO(ishell): support fast Api calls.
      return;
    case StackFrame::EXIT:
    case StackFrame::BUILTIN_EXIT:
      // This is a regular runtime function or C++ builtin.
      return;
#if V8_ENABLE_WEBASSEMBLY
    case StackFrame::WASM:
      // No more info.
      return;
#endif  // V8_ENABLE_WEBASSEMBLY
    default:
      // Other types are not expected, so just hard-crash.
      CHECK_NE(frame_type, frame_type);
  }
}

void Isolate::ReportExceptionFunctionCallback(
    DirectHandle<JSReceiver> receiver,
    DirectHandle<FunctionTemplateInfo> function,
    v8::ExceptionContext exception_context) {
  DCHECK(exception_context == v8::ExceptionContext::kConstructor ||
         exception_context == v8::ExceptionContext::kOperation);
  DCHECK_NOT_NULL(exception_propagation_callback_);

  // Ignore exceptions that we can't extend.
  if (!IsJSReceiver(this->exception())) return;
  Handle<JSReceiver> exception(Cast<JSReceiver>(this->exception()), this);

  DirectHandle<Object> maybe_message(pending_message(), this);

  Handle<String> property_name =
      IsUndefined(function->class_name(), this)
          ? factory()->empty_string()
          : Handle<String>(Cast<String>(function->class_name()), this);
  Handle<String> interface_name =
      IsUndefined(function->interface_name(), this)
          ? factory()->empty_string()
          : Handle<String>(Cast<String>(function->interface_name()), this);
  if (exception_context != ExceptionContext::kConstructor) {
    exception_context =
        static_cast<ExceptionContext>(function->exception_context());
  }

  {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(this);
    // Ignore any exceptions thrown inside the callback and rethrow the
    // original exception/message.
    TryCatch try_catch(v8_isolate);

    exception_propagation_callback_(v8::ExceptionPropagationMessage(
        v8_isolate, v8::Utils::ToLocal(exception),
        v8::Utils::ToLocal(interface_name), v8::Utils::ToLocal(property_name),
        exception_context));

    try_catch.Reset();
  }
  ReThrow(*exception, *maybe_message);
}

void Isolate::ReportExceptionPropertyCallback(
    Handle<JSReceiver> holder, Handle<Name> name,
    v8::ExceptionContext exception_context) {
  DCHECK_NOT_NULL(exception_propagation_callback_);

  if (!IsJSReceiver(this->exception())) return;
  Handle<JSReceiver> exception(Cast<JSReceiver>(this->exception()), this);

  DirectHandle<Object> maybe_message(pending_message(), this);

  Handle<String> property_name;
  std::ignore = Name::ToFunctionName(this, name).ToHandle(&property_name);
  Handle<String> interface_name = JSReceiver::GetConstructorName(this, holder);

  {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(this);
    // Ignore any exceptions thrown inside the callback and rethrow the
    // original exception/message.
    TryCatch try_catch(v8_isolate);

    exception_propagation_callback_(v8::ExceptionPropagationMessage(
        v8_isolate, v8::Utils::ToLocal(exception),
        v8::Utils::ToLocal(interface_name), v8::Utils::ToLocal(property_name),
        exception_context));

    try_catch.Reset();
  }
  ReThrow(*exception, *maybe_message);
}

void Isolate::SetExceptionPropagationCallback(
    ExceptionPropagationCallback callback) {
  exception_propagation_callback_ = callback;
}

bool Isolate::InitializeCounters() {
  if (async_counters_) return false;
  async_counters_ = std::make_shared<Counters>(this);
  return true;
}

void Isolate::InitializeLoggingAndCounters() {
  if (v8_file_logger_ == nullptr) {
    v8_file_logger_ = new V8FileLogger(this);
  }
  InitializeCounters();
}

namespace {

void FinalizeBuiltinCodeObjects(Isolate* isolate) {
  DCHECK_NOT_NULL(isolate->embedded_blob_code());
  DCHECK_NE(0, isolate->embedded_blob_code_size());
  DCHECK_NOT_NULL(isolate->embedded_blob_data());
  DCHECK_NE(0, isolate->embedded_blob_data_size());

  EmbeddedData d = EmbeddedData::FromBlob(isolate);
  HandleScope scope(isolate);
  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    DirectHandle<Code> old_code = isolate->builtins()->code_handle(builtin);
    // Note that `old_code.instruction_start` might point to `old_code`'s
    // InstructionStream which might be GCed once we replace the old code
    // with the new code.
    Address instruction_start = d.InstructionStartOf(builtin);
    DirectHandle<Code> new_code =
        isolate->factory()->NewCodeObjectForEmbeddedBuiltin(old_code,
                                                            instruction_start);

    // From this point onwards, the old builtin code object is unreachable and
    // will be collected by the next GC.
    isolate->builtins()->set_code(builtin, *new_code);
  }
}

#ifdef DEBUG
bool IsolateIsCompatibleWithEmbeddedBlob(Isolate* isolate) {
  EmbeddedData d = EmbeddedData::FromBlob(isolate);
  return (d.IsolateHash() == isolate->HashIsolateForEmbeddedBlob());
}
#endif  // DEBUG

}  // namespace

void Isolate::InitializeDefaultEmbeddedBlob() {
  const uint8_t* code = DefaultEmbeddedBlobCode();
  uint32_t code_size = DefaultEmbeddedBlobCodeSize();
  const uint8_t* data = DefaultEmbeddedBlobData();
  uint32_t data_size = DefaultEmbeddedBlobDataSize();

  if (StickyEmbeddedBlobCode() != nullptr) {
    base::MutexGuard guard(current_embedded_blob_refcount_mutex_.Pointer());
    // Check again now that we hold the lock.
    if (StickyEmbeddedBlobCode() != nullptr) {
      code = StickyEmbeddedBlobCode();
      code_size = StickyEmbeddedBlobCodeSize();
      data = StickyEmbeddedBlobData();
      data_size = StickyEmbeddedBlobDataSize();
      current_embedded_blob_refs_++;
    }
  }

  if (code_size == 0) {
    CHECK_EQ(0, data_size);
  } else {
    SetEmbeddedBlob(code, code_size, data, data_size);
  }
}

void Isolate::CreateAndSetEmbeddedBlob() {
  base::MutexGuard guard(current_embedded_blob_refcount_mutex_.Pointer());

  PrepareBuiltinSourcePositionMap();
  PrepareBuiltinLabelInfoMap();

  // If a sticky blob has been set, we reuse it.
  if (StickyEmbeddedBlobCode() != nullptr) {
    CHECK_EQ(embedded_blob_code(), StickyEmbeddedBlobCode());
    CHECK_EQ(embedded_blob_data(), StickyEmbeddedBlobData());
    CHECK_EQ(CurrentEmbeddedBlobCode(), StickyEmbeddedBlobCode());
    CHECK_EQ(CurrentEmbeddedBlobData(), StickyEmbeddedBlobData());
  } else {
    // Create and set a new embedded blob.
    uint8_t* code;
    uint32_t code_size;
    uint8_t* data;
    uint32_t data_size;
    OffHeapInstructionStream::CreateOffHeapOffHeapInstructionStream(
        this, &code, &code_size, &data, &data_size);

    CHECK_EQ(0, current_embedded_blob_refs_);
    const uint8_t* const_code = const_cast<const uint8_t*>(code);
    const uint8_t* const_data = const_cast<const uint8_t*>(data);
    SetEmbeddedBlob(const_code, code_size, const_data, data_size);
    current_embedded_blob_refs_++;

    SetStickyEmbeddedBlob(code, code_size, data, data_size);
  }

  MaybeRemapEmbeddedBuiltinsIntoCodeRange();
  FinalizeBuiltinCodeObjects(this);
}

void Isolate::InitializeIsShortBuiltinCallsEnabled() {
  if (V8_SHORT_BUILTIN_CALLS_BOOL && v8_flags.short_builtin_calls) {
#if defined(V8_OS_ANDROID)
    // On Android, the check is not operative to detect memory, and re-embedded
    // builtins don't have a memory cost.
    is_short_builtin_calls_enabled_ = true;
#else
    // Check if the system has more than 4GB of physical memory by comparing the
    // old space size with respective threshold value.
    is_short_builtin_calls_enabled_ = (heap_.MaxOldGenerationSize() >=
                                       kShortBuiltinCallsOldSpaceSizeThreshold);
#endif  // defined(V8_OS_ANDROID)
    // Additionally, enable if there is already a process-wide CodeRange that
    // has re-embedded builtins.
    if (COMPRESS_POINTERS_BOOL) {
      CodeRange* code_range = isolate_group()->GetCodeRange();
      if (code_range && code_range->embedded_blob_code_copy() != nullptr) {
        is_short_builtin_calls_enabled_ = true;
      }
    }
    if (V8_ENABLE_NEAR_CODE_RANGE_BOOL) {
      // The short builtin calls could still be enabled if allocated code range
      // is close enough to embedded builtins so that the latter could be
      // reached using pc-relative (short) calls/jumps.
      is_short_builtin_calls_enabled_ |=
          GetShortBuiltinsCallRegion().contains(heap_.code_region());
    }
  }
}

void Isolate::MaybeRemapEmbeddedBuiltinsIntoCodeRange() {
  if (!is_short_builtin_calls_enabled() || !RequiresCodeRange()) {
    return;
  }
  if (V8_ENABLE_NEAR_CODE_RANGE_BOOL &&
      GetShortBuiltinsCallRegion().contains(heap_.code_region())) {
    // The embedded builtins are within the pc-relative reach from the code
    // range, so there's no need to remap embedded builtins.
    return;
  }

  CHECK_NOT_NULL(embedded_blob_code_);
  CHECK_NE(embedded_blob_code_size_, 0);

  DCHECK_NOT_NULL(heap_.code_range_);
  embedded_blob_code_ = heap_.code_range_->RemapEmbeddedBuiltins(
      this, embedded_blob_code_, embedded_blob_code_size_);
  CHECK_NOT_NULL(embedded_blob_code_);
  // The un-embedded code blob is already a part of the registered code range
  // so it's not necessary to register it again.
}

void Isolate::TearDownEmbeddedBlob() {
  // Nothing to do in case the blob is embedded into the binary or unset.
  if (StickyEmbeddedBlobCode() == nullptr) return;

  if (!is_short_builtin_calls_enabled()) {
    CHECK_EQ(embedded_blob_code(), StickyEmbeddedBlobCode());
    CHECK_EQ(embedded_blob_data(), StickyEmbeddedBlobData());
  }
  CHECK_EQ(CurrentEmbeddedBlobCode(), StickyEmbeddedBlobCode());
  CHECK_EQ(CurrentEmbeddedBlobData(), StickyEmbeddedBlobData());

  base::MutexGuard guard(current_embedded_blob_refcount_mutex_.Pointer());
  current_embedded_blob_refs_--;
  if (current_embedded_blob_refs_ == 0 && enable_embedded_blob_refcounting_) {
    // We own the embedded blob and are the last holder. Free it.
    OffHeapInstructionStream::FreeOffHeapOffHeapInstructionStream(
        const_cast<uint8_t*>(CurrentEmbeddedBlobCode()),
        embedded_blob_code_size(),
        const_cast<uint8_t*>(CurrentEmbeddedBlobData()),
        embedded_blob_data_size());
    ClearEmbeddedBlob();
  }
}

bool Isolate::InitWithoutSnapshot() {
  return Init(nullptr, nullptr, nullptr, false);
}

bool Isolate::InitWithSnapshot(SnapshotData* startup_snapshot_data,
                               SnapshotData* read_only_snapshot_data,
                               SnapshotData* shared_heap_snapshot_data,
                               bool can_rehash) {
  DCHECK_NOT_NULL(startup_snapshot_data);
  DCHECK_NOT_NULL(read_only_snapshot_data);
  DCHECK_NOT_NULL(shared_heap_snapshot_data);
  return Init(startup_snapshot_data, read_only_snapshot_data,
              shared_heap_snapshot_data, can_rehash);
}

namespace {
static std::string ToHexString(uintptr_t address) {
  std::stringstream stream_address;
  stream_address << "0x" << std::hex << address;
  return stream_address.str();
}
}  // namespace

void Isolate::AddCrashKeysForIsolateAndHeapPointers() {
  DCHECK_NOT_NULL(add_crash_key_callback_);

  const uintptr_t isolate_address = reinterpret_cast<uintptr_t>(this);
  add_crash_key_callback_(v8::CrashKeyId::kIsolateAddress,
                          ToHexString(isolate_address));

  const uintptr_t ro_space_firstpage_address =
      heap()->read_only_space()->FirstPageAddress();
  add_crash_key_callback_(v8::CrashKeyId::kReadonlySpaceFirstPageAddress,
                          ToHexString(ro_space_firstpage_address));

  const uintptr_t old_space_firstpage_address =
      heap()->old_space()->FirstPageAddress();
  add_crash_key_callback_(v8::CrashKeyId::kOldSpaceFirstPageAddress,
                          ToHexString(old_space_firstpage_address));

  if (heap()->code_range_base()) {
    const uintptr_t code_range_base_address = heap()->code_range_base();
    add_crash_key_callback_(v8::CrashKeyId::kCodeRangeBaseAddress,
                            ToHexString(code_range_base_address));
  }

  if (heap()->code_space()->first_page()) {
    const uintptr_t code_space_firstpage_address =
        heap()->code_space()->FirstPageAddress();
    add_crash_key_callback_(v8::CrashKeyId::kCodeSpaceFirstPageAddress,
                            ToHexString(code_space_firstpage_address));
  }
  const v8::StartupData* data = Snapshot::DefaultSnapshotBlob();
  // TODO(cbruni): Implement strategy to infrequently collect this.
  const uint32_t v8_snapshot_checksum_calculated = 0;
  add_crash_key_callback_(v8::CrashKeyId::kSnapshotChecksumCalculated,
                          ToHexString(v8_snapshot_checksum_calculated));
  const uint32_t v8_snapshot_checksum_expected =
      Snapshot::GetExpectedChecksum(data);
  add_crash_key_callback_(v8::CrashKeyId::kSnapshotChecksumExpected,
                          ToHexString(v8_snapshot_checksum_expected));
}

void Isolate::InitializeCodeRanges() {
  DCHECK_NULL(GetCodePages());
  MemoryRange embedded_range{
      reinterpret_cast<const void*>(embedded_blob_code()),
      embedded_blob_code_size()};
  code_pages_buffer1_.push_back(embedded_range);
  SetCodePages(&code_pages_buffer1_);
}

namespace {

// This global counter contains number of stack loads/stores per optimized/wasm
// function.
using MapOfLoadsAndStoresPerFunction =
    std::map<std::string /* function_name */,
             std::pair<uint64_t /* loads */, uint64_t /* stores */>>;
MapOfLoadsAndStoresPerFunction* stack_access_count_map = nullptr;

class BigIntPlatform : public bigint::Platform {
 public:
  explicit BigIntPlatform(Isolate* isolate) : isolate_(isolate) {}
  ~BigIntPlatform() override = default;

  bool InterruptRequested() override {
    StackLimitCheck interrupt_check(isolate_);
    return (interrupt_check.InterruptRequested() &&
            isolate_->stack_guard()->HasTerminationRequest());
  }

 private:
  Isolate* isolate_;
};
}  // namespace

#ifdef V8_COMPRESS_POINTERS
VirtualMemoryCage* Isolate::GetPtrComprCodeCageForTesting() {
  return V8_EXTERNAL_CODE_SPACE_BOOL ? heap_.code_range()
                                     : isolate_group_->GetPtrComprCage();
}
#endif  // V8_COMPRESS_POINTERS

void Isolate::VerifyStaticRoots() {
#if V8_STATIC_ROOTS_BOOL
  static_assert(ReadOnlyHeap::IsReadOnlySpaceShared(),
                "Static read only roots are only supported when there is one "
                "shared read only space per cage");
#define STATIC_ROOTS_FAILED_MSG                                            \
  "Read-only heap layout changed. Run `tools/dev/gen-static-roots.py` to " \
  "update static-roots.h."
  static_assert(static_cast<int>(RootIndex::kReadOnlyRootsCount) ==
                    StaticReadOnlyRootsPointerTable.size(),
                STATIC_ROOTS_FAILED_MSG);
  auto& roots = roots_table();
  RootIndex idx = RootIndex::kFirstReadOnlyRoot;
  for (Tagged_t cmp_ptr : StaticReadOnlyRootsPointerTable) {
    Address the_root = roots[idx];
    Address ptr =
        V8HeapCompressionScheme::DecompressTagged(cage_base(), cmp_ptr);
    CHECK_WITH_MSG(the_root == ptr, STATIC_ROOTS_FAILED_MSG);
    ++idx;
  }

  idx = RootIndex::kFirstReadOnlyRoot;
#define CHECK_NAME(_1, _2, CamelName)                                     \
  CHECK_WITH_MSG(StaticReadOnlyRoot::k##CamelName ==                      \
                     V8HeapCompressionScheme::CompressObject(roots[idx]), \
                 STATIC_ROOTS_FAILED_MSG);                                \
  ++idx;
  STRONG_READ_ONLY_ROOT_LIST(CHECK_NAME)
#undef CHECK_NAME

  // Check if instance types to map range mappings are still valid.
  //
  // Is##type(map) may be computed by checking if the map pointer lies in a
  // statically known range of addresses, whereas Is##type(instance_type) is the
  // definitive source of truth. If they disagree it means that a particular
  // entry in InstanceTypeChecker::kUniqueMapRangeOfInstanceTypeRangeList is out
  // of date. This can also happen if an instance type is starting to be used by
  // more maps.
  //
  // If this check fails either re-arrange allocations in the read-only heap
  // such that the static map range is restored (consult static-roots.h for a
  // sorted list of addresses) or remove the offending entry from the list.
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = roots_table().slot(idx).load(this);
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Cast<Map>(obj);

#define INSTANCE_TYPE_CHECKER_SINGLE(type, _)  \
  CHECK_EQ(InstanceTypeChecker::Is##type(map), \
           InstanceTypeChecker::Is##type(map->instance_type()));
    INSTANCE_TYPE_CHECKERS_SINGLE(INSTANCE_TYPE_CHECKER_SINGLE)
#undef INSTANCE_TYPE_CHECKER_SINGLE

#define INSTANCE_TYPE_CHECKER_RANGE(type, _1, _2) \
  CHECK_EQ(InstanceTypeChecker::Is##type(map),    \
           InstanceTypeChecker::Is##type(map->instance_type()));
    INSTANCE_TYPE_CHECKERS_RANGE(INSTANCE_TYPE_CHECKER_RANGE)
#undef INSTANCE_TYPE_CHECKER_RANGE

    // This limit is used in various places as a fast IsJSReceiver check.
    CHECK_IMPLIES(
        InstanceTypeChecker::IsPrimitiveHeapObject(map->instance_type()),
        V8HeapCompressionScheme::CompressObject(map.ptr()) <
            InstanceTypeChecker::kNonJsReceiverMapLimit);
    CHECK_IMPLIES(InstanceTypeChecker::IsJSReceiver(map->instance_type()),
                  V8HeapCompressionScheme::CompressObject(map.ptr()) >=
                      InstanceTypeChecker::kNonJsReceiverMapLimit);
    CHECK(InstanceTypeChecker::kNonJsReceiverMapLimit <
          read_only_heap()->read_only_space()->Size());

    if (InstanceTypeChecker::IsString(map->instance_type())) {
      CHECK_EQ(InstanceTypeChecker::IsString(map),
               InstanceTypeChecker::IsString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsSeqString(map),
               InstanceTypeChecker::IsSeqString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsExternalString(map),
               InstanceTypeChecker::IsExternalString(map->instance_type()));
      CHECK_EQ(
          InstanceTypeChecker::IsUncachedExternalString(map),
          InstanceTypeChecker::IsUncachedExternalString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsInternalizedString(map),
               InstanceTypeChecker::IsInternalizedString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsConsString(map),
               InstanceTypeChecker::IsConsString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsSlicedString(map),
               InstanceTypeChecker::IsSlicedString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsThinString(map),
               InstanceTypeChecker::IsThinString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsOneByteString(map),
               InstanceTypeChecker::IsOneByteString(map->instance_type()));
      CHECK_EQ(InstanceTypeChecker::IsTwoByteString(map),
               InstanceTypeChecker::IsTwoByteString(map->instance_type()));
    }
  }

  // Sanity check the API
  CHECK_EQ(
      v8::internal::Internals::GetRoot(reinterpret_cast<v8::Isolate*>(this),
                                       static_cast<int>(RootIndex::kNullValue)),
      ReadOnlyRoots(this).null_value().ptr());
#undef STATIC_ROOTS_FAILED_MSG
#endif  // V8_STATIC_ROOTS_BOOL
}

bool Isolate::Init(SnapshotData* startup_snapshot_data,
                   SnapshotData* read_only_snapshot_data,
                   SnapshotData* shared_heap_snapshot_data, bool can_rehash) {
  TRACE_ISOLATE(init);

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  CHECK_EQ(V8HeapCompressionScheme::base(), cage_base());
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

  const bool create_heap_objects = (shared_heap_snapshot_data == nullptr);
  // We either have both or none.
  DCHECK_EQ(create_heap_objects, startup_snapshot_data == nullptr);
  DCHECK_EQ(create_heap_objects, read_only_snapshot_data == nullptr);

  EnableRoAllocationForSnapshotScope enable_ro_allocation(this);

  base::ElapsedTimer timer;
  if (create_heap_objects && v8_flags.profile_deserialization) timer.Start();

  time_millis_at_init_ = heap_.MonotonicallyIncreasingTimeInMs();

  Isolate* use_shared_space_isolate = nullptr;

  if (HasFlagThatRequiresSharedHeap()) {
    if (process_wide_shared_space_isolate_) {
      owns_shareable_data_ = false;
      use_shared_space_isolate = process_wide_shared_space_isolate_;
    } else {
      process_wide_shared_space_isolate_ = this;
      use_shared_space_isolate = this;
      is_shared_space_isolate_ = true;
      DCHECK(owns_shareable_data_);
    }
  }

  CHECK_IMPLIES(is_shared_space_isolate_, V8_CAN_CREATE_SHARED_HEAP_BOOL);

  stress_deopt_count_ = v8_flags.deopt_every_n_times;
  force_slow_path_ = v8_flags.force_slow_path;

  has_fatal_error_ = false;

  // The initialization process does not handle memory exhaustion.
  AlwaysAllocateScope always_allocate(heap());

#define ASSIGN_ELEMENT(CamelName, hacker_name)                  \
  isolate_addresses_[IsolateAddressId::k##CamelName##Address] = \
      reinterpret_cast<Address>(hacker_name##_address());
  FOR_EACH_ISOLATE_ADDRESS_NAME(ASSIGN_ELEMENT)
#undef ASSIGN_ELEMENT

  // We need to initialize code_pages_ before any on-heap code is allocated to
  // make sure we record all code allocations.
  InitializeCodeRanges();

  compilation_cache_ = new CompilationCache(this);
  descriptor_lookup_cache_ = new DescriptorLookupCache();
  global_handles_ = new GlobalHandles(this);
  eternal_handles_ = new EternalHandles();
  bootstrapper_ = new Bootstrapper(this);
  handle_scope_implementer_ = new HandleScopeImplementer(this);
  load_stub_cache_ = new StubCache(this);
  store_stub_cache_ = new StubCache(this);
  define_own_stub_cache_ = new StubCache(this);
  materialized_object_store_ = new MaterializedObjectStore(this);
  regexp_stack_ = new RegExpStack();
  date_cache_ = new DateCache();
  heap_profiler_ = new HeapProfiler(heap());
  interpreter_ = new interpreter::Interpreter(this);
  bigint_processor_ = bigint::Processor::New(new BigIntPlatform(this));

  if (is_shared_space_isolate_) {
    global_safepoint_ = std::make_unique<GlobalSafepoint>(this);
  }

  if (v8_flags.lazy_compile_dispatcher) {
    lazy_compile_dispatcher_ = std::make_unique<LazyCompileDispatcher>(
        this, V8::GetCurrentPlatform(), v8_flags.stack_size);
  }
#ifdef V8_ENABLE_SPARKPLUG
  baseline_batch_compiler_ = new baseline::BaselineBatchCompiler(this);
#endif  // V8_ENABLE_SPARKPLUG
#ifdef V8_ENABLE_MAGLEV
  maglev_concurrent_dispatcher_ = new maglev::MaglevConcurrentDispatcher(this);
#endif  // V8_ENABLE_MAGLEV

#if USE_SIMULATOR
  simulator_data_ = new SimulatorData;
#endif

  // Enable logging before setting up the heap
  v8_file_logger_->SetUp(this);

  metrics_recorder_ = std::make_shared<metrics::Recorder>();

  {
    // Ensure that the thread has a valid stack guard.  The v8::Locker object
    // will ensure this too, but we don't have to use lockers if we are only
    // using one thread.
    ExecutionAccess lock(this);
    stack_guard()->InitThread(lock);
  }

  // Create LocalIsolate/LocalHeap for the main thread and set state to Running.
  main_thread_local_isolate_.reset(new LocalIsolate(this, ThreadKind::kMain));

  {
    IgnoreLocalGCRequests ignore_gc_requests(heap());
    main_thread_local_heap()->Unpark();
  }

  // Requires a LocalHeap to be set up to register a GC epilogue callback.
  inner_pointer_to_code_cache_ = new InnerPointerToCodeCache(this);

#if V8_ENABLE_WEBASSEMBLY
  wasm_code_look_up_cache_ = new wasm::WasmCodeLookupCache;
#endif  // V8_ENABLE_WEBASSEMBLY

  // Lock clients_mutex_ in order to prevent shared GCs from other clients
  // during deserialization.
  std::optional<base::RecursiveMutexGuard> clients_guard;

  if (use_shared_space_isolate && !is_shared_space_isolate()) {
    clients_guard.emplace(
        &use_shared_space_isolate->global_safepoint()->clients_mutex_);
    use_shared_space_isolate->global_safepoint()->AppendClient(this);
  }

  shared_space_isolate_ = use_shared_space_isolate;

  isolate_data_.is_shared_space_isolate_flag_ = is_shared_space_isolate();
  isolate_data_.uses_shared_heap_flag_ = has_shared_space();

  if (use_shared_space_isolate && !is_shared_space_isolate() &&
      use_shared_space_isolate->heap()
          ->incremental_marking()
          ->IsMajorMarking()) {
    heap_.SetIsMarkingFlag(true);
  }

  // Set up the object heap.
  DCHECK(!heap_.HasBeenSetUp());
  heap_.SetUp(main_thread_local_heap());
  InitializeIsShortBuiltinCallsEnabled();
  if (!create_heap_objects) {
    // Must be done before deserializing RO space, since RO space may contain
    // builtin Code objects which point into the (potentially remapped)
    // embedded blob.
    MaybeRemapEmbeddedBuiltinsIntoCodeRange();
  }
  {
    // Must be done before deserializing RO space since the deserialization
    // process refers to these data structures.
    isolate_data_.external_reference_table()->InitIsolateIndependent(
        isolate_group()->external_ref_table());
#ifdef V8_COMPRESS_POINTERS
    external_pointer_table().Initialize();
    external_pointer_table().InitializeSpace(
        heap()->read_only_external_pointer_space());
    external_pointer_table().AttachSpaceToReadOnlySegment(
        heap()->read_only_external_pointer_space());
    external_pointer_table().InitializeSpace(
        heap()->young_external_pointer_space());
    external_pointer_table().InitializeSpace(
        heap()->old_external_pointer_space());
    cpp_heap_pointer_table().Initialize();
    cpp_heap_pointer_table().InitializeSpace(heap()->cpp_heap_pointer_space());
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
    trusted_pointer_table().Initialize();
    trusted_pointer_table().InitializeSpace(heap()->trusted_pointer_space());
#endif  // V8_ENABLE_SANDBOX
  }
  ReadOnlyHeap::SetUp(this, read_only_snapshot_data, can_rehash);
  heap_.SetUpSpaces(isolate_data_.new_allocation_info_,
                    isolate_data_.old_allocation_info_);

  DCHECK_EQ(this, Isolate::Current());
  PerIsolateThreadData* const current_data = CurrentPerIsolateThreadData();
  DCHECK_EQ(current_data->isolate(), this);
  SetIsolateThreadLocals(this, current_data);

  if (OwnsStringTables()) {
    string_table_ = std::make_unique<StringTable>(this);
    string_forwarding_table_ = std::make_unique<StringForwardingTable>(this);
  } else {
    // Only refer to shared string table after attaching to the shared isolate.
    DCHECK(has_shared_space());
    DCHECK(!is_shared_space_isolate());
    DCHECK_NOT_NULL(string_table());
    DCHECK_NOT_NULL(string_forwarding_table());
  }

#ifdef V8_EXTERNAL_CODE_SPACE
  {
    VirtualMemoryCage* code_cage;
    if (heap_.code_range()) {
      code_cage = heap_.code_range();
    } else {
      CHECK(jitless_);
      // In jitless mode the code space pages will be allocated in the main
      // pointer compression cage.
      code_cage = isolate_group_->GetPtrComprCage();
    }
    code_cage_base_ = ExternalCodeCompressionScheme::PrepareCageBaseAddress(
        code_cage->base());
    if (COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL) {
      // .. now that it's available, initialize the thread-local base.
      ExternalCodeCompressionScheme::InitBase(code_cage_base_);
    }
    CHECK_EQ(ExternalCodeCompressionScheme::base(), code_cage_base_);

    // Ensure that ExternalCodeCompressionScheme is applicable to all objects
    // stored in the code cage.
    using ComprScheme = ExternalCodeCompressionScheme;
    Address base = code_cage->base();
    Address last = base + code_cage->size() - 1;
    PtrComprCageBase code_cage_base{code_cage_base_};
    CHECK_EQ(base, ComprScheme::DecompressTagged(
                       code_cage_base, ComprScheme::CompressObject(base)));
    CHECK_EQ(last, ComprScheme::DecompressTagged(
                       code_cage_base, ComprScheme::CompressObject(last)));
  }
#endif  // V8_EXTERNAL_CODE_SPACE

  isolate_data_.external_reference_table()->Init(this);

#ifdef V8_COMPRESS_POINTERS
  if (owns_shareable_data()) {
    isolate_data_.shared_external_pointer_table_ = new ExternalPointerTable();
    shared_external_pointer_space_ = new ExternalPointerTable::Space();
    shared_external_pointer_table().Initialize();
    shared_external_pointer_table().InitializeSpace(
        shared_external_pointer_space());
  } else {
    DCHECK(has_shared_space());
    isolate_data_.shared_external_pointer_table_ =
        shared_space_isolate()->isolate_data_.shared_external_pointer_table_;
    shared_external_pointer_space_ =
        shared_space_isolate()->shared_external_pointer_space_;
  }
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  GetProcessWideCodePointerTable()->InitializeSpace(
      heap()->code_pointer_space());
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  GetProcessWideJSDispatchTable()->InitializeSpace(
      heap()->js_dispatch_table_space());
#endif  // V8_ENABLE_LEAPTIERING

#if V8_ENABLE_WEBASSEMBLY
  wasm::GetWasmEngine()->AddIsolate(this);
#endif  // V8_ENABLE_WEBASSEMBLY

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
  if (v8_flags.enable_etw_stack_walking) {
    ETWJITInterface::AddIsolate(this);
  }
#endif  // defined(V8_OS_WIN)

  if (setup_delegate_ == nullptr) {
    setup_delegate_ = new SetupIsolateDelegate;
  }

  if (!v8_flags.inline_new) heap_.DisableInlineAllocation();

  if (!setup_delegate_->SetupHeap(this, create_heap_objects)) {
    V8::FatalProcessOutOfMemory(this, "heap object creation");
  }

  if (create_heap_objects) {
    // Terminate the startup and shared heap object caches so we can iterate.
    startup_object_cache_.push_back(ReadOnlyRoots(this).undefined_value());
    shared_heap_object_cache_.push_back(ReadOnlyRoots(this).undefined_value());
  }

  InitializeThreadLocal();

  // Profiler has to be created after ThreadLocal is initialized
  // because it makes use of interrupts.
  tracing_cpu_profiler_.reset(new TracingCpuProfilerImpl(this));

  bootstrapper_->Initialize(create_heap_objects);

  if (create_heap_objects) {
    builtins_constants_table_builder_ = new BuiltinsConstantsTableBuilder(this);

    setup_delegate_->SetupBuiltins(this, true);

    builtins_constants_table_builder_->Finalize();
    delete builtins_constants_table_builder_;
    builtins_constants_table_builder_ = nullptr;

    CreateAndSetEmbeddedBlob();
  } else {
    setup_delegate_->SetupBuiltins(this, false);
  }

  // Initialize custom memcopy and memmove functions (must happen after
  // embedded blob setup).
  init_memcopy_functions();

  if (v8_flags.trace_turbo || v8_flags.trace_turbo_graph ||
      v8_flags.turbo_profiling) {
    PrintF("Concurrent recompilation has been disabled for tracing.\n");
  } else if (OptimizingCompileDispatcher::Enabled()) {
    optimizing_compile_dispatcher_ = new OptimizingCompileDispatcher(this);
  }

  // Initialize before deserialization since collections may occur,
  // clearing/updating ICs (and thus affecting tiering decisions).
  tiering_manager_ = new TieringManager(this);

  if (!create_heap_objects) {
    // If we are deserializing, read the state into the now-empty heap.
    SharedHeapDeserializer shared_heap_deserializer(
        this, shared_heap_snapshot_data, can_rehash);
    shared_heap_deserializer.DeserializeIntoIsolate();

    StartupDeserializer startup_deserializer(this, startup_snapshot_data,
                                             can_rehash);
    startup_deserializer.DeserializeIntoIsolate();
    InitializeBuiltinJSDispatchTable();
  }
  if (DEBUG_BOOL) VerifyStaticRoots();
  load_stub_cache_->Initialize();
  store_stub_cache_->Initialize();
  define_own_stub_cache_->Initialize();
  interpreter_->Initialize();
  heap_.NotifyDeserializationComplete();

  delete setup_delegate_;
  setup_delegate_ = nullptr;

  Builtins::InitializeIsolateDataTables(this);

  // Extra steps in the logger after the heap has been set up.
  v8_file_logger_->LateSetup(this);

#ifdef DEBUG
  // Verify that the current heap state (usually deserialized from the snapshot)
  // is compatible with the embedded blob. If this DCHECK fails, we've likely
  // loaded a snapshot generated by a different V8 version or build-time
  // configuration.
  if (!IsolateIsCompatibleWithEmbeddedBlob(this)) {
    FATAL(
        "The Isolate is incompatible with the embedded blob. This is usually "
        "caused by incorrect usage of mksnapshot. When generating custom "
        "snapshots, embedders must ensure they pass the same flags as during "
        "the V8 build process (e.g.: --turbo-instruction-scheduling).");
  }
#endif  // DEBUG

  if (v8_flags.print_builtin_code) builtins()->PrintBuiltinCode();
  if (v8_flags.print_builtin_size) builtins()->PrintBuiltinSize();

  // Finish initialization of ThreadLocal after deserialization is done.
  clear_exception();
  clear_pending_message();

  // Quiet the heap NaN if needed on target platform.
  if (!create_heap_objects)
    Assembler::QuietNaN(ReadOnlyRoots(this).nan_value());

  if (v8_flags.trace_turbo) {
    // Create an empty file.
    std::ofstream(GetTurboCfgFileName(this).c_str(), std::ios_base::trunc);
  }

  isolate_data_.continuation_preserved_embedder_data_ =
      *factory()->undefined_value();

  {
    HandleScope scope(this);
    ast_string_constants_ = new AstStringConstants(this, HashSeed(this));
  }

  initialized_from_snapshot_ = !create_heap_objects;

  if (v8_flags.stress_sampling_allocation_profiler > 0) {
    uint64_t sample_interval = v8_flags.stress_sampling_allocation_profiler;
    int stack_depth = 128;
    v8::HeapProfiler::SamplingFlags sampling_flags =
        v8::HeapProfiler::SamplingFlags::kSamplingForceGC;
    heap_profiler()->StartSamplingHeapProfiler(sample_interval, stack_depth,
                                               sampling_flags);
  }

  if (create_heap_objects && v8_flags.profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Initializing isolate from scratch took %0.3f ms]\n", ms);
  }

  if (initialized_from_snapshot_) {
    SLOW_DCHECK(SharedFunctionInfo::UniqueIdsAreUnique(this));
  }

  if (v8_flags.harmony_struct) {
    // Initialize or get the struct type registry shared by all isolates.
    if (is_shared_space_isolate()) {
      shared_struct_type_registry_ =
          std::make_unique<SharedStructTypeRegistry>();
    } else {
      DCHECK_NOT_NULL(shared_struct_type_registry());
    }
  }

#ifdef V8_ENABLE_WEBASSEMBLY
#if V8_STATIC_ROOTS_BOOL
  // Protect the payload of wasm null.
  if (!page_allocator()->DecommitPages(
          reinterpret_cast<void*>(factory()->wasm_null()->payload()),
          WasmNull::kSize - kTaggedSize)) {
    V8::FatalProcessOutOfMemory(this, "decommitting WasmNull payload");
  }
#endif  // V8_STATIC_ROOTS_BOOL
#endif  // V8_ENABLE_WEBASSEMBLY

  // Isolate initialization allocates long living objects that should be
  // pretenured to old space.
  DCHECK_IMPLIES(heap()->new_space(), heap()->new_space()->Size() == 0);
  DCHECK_IMPLIES(heap()->new_lo_space(), heap()->new_lo_space()->Size() == 0);
  DCHECK_EQ(heap()->gc_count(), 0);

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
  if (v8_flags.enable_etw_stack_walking) {
    ETWJITInterface::MaybeSetHandlerNow(this);
  }
#endif  // defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)

#if defined(V8_USE_PERFETTO)
  PerfettoLogger::RegisterIsolate(this);
#endif  // defined(V8_USE_PERFETTO)

  initialized_ = true;

  return true;
}

void Isolate::Enter() {
  Isolate* current_isolate = nullptr;
  PerIsolateThreadData* current_data = CurrentPerIsolateThreadData();

#ifdef V8_ENABLE_CHECKS
  // No different thread must have entered the isolate. Allow re-entering.
  ThreadId thread_id = ThreadId::Current();
  if (current_thread_id_.IsValid()) {
    CHECK_EQ(current_thread_id_, thread_id);
  } else {
    CHECK_EQ(0, current_thread_counter_);
    current_thread_id_ = thread_id;
  }
  current_thread_counter_++;
#endif

  // Set the stack start for the main thread that enters the isolate.
  heap()->SetStackStart();

  if (current_data != nullptr) {
    current_isolate = current_data->isolate_;
    DCHECK_NOT_NULL(current_isolate);
    if (current_isolate == this) {
      DCHECK(Current() == this);
      auto entry_stack = entry_stack_.load();
      DCHECK_NOT_NULL(entry_stack);
      DCHECK(entry_stack->previous_thread_data == nullptr ||
             entry_stack->previous_thread_data->thread_id() ==
                 ThreadId::Current());
      // Same thread re-enters the isolate, no need to re-init anything.
      entry_stack->entry_count++;
      return;
    }
  }

  PerIsolateThreadData* data = FindOrAllocatePerThreadDataForThisThread();
  DCHECK_NOT_NULL(data);
  DCHECK(data->isolate_ == this);

  EntryStackItem* item =
      new EntryStackItem(current_data, current_isolate, entry_stack_);
  entry_stack_ = item;

  SetIsolateThreadLocals(this, data);

  // In case it's the first time some thread enters the isolate.
  set_thread_id(data->thread_id());
}

void Isolate::Exit() {
  auto current_entry_stack = entry_stack_.load();
  DCHECK_NOT_NULL(current_entry_stack);
  DCHECK(current_entry_stack->previous_thread_data == nullptr ||
         current_entry_stack->previous_thread_data->thread_id() ==
             ThreadId::Current());

#ifdef V8_ENABLE_CHECKS
  // The current thread must have entered the isolate.
  CHECK_EQ(current_thread_id_, ThreadId::Current());
  if (--current_thread_counter_ == 0) current_thread_id_ = ThreadId::Invalid();
#endif

  if (--current_entry_stack->entry_count > 0) return;

  DCHECK_NOT_NULL(CurrentPerIsolateThreadData());
  DCHECK(CurrentPerIsolateThreadData()->isolate_ == this);

  // Pop the stack.
  entry_stack_ = current_entry_stack->previous_item;

  PerIsolateThreadData* previous_thread_data =
      current_entry_stack->previous_thread_data;
  Isolate* previous_isolate = current_entry_stack->previous_isolate;

  delete current_entry_stack;

  // Reinit the current thread for the isolate it was running before this one.
  SetIsolateThreadLocals(previous_isolate, previous_thread_data);
}

std::unique_ptr<PersistentHandles> Isolate::NewPersistentHandles() {
  return std::make_unique<PersistentHandles>(this);
}

void Isolate::DumpAndResetStats() {
  if (v8_flags.trace_turbo_stack_accesses) {
    StdoutStream os;
    uint64_t total_loads = 0;
    uint64_t total_stores = 0;
    os << "=== Stack access counters === " << std::endl;
    if (!stack_access_count_map) {
      os << "No stack accesses in optimized/wasm functions found.";
    } else {
      DCHECK_NOT_NULL(stack_access_count_map);
      os << "Number of optimized/wasm stack-access functions: "
         << stack_access_count_map->size() << std::endl;
      for (auto it = stack_access_count_map->cbegin();
           it != stack_access_count_map->cend(); it++) {
        std::string function_name((*it).first);
        std::pair<uint64_t, uint64_t> per_func_count = (*it).second;
        os << "Name: " << function_name << ", Loads: " << per_func_count.first
           << ", Stores: " << per_func_count.second << std::endl;
        total_loads += per_func_count.first;
        total_stores += per_func_count.second;
      }
      os << "Total Loads: " << total_loads << ", Total Stores: " << total_stores
         << std::endl;
      stack_access_count_map = nullptr;
    }
  }
  if (turbo_statistics_ != nullptr) {
    DCHECK(v8_flags.turbo_stats || v8_flags.turbo_stats_nvp);
    StdoutStream os;
    if (v8_flags.turbo_stats) {
      AsPrintableStatistics ps = {"Turbofan", *turbo_statistics_, false};
      os << ps << std::endl;
    }
    if (v8_flags.turbo_stats_nvp) {
      AsPrintableStatistics ps = {"Turbofan", *turbo_statistics_, true};
      os << ps << std::endl;
    }
    turbo_statistics_.reset();
  }

#ifdef V8_ENABLE_MAGLEV
  if (maglev_statistics_ != nullptr) {
    DCHECK(v8_flags.maglev_stats || v8_flags.maglev_stats_nvp);
    StdoutStream os;
    if (v8_flags.maglev_stats) {
      AsPrintableStatistics ps = {"Maglev", *maglev_statistics_, false};
      os << ps << std::endl;
    }
    if (v8_flags.maglev_stats_nvp) {
      AsPrintableStatistics ps = {"Maglev", *maglev_statistics_, true};
      os << ps << std::endl;
    }
    maglev_statistics_.reset();
  }
#endif  // V8_ENABLE_MAGLEV

#if V8_ENABLE_WEBASSEMBLY
  // TODO(7424): There is no public API for the {WasmEngine} yet. So for now we
  // just dump and reset the engines statistics together with the Isolate.
  if (v8_flags.turbo_stats_wasm) {
    wasm::GetWasmEngine()->DumpAndResetTurboStatistics();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
#if V8_RUNTIME_CALL_STATS
  if (V8_UNLIKELY(TracingFlags::runtime_stats.load(std::memory_order_relaxed) ==
                  v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE)) {
    counters()->worker_thread_runtime_call_stats()->AddToMainTable(
        counters()->runtime_call_stats());
    counters()->runtime_call_stats()->Print();
    counters()->runtime_call_stats()->Reset();
  }
#endif  // V8_RUNTIME_CALL_STATS
}

void Isolate::DumpAndResetBuiltinsProfileData() {
  if (BasicBlockProfiler::Get()->HasData(this)) {
    if (v8_flags.turbo_profiling_output) {
      FILE* f = std::fopen(v8_flags.turbo_profiling_output, "w");
      if (f == nullptr) {
        FATAL("Unable to open file \"%s\" for writing.\n",
              v8_flags.turbo_profiling_output.value());
      }
      OFStream pgo_stream(f);
      BasicBlockProfiler::Get()->Log(this, pgo_stream);
    } else {
      StdoutStream out;
      BasicBlockProfiler::Get()->Print(this, out);
    }
    BasicBlockProfiler::Get()->ResetCounts(this);
  } else {
    // Only log builtins PGO data if v8 was built with
    // v8_enable_builtins_profiling=true
    CHECK_NULL(v8_flags.turbo_profiling_output);
  }
}

void Isolate::IncreaseConcurrentOptimizationPriority(
    CodeKind kind, Tagged<SharedFunctionInfo> function) {
  DCHECK(kind == CodeKind::TURBOFAN);
  optimizing_compile_dispatcher()->Prioritize(function);
}

void Isolate::AbortConcurrentOptimization(BlockingBehavior behavior) {
  if (concurrent_recompilation_enabled()) {
    DisallowGarbageCollection no_recursive_gc;
    optimizing_compile_dispatcher()->Flush(behavior);
  }
#ifdef V8_ENABLE_MAGLEV
  if (maglev_concurrent_dispatcher()->is_enabled()) {
    DisallowGarbageCollection no_recursive_gc;
    maglev_concurrent_dispatcher()->Flush(behavior);
  }
#endif
}

std::shared_ptr<CompilationStatistics> Isolate::GetTurboStatistics() {
  if (turbo_statistics_ == nullptr) {
    turbo_statistics_.reset(new CompilationStatistics());
  }
  return turbo_statistics_;
}

#ifdef V8_ENABLE_MAGLEV

std::shared_ptr<CompilationStatistics> Isolate::GetMaglevStatistics() {
  if (maglev_statistics_ == nullptr) {
    maglev_statistics_.reset(new CompilationStatistics());
  }
  return maglev_statistics_;
}

#endif  // V8_ENABLE_MAGLEV

CodeTracer* Isolate::GetCodeTracer() {
  if (code_tracer() == nullptr) set_code_tracer(new CodeTracer(id()));
  return code_tracer();
}

bool Isolate::use_optimizer() {
  // TODO(v8:7700): Update this predicate for a world with multiple tiers.
  return (v8_flags.turbofan || v8_flags.maglev) && !serializer_enabled_ &&
         CpuFeatures::SupportsOptimizer() && !is_precise_count_code_coverage();
}

void Isolate::IncreaseTotalRegexpCodeGenerated(DirectHandle<HeapObject> code) {
  PtrComprCageBase cage_base(this);
  DCHECK(IsCode(*code, cage_base) || IsTrustedByteArray(*code, cage_base));
  total_regexp_code_generated_ += code->Size(cage_base);
}

bool Isolate::NeedsDetailedOptimizedCodeLineInfo() const {
  return NeedsSourcePositions() || detailed_source_positions_for_profiling();
}

bool Isolate::IsLoggingCodeCreation() const {
  return v8_file_logger()->is_listening_to_code_events() || is_profiling() ||
         v8_flags.log_function_events ||
         logger()->is_listening_to_code_events();
}

bool Isolate::AllowsCodeCompaction() const {
  return v8_flags.compact_code_space && logger()->allows_code_compaction();
}

bool Isolate::NeedsSourcePositions() const {
  return
      // Static conditions.
      v8_flags.trace_deopt || v8_flags.trace_turbo ||
      v8_flags.trace_turbo_graph || v8_flags.turbo_profiling ||
      v8_flags.print_maglev_code || v8_flags.perf_prof || v8_flags.log_maps ||
      v8_flags.log_ic || v8_flags.log_function_events ||
      v8_flags.heap_snapshot_on_oom ||
      // Dynamic conditions; changing any of these conditions triggers source
      // position collection for the entire heap
      // (CollectSourcePositionsForAllBytecodeArrays).
      is_profiling() || debug_->is_active() || v8_file_logger_->is_logging();
}

void Isolate::SetFeedbackVectorsForProfilingTools(Tagged<Object> value) {
  DCHECK(IsUndefined(value, this) || IsArrayList(value));
  heap()->set_feedback_vectors_for_profiling_tools(value);
}

void Isolate::MaybeInitializeVectorListFromHeap() {
  if (!IsUndefined(heap()->feedback_vectors_for_profiling_tools(), this)) {
    // Already initialized, return early.
    DCHECK(IsArrayList(heap()->feedback_vectors_for_profiling_tools()));
    return;
  }

  // Collect existing feedback vectors.
  std::vector<Handle<FeedbackVector>> vectors;

  {
    HeapObjectIterator heap_iterator(heap());
    for (Tagged<HeapObject> current_obj = heap_iterator.Next();
         !current_obj.is_null(); current_obj = heap_iterator.Next()) {
      if (!IsFeedbackVector(current_obj)) continue;

      Tagged<FeedbackVector> vector = Cast<FeedbackVector>(current_obj);
      Tagged<SharedFunctionInfo> shared = vector->shared_function_info();

      // No need to preserve the feedback vector for non-user-visible functions.
      if (!shared->IsSubjectToDebugging()) continue;

      vectors.emplace_back(vector, this);
    }
  }

  // Add collected feedback vectors to the root list lest we lose them to GC.
  Handle<ArrayList> list =
      ArrayList::New(this, static_cast<int>(vectors.size()));
  for (const auto& vector : vectors) list = ArrayList::Add(this, list, vector);
  SetFeedbackVectorsForProfilingTools(*list);
}

void Isolate::set_date_cache(DateCache* date_cache) {
  if (date_cache != date_cache_) {
    delete date_cache_;
  }
  date_cache_ = date_cache;
}

Isolate::KnownPrototype Isolate::IsArrayOrObjectOrStringPrototype(
    Tagged<Object> object) {
  Tagged<Object> context = heap()->native_contexts_list();
  while (!IsUndefined(context, this)) {
    Tagged<Context> current_context = Cast<Context>(context);
    if (current_context->initial_object_prototype() == object) {
      return KnownPrototype::kObject;
    } else if (current_context->initial_array_prototype() == object) {
      return KnownPrototype::kArray;
    } else if (current_context->initial_string_prototype() == object) {
      return KnownPrototype::kString;
    }
    context = current_context->next_context_link();
  }
  return KnownPrototype::kNone;
}

bool Isolate::IsInAnyContext(Tagged<Object> object, uint32_t index) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> context = heap()->native_contexts_list();
  while (!IsUndefined(context, this)) {
    Tagged<Context> current_context = Cast<Context>(context);
    if (current_context->get(index) == object) {
      return true;
    }
    context = current_context->next_context_link();
  }
  return false;
}

void Isolate::UpdateNoElementsProtectorOnSetElement(
    DirectHandle<JSObject> object) {
  DisallowGarbageCollection no_gc;
  if (!object->map()->is_prototype_map()) return;
  if (!Protectors::IsNoElementsIntact(this)) return;
  KnownPrototype obj_type = IsArrayOrObjectOrStringPrototype(*object);
  if (obj_type == KnownPrototype::kNone) return;
  if (obj_type == KnownPrototype::kObject) {
    this->CountUsage(v8::Isolate::kObjectPrototypeHasElements);
  } else if (obj_type == KnownPrototype::kArray) {
    this->CountUsage(v8::Isolate::kArrayPrototypeHasElements);
  }
  Protectors::InvalidateNoElements(this);
}

void Isolate::UpdateProtectorsOnSetPrototype(
    DirectHandle<JSObject> object, DirectHandle<Object> new_prototype) {
  UpdateNoElementsProtectorOnSetPrototype(object);
  UpdateTypedArraySpeciesLookupChainProtectorOnSetPrototype(object);
  UpdateNumberStringNotRegexpLikeProtectorOnSetPrototype(object);
  UpdateStringWrapperToPrimitiveProtectorOnSetPrototype(object, new_prototype);
}

void Isolate::UpdateTypedArraySpeciesLookupChainProtectorOnSetPrototype(
    DirectHandle<JSObject> object) {
  // Setting the __proto__ of TypedArray constructor could change TypedArray's
  // @@species. So we need to invalidate the @@species protector.
  if (IsTypedArrayConstructor(*object) &&
      Protectors::IsTypedArraySpeciesLookupChainIntact(this)) {
    Protectors::InvalidateTypedArraySpeciesLookupChain(this);
  }
}

void Isolate::UpdateNumberStringNotRegexpLikeProtectorOnSetPrototype(
    DirectHandle<JSObject> object) {
  if (!Protectors::IsNumberStringNotRegexpLikeIntact(this)) {
    return;
  }
  // We need to protect the prototype chain of `Number.prototype` and
  // `String.prototype`.
  // Since `Object.prototype.__proto__` is not writable, we can assume it
  // doesn't occur here. We detect `Number.prototype` and `String.prototype` by
  // checking for a prototype that is a JSPrimitiveWrapper. This is a safe
  // approximation. Using JSPrimitiveWrapper as prototype should be
  // sufficiently rare.
  DCHECK(!IsJSObjectPrototype(*object));
  if (object->map()->is_prototype_map() && (IsJSPrimitiveWrapper(*object))) {
    Protectors::InvalidateNumberStringNotRegexpLike(this);
  }
}

void Isolate::UpdateStringWrapperToPrimitiveProtectorOnSetPrototype(
    DirectHandle<JSObject> object, DirectHandle<Object> new_prototype) {
  if (!Protectors::IsStringWrapperToPrimitiveIntact(this)) {
    return;
  }

  // We can have a custom @@toPrimitive on a string wrapper also if we subclass
  // String and the subclass (or one of its subclasses) defines its own
  // @@toPrimive. Thus we invalidate the protector whenever we detect
  // subclassing String - it should be reasonably rare.
  if (IsStringWrapper(*object) || IsStringWrapper(*new_prototype)) {
    Protectors::InvalidateStringWrapperToPrimitive(this);
  }
}

static base::RandomNumberGenerator* ensure_rng_exists(
    base::RandomNumberGenerator** rng, int seed) {
  if (*rng == nullptr) {
    if (seed != 0) {
      *rng = new base::RandomNumberGenerator(seed);
    } else {
      *rng = new base::RandomNumberGenerator();
    }
  }
  return *rng;
}

base::RandomNumberGenerator* Isolate::random_number_generator() {
  // TODO(bmeurer) Initialized lazily because it depends on flags; can
  // be fixed once the default isolate cleanup is done.
  return ensure_rng_exists(&random_number_generator_, v8_flags.random_seed);
}

base::RandomNumberGenerator* Isolate::fuzzer_rng() {
  if (fuzzer_rng_ == nullptr) {
    int64_t seed = v8_flags.fuzzer_random_seed;
    if (seed == 0) {
      seed = random_number_generator()->initial_seed();
    }

    fuzzer_rng_ = new base::RandomNumberGenerator(seed);
  }

  return fuzzer_rng_;
}

int Isolate::GenerateIdentityHash(uint32_t mask) {
  int hash;
  int attempts = 0;
  do {
    hash = random_number_generator()->NextInt() & mask;
  } while (hash == 0 && attempts++ < 30);
  return hash != 0 ? hash : 1;
}

#ifdef DEBUG
#define ISOLATE_FIELD_OFFSET(type, name, ignored) \
  const intptr_t Isolate::name##_debug_offset_ = OFFSET_OF(Isolate, name##_);
ISOLATE_INIT_LIST(ISOLATE_FIELD_OFFSET)
ISOLATE_INIT_ARRAY_LIST(ISOLATE_FIELD_OFFSET)
#undef ISOLATE_FIELD_OFFSET
#endif

Handle<Symbol> Isolate::SymbolFor(RootIndex dictionary_index,
                                  Handle<String> name, bool private_symbol) {
  Handle<String> key = factory()->InternalizeString(name);
  Handle<RegisteredSymbolTable> dictionary =
      Cast<RegisteredSymbolTable>(root_handle(dictionary_index));
  InternalIndex entry = dictionary->FindEntry(this, key);
  Handle<Symbol> symbol;
  if (entry.is_not_found()) {
    symbol =
        private_symbol ? factory()->NewPrivateSymbol() : factory()->NewSymbol();
    symbol->set_description(*key);
    dictionary = RegisteredSymbolTable::Add(this, dictionary, key, symbol);

    switch (dictionary_index) {
      case RootIndex::kPublicSymbolTable:
        symbol->set_is_in_public_symbol_table(true);
        heap()->set_public_symbol_table(*dictionary);
        break;
      case RootIndex::kApiSymbolTable:
        heap()->set_api_symbol_table(*dictionary);
        break;
      case RootIndex::kApiPrivateSymbolTable:
        heap()->set_api_private_symbol_table(*dictionary);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    symbol = Handle<Symbol>(Cast<Symbol>(dictionary->ValueAt(entry)), this);
  }
  return symbol;
}

void Isolate::AddBeforeCallEnteredCallback(BeforeCallEnteredCallback callback) {
  auto pos = std::find(before_call_entered_callbacks_.begin(),
                       before_call_entered_callbacks_.end(), callback);
  if (pos != before_call_entered_callbacks_.end()) return;
  before_call_entered_callbacks_.push_back(callback);
}

void Isolate::RemoveBeforeCallEnteredCallback(
    BeforeCallEnteredCallback callback) {
  auto pos = std::find(before_call_entered_callbacks_.begin(),
                       before_call_entered_callbacks_.end(), callback);
  if (pos == before_call_entered_callbacks_.end()) return;
  before_call_entered_callbacks_.erase(pos);
}

void Isolate::AddCallCompletedCallback(CallCompletedCallback callback) {
  auto pos = std::find(call_completed_callbacks_.begin(),
                       call_completed_callbacks_.end(), callback);
  if (pos != call_completed_callbacks_.end()) return;
  call_completed_callbacks_.push_back(callback);
}

void Isolate::RemoveCallCompletedCallback(CallCompletedCallback callback) {
  auto pos = std::find(call_completed_callbacks_.begin(),
                       call_completed_callbacks_.end(), callback);
  if (pos == call_completed_callbacks_.end()) return;
  call_completed_callbacks_.erase(pos);
}

void Isolate::FireCallCompletedCallbackInternal(
    MicrotaskQueue* microtask_queue) {
  DCHECK(thread_local_top()->CallDepthIsZero());

  bool perform_checkpoint =
      microtask_queue &&
      microtask_queue->microtasks_policy() == v8::MicrotasksPolicy::kAuto &&
      !is_execution_terminating();

  v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(this);
  if (perform_checkpoint) microtask_queue->PerformCheckpoint(isolate);

  if (call_completed_callbacks_.empty()) return;
  // Fire callbacks.  Increase call depth to prevent recursive callbacks.
  v8::Isolate::SuppressMicrotaskExecutionScope suppress(isolate);
  std::vector<CallCompletedCallback> callbacks(call_completed_callbacks_);
  for (auto& callback : callbacks) {
    callback(reinterpret_cast<v8::Isolate*>(this));
  }
}

#ifdef V8_ENABLE_WEBASSEMBLY
void Isolate::WasmInitJSPIFeature() {
  if (IsUndefined(root(RootIndex::kActiveContinuation))) {
    wasm::StackMemory* stack(wasm::StackMemory::GetCurrentStackView(this));
    this->wasm_stacks().emplace_back(stack);
    stack->set_index(0);
    if (v8_flags.trace_wasm_stack_switching) {
      PrintF("Set up native stack object (limit: %p, base: %p)\n",
             stack->jslimit(), reinterpret_cast<void*>(stack->base()));
    }
    HandleScope scope(this);
    DirectHandle<WasmContinuationObject> continuation =
        WasmContinuationObject::New(this, stack, wasm::JumpBuffer::Active,
                                    AllocationType::kOld);
    heap()
        ->roots_table()
        .slot(RootIndex::kActiveContinuation)
        .store(*continuation);
  }
}
#endif

void Isolate::UpdatePromiseHookProtector() {
  if (Protectors::IsPromiseHookIntact(this)) {
    HandleScope scope(this);
    Protectors::InvalidatePromiseHook(this);
  }
}

void Isolate::PromiseHookStateUpdated() {
  promise_hook_flags_ =
    (promise_hook_flags_ & PromiseHookFields::HasContextPromiseHook::kMask) |
    PromiseHookFields::HasIsolatePromiseHook::encode(promise_hook_) |
    PromiseHookFields::HasAsyncEventDelegate::encode(async_event_delegate_) |
    PromiseHookFields::IsDebugActive::encode(debug()->is_active());

  if (promise_hook_flags_ != 0) {
    UpdatePromiseHookProtector();
  }
}

namespace {

MaybeHandle<JSPromise> NewRejectedPromise(Isolate* isolate,
                                          v8::Local<v8::Context> api_context,
                                          Handle<Object> exception) {
  v8::Local<v8::Promise::Resolver> resolver;
  API_ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, resolver,
                                       v8::Promise::Resolver::New(api_context),
                                       MaybeHandle<JSPromise>());

  MAYBE_RETURN_ON_EXCEPTION_VALUE(
      isolate, resolver->Reject(api_context, v8::Utils::ToLocal(exception)),
      MaybeHandle<JSPromise>());

  v8::Local<v8::Promise> promise = resolver->GetPromise();
  return v8::Utils::OpenHandle(*promise);
}

}  // namespace

MaybeHandle<JSPromise> Isolate::RunHostImportModuleDynamicallyCallback(
    MaybeHandle<Script> maybe_referrer, Handle<Object> specifier,
    MaybeHandle<Object> maybe_import_options_argument) {
  DCHECK(!is_execution_terminating());
  v8::Local<v8::Context> api_context = v8::Utils::ToLocal(native_context());
  if (host_import_module_dynamically_callback_ == nullptr) {
    Handle<Object> exception =
        factory()->NewError(error_function(), MessageTemplate::kUnsupported);
    return NewRejectedPromise(this, api_context, exception);
  }

  Handle<String> specifier_str;
  MaybeHandle<String> maybe_specifier = Object::ToString(this, specifier);
  if (!maybe_specifier.ToHandle(&specifier_str)) {
    if (is_execution_terminating()) {
      return MaybeHandle<JSPromise>();
    }
    Handle<Object> exception(this->exception(), this);
    clear_exception();
    return NewRejectedPromise(this, api_context, exception);
  }
  DCHECK(!has_exception());

  v8::Local<v8::Promise> promise;
  Handle<FixedArray> import_attributes_array;
  if (!GetImportAttributesFromArgument(maybe_import_options_argument)
           .ToHandle(&import_attributes_array)) {
    if (is_execution_terminating()) {
      return MaybeHandle<JSPromise>();
    }
    Handle<Object> exception(this->exception(), this);
    clear_exception();
    return NewRejectedPromise(this, api_context, exception);
  }
  Handle<FixedArray> host_defined_options;
  Handle<Object> resource_name;
  if (maybe_referrer.is_null()) {
    host_defined_options = factory()->empty_fixed_array();
    resource_name = factory()->null_value();
  } else {
    DirectHandle<Script> referrer = maybe_referrer.ToHandleChecked();
    host_defined_options = handle(referrer->host_defined_options(), this);
    resource_name = handle(referrer->name(), this);
  }

  API_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      this, promise,
      host_import_module_dynamically_callback_(
          api_context, v8::Utils::ToLocal(host_defined_options),
          v8::Utils::ToLocal(resource_name), v8::Utils::ToLocal(specifier_str),
          ToApiHandle<v8::FixedArray>(import_attributes_array)),
      MaybeHandle<JSPromise>());
  return v8::Utils::OpenHandle(*promise);
}

MaybeHandle<FixedArray> Isolate::GetImportAttributesFromArgument(
    MaybeHandle<Object> maybe_import_options_argument) {
  Handle<FixedArray> import_attributes_array = factory()->empty_fixed_array();
  Handle<Object> import_options_argument;
  if (!maybe_import_options_argument.ToHandle(&import_options_argument) ||
      IsUndefined(*import_options_argument)) {
    return import_attributes_array;
  }

  // The parser shouldn't have allowed the second argument to import() if
  // the flag wasn't enabled.
  DCHECK(v8_flags.harmony_import_assertions ||
         v8_flags.harmony_import_attributes);

  if (!IsJSReceiver(*import_options_argument)) {
    this->Throw(
        *factory()->NewTypeError(MessageTemplate::kNonObjectImportArgument));
    return MaybeHandle<FixedArray>();
  }

  Handle<JSReceiver> import_options_argument_receiver =
      Cast<JSReceiver>(import_options_argument);

  Handle<Object> import_attributes_object;

  if (v8_flags.harmony_import_attributes) {
    Handle<Name> with_key = factory()->with_string();
    if (!JSReceiver::GetProperty(this, import_options_argument_receiver,
                                 with_key)
             .ToHandle(&import_attributes_object)) {
      // This can happen if the property has a getter function that throws
      // an error.
      return MaybeHandle<FixedArray>();
    }
  }

  if (v8_flags.harmony_import_assertions &&
      (!v8_flags.harmony_import_attributes ||
       IsUndefined(*import_attributes_object))) {
    Handle<Name> assert_key = factory()->assert_string();
    if (!JSReceiver::GetProperty(this, import_options_argument_receiver,
                                 assert_key)
             .ToHandle(&import_attributes_object)) {
      // This can happen if the property has a getter function that throws
      // an error.
      return MaybeHandle<FixedArray>();
    }

    if (V8_UNLIKELY(!IsUndefined(*import_attributes_object))) {
      MessageLocation* location = nullptr;
      MessageLocation computed_location;
      if (ComputeLocation(&computed_location)) {
        location = &computed_location;
      }
      DirectHandle<JSMessageObject> message = MessageHandler::MakeMessageObject(
          this, MessageTemplate::kImportAssertDeprecated, location,
          factory()->NewStringFromAsciiChecked("V8 v12.6 and Chrome 126"),
          Handle<FixedArray>::null());
      message->set_error_level(v8::Isolate::kMessageWarning);
      MessageHandler::ReportMessage(this, location, message);
    }
  }

  // If there is no 'with' or 'assert' option in the options bag, it's not an
  // error. Just do the import() as if no assertions were provided.
  if (IsUndefined(*import_attributes_object)) return import_attributes_array;

  if (!IsJSReceiver(*import_attributes_object)) {
    this->Throw(
        *factory()->NewTypeError(MessageTemplate::kNonObjectAssertOption));
    return MaybeHandle<FixedArray>();
  }

  Handle<JSReceiver> import_attributes_object_receiver =
      Cast<JSReceiver>(import_attributes_object);

  Handle<FixedArray> assertion_keys;
  if (!KeyAccumulator::GetKeys(this, import_attributes_object_receiver,
                               KeyCollectionMode::kOwnOnly, ENUMERABLE_STRINGS,
                               GetKeysConversion::kConvertToString)
           .ToHandle(&assertion_keys)) {
    // This happens if the assertions object is a Proxy whose ownKeys() or
    // getOwnPropertyDescriptor() trap throws.
    return MaybeHandle<FixedArray>();
  }

  bool has_non_string_attribute = false;

  // The assertions will be passed to the host in the form: [key1,
  // value1, key2, value2, ...].
  constexpr size_t kAttributeEntrySizeForDynamicImport = 2;
  import_attributes_array = factory()->NewFixedArray(static_cast<int>(
      assertion_keys->length() * kAttributeEntrySizeForDynamicImport));
  for (int i = 0; i < assertion_keys->length(); i++) {
    Handle<String> assertion_key(Cast<String>(assertion_keys->get(i)), this);
    Handle<Object> assertion_value;
    if (!Object::GetPropertyOrElement(this, import_attributes_object_receiver,
                                      assertion_key)
             .ToHandle(&assertion_value)) {
      // This can happen if the property has a getter function that throws
      // an error.
      return MaybeHandle<FixedArray>();
    }

    if (!IsString(*assertion_value)) {
      has_non_string_attribute = true;
    }

    import_attributes_array->set((i * kAttributeEntrySizeForDynamicImport),
                                 *assertion_key);
    import_attributes_array->set((i * kAttributeEntrySizeForDynamicImport) + 1,
                                 *assertion_value);
  }

  if (has_non_string_attribute) {
    this->Throw(*factory()->NewTypeError(
        MessageTemplate::kNonStringImportAssertionValue));
    return MaybeHandle<FixedArray>();
  }

  return import_attributes_array;
}

void Isolate::ClearKeptObjects() { heap()->ClearKeptObjects(); }

void Isolate::SetHostImportModuleDynamicallyCallback(
    HostImportModuleDynamicallyCallback callback) {
  host_import_module_dynamically_callback_ = callback;
}

MaybeHandle<JSObject> Isolate::RunHostInitializeImportMetaObjectCallback(
    Handle<SourceTextModule> module) {
  CHECK(IsTheHole(module->import_meta(kAcquireLoad), this));
  Handle<JSObject> import_meta = factory()->NewJSObjectWithNullProto();
  if (host_initialize_import_meta_object_callback_ != nullptr) {
    v8::Local<v8::Context> api_context = v8::Utils::ToLocal(native_context());
    host_initialize_import_meta_object_callback_(
        api_context, Utils::ToLocal(Cast<Module>(module)),
        v8::Local<v8::Object>::Cast(v8::Utils::ToLocal(import_meta)));
    if (has_exception()) return {};
  }
  return import_meta;
}

void Isolate::SetHostInitializeImportMetaObjectCallback(
    HostInitializeImportMetaObjectCallback callback) {
  host_initialize_import_meta_object_callback_ = callback;
}

void Isolate::SetHostCreateShadowRealmContextCallback(
    HostCreateShadowRealmContextCallback callback) {
  host_create_shadow_realm_context_callback_ = callback;
}

MaybeHandle<NativeContext> Isolate::RunHostCreateShadowRealmContextCallback() {
  if (host_create_shadow_realm_context_callback_ == nullptr) {
    DirectHandle<Object> exception =
        factory()->NewError(error_function(), MessageTemplate::kUnsupported);
    Throw(*exception);
    return kNullMaybeHandle;
  }

  v8::Local<v8::Context> api_context = v8::Utils::ToLocal(native_context());
  v8::Local<v8::Context> shadow_realm_context;
  API_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      this, shadow_realm_context,
      host_create_shadow_realm_context_callback_(api_context),
      MaybeHandle<NativeContext>());
  Handle<Context> shadow_realm_context_handle =
      v8::Utils::OpenHandle(*shadow_realm_context);
  DCHECK(IsNativeContext(*shadow_realm_context_handle));
  shadow_realm_context_handle->set_scope_info(
      ReadOnlyRoots(this).shadow_realm_scope_info());
  return Cast<NativeContext>(shadow_realm_context_handle);
}

MaybeHandle<Object> Isolate::RunPrepareStackTraceCallback(
    Handle<NativeContext> context, Handle<JSObject> error,
    Handle<JSArray> sites) {
  v8::Local<v8::Context> api_context = Utils::ToLocal(context);

  v8::Local<v8::Value> stack;
  API_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      this, stack,
      prepare_stack_trace_callback_(api_context, Utils::ToLocal(error),
                                    Utils::ToLocal(sites)),
      MaybeHandle<Object>());
  return Utils::OpenHandle(*stack);
}

int Isolate::LookupOrAddExternallyCompiledFilename(const char* filename) {
  if (embedded_file_writer_ != nullptr) {
    return embedded_file_writer_->LookupOrAddExternallyCompiledFilename(
        filename);
  }
  return 0;
}

const char* Isolate::GetExternallyCompiledFilename(int index) const {
  if (embedded_file_writer_ != nullptr) {
    return embedded_file_writer_->GetExternallyCompiledFilename(index);
  }
  return "";
}

int Isolate::GetExternallyCompiledFilenameCount() const {
  if (embedded_file_writer_ != nullptr) {
    return embedded_file_writer_->GetExternallyCompiledFilenameCount();
  }
  return 0;
}

void Isolate::PrepareBuiltinSourcePositionMap() {
  if (embedded_file_writer_ != nullptr) {
    return embedded_file_writer_->PrepareBuiltinSourcePositionMap(
        this->builtins());
  }
}

void Isolate::PrepareBuiltinLabelInfoMap() {
  if (embedded_file_writer_ != nullptr) {
    embedded_file_writer_->PrepareBuiltinLabelInfoMap(
        heap()->construct_stub_create_deopt_pc_offset().value(),
        heap()->construct_stub_invoke_deopt_pc_offset().value());
  }
}

#if defined(V8_OS_WIN64)
void Isolate::SetBuiltinUnwindData(
    Builtin builtin,
    const win64_unwindinfo::BuiltinUnwindInfo& unwinding_info) {
  if (embedded_file_writer_ != nullptr) {
    embedded_file_writer_->SetBuiltinUnwindData(builtin, unwinding_info);
  }
}
#endif  // V8_OS_WIN64

void Isolate::SetPrepareStackTraceCallback(PrepareStackTraceCallback callback) {
  prepare_stack_trace_callback_ = callback;
}

bool Isolate::HasPrepareStackTraceCallback() const {
  return prepare_stack_trace_callback_ != nullptr;
}

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
void Isolate::SetFilterETWSessionByURLCallback(
    FilterETWSessionByURLCallback callback) {
  filter_etw_session_by_url_callback_ = callback;
}

bool Isolate::RunFilterETWSessionByURLCallback(
    const std::string& etw_filter_payload) {
  if (!filter_etw_session_by_url_callback_) return true;
  v8::Local<v8::Context> context = Utils::ToLocal(native_context());
  return filter_etw_session_by_url_callback_(context, etw_filter_payload);
}
#endif  // V8_OS_WIN && V8_ENABLE_ETW_STACK_WALKING

void Isolate::SetAddCrashKeyCallback(AddCrashKeyCallback callback) {
  add_crash_key_callback_ = callback;

  // Log the initial set of data.
  AddCrashKeysForIsolateAndHeapPointers();
}

void Isolate::SetAtomicsWaitCallback(v8::Isolate::AtomicsWaitCallback callback,
                                     void* data) {
  atomics_wait_callback_ = callback;
  atomics_wait_callback_data_ = data;
}

void Isolate::RunAtomicsWaitCallback(v8::Isolate::AtomicsWaitEvent event,
                                     Handle<JSArrayBuffer> array_buffer,
                                     size_t offset_in_bytes, int64_t value,
                                     double timeout_in_ms,
                                     AtomicsWaitWakeHandle* stop_handle) {
  DCHECK(array_buffer->is_shared());
  if (atomics_wait_callback_ == nullptr) return;
  HandleScope handle_scope(this);
  atomics_wait_callback_(
      event, v8::Utils::ToLocalShared(array_buffer), offset_in_bytes, value,
      timeout_in_ms,
      reinterpret_cast<v8::Isolate::AtomicsWaitWakeHandle*>(stop_handle),
      atomics_wait_callback_data_);
}

void Isolate::SetPromiseHook(PromiseHook hook) {
  promise_hook_ = hook;
  PromiseHookStateUpdated();
}

void Isolate::RunAllPromiseHooks(PromiseHookType type,
                                 Handle<JSPromise> promise,
                                 Handle<Object> parent) {
#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  if (HasContextPromiseHooks()) {
    native_context()->RunPromiseHook(type, promise, parent);
  }
#endif
  if (HasIsolatePromiseHooks() || HasAsyncEventDelegate()) {
    RunPromiseHook(type, promise, parent);
  }
}

void Isolate::RunPromiseHook(PromiseHookType type, Handle<JSPromise> promise,
                             Handle<Object> parent) {
  if (!HasIsolatePromiseHooks()) return;
  DCHECK(promise_hook_ != nullptr);
  promise_hook_(type, v8::Utils::PromiseToLocal(promise),
                v8::Utils::ToLocal(parent));
}

void Isolate::OnAsyncFunctionSuspended(Handle<JSPromise> promise,
                                       Handle<JSPromise> parent) {
  DCHECK_EQ(0, promise->async_task_id());
  RunAllPromiseHooks(PromiseHookType::kInit, promise, parent);
  if (HasAsyncEventDelegate()) {
    DCHECK_NE(nullptr, async_event_delegate_);
    promise->set_async_task_id(++async_task_count_);
    async_event_delegate_->AsyncEventOccurred(debug::kDebugAwait,
                                              promise->async_task_id(), false);
  }
}

void Isolate::OnPromiseThen(DirectHandle<JSPromise> promise) {
  if (!HasAsyncEventDelegate()) return;
  Maybe<debug::DebugAsyncActionType> action_type =
      Nothing<debug::DebugAsyncActionType>();
  for (JavaScriptStackFrameIterator it(this); !it.done(); it.Advance()) {
    std::vector<Handle<SharedFunctionInfo>> infos;
    it.frame()->GetFunctions(&infos);
    for (auto it = infos.rbegin(); it != infos.rend(); ++it) {
      DirectHandle<SharedFunctionInfo> info = *it;
      if (info->HasBuiltinId()) {
        // We should not report PromiseThen and PromiseCatch which is called
        // indirectly, e.g. Promise.all calls Promise.then internally.
        switch (info->builtin_id()) {
          case Builtin::kPromisePrototypeCatch:
            action_type = Just(debug::kDebugPromiseCatch);
            continue;
          case Builtin::kPromisePrototypeFinally:
            action_type = Just(debug::kDebugPromiseFinally);
            continue;
          case Builtin::kPromisePrototypeThen:
            action_type = Just(debug::kDebugPromiseThen);
            continue;
          default:
            return;
        }
      }
      if (info->IsUserJavaScript() && action_type.IsJust()) {
        DCHECK_EQ(0, promise->async_task_id());
        promise->set_async_task_id(++async_task_count_);
        async_event_delegate_->AsyncEventOccurred(action_type.FromJust(),
                                                  promise->async_task_id(),
                                                  debug()->IsBlackboxed(info));
      }
      return;
    }
  }
}

void Isolate::OnPromiseBefore(Handle<JSPromise> promise) {
  RunPromiseHook(PromiseHookType::kBefore, promise,
                 factory()->undefined_value());
  if (HasAsyncEventDelegate()) {
    if (promise->async_task_id()) {
      async_event_delegate_->AsyncEventOccurred(
          debug::kDebugWillHandle, promise->async_task_id(), false);
    }
  }
}

void Isolate::OnPromiseAfter(Handle<JSPromise> promise) {
  RunPromiseHook(PromiseHookType::kAfter, promise,
                 factory()->undefined_value());
  if (HasAsyncEventDelegate()) {
    if (promise->async_task_id()) {
      async_event_delegate_->AsyncEventOccurred(
          debug::kDebugDidHandle, promise->async_task_id(), false);
    }
  }
}

void Isolate::OnTerminationDuringRunMicrotasks() {
  DCHECK(is_execution_terminating());
  // This performs cleanup for when RunMicrotasks (in
  // builtins-microtask-queue-gen.cc) is aborted via a termination exception.
  // This has to be kept in sync with the code in said file. Currently this
  // includes:
  //
  //  (1) Resetting the |current_microtask| slot on the Isolate to avoid leaking
  //      memory (and also to keep |current_microtask| not being undefined as an
  //      indicator that we're currently pumping the microtask queue).
  //  (2) Empty the promise stack to avoid leaking memory.
  //  (3) If the |current_microtask| is a promise reaction or resolve thenable
  //      job task, then signal the async event delegate and debugger that the
  //      microtask finished running.
  //

  // Reset the |current_microtask| global slot.
  DirectHandle<Microtask> current_microtask(
      Cast<Microtask>(heap()->current_microtask()), this);
  heap()->set_current_microtask(ReadOnlyRoots(this).undefined_value());

  if (IsPromiseReactionJobTask(*current_microtask)) {
    auto promise_reaction_job_task =
        Cast<PromiseReactionJobTask>(current_microtask);
    Handle<HeapObject> promise_or_capability(
        promise_reaction_job_task->promise_or_capability(), this);
    if (IsPromiseCapability(*promise_or_capability)) {
      promise_or_capability = handle(
          Cast<PromiseCapability>(promise_or_capability)->promise(), this);
    }
    if (IsJSPromise(*promise_or_capability)) {
      OnPromiseAfter(Cast<JSPromise>(promise_or_capability));
    }
  } else if (IsPromiseResolveThenableJobTask(*current_microtask)) {
    auto promise_resolve_thenable_job_task =
        Cast<PromiseResolveThenableJobTask>(current_microtask);
    Handle<JSPromise> promise_to_resolve(
        promise_resolve_thenable_job_task->promise_to_resolve(), this);
    OnPromiseAfter(promise_to_resolve);
  }

  SetTerminationOnExternalTryCatch();
}

void Isolate::SetPromiseRejectCallback(PromiseRejectCallback callback) {
  promise_reject_callback_ = callback;
}

void Isolate::ReportPromiseReject(Handle<JSPromise> promise,
                                  Handle<Object> value,
                                  v8::PromiseRejectEvent event) {
  if (promise_reject_callback_ == nullptr) return;
  promise_reject_callback_(v8::PromiseRejectMessage(
      v8::Utils::PromiseToLocal(promise), event, v8::Utils::ToLocal(value)));
}

void Isolate::SetUseCounterCallback(v8::Isolate::UseCounterCallback callback) {
  DCHECK(!use_counter_callback_);
  use_counter_callback_ = callback;
}

void Isolate::CountUsage(v8::Isolate::UseCounterFeature feature) {
  CountUsage(base::VectorOf({feature}));
}

void Isolate::CountUsage(
    base::Vector<const v8::Isolate::UseCounterFeature> features) {
  // The counter callback
  // - may cause the embedder to call into V8, which is not generally possible
  //   during GC.
  // - requires a current native context, which may not always exist.
  // TODO(jgruber): Consider either removing the native context requirement in
  // blink, or passing it to the callback explicitly.
  if (heap_.gc_state() == Heap::NOT_IN_GC && !context().is_null()) {
    DCHECK(IsContext(context()));
    DCHECK(IsNativeContext(context()->native_context()));
    if (use_counter_callback_) {
      HandleScope handle_scope(this);
      for (auto feature : features) {
        use_counter_callback_(reinterpret_cast<v8::Isolate*>(this), feature);
      }
    }
  } else {
    heap_.IncrementDeferredCounts(features);
  }
}

int Isolate::GetNextScriptId() { return heap()->NextScriptId(); }

// static
std::string Isolate::GetTurboCfgFileName(Isolate* isolate) {
  if (const char* filename = v8_flags.trace_turbo_cfg_file) return filename;
  std::ostringstream os;
  os << "turbo-" << base::OS::GetCurrentProcessId() << "-";
  if (isolate != nullptr) {
    os << isolate->id();
  } else {
    os << "any";
  }
  os << ".cfg";
  return os.str();
}

// Heap::detached_contexts tracks detached contexts as pairs
// (the context, number of GC since the context was detached).
void Isolate::AddDetachedContext(Handle<Context> context) {
  HandleScope scope(this);
  Handle<WeakArrayList> detached_contexts = factory()->detached_contexts();
  detached_contexts = WeakArrayList::AddToEnd(
      this, detached_contexts, MaybeObjectHandle::Weak(context), Smi::zero());
  heap()->set_detached_contexts(*detached_contexts);
}

void Isolate::CheckDetachedContextsAfterGC() {
  HandleScope scope(this);
  DirectHandle<WeakArrayList> detached_contexts =
      factory()->detached_contexts();
  int length = detached_contexts->length();
  if (length == 0) return;
  int new_length = 0;
  for (int i = 0; i < length; i += 2) {
    Tagged<MaybeObject> context = detached_contexts->Get(i);
    DCHECK(context.IsWeakOrCleared());
    if (!context.IsCleared()) {
      int mark_sweeps = detached_contexts->Get(i + 1).ToSmi().value();
      detached_contexts->Set(new_length, context);
      detached_contexts->Set(new_length + 1, Smi::FromInt(mark_sweeps + 1));
      new_length += 2;
    }
  }
  detached_contexts->set_length(new_length);
  while (new_length < length) {
    detached_contexts->Set(new_length, Smi::zero());
    ++new_length;
  }

  if (v8_flags.trace_detached_contexts) {
    PrintF("%d detached contexts are collected out of %d\n",
           length - new_length, length);
    for (int i = 0; i < new_length; i += 2) {
      Tagged<MaybeObject> context = detached_contexts->Get(i);
      int mark_sweeps = detached_contexts->Get(i + 1).ToSmi().value();
      DCHECK(context.IsWeakOrCleared());
      if (mark_sweeps > 3) {
        PrintF("detached context %p\n survived %d GCs (leak?)\n",
               reinterpret_cast<void*>(context.ptr()), mark_sweeps);
      }
    }
  }
}

void Isolate::DetachGlobal(Handle<Context> env) {
  counters()->errors_thrown_per_context()->AddSample(
      env->native_context()->GetErrorsThrown());

  ReadOnlyRoots roots(this);
  DirectHandle<JSGlobalProxy> global_proxy(env->global_proxy(), this);
  // NOTE: Turbofan's JSNativeContextSpecialization and Maglev depend on
  // DetachGlobal causing a map change.
  JSObject::ForceSetPrototype(this, global_proxy, factory()->null_value());
  // Detach the global object from the native context by making its map
  // contextless (use the global metamap instead of the contextful one).
  global_proxy->map()->set_map(roots.meta_map());
  global_proxy->map()->set_constructor_or_back_pointer(roots.null_value(),
                                                       kRelaxedStore);
  if (v8_flags.track_detached_contexts) AddDetachedContext(env);
  DCHECK(global_proxy->IsDetached());

  env->native_context()->set_microtask_queue(this, nullptr);
}

void Isolate::UpdateLoadStartTime() { heap()->UpdateLoadStartTime(); }

void Isolate::SetRAILMode(RAILMode rail_mode) {
  RAILMode old_rail_mode = rail_mode_.load();
  if (old_rail_mode != PERFORMANCE_LOAD && rail_mode == PERFORMANCE_LOAD) {
    heap()->NotifyLoadingStarted();
  }
  rail_mode_.store(rail_mode);
  if (old_rail_mode == PERFORMANCE_LOAD && rail_mode != PERFORMANCE_LOAD) {
    heap()->NotifyLoadingEnded();
  }
  if (v8_flags.trace_rail) {
    PrintIsolate(this, "RAIL mode: %s\n", RAILModeName(rail_mode));
  }
}

void Isolate::SetPriority(v8::Isolate::Priority priority) {
  priority_ = priority;
  if (priority_ == v8::Isolate::Priority::kBestEffort) {
    heap()->ActivateMemoryReducerIfNeeded();
  }
}

void Isolate::PrintWithTimestamp(const char* format, ...) {
  base::OS::Print("[%d:%p] %8.0f ms: ", base::OS::GetCurrentProcessId(),
                  static_cast<void*>(this), time_millis_since_init());
  va_list arguments;
  va_start(arguments, format);
  base::OS::VPrint(format, arguments);
  va_end(arguments);
}

void Isolate::SetIdle(bool is_idle) {
  StateTag state = current_vm_state();
  if (js_entry_sp() != kNullAddress) return;
  DCHECK(state == EXTERNAL || state == IDLE);
  if (is_idle) {
    set_current_vm_state(IDLE);
  } else if (state == IDLE) {
    set_current_vm_state(EXTERNAL);
  }
}

void Isolate::CollectSourcePositionsForAllBytecodeArrays() {
  if (!initialized_) return;

  HandleScope scope(this);
  std::vector<Handle<SharedFunctionInfo>> sfis;
  {
    HeapObjectIterator iterator(heap());
    for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
         obj = iterator.Next()) {
      if (!IsSharedFunctionInfo(obj)) continue;
      Tagged<SharedFunctionInfo> sfi = Cast<SharedFunctionInfo>(obj);
      // If the script is a Smi, then the SharedFunctionInfo is in
      // the process of being deserialized.
      Tagged<Object> script = sfi->raw_script(kAcquireLoad);
      if (IsSmi(script)) {
        DCHECK_EQ(script, Smi::uninitialized_deserialization_value());
        continue;
      }
      if (!sfi->CanCollectSourcePosition(this)) continue;
      sfis.push_back(Handle<SharedFunctionInfo>(sfi, this));
    }
  }
  for (auto sfi : sfis) {
    SharedFunctionInfo::EnsureSourcePositionsAvailable(this, sfi);
  }
}

#ifdef V8_INTL_SUPPORT

namespace {

std::string GetStringFromLocales(Isolate* isolate,
                                 DirectHandle<Object> locales) {
  if (IsUndefined(*locales, isolate)) return "";
  return std::string(Cast<String>(*locales)->ToCString().get());
}

bool StringEqualsLocales(Isolate* isolate, const std::string& str,
                         Handle<Object> locales) {
  if (IsUndefined(*locales, isolate)) return str.empty();
  return Cast<String>(locales)->IsEqualTo(
      base::VectorOf(str.c_str(), str.length()));
}

}  // namespace

const std::string& Isolate::DefaultLocale() {
  if (default_locale_.empty()) {
    icu::Locale default_locale;
    // Translate ICU's fallback locale to a well-known locale.
    if (strcmp(default_locale.getName(), "en_US_POSIX") == 0 ||
        strcmp(default_locale.getName(), "c") == 0) {
      set_default_locale("en-US");
    } else {
      // Set the locale
      set_default_locale(default_locale.isBogus()
                             ? "und"
                             : Intl::ToLanguageTag(default_locale).FromJust());
    }
    DCHECK(!default_locale_.empty());
  }
  return default_locale_;
}

void Isolate::ResetDefaultLocale() {
  default_locale_.clear();
  clear_cached_icu_objects();
  // We inline fast paths assuming certain locales. Since this path is rarely
  // taken, we deoptimize everything to keep things simple.
  Deoptimizer::DeoptimizeAll(this);
}

icu::UMemory* Isolate::get_cached_icu_object(ICUObjectCacheType cache_type,
                                             Handle<Object> locales) {
  const ICUObjectCacheEntry& entry =
      icu_object_cache_[static_cast<int>(cache_type)];
  return StringEqualsLocales(this, entry.locales, locales) ? entry.obj.get()
                                                           : nullptr;
}

void Isolate::set_icu_object_in_cache(ICUObjectCacheType cache_type,
                                      DirectHandle<Object> locales,
                                      std::shared_ptr<icu::UMemory> obj) {
  icu_object_cache_[static_cast<int>(cache_type)] = {
      GetStringFromLocales(this, locales), std::move(obj)};
}

void Isolate::clear_cached_icu_object(ICUObjectCacheType cache_type) {
  icu_object_cache_[static_cast<int>(cache_type)] = ICUObjectCacheEntry{};
}

void Isolate::clear_cached_icu_objects() {
  for (int i = 0; i < kICUObjectCacheTypeCount; i++) {
    clear_cached_icu_object(static_cast<ICUObjectCacheType>(i));
  }
}

#endif  // V8_INTL_SUPPORT

bool StackLimitCheck::HandleStackOverflowAndTerminationRequest() {
  DCHECK(InterruptRequested());
  if (V8_UNLIKELY(HasOverflowed())) {
    isolate_->StackOverflow();
    return true;
  }
  if (V8_UNLIKELY(isolate_->stack_guard()->HasTerminationRequest())) {
    isolate_->TerminateExecution();
    return true;
  }
  return false;
}

bool StackLimitCheck::JsHasOverflowed(uintptr_t gap) const {
  StackGuard* stack_guard = isolate_->stack_guard();
#ifdef USE_SIMULATOR
  // The simulator uses a separate JS stack.
  Address jssp_address = Simulator::current(isolate_)->get_sp();
  uintptr_t jssp = static_cast<uintptr_t>(jssp_address);
  if (jssp - gap < stack_guard->real_jslimit()) return true;
#endif  // USE_SIMULATOR
  return GetCurrentStackPosition() - gap < stack_guard->real_climit();
}

bool StackLimitCheck::WasmHasOverflowed(uintptr_t gap) const {
  StackGuard* stack_guard = isolate_->stack_guard();
  auto sp = isolate_->thread_local_top()->secondary_stack_sp_;
  auto limit = isolate_->thread_local_top()->secondary_stack_limit_;
  if (sp == 0) {
#ifdef USE_SIMULATOR
    // The simulator uses a separate JS stack.
    // Use it if code is executed on the central stack.
    Address jssp_address = Simulator::current(isolate_)->get_sp();
    uintptr_t jssp = static_cast<uintptr_t>(jssp_address);
    if (jssp - gap < stack_guard->real_jslimit()) return true;
#endif  // USE_SIMULATOR
    sp = GetCurrentStackPosition();
    limit = stack_guard->real_climit();
  }
  return sp - gap < limit;
}

SaveContext::SaveContext(Isolate* isolate) : isolate_(isolate) {
  if (!isolate->context().is_null()) {
    context_ = Handle<Context>(isolate->context(), isolate);
  }
  if (!isolate->topmost_script_having_context().is_null()) {
    topmost_script_having_context_ =
        Handle<Context>(isolate->topmost_script_having_context(), isolate);
  }
}

SaveContext::~SaveContext() {
  isolate_->set_context(context_.is_null() ? Tagged<Context>() : *context_);
  isolate_->set_topmost_script_having_context(
      topmost_script_having_context_.is_null()
          ? Tagged<Context>()
          : *topmost_script_having_context_);
}

SaveAndSwitchContext::SaveAndSwitchContext(Isolate* isolate,
                                           Tagged<Context> new_context)
    : SaveContext(isolate) {
  isolate->set_context(new_context);
}

#ifdef DEBUG
AssertNoContextChange::AssertNoContextChange(Isolate* isolate)
    : isolate_(isolate),
      context_(isolate->context(), isolate),
      topmost_script_having_context_(isolate->topmost_script_having_context(),
                                     isolate) {}

namespace {

bool Overlapping(const MemoryRange& a, const MemoryRange& b) {
  uintptr_t a1 = reinterpret_cast<uintptr_t>(a.start);
  uintptr_t a2 = a1 + a.length_in_bytes;
  uintptr_t b1 = reinterpret_cast<uintptr_t>(b.start);
  uintptr_t b2 = b1 + b.length_in_bytes;
  // Either b1 or b2 are in the [a1, a2) range.
  return (a1 <= b1 && b1 < a2) || (a1 <= b2 && b2 < a2);
}

}  // anonymous namespace

#endif  // DEBUG

void Isolate::AddCodeMemoryRange(MemoryRange range) {
  base::MutexGuard guard(&code_pages_mutex_);
  std::vector<MemoryRange>* old_code_pages = GetCodePages();
  DCHECK_NOT_NULL(old_code_pages);
#ifdef DEBUG
  auto overlapping = [range](const MemoryRange& a) {
    return Overlapping(range, a);
  };
  DCHECK_EQ(old_code_pages->end(),
            std::find_if(old_code_pages->begin(), old_code_pages->end(),
                         overlapping));
#endif

  std::vector<MemoryRange>* new_code_pages;
  if (old_code_pages == &code_pages_buffer1_) {
    new_code_pages = &code_pages_buffer2_;
  } else {
    new_code_pages = &code_pages_buffer1_;
  }

  // Copy all existing data from the old vector to the new vector and insert the
  // new page.
  new_code_pages->clear();
  new_code_pages->reserve(old_code_pages->size() + 1);
  std::merge(old_code_pages->begin(), old_code_pages->end(), &range, &range + 1,
             std::back_inserter(*new_code_pages),
             [](const MemoryRange& a, const MemoryRange& b) {
               return a.start < b.start;
             });

  // Atomically switch out the pointer
  SetCodePages(new_code_pages);
}

// |chunk| is either a Page or an executable LargePage.
void Isolate::AddCodeMemoryChunk(MutablePageMetadata* chunk) {
  // We only keep track of individual code pages/allocations if we are on arm32,
  // because on x64 and arm64 we have a code range which makes this unnecessary.
#if defined(V8_TARGET_ARCH_ARM)
  void* new_page_start = reinterpret_cast<void*>(chunk->area_start());
  size_t new_page_size = chunk->area_size();

  MemoryRange new_range{new_page_start, new_page_size};

  AddCodeMemoryRange(new_range);
#endif  // !defined(V8_TARGET_ARCH_ARM)
}

void Isolate::AddCodeRange(Address begin, size_t length_in_bytes) {
  AddCodeMemoryRange(
      MemoryRange{reinterpret_cast<void*>(begin), length_in_bytes});
}

bool Isolate::RequiresCodeRange() const {
  return kPlatformRequiresCodeRange && !jitless_;
}

v8::metrics::Recorder::ContextId Isolate::GetOrRegisterRecorderContextId(
    DirectHandle<NativeContext> context) {
  if (serializer_enabled_) return v8::metrics::Recorder::ContextId::Empty();
  i::Tagged<i::Object> id = context->recorder_context_id();
  if (IsNullOrUndefined(id)) {
    CHECK_LT(last_recorder_context_id_, i::Smi::kMaxValue);
    context->set_recorder_context_id(
        i::Smi::FromIntptr(++last_recorder_context_id_));
    v8::HandleScope handle_scope(reinterpret_cast<v8::Isolate*>(this));
    auto result = recorder_context_id_map_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(last_recorder_context_id_),
        std::forward_as_tuple(reinterpret_cast<v8::Isolate*>(this),
                              ToApiHandle<v8::Context>(context)));
    result.first->second.SetWeak(
        reinterpret_cast<void*>(last_recorder_context_id_),
        RemoveContextIdCallback, v8::WeakCallbackType::kParameter);
    return v8::metrics::Recorder::ContextId(last_recorder_context_id_);
  } else {
    DCHECK(IsSmi(id));
    return v8::metrics::Recorder::ContextId(
        static_cast<uintptr_t>(i::Smi::ToInt(id)));
  }
}

MaybeLocal<v8::Context> Isolate::GetContextFromRecorderContextId(
    v8::metrics::Recorder::ContextId id) {
  auto result = recorder_context_id_map_.find(id.id_);
  if (result == recorder_context_id_map_.end() || result->second.IsEmpty())
    return MaybeLocal<v8::Context>();
  return result->second.Get(reinterpret_cast<v8::Isolate*>(this));
}

void Isolate::UpdateLongTaskStats() {
  if (last_long_task_stats_counter_ != isolate_data_.long_task_stats_counter_) {
    last_long_task_stats_counter_ = isolate_data_.long_task_stats_counter_;
    long_task_stats_ = v8::metrics::LongTaskStats{};
  }
}

v8::metrics::LongTaskStats* Isolate::GetCurrentLongTaskStats() {
  UpdateLongTaskStats();
  return &long_task_stats_;
}

void Isolate::RemoveContextIdCallback(const v8::WeakCallbackInfo<void>& data) {
  Isolate* isolate = reinterpret_cast<Isolate*>(data.GetIsolate());
  uintptr_t context_id = reinterpret_cast<uintptr_t>(data.GetParameter());
  isolate->recorder_context_id_map_.erase(context_id);
}

LocalHeap* Isolate::main_thread_local_heap() {
  return main_thread_local_isolate()->heap();
}

LocalHeap* Isolate::CurrentLocalHeap() {
  LocalHeap* local_heap = LocalHeap::Current();
  if (local_heap) return local_heap;
  DCHECK_EQ(ThreadId::Current(), thread_id());
  return main_thread_local_heap();
}

// |chunk| is either a Page or an executable LargePage.
void Isolate::RemoveCodeMemoryChunk(MutablePageMetadata* chunk) {
  // We only keep track of individual code pages/allocations if we are on arm32,
  // because on x64 and arm64 we have a code range which makes this unnecessary.
#if defined(V8_TARGET_ARCH_ARM)
  void* removed_page_start = reinterpret_cast<void*>(chunk->area_start());
  std::vector<MemoryRange>* old_code_pages = GetCodePages();
  DCHECK_NOT_NULL(old_code_pages);

  std::vector<MemoryRange>* new_code_pages;
  if (old_code_pages == &code_pages_buffer1_) {
    new_code_pages = &code_pages_buffer2_;
  } else {
    new_code_pages = &code_pages_buffer1_;
  }

  // Copy all existing data from the old vector to the new vector except the
  // removed page.
  new_code_pages->clear();
  new_code_pages->reserve(old_code_pages->size() - 1);
  std::remove_copy_if(old_code_pages->begin(), old_code_pages->end(),
                      std::back_inserter(*new_code_pages),
                      [removed_page_start](const MemoryRange& range) {
                        return range.start == removed_page_start;
                      });
  DCHECK_EQ(old_code_pages->size(), new_code_pages->size() + 1);
  // Atomically switch out the pointer
  SetCodePages(new_code_pages);
#endif  // !defined(V8_TARGET_ARCH_ARM)
}

#if V8_ENABLE_DRUMBRAKE
void Isolate::initialize_wasm_execution_timer() {
  DCHECK(v8_flags.wasm_enable_exec_time_histograms &&
         v8_flags.slow_histograms && !v8_flags.wasm_jitless);
  wasm_execution_timer_ =
      std::make_unique<wasm::WasmExecutionTimer>(this, false);
}
#endif  // V8_ENABLE_DRUMBRAKE

#undef TRACE_ISOLATE

// static
Address Isolate::load_from_stack_count_address(const char* function_name) {
  DCHECK_NOT_NULL(function_name);
  if (!stack_access_count_map) {
    stack_access_count_map = new MapOfLoadsAndStoresPerFunction{};
  }
  auto& map = *stack_access_count_map;
  std::string name(function_name);
  // It is safe to return the address of std::map values.
  // Only iterators and references to the erased elements are invalidated.
  return reinterpret_cast<Address>(&map[name].first);
}

// static
Address Isolate::store_to_stack_count_address(const char* function_name) {
  DCHECK_NOT_NULL(function_name);
  if (!stack_access_count_map) {
    stack_access_count_map = new MapOfLoadsAndStoresPerFunction{};
  }
  auto& map = *stack_access_count_map;
  std::string name(function_name);
  // It is safe to return the address of std::map values.
  // Only iterators and references to the erased elements are invalidated.
  return reinterpret_cast<Address>(&map[name].second);
}

void Isolate::LocalsBlockListCacheSet(Handle<ScopeInfo> scope_info,
                                      Handle<ScopeInfo> outer_scope_info,
                                      Handle<StringSet> locals_blocklist) {
  Handle<EphemeronHashTable> cache;
  if (IsEphemeronHashTable(heap()->locals_block_list_cache())) {
    cache = handle(Cast<EphemeronHashTable>(heap()->locals_block_list_cache()),
                   this);
  } else {
    CHECK(IsUndefined(heap()->locals_block_list_cache()));
    constexpr int kInitialCapacity = 8;
    cache = EphemeronHashTable::New(this, kInitialCapacity);
  }
  DCHECK(IsEphemeronHashTable(*cache));

  Handle<Object> value;
  if (!outer_scope_info.is_null()) {
    value = factory()->NewTuple2(outer_scope_info, locals_blocklist,
                                 AllocationType::kYoung);
  } else {
    value = locals_blocklist;
  }

  CHECK(!value.is_null());
  cache = EphemeronHashTable::Put(cache, scope_info, value);
  heap()->set_locals_block_list_cache(*cache);
}

Tagged<Object> Isolate::LocalsBlockListCacheGet(Handle<ScopeInfo> scope_info) {
  DisallowGarbageCollection no_gc;

  if (!IsEphemeronHashTable(heap()->locals_block_list_cache())) {
    return ReadOnlyRoots(this).the_hole_value();
  }

  Tagged<Object> maybe_value =
      Cast<EphemeronHashTable>(heap()->locals_block_list_cache())
          ->Lookup(scope_info);
  if (IsTuple2(maybe_value)) return Cast<Tuple2>(maybe_value)->value2();

  CHECK(IsStringSet(maybe_value) || IsTheHole(maybe_value));
  return maybe_value;
}

std::list<std::unique_ptr<detail::WaiterQueueNode>>&
Isolate::async_waiter_queue_nodes() {
  return async_waiter_queue_nodes_;
}

void DefaultWasmAsyncResolvePromiseCallback(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Promise::Resolver> resolver, v8::Local<v8::Value> result,
    WasmAsyncSuccess success) {
  MicrotasksScope microtasks_scope(context,
                                   MicrotasksScope::kDoNotRunMicrotasks);

  Maybe<bool> ret = success == WasmAsyncSuccess::kSuccess
                        ? resolver->Resolve(context, result)
                        : resolver->Reject(context, result);
  // It's guaranteed that no exceptions will be thrown by these
  // operations, but execution might be terminating.
  CHECK(ret.IsJust() ? ret.FromJust() : isolate->IsExecutionTerminating());
}

void Isolate::InitializeBuiltinJSDispatchTable() {
#ifdef V8_ENABLE_LEAPTIERING
  for (JSBuiltinDispatchHandleRoot::Idx idx =
           JSBuiltinDispatchHandleRoot::kFirst;
       idx < JSBuiltinDispatchHandleRoot::kEnd;
       idx = static_cast<JSBuiltinDispatchHandleRoot::Idx>(
           static_cast<int>(idx) + 1)) {
    Builtin builtin = JSBuiltinDispatchHandleRoot::to_builtin(idx);
    Tagged<Code> code = builtins_.code(builtin);
    DCHECK(code->entrypoint_tag() == CodeEntrypointTag::kJSEntrypointTag);
    // TODO(olivf, 40931165): It might be more robust to get the static
    // parameter count of this builtin.
    JSDispatchHandle handle =
        GetProcessWideJSDispatchTable()->AllocateAndInitializeEntry(
            read_only_heap()->js_dispatch_table_space(),
            code->parameter_count());
    DCHECK(!GetProcessWideJSDispatchTable()->HasCode(handle));
    GetProcessWideJSDispatchTable()->SetCode(handle, code);
    builtin_dispatch_table()[idx] = handle;
  }
#endif
}

}  // namespace internal
}  // namespace v8
