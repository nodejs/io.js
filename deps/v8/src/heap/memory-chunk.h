// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_H_
#define V8_HEAP_MEMORY_CHUNK_H_

#include "src/base/build_config.h"
#include "src/base/functional.h"
#include "src/flags/flags.h"

#if V8_ENABLE_STICKY_MARK_BITS_BOOL
#define UNREACHABLE_WITH_STICKY_MARK_BITS() UNREACHABLE()
#else
#define UNREACHABLE_WITH_STICKY_MARK_BITS()
#endif

namespace v8 {
namespace internal {

class Heap;
class MemoryChunkMetadata;
class ReadOnlyPageMetadata;
class PageMetadata;
class LargePageMetadata;
class CodeStubAssembler;
class ExternalReference;
template <typename T>
class Tagged;
class TestDebugHelper;

enum class MarkingMode { kNoMarking, kMinorMarking, kMajorMarking };

class V8_EXPORT_PRIVATE MemoryChunk final {
 public:
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

    // Used in young generation checks. When sticky mark-bits are enabled and
    // major GC in progress, treat all objects as old.
    IS_MAJOR_GC_IN_PROGRESS = 1u << 7,

    // Used to mark chunks belonging to spaces that do not suppor young gen
    // allocations. Such chunks can never contain any young objects.
    CONTAINS_ONLY_OLD = 1u << 8,

    // ----------------------------------------------------------------
    // Values below here are not critical for the heap write barrier.

    LARGE_PAGE = 1u << 9,
    EVACUATION_CANDIDATE = 1u << 10,
    NEVER_EVACUATE = 1u << 11,

    // |PAGE_NEW_OLD_PROMOTION|: A page tagged with this flag has been promoted
    // from new to old space during evacuation.
    PAGE_NEW_OLD_PROMOTION = 1u << 12,

    // This flag is intended to be used for testing. Works only when both
    // v8_flags.stress_compaction and
    // v8_flags.manual_evacuation_candidates_selection are set. It forces the
    // page to become an evacuation candidate at next candidates selection
    // cycle.
    FORCE_EVACUATION_CANDIDATE_FOR_TESTING = 1u << 13,

    // This flag is intended to be used for testing.
    NEVER_ALLOCATE_ON_PAGE = 1u << 14,

    // The memory chunk is already logically freed, however the actual freeing
    // still has to be performed.
    PRE_FREED = 1u << 15,

    // |COMPACTION_WAS_ABORTED|: Indicates that the compaction in this page
    //   has been aborted and needs special handling by the sweeper.
    COMPACTION_WAS_ABORTED = 1u << 16,

    NEW_SPACE_BELOW_AGE_MARK = 1u << 17,

    // The memory chunk freeing bookkeeping has been performed but the chunk has
    // not yet been freed.
    UNREGISTERED = 1u << 18,

    // The memory chunk is pinned in memory and can't be moved. This is likely
    // because there exists a potential pointer to somewhere in the chunk which
    // can't be updated.
    PINNED = 1u << 19,

    // A Page with code objects.
    IS_EXECUTABLE = 1u << 20,

    // The memory chunk belongs to the trusted space. When the sandbox is
    // enabled, the trusted space is located outside of the sandbox and so its
    // content cannot be corrupted by an attacker.
    IS_TRUSTED = 1u << 21,
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
  static constexpr MainThreadFlags kIsInReadOnlyHeapMask = READ_ONLY_HEAP;
  static constexpr MainThreadFlags kIsLargePageMask = LARGE_PAGE;
  static constexpr MainThreadFlags kInSharedHeap = IN_WRITABLE_SHARED_SPACE;
  static constexpr MainThreadFlags kIncrementalMarking = INCREMENTAL_MARKING;
  static constexpr MainThreadFlags kSkipEvacuationSlotsRecordingMask =
      MainThreadFlags(kEvacuationCandidateMask) |
      MainThreadFlags(kIsInYoungGenerationMask);

  static constexpr MainThreadFlags kIsOnlyOldOrMajorGCInProgressMask =
      MainThreadFlags(CONTAINS_ONLY_OLD) |
      MainThreadFlags(IS_MAJOR_GC_IN_PROGRESS);

  MemoryChunk(MainThreadFlags flags, MemoryChunkMetadata* metadata);

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

  V8_INLINE MemoryChunkMetadata* Metadata();

  V8_INLINE const MemoryChunkMetadata* Metadata() const;

  V8_INLINE bool IsFlagSet(Flag flag) const {
    return main_thread_flags_ & flag;
  }

  V8_INLINE bool IsMarking() const { return IsFlagSet(INCREMENTAL_MARKING); }

  V8_INLINE bool InWritableSharedSpace() const {
    return IsFlagSet(IN_WRITABLE_SHARED_SPACE);
  }

  V8_INLINE bool InYoungGeneration() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
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

  void SetFlagSlow(Flag flag);
  void ClearFlagSlow(Flag flag);

  V8_INLINE MainThreadFlags GetFlags() const { return main_thread_flags_; }

  V8_INLINE void SetFlagUnlocked(Flag flag) { main_thread_flags_ |= flag; }
  V8_INLINE void ClearFlagUnlocked(Flag flag) {
    main_thread_flags_ = main_thread_flags_.without(flag);
  }
  // Set or clear multiple flags at a time. `mask` indicates which flags are
  // should be replaced with new `flags`.
  V8_INLINE void ClearFlagsUnlocked(MainThreadFlags flags) {
    main_thread_flags_ &= ~flags;
  }
  V8_INLINE void SetFlagsUnlocked(MainThreadFlags flags,
                                  MainThreadFlags mask = kAllFlagsMask) {
    main_thread_flags_ = (main_thread_flags_ & ~mask) | (flags & mask);
  }

  V8_INLINE void SetFlagNonExecutable(Flag flag) {
    return SetFlagUnlocked(flag);
  }
  V8_INLINE void ClearFlagNonExecutable(Flag flag) {
    return ClearFlagUnlocked(flag);
  }
  V8_INLINE void SetFlagsNonExecutable(MainThreadFlags flags,
                                       MainThreadFlags mask = kAllFlagsMask) {
    return SetFlagsUnlocked(flags, mask);
  }
  V8_INLINE void ClearFlagsNonExecutable(MainThreadFlags flags) {
    return ClearFlagsUnlocked(flags);
  }
  V8_INLINE void SetMajorGCInProgress() {
    SetFlagUnlocked(IS_MAJOR_GC_IN_PROGRESS);
  }
  V8_INLINE void ResetMajorGCInProgress() {
    ClearFlagUnlocked(IS_MAJOR_GC_IN_PROGRESS);
  }

  V8_INLINE Heap* GetHeap();

  // Emits a memory barrier. For TSAN builds the other thread needs to perform
  // MemoryChunk::SynchronizedLoad() to simulate the barrier.
  void InitializationMemoryFence();

#ifdef THREAD_SANITIZER
  void SynchronizedLoad() const;
  bool InReadOnlySpace() const;
#else
  V8_INLINE bool InReadOnlySpace() const { return IsFlagSet(READ_ONLY_HEAP); }
#endif

#ifdef V8_ENABLE_SANDBOX
  // Flags are stored in the page header and are not safe to rely on for sandbox
  // checks. This alternative version will check if the page is read-only
  // without relying on the inline flag.
  bool SandboxSafeInReadOnlySpace() const;
#endif

  V8_INLINE bool InCodeSpace() const { return IsFlagSet(IS_EXECUTABLE); }

  V8_INLINE bool InTrustedSpace() const { return IsFlagSet(IS_TRUSTED); }

  bool NeverEvacuate() const { return IsFlagSet(NEVER_EVACUATE); }
  void MarkNeverEvacuate() { SetFlagSlow(NEVER_EVACUATE); }

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

  bool IsFromPage() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
    return IsFlagSet(FROM_PAGE);
  }
  bool IsToPage() const {
    UNREACHABLE_WITH_STICKY_MARK_BITS();
    return IsFlagSet(TO_PAGE);
  }
  bool IsLargePage() const { return IsFlagSet(LARGE_PAGE); }
  bool InNewSpace() const { return InYoungGeneration() && !IsLargePage(); }
  bool InNewLargeObjectSpace() const {
    return InYoungGeneration() && IsLargePage();
  }
  bool IsPinned() const { return IsFlagSet(PINNED); }
  bool IsOnlyOldOrMajorMarkingOn() const {
    return GetFlags() & kIsOnlyOldOrMajorGCInProgressMask;
  }

  V8_INLINE static constexpr bool IsAligned(Address address) {
    return (address & kAlignmentMask) == 0;
  }

  static MainThreadFlags OldGenerationPageFlags(MarkingMode marking_mode,
                                                AllocationSpace space);
  static MainThreadFlags YoungGenerationPageFlags(MarkingMode marking_mode);

  void SetOldGenerationPageFlags(MarkingMode marking_mode,
                                 AllocationSpace space);
  void SetYoungGenerationPageFlags(MarkingMode marking_mode);

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
  // RememberedSetOperations take an offset to an end address that can be behind
  // the allocated memory.
  size_t OffsetMaybeOutOfRange(Address addr) const;
#else
  size_t Offset(Address addr) const { return addr - address(); }
  size_t OffsetMaybeOutOfRange(Address addr) const { return Offset(addr); }
#endif

#ifdef V8_ENABLE_SANDBOX
  static void ClearMetadataPointer(MemoryChunkMetadata* metadata);
#endif

 private:
  // Flags that are only mutable from the main thread when no concurrent
  // component (e.g. marker, sweeper, compilation, allocation) is running.
  MainThreadFlags main_thread_flags_;

#ifdef V8_ENABLE_SANDBOX
  uint32_t metadata_index_;
#else
  MemoryChunkMetadata* metadata_;
#endif

  static constexpr intptr_t kAlignment =
      (static_cast<uintptr_t>(1) << kPageSizeBits);
  static constexpr intptr_t kAlignmentMask = kAlignment - 1;

#ifdef V8_ENABLE_SANDBOX
#ifndef V8_EXTERNAL_CODE_SPACE
#error The global metadata pointer table requires a single external code space.
#endif
#ifdef V8_ENABLE_THIRD_PARTY_HEAP
#error The global metadata pointer table is incompatible with third party heap.
#endif

  static constexpr size_t kPagesInMainCage =
      kPtrComprCageReservationSize / kRegularPageSize;
  static constexpr size_t kPagesInCodeCage =
      kMaximalCodeRangeSize / kRegularPageSize;
  static constexpr size_t kPagesInTrustedCage =
      kMaximalTrustedRangeSize / kRegularPageSize;

  static constexpr size_t kMainCageMetadataOffset = 0;
  static constexpr size_t kTrustedSpaceMetadataOffset =
      kMainCageMetadataOffset + kPagesInMainCage;
  static constexpr size_t kCodeRangeMetadataOffset =
      kTrustedSpaceMetadataOffset + kPagesInTrustedCage;

  static constexpr size_t kMetadataPointerTableSizeLog2 = base::bits::BitWidth(
      kPagesInMainCage + kPagesInCodeCage + kPagesInTrustedCage);
  static constexpr size_t kMetadataPointerTableSize =
      1 << kMetadataPointerTableSizeLog2;
  static constexpr size_t kMetadataPointerTableSizeMask =
      kMetadataPointerTableSize - 1;

  static MemoryChunkMetadata*
      metadata_pointer_table_[kMetadataPointerTableSize];

  V8_INLINE static MemoryChunkMetadata* FromIndex(uint32_t index);
  static uint32_t MetadataTableIndex(Address chunk_address);

  V8_INLINE static Address MetadataTableAddress() {
    return reinterpret_cast<Address>(metadata_pointer_table_);
  }

  // For access to the kMetadataPointerTableSizeMask;
  friend class CodeStubAssembler;
  friend class MacroAssembler;
  // For access to the MetadataTableAddress;
  friend class ExternalReference;
  friend class TestDebugHelper;

#endif  // V8_ENABLE_SANDBOX

  friend class MemoryChunkValidator;
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

#undef UNREACHABLE_WITH_STICKY_MARK_BITS

#endif  // V8_HEAP_MEMORY_CHUNK_H_
