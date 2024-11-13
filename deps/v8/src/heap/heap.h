// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_H_
#define V8_HEAP_HEAP_H_

#include <atomic>
#include <cmath>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!
#include "include/v8-callbacks.h"
#include "include/v8-embedder-heap.h"
#include "include/v8-internal.h"
#include "include/v8-isolate.h"
#include "src/base/atomic-utils.h"
#include "src/base/enum-set.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/small-vector.h"
#include "src/builtins/accessors.h"
#include "src/common/assert-scope.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/allocation-result.h"
#include "src/heap/gc-callbacks.h"
#include "src/heap/heap-allocator.h"
#include "src/heap/marking-state.h"
#include "src/heap/minor-gc-job.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/sweeper.h"
#include "src/init/heap-symbols.h"
#include "src/objects/allocation-site.h"
#include "src/objects/fixed-array.h"
#include "src/objects/hash-table.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "src/objects/visitors.h"
#include "src/roots/roots.h"
#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/js-dispatch-table.h"
#include "src/sandbox/trusted-pointer-table.h"
#include "src/utils/allocation.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace cppgc::internal {
enum class HeapObjectNameForUnnamedObject : uint8_t;
class ClassNameAsHeapObjectNameScope;
}  // namespace cppgc::internal

namespace heap::base {
class Stack;
}  // namespace heap::base

namespace v8 {

namespace debug {
using OutOfMemoryCallback = void (*)(void* data);
}  // namespace debug

namespace internal {

namespace heap {
class HeapTester;
class TestMemoryAllocatorScope;
}  // namespace heap

namespace third_party_heap {
class Heap;
class Impl;
}  // namespace third_party_heap

class ArrayBufferCollector;
class ArrayBufferSweeper;
class BackingStore;
class MemoryChunkMetadata;
class Boolean;
class CodeLargeObjectSpace;
class CodeRange;
class CollectionBarrier;
class ConcurrentMarking;
class CppHeap;
class EphemeronRememberedSet;
class GCTracer;
class IncrementalMarking;
class IsolateSafepoint;
class HeapObjectAllocationTracker;
class HeapObjectsFilter;
class HeapStats;
class Isolate;
class JSArrayBuffer;
class JSFinalizationRegistry;
class JSPromise;
class LinearAllocationArea;
class LocalHeap;
class MemoryAllocator;
class MemoryBalancer;
class MutablePageMetadata;
class MemoryMeasurement;
class MemoryReducer;
class MinorMarkSweepCollector;
class NativeContext;
class NopRwxMemoryWriteScope;
class ObjectIterator;
class ObjectStats;
class PageMetadata;
class PagedSpace;
class PagedNewSpace;
class ReadOnlyHeap;
class RootVisitor;
class RwxMemoryWriteScope;
class SafepointScope;
class Scavenger;
class ScavengerCollector;
class SemiSpaceNewSpace;
class SharedLargeObjectSpace;
class SharedReadOnlySpace;
class SharedSpace;
class SharedTrustedLargeObjectSpace;
class SharedTrustedSpace;
class Space;
class StickySpace;
class StressScavengeObserver;
class TimedHistogram;
class TrustedLargeObjectSpace;
class TrustedRange;
class TrustedSpace;
class WeakObjectRetainer;

enum class ClearRecordedSlots { kYes, kNo };

enum class InvalidateRecordedSlots { kYes, kNo };

enum class InvalidateExternalPointerSlots { kYes, kNo };

enum class ClearFreedMemoryMode { kClearFreedMemory, kDontClearFreedMemory };

enum class SkipRoot {
  kExternalStringTable,
  kGlobalHandles,
  kTracedHandles,
  kOldGeneration,
  kStack,
  kMainThreadHandles,
  kUnserializable,
  kWeak,
  kConservativeStack,
  kReadOnlyBuiltins,
};

enum class EmbedderStackStateOrigin {
  kImplicitThroughTask,
  kExplicitInvocation,
};

class StrongRootsEntry final {
  explicit StrongRootsEntry(const char* label) : label(label) {}

  // Label that identifies the roots in tooling.
  const char* label;
  FullObjectSlot start;
  FullObjectSlot end;
  StrongRootsEntry* prev;
  StrongRootsEntry* next;

  friend class Heap;
};

#ifdef DEBUG
struct CommentStatistic {
  const char* comment;
  int size;
  int count;
  void Clear() {
    comment = nullptr;
    size = 0;
    count = 0;
  }
  // Must be small, since an iteration is used for lookup.
  static const int kMaxComments = 64;
};
#endif

// An alias for std::unordered_map<Tagged<HeapObject>, T> which also
// sets proper Hash and KeyEqual functions.
template <typename T>
using UnorderedHeapObjectMap =
    std::unordered_map<Tagged<HeapObject>, T, Object::Hasher,
                       Object::KeyEqualSafe>;

enum class GCFlag : uint8_t {
  kNoFlags = 0,
  kReduceMemoryFootprint = 1 << 0,
  // GCs that are forced, either through testing configurations (requiring
  // --expose-gc) or through DevTools (using LowMemoryNotification).
  kForced = 1 << 1,
};

using GCFlags = base::Flags<GCFlag, uint8_t>;
DEFINE_OPERATORS_FOR_FLAGS(GCFlags)

class Heap final {
 public:
  enum class HeapGrowingMode { kSlow, kConservative, kMinimal, kDefault };

  enum HeapState {
    NOT_IN_GC,
    SCAVENGE,
    MARK_COMPACT,
    MINOR_MARK_SWEEP,
    TEAR_DOWN
  };

  // Emits GC events for DevTools timeline.
  class V8_NODISCARD DevToolsTraceEventScope {
   public:
    DevToolsTraceEventScope(Heap* heap, const char* event_name,
                            const char* event_type);
    ~DevToolsTraceEventScope();

   private:
    Heap* heap_;
    const char* event_name_;
  };

  class ExternalMemoryAccounting {
   public:
    int64_t total() const { return total_.load(std::memory_order_relaxed); }
    int64_t limit() const { return limit_.load(std::memory_order_relaxed); }
    int64_t low_since_mark_compact() const {
      return low_since_mark_compact_.load(std::memory_order_relaxed);
    }

    void ResetAfterGC() {
      set_low_since_mark_compact(total());
      set_limit(total() + kExternalAllocationSoftLimit);
    }

    int64_t Update(int64_t delta) {
      const int64_t amount =
          total_.fetch_add(delta, std::memory_order_relaxed) + delta;
      if (amount < low_since_mark_compact()) {
        set_low_since_mark_compact(amount);
        set_limit(amount + kExternalAllocationSoftLimit);
      }
      return amount;
    }

    int64_t AllocatedSinceMarkCompact() const {
      int64_t total_bytes = total();
      int64_t low_since_mark_compact_bytes = low_since_mark_compact();

      if (total_bytes <= low_since_mark_compact_bytes) {
        return 0;
      }
      return static_cast<uint64_t>(total_bytes - low_since_mark_compact_bytes);
    }

   private:
    void set_total(int64_t value) {
      total_.store(value, std::memory_order_relaxed);
    }

    void set_limit(int64_t value) {
      limit_.store(value, std::memory_order_relaxed);
    }

    void set_low_since_mark_compact(int64_t value) {
      low_since_mark_compact_.store(value, std::memory_order_relaxed);
    }

    // The amount of external memory registered through the API.
    std::atomic<int64_t> total_{0};

    // The limit when to trigger memory pressure from the API.
    std::atomic<int64_t> limit_{kExternalAllocationSoftLimit};

    // Caches the amount of external memory registered at the last MC.
    std::atomic<int64_t> low_since_mark_compact_{0};
  };

  // Taking this mutex prevents the GC from entering a phase that relocates
  // object references.
  base::Mutex* relocation_mutex() { return &relocation_mutex_; }

  // Support for context snapshots.  After calling this we have a linear
  // space to write objects in each space.
  struct Chunk {
    uint32_t size;
    Address start;
    Address end;
  };
  using Reservation = std::vector<Chunk>;

#if V8_OS_ANDROID
  // Don't apply pointer multiplier on Android since it has no swap space and
  // should instead adapt it's heap size based on available physical memory.
  static const int kPointerMultiplier = 1;
  static const int kHeapLimitMultiplier = 1;
#else
  static const int kPointerMultiplier = kTaggedSize / 4;
  // The heap limit needs to be computed based on the system pointer size
  // because we want a pointer-compressed heap to have larger limit than
  // an ordinary 32-bit which that is constrained by 2GB virtual address space.
  static const int kHeapLimitMultiplier = kSystemPointerSize / 4;
#endif

  static const size_t kMaxInitialOldGenerationSize =
      256 * MB * kHeapLimitMultiplier;

  // These constants control heap configuration based on the physical memory.
  static constexpr size_t kPhysicalMemoryToOldGenerationRatio = 4;
  static constexpr size_t kOldGenerationLowMemory =
      128 * MB * kHeapLimitMultiplier;
  static constexpr size_t kNewLargeObjectSpaceToSemiSpaceRatio = 1;

  static const int kTraceRingBufferSize = 512;
  static const int kStacktraceBufferSize = 512;

  // The minimum size of a HeapObject on the heap.
  static const int kMinObjectSizeInTaggedWords = 2;

  static size_t DefaultMinSemiSpaceSize();
  V8_EXPORT_PRIVATE static size_t DefaultMaxSemiSpaceSize();
  // Young generation size is the same for compressed heaps and 32-bit heaps.
  static size_t OldGenerationToSemiSpaceRatio();
  static size_t OldGenerationToSemiSpaceRatioLowMemory();

  // Calculates the maximum amount of filler that could be required by the
  // given alignment.
  V8_EXPORT_PRIVATE static int GetMaximumFillToAlign(
      AllocationAlignment alignment);
  // Calculates the actual amount of filler required for a given address at the
  // given alignment.
  V8_EXPORT_PRIVATE static int GetFillToAlign(Address address,
                                              AllocationAlignment alignment);

  // Returns the size of the initial area of a code-range, which is marked
  // writable and reserved to contain unwind information.
  static size_t GetCodeRangeReservedAreaSize();

  [[noreturn]] V8_EXPORT_PRIVATE void FatalProcessOutOfMemory(
      const char* location);

  // Checks whether the space is valid.
  static bool IsValidAllocationSpace(AllocationSpace space);

  // Helper function to get the bytecode flushing mode based on the flags. This
  // is required because it is not safe to access flags in concurrent marker.
  static inline base::EnumSet<CodeFlushMode> GetCodeFlushMode(Isolate* isolate);

  static inline bool IsYoungGenerationCollector(GarbageCollector collector) {
    return collector == GarbageCollector::SCAVENGER ||
           collector == GarbageCollector::MINOR_MARK_SWEEPER;
  }

  static inline GarbageCollector YoungGenerationCollector() {
    return (v8_flags.minor_ms) ? GarbageCollector::MINOR_MARK_SWEEPER
                               : GarbageCollector::SCAVENGER;
  }

  // Copy block of memory from src to dst. Size of block should be aligned
  // by pointer size.
  static inline void CopyBlock(Address dst, Address src, int byte_size);

  // Executes generational and/or marking write barrier for a [start, end) range
  // of non-weak slots inside |object|.
  template <typename TSlot>
  V8_EXPORT_PRIVATE void WriteBarrierForRange(Tagged<HeapObject> object,
                                              TSlot start, TSlot end);

  // Implements slow path of both generational & shared heap barrier.
  V8_EXPORT_PRIVATE static void CombinedGenerationalAndSharedBarrierSlow(
      Tagged<HeapObject> object, Address slot, Tagged<HeapObject> value);
  V8_EXPORT_PRIVATE static void
  CombinedGenerationalAndSharedEphemeronBarrierSlow(
      Tagged<EphemeronHashTable> table, Address slot, Tagged<HeapObject> value);

  V8_EXPORT_PRIVATE static void GenerationalBarrierSlow(
      Tagged<HeapObject> object, Address slot, Tagged<HeapObject> value);

  V8_EXPORT_PRIVATE static void SharedHeapBarrierSlow(Tagged<HeapObject> object,
                                                      Address slot);
  V8_EXPORT_PRIVATE static void GenerationalBarrierForCodeSlow(
      Tagged<InstructionStream> host, RelocInfo* rinfo,
      Tagged<HeapObject> value);
  V8_EXPORT_PRIVATE static bool PageFlagsAreConsistent(
      Tagged<HeapObject> object);

  V8_EXPORT_PRIVATE inline void RecordEphemeronKeyWrite(
      Tagged<EphemeronHashTable> table, Address key_slot);
  V8_EXPORT_PRIVATE static void EphemeronKeyWriteBarrierFromCode(
      Address raw_object, Address address, Isolate* isolate);

  EphemeronRememberedSet* ephemeron_remembered_set() {
    return ephemeron_remembered_set_.get();
  }

  // Notifies the heap that is ok to start marking or other activities that
  // should not happen during deserialization.
  void NotifyDeserializationComplete();

  // Weakens StrongDescriptorArray objects into regular DescriptorArray objects.
  //
  // Thread-safe.
  void WeakenDescriptorArrays(
      GlobalHandleVector<DescriptorArray> strong_descriptor_arrays);

  void NotifyBootstrapComplete();

  void NotifyOldGenerationExpansion(LocalHeap* local_heap,
                                    AllocationSpace space,
                                    MutablePageMetadata* chunk);

  inline Address* NewSpaceAllocationTopAddress();
  inline Address* NewSpaceAllocationLimitAddress();
  inline Address* OldSpaceAllocationTopAddress();
  inline Address* OldSpaceAllocationLimitAddress();

  size_t NewSpaceSize();
  size_t NewSpaceCapacity() const;
  size_t NewSpaceTargetCapacity() const;

  // Move len non-weak tagged elements from src_slot to dst_slot of dst_object.
  // The source and destination memory ranges can overlap.
  V8_EXPORT_PRIVATE void MoveRange(Tagged<HeapObject> dst_object,
                                   ObjectSlot dst_slot, ObjectSlot src_slot,
                                   int len, WriteBarrierMode mode);

  // Copy len non-weak tagged elements from src_slot to dst_slot of dst_object.
  // The source and destination memory ranges must not overlap.
  template <typename TSlot>
  V8_EXPORT_PRIVATE void CopyRange(Tagged<HeapObject> dst_object,
                                   TSlot dst_slot, TSlot src_slot, int len,
                                   WriteBarrierMode mode);

  // Initialize a filler object to keep the ability to iterate over the heap
  // when introducing gaps within pages. This method will verify that no slots
  // are recorded in this free memory.
  V8_EXPORT_PRIVATE void CreateFillerObjectAt(
      Address addr, int size,
      ClearFreedMemoryMode clear_memory_mode =
          ClearFreedMemoryMode::kDontClearFreedMemory);

  // Initialize a filler object at a specific address. Unlike
  // `CreateFillerObjectAt` this method will not perform slot verification since
  // this would race on background threads.
  void CreateFillerObjectAtBackground(const WritableFreeSpace& free_space);

  bool CanMoveObjectStart(Tagged<HeapObject> object);

  bool IsImmovable(Tagged<HeapObject> object);

  V8_EXPORT_PRIVATE static bool IsLargeObject(Tagged<HeapObject> object);

  // Trim the given array from the left. Note that this relocates the object
  // start and hence is only valid if there is only a single reference to it.
  V8_EXPORT_PRIVATE Tagged<FixedArrayBase> LeftTrimFixedArray(
      Tagged<FixedArrayBase> obj, int elements_to_trim);

#define RIGHT_TRIMMABLE_ARRAY_LIST(V) \
  V(ArrayList)                        \
  V(ByteArray)                        \
  V(FixedArray)                       \
  V(FixedDoubleArray)                 \
  V(TransitionArray)                  \
  V(WeakFixedArray)

  // Trim the given array from the right.
  template <typename Array>
  void RightTrimArray(Tagged<Array> object, int new_capacity, int old_capacity);

  // Converts the given boolean condition to JavaScript boolean value.
  inline Tagged<Boolean> ToBoolean(bool condition);

  // Notify the heap that a context has been disposed. `has_dependent_context`
  // implies that a top-level context (no dependent contexts) has been disposed.
  V8_EXPORT_PRIVATE int NotifyContextDisposed(bool has_dependent_context);

  void set_native_contexts_list(Tagged<Object> object) {
    native_contexts_list_.store(object.ptr(), std::memory_order_release);
  }

  Tagged<Object> native_contexts_list() const {
    return Tagged<Object>(
        native_contexts_list_.load(std::memory_order_acquire));
  }

  void set_allocation_sites_list(Tagged<Object> object) {
    allocation_sites_list_ = object;
  }
  Tagged<Object> allocation_sites_list() { return allocation_sites_list_; }

  void set_dirty_js_finalization_registries_list(Tagged<Object> object) {
    dirty_js_finalization_registries_list_ = object;
  }
  Tagged<Object> dirty_js_finalization_registries_list() {
    return dirty_js_finalization_registries_list_;
  }
  void set_dirty_js_finalization_registries_list_tail(Tagged<Object> object) {
    dirty_js_finalization_registries_list_tail_ = object;
  }
  Tagged<Object> dirty_js_finalization_registries_list_tail() {
    return dirty_js_finalization_registries_list_tail_;
  }

  // Used in CreateAllocationSiteStub and the (de)serializer.
  Address allocation_sites_list_address() {
    return reinterpret_cast<Address>(&allocation_sites_list_);
  }

  // Traverse all the allocation_sites [nested_site and weak_next] in the list
  // and foreach call the visitor
  void ForeachAllocationSite(
      Tagged<Object> list,
      const std::function<void(Tagged<AllocationSite>)>& visitor);

  // Number of mark-sweeps.
  int ms_count() const { return ms_count_; }

  // Checks whether the given object is allowed to be migrated from it's
  // current space into the given destination space. Used for debugging.
  bool AllowedToBeMigrated(Tagged<Map> map, Tagged<HeapObject> object,
                           AllocationSpace dest);

  void CheckHandleCount();

  // Print short heap statistics.
  void PrintShortHeapStatistics();

  // Print statistics of freelists of old_space:
  //  with v8_flags.trace_gc_freelists: summary of each FreeListCategory.
  //  with v8_flags.trace_gc_freelists_verbose: also prints the statistics of
  //  each FreeListCategory of each page.
  void PrintFreeListsStats();

  // Dump heap statistics in JSON format.
  void DumpJSONHeapStatistics(std::stringstream& stream);

  inline HeapState gc_state() const {
    return gc_state_.load(std::memory_order_relaxed);
  }
  V8_EXPORT_PRIVATE void SetGCState(HeapState state);
  bool IsTearingDown() const { return gc_state() == TEAR_DOWN; }
  bool IsInGC() const {
    // Load state only once and store it in local variable. Otherwise multiples
    // loads could return different states on background threads.
    HeapState state = gc_state();
    return state != NOT_IN_GC && state != TEAR_DOWN;
  }
  bool force_oom() const { return force_oom_; }

  bool ignore_local_gc_requests() const {
    return ignore_local_gc_requests_depth_ > 0;
  }

  bool IsAllocationObserverActive() const {
    return pause_allocation_observers_depth_ == 0;
  }

  bool IsGCWithMainThreadStack() const;

  // This method is only safe to use in a safepoint.
  bool IsGCWithStack() const;

  bool CanShortcutStringsDuringGC(GarbageCollector collector) const;

  // Performs GC after background allocation failure.
  void CollectGarbageForBackground(LocalHeap* local_heap);

  //
  // Support for the API.
  //

  void CreateReadOnlyApiObjects();
  void CreateMutableApiObjects();

  V8_EXPORT_PRIVATE void MemoryPressureNotification(
      v8::MemoryPressureLevel level, bool is_isolate_locked);
  void CheckMemoryPressure();

  V8_EXPORT_PRIVATE void AddNearHeapLimitCallback(v8::NearHeapLimitCallback,
                                                  void* data);
  V8_EXPORT_PRIVATE void RemoveNearHeapLimitCallback(
      v8::NearHeapLimitCallback callback, size_t heap_limit);
  V8_EXPORT_PRIVATE void AutomaticallyRestoreInitialHeapLimit(
      double threshold_percent);

  V8_EXPORT_PRIVATE void AppendArrayBufferExtension(
      Tagged<JSArrayBuffer> object, ArrayBufferExtension* extension);
  void DetachArrayBufferExtension(ArrayBufferExtension* extension);

  IsolateSafepoint* safepoint() { return safepoint_.get(); }

  V8_EXPORT_PRIVATE double MonotonicallyIncreasingTimeInMs() const;

#if DEBUG
  void VerifyNewSpaceTop();
#endif  // DEBUG

  void RecordStats(HeapStats* stats, bool take_snapshot = false);

  bool MeasureMemory(std::unique_ptr<v8::MeasureMemoryDelegate> delegate,
                     v8::MeasureMemoryExecution execution);

  std::unique_ptr<v8::MeasureMemoryDelegate> MeasureMemoryDelegate(
      Handle<NativeContext> context, Handle<JSPromise> promise,
      v8::MeasureMemoryMode mode);

  void VisitExternalResources(v8::ExternalResourceVisitor* visitor);

  void IncrementDeferredCounts(
      base::Vector<const v8::Isolate::UseCounterFeature> features);

  inline int NextScriptId();
  inline int NextDebuggingId();
  inline int GetNextTemplateSerialNumber();

  void SetSerializedObjects(Tagged<HeapObject> objects);
  void SetSerializedGlobalProxySizes(Tagged<FixedArray> sizes);

  void SetBasicBlockProfilingData(DirectHandle<ArrayList> list);

  // For post mortem debugging.
  void RememberUnmappedPage(Address page, bool compacted);

  int64_t external_memory_hard_limit() { return max_old_generation_size() / 2; }

  V8_INLINE int64_t external_memory();
  V8_EXPORT_PRIVATE int64_t external_memory_limit();
  V8_INLINE int64_t update_external_memory(int64_t delta);

  V8_EXPORT_PRIVATE size_t YoungArrayBufferBytes();
  V8_EXPORT_PRIVATE size_t OldArrayBufferBytes();

  uint64_t backing_store_bytes() const {
    return backing_store_bytes_.load(std::memory_order_relaxed);
  }

  void CompactWeakArrayLists();

  V8_EXPORT_PRIVATE void AddRetainedMaps(DirectHandle<NativeContext> context,
                                         GlobalHandleVector<Map> maps);

  // This event is triggered after object is moved to a new place.
  void OnMoveEvent(Tagged<HeapObject> source, Tagged<HeapObject> target,
                   int size_in_bytes);

  bool deserialization_complete() const { return deserialization_complete_; }

  // We can only invoke Safepoint() on the main thread local heap after
  // deserialization is complete. Before that, main_thread_local_heap_ might be
  // null.
  V8_INLINE bool CanSafepoint() const { return deserialization_complete(); }

  bool HasLowAllocationRate();
  bool HasHighFragmentation();

  void ActivateMemoryReducerIfNeeded();

  V8_EXPORT_PRIVATE bool ShouldOptimizeForMemoryUsage();

  // Returns true when GC should optimize for battery.
  V8_EXPORT_PRIVATE bool ShouldOptimizeForBattery() const;

  bool HighMemoryPressure() {
    return memory_pressure_level_.load(std::memory_order_relaxed) !=
           v8::MemoryPressureLevel::kNone;
  }

  bool CollectionRequested();

  void CheckCollectionRequested();

  void RestoreHeapLimit(size_t heap_limit) {
    // Do not set the limit lower than the live size + some slack.
    size_t min_limit = SizeOfObjects() + SizeOfObjects() / 4;
    SetOldGenerationAndGlobalMaximumSize(
        std::min(max_old_generation_size(), std::max(heap_limit, min_limit)));
  }

#if V8_ENABLE_WEBASSEMBLY
  // TODO(manoskouk): Consider inlining/moving this if
  // STRONG_MUTABLE_MOVABLE_ROOT_LIST setters become public.
  V8_EXPORT_PRIVATE void EnsureWasmCanonicalRttsSize(int length);
  V8_EXPORT_PRIVATE void ClearWasmCanonicalRttsForTesting();
#endif

  // ===========================================================================
  // Initialization. ===========================================================
  // ===========================================================================

  void ConfigureHeap(const v8::ResourceConstraints& constraints,
                     v8::CppHeap* cpp_heap);
  void ConfigureHeapDefault();

  // Prepares the heap, setting up for deserialization.
  void SetUp(LocalHeap* main_thread_local_heap);

  // Sets read-only heap and space.
  void SetUpFromReadOnlyHeap(ReadOnlyHeap* ro_heap);

  void ReplaceReadOnlySpace(SharedReadOnlySpace* shared_ro_space);

  // Sets up the heap memory without creating any objects.
  void SetUpSpaces(LinearAllocationArea& new_allocation_info,
                   LinearAllocationArea& old_allocation_info);

  // Prepares the heap, setting up for deserialization.
  void InitializeMainThreadLocalHeap(LocalHeap* main_thread_local_heap);

  // (Re-)Initialize hash seed from flag or RNG.
  void InitializeHashSeed();

  // Invoked once for the process from V8::Initialize.
  static void InitializeOncePerProcess();

  // Bootstraps the object heap with the core set of objects required to run.
  // Returns whether it succeeded.
  bool CreateReadOnlyHeapObjects();
  bool CreateMutableHeapObjects();

  // Create ObjectStats if live_object_stats_ or dead_object_stats_ are nullptr.
  void CreateObjectStats();

  // Sets the TearDown state, so no new GC tasks get posted.
  void StartTearDown();

  // Destroys all data that might require the shared heap.
  void TearDownWithSharedHeap();

  // Destroys all memory allocated by the heap.
  void TearDown();

  // Returns whether SetUp has been called.
  bool HasBeenSetUp() const;

  // ===========================================================================
  // Getters for spaces. =======================================================
  // ===========================================================================

  V8_INLINE Address NewSpaceTop();
  V8_INLINE Address NewSpaceLimit();

  NewSpace* new_space() const { return new_space_; }
  inline PagedNewSpace* paged_new_space() const;
  inline SemiSpaceNewSpace* semi_space_new_space() const;
  OldSpace* old_space() const { return old_space_; }
  inline StickySpace* sticky_space() const;
  CodeSpace* code_space() const { return code_space_; }
  SharedSpace* shared_space() const { return shared_space_; }
  OldLargeObjectSpace* lo_space() const { return lo_space_; }
  CodeLargeObjectSpace* code_lo_space() const { return code_lo_space_; }
  SharedLargeObjectSpace* shared_lo_space() const { return shared_lo_space_; }
  NewLargeObjectSpace* new_lo_space() const { return new_lo_space_; }
  ReadOnlySpace* read_only_space() const { return read_only_space_; }
  TrustedSpace* trusted_space() const { return trusted_space_; }
  SharedTrustedSpace* shared_trusted_space() const {
    return shared_trusted_space_;
  }
  TrustedLargeObjectSpace* trusted_lo_space() const {
    return trusted_lo_space_;
  }
  SharedTrustedLargeObjectSpace* shared_trusted_lo_space() const {
    return shared_trusted_lo_space_;
  }

  PagedSpace* shared_allocation_space() const {
    return shared_allocation_space_;
  }
  OldLargeObjectSpace* shared_lo_allocation_space() const {
    return shared_lo_allocation_space_;
  }
  SharedTrustedSpace* shared_trusted_allocation_space() const {
    return shared_trusted_allocation_space_;
  }
  SharedTrustedLargeObjectSpace* shared_trusted_lo_allocation_space() const {
    return shared_trusted_lo_allocation_space_;
  }

  inline PagedSpace* paged_space(int idx) const;
  inline Space* space(int idx) const;

#ifdef V8_COMPRESS_POINTERS
  ExternalPointerTable::Space* young_external_pointer_space() {
    return &young_external_pointer_space_;
  }
  ExternalPointerTable::Space* old_external_pointer_space() {
    return &old_external_pointer_space_;
  }
  ExternalPointerTable::Space* read_only_external_pointer_space() {
    return &read_only_external_pointer_space_;
  }
  CppHeapPointerTable::Space* cpp_heap_pointer_space() {
    return &cpp_heap_pointer_space_;
  }
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  TrustedPointerTable::Space* trusted_pointer_space() {
    return &trusted_pointer_space_;
  }

  CodePointerTable::Space* code_pointer_space() { return &code_pointer_space_; }

#endif  // V8_ENABLE_SANDBOX

#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable::Space* js_dispatch_table_space() {
    return &js_dispatch_table_space_;
  }
#endif  // V8_ENABLE_LEAPTIERING

  // ===========================================================================
  // Getters to other components. ==============================================
  // ===========================================================================

  GCTracer* tracer() { return tracer_.get(); }

  MemoryAllocator* memory_allocator() { return memory_allocator_.get(); }
  const MemoryAllocator* memory_allocator() const {
    return memory_allocator_.get();
  }

  inline Isolate* isolate() const;

  // Check if we run on isolate's main thread.
  inline bool IsMainThread() const;

  MarkCompactCollector* mark_compact_collector() {
    return mark_compact_collector_.get();
  }

  MinorMarkSweepCollector* minor_mark_sweep_collector() {
    return minor_mark_sweep_collector_.get();
  }

  Sweeper* sweeper() { return sweeper_.get(); }

  ArrayBufferSweeper* array_buffer_sweeper() {
    return array_buffer_sweeper_.get();
  }

  // The potentially overreserved address space region reserved by the code
  // range if it exists or empty region otherwise.
  const base::AddressRegion& code_region();

  CodeRange* code_range() {
#ifdef V8_COMPRESS_POINTERS
    return code_range_;
#else
    return code_range_.get();
#endif
  }

  // The base of the code range if it exists or null address.
  inline Address code_range_base();

  LocalHeap* main_thread_local_heap() { return main_thread_local_heap_; }

  Heap* AsHeap() { return this; }

  // ===========================================================================
  // Root set access. ==========================================================
  // ===========================================================================

  // Shortcut to the roots table stored in the Isolate.
  V8_INLINE RootsTable& roots_table();

// Heap root getters.
#define ROOT_ACCESSOR(type, name, CamelName) inline Tagged<type> name();
  MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  V8_INLINE Tagged<FixedArray> single_character_string_table();

  V8_INLINE void SetRootMaterializedObjects(Tagged<FixedArray> objects);
  V8_INLINE void SetRootScriptList(Tagged<Object> value);
  V8_INLINE void SetRootNoScriptSharedFunctionInfos(Tagged<Object> value);
  V8_INLINE void SetMessageListeners(Tagged<ArrayList> value);
  V8_INLINE void SetFunctionsMarkedForManualOptimization(
      Tagged<Object> bytecode);

  StrongRootsEntry* RegisterStrongRoots(const char* label, FullObjectSlot start,
                                        FullObjectSlot end);
  void UnregisterStrongRoots(StrongRootsEntry* entry);
  void UpdateStrongRoots(StrongRootsEntry* entry, FullObjectSlot start,
                         FullObjectSlot end);

  void SetBuiltinsConstantsTable(Tagged<FixedArray> cache);
  void SetDetachedContexts(Tagged<WeakArrayList> detached_contexts);

  void EnqueueDirtyJSFinalizationRegistry(
      Tagged<JSFinalizationRegistry> finalization_registry,
      std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                         Tagged<Object> target)>
          gc_notify_updated_slot);

  MaybeHandle<JSFinalizationRegistry> DequeueDirtyJSFinalizationRegistry();

  // Called from Heap::NotifyContextDisposed to remove all
  // FinalizationRegistries with {context} from the dirty list when the context
  // e.g. navigates away or is detached. If the dirty list is empty afterwards,
  // the cleanup task is aborted if needed.
  void RemoveDirtyFinalizationRegistriesOnContext(
      Tagged<NativeContext> context);

  inline bool HasDirtyJSFinalizationRegistries();

  void PostFinalizationRegistryCleanupTaskIfNeeded();

  void set_is_finalization_registry_cleanup_task_posted(bool posted) {
    is_finalization_registry_cleanup_task_posted_ = posted;
  }

  bool is_finalization_registry_cleanup_task_posted() {
    return is_finalization_registry_cleanup_task_posted_;
  }

  V8_EXPORT_PRIVATE void KeepDuringJob(DirectHandle<HeapObject> target);
  void ClearKeptObjects();

  // ===========================================================================
  // Inline allocation. ========================================================
  // ===========================================================================

  // Switch whether inline bump-pointer allocation should be used.
  V8_EXPORT_PRIVATE void EnableInlineAllocation();
  V8_EXPORT_PRIVATE void DisableInlineAllocation();

  // ===========================================================================
  // Methods triggering GCs. ===================================================
  // ===========================================================================

  // Performs garbage collection operation.
  // Returns whether there is a chance that another major GC could
  // collect more garbage.
  V8_EXPORT_PRIVATE void CollectGarbage(
      AllocationSpace space, GarbageCollectionReason gc_reason,
      const GCCallbackFlags gc_callback_flags = kNoGCCallbackFlags);

  // Performs a full garbage collection.
  V8_EXPORT_PRIVATE void CollectAllGarbage(
      GCFlags gc_flags, GarbageCollectionReason gc_reason,
      const GCCallbackFlags gc_callback_flags = kNoGCCallbackFlags);

  // Last hope garbage collection. Will try to free as much memory as possible
  // with multiple rounds of garbage collection.
  V8_EXPORT_PRIVATE void CollectAllAvailableGarbage(
      GarbageCollectionReason gc_reason);

  // Precise garbage collection that potentially finalizes already running
  // incremental marking before performing an atomic garbage collection.
  // Only use if absolutely necessary or in tests to avoid floating garbage!
  V8_EXPORT_PRIVATE void PreciseCollectAllGarbage(
      GCFlags gc_flags, GarbageCollectionReason gc_reason,
      const GCCallbackFlags gc_callback_flags = kNoGCCallbackFlags);

  // Performs garbage collection operation for the shared heap.
  V8_EXPORT_PRIVATE bool CollectGarbageShared(
      LocalHeap* local_heap, GarbageCollectionReason gc_reason);

  // Requests garbage collection from some other thread.
  V8_EXPORT_PRIVATE bool CollectGarbageFromAnyThread(
      LocalHeap* local_heap,
      GarbageCollectionReason gc_reason =
          GarbageCollectionReason::kBackgroundAllocationFailure);

  // Reports and external memory pressure event, either performs a major GC or
  // completes incremental marking in order to free external resources.
  void ReportExternalMemoryPressure();

  using GetExternallyAllocatedMemoryInBytesCallback =
      v8::Isolate::GetExternallyAllocatedMemoryInBytesCallback;

  void SetGetExternallyAllocatedMemoryInBytesCallback(
      GetExternallyAllocatedMemoryInBytesCallback callback) {
    external_memory_callback_ = callback;
  }

  // Invoked when GC was requested via the stack guard.
  void HandleGCRequest();

  // ===========================================================================
  // Iterators. ================================================================
  // ===========================================================================

  // In the case of shared GC, kMainIsolate is used for the main isolate and
  // kClientIsolate for the (other) client isolates.
  enum class IterateRootsMode { kMainIsolate, kClientIsolate };

  // None of these methods iterate over the read-only roots. To do this use
  // ReadOnlyRoots::Iterate. Read-only root iteration is not necessary for
  // garbage collection and is usually only performed as part of
  // (de)serialization or heap verification.

  // Iterates over the strong roots and the weak roots.
  void IterateRoots(
      RootVisitor* v, base::EnumSet<SkipRoot> options,
      IterateRootsMode roots_mode = IterateRootsMode::kMainIsolate);
  void IterateRootsIncludingClients(RootVisitor* v,
                                    base::EnumSet<SkipRoot> options);

  // Iterates over entries in the smi roots list.  Only interesting to the
  // serializer/deserializer, since GC does not care about smis.
  void IterateSmiRoots(RootVisitor* v);
  // Iterates over weak string tables.
  void IterateWeakRoots(RootVisitor* v, base::EnumSet<SkipRoot> options);
  void IterateWeakGlobalHandles(RootVisitor* v);
  void IterateBuiltins(RootVisitor* v);

  void IterateStackRoots(RootVisitor* v);

  void IterateConservativeStackRoots(
      RootVisitor* v,
      IterateRootsMode roots_mode = IterateRootsMode::kMainIsolate);

  // ===========================================================================
  // Remembered set API. =======================================================
  // ===========================================================================

  // Used for query incremental marking status in generated code.
  uint8_t* IsMarkingFlagAddress();
  uint8_t* IsMinorMarkingFlagAddress();

  void ClearRecordedSlotRange(Address start, Address end);
  static int InsertIntoRememberedSetFromCode(MutablePageMetadata* chunk,
                                             size_t slot_offset);

#ifdef DEBUG
  void VerifySlotRangeHasNoRecordedSlots(Address start, Address end);
#endif

  // ===========================================================================
  // Incremental marking API. ==================================================
  // ===========================================================================

  GCFlags GCFlagsForIncrementalMarking() {
    return ShouldOptimizeForMemoryUsage() ? GCFlag::kReduceMemoryFootprint
                                          : GCFlag::kNoFlags;
  }

  // Starts incremental marking assuming incremental marking is currently
  // stopped.
  V8_EXPORT_PRIVATE void StartIncrementalMarking(
      GCFlags gc_flags, GarbageCollectionReason gc_reason,
      GCCallbackFlags gc_callback_flags = GCCallbackFlags::kNoGCCallbackFlags,
      GarbageCollector collector = GarbageCollector::MARK_COMPACTOR);

  V8_EXPORT_PRIVATE void StartIncrementalMarkingOnInterrupt();

  V8_EXPORT_PRIVATE void StartIncrementalMarkingIfAllocationLimitIsReached(
      LocalHeap* local_heap, GCFlags gc_flags,
      GCCallbackFlags gc_callback_flags = GCCallbackFlags::kNoGCCallbackFlags);

  // Synchronously finalizes incremental marking.
  V8_EXPORT_PRIVATE void FinalizeIncrementalMarkingAtomically(
      GarbageCollectionReason gc_reason);

  V8_EXPORT_PRIVATE void CompleteSweepingFull();
  void CompleteSweepingYoung();

  // Ensures that sweeping is finished for that object's page.
  void EnsureSweepingCompletedForObject(Tagged<HeapObject> object);

  IncrementalMarking* incremental_marking() const {
    return incremental_marking_.get();
  }

  // ===========================================================================
  // Concurrent marking API. ===================================================
  // ===========================================================================

  ConcurrentMarking* concurrent_marking() const {
    return concurrent_marking_.get();
  }

  // The runtime uses this function to notify potentially unsafe object layout
  // changes that require special synchronization with the concurrent marker.
  // By default recorded slots in the object are invalidated. Pass
  // InvalidateRecordedSlots::kNo if this is not necessary or to perform this
  // manually.
  // If the object contains external pointer slots, then these need to be
  // invalidated as well if a GC marker may have observed them previously. To
  // do this, pass HasExernalPointerSlots::kYes.
  void NotifyObjectLayoutChange(
      Tagged<HeapObject> object, const DisallowGarbageCollection&,
      InvalidateRecordedSlots invalidate_recorded_slots,
      InvalidateExternalPointerSlots invalidate_external_pointer_slots,
      int new_size = 0);
  V8_EXPORT_PRIVATE static void NotifyObjectLayoutChangeDone(
      Tagged<HeapObject> object);

  // The runtime uses this function to inform the GC of object size changes. The
  // GC will fill this area with a filler object and might clear recorded slots
  // in that area.
  void NotifyObjectSizeChange(Tagged<HeapObject>, int old_size, int new_size,
                              ClearRecordedSlots clear_recorded_slots);

  // ===========================================================================
  // Deoptimization support API. ===============================================
  // ===========================================================================

  // Setters for code offsets of well-known deoptimization targets.
  void SetConstructStubCreateDeoptPCOffset(int pc_offset);
  void SetConstructStubInvokeDeoptPCOffset(int pc_offset);
  void SetInterpreterEntryReturnPCOffset(int pc_offset);

  void DeoptMarkedAllocationSites();

  // ===========================================================================
  // Unified heap (C++) support. ===============================================
  // ===========================================================================

  V8_EXPORT_PRIVATE void AttachCppHeap(v8::CppHeap* cpp_heap);
  V8_EXPORT_PRIVATE void DetachCppHeap();

  v8::CppHeap* cpp_heap() const { return cpp_heap_; }

  std::optional<StackState> overridden_stack_state() const;

  // Set stack information from the stack of the current thread.
  V8_EXPORT_PRIVATE void SetStackStart();

  // Stack information of the main thread.
  V8_EXPORT_PRIVATE ::heap::base::Stack& stack();
  V8_EXPORT_PRIVATE const ::heap::base::Stack& stack() const;

  // ===========================================================================
  // Embedder roots optimizations. =============================================
  // ===========================================================================

  V8_EXPORT_PRIVATE
  void SetEmbedderRootsHandler(EmbedderRootsHandler* handler);

  EmbedderRootsHandler* GetEmbedderRootsHandler() const;

  // ===========================================================================
  // External string table API. ================================================
  // ===========================================================================

  // Registers an external string.
  inline void RegisterExternalString(Tagged<String> string);

  // Called when a string's resource is changed. The size of the payload is sent
  // as argument of the method.
  V8_EXPORT_PRIVATE void UpdateExternalString(Tagged<String> string,
                                              size_t old_payload,
                                              size_t new_payload);

  // Finalizes an external string by deleting the associated external
  // data and clearing the resource pointer.
  inline void FinalizeExternalString(Tagged<String> string);

  static Tagged<String> UpdateYoungReferenceInExternalStringTableEntry(
      Heap* heap, FullObjectSlot pointer);

  // ===========================================================================
  // Methods checking/returning the space of a given object/address. ===========
  // ===========================================================================

  // Returns whether the object resides in new space.
  static inline bool InYoungGeneration(Tagged<Object> object);
  static inline bool InYoungGeneration(Tagged<MaybeObject> object);
  static inline bool InYoungGeneration(Tagged<HeapObject> heap_object);
  static inline bool InFromPage(Tagged<Object> object);
  static inline bool InFromPage(Tagged<MaybeObject> object);
  static inline bool InFromPage(Tagged<HeapObject> heap_object);
  static inline bool InToPage(Tagged<Object> object);
  static inline bool InToPage(Tagged<MaybeObject> object);
  static inline bool InToPage(Tagged<HeapObject> heap_object);

  // Returns whether the object resides in old space.
  inline bool InOldSpace(Tagged<Object> object);

  // Checks whether an address/object is in the non-read-only heap (including
  // auxiliary area and unused area). Use IsValidHeapObject if checking both
  // heaps is required.
  V8_EXPORT_PRIVATE bool Contains(Tagged<HeapObject> value) const;
  // Same as above, but checks whether the object resides in any of the code
  // spaces.
  V8_EXPORT_PRIVATE bool ContainsCode(Tagged<HeapObject> value) const;

  // Checks whether an address/object is in the non-read-only heap (including
  // auxiliary area and unused area). Use IsValidHeapObject if checking both
  // heaps is required.
  V8_EXPORT_PRIVATE bool SharedHeapContains(Tagged<HeapObject> value) const;

  // Returns whether the object must be in the shared old space.
  V8_EXPORT_PRIVATE bool MustBeInSharedOldSpace(Tagged<HeapObject> value);

  // Checks whether an address/object in a space.
  // Currently used by tests, serialization and heap verification only.
  V8_EXPORT_PRIVATE bool InSpace(Tagged<HeapObject> value,
                                 AllocationSpace space) const;

  // Slow methods that can be used for verification as they can also be used
  // with off-heap Addresses.
  V8_EXPORT_PRIVATE bool InSpaceSlow(Address addr, AllocationSpace space) const;

  static inline Heap* FromWritableHeapObject(Tagged<HeapObject> obj);

  // ===========================================================================
  // Object statistics tracking. ===============================================
  // ===========================================================================

  // Returns the number of buckets used by object statistics tracking during a
  // major GC. Note that the following methods fail gracefully when the bounds
  // are exceeded though.
  size_t NumberOfTrackedHeapObjectTypes();

  // Returns object statistics about count and size at the last major GC.
  // Objects are being grouped into buckets that roughly resemble existing
  // instance types.
  size_t ObjectCountAtLastGC(size_t index);
  size_t ObjectSizeAtLastGC(size_t index);

  // Retrieves names of buckets used by object statistics tracking.
  bool GetObjectTypeName(size_t index, const char** object_type,
                         const char** object_sub_type);

  // The total number of native contexts object on the heap.
  size_t NumberOfNativeContexts();
  // The total number of native contexts that were detached but were not
  // garbage collected yet.
  size_t NumberOfDetachedContexts();

  // ===========================================================================
  // Code statistics.
  // ==========================================================
  // ===========================================================================

  // Collect code (Code and BytecodeArray objects) statistics.
  void CollectCodeStatistics();

  // ===========================================================================
  // GC statistics. ============================================================
  // ===========================================================================

  // Returns the maximum amount of memory reserved for the heap.
  V8_EXPORT_PRIVATE size_t MaxReserved() const;
  size_t MaxSemiSpaceSize() { return max_semi_space_size_; }
  size_t InitialSemiSpaceSize() { return initial_semispace_size_; }
  size_t MaxOldGenerationSize() { return max_old_generation_size(); }

  // Limit on the max old generation size imposed by the underlying allocator.
  V8_EXPORT_PRIVATE static size_t AllocatorLimitOnMaxOldGenerationSize();

  V8_EXPORT_PRIVATE static size_t HeapSizeFromPhysicalMemory(
      uint64_t physical_memory);
  V8_EXPORT_PRIVATE static void GenerationSizesFromHeapSize(
      size_t heap_size, size_t* young_generation_size,
      size_t* old_generation_size);
  V8_EXPORT_PRIVATE static size_t YoungGenerationSizeFromOldGenerationSize(
      size_t old_generation_size);
  V8_EXPORT_PRIVATE static size_t YoungGenerationSizeFromSemiSpaceSize(
      size_t semi_space_size);
  V8_EXPORT_PRIVATE static size_t SemiSpaceSizeFromYoungGenerationSize(
      size_t young_generation_size);
  V8_EXPORT_PRIVATE static size_t MinYoungGenerationSize();
  V8_EXPORT_PRIVATE static size_t MinOldGenerationSize();
  V8_EXPORT_PRIVATE static size_t MaxOldGenerationSize(
      uint64_t physical_memory);

  // Returns the capacity of the heap in bytes w/o growing. Heap grows when
  // more spaces are needed until it reaches the limit.
  size_t Capacity();

  // Returns the capacity of the old generation.
  V8_EXPORT_PRIVATE size_t OldGenerationCapacity() const;

  base::Mutex* heap_expansion_mutex() { return &heap_expansion_mutex_; }

  // Returns the amount of memory currently held alive by the pool.
  size_t CommittedMemoryOfPool();

  // Returns the amount of memory currently committed for the heap.
  size_t CommittedMemory();

  // Returns the amount of memory currently committed for the old space.
  size_t CommittedOldGenerationMemory();

  // Returns the amount of executable memory currently committed for the heap.
  size_t CommittedMemoryExecutable();

  // Returns the amount of physical memory currently committed for the heap.
  size_t CommittedPhysicalMemory();

  // Returns the maximum amount of memory ever committed for the heap.
  size_t MaximumCommittedMemory() { return maximum_committed_; }

  // Updates the maximum committed memory for the heap. Should be called
  // whenever a space grows.
  void UpdateMaximumCommitted();

  // Returns the available bytes in space w/o growing.
  // Heap doesn't guarantee that it can allocate an object that requires
  // all available bytes. Check MaxHeapObjectSize() instead.
  size_t Available();

  // Returns size of all objects residing in the heap.
  V8_EXPORT_PRIVATE size_t SizeOfObjects();

  // Returns size of all global handles in the heap.
  V8_EXPORT_PRIVATE size_t TotalGlobalHandlesSize();

  // Returns size of all allocated/used global handles in the heap.
  V8_EXPORT_PRIVATE size_t UsedGlobalHandlesSize();

  void UpdateSurvivalStatistics(int start_new_space_size);

  inline void IncrementPromotedObjectsSize(size_t object_size) {
    promoted_objects_size_ += object_size;
  }
  inline size_t promoted_objects_size() { return promoted_objects_size_; }

  inline void IncrementNewSpaceSurvivingObjectSize(size_t object_size) {
    new_space_surviving_object_size_ += object_size;
  }
  inline size_t new_space_surviving_object_size() {
    return new_space_surviving_object_size_;
  }

  inline size_t SurvivedYoungObjectSize() {
    return promoted_objects_size_ + new_space_surviving_object_size_;
  }

  inline void IncrementNodesDiedInNewSpace(int count) {
    nodes_died_in_new_space_ += count;
  }

  inline void IncrementNodesCopiedInNewSpace() { nodes_copied_in_new_space_++; }

  inline void IncrementNodesPromoted() { nodes_promoted_++; }

  inline void IncrementYoungSurvivorsCounter(size_t survived) {
    survived_since_last_expansion_ += survived;
  }

  void UpdateNewSpaceAllocationCounter();

  V8_EXPORT_PRIVATE size_t NewSpaceAllocationCounter();

  // This should be used only for testing.
  void set_new_space_allocation_counter(size_t new_value) {
    new_space_allocation_counter_ = new_value;
  }

  void UpdateOldGenerationAllocationCounter() {
    old_generation_allocation_counter_at_last_gc_ =
        OldGenerationAllocationCounter();
    old_generation_size_at_last_gc_ = 0;
    old_generation_wasted_at_last_gc_ = 0;
  }

  size_t OldGenerationAllocationCounter() {
    return old_generation_allocation_counter_at_last_gc_ +
           PromotedSinceLastGC();
  }

  size_t EmbedderAllocationCounter() const;

  // This should be used only for testing.
  void set_old_generation_allocation_counter_at_last_gc(size_t new_value) {
    old_generation_allocation_counter_at_last_gc_ = new_value;
  }

  int gc_count() const { return gc_count_; }

  bool is_current_gc_forced() const { return is_current_gc_forced_; }

  GarbageCollector current_or_last_garbage_collector() const {
    return current_or_last_garbage_collector_;
  }

  // Returns whether the currently in-progress GC should avoid increasing the
  // ages on any objects that live for a set number of collections.
  bool ShouldCurrentGCKeepAgesUnchanged() const {
    return is_current_gc_forced_ || is_current_gc_for_heap_profiler_;
  }

  // Returns the size of objects residing in non-new spaces.
  // Excludes external memory held by those objects.
  V8_EXPORT_PRIVATE size_t OldGenerationSizeOfObjects() const;

  // Returns the amount of wasted bytes in non-new spaces.
  V8_EXPORT_PRIVATE size_t OldGenerationWastedBytes() const;

  // Returns the amount of bytes in non-new spaces not availalbe for allocation,
  // including bytes allocated and wasted.
  V8_EXPORT_PRIVATE size_t OldGenerationConsumedBytes() const;

  // Returns the size of objects residing in new spaces.
  // Excludes external memory held by those objects.
  V8_EXPORT_PRIVATE size_t YoungGenerationSizeOfObjects() const;

  // Returns the amount of wasted bytes in new spaces.
  V8_EXPORT_PRIVATE size_t YoungGenerationWastedBytes() const;

  // Returns the amount of bytes in new space not availalbe for allocation,
  // including bytes allocated and wasted.
  V8_EXPORT_PRIVATE size_t YoungGenerationConsumedBytes() const;

  // Returns the size of objects held by the EmbedderHeapTracer.
  V8_EXPORT_PRIVATE size_t EmbedderSizeOfObjects() const;

  // Returns the global size of objects (embedder + V8 non-new spaces).
  V8_EXPORT_PRIVATE size_t GlobalSizeOfObjects() const;

  // Returns the global amount of wasted bytes.
  V8_EXPORT_PRIVATE size_t GlobalWastedBytes() const;

  // Returns the global amount of bytes not availalbe for allocation, including
  // bytes allocated and wasted.
  V8_EXPORT_PRIVATE size_t GlobalConsumedBytes() const;

  // We allow incremental marking to overshoot the V8 and global allocation
  // limit for performance reasons. If the overshoot is too large then we are
  // more eager to finalize incremental marking.
  bool AllocationLimitOvershotByLargeMargin() const;

  // Return the maximum size objects can be before having to allocate them as
  // large objects. This takes into account allocating in the code space for
  // which the size of the allocatable space per V8 page may depend on the OS
  // page size at runtime. You may use kMaxRegularHeapObjectSize as a constant
  // instead if you know the allocation isn't in the code spaces.
  inline V8_EXPORT_PRIVATE int MaxRegularHeapObjectSize(
      AllocationType allocation);

  // ===========================================================================
  // Prologue/epilogue callback methods.========================================
  // ===========================================================================

  void AddGCPrologueCallback(v8::Isolate::GCCallbackWithData callback,
                             GCType gc_type_filter, void* data);
  void RemoveGCPrologueCallback(v8::Isolate::GCCallbackWithData callback,
                                void* data);

  void AddGCEpilogueCallback(v8::Isolate::GCCallbackWithData callback,
                             GCType gc_type_filter, void* data);
  void RemoveGCEpilogueCallback(v8::Isolate::GCCallbackWithData callback,
                                void* data);

  void CallGCPrologueCallbacks(GCType gc_type, GCCallbackFlags flags,
                               GCTracer::Scope::ScopeId scope_id);
  void CallGCEpilogueCallbacks(GCType gc_type, GCCallbackFlags flags,
                               GCTracer::Scope::ScopeId scope_id);

  // ===========================================================================
  // Allocation methods. =======================================================
  // ===========================================================================

  // Creates a filler object and returns a heap object immediately after it.
  V8_EXPORT_PRIVATE Tagged<HeapObject> PrecedeWithFiller(
      Tagged<HeapObject> object, int filler_size);

  // Creates a filler object and returns a heap object immediately after it.
  // Unlike `PrecedeWithFiller` this method will not perform slot verification
  // since this would race on background threads.
  V8_EXPORT_PRIVATE Tagged<HeapObject> PrecedeWithFillerBackground(
      Tagged<HeapObject> object, int filler_size);

  // Creates a filler object if needed for alignment and returns a heap object
  // immediately after it. If any space is left after the returned object,
  // another filler object is created so the over allocated memory is iterable.
  V8_WARN_UNUSED_RESULT Tagged<HeapObject> AlignWithFillerBackground(
      Tagged<HeapObject> object, int object_size, int allocation_size,
      AllocationAlignment alignment);

  // Allocate an external backing store with the given allocation callback.
  // If the callback fails (indicated by a nullptr result) then this function
  // will re-try the allocation after performing GCs. This is useful for
  // external backing stores that may be retained by (unreachable) V8 objects
  // such as ArrayBuffers, ExternalStrings, etc.
  //
  // The function may also proactively trigger GCs even if the allocation
  // callback does not fail to keep the memory usage low.
  V8_EXPORT_PRIVATE void* AllocateExternalBackingStore(
      const std::function<void*(size_t)>& allocate, size_t byte_length);

  // ===========================================================================
  // Allocation tracking. ======================================================
  // ===========================================================================

  // Adds {new_space_observer} to new space and {observer} to any other space.
  void AddAllocationObserversToAllSpaces(
      AllocationObserver* observer, AllocationObserver* new_space_observer);

  // Removes {new_space_observer} from new space and {observer} from any other
  // space.
  void RemoveAllocationObserversFromAllSpaces(
      AllocationObserver* observer, AllocationObserver* new_space_observer);

  // Check if the given object was recently allocated and its fields may appear
  // as uninitialized to background threads.
  // This predicate may be invoked from a background thread.
  inline bool IsPendingAllocation(Tagged<HeapObject> object);
  inline bool IsPendingAllocation(Tagged<Object> object);

  // Notifies that all previously allocated objects are properly initialized
  // and ensures that IsPendingAllocation returns false for them. This function
  // may be invoked only on the main thread.
  V8_EXPORT_PRIVATE void PublishMainThreadPendingAllocations();

  // ===========================================================================
  // Heap object allocation tracking. ==========================================
  // ===========================================================================

  V8_EXPORT_PRIVATE void AddHeapObjectAllocationTracker(
      HeapObjectAllocationTracker* tracker);
  V8_EXPORT_PRIVATE void RemoveHeapObjectAllocationTracker(
      HeapObjectAllocationTracker* tracker);
  bool has_heap_object_allocation_tracker() const {
    return !allocation_trackers_.empty();
  }

  // ===========================================================================
  // Stack frame support. ======================================================
  // ===========================================================================

  // Searches for a Code object by the given interior pointer.
  V8_EXPORT_PRIVATE Tagged<Code> FindCodeForInnerPointer(Address inner_pointer);
  // Use the GcSafe family of functions if called while GC is in progress.
  Tagged<GcSafeCode> GcSafeFindCodeForInnerPointer(Address inner_pointer);
  std::optional<Tagged<GcSafeCode>> GcSafeTryFindCodeForInnerPointer(
      Address inner_pointer);
  std::optional<Tagged<InstructionStream>>
  GcSafeTryFindInstructionStreamForInnerPointer(Address inner_pointer);
  // Only intended for use from the `jco` gdb macro.
  std::optional<Tagged<Code>> TryFindCodeForInnerPointerForPrinting(
      Address inner_pointer);

  // Returns true if {addr} is contained within {instruction_stream} and false
  // otherwise. Mostly useful for debugging.
  bool GcSafeInstructionStreamContains(
      Tagged<InstructionStream> instruction_stream, Address addr);

  // ===========================================================================
  // Sweeping. =================================================================
  // ===========================================================================

  bool sweeping_in_progress() const { return sweeper_->sweeping_in_progress(); }
  bool sweeping_in_progress_for_space(AllocationSpace space) const {
    return sweeper_->sweeping_in_progress_for_space(space);
  }
  bool minor_sweeping_in_progress() const {
    return sweeper_->minor_sweeping_in_progress();
  }
  bool major_sweeping_in_progress() const {
    return sweeper_->major_sweeping_in_progress();
  }

  void FinishSweepingIfOutOfWork();

  enum class SweepingForcedFinalizationMode { kUnifiedHeap, kV8Only };

  // Ensures that sweeping is finished.
  //
  // Note: Can only be called safely from main thread.
  V8_EXPORT_PRIVATE void EnsureSweepingCompleted(
      SweepingForcedFinalizationMode mode);
  void EnsureYoungSweepingCompleted();

  // =============================================================================

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  void V8_EXPORT_PRIVATE set_allocation_timeout(int allocation_timeout);
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

#ifdef DEBUG
  void VerifyCountersAfterSweeping();
  void VerifyCountersBeforeConcurrentSweeping(GarbageCollector collector);
  void VerifyCommittedPhysicalMemory();

  void Print();
  void PrintHandles();

  // Report code statistics.
  void ReportCodeStatistics(const char* title);
#endif  // DEBUG
  void* GetRandomMmapAddr() {
    void* result = v8::internal::GetRandomMmapAddr();
#if V8_TARGET_ARCH_X64
#if V8_OS_DARWIN
    // The Darwin kernel [as of macOS 10.12.5] does not clean up page
    // directory entries [PDE] created from mmap or mach_vm_allocate, even
    // after the region is destroyed. Using a virtual address space that is
    // too large causes a leak of about 1 wired [can never be paged out] page
    // per call to mmap(). The page is only reclaimed when the process is
    // killed. Confine the hint to a 32-bit section of the virtual address
    // space. See crbug.com/700928.
    uintptr_t offset = reinterpret_cast<uintptr_t>(result) & kMmapRegionMask;
    result = reinterpret_cast<void*>(mmap_region_base_ + offset);
#endif  // V8_OS_DARWIN
#endif  // V8_TARGET_ARCH_X64
    return result;
  }

  // Calculates the nof entries for the full sized number to string cache.
  inline int MaxNumberToStringCacheSize() const;

  static Isolate* GetIsolateFromWritableObject(Tagged<HeapObject> object);

  // Ensure that we have swept all spaces in such a way that we can iterate
  // over all objects.
  V8_EXPORT_PRIVATE void MakeHeapIterable();

  V8_EXPORT_PRIVATE void Unmark();
  V8_EXPORT_PRIVATE void DeactivateMajorGCInProgressFlag();

  // Free all LABs in the heap.
  V8_EXPORT_PRIVATE void FreeLinearAllocationAreas();

  // Frees all LABs owned by the main thread.
  V8_EXPORT_PRIVATE void FreeMainThreadLinearAllocationAreas();

  V8_EXPORT_PRIVATE bool CanPromoteYoungAndExpandOldGeneration(
      size_t size) const;
  V8_EXPORT_PRIVATE bool CanExpandOldGeneration(size_t size) const;

  // Checks whether OldGenerationCapacity() can be expanded by `size` bytes and
  // still fits into `max_old_generation_size_`.
  V8_EXPORT_PRIVATE bool IsOldGenerationExpansionAllowed(
      size_t size, const base::MutexGuard& expansion_mutex_witness) const;

  bool ShouldReduceMemory() const {
    return current_gc_flags_ & GCFlag::kReduceMemoryFootprint;
  }

  MarkingState* marking_state() { return &marking_state_; }

  NonAtomicMarkingState* non_atomic_marking_state() {
    return &non_atomic_marking_state_;
  }

  PretenuringHandler* pretenuring_handler() { return &pretenuring_handler_; }

  bool IsInlineAllocationEnabled() const { return inline_allocation_enabled_; }

  // Returns the amount of external memory registered since last global gc.
  V8_EXPORT_PRIVATE uint64_t AllocatedExternalMemorySinceMarkCompact() const;

  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner() const;

  bool ShouldUseBackgroundThreads() const;
  bool ShouldUseIncrementalMarking() const;

  HeapAllocator* allocator() { return heap_allocator_; }

  bool use_new_space() const {
    DCHECK_IMPLIES(new_space(), !v8_flags.sticky_mark_bits);
    return new_space() || v8_flags.sticky_mark_bits;
  }

 private:
  class AllocationTrackerForDebugging;

  using ExternalStringTableUpdaterCallback =
      Tagged<String> (*)(Heap* heap, FullObjectSlot pointer);

  // External strings table is a place where all external strings are
  // registered.  We need to keep track of such strings to properly
  // finalize them.
  class ExternalStringTable {
   public:
    explicit ExternalStringTable(Heap* heap) : heap_(heap) {}
    ExternalStringTable(const ExternalStringTable&) = delete;
    ExternalStringTable& operator=(const ExternalStringTable&) = delete;

    // Registers an external string.
    inline void AddString(Tagged<String> string);
    bool Contains(Tagged<String> string);

    void IterateAll(RootVisitor* v);
    void IterateYoung(RootVisitor* v);
    void PromoteYoung();

    // Restores internal invariant and gets rid of collected strings. Must be
    // called after each Iterate*() that modified the strings.
    void CleanUpAll();
    void CleanUpYoung();

    // Finalize all registered external strings and clear tables.
    void TearDown();

    void UpdateYoungReferences(
        Heap::ExternalStringTableUpdaterCallback updater_func);
    void UpdateReferences(
        Heap::ExternalStringTableUpdaterCallback updater_func);

    bool HasYoung() const { return !young_strings_.empty(); }

   private:
    void Verify();
    void VerifyYoung();

    Heap* const heap_;

    // To speed up scavenge collections young string are kept separate from old
    // strings.
    std::vector<TaggedBase> young_strings_;
    std::vector<TaggedBase> old_strings_;
    // Used to protect access with --shared-string-table.
    base::Mutex mutex_;
  };

  static const int kInitialEvalCacheSize = 64;
  static const int kInitialNumberStringCacheSize = 256;

  static const int kRememberedUnmappedPages = 128;

  static const int kYoungSurvivalRateHighThreshold = 90;
  static const int kYoungSurvivalRateAllowedDeviation = 15;
  static const int kOldSurvivalRateLowThreshold = 10;

  static const int kMaxMarkCompactsInIdleRound = 7;

  Heap();
  ~Heap();

  Heap(const Heap&) = delete;
  Heap& operator=(const Heap&) = delete;

  static bool IsRegularObjectAllocation(AllocationType allocation) {
    return AllocationType::kYoung == allocation ||
           AllocationType::kOld == allocation;
  }

#define ROOT_ACCESSOR(type, name, CamelName) \
  inline void set_##name(Tagged<type> value);
  ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  int NumberOfScavengeTasks();

  // Checks whether a global GC is necessary
  GarbageCollector SelectGarbageCollector(AllocationSpace space,
                                          GarbageCollectionReason gc_reason,
                                          const char** reason) const;

  // Make all LABs of all threads iterable.
  void MakeLinearAllocationAreasIterable();

  // Enables/Disables black allocation in shared LABs.
  void MarkSharedLinearAllocationAreasBlack();
  void UnmarkSharedLinearAllocationAreas();

  // Performs garbage collection in a safepoint.
  void PerformGarbageCollection(GarbageCollector collector,
                                GarbageCollectionReason gc_reason,
                                const char* collector_reason);

  void PerformHeapVerification();
  std::vector<Isolate*> PauseConcurrentThreadsInClients(
      GarbageCollector collector);
  void ResumeConcurrentThreadsInClients(std::vector<Isolate*> paused_clients);

  // For static-roots builds, pads the object to the required size.
  void StaticRootsEnsureAllocatedSize(DirectHandle<HeapObject> obj,
                                      int required);
  bool CreateEarlyReadOnlyMapsAndObjects();
  bool CreateImportantReadOnlyObjects();
  bool CreateLateReadOnlyNonJSReceiverMaps();
  bool CreateLateReadOnlyJSReceiverMaps();
  bool CreateReadOnlyObjects();

  void CreateInternalAccessorInfoObjects();
  void CreateInitialMutableObjects();

  enum class VerifyNoSlotsRecorded { kYes, kNo };

  // Creates a filler object in the specified memory area. This method is the
  // internal method used by all CreateFillerObjectAtXXX-methods.
  void CreateFillerObjectAtRaw(const WritableFreeSpace& free_space,
                               ClearFreedMemoryMode clear_memory_mode,
                               ClearRecordedSlots clear_slots_mode,
                               VerifyNoSlotsRecorded verify_no_slots_recorded);

  // Range write barrier implementation.
  template <int kModeMask, typename TSlot>
  V8_INLINE void WriteBarrierForRangeImpl(MemoryChunk* source_page,
                                          Tagged<HeapObject> object,
                                          TSlot start_slot, TSlot end_slot);

  // Deopts all code that contains allocation instruction which are tenured or
  // not tenured. Moreover it clears the pretenuring allocation site statistics.
  void ResetAllAllocationSitesDependentCode(AllocationType allocation);

  // Evaluates local pretenuring for the old space and calls
  // ResetAllTenuredAllocationSitesDependentCode if too many objects died in
  // the old space.
  void EvaluateOldSpaceLocalPretenuring(uint64_t size_of_objects_before_gc);

  // Record statistics after garbage collection.
  void ReportStatisticsAfterGC();

  // Flush the number to string cache.
  void FlushNumberStringCache();

  void ActivateMemoryReducerIfNeededOnMainThread();

  void ShrinkOldGenerationAllocationLimitIfNotConfigured();

  double ComputeMutatorUtilization(const char* tag, double mutator_speed,
                                   double gc_speed);
  bool HasLowYoungGenerationAllocationRate();
  bool HasLowOldGenerationAllocationRate();
  bool HasLowEmbedderAllocationRate();

  enum class ResizeNewSpaceMode { kShrink, kGrow, kNone };
  ResizeNewSpaceMode ShouldResizeNewSpace();
  void ExpandNewSpaceSize();
  void ReduceNewSpaceSize();

  void PrintMaxMarkingLimitReached();
  void PrintMaxNewSpaceSizeReached();

  int NextStressMarkingLimit();

  void AddToRingBuffer(const char* string);
  void GetFromRingBuffer(char* buffer);

  void CompactRetainedMaps(Tagged<WeakArrayList> retained_maps);

  void CollectGarbageOnMemoryPressure();

  void EagerlyFreeExternalMemoryAndWasmCode();

  bool InvokeNearHeapLimitCallback();

  void InvokeIncrementalMarkingPrologueCallbacks();
  void InvokeIncrementalMarkingEpilogueCallbacks();

  // Casts a heap object to an InstructionStream, DCHECKs that the
  // inner_pointer is within the object, and returns the attached Code object.
  Tagged<GcSafeCode> GcSafeGetCodeFromInstructionStream(
      Tagged<HeapObject> instruction_stream, Address inner_pointer);
  // Returns the map of a HeapObject. Can be used during garbage collection,
  // i.e. it supports a forwarded map.
  Tagged<Map> GcSafeMapOfHeapObject(Tagged<HeapObject> object);

  // ===========================================================================
  // Actual GC. ================================================================
  // ===========================================================================

  // Code that should be run before and after each GC.  Includes
  // some reporting/verification activities when compiled with DEBUG set.
  void GarbageCollectionPrologue(GarbageCollectionReason gc_reason,
                                 const v8::GCCallbackFlags gc_callback_flags);
  void GarbageCollectionPrologueInSafepoint();
  void GarbageCollectionEpilogue(GarbageCollector collector);
  void GarbageCollectionEpilogueInSafepoint(GarbageCollector collector);

  // Performs a major collection in the whole heap.
  void MarkCompact();
  // Performs a minor collection of just the young generation.
  void MinorMarkSweep();

  // Code to be run before and after mark-compact.
  void MarkCompactPrologue();
  void MarkCompactEpilogue();

  // Performs a minor collection in new generation.
  void Scavenge();

  void UpdateYoungReferencesInExternalStringTable(
      ExternalStringTableUpdaterCallback updater_func);

  void UpdateReferencesInExternalStringTable(
      ExternalStringTableUpdaterCallback updater_func);

  void ProcessAllWeakReferences(WeakObjectRetainer* retainer);
  void ProcessNativeContexts(WeakObjectRetainer* retainer);
  void ProcessAllocationSites(WeakObjectRetainer* retainer);
  void ProcessDirtyJSFinalizationRegistries(WeakObjectRetainer* retainer);
  void ProcessWeakListRoots(WeakObjectRetainer* retainer);

  // ===========================================================================
  // GC statistics. ============================================================
  // ===========================================================================

  inline size_t OldGenerationSpaceAvailable() {
    uint64_t bytes = OldGenerationConsumedBytes() +
                     AllocatedExternalMemorySinceMarkCompact();

    if (old_generation_allocation_limit() <= bytes) return 0;
    return old_generation_allocation_limit() - static_cast<size_t>(bytes);
  }

  void UpdateTotalGCTime(base::TimeDelta duration);

  bool IsIneffectiveMarkCompact(size_t old_generation_size,
                                double mutator_utilization);
  void CheckIneffectiveMarkCompact(size_t old_generation_size,
                                   double mutator_utilization);
  void ReportIneffectiveMarkCompactIfNeeded();

  inline void IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  inline void DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  // ===========================================================================
  // Growing strategy. =========================================================
  // ===========================================================================

  MemoryReducer* memory_reducer() { return memory_reducer_.get(); }

  // For some webpages RAIL mode does not switch from PERFORMANCE_LOAD.
  // This constant limits the effect of load RAIL mode on GC.
  // The value is arbitrary and chosen as the largest load time observed in
  // v8 browsing benchmarks.
  static const int kMaxLoadTimeMs = 7000;

  V8_EXPORT_PRIVATE bool ShouldOptimizeForLoadTime() const;
  void NotifyLoadingStarted();
  void NotifyLoadingEnded();
  void UpdateLoadStartTime();

  size_t old_generation_allocation_limit() const {
    return old_generation_allocation_limit_.load(std::memory_order_relaxed);
  }

  size_t global_allocation_limit() const {
    return global_allocation_limit_.load(std::memory_order_relaxed);
  }

  bool old_generation_allocation_limit_configured() const {
    return old_generation_allocation_limit_configured_.load(
        std::memory_order_relaxed);
  }

  void set_old_generation_allocation_limit_configured(bool value) {
    old_generation_allocation_limit_configured_.store(
        value, std::memory_order_relaxed);
  }

  size_t max_old_generation_size() const {
    return max_old_generation_size_.load(std::memory_order_relaxed);
  }

  size_t min_old_generation_size() const { return min_old_generation_size_; }

  // Sets max_old_generation_size_ and computes the new global heap limit from
  // it.
  void SetOldGenerationAndGlobalMaximumSize(size_t max_old_generation_size);

  // Sets allocation limits for both old generation and the global heap.
  void SetOldGenerationAndGlobalAllocationLimit(
      size_t new_old_generation_allocation_limit,
      size_t new_global_allocation_limit);

  void ResetOldGenerationAndGlobalAllocationLimit();

  bool always_allocate() { return always_allocate_scope_count_ != 0; }

  bool ShouldExpandOldGenerationOnSlowAllocation(LocalHeap* local_heap,
                                                 AllocationOrigin origin);
  bool ShouldExpandYoungGenerationOnSlowAllocation();

  HeapGrowingMode CurrentHeapGrowingMode();

  double PercentToOldGenerationLimit() const;
  double PercentToGlobalMemoryLimit() const;
  enum class IncrementalMarkingLimit {
    kNoLimit,
    kSoftLimit,
    kHardLimit,
    kFallbackForEmbedderLimit
  };
  IncrementalMarkingLimit IncrementalMarkingLimitReached();

  bool ShouldStressCompaction() const;

  size_t GlobalMemoryAvailable();

  void RecomputeLimits(GarbageCollector collector, base::TimeTicks time);
  void RecomputeLimitsAfterLoadingIfNeeded();
  struct LimitsCompuatationResult {
    size_t old_generation_allocation_limit;
    size_t global_allocation_limit;
  };
  static LimitsCompuatationResult ComputeNewAllocationLimits(Heap* heap);

  // ===========================================================================
  // GC Tasks. =================================================================
  // ===========================================================================

  void ScheduleMinorGCTaskIfNeeded();
  V8_EXPORT_PRIVATE void StartMinorMSIncrementalMarkingIfNeeded();
  bool MinorMSSizeTaskTriggerReached() const;

  MinorGCJob* minor_gc_job() { return minor_gc_job_.get(); }

  // ===========================================================================
  // Allocation methods. =======================================================
  // ===========================================================================

  // Allocates a JS Map in the heap.
  V8_WARN_UNUSED_RESULT AllocationResult
  AllocateMap(AllocationType allocation_type, InstanceType instance_type,
              int instance_size,
              ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
              int inobject_properties = 0);

  // Allocate an uninitialized object.  The memory is non-executable if the
  // hardware and OS allow.  This is the single choke-point for allocations
  // performed by the runtime and should not be bypassed (to extend this to
  // inlined allocations, use the Heap::DisableInlineAllocation() support).
  V8_WARN_UNUSED_RESULT V8_INLINE AllocationResult
  AllocateRaw(int size_in_bytes, AllocationType allocation,
              AllocationOrigin origin = AllocationOrigin::kRuntime,
              AllocationAlignment alignment = kTaggedAligned);

  // This method will try to allocate objects quickly (AllocationType::kYoung)
  // otherwise it falls back to a slower path indicated by the mode.
  enum AllocationRetryMode { kLightRetry, kRetryOrFail };
  template <AllocationRetryMode mode>
  V8_WARN_UNUSED_RESULT V8_INLINE Tagged<HeapObject> AllocateRawWith(
      int size, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime,
      AllocationAlignment alignment = kTaggedAligned);

  // Call AllocateRawWith with kRetryOrFail. Matches the method in LocalHeap.
  V8_WARN_UNUSED_RESULT inline Address AllocateRawOrFail(
      int size, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime,
      AllocationAlignment alignment = kTaggedAligned);

  // Allocates a heap object based on the map.
  V8_WARN_UNUSED_RESULT AllocationResult Allocate(DirectHandle<Map> map,
                                                  AllocationType allocation);

  // Allocates a partial map for bootstrapping.
  V8_WARN_UNUSED_RESULT AllocationResult
  AllocatePartialMap(InstanceType instance_type, int instance_size);

  void FinalizePartialMap(Tagged<Map> map);

  void set_force_oom(bool value) { force_oom_ = value; }
  void set_force_gc_on_next_allocation() {
    force_gc_on_next_allocation_ = true;
  }

  // Helper for IsPendingAllocation.
  inline bool IsPendingAllocationInternal(Tagged<HeapObject> object);

#ifdef DEBUG
  V8_EXPORT_PRIVATE void IncrementObjectCounters();
#endif  // DEBUG

  std::vector<Handle<NativeContext>> FindAllNativeContexts();
  std::vector<Tagged<WeakArrayList>> FindAllRetainedMaps();
  MemoryMeasurement* memory_measurement() { return memory_measurement_.get(); }

  AllocationType allocation_type_for_in_place_internalizable_strings() const {
    return allocation_type_for_in_place_internalizable_strings_;
  }

  bool IsStressingScavenge();

  void SetIsMarkingFlag(bool value);
  void SetIsMinorMarkingFlag(bool value);

  size_t PromotedSinceLastGC() {
    size_t old_generation_size = OldGenerationSizeOfObjects();
    return old_generation_size > old_generation_size_at_last_gc_
               ? old_generation_size - old_generation_size_at_last_gc_
               : 0;
  }

  ExternalMemoryAccounting external_memory_;

  // This can be calculated directly from a pointer to the heap; however, it is
  // more expedient to get at the isolate directly from within Heap methods.
  Isolate* isolate_ = nullptr;

  HeapAllocator* heap_allocator_ = nullptr;

  // These limits are initialized in Heap::ConfigureHeap based on the resource
  // constraints and flags.
  size_t code_range_size_ = 0;
  size_t max_semi_space_size_ = 0;
  size_t initial_semispace_size_ = 0;
  // Full garbage collections can be skipped if the old generation size
  // is below this threshold.
  size_t min_old_generation_size_ = 0;
  // If the old generation size exceeds this limit, then V8 will
  // crash with out-of-memory error.
  std::atomic<size_t> max_old_generation_size_{0};
  // TODO(mlippautz): Clarify whether this should take some embedder
  // configurable limit into account.
  size_t min_global_memory_size_ = 0;
  size_t max_global_memory_size_ = 0;

  size_t initial_max_old_generation_size_ = 0;
  size_t initial_max_old_generation_size_threshold_ = 0;
  size_t initial_old_generation_size_ = 0;

  // Before the first full GC the old generation allocation limit is considered
  // to be *not* configured (unless initial limits were provided by the
  // embedder). In this mode V8 starts with a very large old generation
  // allocation limit initially. Minor GCs may then shrink this initial limit
  // down until the first full GC computes a proper old generation allocation
  // limit in Heap::RecomputeLimits. The old generation allocation limit is then
  // considered to be configured for all subsequent GCs. After the first full GC
  // this field is only ever reset for top context disposals.
  std::atomic<bool> old_generation_allocation_limit_configured_ = false;

  size_t maximum_committed_ = 0;
  size_t old_generation_capacity_after_bootstrap_ = 0;

  // Backing store bytes (array buffers and external strings).
  // Use uint64_t counter since the counter could overflow the 32-bit range
  // temporarily on 32-bit.
  std::atomic<uint64_t> backing_store_bytes_{0};

  // For keeping track of how much data has survived
  // scavenge since last new space expansion.
  size_t survived_since_last_expansion_ = 0;

  // This is not the depth of nested AlwaysAllocateScope's but rather a single
  // count, as scopes can be acquired from multiple tasks (read: threads).
  std::atomic<size_t> always_allocate_scope_count_{0};

  // Stores the memory pressure level that set by MemoryPressureNotification
  // and reset by a mark-compact garbage collection.
  std::atomic<v8::MemoryPressureLevel> memory_pressure_level_;

  std::vector<std::pair<v8::NearHeapLimitCallback, void*>>
      near_heap_limit_callbacks_;

  // For keeping track of context disposals.
  int contexts_disposed_ = 0;

  // Spaces owned by this heap through space_.
  NewSpace* new_space_ = nullptr;
  OldSpace* old_space_ = nullptr;
  CodeSpace* code_space_ = nullptr;
  SharedSpace* shared_space_ = nullptr;
  OldLargeObjectSpace* lo_space_ = nullptr;
  CodeLargeObjectSpace* code_lo_space_ = nullptr;
  NewLargeObjectSpace* new_lo_space_ = nullptr;
  SharedLargeObjectSpace* shared_lo_space_ = nullptr;
  ReadOnlySpace* read_only_space_ = nullptr;
  TrustedSpace* trusted_space_ = nullptr;
  SharedTrustedSpace* shared_trusted_space_ = nullptr;
  TrustedLargeObjectSpace* trusted_lo_space_ = nullptr;
  SharedTrustedLargeObjectSpace* shared_trusted_lo_space_ = nullptr;

  // Either pointer to owned shared spaces or pointer to unowned shared spaces
  // in another isolate.
  PagedSpace* shared_allocation_space_ = nullptr;
  OldLargeObjectSpace* shared_lo_allocation_space_ = nullptr;
  SharedTrustedSpace* shared_trusted_allocation_space_ = nullptr;
  SharedTrustedLargeObjectSpace* shared_trusted_lo_allocation_space_ = nullptr;

  // Map from the space id to the space.
  std::unique_ptr<Space> space_[LAST_SPACE + 1];

#ifdef V8_COMPRESS_POINTERS
  // The spaces in the ExternalPointerTable containing entries owned by objects
  // in this heap.
  ExternalPointerTable::Space young_external_pointer_space_;
  ExternalPointerTable::Space old_external_pointer_space_;
  // Likewise but for slots in host objects in ReadOnlySpace.
  ExternalPointerTable::Space read_only_external_pointer_space_;
  // Space in the ExternalPointerTable containing entries owned by objects in
  // this heap. The entries exclusively point to CppHeap objects.
  CppHeapPointerTable::Space cpp_heap_pointer_space_;
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  // Likewise, but for the trusted pointer table.
  TrustedPointerTable::Space trusted_pointer_space_;

  // The space in the process-wide code pointer table managed by this heap.
  CodePointerTable::Space code_pointer_space_;

  // The space in the process-wide JSDispatchTable managed by this heap.
  JSDispatchTable::Space js_dispatch_table_space_;
#endif  // V8_ENABLE_SANDBOX

  LocalHeap* main_thread_local_heap_ = nullptr;

  std::atomic<HeapState> gc_state_{NOT_IN_GC};

  // Starts marking when stress_marking_percentage_% of the marking start limit
  // is reached.
  int stress_marking_percentage_ = 0;

  // Observer that can cause early scavenge start.
  StressScavengeObserver* stress_scavenge_observer_ = nullptr;

  // The maximum percent of the marking limit reached without causing marking.
  // This is tracked when specifying --fuzzer-gc-analysis.
  std::atomic<double> max_marking_limit_reached_ = 0.0;

  // How many mark-sweep collections happened.
  unsigned int ms_count_ = 0;

  // How many gc happened.
  unsigned int gc_count_ = 0;

  // The number of Mark-Compact garbage collections that are considered as
  // ineffective. See IsIneffectiveMarkCompact() predicate.
  int consecutive_ineffective_mark_compacts_ = 0;

  static const uintptr_t kMmapRegionMask = 0xFFFFFFFFu;
  uintptr_t mmap_region_base_ = 0;

  // For post mortem debugging.
  int remembered_unmapped_pages_index_ = 0;
  Address remembered_unmapped_pages_[kRememberedUnmappedPages];

  // Limit that triggers a global GC on the next (normally caused) GC.  This
  // is checked when we have already decided to do a GC to help determine
  // which collector to invoke, before expanding a paged space in the old
  // generation and on every allocation in large object space.
  std::atomic<size_t> old_generation_allocation_limit_{0};
  std::atomic<size_t> global_allocation_limit_{0};

  // Weak list heads, threaded through the objects.
  // List heads are initialized lazily and contain the undefined_value at start.
  // {native_contexts_list_} is an Address instead of an Object to allow the use
  // of atomic accessors.
  std::atomic<Address> native_contexts_list_;
  Tagged<Object> allocation_sites_list_ = Smi::zero();
  Tagged<Object> dirty_js_finalization_registries_list_ = Smi::zero();
  // Weak list tails.
  Tagged<Object> dirty_js_finalization_registries_list_tail_ = Smi::zero();

  GCCallbacks gc_prologue_callbacks_;
  GCCallbacks gc_epilogue_callbacks_;

  GetExternallyAllocatedMemoryInBytesCallback external_memory_callback_;

  base::SmallVector<v8::Isolate::UseCounterFeature, 8> deferred_counters_;

  size_t promoted_objects_size_ = 0;
  double promotion_ratio_ = 0.0;
  double promotion_rate_ = 0.0;
  size_t new_space_surviving_object_size_ = 0;
  size_t previous_new_space_surviving_object_size_ = 0;
  double new_space_surviving_rate_ = 0.0;
  int nodes_died_in_new_space_ = 0;
  int nodes_copied_in_new_space_ = 0;
  int nodes_promoted_ = 0;

  // Total time spent in GC.
  base::TimeDelta total_gc_time_ms_;

  // Last time a garbage collection happened.
  double last_gc_time_ = 0.0;

  std::unique_ptr<GCTracer> tracer_;
  std::unique_ptr<Sweeper> sweeper_;
  std::unique_ptr<MarkCompactCollector> mark_compact_collector_;
  std::unique_ptr<MinorMarkSweepCollector> minor_mark_sweep_collector_;
  std::unique_ptr<ScavengerCollector> scavenger_collector_;
  std::unique_ptr<ArrayBufferSweeper> array_buffer_sweeper_;

  std::unique_ptr<MemoryAllocator> memory_allocator_;
  std::unique_ptr<IncrementalMarking> incremental_marking_;
  std::unique_ptr<ConcurrentMarking> concurrent_marking_;
  std::unique_ptr<MemoryMeasurement> memory_measurement_;
  std::unique_ptr<MemoryReducer> memory_reducer_;
  std::unique_ptr<ObjectStats> live_object_stats_;
  std::unique_ptr<ObjectStats> dead_object_stats_;
  std::unique_ptr<MinorGCJob> minor_gc_job_;
  std::unique_ptr<AllocationObserver> minor_gc_task_observer_;
  std::unique_ptr<AllocationObserver> stress_concurrent_allocation_observer_;
  std::unique_ptr<AllocationTrackerForDebugging>
      allocation_tracker_for_debugging_;
  std::unique_ptr<EphemeronRememberedSet> ephemeron_remembered_set_;

  std::shared_ptr<v8::TaskRunner> task_runner_;

  // This object controls virtual space reserved for code on the V8 heap. This
  // is only valid for 64-bit architectures where kPlatformRequiresCodeRange.
  //
  // Owned by the isolate group when V8_COMPRESS_POINTERS, otherwise owned by
  // the heap.
#ifdef V8_COMPRESS_POINTERS
  CodeRange* code_range_ = nullptr;
#else
  std::unique_ptr<CodeRange> code_range_;
#endif

  // The process-wide virtual space reserved for trusted objects in the V8 heap.
  // Only used when the sandbox is enabled.
#if V8_ENABLE_SANDBOX
  TrustedRange* trusted_range_ = nullptr;
#endif

  // V8 configuration where V8 owns the heap which is either created or passed
  // in during Isolate initialization.
  std::unique_ptr<CppHeap> owning_cpp_heap_;
  // Deprecated API where the heap is owned by the embedder. This field is
  // always set, independent of which CppHeap configuration (owned, unowned) is
  // used. As soon as Isolate::AttachCppHeap() is removed, this field should
  // also be removed and we should exclusively rely on the owning version.
  v8::CppHeap* cpp_heap_ = nullptr;
  EmbedderRootsHandler* embedder_roots_handler_ =
      nullptr;  // Owned by the embedder.

  StackState embedder_stack_state_ = StackState::kMayContainHeapPointers;
  std::optional<EmbedderStackStateOrigin> embedder_stack_state_origin_;

  StrongRootsEntry* strong_roots_head_ = nullptr;
  base::Mutex strong_roots_mutex_;

  base::Mutex heap_expansion_mutex_;

  bool need_to_remove_stress_concurrent_allocation_observer_ = false;

  // This counter is increased before each GC and never reset.
  // To account for the bytes allocated since the last GC, use the
  // NewSpaceAllocationCounter() function.
  size_t new_space_allocation_counter_ = 0;

  // This counter is increased before each GC and never reset. To
  // account for the bytes allocated since the last GC, use the
  // OldGenerationAllocationCounter() function.
  size_t old_generation_allocation_counter_at_last_gc_ = 0;

  // The size of objects in old generation after the last MarkCompact GC.
  size_t old_generation_size_at_last_gc_{0};

  // The wasted bytes in old generation after the last MarkCompact GC.
  size_t old_generation_wasted_at_last_gc_{0};

  // The size of global memory after the last MarkCompact GC.
  size_t global_consumed_memory_at_last_gc_ = 0;

  char trace_ring_buffer_[kTraceRingBufferSize];

  // If it's not full then the data is from 0 to ring_buffer_end_.  If it's
  // full then the data is from ring_buffer_end_ to the end of the buffer and
  // from 0 to ring_buffer_end_.
  bool ring_buffer_full_ = false;
  size_t ring_buffer_end_ = 0;

  // Flag is set when the heap has been configured.  The heap can be repeatedly
  // configured through the API until it is set up.
  bool configured_ = false;

  // Currently set GC flags that are respected by all GC components.
  GCFlags current_gc_flags_ = GCFlag::kNoFlags;
  // Currently set GC callback flags that are used to pass information between
  // the embedder and V8's GC.
  GCCallbackFlags current_gc_callback_flags_ =
      GCCallbackFlags::kNoGCCallbackFlags;

  std::unique_ptr<IsolateSafepoint> safepoint_;

  bool is_current_gc_forced_ = false;
  bool is_current_gc_for_heap_profiler_ = false;
  GarbageCollector current_or_last_garbage_collector_ =
      GarbageCollector::SCAVENGER;

  ExternalStringTable external_string_table_;

  const AllocationType allocation_type_for_in_place_internalizable_strings_;

  base::Mutex relocation_mutex_;

  std::unique_ptr<CollectionBarrier> collection_barrier_;

  int ignore_local_gc_requests_depth_ = 0;

  int gc_callbacks_depth_ = 0;

  bool deserialization_complete_ = false;

  int max_regular_code_object_size_ = 0;

  bool inline_allocation_enabled_ = true;

  int pause_allocation_observers_depth_ = 0;

  // Used for testing purposes.
  bool force_oom_ = false;
  bool force_gc_on_next_allocation_ = false;
  bool delay_sweeper_tasks_for_testing_ = false;

  std::vector<HeapObjectAllocationTracker*> allocation_trackers_;

  bool is_finalization_registry_cleanup_task_posted_ = false;

  std::unique_ptr<third_party_heap::Heap> tp_heap_;

  MarkingState marking_state_;
  NonAtomicMarkingState non_atomic_marking_state_;

  PretenuringHandler pretenuring_handler_;

  // This field is used only when not running with MinorMS.
  ResizeNewSpaceMode resize_new_space_mode_ = ResizeNewSpaceMode::kNone;

  std::unique_ptr<MemoryBalancer> mb_;

  std::atomic<double> load_start_time_ms_{0};
  bool update_allocation_limits_after_loading_ = false;
  // Full GC may trigger during loading due to overshooting allocation limits.
  // In such cases we may want to update the limits again once loading is
  // actually finished.
  bool is_full_gc_during_loading_ = false;

  // Classes in "heap" can be friends.
  friend class ActivateMemoryReducerTask;
  friend class AlwaysAllocateScope;
  friend class ArrayBufferCollector;
  friend class ArrayBufferSweeper;
  friend class ConcurrentMarking;
  friend class ConservativeTracedHandlesMarkingVisitor;
  friend class CppHeap;
  friend class EmbedderStackStateScope;
  friend class EvacuateVisitorBase;
  friend class GCCallbacksScope;
  friend class GCTracer;
  friend class HeapAllocator;
  friend class HeapObjectIterator;
  friend class HeapVerifier;
  friend class IgnoreLocalGCRequests;
  friend class IncrementalMarking;
  friend class IncrementalMarkingJob;
  friend class LargeObjectSpace;
  friend class LocalHeap;
  friend class MarkingBarrier;
  friend class OldLargeObjectSpace;
  template <typename ConcreteVisitor>
  friend class MarkingVisitorBase;
  friend class MarkCompactCollector;
  friend class MemoryBalancer;
  friend class MinorGCJob;
  friend class MinorGCTaskObserver;
  friend class MinorMarkSweepCollector;
  friend class MinorMSIncrementalMarkingTaskObserver;
  friend class NewLargeObjectSpace;
  friend class NewSpace;
  friend class ObjectStatsCollector;
  friend class PageMetadata;
  friend class PagedNewSpaceAllocatorPolicy;
  friend class PagedSpaceAllocatorPolicy;
  friend class PagedSpaceBase;
  friend class PagedSpaceForNewSpace;
  friend class PauseAllocationObserversScope;
  friend class PretenuringHandler;
  friend class ReadOnlyRoots;
  friend class DisableConservativeStackScanningScopeForTesting;
  friend class Scavenger;
  friend class ScavengerCollector;
  friend class ScheduleMinorGCTaskObserver;
  friend class StressConcurrentAllocationObserver;
  friend class Space;
  friend class SpaceWithLinearArea;
  friend class Sweeper;
  friend class UnifiedHeapMarkingState;
  friend class heap::TestMemoryAllocatorScope;
  friend class third_party_heap::Heap;
  friend class third_party_heap::Impl;

  // The allocator interface.
  friend class Factory;
  friend class LocalFactory;
  template <typename IsolateT>
  friend class Deserializer;

  // The Isolate constructs us.
  friend class Isolate;

  // Used in cctest.
  friend class heap::HeapTester;
  FRIEND_TEST(SpacesTest, InlineAllocationObserverCadence);
  FRIEND_TEST(SpacesTest, AllocationObserver);
  friend class HeapInternalsBase;
};

#define DECL_RIGHT_TRIM(T)                                        \
  extern template EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) void \
  Heap::RightTrimArray<T>(Tagged<T> object, int new_capacity,     \
                          int old_capacity);
RIGHT_TRIMMABLE_ARRAY_LIST(DECL_RIGHT_TRIM)
#undef DECL_RIGHT_TRIM

class HeapStats {
 public:
  static const int kStartMarker = 0xDECADE00;
  static const int kEndMarker = 0xDECADE01;

  intptr_t* start_marker;                  //  0
  size_t* ro_space_size;                   //  1
  size_t* ro_space_capacity;               //  2
  size_t* new_space_size;                  //  3
  size_t* new_space_capacity;              //  4
  size_t* old_space_size;                  //  5
  size_t* old_space_capacity;              //  6
  size_t* code_space_size;                 //  7
  size_t* code_space_capacity;             //  8
  size_t* map_space_size;                  //  9
  size_t* map_space_capacity;              // 10
  size_t* lo_space_size;                   // 11
  size_t* code_lo_space_size;              // 12
  size_t* global_handle_count;             // 13
  size_t* weak_global_handle_count;        // 14
  size_t* pending_global_handle_count;     // 15
  size_t* near_death_global_handle_count;  // 16
  size_t* free_global_handle_count;        // 17
  size_t* memory_allocator_size;           // 18
  size_t* memory_allocator_capacity;       // 19
  size_t* malloced_memory;                 // 20
  size_t* malloced_peak_memory;            // 21
  size_t* objects_per_type;                // 22
  size_t* size_per_type;                   // 23
  int* os_error;                           // 24
  char* last_few_messages;                 // 25
  char* js_stacktrace;                     // 26
  intptr_t* end_marker;                    // 27
};

// Disables GC for all allocations. It should not be used
// outside heap, deserializer, and isolate bootstrap.
// Use AlwaysAllocateScopeForTesting in tests.
class V8_NODISCARD AlwaysAllocateScope {
 public:
  inline ~AlwaysAllocateScope();

 private:
  friend class AlwaysAllocateScopeForTesting;
  friend class Evacuator;
  friend class Heap;
  friend class HeapAllocator;
  friend class Isolate;
  // TODO(1445003): Remove this after investigating the crash.
  friend class GlobalBackingStoreRegistry;

  explicit inline AlwaysAllocateScope(Heap* heap);
  Heap* heap_;
};

class V8_NODISCARD GCCallbacksScope final {
 public:
  explicit GCCallbacksScope(Heap* heap);
  ~GCCallbacksScope();

  bool CheckReenter() const;

 private:
  Heap* const heap_;
};

class V8_NODISCARD AlwaysAllocateScopeForTesting {
 public:
  explicit inline AlwaysAllocateScopeForTesting(Heap* heap);

 private:
  AlwaysAllocateScope scope_;
};

class CodePageMemoryModificationScopeForDebugging {
 public:
  // When we zap newly allocated MemoryChunks, the chunk is not initialized yet
  // and we can't use the regular CodePageMemoryModificationScope since it will
  // access the page header. Hence, use the VirtualMemory for tracking instead.
  explicit CodePageMemoryModificationScopeForDebugging(
      Heap* heap, VirtualMemory* reservation, base::AddressRegion region);
  explicit CodePageMemoryModificationScopeForDebugging(
      MemoryChunkMetadata* chunk);
  ~CodePageMemoryModificationScopeForDebugging();

 private:
#if V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT || \
    V8_HEAP_USE_PKU_JIT_WRITE_PROTECT || V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT
  RwxMemoryWriteScope rwx_write_scope_;
#endif
};

class V8_NODISCARD IgnoreLocalGCRequests {
 public:
  explicit inline IgnoreLocalGCRequests(Heap* heap);
  inline ~IgnoreLocalGCRequests();

 private:
  Heap* heap_;
};

// Space iterator for iterating over all the paged spaces of the heap: Map
// space, old space and code space. Returns each space in turn, and null when it
// is done.
class V8_EXPORT_PRIVATE PagedSpaceIterator {
 public:
  explicit PagedSpaceIterator(const Heap* heap)
      : heap_(heap), counter_(FIRST_GROWABLE_PAGED_SPACE) {}
  PagedSpace* Next();

 private:
  const Heap* const heap_;
  int counter_;
};

// A HeapObjectIterator provides iteration over the entire non-read-only heap.
// It aggregates the specific iterators for the different spaces as these can
// only iterate over one space only.
//
// HeapObjectIterator ensures there is no allocation during its lifetime (using
// an embedded DisallowGarbageCollection instance).
//
// HeapObjectIterator can skip free list nodes (that is, de-allocated heap
// objects that still remain in the heap).
//
// See ReadOnlyHeapObjectIterator if you need to iterate over read-only space
// objects, or CombinedHeapObjectIterator if you need to iterate over both
// heaps.
class V8_EXPORT_PRIVATE HeapObjectIterator {
 public:
  enum HeapObjectsFiltering { kNoFiltering, kFilterUnreachable };

  explicit HeapObjectIterator(Heap* heap,
                              HeapObjectsFiltering filtering = kNoFiltering);
  // .. when already in a SafepointScope:
  HeapObjectIterator(Heap* heap, const SafepointScope& safepoint_scope,
                     HeapObjectsFiltering filtering = kNoFiltering);
  ~HeapObjectIterator();

  Tagged<HeapObject> Next();

 private:
  HeapObjectIterator(Heap* heap, SafepointScope* safepoint_scope_or_nullptr,
                     HeapObjectsFiltering filtering);

  Tagged<HeapObject> NextObject();

  Heap* heap_;
  DISALLOW_GARBAGE_COLLECTION(no_heap_allocation_)

  // The safepoint scope pointer is null if a scope already existed when the
  // iterator was created (i.e. when using the constructor that passes a
  // safepoint_scope reference).
  std::unique_ptr<SafepointScope> safepoint_scope_;  // nullable
  std::unique_ptr<HeapObjectsFilter> filter_;
  // Space iterator for iterating all the spaces.
  SpaceIterator space_iterator_;
  // Object iterator for the space currently being iterated.
  std::unique_ptr<ObjectIterator> object_iterator_;
};

// Abstract base class for checking whether a weak object should be retained.
class WeakObjectRetainer {
 public:
  virtual ~WeakObjectRetainer() = default;

  // Return whether this object should be retained. If nullptr is returned the
  // object has no references. Otherwise the address of the retained object
  // should be returned as in some GC situations the object has been moved.
  virtual Tagged<Object> RetainAs(Tagged<Object> object) = 0;
};

// -----------------------------------------------------------------------------
// Allows observation of heap object allocations.
class HeapObjectAllocationTracker {
 public:
  virtual void AllocationEvent(Address addr, int size) = 0;
  virtual void MoveEvent(Address from, Address to, int size) {}
  virtual void UpdateObjectSizeEvent(Address addr, int size) {}
  virtual ~HeapObjectAllocationTracker() = default;
};

template <typename T>
inline T ForwardingAddress(T heap_obj);

// Specialized strong root allocator for blocks of Addresses, retained
// as strong references.
template <>
class StrongRootAllocator<Address> : public StrongRootAllocatorBase {
 public:
  using value_type = Address;

  explicit StrongRootAllocator(Heap* heap) : StrongRootAllocatorBase(heap) {}
  explicit StrongRootAllocator(Isolate* isolate)
      : StrongRootAllocatorBase(isolate) {}
  explicit StrongRootAllocator(v8::Isolate* isolate)
      : StrongRootAllocatorBase(reinterpret_cast<Isolate*>(isolate)) {}
  template <typename U>
  StrongRootAllocator(const StrongRootAllocator<U>& other) V8_NOEXCEPT
      : StrongRootAllocatorBase(other) {}

  Address* allocate(size_t n) { return allocate_impl(n); }
  void deallocate(Address* p, size_t n) noexcept {
    return deallocate_impl(p, n);
  }
};

class V8_EXPORT_PRIVATE V8_NODISCARD EmbedderStackStateScope final {
 public:
  EmbedderStackStateScope(Heap* heap, EmbedderStackStateOrigin origin,
                          StackState stack_state);
  ~EmbedderStackStateScope();

 private:
  Heap* const heap_;
  const StackState old_stack_state_;
  std::optional<EmbedderStackStateOrigin> old_origin_;
};

class V8_NODISCARD DisableConservativeStackScanningScopeForTesting {
 public:
  explicit inline DisableConservativeStackScanningScopeForTesting(Heap* heap)
      : embedder_scope_(heap, EmbedderStackStateOrigin::kExplicitInvocation,
                        StackState::kNoHeapPointers) {}

 private:
  EmbedderStackStateScope embedder_scope_;
};

class V8_NODISCARD CppClassNamesAsHeapObjectNameScope final {
 public:
  explicit CppClassNamesAsHeapObjectNameScope(v8::CppHeap* heap);
  ~CppClassNamesAsHeapObjectNameScope();

 private:
  std::unique_ptr<cppgc::internal::ClassNameAsHeapObjectNameScope> scope_;
};

}  // namespace internal
}  // namespace v8

// Opt out from libc++ backing sanitization, since root iteration walks up to
// the capacity.
#ifdef _LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS
template <typename T>
struct ::std::__asan_annotate_container_with_allocator<
    v8::internal::StrongRootAllocator<T>> : ::std::false_type {};
#endif  // _LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS

#endif  // V8_HEAP_HEAP_H_
