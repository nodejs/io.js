// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/container/internal/raw_hash_set.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/endian.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hashtablez_sampler.h"
#include "absl/hash/hash.h"
#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Represents a control byte corresponding to a full slot with arbitrary hash.
constexpr ctrl_t ZeroCtrlT() { return static_cast<ctrl_t>(0); }

// We have space for `growth_info` before a single block of control bytes. A
// single block of empty control bytes for tables without any slots allocated.
// This enables removing a branch in the hot path of find(). In order to ensure
// that the control bytes are aligned to 16, we have 16 bytes before the control
// bytes even though growth_info only needs 8.
alignas(16) ABSL_CONST_INIT ABSL_DLL const ctrl_t kEmptyGroup[32] = {
    ZeroCtrlT(),       ZeroCtrlT(),    ZeroCtrlT(),    ZeroCtrlT(),
    ZeroCtrlT(),       ZeroCtrlT(),    ZeroCtrlT(),    ZeroCtrlT(),
    ZeroCtrlT(),       ZeroCtrlT(),    ZeroCtrlT(),    ZeroCtrlT(),
    ZeroCtrlT(),       ZeroCtrlT(),    ZeroCtrlT(),    ZeroCtrlT(),
    ctrl_t::kSentinel, ctrl_t::kEmpty, ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty, ctrl_t::kEmpty};

// We need one full byte followed by a sentinel byte for iterator::operator++ to
// work. We have a full group after kSentinel to be safe (in case operator++ is
// changed to read a full group).
ABSL_CONST_INIT ABSL_DLL const ctrl_t kSooControl[17] = {
    ZeroCtrlT(),    ctrl_t::kSentinel, ZeroCtrlT(),    ctrl_t::kEmpty,
    ctrl_t::kEmpty, ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty, ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty, ctrl_t::kEmpty,    ctrl_t::kEmpty, ctrl_t::kEmpty,
    ctrl_t::kEmpty};
static_assert(NumControlBytes(SooCapacity()) <= 17,
              "kSooControl capacity too small");

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr size_t Group::kWidth;
#endif

namespace {

// Returns "random" seed.
inline size_t RandomSeed() {
#ifdef ABSL_HAVE_THREAD_LOCAL
  static thread_local size_t counter = 0;
  // On Linux kernels >= 5.4 the MSAN runtime has a false-positive when
  // accessing thread local storage data from loaded libraries
  // (https://github.com/google/sanitizers/issues/1265), for this reason counter
  // needs to be annotated as initialized.
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(&counter, sizeof(size_t));
  size_t value = ++counter;
#else   // ABSL_HAVE_THREAD_LOCAL
  static std::atomic<size_t> counter(0);
  size_t value = counter.fetch_add(1, std::memory_order_relaxed);
#endif  // ABSL_HAVE_THREAD_LOCAL
  return value ^ static_cast<size_t>(reinterpret_cast<uintptr_t>(&counter));
}

bool ShouldRehashForBugDetection(const ctrl_t* ctrl, size_t capacity) {
  // Note: we can't use the abseil-random library because abseil-random
  // depends on swisstable. We want to return true with probability
  // `min(1, RehashProbabilityConstant() / capacity())`. In order to do this,
  // we probe based on a random hash and see if the offset is less than
  // RehashProbabilityConstant().
  return probe(ctrl, capacity, absl::HashOf(RandomSeed())).offset() <
         RehashProbabilityConstant();
}

// Find a non-deterministic hash for single group table.
// Last two bits are used to find a position for a newly inserted element after
// resize.
// This function is mixing all bits of hash and control pointer to maximize
// entropy.
size_t SingleGroupTableH1(size_t hash, ctrl_t* control) {
  return static_cast<size_t>(absl::popcount(
      hash ^ static_cast<size_t>(reinterpret_cast<uintptr_t>(control))));
}

}  // namespace

GenerationType* EmptyGeneration() {
  if (SwisstableGenerationsEnabled()) {
    constexpr size_t kNumEmptyGenerations = 1024;
    static constexpr GenerationType kEmptyGenerations[kNumEmptyGenerations]{};
    return const_cast<GenerationType*>(
        &kEmptyGenerations[RandomSeed() % kNumEmptyGenerations]);
  }
  return nullptr;
}

bool CommonFieldsGenerationInfoEnabled::
    should_rehash_for_bug_detection_on_insert(const ctrl_t* ctrl,
                                              size_t capacity) const {
  if (reserved_growth_ == kReservedGrowthJustRanOut) return true;
  if (reserved_growth_ > 0) return false;
  return ShouldRehashForBugDetection(ctrl, capacity);
}

bool CommonFieldsGenerationInfoEnabled::should_rehash_for_bug_detection_on_move(
    const ctrl_t* ctrl, size_t capacity) const {
  return ShouldRehashForBugDetection(ctrl, capacity);
}

bool ShouldInsertBackwardsForDebug(size_t capacity, size_t hash,
                                   const ctrl_t* ctrl) {
  // To avoid problems with weak hashes and single bit tests, we use % 13.
  // TODO(kfm,sbenza): revisit after we do unconditional mixing
  return !is_small(capacity) && (H1(hash, ctrl) ^ RandomSeed()) % 13 > 6;
}

size_t PrepareInsertAfterSoo(size_t hash, size_t slot_size,
                             CommonFields& common) {
  assert(common.capacity() == NextCapacity(SooCapacity()));
  // After resize from capacity 1 to 3, we always have exactly the slot with
  // index 1 occupied, so we need to insert either at index 0 or index 2.
  assert(HashSetResizeHelper::SooSlotIndex() == 1);
  PrepareInsertCommon(common);
  const size_t offset = SingleGroupTableH1(hash, common.control()) & 2;
  common.growth_info().OverwriteEmptyAsFull();
  SetCtrlInSingleGroupTable(common, offset, H2(hash), slot_size);
  common.infoz().RecordInsert(hash, /*distance_from_desired=*/0);
  return offset;
}

void ConvertDeletedToEmptyAndFullToDeleted(ctrl_t* ctrl, size_t capacity) {
  assert(ctrl[capacity] == ctrl_t::kSentinel);
  assert(IsValidCapacity(capacity));
  for (ctrl_t* pos = ctrl; pos < ctrl + capacity; pos += Group::kWidth) {
    Group{pos}.ConvertSpecialToEmptyAndFullToDeleted(pos);
  }
  // Copy the cloned ctrl bytes.
  std::memcpy(ctrl + capacity + 1, ctrl, NumClonedBytes());
  ctrl[capacity] = ctrl_t::kSentinel;
}
// Extern template instantiation for inline function.
template FindInfo find_first_non_full(const CommonFields&, size_t);

FindInfo find_first_non_full_outofline(const CommonFields& common,
                                       size_t hash) {
  return find_first_non_full(common, hash);
}

namespace {

// Returns the address of the slot just after slot assuming each slot has the
// specified size.
static inline void* NextSlot(void* slot, size_t slot_size) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot) + slot_size);
}

// Returns the address of the slot just before slot assuming each slot has the
// specified size.
static inline void* PrevSlot(void* slot, size_t slot_size) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot) - slot_size);
}

// Finds guaranteed to exists empty slot from the given position.
// NOTE: this function is almost never triggered inside of the
// DropDeletesWithoutResize, so we keep it simple.
// The table is rather sparse, so empty slot will be found very quickly.
size_t FindEmptySlot(size_t start, size_t end, const ctrl_t* ctrl) {
  for (size_t i = start; i < end; ++i) {
    if (IsEmpty(ctrl[i])) {
      return i;
    }
  }
  assert(false && "no empty slot");
  return ~size_t{};
}

void DropDeletesWithoutResize(CommonFields& common,
                              const PolicyFunctions& policy) {
  void* set = &common;
  void* slot_array = common.slot_array();
  const size_t capacity = common.capacity();
  assert(IsValidCapacity(capacity));
  assert(!is_small(capacity));
  // Algorithm:
  // - mark all DELETED slots as EMPTY
  // - mark all FULL slots as DELETED
  // - for each slot marked as DELETED
  //     hash = Hash(element)
  //     target = find_first_non_full(hash)
  //     if target is in the same group
  //       mark slot as FULL
  //     else if target is EMPTY
  //       transfer element to target
  //       mark slot as EMPTY
  //       mark target as FULL
  //     else if target is DELETED
  //       swap current element with target element
  //       mark target as FULL
  //       repeat procedure for current slot with moved from element (target)
  ctrl_t* ctrl = common.control();
  ConvertDeletedToEmptyAndFullToDeleted(ctrl, capacity);
  const void* hash_fn = policy.hash_fn(common);
  auto hasher = policy.hash_slot;
  auto transfer = policy.transfer;
  const size_t slot_size = policy.slot_size;

  size_t total_probe_length = 0;
  void* slot_ptr = SlotAddress(slot_array, 0, slot_size);

  // The index of an empty slot that can be used as temporary memory for
  // the swap operation.
  constexpr size_t kUnknownId = ~size_t{};
  size_t tmp_space_id = kUnknownId;

  for (size_t i = 0; i != capacity;
       ++i, slot_ptr = NextSlot(slot_ptr, slot_size)) {
    assert(slot_ptr == SlotAddress(slot_array, i, slot_size));
    if (IsEmpty(ctrl[i])) {
      tmp_space_id = i;
      continue;
    }
    if (!IsDeleted(ctrl[i])) continue;
    const size_t hash = (*hasher)(hash_fn, slot_ptr);
    const FindInfo target = find_first_non_full(common, hash);
    const size_t new_i = target.offset;
    total_probe_length += target.probe_length;

    // Verify if the old and new i fall within the same group wrt the hash.
    // If they do, we don't need to move the object as it falls already in the
    // best probe we can.
    const size_t probe_offset = probe(common, hash).offset();
    const auto probe_index = [probe_offset, capacity](size_t pos) {
      return ((pos - probe_offset) & capacity) / Group::kWidth;
    };

    // Element doesn't move.
    if (ABSL_PREDICT_TRUE(probe_index(new_i) == probe_index(i))) {
      SetCtrl(common, i, H2(hash), slot_size);
      continue;
    }

    void* new_slot_ptr = SlotAddress(slot_array, new_i, slot_size);
    if (IsEmpty(ctrl[new_i])) {
      // Transfer element to the empty spot.
      // SetCtrl poisons/unpoisons the slots so we have to call it at the
      // right time.
      SetCtrl(common, new_i, H2(hash), slot_size);
      (*transfer)(set, new_slot_ptr, slot_ptr);
      SetCtrl(common, i, ctrl_t::kEmpty, slot_size);
      // Initialize or change empty space id.
      tmp_space_id = i;
    } else {
      assert(IsDeleted(ctrl[new_i]));
      SetCtrl(common, new_i, H2(hash), slot_size);
      // Until we are done rehashing, DELETED marks previously FULL slots.

      if (tmp_space_id == kUnknownId) {
        tmp_space_id = FindEmptySlot(i + 1, capacity, ctrl);
      }
      void* tmp_space = SlotAddress(slot_array, tmp_space_id, slot_size);
      SanitizerUnpoisonMemoryRegion(tmp_space, slot_size);

      // Swap i and new_i elements.
      (*transfer)(set, tmp_space, new_slot_ptr);
      (*transfer)(set, new_slot_ptr, slot_ptr);
      (*transfer)(set, slot_ptr, tmp_space);

      SanitizerPoisonMemoryRegion(tmp_space, slot_size);

      // repeat the processing of the ith slot
      --i;
      slot_ptr = PrevSlot(slot_ptr, slot_size);
    }
  }
  ResetGrowthLeft(common);
  common.infoz().RecordRehash(total_probe_length);
}

static bool WasNeverFull(CommonFields& c, size_t index) {
  if (is_single_group(c.capacity())) {
    return true;
  }
  const size_t index_before = (index - Group::kWidth) & c.capacity();
  const auto empty_after = Group(c.control() + index).MaskEmpty();
  const auto empty_before = Group(c.control() + index_before).MaskEmpty();

  // We count how many consecutive non empties we have to the right and to the
  // left of `it`. If the sum is >= kWidth then there is at least one probe
  // window that might have seen a full group.
  return empty_before && empty_after &&
         static_cast<size_t>(empty_after.TrailingZeros()) +
                 empty_before.LeadingZeros() <
             Group::kWidth;
}

}  // namespace

void EraseMetaOnly(CommonFields& c, size_t index, size_t slot_size) {
  assert(IsFull(c.control()[index]) && "erasing a dangling iterator");
  c.decrement_size();
  c.infoz().RecordErase();

  if (WasNeverFull(c, index)) {
    SetCtrl(c, index, ctrl_t::kEmpty, slot_size);
    c.growth_info().OverwriteFullAsEmpty();
    return;
  }

  c.growth_info().OverwriteFullAsDeleted();
  SetCtrl(c, index, ctrl_t::kDeleted, slot_size);
}

void ClearBackingArray(CommonFields& c, const PolicyFunctions& policy,
                       bool reuse, bool soo_enabled) {
  c.set_size(0);
  if (reuse) {
    assert(!soo_enabled || c.capacity() > SooCapacity());
    ResetCtrl(c, policy.slot_size);
    ResetGrowthLeft(c);
    c.infoz().RecordStorageChanged(0, c.capacity());
  } else {
    // We need to record infoz before calling dealloc, which will unregister
    // infoz.
    c.infoz().RecordClearedReservation();
    c.infoz().RecordStorageChanged(0, soo_enabled ? SooCapacity() : 0);
    (*policy.dealloc)(c, policy);
    c = soo_enabled ? CommonFields{soo_tag_t{}} : CommonFields{non_soo_tag_t{}};
  }
}

void HashSetResizeHelper::GrowIntoSingleGroupShuffleControlBytes(
    ctrl_t* __restrict new_ctrl, size_t new_capacity) const {
  assert(is_single_group(new_capacity));
  constexpr size_t kHalfWidth = Group::kWidth / 2;
  ABSL_ASSUME(old_capacity_ < kHalfWidth);
  ABSL_ASSUME(old_capacity_ > 0);
  static_assert(Group::kWidth == 8 || Group::kWidth == 16,
                "Group size is not supported.");

  // NOTE: operations are done with compile time known size = 8.
  // Compiler optimizes that into single ASM operation.

  // Load the bytes from old_capacity. This contains
  // - the sentinel byte
  // - all the old control bytes
  // - the rest is filled with kEmpty bytes
  // Example:
  // old_ctrl =     012S012EEEEEEEEE...
  // copied_bytes = S012EEEE
  uint64_t copied_bytes =
      absl::little_endian::Load64(old_ctrl() + old_capacity_);

  // We change the sentinel byte to kEmpty before storing to both the start of
  // the new_ctrl, and past the end of the new_ctrl later for the new cloned
  // bytes. Note that this is faster than setting the sentinel byte to kEmpty
  // after the copy directly in new_ctrl because we are limited on store
  // bandwidth.
  static constexpr uint64_t kEmptyXorSentinel =
      static_cast<uint8_t>(ctrl_t::kEmpty) ^
      static_cast<uint8_t>(ctrl_t::kSentinel);

  // Replace the first byte kSentinel with kEmpty.
  // Resulting bytes will be shifted by one byte old control blocks.
  // Example:
  // old_ctrl = 012S012EEEEEEEEE...
  // before =   S012EEEE
  // after  =   E012EEEE
  copied_bytes ^= kEmptyXorSentinel;

  if (Group::kWidth == 8) {
    // With group size 8, we can grow with two write operations.
    assert(old_capacity_ < 8 && "old_capacity_ is too large for group size 8");
    absl::little_endian::Store64(new_ctrl, copied_bytes);

    static constexpr uint64_t kSentinal64 =
        static_cast<uint8_t>(ctrl_t::kSentinel);

    // Prepend kSentinel byte to the beginning of copied_bytes.
    // We have maximum 3 non-empty bytes at the beginning of copied_bytes for
    // group size 8.
    // Example:
    // old_ctrl = 012S012EEEE
    // before =   E012EEEE
    // after  =   SE012EEE
    copied_bytes = (copied_bytes << 8) ^ kSentinal64;
    absl::little_endian::Store64(new_ctrl + new_capacity, copied_bytes);
    // Example for capacity 3:
    // old_ctrl = 012S012EEEE
    // After the first store:
    //           >!
    // new_ctrl = E012EEEE???????
    // After the second store:
    //                  >!
    // new_ctrl = E012EEESE012EEE
    return;
  }

  assert(Group::kWidth == 16);

  // Fill the second half of the main control bytes with kEmpty.
  // For small capacity that may write into mirrored control bytes.
  // It is fine as we will overwrite all the bytes later.
  std::memset(new_ctrl + kHalfWidth, static_cast<int8_t>(ctrl_t::kEmpty),
              kHalfWidth);
  // Fill the second half of the mirrored control bytes with kEmpty.
  std::memset(new_ctrl + new_capacity + kHalfWidth,
              static_cast<int8_t>(ctrl_t::kEmpty), kHalfWidth);
  // Copy the first half of the non-mirrored control bytes.
  absl::little_endian::Store64(new_ctrl, copied_bytes);
  new_ctrl[new_capacity] = ctrl_t::kSentinel;
  // Copy the first half of the mirrored control bytes.
  absl::little_endian::Store64(new_ctrl + new_capacity + 1, copied_bytes);

  // Example for growth capacity 1->3:
  // old_ctrl =                  0S0EEEEEEEEEEEEEE
  // new_ctrl at the end =       E0ESE0EEEEEEEEEEEEE
  //                                    >!
  // new_ctrl after 1st memset = ????????EEEEEEEE???
  //                                       >!
  // new_ctrl after 2nd memset = ????????EEEEEEEEEEE
  //                            >!
  // new_ctrl after 1st store =  E0EEEEEEEEEEEEEEEEE
  // new_ctrl after kSentinel =  E0ESEEEEEEEEEEEEEEE
  //                                >!
  // new_ctrl after 2nd store =  E0ESE0EEEEEEEEEEEEE

  // Example for growth capacity 3->7:
  // old_ctrl =                  012S012EEEEEEEEEEEE
  // new_ctrl at the end =       E012EEESE012EEEEEEEEEEE
  //                                    >!
  // new_ctrl after 1st memset = ????????EEEEEEEE???????
  //                                           >!
  // new_ctrl after 2nd memset = ????????EEEEEEEEEEEEEEE
  //                            >!
  // new_ctrl after 1st store =  E012EEEEEEEEEEEEEEEEEEE
  // new_ctrl after kSentinel =  E012EEESEEEEEEEEEEEEEEE
  //                                >!
  // new_ctrl after 2nd store =  E012EEESE012EEEEEEEEEEE


  // Example for growth capacity 7->15:
  // old_ctrl =                  0123456S0123456EEEEEEEE
  // new_ctrl at the end =       E0123456EEEEEEESE0123456EEEEEEE
  //                                    >!
  // new_ctrl after 1st memset = ????????EEEEEEEE???????????????
  //                                                   >!
  // new_ctrl after 2nd memset = ????????EEEEEEEE???????EEEEEEEE
  //                            >!
  // new_ctrl after 1st store =  E0123456EEEEEEEE???????EEEEEEEE
  // new_ctrl after kSentinel =  E0123456EEEEEEES???????EEEEEEEE
  //                                            >!
  // new_ctrl after 2nd store =  E0123456EEEEEEESE0123456EEEEEEE
}

void HashSetResizeHelper::InitControlBytesAfterSoo(ctrl_t* new_ctrl, ctrl_t h2,
                                                   size_t new_capacity) {
  assert(is_single_group(new_capacity));
  std::memset(new_ctrl, static_cast<int8_t>(ctrl_t::kEmpty),
              NumControlBytes(new_capacity));
  assert(HashSetResizeHelper::SooSlotIndex() == 1);
  // This allows us to avoid branching on had_soo_slot_.
  assert(had_soo_slot_ || h2 == ctrl_t::kEmpty);
  new_ctrl[1] = new_ctrl[new_capacity + 2] = h2;
  new_ctrl[new_capacity] = ctrl_t::kSentinel;
}

void HashSetResizeHelper::GrowIntoSingleGroupShuffleTransferableSlots(
    void* new_slots, size_t slot_size) const {
  ABSL_ASSUME(old_capacity_ > 0);
  SanitizerUnpoisonMemoryRegion(old_slots(), slot_size * old_capacity_);
  std::memcpy(SlotAddress(new_slots, 1, slot_size), old_slots(),
              slot_size * old_capacity_);
}

void HashSetResizeHelper::GrowSizeIntoSingleGroupTransferable(
    CommonFields& c, size_t slot_size) {
  assert(old_capacity_ < Group::kWidth / 2);
  assert(is_single_group(c.capacity()));
  assert(IsGrowingIntoSingleGroupApplicable(old_capacity_, c.capacity()));

  GrowIntoSingleGroupShuffleControlBytes(c.control(), c.capacity());
  GrowIntoSingleGroupShuffleTransferableSlots(c.slot_array(), slot_size);

  // We poison since GrowIntoSingleGroupShuffleTransferableSlots
  // may leave empty slots unpoisoned.
  PoisonSingleGroupEmptySlots(c, slot_size);
}

void HashSetResizeHelper::TransferSlotAfterSoo(CommonFields& c,
                                               size_t slot_size) {
  assert(was_soo_);
  assert(had_soo_slot_);
  assert(is_single_group(c.capacity()));
  std::memcpy(SlotAddress(c.slot_array(), SooSlotIndex(), slot_size),
              old_soo_data(), slot_size);
  PoisonSingleGroupEmptySlots(c, slot_size);
}

namespace {

// Called whenever the table needs to vacate empty slots either by removing
// tombstones via rehash or growth.
ABSL_ATTRIBUTE_NOINLINE
FindInfo FindInsertPositionWithGrowthOrRehash(CommonFields& common, size_t hash,
                                              const PolicyFunctions& policy) {
  const size_t cap = common.capacity();
  if (cap > Group::kWidth &&
      // Do these calculations in 64-bit to avoid overflow.
      common.size() * uint64_t{32} <= cap * uint64_t{25}) {
    // Squash DELETED without growing if there is enough capacity.
    //
    // Rehash in place if the current size is <= 25/32 of capacity.
    // Rationale for such a high factor: 1) DropDeletesWithoutResize() is
    // faster than resize, and 2) it takes quite a bit of work to add
    // tombstones.  In the worst case, seems to take approximately 4
    // insert/erase pairs to create a single tombstone and so if we are
    // rehashing because of tombstones, we can afford to rehash-in-place as
    // long as we are reclaiming at least 1/8 the capacity without doing more
    // than 2X the work.  (Where "work" is defined to be size() for rehashing
    // or rehashing in place, and 1 for an insert or erase.)  But rehashing in
    // place is faster per operation than inserting or even doubling the size
    // of the table, so we actually afford to reclaim even less space from a
    // resize-in-place.  The decision is to rehash in place if we can reclaim
    // at about 1/8th of the usable capacity (specifically 3/28 of the
    // capacity) which means that the total cost of rehashing will be a small
    // fraction of the total work.
    //
    // Here is output of an experiment using the BM_CacheInSteadyState
    // benchmark running the old case (where we rehash-in-place only if we can
    // reclaim at least 7/16*capacity) vs. this code (which rehashes in place
    // if we can recover 3/32*capacity).
    //
    // Note that although in the worst-case number of rehashes jumped up from
    // 15 to 190, but the number of operations per second is almost the same.
    //
    // Abridged output of running BM_CacheInSteadyState benchmark from
    // raw_hash_set_benchmark.   N is the number of insert/erase operations.
    //
    //      | OLD (recover >= 7/16        | NEW (recover >= 3/32)
    // size |    N/s LoadFactor NRehashes |    N/s LoadFactor NRehashes
    //  448 | 145284       0.44        18 | 140118       0.44        19
    //  493 | 152546       0.24        11 | 151417       0.48        28
    //  538 | 151439       0.26        11 | 151152       0.53        38
    //  583 | 151765       0.28        11 | 150572       0.57        50
    //  628 | 150241       0.31        11 | 150853       0.61        66
    //  672 | 149602       0.33        12 | 150110       0.66        90
    //  717 | 149998       0.35        12 | 149531       0.70       129
    //  762 | 149836       0.37        13 | 148559       0.74       190
    //  807 | 149736       0.39        14 | 151107       0.39        14
    //  852 | 150204       0.42        15 | 151019       0.42        15
    DropDeletesWithoutResize(common, policy);
  } else {
    // Otherwise grow the container.
    policy.resize(common, NextCapacity(cap), HashtablezInfoHandle{});
  }
  // This function is typically called with tables containing deleted slots.
  // The table will be big and `FindFirstNonFullAfterResize` will always
  // fallback to `find_first_non_full`. So using `find_first_non_full` directly.
  return find_first_non_full(common, hash);
}

}  // namespace

const void* GetHashRefForEmptyHasher(const CommonFields& common) {
  // Empty base optimization typically make the empty base class address to be
  // the same as the first address of the derived class object.
  // But we generally assume that for empty hasher we can return any valid
  // pointer.
  return &common;
}

FindInfo HashSetResizeHelper::FindFirstNonFullAfterResize(const CommonFields& c,
                                                          size_t old_capacity,
                                                          size_t hash) {
  size_t new_capacity = c.capacity();
  if (!IsGrowingIntoSingleGroupApplicable(old_capacity, new_capacity)) {
    return find_first_non_full(c, hash);
  }

  // We put the new element either at the beginning or at the end of the table
  // with approximately equal probability.
  size_t offset =
      SingleGroupTableH1(hash, c.control()) & 1 ? 0 : new_capacity - 1;

  assert(IsEmpty(c.control()[offset]));
  return FindInfo{offset, 0};
}

size_t PrepareInsertNonSoo(CommonFields& common, size_t hash, FindInfo target,
                           const PolicyFunctions& policy) {
  // When there are no deleted slots in the table
  // and growth_left is positive, we can insert at the first
  // empty slot in the probe sequence (target).
  const bool use_target_hint =
      // Optimization is disabled when generations are enabled.
      // We have to rehash even sparse tables randomly in such mode.
      !SwisstableGenerationsEnabled() &&
      common.growth_info().HasNoDeletedAndGrowthLeft();
  if (ABSL_PREDICT_FALSE(!use_target_hint)) {
    // Notes about optimized mode when generations are disabled:
    // We do not enter this branch if table has no deleted slots
    // and growth_left is positive.
    // We enter this branch in the following cases listed in decreasing
    // frequency:
    // 1. Table without deleted slots (>95% cases) that needs to be resized.
    // 2. Table with deleted slots that has space for the inserting element.
    // 3. Table with deleted slots that needs to be rehashed or resized.
    if (ABSL_PREDICT_TRUE(common.growth_info().HasNoGrowthLeftAndNoDeleted())) {
      const size_t old_capacity = common.capacity();
      policy.resize(common, NextCapacity(old_capacity), HashtablezInfoHandle{});
      target = HashSetResizeHelper::FindFirstNonFullAfterResize(
          common, old_capacity, hash);
    } else {
      // Note: the table may have no deleted slots here when generations
      // are enabled.
      const bool rehash_for_bug_detection =
          common.should_rehash_for_bug_detection_on_insert();
      if (rehash_for_bug_detection) {
        // Move to a different heap allocation in order to detect bugs.
        const size_t cap = common.capacity();
        policy.resize(common,
                      common.growth_left() > 0 ? cap : NextCapacity(cap),
                      HashtablezInfoHandle{});
      }
      if (ABSL_PREDICT_TRUE(common.growth_left() > 0)) {
        target = find_first_non_full(common, hash);
      } else {
        target = FindInsertPositionWithGrowthOrRehash(common, hash, policy);
      }
    }
  }
  PrepareInsertCommon(common);
  common.growth_info().OverwriteControlAsFull(common.control()[target.offset]);
  SetCtrl(common, target.offset, H2(hash), policy.slot_size);
  common.infoz().RecordInsert(hash, target.probe_length);
  return target.offset;
}

void HashTableSizeOverflow() {
  ABSL_RAW_LOG(FATAL, "Hash table size overflow");
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
