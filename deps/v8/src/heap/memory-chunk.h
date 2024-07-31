// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_H_
#define V8_HEAP_MEMORY_CHUNK_H_

#include "src/base/build_config.h"
#include "src/base/functional.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

class Heap;
class MemoryChunkMetadata;
template <typename T>
class Tagged;

class V8_EXPORT_PRIVATE MemoryChunk final {
 public:
  explicit MemoryChunk(MemoryChunkMetadata* metadata)
      : main_thread_flags_(NO_FLAGS), metadata_(metadata) {}

  // All possible flags that can be set on a page. While the value of flags
  // doesn't matter in principle, keep flags used in the write barrier together
  // in order to have dense page flag checks in the write barrier.
  enum Flag : uintptr_t {
    NO_FLAGS = 0u,

    // This page belongs to a shared heap.
    IN_WRITABLE_SHARED_SPACE = 1u << 0,

    // These two flags are used in the write barrier to catch "interesting"
    // references.
    POINTERS_TO_HERE_ARE_INTERESTING = 1u << 1,
    POINTERS_FROM_HERE_ARE_INTERESTING = 1u << 2,

    // A page in the from-space or a young large page that was not scavenged
    // yet.
    FROM_PAGE = 1u << 3,
    // A page in the to-space or a young large page that was scavenged.
    TO_PAGE = 1u << 4,

    // |INCREMENTAL_MARKING|: Indicates whether incremental marking is currently
    // enabled.
    INCREMENTAL_MARKING = 1u << 5,

    // The memory chunk belongs to the read-only heap and does not participate
    // in garbage collection. This is used instead of owner for identity
    // checking since read-only chunks have no owner once they are detached.
    READ_ONLY_HEAP = 1u << 6,

    // ----------------------------------------------------------------
    // Values below here are not critical for the heap write barrier.

    LARGE_PAGE = 1u << 7,
    EVACUATION_CANDIDATE = 1u << 8,
    NEVER_EVACUATE = 1u << 9,

    // |PAGE_NEW_OLD_PROMOTION|: A page tagged with this flag has been promoted
    // from new to old space during evacuation.
    PAGE_NEW_OLD_PROMOTION = 1u << 10,

    // This flag is intended to be used for testing. Works only when both
    // v8_flags.stress_compaction and
    // v8_flags.manual_evacuation_candidates_selection are set. It forces the
    // page to become an evacuation candidate at next candidates selection
    // cycle.
    FORCE_EVACUATION_CANDIDATE_FOR_TESTING = 1u << 11,

    // This flag is intended to be used for testing.
    NEVER_ALLOCATE_ON_PAGE = 1u << 12,

    // The memory chunk is already logically freed, however the actual freeing
    // still has to be performed.
    PRE_FREED = 1u << 13,

    // |COMPACTION_WAS_ABORTED|: Indicates that the compaction in this page
    //   has been aborted and needs special handling by the sweeper.
    COMPACTION_WAS_ABORTED = 1u << 14,

    NEW_SPACE_BELOW_AGE_MARK = 1u << 15,

    // The memory chunk freeing bookkeeping has been performed but the chunk has
    // not yet been freed.
    UNREGISTERED = 1u << 16,

    // The memory chunk is pinned in memory and can't be moved. This is likely
    // because there exists a potential pointer to somewhere in the chunk which
    // can't be updated.
    PINNED = 1u << 17,

    // A Page with code objects.
    IS_EXECUTABLE = 1u << 18,

    // The memory chunk belongs to the trusted space. When the sandbox is
    // enabled, the trusted space is located outside of the sandbox and so its
    // content cannot be corrupted by an attacker.
    IS_TRUSTED = 1u << 19,
  };

  using MainThreadFlags = base::Flags<Flag, uintptr_t>;

  static constexpr MainThreadFlags kAllFlagsMask = ~MainThreadFlags(NO_FLAGS);
  static constexpr MainThreadFlags kPointersToHereAreInterestingMask =
      POINTERS_TO_HERE_ARE_INTERESTING;
  static constexpr MainThreadFlags kPointersFromHereAreInterestingMask =
      POINTERS_FROM_HERE_ARE_INTERESTING;
  static constexpr MainThreadFlags kEvacuationCandidateMask =
      EVACUATION_CANDIDATE;
  static constexpr MainThreadFlags kIsInYoungGenerationMask =
      MainThreadFlags(FROM_PAGE) | MainThreadFlags(TO_PAGE);
  static constexpr MainThreadFlags kIsLargePageMask = LARGE_PAGE;
  static constexpr MainThreadFlags kInSharedHeap = IN_WRITABLE_SHARED_SPACE;
  static constexpr MainThreadFlags kIncrementalMarking = INCREMENTAL_MARKING;
  static constexpr MainThreadFlags kSkipEvacuationSlotsRecordingMask =
      MainThreadFlags(kEvacuationCandidateMask) |
      MainThreadFlags(kIsInYoungGenerationMask);

  // Page flags copied from from-space to to-space when flipping semispaces.
  static constexpr MainThreadFlags kCopyOnFlipFlagsMask =
      MainThreadFlags(POINTERS_TO_HERE_ARE_INTERESTING) |
      MainThreadFlags(POINTERS_FROM_HERE_ARE_INTERESTING) |
      MainThreadFlags(INCREMENTAL_MARKING);

  V8_INLINE Address address() const { return reinterpret_cast<Address>(this); }

  static constexpr Address BaseAddress(Address a) {
    // If this changes, we also need to update
    // CodeStubAssembler::MemoryChunkFromAddress and
    // MacroAssembler::MemoryChunkHeaderFromObject
    return a & ~kAlignmentMask;
  }

  V8_INLINE static MemoryChunk* FromAddress(Address addr) {
    DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
    return reinterpret_cast<MemoryChunk*>(BaseAddress(addr));
  }

  template <typename HeapObject>
  V8_INLINE static MemoryChunk* FromHeapObject(Tagged<HeapObject> object) {
    DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
    return FromAddress(object.ptr());
  }

  V8_INLINE MemoryChunkMetadata* Metadata() {
    // If this changes, we also need to update
    // CodeStubAssembler::PageMetadataFromMemoryChunk
    return metadata_;
  }

  V8_INLINE const MemoryChunkMetadata* Metadata() const {
    // If this changes, we also need to update
    // CodeStubAssembler::PageMetadataFromMemoryChunk
    return metadata_;
  }

  V8_INLINE bool IsFlagSet(Flag flag) const {
    return main_thread_flags_ & flag;
  }

  V8_INLINE bool IsMarking() const { return IsFlagSet(INCREMENTAL_MARKING); }

  V8_INLINE bool InWritableSharedSpace() const {
    return IsFlagSet(IN_WRITABLE_SHARED_SPACE);
  }

  V8_INLINE bool InYoungGeneration() const {
    if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) return false;
    constexpr uintptr_t kYoungGenerationMask = FROM_PAGE | TO_PAGE;
    return GetFlags() & kYoungGenerationMask;
  }

  // Checks whether chunk is either in young gen or shared heap.
  V8_INLINE bool IsYoungOrSharedChunk() const {
    if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) return false;
    constexpr uintptr_t kYoungOrSharedChunkMask =
        FROM_PAGE | TO_PAGE | IN_WRITABLE_SHARED_SPACE;
    return GetFlags() & kYoungOrSharedChunkMask;
  }

  V8_INLINE MainThreadFlags GetFlags() const { return main_thread_flags_; }
  V8_INLINE void SetFlag(Flag flag) { main_thread_flags_ |= flag; }
  V8_INLINE void ClearFlag(Flag flag) {
    main_thread_flags_ = main_thread_flags_.without(flag);
  }
  // Set or clear multiple flags at a time. `mask` indicates which flags are
  // should be replaced with new `flags`.
  V8_INLINE void ClearFlags(MainThreadFlags flags) {
    main_thread_flags_ &= ~flags;
  }
  V8_INLINE void SetFlags(MainThreadFlags flags,
                          MainThreadFlags mask = kAllFlagsMask) {
    main_thread_flags_ = (main_thread_flags_ & ~mask) | (flags & mask);
  }

  Heap* GetHeap();

  // Emits a memory barrier. For TSAN builds the other thread needs to perform
  // MemoryChunk::SynchronizedLoad() to simulate the barrier.
  void InitializationMemoryFence();

#ifdef THREAD_SANITIZER
  void SynchronizedLoad() const;
  bool InReadOnlySpace() const;
#else
  V8_INLINE bool InReadOnlySpace() const { return IsFlagSet(READ_ONLY_HEAP); }
#endif

  V8_INLINE bool InCodeSpace() const { return IsFlagSet(IS_EXECUTABLE); }

  V8_INLINE bool InTrustedSpace() const { return IsFlagSet(IS_TRUSTED); }

  bool NeverEvacuate() const { return IsFlagSet(NEVER_EVACUATE); }
  void MarkNeverEvacuate() { SetFlag(NEVER_EVACUATE); }

  bool CanAllocate() const {
    return !IsEvacuationCandidate() && !IsFlagSet(NEVER_ALLOCATE_ON_PAGE);
  }

  bool IsEvacuationCandidate() const {
    DCHECK(!(IsFlagSet(NEVER_EVACUATE) && IsFlagSet(EVACUATION_CANDIDATE)));
    return IsFlagSet(EVACUATION_CANDIDATE);
  }

  bool ShouldSkipEvacuationSlotRecording() const {
    MainThreadFlags flags = GetFlags();
    return ((flags & kSkipEvacuationSlotsRecordingMask) != 0) &&
           ((flags & COMPACTION_WAS_ABORTED) == 0);
  }

  Executability executable() const {
    return IsFlagSet(IS_EXECUTABLE) ? EXECUTABLE : NOT_EXECUTABLE;
  }

  bool IsFromPage() const { return IsFlagSet(FROM_PAGE); }
  bool IsToPage() const { return IsFlagSet(TO_PAGE); }
  bool IsLargePage() const { return IsFlagSet(LARGE_PAGE); }
  bool InNewSpace() const { return InYoungGeneration() && !IsLargePage(); }
  bool InNewLargeObjectSpace() const {
    return InYoungGeneration() && IsLargePage();
  }
  bool IsPinned() const { return IsFlagSet(PINNED); }

  V8_INLINE static constexpr bool IsAligned(Address address) {
    return (address & kAlignmentMask) == 0;
  }

#ifdef DEBUG
  bool IsTrusted() const;
#else
  bool IsTrusted() const { return IsFlagSet(IS_TRUSTED); }
#endif

  static intptr_t GetAlignmentForAllocation() { return kAlignment; }
  // The macro and code stub assemblers need access to the alignment mask to
  // implement functionality from this class. In particular, this is used to
  // implement the header lookups and to calculate the object offsets in the
  // page.
  static constexpr intptr_t GetAlignmentMaskForAssembler() {
    return kAlignmentMask;
  }

  static constexpr uint32_t AddressToOffset(Address address) {
    return static_cast<uint32_t>(address) & kAlignmentMask;
  }

#ifdef DEBUG
  size_t Offset(Address addr) const;
#else
  size_t Offset(Address addr) const { return addr - address(); }
#endif

 private:
  // Flags that are only mutable from the main thread when no concurrent
  // component (e.g. marker, sweeper, compilation, allocation) is running.
  MainThreadFlags main_thread_flags_;

  MemoryChunkMetadata* metadata_;

  static constexpr intptr_t kAlignment =
      (static_cast<uintptr_t>(1) << kPageSizeBits);
  static constexpr intptr_t kAlignmentMask = kAlignment - 1;

  friend class BasicMemoryChunkValidator;
};

DEFINE_OPERATORS_FOR_FLAGS(MemoryChunk::MainThreadFlags)

}  // namespace internal

namespace base {

// Define special hash function for chunk pointers, to be used with std data
// structures, e.g.
// std::unordered_set<MemoryChunk*, base::hash<MemoryChunk*>
// This hash function discards the trailing zero bits (chunk alignment).
// Notice that, when pointer compression is enabled, it also discards the
// cage base.
template <>
struct hash<const i::MemoryChunk*> {
  V8_INLINE size_t operator()(const i::MemoryChunk* chunk) const {
    return static_cast<v8::internal::Tagged_t>(
               reinterpret_cast<uintptr_t>(chunk)) >>
           kPageSizeBits;
  }
};

template <>
struct hash<i::MemoryChunk*> {
  V8_INLINE size_t operator()(i::MemoryChunk* chunk) const {
    return hash<const i::MemoryChunk*>()(chunk);
  }
};

}  // namespace base

}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_H_
