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
#include "absl/base/optimization.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hashtablez_sampler.h"
#include "absl/hash/hash.h"

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
  const size_t offset = H1(hash, common.control()) & 2;
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
    c = soo_enabled ? CommonFields{soo_tag_t{}} : CommonFields{};
  }
}

void HashSetResizeHelper::GrowIntoSingleGroupShuffleControlBytes(
    ctrl_t* __restrict new_ctrl, size_t new_capacity) const {
  assert(is_single_group(new_capacity));
  constexpr size_t kHalfWidth = Group::kWidth / 2;
  constexpr size_t kQuarterWidth = Group::kWidth / 4;
  assert(old_capacity_ < kHalfWidth);
  static_assert(sizeof(uint64_t) >= kHalfWidth,
                "Group size is too large. The ctrl bytes for half a group must "
                "fit into a uint64_t for this implementation.");
  static_assert(sizeof(uint64_t) <= Group::kWidth,
                "Group size is too small. The ctrl bytes for a group must "
                "cover a uint64_t for this implementation.");

  const size_t half_old_capacity = old_capacity_ / 2;

  // NOTE: operations are done with compile time known size = kHalfWidth.
  // Compiler optimizes that into single ASM operation.

  // Load the bytes from half_old_capacity + 1. This contains the last half of
  // old_ctrl bytes, followed by the sentinel byte, and then the first half of
  // the cloned bytes. This effectively shuffles the control bytes.
  uint64_t copied_bytes = 0;
  copied_bytes =
      absl::little_endian::Load64(old_ctrl() + half_old_capacity + 1);

  // We change the sentinel byte to kEmpty before storing to both the start of
  // the new_ctrl, and past the end of the new_ctrl later for the new cloned
  // bytes. Note that this is faster than setting the sentinel byte to kEmpty
  // after the copy directly in new_ctrl because we are limited on store
  // bandwidth.
  constexpr uint64_t kEmptyXorSentinel =
      static_cast<uint8_t>(ctrl_t::kEmpty) ^
      static_cast<uint8_t>(ctrl_t::kSentinel);
  const uint64_t mask_convert_old_sentinel_to_empty =
      kEmptyXorSentinel << (half_old_capacity * 8);
  copied_bytes ^= mask_convert_old_sentinel_to_empty;

  // Copy second half of bytes to the beginning. This correctly sets the bytes
  // [0, old_capacity]. We potentially copy more bytes in order to have compile
  // time known size. Mirrored bytes from the old_ctrl() will also be copied. In
  // case of old_capacity_ == 3, we will copy 1st element twice.
  // Examples:
  // (old capacity = 1)
  // old_ctrl = 0S0EEEEEEE...
  // new_ctrl = E0EEEEEE??...
  //
  // (old capacity = 3)
  // old_ctrl = 012S012EEEEE...
  // new_ctrl = 12E012EE????...
  //
  // (old capacity = 7)
  // old_ctrl = 0123456S0123456EE...
  // new_ctrl = 456E0123?????????...
  absl::little_endian::Store64(new_ctrl, copied_bytes);

  // Set the space [old_capacity + 1, new_capacity] to empty as these bytes will
  // not be written again. This is safe because
  // NumControlBytes = new_capacity + kWidth and new_capacity >=
  // old_capacity+1.
  // Examples:
  // (old_capacity = 3, new_capacity = 15)
  // new_ctrl  = 12E012EE?????????????...??
  // *new_ctrl = 12E0EEEEEEEEEEEEEEEE?...??
  // position        /          S
  //
  // (old_capacity = 7, new_capacity = 15)
  // new_ctrl  = 456E0123?????????????????...??
  // *new_ctrl = 456E0123EEEEEEEEEEEEEEEE?...??
  // position            /      S
  std::memset(new_ctrl + old_capacity_ + 1, static_cast<int8_t>(ctrl_t::kEmpty),
              Group::kWidth);

  // Set the last kHalfWidth bytes to empty, to ensure the bytes all the way to
  // the end are initialized.
  // Examples:
  // new_ctrl  = 12E0EEEEEEEEEEEEEEEE?...???????
  // *new_ctrl = 12E0EEEEEEEEEEEEEEEE???EEEEEEEE
  // position                   S       /
  //
  // new_ctrl  = 456E0123EEEEEEEEEEEEEEEE???????
  // *new_ctrl = 456E0123EEEEEEEEEEEEEEEEEEEEEEE
  // position                   S       /
  std::memset(new_ctrl + NumControlBytes(new_capacity) - kHalfWidth,
              static_cast<int8_t>(ctrl_t::kEmpty), kHalfWidth);

  // Copy the first bytes to the end (starting at new_capacity +1) to set the
  // cloned bytes. Note that we use the already copied bytes from old_ctrl here
  // rather than copying from new_ctrl to avoid a Read-after-Write hazard, since
  // new_ctrl was just written to. The first old_capacity-1 bytes are set
  // correctly. Then there may be up to old_capacity bytes that need to be
  // overwritten, and any remaining bytes will be correctly set to empty. This
  // sets [new_capacity + 1, new_capacity +1 + old_capacity] correctly.
  // Examples:
  // new_ctrl  = 12E0EEEEEEEEEEEEEEEE?...???????
  // *new_ctrl = 12E0EEEEEEEEEEEE12E012EEEEEEEEE
  // position                   S/
  //
  // new_ctrl  = 456E0123EEEEEEEE?...???EEEEEEEE
  // *new_ctrl = 456E0123EEEEEEEE456E0123EEEEEEE
  // position                   S/
  absl::little_endian::Store64(new_ctrl + new_capacity + 1, copied_bytes);

  // Set The remaining bytes at the end past the cloned bytes to empty. The
  // incorrectly set bytes are [new_capacity + old_capacity + 2,
  // min(new_capacity + 1 + kHalfWidth, new_capacity + old_capacity + 2 +
  // half_old_capacity)]. Taking the difference, we need to set min(kHalfWidth -
  // (old_capacity + 1), half_old_capacity)]. Since old_capacity < kHalfWidth,
  // half_old_capacity < kQuarterWidth, so we set kQuarterWidth beginning at
  // new_capacity + old_capacity + 2 to kEmpty.
  // Examples:
  // new_ctrl  = 12E0EEEEEEEEEEEE12E012EEEEEEEEE
  // *new_ctrl = 12E0EEEEEEEEEEEE12E0EEEEEEEEEEE
  // position                   S    /
  //
  // new_ctrl  = 456E0123EEEEEEEE456E0123EEEEEEE
  // *new_ctrl = 456E0123EEEEEEEE456E0123EEEEEEE (no change)
  // position                   S        /
  std::memset(new_ctrl + new_capacity + old_capacity_ + 2,
              static_cast<int8_t>(ctrl_t::kEmpty), kQuarterWidth);

  // Finally, we set the new sentinel byte.
  new_ctrl[new_capacity] = ctrl_t::kSentinel;
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
  assert(old_capacity_ > 0);
  const size_t half_old_capacity = old_capacity_ / 2;

  SanitizerUnpoisonMemoryRegion(old_slots(), slot_size * old_capacity_);
  std::memcpy(new_slots,
              SlotAddress(old_slots(), half_old_capacity + 1, slot_size),
              slot_size * half_old_capacity);
  std::memcpy(SlotAddress(new_slots, half_old_capacity + 1, slot_size),
              old_slots(), slot_size * (half_old_capacity + 1));
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

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
