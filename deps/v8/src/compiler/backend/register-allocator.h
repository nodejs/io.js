// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_H_
#define V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_H_

#include "src/base/bits.h"
#include "src/base/compiler-specific.h"
#include "src/codegen/register-configuration.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocation.h"
#include "src/flags/flags.h"
#include "src/utils/ostreams.h"
#include "src/utils/sparse-bit-vector.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

static const int32_t kUnassignedRegister = RegisterConfiguration::kMaxRegisters;

// This class represents a single point of a InstructionOperand's lifetime. For
// each instruction there are four lifetime positions:
//
//   [[START, END], [START, END]]
//
// Where the first half position corresponds to
//
//  [GapPosition::START, GapPosition::END]
//
// and the second half position corresponds to
//
//  [Lifetime::USED_AT_START, Lifetime::USED_AT_END]
//
class LifetimePosition final {
 public:
  // Return the lifetime position that corresponds to the beginning of
  // the gap with the given index.
  static LifetimePosition GapFromInstructionIndex(int index) {
    return LifetimePosition(index * kStep);
  }
  // Return the lifetime position that corresponds to the beginning of
  // the instruction with the given index.
  static LifetimePosition InstructionFromInstructionIndex(int index) {
    return LifetimePosition(index * kStep + kHalfStep);
  }

  static bool ExistsGapPositionBetween(LifetimePosition pos1,
                                       LifetimePosition pos2) {
    if (pos1 > pos2) std::swap(pos1, pos2);
    LifetimePosition next(pos1.value_ + 1);
    if (next.IsGapPosition()) return next < pos2;
    return next.NextFullStart() < pos2;
  }

  // Returns a numeric representation of this lifetime position.
  int value() const { return value_; }

  // Returns the index of the instruction to which this lifetime position
  // corresponds.
  int ToInstructionIndex() const {
    DCHECK(IsValid());
    return value_ / kStep;
  }

  // Returns true if this lifetime position corresponds to a START value
  bool IsStart() const { return (value_ & (kHalfStep - 1)) == 0; }
  // Returns true if this lifetime position corresponds to an END value
  bool IsEnd() const { return (value_ & (kHalfStep - 1)) == 1; }
  // Returns true if this lifetime position corresponds to a gap START value
  bool IsFullStart() const { return (value_ & (kStep - 1)) == 0; }

  bool IsGapPosition() const { return (value_ & 0x2) == 0; }
  bool IsInstructionPosition() const { return !IsGapPosition(); }

  // Returns the lifetime position for the current START.
  LifetimePosition Start() const {
    DCHECK(IsValid());
    return LifetimePosition(value_ & ~(kHalfStep - 1));
  }

  // Returns the lifetime position for the current gap START.
  LifetimePosition FullStart() const {
    DCHECK(IsValid());
    return LifetimePosition(value_ & ~(kStep - 1));
  }

  // Returns the lifetime position for the current END.
  LifetimePosition End() const {
    DCHECK(IsValid());
    return LifetimePosition(Start().value_ + kHalfStep / 2);
  }

  // Returns the lifetime position for the beginning of the next START.
  LifetimePosition NextStart() const {
    DCHECK(IsValid());
    return LifetimePosition(Start().value_ + kHalfStep);
  }

  // Returns the lifetime position for the beginning of the next gap START.
  LifetimePosition NextFullStart() const {
    DCHECK(IsValid());
    return LifetimePosition(FullStart().value_ + kStep);
  }

  // Returns the lifetime position for the beginning of the previous START.
  LifetimePosition PrevStart() const {
    DCHECK(IsValid());
    DCHECK_LE(kHalfStep, value_);
    return LifetimePosition(Start().value_ - kHalfStep);
  }

  // Constructs the lifetime position which does not correspond to any
  // instruction.
  LifetimePosition() : value_(-1) {}

  // Returns true if this lifetime positions corrensponds to some
  // instruction.
  bool IsValid() const { return value_ != -1; }

  bool operator<(const LifetimePosition& that) const {
    return this->value_ < that.value_;
  }

  bool operator<=(const LifetimePosition& that) const {
    return this->value_ <= that.value_;
  }

  bool operator==(const LifetimePosition& that) const {
    return this->value_ == that.value_;
  }

  bool operator!=(const LifetimePosition& that) const {
    return this->value_ != that.value_;
  }

  bool operator>(const LifetimePosition& that) const {
    return this->value_ > that.value_;
  }

  bool operator>=(const LifetimePosition& that) const {
    return this->value_ >= that.value_;
  }

  // APIs to aid debugging. For general-stream APIs, use operator<<.
  void Print() const;

  static inline LifetimePosition Invalid() { return LifetimePosition(); }

  static inline LifetimePosition MaxPosition() {
    // We have to use this kind of getter instead of static member due to
    // crash bug in GDB.
    return LifetimePosition(kMaxInt);
  }

  static inline LifetimePosition FromInt(int value) {
    return LifetimePosition(value);
  }

 private:
  static const int kHalfStep = 2;
  static const int kStep = 2 * kHalfStep;

  static_assert(base::bits::IsPowerOfTwo(kHalfStep),
                "Code relies on kStep and kHalfStep being a power of two");

  explicit LifetimePosition(int value) : value_(value) {}

  int value_;
};

inline std::ostream& operator<<(std::ostream& os, const LifetimePosition pos) {
  os << '@' << pos.ToInstructionIndex();
  if (pos.IsGapPosition()) {
    os << 'g';
  } else {
    os << 'i';
  }
  if (pos.IsStart()) {
    os << 's';
  } else {
    os << 'e';
  }
  return os;
}

class SpillRange;
class LiveRange;
class TopLevelLiveRange;

class RegisterAllocationData final : public ZoneObject {
 public:
  RegisterAllocationData(const RegisterAllocationData&) = delete;
  RegisterAllocationData& operator=(const RegisterAllocationData&) = delete;

  // Encodes whether a spill happens in deferred code (kSpillDeferred) or
  // regular code (kSpillAtDefinition).
  enum SpillMode { kSpillAtDefinition, kSpillDeferred };

  static constexpr int kNumberOfFixedRangesPerRegister = 2;

  class PhiMapValue : public ZoneObject {
   public:
    PhiMapValue(PhiInstruction* phi, const InstructionBlock* block, Zone* zone);

    const PhiInstruction* phi() const { return phi_; }
    const InstructionBlock* block() const { return block_; }

    // For hinting.
    int assigned_register() const { return assigned_register_; }
    void set_assigned_register(int register_code) {
      DCHECK_EQ(assigned_register_, kUnassignedRegister);
      assigned_register_ = register_code;
    }
    void UnsetAssignedRegister() { assigned_register_ = kUnassignedRegister; }

    void AddOperand(InstructionOperand* operand);
    void CommitAssignment(const InstructionOperand& operand);

   private:
    PhiInstruction* const phi_;
    const InstructionBlock* const block_;
    ZoneVector<InstructionOperand*> incoming_operands_;
    int assigned_register_;
  };
  using PhiMap = ZoneMap<int, PhiMapValue*>;

  struct DelayedReference {
    ReferenceMap* map;
    InstructionOperand* operand;
  };
  using DelayedReferences = ZoneVector<DelayedReference>;
  using RangesWithPreassignedSlots =
      ZoneVector<std::pair<TopLevelLiveRange*, int>>;

  RegisterAllocationData(const RegisterConfiguration* config,
                         Zone* allocation_zone, Frame* frame,
                         InstructionSequence* code, TickCounter* tick_counter,
                         const char* debug_name = nullptr);

  const ZoneVector<TopLevelLiveRange*>& live_ranges() const {
    return live_ranges_;
  }
  ZoneVector<TopLevelLiveRange*>& live_ranges() { return live_ranges_; }
  const ZoneVector<TopLevelLiveRange*>& fixed_live_ranges() const {
    return fixed_live_ranges_;
  }
  ZoneVector<TopLevelLiveRange*>& fixed_live_ranges() {
    return fixed_live_ranges_;
  }
  ZoneVector<TopLevelLiveRange*>& fixed_float_live_ranges() {
    return fixed_float_live_ranges_;
  }
  const ZoneVector<TopLevelLiveRange*>& fixed_float_live_ranges() const {
    return fixed_float_live_ranges_;
  }
  ZoneVector<TopLevelLiveRange*>& fixed_double_live_ranges() {
    return fixed_double_live_ranges_;
  }
  const ZoneVector<TopLevelLiveRange*>& fixed_double_live_ranges() const {
    return fixed_double_live_ranges_;
  }
  ZoneVector<TopLevelLiveRange*>& fixed_simd128_live_ranges() {
    return fixed_simd128_live_ranges_;
  }
  const ZoneVector<TopLevelLiveRange*>& fixed_simd128_live_ranges() const {
    return fixed_simd128_live_ranges_;
  }
  ZoneVector<SparseBitVector*>& live_in_sets() { return live_in_sets_; }
  ZoneVector<SparseBitVector*>& live_out_sets() { return live_out_sets_; }
  DelayedReferences& delayed_references() { return delayed_references_; }
  InstructionSequence* code() const { return code_; }
  // This zone is for data structures only needed during register allocation
  // phases.
  Zone* allocation_zone() const { return allocation_zone_; }
  // This zone is for InstructionOperands and moves that live beyond register
  // allocation.
  Zone* code_zone() const { return code()->zone(); }
  Frame* frame() const { return frame_; }
  const char* debug_name() const { return debug_name_; }
  const RegisterConfiguration* config() const { return config_; }

  MachineRepresentation RepresentationFor(int virtual_register);

  TopLevelLiveRange* GetLiveRangeFor(int index);
  // Creates a new live range.
  TopLevelLiveRange* NewLiveRange(int index, MachineRepresentation rep);

  SpillRange* AssignSpillRangeToLiveRange(TopLevelLiveRange* range,
                                          SpillMode spill_mode);
  SpillRange* CreateSpillRangeForLiveRange(TopLevelLiveRange* range);

  MoveOperands* AddGapMove(int index, Instruction::GapPosition position,
                           const InstructionOperand& from,
                           const InstructionOperand& to);

  bool ExistsUseWithoutDefinition();
  bool RangesDefinedInDeferredStayInDeferred();

  void MarkFixedUse(MachineRepresentation rep, int index);
  bool HasFixedUse(MachineRepresentation rep, int index);

  void MarkAllocated(MachineRepresentation rep, int index);

  PhiMapValue* InitializePhiMap(const InstructionBlock* block,
                                PhiInstruction* phi);
  PhiMapValue* GetPhiMapValueFor(TopLevelLiveRange* top_range);
  PhiMapValue* GetPhiMapValueFor(int virtual_register);
  bool IsBlockBoundary(LifetimePosition pos) const;

  RangesWithPreassignedSlots& preassigned_slot_ranges() {
    return preassigned_slot_ranges_;
  }

  void RememberSpillState(RpoNumber block,
                          const ZoneVector<LiveRange*>& state) {
    spill_state_[block.ToSize()] = state;
  }

  ZoneVector<LiveRange*>& GetSpillState(RpoNumber block) {
    auto& result = spill_state_[block.ToSize()];
    return result;
  }

  void ResetSpillState() {
    for (auto& state : spill_state_) {
      state.clear();
    }
  }

  TickCounter* tick_counter() { return tick_counter_; }

  ZoneMap<TopLevelLiveRange*, AllocatedOperand*>& slot_for_const_range() {
    return slot_for_const_range_;
  }

 private:
  Zone* const allocation_zone_;
  Frame* const frame_;
  InstructionSequence* const code_;
  const char* const debug_name_;
  const RegisterConfiguration* const config_;
  PhiMap phi_map_;
  ZoneVector<SparseBitVector*> live_in_sets_;
  ZoneVector<SparseBitVector*> live_out_sets_;
  ZoneVector<TopLevelLiveRange*> live_ranges_;
  ZoneVector<TopLevelLiveRange*> fixed_live_ranges_;
  ZoneVector<TopLevelLiveRange*> fixed_float_live_ranges_;
  ZoneVector<TopLevelLiveRange*> fixed_double_live_ranges_;
  ZoneVector<TopLevelLiveRange*> fixed_simd128_live_ranges_;
  DelayedReferences delayed_references_;
  BitVector* assigned_registers_;
  BitVector* assigned_double_registers_;
  BitVector* assigned_simd128_registers_;
  BitVector* fixed_register_use_;
  BitVector* fixed_fp_register_use_;
  BitVector* fixed_simd128_register_use_;
  int virtual_register_count_;
  RangesWithPreassignedSlots preassigned_slot_ranges_;
  ZoneVector<ZoneVector<LiveRange*>> spill_state_;
  TickCounter* const tick_counter_;
  ZoneMap<TopLevelLiveRange*, AllocatedOperand*> slot_for_const_range_;
};

// Representation of the non-empty interval [start,end[.
// This is a value class given that it only contains two (32-bit) positions.
class UseInterval final {
 public:
  UseInterval(LifetimePosition start, LifetimePosition end)
      : start_(start), end_(end) {
    DCHECK_LT(start, end);
  }

  LifetimePosition start() const { return start_; }
  void set_start(LifetimePosition start) {
    DCHECK_LT(start, end_);
    start_ = start;
  }
  LifetimePosition end() const { return end_; }
  void set_end(LifetimePosition end) {
    DCHECK_LT(start_, end);
    end_ = end;
  }

  // Split this interval at the given position without effecting the
  // live range that owns it. The interval must contain the position.
  UseInterval SplitAt(LifetimePosition pos) {
    DCHECK(Contains(pos) && pos != start());
    UseInterval after(pos, end_);
    end_ = pos;
    return after;
  }

  // If this interval intersects with other return smallest position
  // that belongs to both of them.
  LifetimePosition Intersect(const UseInterval& other) const {
    LifetimePosition intersection_start = std::max(start_, other.start_);
    LifetimePosition intersection_end = std::min(end_, other.end_);
    if (intersection_start < intersection_end) return intersection_start;
    return LifetimePosition::Invalid();
  }

  bool Contains(LifetimePosition point) const {
    return start_ <= point && point < end_;
  }

  // Returns the index of the first gap covered by this interval.
  int FirstGapIndex() const {
    int ret = start_.ToInstructionIndex();
    if (start_.IsInstructionPosition()) {
      ++ret;
    }
    return ret;
  }

  // Returns the index of the last gap covered by this interval.
  int LastGapIndex() const {
    int ret = end_.ToInstructionIndex();
    if (end_.IsGapPosition() && end_.IsStart()) {
      --ret;
    }
    return ret;
  }

  bool operator==(const UseInterval& other) const {
    return std::tie(start_, end_) == std::tie(other.start_, other.end_);
  }
  bool operator!=(const UseInterval& other) const { return !(*this == other); }

  bool operator<(const UseInterval& other) const {
    return start_ < other.start_;
  }

  void PrettyPrint(std::ostream& os) const {
    os << '[' << start() << ", " << end() << ')';
  }

 private:
  LifetimePosition start_;
  LifetimePosition end_;
};

enum class UsePositionType : uint8_t {
  kRegisterOrSlot,
  kRegisterOrSlotOrConstant,
  kRequiresRegister,
  kRequiresSlot
};

enum class UsePositionHintType : uint8_t {
  kNone,
  kOperand,
  kUsePos,
  kPhi,
  kUnresolved
};

// Representation of a use position.
class V8_EXPORT_PRIVATE UsePosition final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  UsePosition(LifetimePosition pos, InstructionOperand* operand, void* hint,
              UsePositionHintType hint_type);
  UsePosition(const UsePosition&) = delete;
  UsePosition& operator=(const UsePosition&) = delete;

  InstructionOperand* operand() const { return operand_; }
  bool HasOperand() const { return operand_ != nullptr; }

  bool RegisterIsBeneficial() const {
    return RegisterBeneficialField::decode(flags_);
  }
  bool SpillDetrimental() const {
    return SpillDetrimentalField::decode(flags_);
  }

  UsePositionType type() const { return TypeField::decode(flags_); }
  void set_type(UsePositionType type, bool register_beneficial);

  LifetimePosition pos() const { return pos_; }

  // For hinting only.
  void set_assigned_register(int register_code) {
    flags_ = AssignedRegisterField::update(flags_, register_code);
  }
  void set_spill_detrimental() {
    flags_ = SpillDetrimentalField::update(flags_, true);
  }

  UsePositionHintType hint_type() const {
    return HintTypeField::decode(flags_);
  }
  bool HasHint() const;
  bool HintRegister(int* register_code) const;
  void SetHint(UsePosition* use_pos);
  void ResolveHint(UsePosition* use_pos);
  bool IsResolved() const {
    return hint_type() != UsePositionHintType::kUnresolved;
  }
  static UsePositionHintType HintTypeForOperand(const InstructionOperand& op);

  struct Ordering {
    bool operator()(const UsePosition* left, const UsePosition* right) const {
      return left->pos() < right->pos();
    }
  };

 private:
  using TypeField = base::BitField<UsePositionType, 0, 2>;
  using HintTypeField = base::BitField<UsePositionHintType, 2, 3>;
  using RegisterBeneficialField = base::BitField<bool, 5, 1>;
  using AssignedRegisterField = base::BitField<int32_t, 6, 6>;
  using SpillDetrimentalField = base::BitField<int32_t, 12, 1>;

  InstructionOperand* const operand_;
  void* hint_;
  LifetimePosition const pos_;
  uint32_t flags_;
};

class SpillRange;
class TopLevelLiveRange;
class LiveRangeBundle;

enum GrowthDirection { kFront, kFrontOrBack };

// A data structure that:
// - Allocates its elements in the Zone.
// - Has O(1) random access.
// - Inserts at the front are O(1) (asymptotically).
// - Can be split efficiently into two halves, and merged again efficiently
//   if those were not modified in the meantime.
// - Has empty storage at the front and back, such that split halves both
//   can perform inserts without reallocating.
template <typename T>
class DoubleEndedSplitVector {
 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;

  // This allows us to skip calling destructors and use simple copies,
  // which is sufficient for the exclusive use here in the register allocator.
  ASSERT_TRIVIALLY_COPYABLE(T);
  static_assert(std::is_trivially_destructible<T>::value);

  size_t size() const { return data_end_ - data_begin_; }
  bool empty() const { return size() == 0; }
  size_t capacity() const { return storage_end_ - storage_begin_; }

  T* data() const { return data_begin_; }

  void clear() { data_begin_ = data_end_; }

  T& operator[](size_t position) {
    DCHECK_LT(position, size());
    return data_begin_[position];
  }
  const T& operator[](size_t position) const {
    DCHECK_LT(position, size());
    return data_begin_[position];
  }

  iterator begin() { return data_begin_; }
  const_iterator begin() const { return data_begin_; }
  iterator end() { return data_end_; }
  const_iterator end() const { return data_end_; }

  T& front() {
    DCHECK(!empty());
    return *begin();
  }
  const T& front() const {
    DCHECK(!empty());
    return *begin();
  }
  T& back() {
    DCHECK(!empty());
    return *std::prev(end());
  }
  const T& back() const {
    DCHECK(!empty());
    return *std::prev(end());
  }

  void push_front(Zone* zone, const T& value) {
    EnsureOneMoreCapacityAt<kFront>(zone);
    --data_begin_;
    *data_begin_ = value;
  }
  void pop_front() {
    DCHECK(!empty());
    ++data_begin_;
  }

  // This can be configured to arrange the data in the middle of the backing
  // store (`kFrontOrBack`, default), or at the end of the backing store, if
  // subsequent inserts are mostly at the front (`kFront`).
  template <GrowthDirection direction = kFrontOrBack>
  iterator insert(Zone* zone, const_iterator position, const T& value) {
    DCHECK_LE(begin(), position);
    DCHECK_LE(position, end());
    size_t old_size = size();

    size_t insert_index = position - data_begin_;
    EnsureOneMoreCapacityAt<direction>(zone);

    // Make space for the insertion.
    // Copy towards the end with more remaining space, such that over time
    // the data is roughly centered, which is beneficial in case of splitting.
    if (direction == kFront || space_at_front() >= space_at_back()) {
      // Copy to the left.
      DCHECK_GT(space_at_front(), 0);
      T* copy_src_begin = data_begin_;
      T* copy_src_end = data_begin_ + insert_index;
      --data_begin_;
      std::copy(copy_src_begin, copy_src_end, data_begin_);
    } else {
      // Copy to the right.
      DCHECK_GT(space_at_back(), 0);
      T* copy_src_begin = data_begin_ + insert_index;
      T* copy_src_end = data_end_;
      ++data_end_;
      std::copy_backward(copy_src_begin, copy_src_end, data_end_);
    }

    T* insert_position = data_begin_ + insert_index;
    *insert_position = value;

#ifdef DEBUG
    Verify();
#endif
    DCHECK_LE(begin(), insert_position);
    DCHECK_LT(insert_position, end());
    DCHECK_EQ(size(), old_size + 1);
    USE(old_size);

    return insert_position;
  }

  // Returns a split-off vector from `split_begin` to `end()`.
  // Afterwards, `this` ends just before `split_begin`.
  // This does not allocate; it instead splits the backing store in two halves.
  DoubleEndedSplitVector<T> SplitAt(const_iterator split_begin_const) {
    iterator split_begin = const_cast<iterator>(split_begin_const);

    DCHECK_LE(data_begin_, split_begin);
    DCHECK_LE(split_begin, data_end_);
    size_t old_size = size();

    // NOTE: The splitted allocation might no longer fulfill alignment
    // requirements by the Zone allocator, so do not delete it!
    DoubleEndedSplitVector split_off;
    split_off.storage_begin_ = split_begin;
    split_off.data_begin_ = split_begin;
    split_off.data_end_ = data_end_;
    split_off.storage_end_ = storage_end_;
    data_end_ = split_begin;
    storage_end_ = split_begin;

#ifdef DEBUG
    Verify();
    split_off.Verify();
#endif
    DCHECK_EQ(size() + split_off.size(), old_size);
    USE(old_size);

    return split_off;
  }

  // Appends the elements from `other` after the end of `this`.
  // In particular if `other` is directly adjacent to `this`, it does not
  // allocate or copy.
  void Append(Zone* zone, DoubleEndedSplitVector<T> other) {
    if (data_end_ == other.data_begin_) {
      // The `other`s elements are directly adjacent to ours, so just extend
      // our storage to encompass them.
      // This could happen if `other` comes from an earlier `this->SplitAt()`.
      // For the usage here in the register allocator, this is always the case.
      DCHECK_EQ(other.storage_begin_, other.data_begin_);
      DCHECK_EQ(data_end_, storage_end_);
      data_end_ = other.data_end_;
      storage_end_ = other.storage_end_;
      return;
    }

    // General case: Copy into newly allocated vector.
    // TODO(dlehmann): One could check if `this` or `other` has enough capacity
    // such that one can avoid the allocation, but currently we never reach
    // this path anyway.
    DoubleEndedSplitVector<T> result;
    size_t merged_size = this->size() + other.size();
    result.GrowAt<kFront>(zone, merged_size);

    result.data_begin_ -= merged_size;
    std::copy(this->begin(), this->end(), result.data_begin_);
    std::copy(other.begin(), other.end(), result.data_begin_ + this->size());
    DCHECK_EQ(result.data_begin_ + merged_size, result.data_end_);

    *this = std::move(result);

#ifdef DEBUG
    Verify();
#endif
    DCHECK_EQ(size(), merged_size);
  }

 private:
  static constexpr size_t kMinCapacity = 2;

  size_t space_at_front() const { return data_begin_ - storage_begin_; }
  size_t space_at_back() const { return storage_end_ - data_end_; }

  template <GrowthDirection direction>
  V8_INLINE void EnsureOneMoreCapacityAt(Zone* zone) {
    if constexpr (direction == kFront) {
      if (V8_LIKELY(space_at_front() > 0)) return;
      GrowAt<kFront>(zone, capacity() * 2);
      DCHECK_GT(space_at_front(), 0);
    } else {
      if (V8_LIKELY(space_at_front() > 0 || space_at_back() > 0)) return;
      GrowAt<kFrontOrBack>(zone, capacity() * 2);
      DCHECK(space_at_front() > 0 || space_at_back() > 0);
    }
  }

  template <GrowthDirection direction>
  V8_NOINLINE V8_PRESERVE_MOST void GrowAt(Zone* zone,
                                           size_t new_minimum_capacity) {
    DoubleEndedSplitVector<T> old = std::move(*this);

    size_t new_capacity = std::max(kMinCapacity, new_minimum_capacity);
    storage_begin_ = zone->AllocateArray<T>(new_capacity);
    storage_end_ = storage_begin_ + new_capacity;

    size_t remaining_capacity = new_capacity - old.size();
    size_t remaining_capacity_front =
        direction == kFront ? remaining_capacity : remaining_capacity / 2;

    data_begin_ = storage_begin_ + remaining_capacity_front;
    data_end_ = data_begin_ + old.size();
    std::copy(old.begin(), old.end(), data_begin_);

#ifdef DEBUG
    Verify();
#endif
    DCHECK_EQ(size(), old.size());
  }

#ifdef DEBUG
  void Verify() const {
    DCHECK_LE(storage_begin_, data_begin_);
    DCHECK_LE(data_begin_, data_end_);
    DCHECK_LE(data_end_, storage_end_);
  }
#endif

  // Do not store a pointer to the `Zone` to save memory when there are very
  // many `LiveRange`s (which each contain this vector twice).
  // It makes the API a bit cumbersome, because the Zone has to be explicitly
  // passed around, but is worth the 1-3% of max zone memory reduction.

  T* storage_begin_ = nullptr;
  T* data_begin_ = nullptr;
  T* data_end_ = nullptr;
  T* storage_end_ = nullptr;
};

using UseIntervalVector = DoubleEndedSplitVector<UseInterval>;
using UsePositionVector = DoubleEndedSplitVector<UsePosition*>;

// Representation of SSA values' live ranges as a collection of (continuous)
// intervals over the instruction ordering.
class V8_EXPORT_PRIVATE LiveRange : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  LiveRange(const LiveRange&) = delete;
  LiveRange& operator=(const LiveRange&) = delete;

  const UseIntervalVector& intervals() const { return intervals_; }
  base::Vector<UsePosition*> positions() const { return positions_span_; }

  TopLevelLiveRange* TopLevel() { return top_level_; }
  const TopLevelLiveRange* TopLevel() const { return top_level_; }

  bool IsTopLevel() const;

  LiveRange* next() const { return next_; }

  int relative_id() const { return relative_id_; }

  bool IsEmpty() const { return intervals_.empty(); }

  InstructionOperand GetAssignedOperand() const;

  MachineRepresentation representation() const {
    return RepresentationField::decode(bits_);
  }

  int assigned_register() const { return AssignedRegisterField::decode(bits_); }
  bool HasRegisterAssigned() const {
    return assigned_register() != kUnassignedRegister;
  }
  void set_assigned_register(int reg);
  void UnsetAssignedRegister();

  bool ShouldRecombine() const { return RecombineField::decode(bits_); }

  void SetRecombine() { bits_ = RecombineField::update(bits_, true); }
  void set_controlflow_hint(int reg) {
    bits_ = ControlFlowRegisterHint::update(bits_, reg);
  }
  int controlflow_hint() const {
    return ControlFlowRegisterHint::decode(bits_);
  }
  bool RegisterFromControlFlow(int* reg) {
    int hint = controlflow_hint();
    if (hint != kUnassignedRegister) {
      *reg = hint;
      return true;
    }
    return false;
  }
  bool spilled() const { return SpilledField::decode(bits_); }
  void AttachToNext(Zone* zone);
  void Unspill();
  void Spill();

  RegisterKind kind() const;

  // Returns use position in this live range that follows both start
  // and last processed use position.
  UsePosition* const* NextUsePosition(LifetimePosition start) const;

  // Returns use position for which register is required in this live
  // range and which follows both start and last processed use position
  UsePosition* NextRegisterPosition(LifetimePosition start) const;

  // Returns use position for which register is beneficial in this live
  // range and which follows both start and last processed use position
  UsePosition* NextUsePositionRegisterIsBeneficial(
      LifetimePosition start) const;

  // Returns lifetime position for which register is beneficial in this live
  // range and which follows both start and last processed use position.
  LifetimePosition NextLifetimePositionRegisterIsBeneficial(
      const LifetimePosition& start) const;

  // Returns use position for which spilling is detrimental in this live
  // range and which follows both start and last processed use position
  UsePosition* NextUsePositionSpillDetrimental(LifetimePosition start) const;

  // Can this live range be spilled at this position.
  bool CanBeSpilled(LifetimePosition pos) const;

  // Splits this live range and links the resulting ranges together.
  // Returns the child, which starts at position.
  // All uses following the given position will be moved from this
  // live range to the result live range.
  // The current range will terminate at position, while result will start from
  // position.
  LiveRange* SplitAt(LifetimePosition position, Zone* zone);

  // Returns false when no register is hinted, otherwise sets register_index.
  // Uses {current_hint_position_} as a cache, and tries to update it.
  bool RegisterFromFirstHint(int* register_index);

  UsePosition* current_hint_position() const {
    return positions_span_[current_hint_position_index_];
  }

  LifetimePosition Start() const {
    DCHECK(!IsEmpty());
    DCHECK_EQ(start_, intervals_.front().start());
    return start_;
  }

  LifetimePosition End() const {
    DCHECK(!IsEmpty());
    DCHECK_EQ(end_, intervals_.back().end());
    return end_;
  }

  bool ShouldBeAllocatedBefore(const LiveRange* other) const;
  bool CanCover(LifetimePosition position) const;
  bool Covers(LifetimePosition position);
  LifetimePosition NextStartAfter(LifetimePosition position);
  LifetimePosition NextEndAfter(LifetimePosition position);
  LifetimePosition FirstIntersection(LiveRange* other);
  LifetimePosition NextStart() const { return next_start_; }

#ifdef DEBUG
  void VerifyChildStructure() const {
    VerifyIntervals();
    VerifyPositions();
  }
#endif

  void ConvertUsesToOperand(const InstructionOperand& op,
                            const InstructionOperand& spill_op);
  void SetUseHints(int register_index);
  void UnsetUseHints() { SetUseHints(kUnassignedRegister); }
  void ResetCurrentHintPosition() { current_hint_position_index_ = 0; }

  void Print(const RegisterConfiguration* config, bool with_children) const;
  void Print(bool with_children) const;

  bool RegisterFromBundle(int* hint) const;
  void UpdateBundleRegister(int reg) const;

 private:
  friend class TopLevelLiveRange;
  friend Zone;

  explicit LiveRange(int relative_id, MachineRepresentation rep,
                     TopLevelLiveRange* top_level);

  void set_spilled(bool value) { bits_ = SpilledField::update(bits_, value); }

  UseIntervalVector::iterator FirstSearchIntervalForPosition(
      LifetimePosition position);
  void AdvanceLastProcessedMarker(UseIntervalVector::iterator to_start_of,
                                  LifetimePosition but_not_past);

#ifdef DEBUG
  void VerifyPositions() const;
  void VerifyIntervals() const;
#endif

  using SpilledField = base::BitField<bool, 0, 1>;
  // Bits (1,7[ are used by TopLevelLiveRange.
  using AssignedRegisterField = base::BitField<int32_t, 7, 6>;
  using RepresentationField = base::BitField<MachineRepresentation, 13, 8>;
  using RecombineField = base::BitField<bool, 21, 1>;
  using ControlFlowRegisterHint = base::BitField<uint8_t, 22, 6>;
  // Bits 28-31 are used by TopLevelLiveRange.

  // Unique among children of the same virtual register.
  int relative_id_;
  uint32_t bits_;

  UseIntervalVector intervals_;
  // This is a view into the `positions_` owned by the `TopLevelLiveRange`.
  // This allows cheap splitting and merging of `LiveRange`s.
  base::Vector<UsePosition*> positions_span_;

  TopLevelLiveRange* top_level_;
  // TODO(dlehmann): Remove linked list fully and instead use only the
  // `TopLevelLiveRange::children_` vector. This requires API changes to
  // `SplitAt` and `AttachToNext`, as they need access to a vector iterator.
  LiveRange* next_;

  // This is used as a cache in `FirstSearchIntervalForPosition`.
  UseIntervalVector::iterator current_interval_;
  // This is used as a cache in `BuildLiveRanges` and during register
  // allocation.
  size_t current_hint_position_index_ = 0;

  // Next interval start, relative to the current linear scan position.
  LifetimePosition next_start_;

  // Just a cache for `Start()` and `End()` that improves locality
  // (i.e., one less pointer indirection).
  LifetimePosition start_;
  LifetimePosition end_;
};

struct LiveRangeOrdering {
  bool operator()(const LiveRange* left, const LiveRange* right) const {
    return left->Start() < right->Start();
  }
};
// Bundle live ranges that are connected by phis and do not overlap. This tries
// to restore some pre-SSA information and is used as a hint to allocate the
// same spill slot or reuse the same register for connected live ranges.
class LiveRangeBundle : public ZoneObject {
 public:
  explicit LiveRangeBundle(Zone* zone, int id)
      : ranges_(zone), intervals_(zone), id_(id) {}

  int id() const { return id_; }

  int reg() const { return reg_; }
  void set_reg(int reg) {
    DCHECK_EQ(reg_, kUnassignedRegister);
    reg_ = reg;
  }

  void MergeSpillRangesAndClear();
  bool TryAddRange(TopLevelLiveRange* range);
  // If merging is possible, merge either {lhs} into {rhs} or {rhs} into
  // {lhs}, clear the source and return the result. Otherwise return nullptr.
  static LiveRangeBundle* TryMerge(LiveRangeBundle* lhs, LiveRangeBundle* rhs);

 private:
  void AddRange(TopLevelLiveRange* range);

  // A flat set, sorted by `LiveRangeOrdering`.
  ZoneVector<TopLevelLiveRange*> ranges_;
  // A flat set, sorted by their `start()` position.
  ZoneVector<UseInterval> intervals_;

  int id_;
  int reg_ = kUnassignedRegister;
};

// Register allocation splits LiveRanges so it can make more fine-grained
// allocation and spilling decisions. The LiveRanges that belong to the same
// virtual register form a linked-list, and the head of this list is a
// TopLevelLiveRange.
class V8_EXPORT_PRIVATE TopLevelLiveRange final : public LiveRange {
 public:
  explicit TopLevelLiveRange(int vreg, MachineRepresentation rep, Zone* zone);
  TopLevelLiveRange(const TopLevelLiveRange&) = delete;
  TopLevelLiveRange& operator=(const TopLevelLiveRange&) = delete;

  int spill_start_index() const { return spill_start_index_; }

  bool IsFixed() const { return vreg_ < 0; }

  bool IsDeferredFixed() const { return DeferredFixedField::decode(bits_); }
  void set_deferred_fixed() { bits_ = DeferredFixedField::update(bits_, true); }
  bool is_phi() const { return IsPhiField::decode(bits_); }
  void set_is_phi(bool value) { bits_ = IsPhiField::update(bits_, value); }

  bool is_non_loop_phi() const { return IsNonLoopPhiField::decode(bits_); }
  bool is_loop_phi() const { return is_phi() && !is_non_loop_phi(); }
  void set_is_non_loop_phi(bool value) {
    bits_ = IsNonLoopPhiField::update(bits_, value);
  }
  bool SpillAtLoopHeaderNotBeneficial() const {
    return SpillAtLoopHeaderNotBeneficialField::decode(bits_);
  }
  void set_spilling_at_loop_header_not_beneficial() {
    bits_ = SpillAtLoopHeaderNotBeneficialField::update(bits_, true);
  }

  enum SlotUseKind { kNoSlotUse, kDeferredSlotUse, kGeneralSlotUse };

  bool has_slot_use() const {
    return slot_use_kind() > SlotUseKind::kNoSlotUse;
  }

  bool has_non_deferred_slot_use() const {
    return slot_use_kind() == SlotUseKind::kGeneralSlotUse;
  }

  void reset_slot_use() {
    bits_ = HasSlotUseField::update(bits_, SlotUseKind::kNoSlotUse);
  }
  void register_slot_use(SlotUseKind value) {
    bits_ = HasSlotUseField::update(bits_, std::max(slot_use_kind(), value));
  }
  SlotUseKind slot_use_kind() const { return HasSlotUseField::decode(bits_); }

  // Add a new interval or a new use position to this live range.
  void EnsureInterval(LifetimePosition start, LifetimePosition end, Zone* zone);
  void AddUseInterval(LifetimePosition start, LifetimePosition end, Zone* zone);
  void AddUsePosition(UsePosition* pos, Zone* zone);

  // Shorten the most recently added interval by setting a new start.
  void ShortenTo(LifetimePosition start);

  // Spill range management.
  void SetSpillRange(SpillRange* spill_range);

  // Encodes whether a range is also available from a memory location:
  //   kNoSpillType: not availble in memory location.
  //   kSpillOperand: computed in a memory location at range start.
  //   kSpillRange: copied (spilled) to memory location at the definition,
  //                or at the beginning of some later blocks if
  //                LateSpillingSelected() is true.
  //   kDeferredSpillRange: copied (spilled) to memory location at entry
  //                        to deferred blocks that have a use from memory.
  //
  // Ranges either start out at kSpillOperand, which is also their final
  // state, or kNoSpillType. When spilled only in deferred code, a range
  // ends up with kDeferredSpillRange, while when spilled in regular code,
  // a range will be tagged as kSpillRange.
  enum class SpillType {
    kNoSpillType,
    kSpillOperand,
    kSpillRange,
    kDeferredSpillRange
  };
  void set_spill_type(SpillType value) {
    bits_ = SpillTypeField::update(bits_, value);
  }
  SpillType spill_type() const { return SpillTypeField::decode(bits_); }
  InstructionOperand* GetSpillOperand() const {
    DCHECK_EQ(SpillType::kSpillOperand, spill_type());
    return spill_operand_;
  }

  SpillRange* GetAllocatedSpillRange() const {
    DCHECK_NE(SpillType::kSpillOperand, spill_type());
    return spill_range_;
  }

  SpillRange* GetSpillRange() const {
    DCHECK_GE(spill_type(), SpillType::kSpillRange);
    return spill_range_;
  }
  bool HasNoSpillType() const {
    return spill_type() == SpillType::kNoSpillType;
  }
  bool HasSpillOperand() const {
    return spill_type() == SpillType::kSpillOperand;
  }
  bool HasSpillRange() const { return spill_type() >= SpillType::kSpillRange; }
  bool HasGeneralSpillRange() const {
    return spill_type() == SpillType::kSpillRange;
  }
  AllocatedOperand GetSpillRangeOperand() const;

  void RecordSpillLocation(Zone* zone, int gap_index,
                           InstructionOperand* operand);
  void SetSpillOperand(InstructionOperand* operand);
  void SetSpillStartIndex(int start) {
    spill_start_index_ = std::min(start, spill_start_index_);
  }

  // Omits any moves from spill_move_insertion_locations_ that can be skipped.
  void FilterSpillMoves(RegisterAllocationData* data,
                        const InstructionOperand& operand);

  // Writes all moves from spill_move_insertion_locations_ to the schedule.
  void CommitSpillMoves(RegisterAllocationData* data,
                        const InstructionOperand& operand);

  // If all the children of this range are spilled in deferred blocks, and if
  // for any non-spilled child with a use position requiring a slot, that range
  // is contained in a deferred block, mark the range as
  // IsSpilledOnlyInDeferredBlocks, so that we avoid spilling at definition,
  // and instead let the LiveRangeConnector perform the spills within the
  // deferred blocks. If so, we insert here spills for non-spilled ranges
  // with slot use positions.
  void TreatAsSpilledInDeferredBlock(Zone* zone) {
    spill_start_index_ = -1;
    spilled_in_deferred_blocks_ = true;
    spill_move_insertion_locations_ = nullptr;
    list_of_blocks_requiring_spill_operands_ = zone->New<SparseBitVector>(zone);
  }

  // Updates internal data structures to reflect that this range is not
  // spilled at definition but instead spilled in some blocks only.
  void TransitionRangeToDeferredSpill(Zone* zone) {
    spill_start_index_ = -1;
    spill_move_insertion_locations_ = nullptr;
    list_of_blocks_requiring_spill_operands_ = zone->New<SparseBitVector>(zone);
  }

  // Promotes this range to spill at definition if it was marked for spilling
  // in deferred blocks before.
  void TransitionRangeToSpillAtDefinition() {
    DCHECK_NOT_NULL(spill_move_insertion_locations_);
    if (spill_type() == SpillType::kDeferredSpillRange) {
      set_spill_type(SpillType::kSpillRange);
    }
  }

  bool MayRequireSpillRange() const {
    return !HasSpillOperand() && spill_range_ == nullptr;
  }
  void UpdateSpillRangePostMerge(TopLevelLiveRange* merged);
  int vreg() const { return vreg_; }

#ifdef DEBUG
  void Verify() const;
  void VerifyChildrenInOrder() const;
#endif

  // Returns the child `LiveRange` covering the given position, or `nullptr`
  // if no such range exists. Uses a binary search.
  LiveRange* GetChildCovers(LifetimePosition pos);

  const ZoneVector<LiveRange*>& Children() const { return children_; }

  int GetNextChildId() { return ++last_child_id_; }

  bool IsSpilledOnlyInDeferredBlocks(const RegisterAllocationData* data) const {
    return spill_type() == SpillType::kDeferredSpillRange;
  }

  struct SpillMoveInsertionList;

  SpillMoveInsertionList* GetSpillMoveInsertionLocations(
      const RegisterAllocationData* data) const {
    DCHECK(!IsSpilledOnlyInDeferredBlocks(data));
    return spill_move_insertion_locations_;
  }

  void MarkHasPreassignedSlot() { has_preassigned_slot_ = true; }
  bool has_preassigned_slot() const { return has_preassigned_slot_; }

  // Late spilling refers to spilling at places after the definition. These
  // spills are guaranteed to cover at least all of the sub-ranges where the
  // register allocator chose to evict the value from a register.
  void SetLateSpillingSelected(bool late_spilling_selected) {
    DCHECK(spill_type() == SpillType::kSpillRange);
    SpillRangeMode new_mode = late_spilling_selected
                                  ? SpillRangeMode::kSpillLater
                                  : SpillRangeMode::kSpillAtDefinition;
    // A single TopLevelLiveRange should never be used in both modes.
    DCHECK(SpillRangeModeField::decode(bits_) == SpillRangeMode::kNotSet ||
           SpillRangeModeField::decode(bits_) == new_mode);
    bits_ = SpillRangeModeField::update(bits_, new_mode);
  }
  bool LateSpillingSelected() const {
    // Nobody should be reading this value until it's been decided.
    DCHECK_IMPLIES(HasGeneralSpillRange(), SpillRangeModeField::decode(bits_) !=
                                               SpillRangeMode::kNotSet);
    return SpillRangeModeField::decode(bits_) == SpillRangeMode::kSpillLater;
  }

  void AddBlockRequiringSpillOperand(RpoNumber block_id,
                                     const RegisterAllocationData* data) {
    DCHECK(IsSpilledOnlyInDeferredBlocks(data));
    GetListOfBlocksRequiringSpillOperands(data)->Add(block_id.ToInt());
  }

  SparseBitVector* GetListOfBlocksRequiringSpillOperands(
      const RegisterAllocationData* data) const {
    DCHECK(IsSpilledOnlyInDeferredBlocks(data));
    return list_of_blocks_requiring_spill_operands_;
  }

  LiveRangeBundle* get_bundle() const { return bundle_; }
  void set_bundle(LiveRangeBundle* bundle) { bundle_ = bundle; }

 private:
  friend class LiveRange;

  // If spill type is kSpillRange, then this value indicates whether we've
  // chosen to spill at the definition or at some later points.
  enum class SpillRangeMode : uint8_t {
    kNotSet,
    kSpillAtDefinition,
    kSpillLater,
  };

  using HasSlotUseField = base::BitField<SlotUseKind, 1, 2>;
  using IsPhiField = base::BitField<bool, 3, 1>;
  using IsNonLoopPhiField = base::BitField<bool, 4, 1>;
  using SpillTypeField = base::BitField<SpillType, 5, 2>;
  using DeferredFixedField = base::BitField<bool, 28, 1>;
  using SpillAtLoopHeaderNotBeneficialField = base::BitField<bool, 29, 1>;
  using SpillRangeModeField = base::BitField<SpillRangeMode, 30, 2>;

  int vreg_;
  int last_child_id_;
  union {
    // Correct value determined by spill_type()
    InstructionOperand* spill_operand_;
    SpillRange* spill_range_;
  };

  union {
    SpillMoveInsertionList* spill_move_insertion_locations_;
    SparseBitVector* list_of_blocks_requiring_spill_operands_;
  };

  LiveRangeBundle* bundle_ = nullptr;

  UsePositionVector positions_;

  // This is a cache for the binary search in `GetChildCovers`.
  // The `LiveRange`s are sorted by their `Start()` position.
  ZoneVector<LiveRange*> children_;

  // TODO(mtrofin): generalize spilling after definition, currently specialized
  // just for spill in a single deferred block.
  bool spilled_in_deferred_blocks_;
  bool has_preassigned_slot_;

  int spill_start_index_;
};

struct PrintableLiveRange {
  const RegisterConfiguration* register_configuration_;
  const LiveRange* range_;
};

std::ostream& operator<<(std::ostream& os,
                         const PrintableLiveRange& printable_range);

// Represent the spill operand of a LiveRange and its use intervals. After
// register allocation, disjoint spill ranges are merged and they get assigned
// the same spill slot by OperandAssigner::AssignSpillSlots().
// TODO(dlehmann): `SpillRange`s seem awefully similar to `LiveRangeBundle`s,
// especially since both store `ranges_` and `intervals_` and check for
// intersection in exactly the same way. I wonder if we can merge those two
// concepts and save a bunch of memory by not storing ranges and intervals
// twice.
class SpillRange final : public ZoneObject {
 public:
  static const int kUnassignedSlot = -1;

  SpillRange(TopLevelLiveRange* range, Zone* zone);
  SpillRange(const SpillRange&) = delete;
  SpillRange& operator=(const SpillRange&) = delete;

  bool IsEmpty() const { return ranges_.empty(); }
  bool TryMerge(SpillRange* other);
  bool HasSlot() const { return assigned_slot_ != kUnassignedSlot; }

  void set_assigned_slot(int index) {
    DCHECK_EQ(kUnassignedSlot, assigned_slot_);
    assigned_slot_ = index;
  }
  int assigned_slot() const {
    DCHECK_NE(kUnassignedSlot, assigned_slot_);
    return assigned_slot_;
  }

  // Spill slots can be 4, 8, or 16 bytes wide.
  int byte_width() const { return byte_width_; }

  void Print() const;

 private:
  ZoneVector<TopLevelLiveRange*> ranges_;
  // A flat set, sorted by their `start()` position.
  ZoneVector<UseInterval> intervals_;

  int assigned_slot_;
  int byte_width_;
};

class ConstraintBuilder final : public ZoneObject {
 public:
  explicit ConstraintBuilder(RegisterAllocationData* data);
  ConstraintBuilder(const ConstraintBuilder&) = delete;
  ConstraintBuilder& operator=(const ConstraintBuilder&) = delete;

  // Phase 1 : insert moves to account for fixed register operands.
  void MeetRegisterConstraints();

  // Phase 2: deconstruct SSA by inserting moves in successors and the headers
  // of blocks containing phis.
  void ResolvePhis();

 private:
  RegisterAllocationData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  Zone* allocation_zone() const { return data()->allocation_zone(); }

  InstructionOperand* AllocateFixed(UnallocatedOperand* operand, int pos,
                                    bool is_tagged, bool is_input);
  void MeetRegisterConstraints(const InstructionBlock* block);
  void MeetConstraintsBefore(int index);
  void MeetConstraintsAfter(int index);
  void MeetRegisterConstraintsForLastInstructionInBlock(
      const InstructionBlock* block);
  void ResolvePhis(const InstructionBlock* block);

  RegisterAllocationData* const data_;
};

class LiveRangeBuilder final : public ZoneObject {
 public:
  explicit LiveRangeBuilder(RegisterAllocationData* data, Zone* local_zone);
  LiveRangeBuilder(const LiveRangeBuilder&) = delete;
  LiveRangeBuilder& operator=(const LiveRangeBuilder&) = delete;

  // Phase 3: compute liveness of all virtual register.
  void BuildLiveRanges();
  static SparseBitVector* ComputeLiveOut(const InstructionBlock* block,
                                         RegisterAllocationData* data);

 private:
  using SpillMode = RegisterAllocationData::SpillMode;
  static constexpr int kNumberOfFixedRangesPerRegister =
      RegisterAllocationData::kNumberOfFixedRangesPerRegister;

  RegisterAllocationData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  Zone* allocation_zone() const { return data()->allocation_zone(); }
  Zone* code_zone() const { return code()->zone(); }
  const RegisterConfiguration* config() const { return data()->config(); }
  ZoneVector<SparseBitVector*>& live_in_sets() const {
    return data()->live_in_sets();
  }

#ifdef DEBUG
  // Verification.
  void Verify() const;
  bool IntervalStartsAtBlockBoundary(UseInterval interval) const;
  bool IntervalPredecessorsCoveredByRange(UseInterval interval,
                                          TopLevelLiveRange* range) const;
  bool NextIntervalStartsInDifferentBlocks(UseInterval interval,
                                           UseInterval next) const;
#endif

  // Liveness analysis support.
  void AddInitialIntervals(const InstructionBlock* block,
                           SparseBitVector* live_out);
  void ProcessInstructions(const InstructionBlock* block,
                           SparseBitVector* live);
  void ProcessPhis(const InstructionBlock* block, SparseBitVector* live);
  void ProcessLoopHeader(const InstructionBlock* block, SparseBitVector* live);

  static int FixedLiveRangeID(int index) { return -index - 1; }
  int FixedFPLiveRangeID(int index, MachineRepresentation rep);
  TopLevelLiveRange* FixedLiveRangeFor(int index, SpillMode spill_mode);
  TopLevelLiveRange* FixedFPLiveRangeFor(int index, MachineRepresentation rep,
                                         SpillMode spill_mode);
  TopLevelLiveRange* FixedSIMD128LiveRangeFor(int index, SpillMode spill_mode);

  void MapPhiHint(InstructionOperand* operand, UsePosition* use_pos);
  void ResolvePhiHint(InstructionOperand* operand, UsePosition* use_pos);

  UsePosition* NewUsePosition(LifetimePosition pos, InstructionOperand* operand,
                              void* hint, UsePositionHintType hint_type);
  UsePosition* NewUsePosition(LifetimePosition pos) {
    return NewUsePosition(pos, nullptr, nullptr, UsePositionHintType::kNone);
  }
  TopLevelLiveRange* LiveRangeFor(InstructionOperand* operand,
                                  SpillMode spill_mode);
  // Helper methods for building intervals.
  UsePosition* Define(LifetimePosition position, InstructionOperand* operand,
                      void* hint, UsePositionHintType hint_type,
                      SpillMode spill_mode);
  void Define(LifetimePosition position, InstructionOperand* operand,
              SpillMode spill_mode) {
    Define(position, operand, nullptr, UsePositionHintType::kNone, spill_mode);
  }
  UsePosition* Use(LifetimePosition block_start, LifetimePosition position,
                   InstructionOperand* operand, void* hint,
                   UsePositionHintType hint_type, SpillMode spill_mode);
  void Use(LifetimePosition block_start, LifetimePosition position,
           InstructionOperand* operand, SpillMode spill_mode) {
    Use(block_start, position, operand, nullptr, UsePositionHintType::kNone,
        spill_mode);
  }
  SpillMode SpillModeForBlock(const InstructionBlock* block) const {
    return block->IsDeferred() ? SpillMode::kSpillDeferred
                               : SpillMode::kSpillAtDefinition;
  }
  RegisterAllocationData* const data_;
  ZoneMap<InstructionOperand*, UsePosition*> phi_hints_;
};

class BundleBuilder final : public ZoneObject {
 public:
  explicit BundleBuilder(RegisterAllocationData* data) : data_(data) {}

  void BuildBundles();

 private:
  RegisterAllocationData* data() const { return data_; }
  InstructionSequence* code() const { return data_->code(); }
  RegisterAllocationData* data_;
  int next_bundle_id_ = 0;
};

class RegisterAllocator : public ZoneObject {
 public:
  RegisterAllocator(RegisterAllocationData* data, RegisterKind kind);
  RegisterAllocator(const RegisterAllocator&) = delete;
  RegisterAllocator& operator=(const RegisterAllocator&) = delete;

 protected:
  using SpillMode = RegisterAllocationData::SpillMode;
  RegisterAllocationData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  RegisterKind mode() const { return mode_; }
  int num_registers() const { return num_registers_; }
  int num_allocatable_registers() const { return num_allocatable_registers_; }
  const int* allocatable_register_codes() const {
    return allocatable_register_codes_;
  }
  // Returns true iff. we must check float register aliasing.
  bool check_fp_aliasing() const { return check_fp_aliasing_; }

  // TODO(mtrofin): explain why splitting in gap START is always OK.
  LifetimePosition GetSplitPositionForInstruction(const LiveRange* range,
                                                  int instruction_index);

  Zone* allocation_zone() const { return data()->allocation_zone(); }

  // Find the optimal split for ranges defined by a memory operand, e.g.
  // constants or function parameters passed on the stack.
  void SplitAndSpillRangesDefinedByMemoryOperand();

  // Split the given range at the given position.
  // If range starts at or after the given position then the
  // original range is returned.
  // Otherwise returns the live range that starts at pos and contains
  // all uses from the original range that follow pos. Uses at pos will
  // still be owned by the original range after splitting.
  LiveRange* SplitRangeAt(LiveRange* range, LifetimePosition pos);

  bool CanProcessRange(LiveRange* range) const {
    return range != nullptr && !range->IsEmpty() && range->kind() == mode();
  }

  // Split the given range in a position from the interval [start, end].
  LiveRange* SplitBetween(LiveRange* range, LifetimePosition start,
                          LifetimePosition end);

  // Find a lifetime position in the interval [start, end] which
  // is optimal for splitting: it is either header of the outermost
  // loop covered by this interval or the latest possible position.
  LifetimePosition FindOptimalSplitPos(LifetimePosition start,
                                       LifetimePosition end);

  void Spill(LiveRange* range, SpillMode spill_mode);

  // If we are trying to spill a range inside the loop try to
  // hoist spill position out to the point just before the loop.
  LifetimePosition FindOptimalSpillingPos(LiveRange* range,
                                          LifetimePosition pos,
                                          SpillMode spill_mode,
                                          LiveRange** begin_spill_out);

  const ZoneVector<TopLevelLiveRange*>& GetFixedRegisters() const;
  const char* RegisterName(int allocation_index) const;

 private:
  RegisterAllocationData* const data_;
  const RegisterKind mode_;
  const int num_registers_;
  int num_allocatable_registers_;
  const int* allocatable_register_codes_;
  bool check_fp_aliasing_;

 private:
  bool no_combining_;
};

// A map from `TopLevelLiveRange`s to their expected physical register.
// Typically this is very small, e.g., on JetStream2 it has 3 elements or less
// >50% of the times it is queried, 8 elements or less >90% of the times,
// and never more than 15 elements. Hence this is backed by a `SmallZoneMap`.
using RangeRegisterSmallMap =
    SmallZoneMap<TopLevelLiveRange*, /* expected_register */ int, 16>;

class LinearScanAllocator final : public RegisterAllocator {
 public:
  LinearScanAllocator(RegisterAllocationData* data, RegisterKind kind,
                      Zone* local_zone);
  LinearScanAllocator(const LinearScanAllocator&) = delete;
  LinearScanAllocator& operator=(const LinearScanAllocator&) = delete;

  // Phase 4: compute register assignments.
  void AllocateRegisters();

 private:
  void MaybeSpillPreviousRanges(LiveRange* begin_range,
                                LifetimePosition begin_pos,
                                LiveRange* end_range);
  void MaybeUndoPreviousSplit(LiveRange* range, Zone* zone);
  void SpillNotLiveRanges(RangeRegisterSmallMap& to_be_live,
                          LifetimePosition position, SpillMode spill_mode);
  LiveRange* AssignRegisterOnReload(LiveRange* range, int reg);
  void ReloadLiveRanges(RangeRegisterSmallMap const& to_be_live,
                        LifetimePosition position);

  void UpdateDeferredFixedRanges(SpillMode spill_mode, InstructionBlock* block);
  bool BlockIsDeferredOrImmediatePredecessorIsNotDeferred(
      const InstructionBlock* block);
  bool HasNonDeferredPredecessor(InstructionBlock* block);

  struct UnhandledLiveRangeOrdering {
    bool operator()(const LiveRange* a, const LiveRange* b) const {
      return a->ShouldBeAllocatedBefore(b);
    }
  };

  struct InactiveLiveRangeOrdering {
    bool operator()(const LiveRange* a, const LiveRange* b) const {
      return a->NextStart() < b->NextStart();
    }
  };

  // NOTE: We also tried a sorted ZoneVector instead of a `ZoneMultiset`
  // (like for `InactiveLiveRangeQueue`), but it does not improve performance
  // or max memory usage.
  // TODO(dlehmann): Try `std::priority_queue`/`std::make_heap` instead.
  using UnhandledLiveRangeQueue =
      ZoneMultiset<LiveRange*, UnhandledLiveRangeOrdering>;
  // Sorted by `InactiveLiveRangeOrdering`.
  // TODO(dlehmann): Try `std::priority_queue`/`std::make_heap` instead.
  using InactiveLiveRangeQueue = ZoneVector<LiveRange*>;
  UnhandledLiveRangeQueue& unhandled_live_ranges() {
    return unhandled_live_ranges_;
  }
  ZoneVector<LiveRange*>& active_live_ranges() { return active_live_ranges_; }
  InactiveLiveRangeQueue& inactive_live_ranges(int reg) {
    return inactive_live_ranges_[reg];
  }
  // At several places in the register allocator we rely on inactive live ranges
  // being sorted. Previously, this was always true by using a std::multiset.
  // But to improve performance and in particular reduce memory usage, we
  // switched to a sorted vector.
  // Insert this to ensure we don't violate the sorted assumption, and to
  // document where we actually rely on inactive live ranges being sorted.
  void SlowDCheckInactiveLiveRangesIsSorted(int reg) {
    SLOW_DCHECK(std::is_sorted(inactive_live_ranges(reg).begin(),
                               inactive_live_ranges(reg).end(),
                               InactiveLiveRangeOrdering()));
  }

  void SetLiveRangeAssignedRegister(LiveRange* range, int reg);

  // Helper methods for updating the life range lists.
  void AddToActive(LiveRange* range);
  void AddToInactive(LiveRange* range);
  void AddToUnhandled(LiveRange* range);
  ZoneVector<LiveRange*>::iterator ActiveToHandled(
      ZoneVector<LiveRange*>::iterator it);
  ZoneVector<LiveRange*>::iterator ActiveToInactive(
      ZoneVector<LiveRange*>::iterator it, LifetimePosition position);
  InactiveLiveRangeQueue::iterator InactiveToHandled(
      InactiveLiveRangeQueue::iterator it);
  InactiveLiveRangeQueue::iterator InactiveToActive(
      InactiveLiveRangeQueue::iterator it, LifetimePosition position);

  void ForwardStateTo(LifetimePosition position);

  int LastDeferredInstructionIndex(InstructionBlock* start);

  // Helper methods for choosing state after control flow events.

  bool ConsiderBlockForControlFlow(InstructionBlock* current_block,
                                   RpoNumber predecessor);
  RpoNumber ChooseOneOfTwoPredecessorStates(InstructionBlock* current_block,
                                            LifetimePosition boundary);
  bool CheckConflict(MachineRepresentation rep, int reg,
                     const RangeRegisterSmallMap& to_be_live);
  void ComputeStateFromManyPredecessors(InstructionBlock* current_block,
                                        RangeRegisterSmallMap& to_be_live);

  // Helper methods for allocating registers.

  // Spilling a phi at range start can be beneficial when the phi input is
  // already spilled and shares the same spill slot. This function tries to
  // guess if spilling the phi is beneficial based on live range bundles and
  // spilled phi inputs.
  bool TryReuseSpillForPhi(TopLevelLiveRange* range);
  int PickRegisterThatIsAvailableLongest(
      LiveRange* current, int hint_reg,
      base::Vector<const LifetimePosition> free_until_pos);
  bool TryAllocateFreeReg(LiveRange* range,
                          base::Vector<const LifetimePosition> free_until_pos);
  bool TryAllocatePreferredReg(
      LiveRange* range, base::Vector<const LifetimePosition> free_until_pos);
  void GetFPRegisterSet(MachineRepresentation rep, int* num_regs,
                        int* num_codes, const int** codes) const;
  void GetSIMD128RegisterSet(int* num_regs, int* num_codes,
                             const int** codes) const;
  void FindFreeRegistersForRange(LiveRange* range,
                                 base::Vector<LifetimePosition> free_until_pos);
  void ProcessCurrentRange(LiveRange* current, SpillMode spill_mode);
  void AllocateBlockedReg(LiveRange* range, SpillMode spill_mode);

  // Spill the given life range after position pos.
  void SpillAfter(LiveRange* range, LifetimePosition pos, SpillMode spill_mode);

  // Spill the given life range after position [start] and up to position [end].
  void SpillBetween(LiveRange* range, LifetimePosition start,
                    LifetimePosition end, SpillMode spill_mode);

  // Spill the given life range after position [start] and up to position [end].
  // Range is guaranteed to be spilled at least until position [until].
  void SpillBetweenUntil(LiveRange* range, LifetimePosition start,
                         LifetimePosition until, LifetimePosition end,
                         SpillMode spill_mode);
  void SplitAndSpillIntersecting(LiveRange* range, SpillMode spill_mode);

  void PrintRangeRow(std::ostream& os, const TopLevelLiveRange* toplevel);

  void PrintRangeOverview();

  UnhandledLiveRangeQueue unhandled_live_ranges_;
  ZoneVector<LiveRange*> active_live_ranges_;
  ZoneVector<InactiveLiveRangeQueue> inactive_live_ranges_;

  // Approximate at what position the set of ranges will change next.
  // Used to avoid scanning for updates even if none are present.
  LifetimePosition next_active_ranges_change_;
  LifetimePosition next_inactive_ranges_change_;

#ifdef DEBUG
  LifetimePosition allocation_finger_;
#endif
};

class OperandAssigner final : public ZoneObject {
 public:
  explicit OperandAssigner(RegisterAllocationData* data);
  OperandAssigner(const OperandAssigner&) = delete;
  OperandAssigner& operator=(const OperandAssigner&) = delete;

  // Phase 5: final decision on spilling mode.
  void DecideSpillingMode();

  // Phase 6: assign spill splots.
  void AssignSpillSlots();

  // Phase 7: commit assignment.
  void CommitAssignment();

 private:
  RegisterAllocationData* data() const { return data_; }

  RegisterAllocationData* const data_;
};

class ReferenceMapPopulator final : public ZoneObject {
 public:
  explicit ReferenceMapPopulator(RegisterAllocationData* data);
  ReferenceMapPopulator(const ReferenceMapPopulator&) = delete;
  ReferenceMapPopulator& operator=(const ReferenceMapPopulator&) = delete;

  // Phase 10: compute values for pointer maps.
  void PopulateReferenceMaps();

 private:
  RegisterAllocationData* data() const { return data_; }

  bool SafePointsAreInOrder() const;

  RegisterAllocationData* const data_;
};

class LiveRangeBoundArray;
// Insert moves of the form
//
//          Operand(child_(k+1)) = Operand(child_k)
//
// where child_k and child_(k+1) are consecutive children of a range (so
// child_k->next() == child_(k+1)), and Operand(...) refers to the
// assigned operand, be it a register or a slot.
class LiveRangeConnector final : public ZoneObject {
 public:
  explicit LiveRangeConnector(RegisterAllocationData* data);
  LiveRangeConnector(const LiveRangeConnector&) = delete;
  LiveRangeConnector& operator=(const LiveRangeConnector&) = delete;

  // Phase 8: reconnect split ranges with moves, when the control flow
  // between the ranges is trivial (no branches).
  void ConnectRanges(Zone* local_zone);

  // Phase 9: insert moves to connect ranges across basic blocks, when the
  // control flow between them cannot be trivially resolved, such as joining
  // branches. Also determines whether to spill at the definition or later, and
  // adds spill moves to the gaps in the schedule.
  void ResolveControlFlow(Zone* local_zone);

 private:
  RegisterAllocationData* data() const { return data_; }
  InstructionSequence* code() const { return data()->code(); }
  Zone* code_zone() const { return code()->zone(); }

  bool CanEagerlyResolveControlFlow(const InstructionBlock* block) const;
  int ResolveControlFlow(const InstructionBlock* block,
                         const InstructionOperand& cur_op,
                         const InstructionBlock* pred,
                         const InstructionOperand& pred_op);

  void CommitSpillsInDeferredBlocks(TopLevelLiveRange* range, Zone* temp_zone);

  RegisterAllocationData* const data_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_REGISTER_ALLOCATOR_H_
