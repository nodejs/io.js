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
#include "absl/container/internal/container_memory.h"
#include "absl/hash/hash.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Represents a control byte corresponding to a full slot with arbitrary hash.
constexpr ctrl_t ZeroCtrlT() { return static_cast<ctrl_t>(0); }

// We have space for `growth_left` before a single block of control bytes. A
// single block of empty control bytes for tables without any slots allocated.
// This enables removing a branch in the hot path of find(). In order to ensure
// that the control bytes are aligned to 16, we have 16 bytes before the control
// bytes even though growth_left only needs 8.
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
  common.set_growth_left(common.growth_left() - 1);
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

void DropDeletesWithoutResize(CommonFields& common, const void* hash_fn,
                              const PolicyFunctions& policy, void* tmp_space) {
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
  auto hasher = policy.hash_slot;
  auto transfer = policy.transfer;
  const size_t slot_size = policy.slot_size;

  size_t total_probe_length = 0;
  void* slot_ptr = SlotAddress(slot_array, 0, slot_size);
  for (size_t i = 0; i != capacity;
       ++i, slot_ptr = NextSlot(slot_ptr, slot_size)) {
    assert(slot_ptr == SlotAddress(slot_array, i, slot_size));
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
    } else {
      assert(IsDeleted(ctrl[new_i]));
      SetCtrl(common, new_i, H2(hash), slot_size);
      // Until we are done rehashing, DELETED marks previously FULL slots.

      // Swap i and new_i elements.
      (*transfer)(set, tmp_space, new_slot_ptr);
      (*transfer)(set, new_slot_ptr, slot_ptr);
      (*transfer)(set, slot_ptr, tmp_space);

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

void EraseMetaOnly(CommonFields& c, size_t index, size_t slot_size) {
  assert(IsFull(c.control()[index]) && "erasing a dangling iterator");
  c.decrement_size();
  c.infoz().RecordErase();

  if (WasNeverFull(c, index)) {
    SetCtrl(c, index, ctrl_t::kEmpty, slot_size);
    c.set_growth_left(c.growth_left() + 1);
    return;
  }

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
    ctrl_t* new_ctrl, size_t new_capacity) const {
  assert(is_single_group(new_capacity));
  constexpr size_t kHalfWidth = Group::kWidth / 2;
  assert(old_capacity_ < kHalfWidth);

  const size_t half_old_capacity = old_capacity_ / 2;

  // NOTE: operations are done with compile time known size = kHalfWidth.
  // Compiler optimizes that into single ASM operation.

  // Copy second half of bytes to the beginning.
  // We potentially copy more bytes in order to have compile time known size.
  // Mirrored bytes from the old_ctrl() will also be copied.
  // In case of old_capacity_ == 3, we will copy 1st element twice.
  // Examples:
  // old_ctrl = 0S0EEEEEEE...
  // new_ctrl = S0EEEEEEEE...
  //
  // old_ctrl = 01S01EEEEE...
  // new_ctrl = 1S01EEEEEE...
  //
  // old_ctrl = 0123456S0123456EE...
  // new_ctrl = 456S0123?????????...
  std::memcpy(new_ctrl, old_ctrl() + half_old_capacity + 1, kHalfWidth);
  // Clean up copied kSentinel from old_ctrl.
  new_ctrl[half_old_capacity] = ctrl_t::kEmpty;

  // Clean up damaged or uninitialized bytes.

  // Clean bytes after the intended size of the copy.
  // Example:
  // new_ctrl = 1E01EEEEEEE????
  // *new_ctrl= 1E0EEEEEEEE????
  // position      /
  std::memset(new_ctrl + old_capacity_ + 1, static_cast<int8_t>(ctrl_t::kEmpty),
              kHalfWidth);
  // Clean non-mirrored bytes that are not initialized.
  // For small old_capacity that may be inside of mirrored bytes zone.
  // Examples:
  // new_ctrl = 1E0EEEEEEEE??????????....
  // *new_ctrl= 1E0EEEEEEEEEEEEE?????....
  // position           /
  //
  // new_ctrl = 456E0123???????????...
  // *new_ctrl= 456E0123EEEEEEEE???...
  // position           /
  std::memset(new_ctrl + kHalfWidth, static_cast<int8_t>(ctrl_t::kEmpty),
              kHalfWidth);
  // Clean last mirrored bytes that are not initialized
  // and will not be overwritten by mirroring.
  // Examples:
  // new_ctrl = 1E0EEEEEEEEEEEEE????????
  // *new_ctrl= 1E0EEEEEEEEEEEEEEEEEEEEE
  // position           S       /
  //
  // new_ctrl = 456E0123EEEEEEEE???????????????
  // *new_ctrl= 456E0123EEEEEEEE???????EEEEEEEE
  // position                  S       /
  std::memset(new_ctrl + new_capacity + kHalfWidth,
              static_cast<int8_t>(ctrl_t::kEmpty), kHalfWidth);

  // Create mirrored bytes. old_capacity_ < kHalfWidth
  // Example:
  // new_ctrl = 456E0123EEEEEEEE???????EEEEEEEE
  // *new_ctrl= 456E0123EEEEEEEE456E0123EEEEEEE
  // position                  S/
  ctrl_t g[kHalfWidth];
  std::memcpy(g, new_ctrl, kHalfWidth);
  std::memcpy(new_ctrl + new_capacity + 1, g, kHalfWidth);

  // Finally set sentinel to its place.
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

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
