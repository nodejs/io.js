// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/register-allocator.h"

#include <iomanip>

#include "src/base/iterator.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/tick-counter.h"
#include "src/compiler/backend/spill-placer.h"
#include "src/compiler/linkage.h"
#include "src/strings/string-stream.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                                       \
  do {                                                   \
    if (v8_flags.trace_turbo_alloc) PrintF(__VA_ARGS__); \
  } while (false)

namespace {

static constexpr int kFloat32Bit =
    RepresentationBit(MachineRepresentation::kFloat32);
static constexpr int kSimd128Bit =
    RepresentationBit(MachineRepresentation::kSimd128);

const InstructionBlock* GetContainingLoop(const InstructionSequence* sequence,
                                          const InstructionBlock* block) {
  RpoNumber index = block->loop_header();
  if (!index.IsValid()) return nullptr;
  return sequence->InstructionBlockAt(index);
}

const InstructionBlock* GetInstructionBlock(const InstructionSequence* code,
                                            LifetimePosition pos) {
  return code->GetInstructionBlock(pos.ToInstructionIndex());
}

Instruction* GetLastInstruction(InstructionSequence* code,
                                const InstructionBlock* block) {
  return code->InstructionAt(block->last_instruction_index());
}

}  // namespace

using DelayedInsertionMapKey = std::pair<ParallelMove*, InstructionOperand>;

struct DelayedInsertionMapCompare {
  bool operator()(const DelayedInsertionMapKey& a,
                  const DelayedInsertionMapKey& b) const {
    if (a.first == b.first) {
      return a.second.Compare(b.second);
    }
    return a.first < b.first;
  }
};

using DelayedInsertionMap = ZoneMap<DelayedInsertionMapKey, InstructionOperand,
                                    DelayedInsertionMapCompare>;

UsePosition::UsePosition(LifetimePosition pos, InstructionOperand* operand,
                         void* hint, UsePositionHintType hint_type)
    : operand_(operand), hint_(hint), pos_(pos), flags_(0) {
  DCHECK_IMPLIES(hint == nullptr, hint_type == UsePositionHintType::kNone);
  bool register_beneficial = true;
  UsePositionType type = UsePositionType::kRegisterOrSlot;
  if (operand_ != nullptr && operand_->IsUnallocated()) {
    const UnallocatedOperand* unalloc = UnallocatedOperand::cast(operand_);
    if (unalloc->HasRegisterPolicy()) {
      type = UsePositionType::kRequiresRegister;
    } else if (unalloc->HasSlotPolicy()) {
      type = UsePositionType::kRequiresSlot;
      register_beneficial = false;
    } else if (unalloc->HasRegisterOrSlotOrConstantPolicy()) {
      type = UsePositionType::kRegisterOrSlotOrConstant;
      register_beneficial = false;
    } else {
      register_beneficial = !unalloc->HasRegisterOrSlotPolicy();
    }
  }
  flags_ = TypeField::encode(type) | HintTypeField::encode(hint_type) |
           RegisterBeneficialField::encode(register_beneficial) |
           AssignedRegisterField::encode(kUnassignedRegister);
  DCHECK(pos_.IsValid());
}

bool UsePosition::HasHint() const {
  int hint_register;
  return HintRegister(&hint_register);
}

bool UsePosition::HintRegister(int* register_code) const {
  if (hint_ == nullptr) return false;
  switch (HintTypeField::decode(flags_)) {
    case UsePositionHintType::kNone:
    case UsePositionHintType::kUnresolved:
      return false;
    case UsePositionHintType::kUsePos: {
      UsePosition* use_pos = reinterpret_cast<UsePosition*>(hint_);
      int assigned_register = AssignedRegisterField::decode(use_pos->flags_);
      if (assigned_register == kUnassignedRegister) return false;
      *register_code = assigned_register;
      return true;
    }
    case UsePositionHintType::kOperand: {
      InstructionOperand* operand =
          reinterpret_cast<InstructionOperand*>(hint_);
      *register_code = LocationOperand::cast(operand)->register_code();
      return true;
    }
    case UsePositionHintType::kPhi: {
      RegisterAllocationData::PhiMapValue* phi =
          reinterpret_cast<RegisterAllocationData::PhiMapValue*>(hint_);
      int assigned_register = phi->assigned_register();
      if (assigned_register == kUnassignedRegister) return false;
      *register_code = assigned_register;
      return true;
    }
  }
  UNREACHABLE();
}

UsePositionHintType UsePosition::HintTypeForOperand(
    const InstructionOperand& op) {
  switch (op.kind()) {
    case InstructionOperand::CONSTANT:
    case InstructionOperand::IMMEDIATE:
      return UsePositionHintType::kNone;
    case InstructionOperand::UNALLOCATED:
      return UsePositionHintType::kUnresolved;
    case InstructionOperand::ALLOCATED:
      if (op.IsRegister() || op.IsFPRegister()) {
        return UsePositionHintType::kOperand;
      } else {
        DCHECK(op.IsStackSlot() || op.IsFPStackSlot());
        return UsePositionHintType::kNone;
      }
    case InstructionOperand::PENDING:
    case InstructionOperand::INVALID:
      break;
  }
  UNREACHABLE();
}

void UsePosition::SetHint(UsePosition* use_pos) {
  DCHECK_NOT_NULL(use_pos);
  hint_ = use_pos;
  flags_ = HintTypeField::update(flags_, UsePositionHintType::kUsePos);
}

void UsePosition::ResolveHint(UsePosition* use_pos) {
  DCHECK_NOT_NULL(use_pos);
  if (HintTypeField::decode(flags_) != UsePositionHintType::kUnresolved) return;
  hint_ = use_pos;
  flags_ = HintTypeField::update(flags_, UsePositionHintType::kUsePos);
}

void UsePosition::set_type(UsePositionType type, bool register_beneficial) {
  DCHECK_IMPLIES(type == UsePositionType::kRequiresSlot, !register_beneficial);
  DCHECK_EQ(kUnassignedRegister, AssignedRegisterField::decode(flags_));
  flags_ = TypeField::encode(type) |
           RegisterBeneficialField::encode(register_beneficial) |
           HintTypeField::encode(HintTypeField::decode(flags_)) |
           AssignedRegisterField::encode(kUnassignedRegister);
}

void LifetimePosition::Print() const { StdoutStream{} << *this << std::endl; }

LiveRange::LiveRange(int relative_id, MachineRepresentation rep,
                     TopLevelLiveRange* top_level)
    : relative_id_(relative_id),
      bits_(0),
      intervals_(),
      positions_span_(),
      top_level_(top_level),
      next_(nullptr),
      current_interval_(intervals_.begin()) {
  DCHECK(AllocatedOperand::IsSupportedRepresentation(rep));
  bits_ = AssignedRegisterField::encode(kUnassignedRegister) |
          RepresentationField::encode(rep) |
          ControlFlowRegisterHint::encode(kUnassignedRegister);
}

#ifdef DEBUG
void LiveRange::VerifyPositions() const {
  SLOW_DCHECK(std::is_sorted(positions().begin(), positions().end(),
                             UsePosition::Ordering()));

  // Verify that each `UsePosition` is covered by a `UseInterval`.
  UseIntervalVector::const_iterator interval = intervals().begin();
  for (UsePosition* pos : positions()) {
    DCHECK_LE(Start(), pos->pos());
    DCHECK_LE(pos->pos(), End());
    DCHECK_NE(interval, intervals().end());
    // NOTE: Even though `UseInterval`s are conceptually half-open (e.g., when
    // splitting), we still regard the `UsePosition` that coincides with
    // the end of an interval as covered by that interval.
    while (!interval->Contains(pos->pos()) && interval->end() != pos->pos()) {
      ++interval;
      DCHECK_NE(interval, intervals().end());
    }
  }
}

void LiveRange::VerifyIntervals() const {
  DCHECK(!intervals().empty());
  DCHECK_EQ(intervals().front().start(), Start());
  // The `UseInterval`s must be sorted and disjoint.
  LifetimePosition last_end = intervals().front().end();
  for (UseIntervalVector::const_iterator interval = intervals().begin() + 1;
       interval != intervals().end(); ++interval) {
    DCHECK_LE(last_end, interval->start());
    last_end = interval->end();
  }
  DCHECK_EQ(last_end, End());
}
#endif

void LiveRange::set_assigned_register(int reg) {
  DCHECK(!HasRegisterAssigned() && !spilled());
  bits_ = AssignedRegisterField::update(bits_, reg);
}

void LiveRange::UnsetAssignedRegister() {
  DCHECK(HasRegisterAssigned() && !spilled());
  bits_ = AssignedRegisterField::update(bits_, kUnassignedRegister);
}

void LiveRange::AttachToNext(Zone* zone) {
  DCHECK_NOT_NULL(next_);

  // Update cache for `TopLevelLiveRange::GetChildCovers()`.
  auto& children = TopLevel()->children_;
  children.erase(std::lower_bound(children.begin(), children.end(), next_,
                                  LiveRangeOrdering()));

  // Merge use intervals.
  intervals_.Append(zone, next_->intervals_);
  // `start_` doesn't change.
  end_ = next_->end_;

  // Merge use positions.
  CHECK_EQ(positions_span_.end(), next_->positions_span_.begin());
  positions_span_ =
      base::VectorOf(positions_span_.begin(),
                     positions_span_.size() + next_->positions_span_.size());

  // Join linked lists of live ranges.
  LiveRange* old_next = next_;
  next_ = next_->next_;
  old_next->next_ = nullptr;
}

void LiveRange::Unspill() {
  DCHECK(spilled());
  set_spilled(false);
  bits_ = AssignedRegisterField::update(bits_, kUnassignedRegister);
}

void LiveRange::Spill() {
  DCHECK(!spilled());
  DCHECK(!TopLevel()->HasNoSpillType());
  set_spilled(true);
  bits_ = AssignedRegisterField::update(bits_, kUnassignedRegister);
}

RegisterKind LiveRange::kind() const {
  if (kFPAliasing == AliasingKind::kIndependent &&
      IsSimd128(representation())) {
    return RegisterKind::kSimd128;
  } else {
    return IsFloatingPoint(representation()) ? RegisterKind::kDouble
                                             : RegisterKind::kGeneral;
  }
}

bool LiveRange::RegisterFromFirstHint(int* register_index) {
  DCHECK_LE(current_hint_position_index_, positions_span_.size());
  if (current_hint_position_index_ == positions_span_.size()) {
    return false;
  }
  DCHECK_GE(positions_span_[current_hint_position_index_]->pos(),
            positions_span_.first()->pos());
  DCHECK_LE(positions_span_[current_hint_position_index_]->pos(), End());

  bool needs_revisit = false;
  UsePosition** pos_it = positions_span_.begin() + current_hint_position_index_;
  for (; pos_it != positions_span_.end(); ++pos_it) {
    if ((*pos_it)->HintRegister(register_index)) {
      break;
    }
    // Phi and use position hints can be assigned during allocation which
    // would invalidate the cached hint position. Make sure we revisit them.
    needs_revisit = needs_revisit ||
                    (*pos_it)->hint_type() == UsePositionHintType::kPhi ||
                    (*pos_it)->hint_type() == UsePositionHintType::kUsePos;
  }
  if (!needs_revisit) {
    current_hint_position_index_ =
        std::distance(positions_span_.begin(), pos_it);
  }
#ifdef DEBUG
  UsePosition** pos_check_it =
      std::find_if(positions_span_.begin(), positions_span_.end(),
                   [](UsePosition* pos) { return pos->HasHint(); });
  CHECK_EQ(pos_it, pos_check_it);
#endif
  return pos_it != positions_span_.end();
}

UsePosition* const* LiveRange::NextUsePosition(LifetimePosition start) const {
  return std::lower_bound(positions_span_.cbegin(), positions_span_.cend(),
                          start, [](UsePosition* use, LifetimePosition start) {
                            return use->pos() < start;
                          });
}

UsePosition* LiveRange::NextUsePositionRegisterIsBeneficial(
    LifetimePosition start) const {
  UsePosition* const* use_pos_it = std::find_if(
      NextUsePosition(start), positions_span_.cend(),
      [](const UsePosition* pos) { return pos->RegisterIsBeneficial(); });
  return use_pos_it == positions_span_.cend() ? nullptr : *use_pos_it;
}

LifetimePosition LiveRange::NextLifetimePositionRegisterIsBeneficial(
    const LifetimePosition& start) const {
  UsePosition* next_use = NextUsePositionRegisterIsBeneficial(start);
  if (next_use == nullptr) return End();
  return next_use->pos();
}

UsePosition* LiveRange::NextUsePositionSpillDetrimental(
    LifetimePosition start) const {
  UsePosition* const* use_pos_it =
      std::find_if(NextUsePosition(start), positions_span_.cend(),
                   [](const UsePosition* pos) {
                     return pos->type() == UsePositionType::kRequiresRegister ||
                            pos->SpillDetrimental();
                   });
  return use_pos_it == positions_span_.cend() ? nullptr : *use_pos_it;
}

UsePosition* LiveRange::NextRegisterPosition(LifetimePosition start) const {
  UsePosition* const* use_pos_it =
      std::find_if(NextUsePosition(start), positions_span_.cend(),
                   [](const UsePosition* pos) {
                     return pos->type() == UsePositionType::kRequiresRegister;
                   });
  return use_pos_it == positions_span_.cend() ? nullptr : *use_pos_it;
}

bool LiveRange::CanBeSpilled(LifetimePosition pos) const {
  // We cannot spill a live range that has a use requiring a register
  // at the current or the immediate next position.
  UsePosition* use_pos = NextRegisterPosition(pos);
  if (use_pos == nullptr) return true;
  return use_pos->pos() > pos.NextStart().End();
}

bool LiveRange::IsTopLevel() const { return top_level_ == this; }

InstructionOperand LiveRange::GetAssignedOperand() const {
  DCHECK(!IsEmpty());
  if (HasRegisterAssigned()) {
    DCHECK(!spilled());
    return AllocatedOperand(LocationOperand::REGISTER, representation(),
                            assigned_register());
  }
  DCHECK(spilled());
  DCHECK(!HasRegisterAssigned());
  if (TopLevel()->HasSpillOperand()) {
    InstructionOperand* op = TopLevel()->GetSpillOperand();
    DCHECK(!op->IsUnallocated());
    return *op;
  }
  return TopLevel()->GetSpillRangeOperand();
}

UseIntervalVector::iterator LiveRange::FirstSearchIntervalForPosition(
    LifetimePosition position) {
  DCHECK_NE(current_interval_, intervals_.end());
  if (current_interval_->start() > position) {
    current_interval_ = std::lower_bound(
        intervals_.begin(), intervals_.end(), position,
        [](const UseInterval& interval, LifetimePosition position) {
          return interval.end() < position;
        });
  }
  return current_interval_;
}

void LiveRange::AdvanceLastProcessedMarker(
    UseIntervalVector::iterator to_start_of, LifetimePosition but_not_past) {
  DCHECK_LE(intervals_.begin(), to_start_of);
  DCHECK_LT(to_start_of, intervals_.end());
  DCHECK_NE(current_interval_, intervals_.end());
  if (to_start_of->start() > but_not_past) return;
  if (to_start_of->start() > current_interval_->start()) {
    current_interval_ = to_start_of;
  }
}

LiveRange* LiveRange::SplitAt(LifetimePosition position, Zone* zone) {
  DCHECK(Start() < position);
  DCHECK(End() > position);

  int new_id = TopLevel()->GetNextChildId();
  LiveRange* result =
      zone->New<LiveRange>(new_id, representation(), TopLevel());

  // Partition original use intervals to the two live ranges.

  // Find the first interval that ends after the position. (This either needs
  // to be split or completely belongs to the split-off LiveRange.)
  UseIntervalVector::iterator split_interval = std::upper_bound(
      intervals_.begin(), intervals_.end(), position,
      [](LifetimePosition position, const UseInterval& interval) {
        return position < interval.end();
      });
  DCHECK_NE(split_interval, intervals_.end());

  bool split_at_start = false;
  if (split_interval->start() == position) {
    split_at_start = true;
  } else if (split_interval->Contains(position)) {
    UseInterval new_interval = split_interval->SplitAt(position);
    split_interval = intervals_.insert(zone, split_interval + 1, new_interval);
  }
  result->intervals_ = intervals_.SplitAt(split_interval);
  DCHECK(!intervals_.empty());
  DCHECK(!result->intervals_.empty());

  result->start_ = result->intervals_.front().start();
  result->end_ = end_;
  end_ = intervals_.back().end();

  // Partition use positions.
  UsePosition** split_position_it;
  if (split_at_start) {
    // The split position coincides with the beginning of a use interval
    // (the end of a lifetime hole). Use at this position should be attributed
    // to the split child because split child owns use interval covering it.
    split_position_it = std::lower_bound(
        positions_span_.begin(), positions_span_.end(), position,
        [](const UsePosition* use_pos, LifetimePosition pos) {
          return use_pos->pos() < pos;
        });
  } else {
    split_position_it = std::lower_bound(
        positions_span_.begin(), positions_span_.end(), position,
        [](const UsePosition* use_pos, LifetimePosition pos) {
          return use_pos->pos() <= pos;
        });
  }

  size_t result_size = std::distance(split_position_it, positions_span_.end());
  result->positions_span_ = base::VectorOf(split_position_it, result_size);
  positions_span_.Truncate(positions_span_.size() - result_size);

  // Update or discard cached iteration state to make sure it does not point
  // to use positions and intervals that no longer belong to this live range.
  if (current_hint_position_index_ >= positions_span_.size()) {
    result->current_hint_position_index_ =
        current_hint_position_index_ - positions_span_.size();
    current_hint_position_index_ = 0;
  }

  current_interval_ = intervals_.begin();
  result->current_interval_ = result->intervals_.begin();

#ifdef DEBUG
  VerifyChildStructure();
  result->VerifyChildStructure();
#endif

  result->top_level_ = TopLevel();
  result->next_ = next_;
  next_ = result;

  // Update cache for `TopLevelLiveRange::GetChildCovers()`.
  auto& children = TopLevel()->children_;
  children.insert(std::upper_bound(children.begin(), children.end(), result,
                                   LiveRangeOrdering()),
                  1, result);
  return result;
}

void LiveRange::ConvertUsesToOperand(const InstructionOperand& op,
                                     const InstructionOperand& spill_op) {
  for (UsePosition* pos : positions_span_) {
    DCHECK(Start() <= pos->pos() && pos->pos() <= End());
    if (!pos->HasOperand()) continue;
    switch (pos->type()) {
      case UsePositionType::kRequiresSlot:
        DCHECK(spill_op.IsStackSlot() || spill_op.IsFPStackSlot());
        InstructionOperand::ReplaceWith(pos->operand(), &spill_op);
        break;
      case UsePositionType::kRequiresRegister:
        DCHECK(op.IsRegister() || op.IsFPRegister());
        [[fallthrough]];
      case UsePositionType::kRegisterOrSlot:
      case UsePositionType::kRegisterOrSlotOrConstant:
        InstructionOperand::ReplaceWith(pos->operand(), &op);
        break;
    }
  }
}

// This implements an ordering on live ranges so that they are ordered by their
// start positions.  This is needed for the correctness of the register
// allocation algorithm.  If two live ranges start at the same offset then there
// is a tie breaker based on where the value is first used.  This part of the
// ordering is merely a heuristic.
bool LiveRange::ShouldBeAllocatedBefore(const LiveRange* other) const {
  LifetimePosition start = Start();
  LifetimePosition other_start = other->Start();
  if (start == other_start) {
    // Prefer register that has a controlflow hint to make sure it gets
    // allocated first. This allows the control flow aware alloction to
    // just put ranges back into the queue without other ranges interfering.
    if (controlflow_hint() < other->controlflow_hint()) {
      return true;
    }
    // The other has a smaller hint.
    if (controlflow_hint() > other->controlflow_hint()) {
      return false;
    }
    // Both have the same hint or no hint at all. Use first use position.
    // To make the order total, handle the case where both positions are null.
    if (positions_span_.empty() && other->positions_span_.empty()) {
      return TopLevel()->vreg() < other->TopLevel()->vreg();
    }
    if (positions_span_.empty()) return false;
    if (other->positions_span_.empty()) return true;
    UsePosition* pos = positions_span_.first();
    UsePosition* other_pos = other->positions_span_.first();
    // To make the order total, handle the case where both positions are equal.
    if (pos->pos() == other_pos->pos())
      return TopLevel()->vreg() < other->TopLevel()->vreg();
    return pos->pos() < other_pos->pos();
  }
  return start < other_start;
}

void LiveRange::SetUseHints(int register_index) {
  for (UsePosition* pos : positions_span_) {
    if (!pos->HasOperand()) continue;
    switch (pos->type()) {
      case UsePositionType::kRequiresSlot:
        break;
      case UsePositionType::kRequiresRegister:
      case UsePositionType::kRegisterOrSlot:
      case UsePositionType::kRegisterOrSlotOrConstant:
        pos->set_assigned_register(register_index);
        break;
    }
  }
}

bool LiveRange::CanCover(LifetimePosition position) const {
  if (IsEmpty()) return false;
  return Start() <= position && position < End();
}

bool LiveRange::Covers(LifetimePosition position) {
  if (!CanCover(position)) return false;
  bool covers = false;
  UseIntervalVector::iterator interval =
      FirstSearchIntervalForPosition(position);
  while (interval != intervals().end() && interval->start() <= position) {
    // The list of `UseInterval`s shall be sorted.
    DCHECK(interval + 1 == intervals().end() ||
           interval[1].start() >= interval->start());
    if (interval->Contains(position)) {
      covers = true;
      break;
    }
    ++interval;
  }
  if (!covers && interval > intervals_.begin()) {
    // To ensure that we advance {current_interval_} below, move back to the
    // last interval starting before position.
    interval--;
    DCHECK_LE(interval->start(), position);
  }
  AdvanceLastProcessedMarker(interval, position);
  return covers;
}

LifetimePosition LiveRange::NextEndAfter(LifetimePosition position) {
  // NOTE: A binary search was measured to be slower, e.g., on the binary from
  // https://crbug.com/v8/9529.
  UseIntervalVector::iterator interval = std::find_if(
      FirstSearchIntervalForPosition(position), intervals_.end(),
      [=](const UseInterval& interval) { return interval.end() >= position; });
  DCHECK_NE(interval, intervals().end());
  return interval->end();
}

LifetimePosition LiveRange::NextStartAfter(LifetimePosition position) {
  // NOTE: A binary search was measured to be slower, e.g., on the binary from
  // https://crbug.com/v8/9529.
  UseIntervalVector::iterator interval =
      std::find_if(FirstSearchIntervalForPosition(position), intervals_.end(),
                   [=](const UseInterval& interval) {
                     return interval.start() >= position;
                   });
  DCHECK_NE(interval, intervals().end());
  next_start_ = interval->start();
  return next_start_;
}

LifetimePosition LiveRange::FirstIntersection(LiveRange* other) {
  if (IsEmpty() || other->IsEmpty() || other->Start() > End() ||
      Start() > other->End())
    return LifetimePosition::Invalid();

  LifetimePosition min_end = std::min(End(), other->End());
  UseIntervalVector::iterator b = other->intervals_.begin();
  LifetimePosition advance_last_processed_up_to = b->start();
  UseIntervalVector::iterator a = FirstSearchIntervalForPosition(b->start());
  while (a != intervals().end() && b != other->intervals().end()) {
    if (a->start() > min_end || b->start() > min_end) break;
    LifetimePosition cur_intersection = a->Intersect(*b);
    if (cur_intersection.IsValid()) {
      return cur_intersection;
    }
    if (a->start() < b->start()) {
      ++a;
      if (a == intervals().end() || a->start() > other->End()) break;
      AdvanceLastProcessedMarker(a, advance_last_processed_up_to);
    } else {
      ++b;
    }
  }
  return LifetimePosition::Invalid();
}

void LiveRange::Print(const RegisterConfiguration* config,
                      bool with_children) const {
  StdoutStream os;
  PrintableLiveRange wrapper;
  wrapper.register_configuration_ = config;
  for (const LiveRange* i = this; i != nullptr; i = i->next()) {
    wrapper.range_ = i;
    os << wrapper << std::endl;
    if (!with_children) break;
  }
}

void LiveRange::Print(bool with_children) const {
  Print(RegisterConfiguration::Default(), with_children);
}

bool LiveRange::RegisterFromBundle(int* hint) const {
  LiveRangeBundle* bundle = TopLevel()->get_bundle();
  if (bundle == nullptr || bundle->reg() == kUnassignedRegister) return false;
  *hint = bundle->reg();
  return true;
}

void LiveRange::UpdateBundleRegister(int reg) const {
  LiveRangeBundle* bundle = TopLevel()->get_bundle();
  if (bundle == nullptr || bundle->reg() != kUnassignedRegister) return;
  bundle->set_reg(reg);
}

struct TopLevelLiveRange::SpillMoveInsertionList : ZoneObject {
  SpillMoveInsertionList(int gap_index, InstructionOperand* operand,
                         SpillMoveInsertionList* next)
      : gap_index(gap_index), operand(operand), next(next) {}
  const int gap_index;
  InstructionOperand* const operand;
  SpillMoveInsertionList* next;
};

TopLevelLiveRange::TopLevelLiveRange(int vreg, MachineRepresentation rep,
                                     Zone* zone)
    : LiveRange(0, rep, this),
      vreg_(vreg),
      last_child_id_(0),
      spill_operand_(nullptr),
      spill_move_insertion_locations_(nullptr),
      children_({this}, zone),
      spilled_in_deferred_blocks_(false),
      has_preassigned_slot_(false),
      spill_start_index_(kMaxInt) {
  bits_ |= SpillTypeField::encode(SpillType::kNoSpillType);
}

void TopLevelLiveRange::RecordSpillLocation(Zone* zone, int gap_index,
                                            InstructionOperand* operand) {
  DCHECK(HasNoSpillType());
  spill_move_insertion_locations_ = zone->New<SpillMoveInsertionList>(
      gap_index, operand, spill_move_insertion_locations_);
}

void TopLevelLiveRange::CommitSpillMoves(RegisterAllocationData* data,
                                         const InstructionOperand& op) {
  DCHECK_IMPLIES(op.IsConstant(),
                 GetSpillMoveInsertionLocations(data) == nullptr);

  if (HasGeneralSpillRange()) {
    SetLateSpillingSelected(false);
  }

  InstructionSequence* sequence = data->code();
  Zone* zone = sequence->zone();

  for (SpillMoveInsertionList* to_spill = GetSpillMoveInsertionLocations(data);
       to_spill != nullptr; to_spill = to_spill->next) {
    Instruction* instr = sequence->InstructionAt(to_spill->gap_index);
    ParallelMove* move =
        instr->GetOrCreateParallelMove(Instruction::START, zone);
    move->AddMove(*to_spill->operand, op);
    instr->block()->mark_needs_frame();
  }
}

void TopLevelLiveRange::FilterSpillMoves(RegisterAllocationData* data,
                                         const InstructionOperand& op) {
  DCHECK_IMPLIES(op.IsConstant(),
                 GetSpillMoveInsertionLocations(data) == nullptr);
  bool might_be_duplicated = has_slot_use() || spilled();
  InstructionSequence* sequence = data->code();

  SpillMoveInsertionList* previous = nullptr;
  for (SpillMoveInsertionList* to_spill = GetSpillMoveInsertionLocations(data);
       to_spill != nullptr; previous = to_spill, to_spill = to_spill->next) {
    Instruction* instr = sequence->InstructionAt(to_spill->gap_index);
    ParallelMove* move = instr->GetParallelMove(Instruction::START);
    // Skip insertion if it's possible that the move exists already as a
    // constraint move from a fixed output register to a slot.
    bool found = false;
    if (move != nullptr && (might_be_duplicated || has_preassigned_slot())) {
      for (MoveOperands* move_op : *move) {
        if (move_op->IsEliminated()) continue;
        if (move_op->source().Equals(*to_spill->operand) &&
            move_op->destination().Equals(op)) {
          found = true;
          if (has_preassigned_slot()) move_op->Eliminate();
          break;
        }
      }
    }
    if (found || has_preassigned_slot()) {
      // Remove the item from the list.
      if (previous == nullptr) {
        spill_move_insertion_locations_ = to_spill->next;
      } else {
        previous->next = to_spill->next;
      }
      // Even though this location doesn't need a spill instruction, the
      // block does require a frame.
      instr->block()->mark_needs_frame();
    }
  }
}

void TopLevelLiveRange::SetSpillOperand(InstructionOperand* operand) {
  DCHECK(HasNoSpillType());
  DCHECK(!operand->IsUnallocated() && !operand->IsImmediate());
  set_spill_type(SpillType::kSpillOperand);
  spill_operand_ = operand;
}

void TopLevelLiveRange::SetSpillRange(SpillRange* spill_range) {
  DCHECK(!HasSpillOperand());
  DCHECK(spill_range);
  spill_range_ = spill_range;
}

AllocatedOperand TopLevelLiveRange::GetSpillRangeOperand() const {
  SpillRange* spill_range = GetSpillRange();
  int index = spill_range->assigned_slot();
  return AllocatedOperand(LocationOperand::STACK_SLOT, representation(), index);
}

LiveRange* TopLevelLiveRange::GetChildCovers(LifetimePosition pos) {
#ifdef DEBUG
  // Make sure the cache contains the correct, actual children.
  LiveRange* child = this;
  for (LiveRange* cached_child : children_) {
    DCHECK_EQ(cached_child, child);
    child = child->next();
  }
  DCHECK_NULL(child);
#endif

  auto child_it =
      std::lower_bound(children_.begin(), children_.end(), pos,
                       [](const LiveRange* range, LifetimePosition pos) {
                         return range->End() <= pos;
                       });
  return child_it == children_.end() || !(*child_it)->Covers(pos) ? nullptr
                                                                  : *child_it;
}

#ifdef DEBUG
void TopLevelLiveRange::Verify() const {
  VerifyChildrenInOrder();
  for (const LiveRange* child = this; child != nullptr; child = child->next()) {
    VerifyChildStructure();
  }
}

void TopLevelLiveRange::VerifyChildrenInOrder() const {
  LifetimePosition last_end = End();
  for (const LiveRange* child = this->next(); child != nullptr;
       child = child->next()) {
    DCHECK(last_end <= child->Start());
    last_end = child->End();
  }
}
#endif

void TopLevelLiveRange::ShortenTo(LifetimePosition start) {
  TRACE("Shorten live range %d to [%d\n", vreg(), start.value());
  DCHECK(!IsEmpty());
  DCHECK_LE(intervals_.front().start(), start);
  intervals_.front().set_start(start);
  start_ = start;
}

void TopLevelLiveRange::EnsureInterval(LifetimePosition start,
                                       LifetimePosition end, Zone* zone) {
  TRACE("Ensure live range %d in interval [%d %d[\n", vreg(), start.value(),
        end.value());
  DCHECK(!IsEmpty());

  // Drop front intervals until intervals_.front().start() > end.
  LifetimePosition new_end = end;
  while (!intervals_.empty() && intervals_.front().start() <= end) {
    if (intervals_.front().end() > end) {
      new_end = intervals_.front().end();
    }
    intervals_.pop_front();
  }
  intervals_.push_front(zone, UseInterval(start, new_end));
  current_interval_ = intervals_.begin();
  if (end_ < new_end) {
    end_ = new_end;
  }
  if (start_ > start) {
    start_ = start;
  }
}

void TopLevelLiveRange::AddUseInterval(LifetimePosition start,
                                       LifetimePosition end, Zone* zone) {
  TRACE("Add to live range %d interval [%d %d[\n", vreg(), start.value(),
        end.value());
  if (intervals_.empty()) {
    intervals_.push_front(zone, UseInterval(start, end));
    start_ = start;
    end_ = end;
  } else {
    UseInterval& first_interval = intervals_.front();
    if (end == first_interval.start()) {
      // Coalesce directly adjacent intervals.
      first_interval.set_start(start);
      start_ = start;
    } else if (end < first_interval.start()) {
      intervals_.push_front(zone, UseInterval(start, end));
      start_ = start;
    } else {
      // Order of instruction's processing (see ProcessInstructions) guarantees
      // that each new use interval either precedes, intersects with or touches
      // the last added interval.
      DCHECK(intervals_.size() == 1 || end <= intervals_.begin()[1].start());
      first_interval.set_start(std::min(start, first_interval.start()));
      first_interval.set_end(std::max(end, first_interval.end()));
      if (start_ > start) {
        start_ = start;
      }
      if (end_ < end) {
        end_ = end;
      }
    }
  }
  current_interval_ = intervals_.begin();
}

void TopLevelLiveRange::AddUsePosition(UsePosition* use_pos, Zone* zone) {
  TRACE("Add to live range %d use position %d\n", vreg(),
        use_pos->pos().value());
  // Since we `ProcessInstructions` in reverse, the `use_pos` is almost always
  // inserted at the front of `positions_`, hence (i) use linear instead of
  // binary search and (ii) grow towards the `kFront` exclusively on `insert`.
  UsePositionVector::iterator insert_it = std::find_if(
      positions_.begin(), positions_.end(), [=](const UsePosition* pos) {
        return UsePosition::Ordering()(use_pos, pos);
      });
  positions_.insert<kFront>(zone, insert_it, use_pos);

  positions_span_ = base::VectorOf(positions_);
  // We must not have child `LiveRange`s yet (e.g. from splitting), otherwise we
  // would have to adjust their `positions_span_` as well.
  DCHECK_NULL(next_);
}

std::ostream& operator<<(std::ostream& os,
                         const PrintableLiveRange& printable_range) {
  const LiveRange* range = printable_range.range_;
  os << "Range: " << range->TopLevel()->vreg() << ":" << range->relative_id()
     << " ";
  if (range->TopLevel()->is_phi()) os << "phi ";
  if (range->TopLevel()->is_non_loop_phi()) os << "nlphi ";

  os << "{" << std::endl;
  for (UsePosition* use_pos : range->positions()) {
    if (use_pos->HasOperand()) {
      os << *use_pos->operand() << use_pos->pos() << " ";
    }
  }
  os << std::endl;

  for (const UseInterval& interval : range->intervals()) {
    interval.PrettyPrint(os);
    os << std::endl;
  }
  os << "}";
  return os;
}

namespace {
void PrintBlockRow(std::ostream& os, const InstructionBlocks& blocks) {
  os << "     ";
  for (auto block : blocks) {
    LifetimePosition start_pos = LifetimePosition::GapFromInstructionIndex(
        block->first_instruction_index());
    LifetimePosition end_pos = LifetimePosition::GapFromInstructionIndex(
                                   block->last_instruction_index())
                                   .NextFullStart();
    int length = end_pos.value() - start_pos.value();
    constexpr int kMaxPrefixLength = 32;
    char buffer[kMaxPrefixLength];
    int rpo_number = block->rpo_number().ToInt();
    const char* deferred_marker = block->IsDeferred() ? "(deferred)" : "";
    int max_prefix_length = std::min(length, kMaxPrefixLength);
    int prefix = snprintf(buffer, max_prefix_length, "[-B%d-%s", rpo_number,
                          deferred_marker);
    os << buffer;
    int remaining = length - std::min(prefix, max_prefix_length) - 1;
    for (int i = 0; i < remaining; ++i) os << '-';
    os << ']';
  }
  os << '\n';
}
}  // namespace

void LinearScanAllocator::PrintRangeRow(std::ostream& os,
                                        const TopLevelLiveRange* toplevel) {
  int position = 0;
  os << std::setw(3) << toplevel->vreg() << ": ";

  const char* kind_string;
  switch (toplevel->spill_type()) {
    case TopLevelLiveRange::SpillType::kSpillRange:
      kind_string = "ss";
      break;
    case TopLevelLiveRange::SpillType::kDeferredSpillRange:
      kind_string = "sd";
      break;
    case TopLevelLiveRange::SpillType::kSpillOperand:
      kind_string = "so";
      break;
    default:
      kind_string = "s?";
  }

  for (const LiveRange* range = toplevel; range != nullptr;
       range = range->next()) {
    for (const UseInterval& interval : range->intervals()) {
      LifetimePosition start = interval.start();
      LifetimePosition end = interval.end();
      CHECK_GE(start.value(), position);
      for (; start.value() > position; position++) {
        os << ' ';
      }
      int length = end.value() - start.value();
      constexpr int kMaxPrefixLength = 32;
      char buffer[kMaxPrefixLength];
      int max_prefix_length = std::min(length + 1, kMaxPrefixLength);
      int prefix;
      if (range->spilled()) {
        prefix = snprintf(buffer, max_prefix_length, "|%s", kind_string);
      } else {
        prefix = snprintf(buffer, max_prefix_length, "|%s",
                          RegisterName(range->assigned_register()));
      }
      os << buffer;
      position += std::min(prefix, max_prefix_length - 1);
      CHECK_GE(end.value(), position);
      const char line_style = range->spilled() ? '-' : '=';
      for (; end.value() > position; position++) {
        os << line_style;
      }
    }
  }
  os << '\n';
}

void LinearScanAllocator::PrintRangeOverview() {
  std::ostringstream os;
  PrintBlockRow(os, code()->instruction_blocks());
  for (auto const toplevel : data()->fixed_live_ranges()) {
    if (toplevel == nullptr) continue;
    PrintRangeRow(os, toplevel);
  }
  int rowcount = 0;
  for (auto toplevel : data()->live_ranges()) {
    if (!CanProcessRange(toplevel)) continue;
    if (rowcount++ % 10 == 0) PrintBlockRow(os, code()->instruction_blocks());
    PrintRangeRow(os, toplevel);
  }
  PrintF("%s\n", os.str().c_str());
}

SpillRange::SpillRange(TopLevelLiveRange* parent, Zone* zone)
    : ranges_(zone),
      intervals_(zone),
      assigned_slot_(kUnassignedSlot),
      byte_width_(ByteWidthForStackSlot(parent->representation())) {
  DCHECK(!parent->IsEmpty());

  // Spill ranges are created for top level. This is so that, when merging
  // decisions are made, we consider the full extent of the virtual register,
  // and avoid clobbering it.
  LifetimePosition last_end = LifetimePosition::MaxPosition();
  for (const LiveRange* range = parent; range != nullptr;
       range = range->next()) {
    // Deep copy the `UseInterval`s, since the `LiveRange`s are subsequently
    // modified, so just storing those has correctness issues.
    for (UseInterval interval : range->intervals()) {
      DCHECK_NE(LifetimePosition::MaxPosition(), interval.start());
      bool can_coalesce = last_end == interval.start();
      if (can_coalesce) {
        intervals_.back().set_end(interval.end());
      } else {
        intervals_.push_back(interval);
      }
      last_end = interval.end();
    }
  }
  ranges_.push_back(parent);
  parent->SetSpillRange(this);
}

// Checks if the `UseInterval`s in `a` intersect with those in `b`.
// Returns the two intervals that intersected, or `std::nullopt` if none did.
static base::Optional<std::pair<UseInterval, UseInterval>>
AreUseIntervalsIntersectingVector(base::Vector<const UseInterval> a,
                                  base::Vector<const UseInterval> b) {
  SLOW_DCHECK(std::is_sorted(a.begin(), a.end()) &&
              std::is_sorted(b.begin(), b.end()));
  if (a.empty() || b.empty() || a.last().end() <= b.first().start() ||
      b.last().end() <= a.first().start()) {
    return {};
  }

  // `a` shall have less intervals then `b`.
  if (a.size() > b.size()) {
    std::swap(a, b);
  }

  auto a_it = a.begin();
  // Advance `b` already to the interval that ends at or after `a_start`.
  LifetimePosition a_start = a.first().start();
  auto b_it = std::lower_bound(
      b.begin(), b.end(), a_start,
      [](const UseInterval& interval, LifetimePosition position) {
        return interval.end() < position;
      });
  while (a_it != a.end() && b_it != b.end()) {
    if (a_it->end() <= b_it->start()) {
      ++a_it;
    } else if (b_it->end() <= a_it->start()) {
      ++b_it;
    } else {
      return std::make_pair(*a_it, *b_it);
    }
  }
  return {};
}

// Used by `LiveRangeBundle`s and `SpillRange`s, hence allow passing different
// containers of `UseInterval`s, as long as they can be converted to a
// `base::Vector` (which is essentially just a memory span).
template <typename ContainerA, typename ContainerB>
base::Optional<std::pair<UseInterval, UseInterval>> AreUseIntervalsIntersecting(
    const ContainerA& a, const ContainerB& b) {
  return AreUseIntervalsIntersectingVector(base::VectorOf(a),
                                           base::VectorOf(b));
}

bool SpillRange::TryMerge(SpillRange* other) {
  if (HasSlot() || other->HasSlot()) return false;
  if (byte_width() != other->byte_width()) return false;
  if (AreUseIntervalsIntersecting(intervals_, other->intervals_)) return false;

  // Merge vectors of `UseInterval`s.
  intervals_.reserve(intervals_.size() + other->intervals_.size());
  for (UseInterval interval : other->intervals_) {
    UseInterval* insert_it =
        std::lower_bound(intervals_.begin(), intervals_.end(), interval);
    // Since the intervals didn't intersect, they should also be unique.
    DCHECK_IMPLIES(insert_it != intervals_.end(), *insert_it != interval);
    intervals_.insert(insert_it, 1, interval);
  }
  other->intervals_.clear();

  // Merge vectors of `TopLevelLiveRange`s.
  for (TopLevelLiveRange* range : other->ranges_) {
    DCHECK(range->GetSpillRange() == other);
    range->SetSpillRange(this);
  }
  ranges_.insert(ranges_.end(), other->ranges_.begin(), other->ranges_.end());
  other->ranges_.clear();

  return true;
}

void SpillRange::Print() const {
  StdoutStream os;
  os << "{" << std::endl;
  for (const TopLevelLiveRange* range : ranges_) {
    os << range->vreg() << " ";
  }
  os << std::endl;

  for (const UseInterval& interval : intervals_) {
    interval.PrettyPrint(os);
    os << std::endl;
  }
  os << "}" << std::endl;
}

RegisterAllocationData::PhiMapValue::PhiMapValue(PhiInstruction* phi,
                                                 const InstructionBlock* block,
                                                 Zone* zone)
    : phi_(phi),
      block_(block),
      incoming_operands_(zone),
      assigned_register_(kUnassignedRegister) {
  incoming_operands_.reserve(phi->operands().size());
}

void RegisterAllocationData::PhiMapValue::AddOperand(
    InstructionOperand* operand) {
  incoming_operands_.push_back(operand);
}

void RegisterAllocationData::PhiMapValue::CommitAssignment(
    const InstructionOperand& assigned) {
  for (InstructionOperand* operand : incoming_operands_) {
    InstructionOperand::ReplaceWith(operand, &assigned);
  }
}

RegisterAllocationData::RegisterAllocationData(
    const RegisterConfiguration* config, Zone* zone, Frame* frame,
    InstructionSequence* code, TickCounter* tick_counter,
    const char* debug_name)
    : allocation_zone_(zone),
      frame_(frame),
      code_(code),
      debug_name_(debug_name),
      config_(config),
      phi_map_(allocation_zone()),
      live_in_sets_(code->InstructionBlockCount(), nullptr, allocation_zone()),
      live_out_sets_(code->InstructionBlockCount(), nullptr, allocation_zone()),
      live_ranges_(code->VirtualRegisterCount(), nullptr, allocation_zone()),
      fixed_live_ranges_(kNumberOfFixedRangesPerRegister *
                             this->config()->num_general_registers(),
                         nullptr, allocation_zone()),
      fixed_float_live_ranges_(allocation_zone()),
      fixed_double_live_ranges_(kNumberOfFixedRangesPerRegister *
                                    this->config()->num_double_registers(),
                                nullptr, allocation_zone()),
      fixed_simd128_live_ranges_(allocation_zone()),
      delayed_references_(allocation_zone()),
      assigned_registers_(nullptr),
      assigned_double_registers_(nullptr),
      virtual_register_count_(code->VirtualRegisterCount()),
      preassigned_slot_ranges_(zone),
      spill_state_(code->InstructionBlockCount(), ZoneVector<LiveRange*>(zone),
                   zone),
      tick_counter_(tick_counter),
      slot_for_const_range_(zone) {
  if (kFPAliasing == AliasingKind::kCombine) {
    fixed_float_live_ranges_.resize(
        kNumberOfFixedRangesPerRegister * this->config()->num_float_registers(),
        nullptr);
    fixed_simd128_live_ranges_.resize(
        kNumberOfFixedRangesPerRegister *
            this->config()->num_simd128_registers(),
        nullptr);
  } else if (kFPAliasing == AliasingKind::kIndependent) {
    fixed_simd128_live_ranges_.resize(
        kNumberOfFixedRangesPerRegister *
            this->config()->num_simd128_registers(),
        nullptr);
  }

  // Eagerly initialize live ranges to avoid repeated null checks.
  DCHECK_EQ(code->VirtualRegisterCount(), live_ranges_.size());
  for (int i = 0; i < code->VirtualRegisterCount(); ++i) {
    live_ranges_[i] = NewLiveRange(i, RepresentationFor(i));
  }

  assigned_registers_ = code_zone()->New<BitVector>(
      this->config()->num_general_registers(), code_zone());
  assigned_double_registers_ = code_zone()->New<BitVector>(
      this->config()->num_double_registers(), code_zone());
  fixed_register_use_ = code_zone()->New<BitVector>(
      this->config()->num_general_registers(), code_zone());
  fixed_fp_register_use_ = code_zone()->New<BitVector>(
      this->config()->num_double_registers(), code_zone());
  if (kFPAliasing == AliasingKind::kIndependent) {
    assigned_simd128_registers_ = code_zone()->New<BitVector>(
        this->config()->num_simd128_registers(), code_zone());
    fixed_simd128_register_use_ = code_zone()->New<BitVector>(
        this->config()->num_simd128_registers(), code_zone());
  }

  this->frame()->SetAllocatedRegisters(assigned_registers_);
  this->frame()->SetAllocatedDoubleRegisters(assigned_double_registers_);
}

MoveOperands* RegisterAllocationData::AddGapMove(
    int index, Instruction::GapPosition position,
    const InstructionOperand& from, const InstructionOperand& to) {
  Instruction* instr = code()->InstructionAt(index);
  ParallelMove* moves = instr->GetOrCreateParallelMove(position, code_zone());
  return moves->AddMove(from, to);
}

MachineRepresentation RegisterAllocationData::RepresentationFor(
    int virtual_register) {
  DCHECK_LT(virtual_register, code()->VirtualRegisterCount());
  return code()->GetRepresentation(virtual_register);
}

TopLevelLiveRange* RegisterAllocationData::GetLiveRangeFor(int index) {
  TopLevelLiveRange* result = live_ranges()[index];
  DCHECK_NOT_NULL(result);
  DCHECK_EQ(live_ranges()[index]->vreg(), index);
  return result;
}

TopLevelLiveRange* RegisterAllocationData::NewLiveRange(
    int index, MachineRepresentation rep) {
  return allocation_zone()->New<TopLevelLiveRange>(index, rep,
                                                   allocation_zone());
}

RegisterAllocationData::PhiMapValue* RegisterAllocationData::InitializePhiMap(
    const InstructionBlock* block, PhiInstruction* phi) {
  RegisterAllocationData::PhiMapValue* map_value =
      allocation_zone()->New<RegisterAllocationData::PhiMapValue>(
          phi, block, allocation_zone());
  auto res =
      phi_map_.insert(std::make_pair(phi->virtual_register(), map_value));
  DCHECK(res.second);
  USE(res);
  return map_value;
}

RegisterAllocationData::PhiMapValue* RegisterAllocationData::GetPhiMapValueFor(
    int virtual_register) {
  auto it = phi_map_.find(virtual_register);
  DCHECK(it != phi_map_.end());
  return it->second;
}

RegisterAllocationData::PhiMapValue* RegisterAllocationData::GetPhiMapValueFor(
    TopLevelLiveRange* top_range) {
  return GetPhiMapValueFor(top_range->vreg());
}

bool RegisterAllocationData::ExistsUseWithoutDefinition() {
  bool found = false;
  for (int operand_index : *live_in_sets()[0]) {
    found = true;
    PrintF("Register allocator error: live v%d reached first block.\n",
           operand_index);
    LiveRange* range = GetLiveRangeFor(operand_index);
    PrintF("  (first use is at position %d in instruction %d)\n",
           range->positions().first()->pos().value(),
           range->positions().first()->pos().ToInstructionIndex());
    if (debug_name() == nullptr) {
      PrintF("\n");
    } else {
      PrintF("  (function: %s)\n", debug_name());
    }
  }
  return found;
}

// If a range is defined in a deferred block, we can expect all the range
// to only cover positions in deferred blocks. Otherwise, a block on the
// hot path would be dominated by a deferred block, meaning it is unreachable
// without passing through the deferred block, which is contradictory.
// In particular, when such a range contributes a result back on the hot
// path, it will be as one of the inputs of a phi. In that case, the value
// will be transferred via a move in the Gap::END's of the last instruction
// of a deferred block.
bool RegisterAllocationData::RangesDefinedInDeferredStayInDeferred() {
  const size_t live_ranges_size = live_ranges().size();
  for (const TopLevelLiveRange* range : live_ranges()) {
    CHECK_EQ(live_ranges_size,
             live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(range);
    if (range->IsEmpty() ||
        !code()
             ->GetInstructionBlock(range->Start().ToInstructionIndex())
             ->IsDeferred()) {
      continue;
    }
    for (const UseInterval& interval : range->intervals()) {
      int first = interval.FirstGapIndex();
      int last = interval.LastGapIndex();
      for (int instr = first; instr <= last;) {
        const InstructionBlock* block = code()->GetInstructionBlock(instr);
        if (!block->IsDeferred()) return false;
        instr = block->last_instruction_index() + 1;
      }
    }
  }
  return true;
}

SpillRange* RegisterAllocationData::AssignSpillRangeToLiveRange(
    TopLevelLiveRange* range, SpillMode spill_mode) {
  using SpillType = TopLevelLiveRange::SpillType;
  DCHECK(!range->HasSpillOperand());

  SpillRange* spill_range = range->GetAllocatedSpillRange();
  if (spill_range == nullptr) {
    spill_range = allocation_zone()->New<SpillRange>(range, allocation_zone());
  }
  if (spill_mode == SpillMode::kSpillDeferred &&
      (range->spill_type() != SpillType::kSpillRange)) {
    range->set_spill_type(SpillType::kDeferredSpillRange);
  } else {
    range->set_spill_type(SpillType::kSpillRange);
  }

  return spill_range;
}

void RegisterAllocationData::MarkFixedUse(MachineRepresentation rep,
                                          int index) {
  switch (rep) {
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kSimd128:
    case MachineRepresentation::kSimd256:
      if (kFPAliasing == AliasingKind::kOverlap) {
        fixed_fp_register_use_->Add(index);
      } else if (kFPAliasing == AliasingKind::kIndependent) {
        if (rep == MachineRepresentation::kFloat32) {
          fixed_fp_register_use_->Add(index);
        } else {
          fixed_simd128_register_use_->Add(index);
        }
      } else {
        int alias_base_index = -1;
        int aliases = config()->GetAliases(
            rep, index, MachineRepresentation::kFloat64, &alias_base_index);
        DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
        while (aliases--) {
          int aliased_reg = alias_base_index + aliases;
          fixed_fp_register_use_->Add(aliased_reg);
        }
      }
      break;
    case MachineRepresentation::kFloat64:
      fixed_fp_register_use_->Add(index);
      break;
    default:
      DCHECK(!IsFloatingPoint(rep));
      fixed_register_use_->Add(index);
      break;
  }
}

bool RegisterAllocationData::HasFixedUse(MachineRepresentation rep, int index) {
  switch (rep) {
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kSimd128:
    case MachineRepresentation::kSimd256: {
      if (kFPAliasing == AliasingKind::kOverlap) {
        return fixed_fp_register_use_->Contains(index);
      } else if (kFPAliasing == AliasingKind::kIndependent) {
        if (rep == MachineRepresentation::kFloat32) {
          return fixed_fp_register_use_->Contains(index);
        } else {
          return fixed_simd128_register_use_->Contains(index);
        }
      } else {
        int alias_base_index = -1;
        int aliases = config()->GetAliases(
            rep, index, MachineRepresentation::kFloat64, &alias_base_index);
        DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
        bool result = false;
        while (aliases-- && !result) {
          int aliased_reg = alias_base_index + aliases;
          result |= fixed_fp_register_use_->Contains(aliased_reg);
        }
        return result;
      }
    }
    case MachineRepresentation::kFloat64:
      return fixed_fp_register_use_->Contains(index);
    default:
      DCHECK(!IsFloatingPoint(rep));
      return fixed_register_use_->Contains(index);
  }
}

void RegisterAllocationData::MarkAllocated(MachineRepresentation rep,
                                           int index) {
  switch (rep) {
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kSimd128:
    case MachineRepresentation::kSimd256:
      if (kFPAliasing == AliasingKind::kOverlap) {
        assigned_double_registers_->Add(index);
      } else if (kFPAliasing == AliasingKind::kIndependent) {
        if (rep == MachineRepresentation::kFloat32) {
          assigned_double_registers_->Add(index);
        } else {
          assigned_simd128_registers_->Add(index);
        }
      } else {
        int alias_base_index = -1;
        int aliases = config()->GetAliases(
            rep, index, MachineRepresentation::kFloat64, &alias_base_index);
        DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
        while (aliases--) {
          int aliased_reg = alias_base_index + aliases;
          assigned_double_registers_->Add(aliased_reg);
        }
      }
      break;
    case MachineRepresentation::kFloat64:
      assigned_double_registers_->Add(index);
      break;
    default:
      DCHECK(!IsFloatingPoint(rep));
      assigned_registers_->Add(index);
      break;
  }
}

bool RegisterAllocationData::IsBlockBoundary(LifetimePosition pos) const {
  return pos.IsFullStart() &&
         (static_cast<size_t>(pos.ToInstructionIndex()) ==
              code()->instructions().size() ||
          code()->GetInstructionBlock(pos.ToInstructionIndex())->code_start() ==
              pos.ToInstructionIndex());
}

ConstraintBuilder::ConstraintBuilder(RegisterAllocationData* data)
    : data_(data) {}

InstructionOperand* ConstraintBuilder::AllocateFixed(
    UnallocatedOperand* operand, int pos, bool is_tagged, bool is_input) {
  TRACE("Allocating fixed reg for op %d\n", operand->virtual_register());
  DCHECK(operand->HasFixedPolicy());
  InstructionOperand allocated;
  MachineRepresentation rep = InstructionSequence::DefaultRepresentation();
  int virtual_register = operand->virtual_register();
  if (virtual_register != InstructionOperand::kInvalidVirtualRegister) {
    rep = data()->RepresentationFor(virtual_register);
  }
  if (operand->HasFixedSlotPolicy()) {
    allocated = AllocatedOperand(AllocatedOperand::STACK_SLOT, rep,
                                 operand->fixed_slot_index());
  } else if (operand->HasFixedRegisterPolicy()) {
    DCHECK(!IsFloatingPoint(rep));
    DCHECK(data()->config()->IsAllocatableGeneralCode(
        operand->fixed_register_index()));
    allocated = AllocatedOperand(AllocatedOperand::REGISTER, rep,
                                 operand->fixed_register_index());
  } else if (operand->HasFixedFPRegisterPolicy()) {
    DCHECK(IsFloatingPoint(rep));
    DCHECK_NE(InstructionOperand::kInvalidVirtualRegister, virtual_register);
    allocated = AllocatedOperand(AllocatedOperand::REGISTER, rep,
                                 operand->fixed_register_index());
  } else {
    UNREACHABLE();
  }
  if (is_input && allocated.IsAnyRegister()) {
    data()->MarkFixedUse(rep, operand->fixed_register_index());
  }
  InstructionOperand::ReplaceWith(operand, &allocated);
  if (is_tagged) {
    TRACE("Fixed reg is tagged at %d\n", pos);
    Instruction* instr = code()->InstructionAt(pos);
    if (instr->HasReferenceMap()) {
      instr->reference_map()->RecordReference(*AllocatedOperand::cast(operand));
    }
  }
  return operand;
}

void ConstraintBuilder::MeetRegisterConstraints() {
  for (InstructionBlock* block : code()->instruction_blocks()) {
    data_->tick_counter()->TickAndMaybeEnterSafepoint();
    MeetRegisterConstraints(block);
  }
}

void ConstraintBuilder::MeetRegisterConstraints(const InstructionBlock* block) {
  int start = block->first_instruction_index();
  int end = block->last_instruction_index();
  DCHECK_NE(-1, start);
  for (int i = start; i <= end; ++i) {
    MeetConstraintsBefore(i);
    if (i != end) MeetConstraintsAfter(i);
  }
  // Meet register constraints for the instruction in the end.
  MeetRegisterConstraintsForLastInstructionInBlock(block);
}

void ConstraintBuilder::MeetRegisterConstraintsForLastInstructionInBlock(
    const InstructionBlock* block) {
  int end = block->last_instruction_index();
  Instruction* last_instruction = code()->InstructionAt(end);
  for (size_t i = 0; i < last_instruction->OutputCount(); i++) {
    InstructionOperand* output_operand = last_instruction->OutputAt(i);
    DCHECK(!output_operand->IsConstant());
    UnallocatedOperand* output = UnallocatedOperand::cast(output_operand);
    int output_vreg = output->virtual_register();
    TopLevelLiveRange* range = data()->GetLiveRangeFor(output_vreg);
    bool assigned = false;
    if (output->HasFixedPolicy()) {
      AllocateFixed(output, -1, false, false);
      // This value is produced on the stack, we never need to spill it.
      if (output->IsStackSlot()) {
        DCHECK(LocationOperand::cast(output)->index() <
               data()->frame()->GetSpillSlotCount());
        range->SetSpillOperand(LocationOperand::cast(output));
        range->SetSpillStartIndex(end);
        assigned = true;
      }

      for (const RpoNumber& succ : block->successors()) {
        const InstructionBlock* successor = code()->InstructionBlockAt(succ);
        DCHECK_EQ(1, successor->PredecessorCount());
        int gap_index = successor->first_instruction_index();
        // Create an unconstrained operand for the same virtual register
        // and insert a gap move from the fixed output to the operand.
        UnallocatedOperand output_copy(UnallocatedOperand::REGISTER_OR_SLOT,
                                       output_vreg);
        data()->AddGapMove(gap_index, Instruction::START, *output, output_copy);
      }
    }

    if (!assigned) {
      for (const RpoNumber& succ : block->successors()) {
        const InstructionBlock* successor = code()->InstructionBlockAt(succ);
        DCHECK_EQ(1, successor->PredecessorCount());
        int gap_index = successor->first_instruction_index();
        range->RecordSpillLocation(allocation_zone(), gap_index, output);
        range->SetSpillStartIndex(gap_index);
      }
    }
  }
}

void ConstraintBuilder::MeetConstraintsAfter(int instr_index) {
  Instruction* first = code()->InstructionAt(instr_index);
  // Handle fixed temporaries.
  for (size_t i = 0; i < first->TempCount(); i++) {
    UnallocatedOperand* temp = UnallocatedOperand::cast(first->TempAt(i));
    if (temp->HasFixedPolicy()) AllocateFixed(temp, instr_index, false, false);
  }
  // Handle constant/fixed output operands.
  for (size_t i = 0; i < first->OutputCount(); i++) {
    InstructionOperand* output = first->OutputAt(i);
    if (output->IsConstant()) {
      int output_vreg = ConstantOperand::cast(output)->virtual_register();
      TopLevelLiveRange* range = data()->GetLiveRangeFor(output_vreg);
      range->SetSpillStartIndex(instr_index + 1);
      range->SetSpillOperand(output);
      continue;
    }
    UnallocatedOperand* first_output = UnallocatedOperand::cast(output);
    TopLevelLiveRange* range =
        data()->GetLiveRangeFor(first_output->virtual_register());
    bool assigned = false;
    if (first_output->HasFixedPolicy()) {
      int output_vreg = first_output->virtual_register();
      UnallocatedOperand output_copy(UnallocatedOperand::REGISTER_OR_SLOT,
                                     output_vreg);
      bool is_tagged = code()->IsReference(output_vreg);
      if (first_output->HasSecondaryStorage()) {
        range->MarkHasPreassignedSlot();
        data()->preassigned_slot_ranges().push_back(
            std::make_pair(range, first_output->GetSecondaryStorage()));
      }
      AllocateFixed(first_output, instr_index, is_tagged, false);

      // This value is produced on the stack, we never need to spill it.
      if (first_output->IsStackSlot()) {
        DCHECK(LocationOperand::cast(first_output)->index() <
               data()->frame()->GetTotalFrameSlotCount());
        range->SetSpillOperand(LocationOperand::cast(first_output));
        range->SetSpillStartIndex(instr_index + 1);
        assigned = true;
      }
      data()->AddGapMove(instr_index + 1, Instruction::START, *first_output,
                         output_copy);
    }
    // Make sure we add a gap move for spilling (if we have not done
    // so already).
    if (!assigned) {
      range->RecordSpillLocation(allocation_zone(), instr_index + 1,
                                 first_output);
      range->SetSpillStartIndex(instr_index + 1);
    }
  }
}

void ConstraintBuilder::MeetConstraintsBefore(int instr_index) {
  Instruction* second = code()->InstructionAt(instr_index);
  // Handle fixed input operands of second instruction.
  ZoneVector<TopLevelLiveRange*>* spilled_consts = nullptr;
  for (size_t i = 0; i < second->InputCount(); i++) {
    InstructionOperand* input = second->InputAt(i);
    if (input->IsImmediate()) {
      continue;  // Ignore immediates.
    }
    UnallocatedOperand* cur_input = UnallocatedOperand::cast(input);
    if (cur_input->HasSlotPolicy()) {
      TopLevelLiveRange* range =
          data()->GetLiveRangeFor(cur_input->virtual_register());
      if (range->HasSpillOperand() && range->GetSpillOperand()->IsConstant()) {
        bool already_spilled = false;
        if (spilled_consts == nullptr) {
          spilled_consts =
              allocation_zone()->New<ZoneVector<TopLevelLiveRange*>>(
                  allocation_zone());
        } else {
          auto it =
              std::find(spilled_consts->begin(), spilled_consts->end(), range);
          already_spilled = it != spilled_consts->end();
        }
        auto it = data()->slot_for_const_range().find(range);
        if (it == data()->slot_for_const_range().end()) {
          DCHECK(!already_spilled);
          int width = ByteWidthForStackSlot(range->representation());
          int index = data()->frame()->AllocateSpillSlot(width);
          auto* slot = AllocatedOperand::New(allocation_zone(),
                                             LocationOperand::STACK_SLOT,
                                             range->representation(), index);
          it = data()->slot_for_const_range().emplace(range, slot).first;
        }
        if (!already_spilled) {
          auto* slot = it->second;
          int input_vreg = cur_input->virtual_register();
          UnallocatedOperand input_copy(UnallocatedOperand::REGISTER_OR_SLOT,
                                        input_vreg);
          // Spill at every use position for simplicity, this case is very rare.
          data()->AddGapMove(instr_index, Instruction::END, input_copy, *slot);
          spilled_consts->push_back(range);
        }
      }
    }
    if (cur_input->HasFixedPolicy()) {
      int input_vreg = cur_input->virtual_register();
      UnallocatedOperand input_copy(UnallocatedOperand::REGISTER_OR_SLOT,
                                    input_vreg);
      bool is_tagged = code()->IsReference(input_vreg);
      AllocateFixed(cur_input, instr_index, is_tagged, true);
      data()->AddGapMove(instr_index, Instruction::END, input_copy, *cur_input);
    }
  }
  // Handle "output same as input" for second instruction.
  for (size_t i = 0; i < second->OutputCount(); i++) {
    InstructionOperand* output = second->OutputAt(i);
    if (!output->IsUnallocated()) continue;
    UnallocatedOperand* second_output = UnallocatedOperand::cast(output);
    if (!second_output->HasSameAsInputPolicy()) continue;
    DCHECK_EQ(0, i);  // Only valid for first output.
    UnallocatedOperand* cur_input =
        UnallocatedOperand::cast(second->InputAt(second_output->input_index()));
    int output_vreg = second_output->virtual_register();
    int input_vreg = cur_input->virtual_register();
    UnallocatedOperand input_copy(UnallocatedOperand::REGISTER_OR_SLOT,
                                  input_vreg);
    *cur_input =
        UnallocatedOperand(*cur_input, second_output->virtual_register());
    MoveOperands* gap_move = data()->AddGapMove(instr_index, Instruction::END,
                                                input_copy, *cur_input);
    DCHECK_NOT_NULL(gap_move);
    if (code()->IsReference(input_vreg) && !code()->IsReference(output_vreg)) {
      if (second->HasReferenceMap()) {
        RegisterAllocationData::DelayedReference delayed_reference = {
            second->reference_map(), &gap_move->source()};
        data()->delayed_references().push_back(delayed_reference);
      }
    }
  }
}

void ConstraintBuilder::ResolvePhis() {
  // Process the blocks in reverse order.
  for (InstructionBlock* block : base::Reversed(code()->instruction_blocks())) {
    data_->tick_counter()->TickAndMaybeEnterSafepoint();
    ResolvePhis(block);
  }
}

void ConstraintBuilder::ResolvePhis(const InstructionBlock* block) {
  for (PhiInstruction* phi : block->phis()) {
    int phi_vreg = phi->virtual_register();
    RegisterAllocationData::PhiMapValue* map_value =
        data()->InitializePhiMap(block, phi);
    InstructionOperand& output = phi->output();
    // Map the destination operands, so the commitment phase can find them.
    for (size_t i = 0; i < phi->operands().size(); ++i) {
      InstructionBlock* cur_block =
          code()->InstructionBlockAt(block->predecessors()[i]);
      UnallocatedOperand input(UnallocatedOperand::REGISTER_OR_SLOT,
                               phi->operands()[i]);
      MoveOperands* move = data()->AddGapMove(
          cur_block->last_instruction_index(), Instruction::END, input, output);
      map_value->AddOperand(&move->destination());
      DCHECK(!code()
                  ->InstructionAt(cur_block->last_instruction_index())
                  ->HasReferenceMap());
    }
    TopLevelLiveRange* live_range = data()->GetLiveRangeFor(phi_vreg);
    int gap_index = block->first_instruction_index();
    live_range->RecordSpillLocation(allocation_zone(), gap_index, &output);
    live_range->SetSpillStartIndex(gap_index);
    // We use the phi-ness of some nodes in some later heuristics.
    live_range->set_is_phi(true);
    live_range->set_is_non_loop_phi(!block->IsLoopHeader());
  }
}

LiveRangeBuilder::LiveRangeBuilder(RegisterAllocationData* data,
                                   Zone* local_zone)
    : data_(data), phi_hints_(local_zone) {}

SparseBitVector* LiveRangeBuilder::ComputeLiveOut(
    const InstructionBlock* block, RegisterAllocationData* data) {
  size_t block_index = block->rpo_number().ToSize();
  SparseBitVector* live_out = data->live_out_sets()[block_index];
  if (live_out == nullptr) {
    // Compute live out for the given block, except not including backward
    // successor edges.
    Zone* zone = data->allocation_zone();
    const InstructionSequence* code = data->code();

    live_out = zone->New<SparseBitVector>(zone);

    // Process all successor blocks.
    for (const RpoNumber& succ : block->successors()) {
      // Add values live on entry to the successor.
      if (succ <= block->rpo_number()) continue;
      SparseBitVector* live_in = data->live_in_sets()[succ.ToSize()];
      if (live_in != nullptr) live_out->Union(*live_in);

      // All phi input operands corresponding to this successor edge are live
      // out from this block.
      const InstructionBlock* successor = code->InstructionBlockAt(succ);
      size_t index = successor->PredecessorIndexOf(block->rpo_number());
      DCHECK(index < successor->PredecessorCount());
      for (PhiInstruction* phi : successor->phis()) {
        live_out->Add(phi->operands()[index]);
      }
    }
    data->live_out_sets()[block_index] = live_out;
  }
  return live_out;
}

void LiveRangeBuilder::AddInitialIntervals(const InstructionBlock* block,
                                           SparseBitVector* live_out) {
  // Add an interval that includes the entire block to the live range for
  // each live_out value.
  LifetimePosition start = LifetimePosition::GapFromInstructionIndex(
      block->first_instruction_index());
  LifetimePosition end = LifetimePosition::InstructionFromInstructionIndex(
                             block->last_instruction_index())
                             .NextStart();
  for (int operand_index : *live_out) {
    TopLevelLiveRange* range = data()->GetLiveRangeFor(operand_index);
    range->AddUseInterval(start, end, allocation_zone());
  }
}

int LiveRangeBuilder::FixedFPLiveRangeID(int index, MachineRepresentation rep) {
  int result = -index - 1;
  switch (rep) {
    case MachineRepresentation::kSimd256:
      result -=
          kNumberOfFixedRangesPerRegister * config()->num_simd128_registers();
      [[fallthrough]];
    case MachineRepresentation::kSimd128:
      result -=
          kNumberOfFixedRangesPerRegister * config()->num_float_registers();
      [[fallthrough]];
    case MachineRepresentation::kFloat32:
      result -=
          kNumberOfFixedRangesPerRegister * config()->num_double_registers();
      [[fallthrough]];
    case MachineRepresentation::kFloat64:
      result -=
          kNumberOfFixedRangesPerRegister * config()->num_general_registers();
      break;
    default:
      UNREACHABLE();
  }
  return result;
}

TopLevelLiveRange* LiveRangeBuilder::FixedLiveRangeFor(int index,
                                                       SpillMode spill_mode) {
  int offset = spill_mode == SpillMode::kSpillAtDefinition
                   ? 0
                   : config()->num_general_registers();
  DCHECK(index < config()->num_general_registers());
  TopLevelLiveRange* result = data()->fixed_live_ranges()[offset + index];
  if (result == nullptr) {
    MachineRepresentation rep = InstructionSequence::DefaultRepresentation();
    result = data()->NewLiveRange(FixedLiveRangeID(offset + index), rep);
    DCHECK(result->IsFixed());
    result->set_assigned_register(index);
    data()->MarkAllocated(rep, index);
    if (spill_mode == SpillMode::kSpillDeferred) {
      result->set_deferred_fixed();
    }
    data()->fixed_live_ranges()[offset + index] = result;
  }
  return result;
}

TopLevelLiveRange* LiveRangeBuilder::FixedFPLiveRangeFor(
    int index, MachineRepresentation rep, SpillMode spill_mode) {
  int num_regs = config()->num_double_registers();
  ZoneVector<TopLevelLiveRange*>* live_ranges =
      &data()->fixed_double_live_ranges();
  if (kFPAliasing == AliasingKind::kCombine) {
    switch (rep) {
      case MachineRepresentation::kFloat32:
        num_regs = config()->num_float_registers();
        live_ranges = &data()->fixed_float_live_ranges();
        break;
      case MachineRepresentation::kSimd128:
        num_regs = config()->num_simd128_registers();
        live_ranges = &data()->fixed_simd128_live_ranges();
        break;
      default:
        break;
    }
  }

  int offset = spill_mode == SpillMode::kSpillAtDefinition ? 0 : num_regs;

  DCHECK(index < num_regs);
  USE(num_regs);
  TopLevelLiveRange* result = (*live_ranges)[offset + index];
  if (result == nullptr) {
    result = data()->NewLiveRange(FixedFPLiveRangeID(offset + index, rep), rep);
    DCHECK(result->IsFixed());
    result->set_assigned_register(index);
    data()->MarkAllocated(rep, index);
    if (spill_mode == SpillMode::kSpillDeferred) {
      result->set_deferred_fixed();
    }
    (*live_ranges)[offset + index] = result;
  }
  return result;
}

TopLevelLiveRange* LiveRangeBuilder::FixedSIMD128LiveRangeFor(
    int index, SpillMode spill_mode) {
  DCHECK_EQ(kFPAliasing, AliasingKind::kIndependent);
  int num_regs = config()->num_simd128_registers();
  ZoneVector<TopLevelLiveRange*>* live_ranges =
      &data()->fixed_simd128_live_ranges();
  int offset = spill_mode == SpillMode::kSpillAtDefinition ? 0 : num_regs;

  DCHECK(index < num_regs);
  USE(num_regs);
  TopLevelLiveRange* result = (*live_ranges)[offset + index];
  if (result == nullptr) {
    result = data()->NewLiveRange(
        FixedFPLiveRangeID(offset + index, MachineRepresentation::kSimd128),
        MachineRepresentation::kSimd128);
    DCHECK(result->IsFixed());
    result->set_assigned_register(index);
    data()->MarkAllocated(MachineRepresentation::kSimd128, index);
    if (spill_mode == SpillMode::kSpillDeferred) {
      result->set_deferred_fixed();
    }
    (*live_ranges)[offset + index] = result;
  }
  return result;
}

TopLevelLiveRange* LiveRangeBuilder::LiveRangeFor(InstructionOperand* operand,
                                                  SpillMode spill_mode) {
  if (operand->IsUnallocated()) {
    return data()->GetLiveRangeFor(
        UnallocatedOperand::cast(operand)->virtual_register());
  } else if (operand->IsConstant()) {
    return data()->GetLiveRangeFor(
        ConstantOperand::cast(operand)->virtual_register());
  } else if (operand->IsRegister()) {
    return FixedLiveRangeFor(
        LocationOperand::cast(operand)->GetRegister().code(), spill_mode);
  } else if (operand->IsFPRegister()) {
    LocationOperand* op = LocationOperand::cast(operand);
    if (kFPAliasing == AliasingKind::kIndependent &&
        op->representation() == MachineRepresentation::kSimd128) {
      return FixedSIMD128LiveRangeFor(op->register_code(), spill_mode);
    }
    return FixedFPLiveRangeFor(op->register_code(), op->representation(),
                               spill_mode);
  } else {
    return nullptr;
  }
}

UsePosition* LiveRangeBuilder::NewUsePosition(LifetimePosition pos,
                                              InstructionOperand* operand,
                                              void* hint,
                                              UsePositionHintType hint_type) {
  return allocation_zone()->New<UsePosition>(pos, operand, hint, hint_type);
}

UsePosition* LiveRangeBuilder::Define(LifetimePosition position,
                                      InstructionOperand* operand, void* hint,
                                      UsePositionHintType hint_type,
                                      SpillMode spill_mode) {
  TopLevelLiveRange* range = LiveRangeFor(operand, spill_mode);
  if (range == nullptr) return nullptr;

  if (range->IsEmpty() || range->Start() > position) {
    // Can happen if there is a definition without use.
    range->AddUseInterval(position, position.NextStart(), allocation_zone());
    range->AddUsePosition(NewUsePosition(position.NextStart()),
                          allocation_zone());
  } else {
    range->ShortenTo(position);
  }
  if (!operand->IsUnallocated()) return nullptr;
  UnallocatedOperand* unalloc_operand = UnallocatedOperand::cast(operand);
  UsePosition* use_pos =
      NewUsePosition(position, unalloc_operand, hint, hint_type);
  range->AddUsePosition(use_pos, allocation_zone());
  return use_pos;
}

UsePosition* LiveRangeBuilder::Use(LifetimePosition block_start,
                                   LifetimePosition position,
                                   InstructionOperand* operand, void* hint,
                                   UsePositionHintType hint_type,
                                   SpillMode spill_mode) {
  TopLevelLiveRange* range = LiveRangeFor(operand, spill_mode);
  if (range == nullptr) return nullptr;
  UsePosition* use_pos = nullptr;
  if (operand->IsUnallocated()) {
    UnallocatedOperand* unalloc_operand = UnallocatedOperand::cast(operand);
    use_pos = NewUsePosition(position, unalloc_operand, hint, hint_type);
    range->AddUsePosition(use_pos, allocation_zone());
  }
  range->AddUseInterval(block_start, position, allocation_zone());
  return use_pos;
}

void LiveRangeBuilder::ProcessInstructions(const InstructionBlock* block,
                                           SparseBitVector* live) {
  int block_start = block->first_instruction_index();
  LifetimePosition block_start_position =
      LifetimePosition::GapFromInstructionIndex(block_start);
  bool fixed_float_live_ranges = false;
  bool fixed_simd128_live_ranges = false;
  if (kFPAliasing == AliasingKind::kCombine) {
    int mask = data()->code()->representation_mask();
    fixed_float_live_ranges = (mask & kFloat32Bit) != 0;
    fixed_simd128_live_ranges = (mask & kSimd128Bit) != 0;
  } else if (kFPAliasing == AliasingKind::kIndependent) {
    int mask = data()->code()->representation_mask();
    fixed_simd128_live_ranges = (mask & kSimd128Bit) != 0;
  }
  SpillMode spill_mode = SpillModeForBlock(block);

  for (int index = block->last_instruction_index(); index >= block_start;
       index--) {
    LifetimePosition curr_position =
        LifetimePosition::InstructionFromInstructionIndex(index);
    Instruction* instr = code()->InstructionAt(index);
    DCHECK_NOT_NULL(instr);
    DCHECK(curr_position.IsInstructionPosition());
    // Process output, inputs, and temps of this instruction.
    for (size_t i = 0; i < instr->OutputCount(); i++) {
      InstructionOperand* output = instr->OutputAt(i);
      if (output->IsUnallocated()) {
        // Unsupported.
        DCHECK(!UnallocatedOperand::cast(output)->HasSlotPolicy());
        int out_vreg = UnallocatedOperand::cast(output)->virtual_register();
        live->Remove(out_vreg);
      } else if (output->IsConstant()) {
        int out_vreg = ConstantOperand::cast(output)->virtual_register();
        live->Remove(out_vreg);
      }
      if (block->IsHandler() && index == block_start && output->IsAllocated() &&
          output->IsRegister() &&
          AllocatedOperand::cast(output)->GetRegister() ==
              v8::internal::kReturnRegister0) {
        // The register defined here is blocked from gap start - it is the
        // exception value.
        // TODO(mtrofin): should we explore an explicit opcode for
        // the first instruction in the handler?
        Define(LifetimePosition::GapFromInstructionIndex(index), output,
               spill_mode);
      } else {
        Define(curr_position, output, spill_mode);
      }
    }

    if (instr->ClobbersRegisters()) {
      for (int i = 0; i < config()->num_allocatable_general_registers(); ++i) {
        // Create a UseInterval at this instruction for all fixed registers,
        // (including the instruction outputs). Adding another UseInterval here
        // is OK because AddUseInterval will just merge it with the existing
        // one at the end of the range.
        int code = config()->GetAllocatableGeneralCode(i);
        TopLevelLiveRange* range = FixedLiveRangeFor(code, spill_mode);
        range->AddUseInterval(curr_position, curr_position.End(),
                              allocation_zone());
      }
    }

    if (instr->ClobbersDoubleRegisters()) {
      for (int i = 0; i < config()->num_allocatable_double_registers(); ++i) {
        // Add a UseInterval for all DoubleRegisters. See comment above for
        // general registers.
        int code = config()->GetAllocatableDoubleCode(i);
        TopLevelLiveRange* range = FixedFPLiveRangeFor(
            code, MachineRepresentation::kFloat64, spill_mode);
        range->AddUseInterval(curr_position, curr_position.End(),
                              allocation_zone());
      }
      // Clobber fixed float registers on archs with non-simple aliasing.
      if (kFPAliasing == AliasingKind::kCombine) {
        if (fixed_float_live_ranges) {
          for (int i = 0; i < config()->num_allocatable_float_registers();
               ++i) {
            // Add a UseInterval for all FloatRegisters. See comment above for
            // general registers.
            int code = config()->GetAllocatableFloatCode(i);
            TopLevelLiveRange* range = FixedFPLiveRangeFor(
                code, MachineRepresentation::kFloat32, spill_mode);
            range->AddUseInterval(curr_position, curr_position.End(),
                                  allocation_zone());
          }
        }
        if (fixed_simd128_live_ranges) {
          for (int i = 0; i < config()->num_allocatable_simd128_registers();
               ++i) {
            int code = config()->GetAllocatableSimd128Code(i);
            TopLevelLiveRange* range = FixedFPLiveRangeFor(
                code, MachineRepresentation::kSimd128, spill_mode);
            range->AddUseInterval(curr_position, curr_position.End(),
                                  allocation_zone());
          }
        }
      } else if (kFPAliasing == AliasingKind::kIndependent) {
        if (fixed_simd128_live_ranges) {
          for (int i = 0; i < config()->num_allocatable_simd128_registers();
               ++i) {
            int code = config()->GetAllocatableSimd128Code(i);
            TopLevelLiveRange* range =
                FixedSIMD128LiveRangeFor(code, spill_mode);
            range->AddUseInterval(curr_position, curr_position.End(),
                                  allocation_zone());
          }
        }
      }
    }

    for (size_t i = 0; i < instr->InputCount(); i++) {
      InstructionOperand* input = instr->InputAt(i);
      if (input->IsImmediate()) {
        continue;  // Ignore immediates.
      }
      LifetimePosition use_pos;
      if (input->IsUnallocated() &&
          UnallocatedOperand::cast(input)->IsUsedAtStart()) {
        use_pos = curr_position;
      } else {
        use_pos = curr_position.End();
      }

      if (input->IsUnallocated()) {
        UnallocatedOperand* unalloc = UnallocatedOperand::cast(input);
        int vreg = unalloc->virtual_register();
        live->Add(vreg);
        if (unalloc->HasSlotPolicy()) {
          data()->GetLiveRangeFor(vreg)->register_slot_use(
              block->IsDeferred()
                  ? TopLevelLiveRange::SlotUseKind::kDeferredSlotUse
                  : TopLevelLiveRange::SlotUseKind::kGeneralSlotUse);
        }
      }
      Use(block_start_position, use_pos, input, spill_mode);
    }

    for (size_t i = 0; i < instr->TempCount(); i++) {
      InstructionOperand* temp = instr->TempAt(i);
      // Unsupported.
      DCHECK_IMPLIES(temp->IsUnallocated(),
                     !UnallocatedOperand::cast(temp)->HasSlotPolicy());
      if (instr->ClobbersTemps()) {
        if (temp->IsRegister()) continue;
        if (temp->IsUnallocated()) {
          UnallocatedOperand* temp_unalloc = UnallocatedOperand::cast(temp);
          if (temp_unalloc->HasFixedPolicy()) {
            continue;
          }
        }
      }
      Use(block_start_position, curr_position.End(), temp, spill_mode);
      Define(curr_position, temp, spill_mode);
    }

    // Process the moves of the instruction's gaps, making their sources live.
    const Instruction::GapPosition kPositions[] = {Instruction::END,
                                                   Instruction::START};
    curr_position = curr_position.PrevStart();
    DCHECK(curr_position.IsGapPosition());
    for (const Instruction::GapPosition& position : kPositions) {
      ParallelMove* move = instr->GetParallelMove(position);
      if (move == nullptr) continue;
      if (position == Instruction::END) {
        curr_position = curr_position.End();
      } else {
        curr_position = curr_position.Start();
      }
      for (MoveOperands* cur : *move) {
        InstructionOperand& from = cur->source();
        InstructionOperand& to = cur->destination();
        void* hint = &to;
        UsePositionHintType hint_type = UsePosition::HintTypeForOperand(to);
        UsePosition* to_use = nullptr;
        int phi_vreg = -1;
        if (to.IsUnallocated()) {
          int to_vreg = UnallocatedOperand::cast(to).virtual_register();
          TopLevelLiveRange* to_range = data()->GetLiveRangeFor(to_vreg);
          if (to_range->is_phi()) {
            phi_vreg = to_vreg;
            if (to_range->is_non_loop_phi()) {
              hint = to_range->current_hint_position();
              hint_type = hint == nullptr ? UsePositionHintType::kNone
                                          : UsePositionHintType::kUsePos;
            } else {
              hint_type = UsePositionHintType::kPhi;
              hint = data()->GetPhiMapValueFor(to_vreg);
            }
          } else {
            if (live->Contains(to_vreg)) {
              to_use =
                  Define(curr_position, &to, &from,
                         UsePosition::HintTypeForOperand(from), spill_mode);
              live->Remove(to_vreg);
            } else {
              cur->Eliminate();
              continue;
            }
          }
        } else {
          Define(curr_position, &to, spill_mode);
        }
        UsePosition* from_use = Use(block_start_position, curr_position, &from,
                                    hint, hint_type, spill_mode);
        // Mark range live.
        if (from.IsUnallocated()) {
          live->Add(UnallocatedOperand::cast(from).virtual_register());
        }
        // When the value is moved to a register to meet input constraints,
        // we should consider this value use similar as a register use in the
        // backward spilling heuristics, even though this value use is not
        // register benefical at the AllocateBlockedReg stage.
        if (to.IsAnyRegister() ||
            (to.IsUnallocated() &&
             UnallocatedOperand::cast(&to)->HasRegisterPolicy())) {
          from_use->set_spill_detrimental();
        }
        // Resolve use position hints just created.
        if (to_use != nullptr && from_use != nullptr) {
          to_use->ResolveHint(from_use);
          from_use->ResolveHint(to_use);
        }
        DCHECK_IMPLIES(to_use != nullptr, to_use->IsResolved());
        DCHECK_IMPLIES(from_use != nullptr, from_use->IsResolved());
        // Potentially resolve phi hint.
        if (phi_vreg != -1) ResolvePhiHint(&from, from_use);
      }
    }
  }
}

void LiveRangeBuilder::ProcessPhis(const InstructionBlock* block,
                                   SparseBitVector* live) {
  for (PhiInstruction* phi : block->phis()) {
    // The live range interval already ends at the first instruction of the
    // block.
    int phi_vreg = phi->virtual_register();
    live->Remove(phi_vreg);
    // Select a hint from a predecessor block that precedes this block in the
    // rpo order. In order of priority:
    // - Avoid hints from deferred blocks.
    // - Prefer hints from allocated (or explicit) operands.
    // - Prefer hints from empty blocks (containing just parallel moves and a
    //   jump). In these cases, if we can elide the moves, the jump threader
    //   is likely to be able to elide the jump.
    // The enforcement of hinting in rpo order is required because hint
    // resolution that happens later in the compiler pipeline visits
    // instructions in reverse rpo order, relying on the fact that phis are
    // encountered before their hints.
    InstructionOperand* hint = nullptr;
    int hint_preference = 0;

    // The cost of hinting increases with the number of predecessors. At the
    // same time, the typical benefit decreases, since this hinting only
    // optimises the execution path through one predecessor. A limit of 2 is
    // sufficient to hit the common if/else pattern.
    int predecessor_limit = 2;

    for (RpoNumber predecessor : block->predecessors()) {
      const InstructionBlock* predecessor_block =
          code()->InstructionBlockAt(predecessor);
      DCHECK_EQ(predecessor_block->rpo_number(), predecessor);

      // Only take hints from earlier rpo numbers.
      if (predecessor >= block->rpo_number()) continue;

      // Look up the predecessor instruction.
      const Instruction* predecessor_instr =
          GetLastInstruction(code(), predecessor_block);
      InstructionOperand* predecessor_hint = nullptr;
      // Phis are assigned in the END position of the last instruction in each
      // predecessor block.
      for (MoveOperands* move :
           *predecessor_instr->GetParallelMove(Instruction::END)) {
        InstructionOperand& to = move->destination();
        if (to.IsUnallocated() &&
            UnallocatedOperand::cast(to).virtual_register() == phi_vreg) {
          predecessor_hint = &move->source();
          break;
        }
      }
      DCHECK_NOT_NULL(predecessor_hint);

      // For each predecessor, generate a score according to the priorities
      // described above, and pick the best one. Flags in higher-order bits have
      // a higher priority than those in lower-order bits.
      int predecessor_hint_preference = 0;
      const int kNotDeferredBlockPreference = (1 << 2);
      const int kMoveIsAllocatedPreference = (1 << 1);
      const int kBlockIsEmptyPreference = (1 << 0);

      // - Avoid hints from deferred blocks.
      if (!predecessor_block->IsDeferred()) {
        predecessor_hint_preference |= kNotDeferredBlockPreference;
      }

      // - Prefer hints from allocated operands.
      //
      // Already-allocated operands are typically assigned using the parallel
      // moves on the last instruction. For example:
      //
      //      gap (v101 = [x0|R|w32]) (v100 = v101)
      //      ArchJmp
      //    ...
      //    phi: v100 = v101 v102
      //
      // We have already found the END move, so look for a matching START move
      // from an allocated operand.
      //
      // Note that we cannot simply look up data()->live_ranges()[vreg] here
      // because the live ranges are still being built when this function is
      // called.
      // TODO(v8): Find a way to separate hinting from live range analysis in
      // BuildLiveRanges so that we can use the O(1) live-range look-up.
      auto moves = predecessor_instr->GetParallelMove(Instruction::START);
      if (moves != nullptr) {
        for (MoveOperands* move : *moves) {
          InstructionOperand& to = move->destination();
          if (predecessor_hint->Equals(to)) {
            if (move->source().IsAllocated()) {
              predecessor_hint_preference |= kMoveIsAllocatedPreference;
            }
            break;
          }
        }
      }

      // - Prefer hints from empty blocks.
      if (predecessor_block->last_instruction_index() ==
          predecessor_block->first_instruction_index()) {
        predecessor_hint_preference |= kBlockIsEmptyPreference;
      }

      if ((hint == nullptr) ||
          (predecessor_hint_preference > hint_preference)) {
        // Take the hint from this predecessor.
        hint = predecessor_hint;
        hint_preference = predecessor_hint_preference;
      }

      if (--predecessor_limit <= 0) break;
    }
    DCHECK_NOT_NULL(hint);

    LifetimePosition block_start = LifetimePosition::GapFromInstructionIndex(
        block->first_instruction_index());
    UsePosition* use_pos = Define(block_start, &phi->output(), hint,
                                  UsePosition::HintTypeForOperand(*hint),
                                  SpillModeForBlock(block));
    MapPhiHint(hint, use_pos);
  }
}

void LiveRangeBuilder::ProcessLoopHeader(const InstructionBlock* block,
                                         SparseBitVector* live) {
  DCHECK(block->IsLoopHeader());
  // Add a live range stretching from the first loop instruction to the last
  // for each value live on entry to the header.
  LifetimePosition start = LifetimePosition::GapFromInstructionIndex(
      block->first_instruction_index());
  LifetimePosition end = LifetimePosition::GapFromInstructionIndex(
                             code()->LastLoopInstructionIndex(block))
                             .NextFullStart();
  for (int operand_index : *live) {
    TopLevelLiveRange* range = data()->GetLiveRangeFor(operand_index);
    range->EnsureInterval(start, end, allocation_zone());
  }
  // Insert all values into the live in sets of all blocks in the loop.
  for (int i = block->rpo_number().ToInt() + 1; i < block->loop_end().ToInt();
       ++i) {
    live_in_sets()[i]->Union(*live);
  }
}

void LiveRangeBuilder::BuildLiveRanges() {
  // Process the blocks in reverse order.
  for (int block_id = code()->InstructionBlockCount() - 1; block_id >= 0;
       --block_id) {
    data_->tick_counter()->TickAndMaybeEnterSafepoint();
    InstructionBlock* block =
        code()->InstructionBlockAt(RpoNumber::FromInt(block_id));
    SparseBitVector* live = ComputeLiveOut(block, data());
    // Initially consider all live_out values live for the entire block. We
    // will shorten these intervals if necessary.
    AddInitialIntervals(block, live);
    // Process the instructions in reverse order, generating and killing
    // live values.
    ProcessInstructions(block, live);
    // All phi output operands are killed by this block.
    ProcessPhis(block, live);
    // Now live is live_in for this block except not including values live
    // out on backward successor edges.
    if (block->IsLoopHeader()) ProcessLoopHeader(block, live);
    live_in_sets()[block_id] = live;
  }
  // Postprocess the ranges.
  const size_t live_ranges_size = data()->live_ranges().size();
  for (TopLevelLiveRange* range : data()->live_ranges()) {
    data_->tick_counter()->TickAndMaybeEnterSafepoint();
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(range);
    // Give slots to all ranges with a non fixed slot use.
    if (range->has_slot_use() && range->HasNoSpillType()) {
      SpillMode spill_mode =
          range->slot_use_kind() ==
                  TopLevelLiveRange::SlotUseKind::kDeferredSlotUse
              ? SpillMode::kSpillDeferred
              : SpillMode::kSpillAtDefinition;
      data()->AssignSpillRangeToLiveRange(range, spill_mode);
    }
    // TODO(bmeurer): This is a horrible hack to make sure that for constant
    // live ranges, every use requires the constant to be in a register.
    // Without this hack, all uses with "any" policy would get the constant
    // operand assigned.
    if (range->HasSpillOperand() && range->GetSpillOperand()->IsConstant()) {
      for (UsePosition* pos : range->positions()) {
        if (pos->type() == UsePositionType::kRequiresSlot ||
            pos->type() == UsePositionType::kRegisterOrSlotOrConstant) {
          continue;
        }
        UsePositionType new_type = UsePositionType::kRegisterOrSlot;
        // Can't mark phis as needing a register.
        if (!pos->pos().IsGapPosition()) {
          new_type = UsePositionType::kRequiresRegister;
        }
        pos->set_type(new_type, true);
      }
    }
    range->ResetCurrentHintPosition();
  }
  for (auto preassigned : data()->preassigned_slot_ranges()) {
    TopLevelLiveRange* range = preassigned.first;
    int slot_id = preassigned.second;
    SpillRange* spill = range->HasSpillRange()
                            ? range->GetSpillRange()
                            : data()->AssignSpillRangeToLiveRange(
                                  range, SpillMode::kSpillAtDefinition);
    spill->set_assigned_slot(slot_id);
  }
#ifdef DEBUG
  Verify();
#endif
}

void LiveRangeBuilder::MapPhiHint(InstructionOperand* operand,
                                  UsePosition* use_pos) {
  DCHECK(!use_pos->IsResolved());
  auto res = phi_hints_.insert(std::make_pair(operand, use_pos));
  DCHECK(res.second);
  USE(res);
}

void LiveRangeBuilder::ResolvePhiHint(InstructionOperand* operand,
                                      UsePosition* use_pos) {
  auto it = phi_hints_.find(operand);
  if (it == phi_hints_.end()) return;
  DCHECK(!it->second->IsResolved());
  it->second->ResolveHint(use_pos);
}

#ifdef DEBUG
void LiveRangeBuilder::Verify() const {
  for (auto& hint : phi_hints_) {
    DCHECK(hint.second->IsResolved());
  }
  for (TopLevelLiveRange* current : data()->live_ranges()) {
    DCHECK_NOT_NULL(current);
    if (!current->IsEmpty()) {
      // New LiveRanges should not be split.
      DCHECK_NULL(current->next());
      // General integrity check.
      current->Verify();
      if (current->intervals().size() < 2) continue;

      // Consecutive intervals should not end and start in the same block,
      // otherwise the intervals should have been joined, because the
      // variable is live throughout that block.
      UseIntervalVector::const_iterator interval = current->intervals().begin();
      UseIntervalVector::const_iterator next_interval = interval + 1;
      DCHECK(NextIntervalStartsInDifferentBlocks(*interval, *next_interval));

      for (++interval; interval != current->intervals().end(); ++interval) {
        // Except for the first interval, the other intevals must start at
        // a block boundary, otherwise data wouldn't flow to them.
        // You might trigger this CHECK if your SSA is not valid. For instance,
        // if the inputs of a Phi node are in the wrong order.
        DCHECK(IntervalStartsAtBlockBoundary(*interval));
        // The last instruction of the predecessors of the block the interval
        // starts must be covered by the range.
        DCHECK(IntervalPredecessorsCoveredByRange(*interval, current));
        next_interval = interval + 1;
        if (next_interval != current->intervals().end()) {
          // Check the consecutive intervals property, except for the last
          // interval, where it doesn't apply.
          DCHECK(
              NextIntervalStartsInDifferentBlocks(*interval, *next_interval));
        }
      }
    }
  }
}

bool LiveRangeBuilder::IntervalStartsAtBlockBoundary(
    UseInterval interval) const {
  LifetimePosition start = interval.start();
  if (!start.IsFullStart()) return false;
  int instruction_index = start.ToInstructionIndex();
  const InstructionBlock* block =
      data()->code()->GetInstructionBlock(instruction_index);
  return block->first_instruction_index() == instruction_index;
}

bool LiveRangeBuilder::IntervalPredecessorsCoveredByRange(
    UseInterval interval, TopLevelLiveRange* range) const {
  LifetimePosition start = interval.start();
  int instruction_index = start.ToInstructionIndex();
  const InstructionBlock* block =
      data()->code()->GetInstructionBlock(instruction_index);
  for (RpoNumber pred_index : block->predecessors()) {
    const InstructionBlock* predecessor =
        data()->code()->InstructionBlockAt(pred_index);
    LifetimePosition last_pos = LifetimePosition::GapFromInstructionIndex(
        predecessor->last_instruction_index());
    last_pos = last_pos.NextStart().End();
    if (!range->Covers(last_pos)) return false;
  }
  return true;
}

bool LiveRangeBuilder::NextIntervalStartsInDifferentBlocks(
    UseInterval interval, UseInterval next) const {
  LifetimePosition end = interval.end();
  LifetimePosition next_start = next.start();
  // Since end is not covered, but the previous position is, move back a
  // position
  end = end.IsStart() ? end.PrevStart().End() : end.Start();
  int last_covered_index = end.ToInstructionIndex();
  const InstructionBlock* block =
      data()->code()->GetInstructionBlock(last_covered_index);
  const InstructionBlock* next_block =
      data()->code()->GetInstructionBlock(next_start.ToInstructionIndex());
  return block->rpo_number() < next_block->rpo_number();
}
#endif

void BundleBuilder::BuildBundles() {
  TRACE("Build bundles\n");
  // Process the blocks in reverse order.
  for (int block_id = code()->InstructionBlockCount() - 1; block_id >= 0;
       --block_id) {
    InstructionBlock* block =
        code()->InstructionBlockAt(RpoNumber::FromInt(block_id));
    TRACE("Block B%d\n", block_id);
    for (auto phi : block->phis()) {
      TopLevelLiveRange* out_range =
          data()->GetLiveRangeFor(phi->virtual_register());
      LiveRangeBundle* out = out_range->get_bundle();
      if (out == nullptr) {
        out = data()->allocation_zone()->New<LiveRangeBundle>(
            data()->allocation_zone(), next_bundle_id_++);
        out->TryAddRange(out_range);
      }
      TRACE("Processing phi for v%d with %d:%d\n", phi->virtual_register(),
            out_range->TopLevel()->vreg(), out_range->relative_id());
      bool phi_interferes_with_backedge_input = false;
      for (auto input : phi->operands()) {
        TopLevelLiveRange* input_range = data()->GetLiveRangeFor(input);
        TRACE("Input value v%d with range %d:%d\n", input,
              input_range->TopLevel()->vreg(), input_range->relative_id());
        LiveRangeBundle* input_bundle = input_range->get_bundle();
        if (input_bundle != nullptr) {
          TRACE("Merge\n");
          LiveRangeBundle* merged =
              LiveRangeBundle::TryMerge(out, input_bundle);
          if (merged != nullptr) {
            DCHECK_EQ(out_range->get_bundle(), merged);
            DCHECK_EQ(input_range->get_bundle(), merged);
            out = merged;
            TRACE("Merged %d and %d to %d\n", phi->virtual_register(), input,
                  out->id());
          } else if (input_range->Start() > out_range->Start()) {
            // We are only interested in values defined after the phi, because
            // those are values that will go over a back-edge.
            phi_interferes_with_backedge_input = true;
          }
        } else {
          TRACE("Add\n");
          if (out->TryAddRange(input_range)) {
            TRACE("Added %d and %d to %d\n", phi->virtual_register(), input,
                  out->id());
          } else if (input_range->Start() > out_range->Start()) {
            // We are only interested in values defined after the phi, because
            // those are values that will go over a back-edge.
            phi_interferes_with_backedge_input = true;
          }
        }
      }
      // Spilling the phi at the loop header is not beneficial if there is
      // a back-edge with an input for the phi that interferes with the phi's
      // value, because in case that input gets spilled it might introduce
      // a stack-to-stack move at the back-edge.
      if (phi_interferes_with_backedge_input)
        out_range->TopLevel()->set_spilling_at_loop_header_not_beneficial();
    }
    TRACE("Done block B%d\n", block_id);
  }
}

bool LiveRangeBundle::TryAddRange(TopLevelLiveRange* range) {
  DCHECK_NULL(range->get_bundle());
  // We may only add a new live range if its use intervals do not
  // overlap with existing intervals in the bundle.
  if (AreUseIntervalsIntersecting(this->intervals_, range->intervals()))
    return false;
  AddRange(range);
  return true;
}

void LiveRangeBundle::AddRange(TopLevelLiveRange* range) {
  TopLevelLiveRange** insert_it = std::lower_bound(
      ranges_.begin(), ranges_.end(), range, LiveRangeOrdering());
  DCHECK_IMPLIES(insert_it != ranges_.end(), *insert_it != range);
  // TODO(dlehmann): We might save some memory by using
  // `DoubleEndedSplitVector::insert<kFront>()` here: Since we add ranges
  // mostly backwards, ranges with an earlier `Start()` are inserted mostly
  // at the front.
  ranges_.insert(insert_it, 1, range);
  range->set_bundle(this);

  // We also tried `std::merge`ing the sorted vectors of `intervals_` directly,
  // but it turns out the (always happening) copies are more expensive
  // than the (apparently seldom) copies due to insertion in the middle.
  for (UseInterval interval : range->intervals()) {
    UseInterval* insert_it =
        std::lower_bound(intervals_.begin(), intervals_.end(), interval);
    DCHECK_IMPLIES(insert_it != intervals_.end(), *insert_it != interval);
    intervals_.insert(insert_it, 1, interval);
  }
}

LiveRangeBundle* LiveRangeBundle::TryMerge(LiveRangeBundle* lhs,
                                           LiveRangeBundle* rhs) {
  if (rhs == lhs) return lhs;

  if (auto found =
          AreUseIntervalsIntersecting(lhs->intervals_, rhs->intervals_)) {
    auto [interval1, interval2] = *found;
    TRACE("No merge %d:%d %d:%d\n", interval1.start().value(),
          interval1.end().value(), interval2.start().value(),
          interval2.end().value());
    return nullptr;
  }
  // Uses are disjoint, merging is possible.

  // Merge the smaller bundle into the bigger.
  if (lhs->intervals_.size() < rhs->intervals_.size()) {
    std::swap(lhs, rhs);
  }
  for (TopLevelLiveRange* range : rhs->ranges_) {
    lhs->AddRange(range);
  }

  rhs->ranges_.clear();
  rhs->intervals_.clear();

  return lhs;
}

void LiveRangeBundle::MergeSpillRangesAndClear() {
  DCHECK_IMPLIES(ranges_.empty(), intervals_.empty());
  SpillRange* target = nullptr;
  for (auto range : ranges_) {
    if (range->TopLevel()->HasSpillRange()) {
      SpillRange* current = range->TopLevel()->GetSpillRange();
      if (target == nullptr) {
        target = current;
      } else if (target != current) {
        target->TryMerge(current);
      }
    }
  }
  // Clear the fields so that we don't try to merge the spill ranges again when
  // we hit the same bundle from a different LiveRange in AssignSpillSlots.
  // LiveRangeBundles are not used after this.
  ranges_.clear();
  intervals_.clear();
}

RegisterAllocator::RegisterAllocator(RegisterAllocationData* data,
                                     RegisterKind kind)
    : data_(data),
      mode_(kind),
      num_registers_(GetRegisterCount(data->config(), kind)),
      num_allocatable_registers_(
          GetAllocatableRegisterCount(data->config(), kind)),
      allocatable_register_codes_(
          GetAllocatableRegisterCodes(data->config(), kind)),
      check_fp_aliasing_(false) {
  if (kFPAliasing == AliasingKind::kCombine && kind == RegisterKind::kDouble) {
    check_fp_aliasing_ = (data->code()->representation_mask() &
                          (kFloat32Bit | kSimd128Bit)) != 0;
  }
}

LifetimePosition RegisterAllocator::GetSplitPositionForInstruction(
    const LiveRange* range, int instruction_index) {
  LifetimePosition ret = LifetimePosition::Invalid();

  ret = LifetimePosition::GapFromInstructionIndex(instruction_index);
  if (range->Start() >= ret || ret >= range->End()) {
    return LifetimePosition::Invalid();
  }
  return ret;
}

void RegisterAllocator::SplitAndSpillRangesDefinedByMemoryOperand() {
  size_t initial_range_count = data()->live_ranges().size();
  for (size_t i = 0; i < initial_range_count; ++i) {
    CHECK_EQ(initial_range_count,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    TopLevelLiveRange* range = data()->live_ranges()[i];
    if (!CanProcessRange(range)) continue;
    // Only assume defined by memory operand if we are guaranteed to spill it or
    // it has a spill operand.
    if (range->HasNoSpillType() ||
        (range->HasSpillRange() && !range->has_non_deferred_slot_use())) {
      continue;
    }
    LifetimePosition start = range->Start();
    TRACE("Live range %d:%d is defined by a spill operand.\n",
          range->TopLevel()->vreg(), range->relative_id());
    LifetimePosition next_pos = start;
    if (next_pos.IsGapPosition()) {
      next_pos = next_pos.NextStart();
    }

    UsePosition* pos = range->NextUsePositionRegisterIsBeneficial(next_pos);
    // If the range already has a spill operand and it doesn't need a
    // register immediately, split it and spill the first part of the range.
    if (pos == nullptr) {
      Spill(range, SpillMode::kSpillAtDefinition);
    } else if (pos->pos() > range->Start().NextStart()) {
      // Do not spill live range eagerly if use position that can benefit from
      // the register is too close to the start of live range.
      LifetimePosition split_pos = GetSplitPositionForInstruction(
          range, pos->pos().ToInstructionIndex());
      // There is no place to split, so we can't split and spill.
      if (!split_pos.IsValid()) continue;

      split_pos =
          FindOptimalSplitPos(range->Start().NextFullStart(), split_pos);

      SplitRangeAt(range, split_pos);
      Spill(range, SpillMode::kSpillAtDefinition);
    }
  }
}

LiveRange* RegisterAllocator::SplitRangeAt(LiveRange* range,
                                           LifetimePosition pos) {
  DCHECK(!range->TopLevel()->IsFixed());
  TRACE("Splitting live range %d:%d at %d\n", range->TopLevel()->vreg(),
        range->relative_id(), pos.value());

  if (pos <= range->Start()) return range;

  // We can't properly connect liveranges if splitting occurred at the end
  // a block.
  DCHECK(pos.IsStart() || pos.IsGapPosition() ||
         (GetInstructionBlock(code(), pos)->last_instruction_index() !=
          pos.ToInstructionIndex()));

  LiveRange* result = range->SplitAt(pos, allocation_zone());
  return result;
}

LiveRange* RegisterAllocator::SplitBetween(LiveRange* range,
                                           LifetimePosition start,
                                           LifetimePosition end) {
  DCHECK(!range->TopLevel()->IsFixed());
  TRACE("Splitting live range %d:%d in position between [%d, %d]\n",
        range->TopLevel()->vreg(), range->relative_id(), start.value(),
        end.value());

  LifetimePosition split_pos = FindOptimalSplitPos(start, end);
  DCHECK(split_pos >= start);
  return SplitRangeAt(range, split_pos);
}

LifetimePosition RegisterAllocator::FindOptimalSplitPos(LifetimePosition start,
                                                        LifetimePosition end) {
  int start_instr = start.ToInstructionIndex();
  int end_instr = end.ToInstructionIndex();
  DCHECK_LE(start_instr, end_instr);

  // We have no choice
  if (start_instr == end_instr) return end;

  const InstructionBlock* start_block = GetInstructionBlock(code(), start);
  const InstructionBlock* end_block = GetInstructionBlock(code(), end);

  if (end_block == start_block) {
    // The interval is split in the same basic block. Split at the latest
    // possible position.
    return end;
  }

  const InstructionBlock* block = end_block;
  // Find header of outermost loop.
  do {
    const InstructionBlock* loop = GetContainingLoop(code(), block);
    if (loop == nullptr ||
        loop->rpo_number().ToInt() <= start_block->rpo_number().ToInt()) {
      // No more loops or loop starts before the lifetime start.
      break;
    }
    block = loop;
  } while (true);

  // We did not find any suitable outer loop. Split at the latest possible
  // position unless end_block is a loop header itself.
  if (block == end_block && !end_block->IsLoopHeader()) return end;

  return LifetimePosition::GapFromInstructionIndex(
      block->first_instruction_index());
}

LifetimePosition RegisterAllocator::FindOptimalSpillingPos(
    LiveRange* range, LifetimePosition pos, SpillMode spill_mode,
    LiveRange** begin_spill_out) {
  *begin_spill_out = range;
  // TODO(herhut): Be more clever here as long as we do not move pos out of
  // deferred code.
  if (spill_mode == SpillMode::kSpillDeferred) return pos;
  const InstructionBlock* block = GetInstructionBlock(code(), pos.Start());
  const InstructionBlock* loop_header =
      block->IsLoopHeader() ? block : GetContainingLoop(code(), block);
  if (loop_header == nullptr) return pos;

  while (loop_header != nullptr) {
    // We are going to spill live range inside the loop.
    // If possible try to move spilling position backwards to loop header.
    // This will reduce number of memory moves on the back edge.
    LifetimePosition loop_start = LifetimePosition::GapFromInstructionIndex(
        loop_header->first_instruction_index());
    // Stop if we moved to a loop header before the value is defined or
    // at the define position that is not beneficial to spill.
    if (range->TopLevel()->Start() > loop_start ||
        (range->TopLevel()->Start() == loop_start &&
         range->TopLevel()->SpillAtLoopHeaderNotBeneficial()))
      return pos;

    LiveRange* live_at_header = range->TopLevel()->GetChildCovers(loop_start);

    if (live_at_header != nullptr && !live_at_header->spilled()) {
      for (const LiveRange* check_use = live_at_header;
           check_use != nullptr && check_use->Start() < pos;
           check_use = check_use->next()) {
        // If we find a use for which spilling is detrimental, don't spill
        // at the loop header
        UsePosition* next_use =
            check_use->NextUsePositionSpillDetrimental(loop_start);
        // UsePosition at the end of a UseInterval may
        // have the same value as the start of next range.
        if (next_use != nullptr && next_use->pos() <= pos) {
          return pos;
        }
      }
      // No register beneficial use inside the loop before the pos.
      *begin_spill_out = live_at_header;
      pos = loop_start;
    }

    // Try hoisting out to an outer loop.
    loop_header = GetContainingLoop(code(), loop_header);
  }
  return pos;
}

void RegisterAllocator::Spill(LiveRange* range, SpillMode spill_mode) {
  DCHECK(!range->spilled());
  DCHECK(spill_mode == SpillMode::kSpillAtDefinition ||
         GetInstructionBlock(code(), range->Start())->IsDeferred());
  TopLevelLiveRange* first = range->TopLevel();
  TRACE("Spilling live range %d:%d mode %d\n", first->vreg(),
        range->relative_id(), spill_mode);

  TRACE("Starting spill type is %d\n", static_cast<int>(first->spill_type()));
  if (first->HasNoSpillType()) {
    TRACE("New spill range needed\n");
    data()->AssignSpillRangeToLiveRange(first, spill_mode);
  }
  // Upgrade the spillmode, in case this was only spilled in deferred code so
  // far.
  if ((spill_mode == SpillMode::kSpillAtDefinition) &&
      (first->spill_type() ==
       TopLevelLiveRange::SpillType::kDeferredSpillRange)) {
    TRACE("Upgrading\n");
    first->set_spill_type(TopLevelLiveRange::SpillType::kSpillRange);
  }
  TRACE("Final spill type is %d\n", static_cast<int>(first->spill_type()));
  range->Spill();
}

const char* RegisterAllocator::RegisterName(int register_code) const {
  if (register_code == kUnassignedRegister) return "unassigned";
  switch (mode()) {
    case RegisterKind::kGeneral:
      return i::RegisterName(Register::from_code(register_code));
    case RegisterKind::kDouble:
      return i::RegisterName(DoubleRegister::from_code(register_code));
    case RegisterKind::kSimd128:
      return i::RegisterName(Simd128Register::from_code(register_code));
  }
}

LinearScanAllocator::LinearScanAllocator(RegisterAllocationData* data,
                                         RegisterKind kind, Zone* local_zone)
    : RegisterAllocator(data, kind),
      unhandled_live_ranges_(local_zone),
      active_live_ranges_(local_zone),
      inactive_live_ranges_(num_registers(), InactiveLiveRangeQueue(local_zone),
                            local_zone),
      next_active_ranges_change_(LifetimePosition::Invalid()),
      next_inactive_ranges_change_(LifetimePosition::Invalid()) {
  active_live_ranges().reserve(8);
}

void LinearScanAllocator::MaybeSpillPreviousRanges(LiveRange* begin_range,
                                                   LifetimePosition begin_pos,
                                                   LiveRange* end_range) {
  // Spill begin_range after begin_pos, then spill every live range of this
  // virtual register until but excluding end_range.
  DCHECK(begin_range->Covers(begin_pos));
  DCHECK_EQ(begin_range->TopLevel(), end_range->TopLevel());

  if (begin_range != end_range) {
    DCHECK_LE(begin_range->End(), end_range->Start());
    if (!begin_range->spilled()) {
      SpillAfter(begin_range, begin_pos, SpillMode::kSpillAtDefinition);
    }
    for (LiveRange* range = begin_range->next(); range != end_range;
         range = range->next()) {
      if (!range->spilled()) {
        range->Spill();
      }
    }
  }
}

void LinearScanAllocator::MaybeUndoPreviousSplit(LiveRange* range, Zone* zone) {
  if (range->next() != nullptr && range->next()->ShouldRecombine()) {
    LiveRange* to_remove = range->next();
    TRACE("Recombining %d:%d with %d\n", range->TopLevel()->vreg(),
          range->relative_id(), to_remove->relative_id());

    // Remove the range from unhandled, as attaching it will change its
    // state and hence ordering in the unhandled set.
    auto removed_cnt = unhandled_live_ranges().erase(to_remove);
    DCHECK_EQ(removed_cnt, 1);
    USE(removed_cnt);

    range->AttachToNext(zone);
  } else if (range->next() != nullptr) {
    TRACE("No recombine for %d:%d to %d\n", range->TopLevel()->vreg(),
          range->relative_id(), range->next()->relative_id());
  }
}

void LinearScanAllocator::SpillNotLiveRanges(RangeRegisterSmallMap& to_be_live,
                                             LifetimePosition position,
                                             SpillMode spill_mode) {
  for (auto it = active_live_ranges().begin();
       it != active_live_ranges().end();) {
    LiveRange* active_range = *it;
    TopLevelLiveRange* toplevel = (*it)->TopLevel();
    auto found = to_be_live.find(toplevel);
    if (found == to_be_live.end()) {
      // Is not contained in {to_be_live}, spill it.
      // Fixed registers are exempt from this. They might have been
      // added from inactive at the block boundary but we know that
      // they cannot conflict as they are built before register
      // allocation starts. It would be algorithmically fine to split
      // them and reschedule but the code does not allow to do this.
      if (toplevel->IsFixed()) {
        TRACE("Keeping reactivated fixed range for %s\n",
              RegisterName(toplevel->assigned_register()));
        ++it;
      } else {
        // When spilling a previously spilled/reloaded range, we add back the
        // tail that we might have split off when we reloaded/spilled it
        // previously. Otherwise we might keep generating small split-offs.
        MaybeUndoPreviousSplit(active_range, allocation_zone());
        TRACE("Putting back %d:%d\n", toplevel->vreg(),
              active_range->relative_id());
        LiveRange* split = SplitRangeAt(active_range, position);
        DCHECK_NE(split, active_range);

        // Make sure we revisit this range once it has a use that requires
        // a register.
        UsePosition* next_use = split->NextRegisterPosition(position);
        if (next_use != nullptr) {
          // Move to the start of the gap before use so that we have a space
          // to perform the potential reload. Otherwise, do not spill but add
          // to unhandled for reallocation.
          LifetimePosition revisit_at = next_use->pos().FullStart();
          TRACE("Next use at %d\n", revisit_at.value());
          if (!data()->IsBlockBoundary(revisit_at)) {
            // Leave some space so we have enough gap room.
            revisit_at = revisit_at.PrevStart().FullStart();
          }
          // If this range became life right at the block boundary that we are
          // currently processing, we do not need to split it. Instead move it
          // to unhandled right away.
          if (position < revisit_at) {
            LiveRange* third_part = SplitRangeAt(split, revisit_at);
            DCHECK_NE(split, third_part);
            Spill(split, spill_mode);
            TRACE("Marking %d:%d to recombine\n", toplevel->vreg(),
                  third_part->relative_id());
            third_part->SetRecombine();
            AddToUnhandled(third_part);
          } else {
            AddToUnhandled(split);
          }
        } else {
          Spill(split, spill_mode);
        }
        it = ActiveToHandled(it);
      }
    } else {
      // This range is contained in {to_be_live}, so we can keep it.
      int expected_register = found->second;
      to_be_live.erase(found);
      if (expected_register == active_range->assigned_register()) {
        // Was life and in correct register, simply pass through.
        TRACE("Keeping %d:%d in %s\n", toplevel->vreg(),
              active_range->relative_id(),
              RegisterName(active_range->assigned_register()));
        ++it;
      } else {
        // Was life but wrong register. Split and schedule for
        // allocation.
        TRACE("Scheduling %d:%d\n", toplevel->vreg(),
              active_range->relative_id());
        LiveRange* split = SplitRangeAt(active_range, position);
        split->set_controlflow_hint(expected_register);
        AddToUnhandled(split);
        it = ActiveToHandled(it);
      }
    }
  }
}

LiveRange* LinearScanAllocator::AssignRegisterOnReload(LiveRange* range,
                                                       int reg) {
  // We know the register is currently free but it might be in
  // use by a currently inactive range. So we might not be able
  // to reload for the full distance. In such case, split here.
  // TODO(herhut):
  // It might be better if we could use the normal unhandled queue and
  // give reloading registers pecedence. That way we would compute the
  // intersection for the entire future.
  LifetimePosition new_end = range->End();
  for (int cur_reg = 0; cur_reg < num_registers(); ++cur_reg) {
    if ((kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) &&
        cur_reg != reg) {
      continue;
    }
    SlowDCheckInactiveLiveRangesIsSorted(cur_reg);
    for (LiveRange* cur_inactive : inactive_live_ranges(cur_reg)) {
      if (kFPAliasing == AliasingKind::kCombine && check_fp_aliasing() &&
          !data()->config()->AreAliases(cur_inactive->representation(), cur_reg,
                                        range->representation(), reg)) {
        continue;
      }
      if (new_end <= cur_inactive->NextStart()) {
        // Inactive ranges are sorted by their next start, so the remaining
        // ranges cannot contribute to new_end.
        break;
      }
      auto next_intersection = cur_inactive->FirstIntersection(range);
      if (!next_intersection.IsValid()) continue;
      new_end = std::min(new_end, next_intersection);
    }
  }
  if (new_end != range->End()) {
    TRACE("Found new end for %d:%d at %d\n", range->TopLevel()->vreg(),
          range->relative_id(), new_end.value());
    LiveRange* tail = SplitRangeAt(range, new_end);
    AddToUnhandled(tail);
  }
  SetLiveRangeAssignedRegister(range, reg);
  return range;
}

void LinearScanAllocator::ReloadLiveRanges(
    RangeRegisterSmallMap const& to_be_live, LifetimePosition position) {
  // Assumption: All ranges in {to_be_live} are currently spilled and there are
  // no conflicting registers in the active ranges.
  // The former is ensured by SpillNotLiveRanges, the latter is by construction
  // of the to_be_live set.
  for (auto [range, reg] : to_be_live) {
    LiveRange* to_resurrect = range->GetChildCovers(position);
    if (to_resurrect == nullptr) {
      // While the range was life until the end of the predecessor block, it is
      // not live in this block. Either there is a lifetime gap or the range
      // died.
      TRACE("No candidate for %d at %d\n", range->vreg(), position.value());
    } else {
      // We might be resurrecting a range that we spilled until its next use
      // before. In such cases, we have to unsplit it before processing as
      // otherwise we might get register changes from one range to the other
      // in the middle of blocks.
      // If there is a gap between this range and the next, we can just keep
      // it as a register change won't hurt.
      MaybeUndoPreviousSplit(to_resurrect, allocation_zone());
      if (to_resurrect->Start() == position) {
        // This range already starts at this block. It might have been spilled,
        // so we have to unspill it. Otherwise, it is already in the unhandled
        // queue waiting for processing.
        DCHECK(!to_resurrect->HasRegisterAssigned());
        TRACE("Reload %d:%d starting at %d itself\n", range->vreg(),
              to_resurrect->relative_id(), position.value());
        if (to_resurrect->spilled()) {
          to_resurrect->Unspill();
          to_resurrect->set_controlflow_hint(reg);
          AddToUnhandled(to_resurrect);
        } else {
          // Assign the preassigned register if we know. Otherwise, nothing to
          // do as already in unhandeled.
          if (reg != kUnassignedRegister) {
            auto erased_cnt = unhandled_live_ranges().erase(to_resurrect);
            DCHECK_EQ(erased_cnt, 1);
            USE(erased_cnt);
            // We know that there is no conflict with active ranges, so just
            // assign the register to the range.
            to_resurrect = AssignRegisterOnReload(to_resurrect, reg);
            AddToActive(to_resurrect);
          }
        }
      } else {
        // This range was spilled before. We have to split it and schedule the
        // second part for allocation (or assign the register if we know).
        DCHECK(to_resurrect->spilled());
        LiveRange* split = SplitRangeAt(to_resurrect, position);
        TRACE("Reload %d:%d starting at %d as %d\n", range->vreg(),
              to_resurrect->relative_id(), split->Start().value(),
              split->relative_id());
        DCHECK_NE(split, to_resurrect);
        if (reg != kUnassignedRegister) {
          // We know that there is no conflict with active ranges, so just
          // assign the register to the range.
          split = AssignRegisterOnReload(split, reg);
          AddToActive(split);
        } else {
          // Let normal register assignment find a suitable register.
          split->set_controlflow_hint(reg);
          AddToUnhandled(split);
        }
      }
    }
  }
}

RpoNumber LinearScanAllocator::ChooseOneOfTwoPredecessorStates(
    InstructionBlock* current_block, LifetimePosition boundary) {
  // Pick the state that would generate the least spill/reloads.
  // Compute vectors of ranges with use counts for both sides.
  // We count uses only for live ranges that are unique to either the left or
  // the right predecessor since many live ranges are shared between both.
  // Shared ranges don't influence the decision anyway and this is faster.
  auto& left = data()->GetSpillState(current_block->predecessors()[0]);
  auto& right = data()->GetSpillState(current_block->predecessors()[1]);

  // Build a set of the `TopLevelLiveRange`s in the left predecessor.
  // Usually this set is very small, e.g., for JetStream2 at most 3 ranges in
  // ~72% of the cases and at most 8 ranges in ~93% of the cases. In those cases
  // `SmallMap` is backed by inline storage and uses fast linear search.
  // In some pathological cases the set grows large (e.g. the Wasm binary of
  // v8:9529) and then `SmallMap` gives us O(log n) worst case lookup when
  // intersecting with the right predecessor below. The set is encoded as a
  // `SmallMap` to `Dummy` values, since we don't have an equivalent `SmallSet`.
  struct Dummy {};
  SmallZoneMap<TopLevelLiveRange*, Dummy, 16> left_set(allocation_zone());
  for (LiveRange* range : left) {
    TopLevelLiveRange* parent = range->TopLevel();
    auto [_, inserted] = left_set.emplace(parent, Dummy{});
    // The `LiveRange`s in `left` come from the spill state, which is just the
    // list of active `LiveRange`s at the end of the block (see
    // `RememberSpillState`). Since at most one `LiveRange` out of a
    // `TopLevelLiveRange` can be active at the same time, there should never be
    // the same `TopLevelLiveRange` twice in `left_set`, hence this check.
    DCHECK(inserted);
    USE(inserted);
  }

  // Now build a list of ranges unique to either the left or right predecessor.
  struct RangeUseCount {
    // The set above contains `TopLevelLiveRange`s, but ultimately we want to
    // count uses of the child `LiveRange` covering `boundary`.
    // The lookup in `GetChildCovers` is O(log n), so do it only once when
    // inserting into this list.
    LiveRange* range;
    // +1 if used in the left predecessor, -1 if used in the right predecessor.
    int use_count_delta;
  };
  SmallZoneVector<RangeUseCount, 16> unique_range_use_counts(allocation_zone());
  for (LiveRange* range : right) {
    TopLevelLiveRange* parent = range->TopLevel();
    auto left_it = left_set.find(parent);
    bool range_is_shared_left_and_right = left_it != left_set.end();
    if (range_is_shared_left_and_right) {
      left_set.erase(left_it);
    } else {
      // This range is unique to the right predecessor, so insert into the list.
      LiveRange* child = parent->GetChildCovers(boundary);
      if (child != nullptr) {
        unique_range_use_counts.push_back({child, -1});
      }
    }
  }
  // So far `unique_range_use_counts` contains only the ranges unique in the
  // right predecessor. Now also add the ranges from the left predecessor.
  for (auto [parent, _] : left_set) {
    LiveRange* child = parent->GetChildCovers(boundary);
    if (child != nullptr) {
      unique_range_use_counts.push_back({child, +1});
    }
  }

  // Finally, count the uses for each range.
  int use_count_difference = 0;
  for (auto [range, use_count] : unique_range_use_counts) {
    if (range->NextUsePositionRegisterIsBeneficial(boundary) != nullptr) {
      use_count_difference += use_count;
    }
  }
  if (use_count_difference == 0) {
    // There is a tie in beneficial register uses. Now, look at any use at all.
    // We do not account for all uses, like flowing into a phi.
    // So we just look at ranges still being live.
    TRACE("Looking at only uses\n");
    for (auto [range, use_count] : unique_range_use_counts) {
      if (range->NextUsePosition(boundary) != range->positions().end()) {
        use_count_difference += use_count;
      }
    }
  }
  TRACE("Left predecessor has %d more uses than right\n", use_count_difference);
  return use_count_difference > 0 ? current_block->predecessors()[0]
                                  : current_block->predecessors()[1];
}

bool LinearScanAllocator::CheckConflict(
    MachineRepresentation rep, int reg,
    const RangeRegisterSmallMap& to_be_live) {
  for (auto [range, expected_reg] : to_be_live) {
    if (data()->config()->AreAliases(range->representation(), expected_reg, rep,
                                     reg)) {
      return true;
    }
  }
  return false;
}

void LinearScanAllocator::ComputeStateFromManyPredecessors(
    InstructionBlock* current_block, RangeRegisterSmallMap& to_be_live) {
  struct Vote {
    size_t count;
    int used_registers[RegisterConfiguration::kMaxRegisters];
    explicit Vote(int reg) : count(1), used_registers{0} {
      used_registers[reg] = 1;
    }
  };
  // Typically this map is very small, e.g., on JetStream2 it has at most 3
  // elements ~80% of the time and at most 8 elements ~94% of the time.
  // Thus use a `SmallZoneMap` to avoid allocations and because linear search
  // in an array is faster than map lookup for such small sizes.
  // We don't want too many inline elements though since `Vote` is pretty large.
  using RangeVoteMap = SmallZoneMap<TopLevelLiveRange*, Vote, 16>;
  static_assert(sizeof(RangeVoteMap) < 4096, "too large stack allocation");
  RangeVoteMap counts(data()->allocation_zone());

  int deferred_blocks = 0;
  for (RpoNumber pred : current_block->predecessors()) {
    if (!ConsiderBlockForControlFlow(current_block, pred)) {
      // Back edges of a loop count as deferred here too.
      deferred_blocks++;
      continue;
    }
    const auto& pred_state = data()->GetSpillState(pred);
    for (LiveRange* range : pred_state) {
      // We might have spilled the register backwards, so the range we
      // stored might have lost its register. Ignore those.
      if (!range->HasRegisterAssigned()) continue;
      TopLevelLiveRange* toplevel = range->TopLevel();
      auto [it, inserted] =
          counts.try_emplace(toplevel, range->assigned_register());
      if (!inserted) {
        it->second.count++;
        it->second.used_registers[range->assigned_register()]++;
      }
    }
  }

  // Choose the live ranges from the majority.
  const size_t majority =
      (current_block->PredecessorCount() + 2 - deferred_blocks) / 2;
  bool taken_registers[RegisterConfiguration::kMaxRegisters] = {false};
  DCHECK(to_be_live.empty());
  auto assign_to_live = [this, majority, &counts](
                            std::function<bool(TopLevelLiveRange*)> filter,
                            RangeRegisterSmallMap& to_be_live,
                            bool* taken_registers) {
    bool check_aliasing =
        kFPAliasing == AliasingKind::kCombine && check_fp_aliasing();
    for (const auto& val : counts) {
      if (!filter(val.first)) continue;
      if (val.second.count >= majority) {
        int register_max = 0;
        int reg = kUnassignedRegister;
        bool conflict = false;
        int num_regs = num_registers();
        int num_codes = num_allocatable_registers();
        const int* codes = allocatable_register_codes();
        MachineRepresentation rep = val.first->representation();
        if (check_aliasing && (rep == MachineRepresentation::kFloat32 ||
                               rep == MachineRepresentation::kSimd128 ||
                               rep == MachineRepresentation::kSimd256))
          GetFPRegisterSet(rep, &num_regs, &num_codes, &codes);
        for (int idx = 0; idx < num_regs; idx++) {
          int uses = val.second.used_registers[idx];
          if (uses == 0) continue;
          if (uses > register_max || (conflict && uses == register_max)) {
            reg = idx;
            register_max = uses;
            conflict = check_aliasing ? CheckConflict(rep, reg, to_be_live)
                                      : taken_registers[reg];
          }
        }
        if (conflict) {
          reg = kUnassignedRegister;
        } else if (!check_aliasing) {
          taken_registers[reg] = true;
        }
        TRACE("Reset %d as live due vote %zu in %s\n", val.first->vreg(),
              val.second.count, RegisterName(reg));
        auto [_, inserted] = to_be_live.emplace(val.first, reg);
        DCHECK(inserted);
        USE(inserted);
      }
    }
  };
  // First round, process fixed registers, as these have precedence.
  // There is only one fixed range per register, so we cannot have
  // conflicts.
  assign_to_live([](TopLevelLiveRange* r) { return r->IsFixed(); }, to_be_live,
                 taken_registers);
  // Second round, process the rest.
  assign_to_live([](TopLevelLiveRange* r) { return !r->IsFixed(); }, to_be_live,
                 taken_registers);
}

bool LinearScanAllocator::ConsiderBlockForControlFlow(
    InstructionBlock* current_block, RpoNumber predecessor) {
  // We ignore predecessors on back edges when looking for control flow effects,
  // as those lie in the future of allocation and we have no data yet. Also,
  // deferred bocks are ignored on deferred to non-deferred boundaries, as we do
  // not want them to influence allocation of non deferred code.
  return (predecessor < current_block->rpo_number()) &&
         (current_block->IsDeferred() ||
          !code()->InstructionBlockAt(predecessor)->IsDeferred());
}

void LinearScanAllocator::UpdateDeferredFixedRanges(SpillMode spill_mode,
                                                    InstructionBlock* block) {
  if (spill_mode == SpillMode::kSpillDeferred) {
    LifetimePosition max = LifetimePosition::InstructionFromInstructionIndex(
        LastDeferredInstructionIndex(block));
    // Adds range back to inactive, resolving resulting conflicts.
    auto add_to_inactive = [this, max](LiveRange* range) {
      AddToInactive(range);
      // Splits other if it conflicts with range. Other is placed in unhandled
      // for later reallocation.
      auto split_conflicting = [this, max](LiveRange* range, LiveRange* other,
                                           std::function<void(LiveRange*)>
                                               update_caches) {
        if (other->TopLevel()->IsFixed()) return;
        int reg = range->assigned_register();
        if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
          if (other->assigned_register() != reg) {
            return;
          }
        } else {
          if (!data()->config()->AreAliases(range->representation(), reg,
                                            other->representation(),
                                            other->assigned_register())) {
            return;
          }
        }
        // The inactive range might conflict, so check whether we need to
        // split and spill. We can look for the first intersection, as there
        // cannot be any intersections in the past, as those would have been a
        // conflict then.
        LifetimePosition next_start = range->FirstIntersection(other);
        if (!next_start.IsValid() || (next_start > max)) {
          // There is no conflict or the conflict is outside of the current
          // stretch of deferred code. In either case we can ignore the
          // inactive range.
          return;
        }
        // They overlap. So we need to split active and reschedule it
        // for allocation.
        TRACE("Resolving conflict of %d with deferred fixed for register %s\n",
              other->TopLevel()->vreg(),
              RegisterName(other->assigned_register()));
        LiveRange* split_off =
            other->SplitAt(next_start, data()->allocation_zone());
        // Try to get the same register after the deferred block.
        split_off->set_controlflow_hint(other->assigned_register());
        DCHECK_NE(split_off, other);
        AddToUnhandled(split_off);
        update_caches(other);
      };
      // Now check for conflicts in active and inactive ranges. We might have
      // conflicts in inactive, as we do not do this check on every block
      // boundary but only on deferred/non-deferred changes but inactive
      // live ranges might become live on any block boundary.
      for (auto active : active_live_ranges()) {
        split_conflicting(range, active, [this](LiveRange* updated) {
          next_active_ranges_change_ =
              std::min(updated->End(), next_active_ranges_change_);
        });
      }
      for (int reg = 0; reg < num_registers(); ++reg) {
        if ((kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) &&
            reg != range->assigned_register()) {
          continue;
        }
        SlowDCheckInactiveLiveRangesIsSorted(reg);
        for (auto inactive : inactive_live_ranges(reg)) {
          if (inactive->NextStart() > max) break;
          split_conflicting(range, inactive, [this](LiveRange* updated) {
            next_inactive_ranges_change_ =
                std::min(updated->End(), next_inactive_ranges_change_);
          });
        }
      }
    };
    if (mode() == RegisterKind::kGeneral) {
      for (TopLevelLiveRange* current : data()->fixed_live_ranges()) {
        if (current != nullptr) {
          if (current->IsDeferredFixed()) {
            add_to_inactive(current);
          }
        }
      }
    } else if (mode() == RegisterKind::kDouble) {
      for (TopLevelLiveRange* current : data()->fixed_double_live_ranges()) {
        if (current != nullptr) {
          if (current->IsDeferredFixed()) {
            add_to_inactive(current);
          }
        }
      }
      if (kFPAliasing == AliasingKind::kCombine && check_fp_aliasing()) {
        for (TopLevelLiveRange* current : data()->fixed_float_live_ranges()) {
          if (current != nullptr) {
            if (current->IsDeferredFixed()) {
              add_to_inactive(current);
            }
          }
        }
        for (TopLevelLiveRange* current : data()->fixed_simd128_live_ranges()) {
          if (current != nullptr) {
            if (current->IsDeferredFixed()) {
              add_to_inactive(current);
            }
          }
        }
      }
    } else {
      DCHECK_EQ(mode(), RegisterKind::kSimd128);
      for (TopLevelLiveRange* current : data()->fixed_simd128_live_ranges()) {
        if (current != nullptr) {
          if (current->IsDeferredFixed()) {
            add_to_inactive(current);
          }
        }
      }
    }
  } else {
    // Remove all ranges.
    for (int reg = 0; reg < num_registers(); ++reg) {
      for (auto it = inactive_live_ranges(reg).begin();
           it != inactive_live_ranges(reg).end();) {
        if ((*it)->TopLevel()->IsDeferredFixed()) {
          it = inactive_live_ranges(reg).erase(it);
        } else {
          ++it;
        }
      }
    }
  }
}

bool LinearScanAllocator::BlockIsDeferredOrImmediatePredecessorIsNotDeferred(
    const InstructionBlock* block) {
  if (block->IsDeferred()) return true;
  if (block->PredecessorCount() == 0) return true;
  bool pred_is_deferred = false;
  for (auto pred : block->predecessors()) {
    if (pred.IsNext(block->rpo_number())) {
      pred_is_deferred = code()->InstructionBlockAt(pred)->IsDeferred();
      break;
    }
  }
  return !pred_is_deferred;
}

bool LinearScanAllocator::HasNonDeferredPredecessor(InstructionBlock* block) {
  for (auto pred : block->predecessors()) {
    InstructionBlock* pred_block = code()->InstructionBlockAt(pred);
    if (!pred_block->IsDeferred()) return true;
  }
  return false;
}

void LinearScanAllocator::AllocateRegisters() {
  DCHECK(unhandled_live_ranges().empty());
  DCHECK(active_live_ranges().empty());
  for (int reg = 0; reg < num_registers(); ++reg) {
    DCHECK(inactive_live_ranges(reg).empty());
  }

  SplitAndSpillRangesDefinedByMemoryOperand();
  data()->ResetSpillState();

  if (v8_flags.trace_turbo_alloc) {
    PrintRangeOverview();
  }

  const size_t live_ranges_size = data()->live_ranges().size();
  for (TopLevelLiveRange* range : data()->live_ranges()) {
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    if (!CanProcessRange(range)) continue;
    for (LiveRange* to_add = range; to_add != nullptr;
         to_add = to_add->next()) {
      if (!to_add->spilled()) {
        AddToUnhandled(to_add);
      }
    }
  }

  if (mode() == RegisterKind::kGeneral) {
    for (TopLevelLiveRange* current : data()->fixed_live_ranges()) {
      if (current != nullptr) {
        if (current->IsDeferredFixed()) continue;
        AddToInactive(current);
      }
    }
  } else if (mode() == RegisterKind::kDouble) {
    for (TopLevelLiveRange* current : data()->fixed_double_live_ranges()) {
      if (current != nullptr) {
        if (current->IsDeferredFixed()) continue;
        AddToInactive(current);
      }
    }
    if (kFPAliasing == AliasingKind::kCombine && check_fp_aliasing()) {
      for (TopLevelLiveRange* current : data()->fixed_float_live_ranges()) {
        if (current != nullptr) {
          if (current->IsDeferredFixed()) continue;
          AddToInactive(current);
        }
      }
      for (TopLevelLiveRange* current : data()->fixed_simd128_live_ranges()) {
        if (current != nullptr) {
          if (current->IsDeferredFixed()) continue;
          AddToInactive(current);
        }
      }
    }
  } else {
    DCHECK(mode() == RegisterKind::kSimd128);
    for (TopLevelLiveRange* current : data()->fixed_simd128_live_ranges()) {
      if (current != nullptr) {
        if (current->IsDeferredFixed()) continue;
        AddToInactive(current);
      }
    }
  }

  RpoNumber last_block = RpoNumber::FromInt(0);
  RpoNumber max_blocks =
      RpoNumber::FromInt(code()->InstructionBlockCount() - 1);
  LifetimePosition next_block_boundary =
      LifetimePosition::InstructionFromInstructionIndex(
          data()
              ->code()
              ->InstructionBlockAt(last_block)
              ->last_instruction_index())
          .NextFullStart();
  SpillMode spill_mode = SpillMode::kSpillAtDefinition;

  // Process all ranges. We also need to ensure that we have seen all block
  // boundaries. Linear scan might have assigned and spilled ranges before
  // reaching the last block and hence we would ignore control flow effects for
  // those. Not only does this produce a potentially bad assignment, it also
  // breaks with the invariant that we undo spills that happen in deferred code
  // when crossing a deferred/non-deferred boundary.
  while (!unhandled_live_ranges().empty() || last_block < max_blocks) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    LiveRange* current = unhandled_live_ranges().empty()
                             ? nullptr
                             : *unhandled_live_ranges().begin();
    LifetimePosition position =
        current ? current->Start() : next_block_boundary;
#ifdef DEBUG
    allocation_finger_ = position;
#endif
    // Check whether we just moved across a block boundary. This will trigger
    // for the first range that is past the current boundary.
    if (position >= next_block_boundary) {
      TRACE("Processing boundary at %d leaving %d\n",
            next_block_boundary.value(), last_block.ToInt());

      // Forward state to before block boundary
      LifetimePosition end_of_block = next_block_boundary.PrevStart().End();
      ForwardStateTo(end_of_block);

      // Remember this state.
      InstructionBlock* current_block = data()->code()->GetInstructionBlock(
          next_block_boundary.ToInstructionIndex());

      // Store current spill state (as the state at end of block). For
      // simplicity, we store the active ranges, e.g., the live ranges that
      // are not spilled.
      data()->RememberSpillState(last_block, active_live_ranges());

      // Only reset the state if this was not a direct fallthrough. Otherwise
      // control flow resolution will get confused (it does not expect changes
      // across fallthrough edges.).
      bool fallthrough =
          (current_block->PredecessorCount() == 1) &&
          current_block->predecessors()[0].IsNext(current_block->rpo_number());

      // When crossing a deferred/non-deferred boundary, we have to load or
      // remove the deferred fixed ranges from inactive.
      if ((spill_mode == SpillMode::kSpillDeferred) !=
          current_block->IsDeferred()) {
        // Update spill mode.
        spill_mode = current_block->IsDeferred()
                         ? SpillMode::kSpillDeferred
                         : SpillMode::kSpillAtDefinition;

        ForwardStateTo(next_block_boundary);

#ifdef DEBUG
        // Allow allocation at current position.
        allocation_finger_ = next_block_boundary;
#endif
        UpdateDeferredFixedRanges(spill_mode, current_block);
      }

      // Allocation relies on the fact that each non-deferred block has at
      // least one non-deferred predecessor. Check this invariant here.
      DCHECK_IMPLIES(!current_block->IsDeferred(),
                     HasNonDeferredPredecessor(current_block));

      if (!fallthrough) {
#ifdef DEBUG
        // Allow allocation at current position.
        allocation_finger_ = next_block_boundary;
#endif

        // We are currently at next_block_boundary - 1. Move the state to the
        // actual block boundary position. In particular, we have to
        // reactivate inactive ranges so that they get rescheduled for
        // allocation if they were not live at the predecessors.
        ForwardStateTo(next_block_boundary);

        RangeRegisterSmallMap to_be_live(allocation_zone());

        // If we end up deciding to use the state of the immediate
        // predecessor, it is better not to perform a change. It would lead to
        // the same outcome anyway.
        // This may never happen on boundaries between deferred and
        // non-deferred code, as we rely on explicit respill to ensure we
        // spill at definition.
        bool no_change_required = false;

        auto pick_state_from = [this, current_block](
                                   RpoNumber pred,
                                   RangeRegisterSmallMap& to_be_live) -> bool {
          TRACE("Using information from B%d\n", pred.ToInt());
          // If this is a fall-through that is not across a deferred
          // boundary, there is nothing to do.
          bool is_noop = pred.IsNext(current_block->rpo_number());
          if (!is_noop) {
            auto& spill_state = data()->GetSpillState(pred);
            TRACE("Not a fallthrough. Adding %zu elements...\n",
                  spill_state.size());
            LifetimePosition pred_end =
                LifetimePosition::GapFromInstructionIndex(
                    this->code()->InstructionBlockAt(pred)->code_end());
            DCHECK(to_be_live.empty());
            for (const auto range : spill_state) {
              // Filter out ranges that were split or had their register
              // stolen by backwards working spill heuristics. These have
              // been spilled after the fact, so ignore them.
              if (range->End() < pred_end || !range->HasRegisterAssigned())
                continue;
              auto [_, inserted] = to_be_live.emplace(
                  range->TopLevel(), range->assigned_register());
              DCHECK(inserted);
              USE(inserted);
            }
          }
          return is_noop;
        };

        // Multiple cases here:
        // 1) We have a single predecessor => this is a control flow split, so
        //     just restore the predecessor state.
        // 2) We have two predecessors => this is a conditional, so break ties
        //     based on what to do based on forward uses, trying to benefit
        //     the same branch if in doubt (make one path fast).
        // 3) We have many predecessors => this is a switch. Compute union
        //     based on majority, break ties by looking forward.
        if (current_block->PredecessorCount() == 1) {
          TRACE("Single predecessor for B%d\n",
                current_block->rpo_number().ToInt());
          no_change_required =
              pick_state_from(current_block->predecessors()[0], to_be_live);
        } else if (current_block->PredecessorCount() == 2) {
          TRACE("Two predecessors for B%d\n",
                current_block->rpo_number().ToInt());
          // If one of the branches does not contribute any information,
          // e.g. because it is deferred or a back edge, we can short cut
          // here right away.
          RpoNumber chosen_predecessor = RpoNumber::Invalid();
          if (!ConsiderBlockForControlFlow(current_block,
                                           current_block->predecessors()[0])) {
            chosen_predecessor = current_block->predecessors()[1];
          } else if (!ConsiderBlockForControlFlow(
                         current_block, current_block->predecessors()[1])) {
            chosen_predecessor = current_block->predecessors()[0];
          } else {
            chosen_predecessor = ChooseOneOfTwoPredecessorStates(
                current_block, next_block_boundary);
          }
          no_change_required = pick_state_from(chosen_predecessor, to_be_live);

        } else {
          // Merge at the end of, e.g., a switch.
          ComputeStateFromManyPredecessors(current_block, to_be_live);
        }

        if (!no_change_required) {
          SpillNotLiveRanges(to_be_live, next_block_boundary, spill_mode);
          ReloadLiveRanges(to_be_live, next_block_boundary);
        }
      }
      // Update block information
      last_block = current_block->rpo_number();
      next_block_boundary = LifetimePosition::InstructionFromInstructionIndex(
                                current_block->last_instruction_index())
                                .NextFullStart();

      // We might have created new unhandled live ranges, so cycle around the
      // loop to make sure we pick the top most range in unhandled for
      // processing.
      continue;
    }

    DCHECK_NOT_NULL(current);

    TRACE("Processing interval %d:%d start=%d\n", current->TopLevel()->vreg(),
          current->relative_id(), position.value());

    // Now we can erase current, as we are sure to process it.
    unhandled_live_ranges().erase(unhandled_live_ranges().begin());

    if (current->IsTopLevel() && TryReuseSpillForPhi(current->TopLevel()))
      continue;

    ForwardStateTo(position);

    DCHECK(!current->HasRegisterAssigned() && !current->spilled());

    ProcessCurrentRange(current, spill_mode);
  }

  if (v8_flags.trace_turbo_alloc) {
    PrintRangeOverview();
  }
}

void LinearScanAllocator::SetLiveRangeAssignedRegister(LiveRange* range,
                                                       int reg) {
  data()->MarkAllocated(range->representation(), reg);
  range->set_assigned_register(reg);
  range->SetUseHints(reg);
  range->UpdateBundleRegister(reg);
  if (range->IsTopLevel() && range->TopLevel()->is_phi()) {
    data()->GetPhiMapValueFor(range->TopLevel())->set_assigned_register(reg);
  }
}

void LinearScanAllocator::AddToActive(LiveRange* range) {
  TRACE("Add live range %d:%d in %s to active\n", range->TopLevel()->vreg(),
        range->relative_id(), RegisterName(range->assigned_register()));
  active_live_ranges().push_back(range);
  next_active_ranges_change_ =
      std::min(next_active_ranges_change_, range->NextEndAfter(range->Start()));
}

void LinearScanAllocator::AddToInactive(LiveRange* range) {
  TRACE("Add live range %d:%d to inactive\n", range->TopLevel()->vreg(),
        range->relative_id());
  next_inactive_ranges_change_ = std::min(
      next_inactive_ranges_change_, range->NextStartAfter(range->Start()));
  DCHECK(range->HasRegisterAssigned());
  // Keep `inactive_live_ranges` sorted.
  inactive_live_ranges(range->assigned_register())
      .insert(std::upper_bound(
                  inactive_live_ranges(range->assigned_register()).begin(),
                  inactive_live_ranges(range->assigned_register()).end(), range,
                  InactiveLiveRangeOrdering()),
              1, range);
}

void LinearScanAllocator::AddToUnhandled(LiveRange* range) {
  if (range == nullptr || range->IsEmpty()) return;
  DCHECK(!range->HasRegisterAssigned() && !range->spilled());
  DCHECK(allocation_finger_ <= range->Start());

  TRACE("Add live range %d:%d to unhandled\n", range->TopLevel()->vreg(),
        range->relative_id());
  unhandled_live_ranges().insert(range);
}

ZoneVector<LiveRange*>::iterator LinearScanAllocator::ActiveToHandled(
    const ZoneVector<LiveRange*>::iterator it) {
  TRACE("Moving live range %d:%d from active to handled\n",
        (*it)->TopLevel()->vreg(), (*it)->relative_id());
  return active_live_ranges().erase(it);
}

ZoneVector<LiveRange*>::iterator LinearScanAllocator::ActiveToInactive(
    const ZoneVector<LiveRange*>::iterator it, LifetimePosition position) {
  LiveRange* range = *it;
  TRACE("Moving live range %d:%d from active to inactive\n",
        (range)->TopLevel()->vreg(), range->relative_id());
  LifetimePosition next_active = range->NextStartAfter(position);
  next_inactive_ranges_change_ =
      std::min(next_inactive_ranges_change_, next_active);
  DCHECK(range->HasRegisterAssigned());
  // Keep `inactive_live_ranges` sorted.
  inactive_live_ranges(range->assigned_register())
      .insert(std::upper_bound(
                  inactive_live_ranges(range->assigned_register()).begin(),
                  inactive_live_ranges(range->assigned_register()).end(), range,
                  InactiveLiveRangeOrdering()),
              1, range);
  return active_live_ranges().erase(it);
}

LinearScanAllocator::InactiveLiveRangeQueue::iterator
LinearScanAllocator::InactiveToHandled(InactiveLiveRangeQueue::iterator it) {
  LiveRange* range = *it;
  TRACE("Moving live range %d:%d from inactive to handled\n",
        range->TopLevel()->vreg(), range->relative_id());
  int reg = range->assigned_register();
  // This must keep the order of `inactive_live_ranges` intact since one of its
  // callers `SplitAndSpillIntersecting` relies on it being sorted.
  return inactive_live_ranges(reg).erase(it);
}

LinearScanAllocator::InactiveLiveRangeQueue::iterator
LinearScanAllocator::InactiveToActive(InactiveLiveRangeQueue::iterator it,
                                      LifetimePosition position) {
  LiveRange* range = *it;
  active_live_ranges().push_back(range);
  TRACE("Moving live range %d:%d from inactive to active\n",
        range->TopLevel()->vreg(), range->relative_id());
  next_active_ranges_change_ =
      std::min(next_active_ranges_change_, range->NextEndAfter(position));
  int reg = range->assigned_register();
  // Remove the element without copying O(n) subsequent elements.
  // The order of `inactive_live_ranges` is established afterwards by sorting in
  // `ForwardStateTo`, which is the only caller.
  std::swap(*it, inactive_live_ranges(reg).back());
  inactive_live_ranges(reg).pop_back();
  return it;
}

void LinearScanAllocator::ForwardStateTo(LifetimePosition position) {
  if (position >= next_active_ranges_change_) {
    next_active_ranges_change_ = LifetimePosition::MaxPosition();
    for (auto it = active_live_ranges().begin();
         it != active_live_ranges().end();) {
      LiveRange* cur_active = *it;
      if (cur_active->End() <= position) {
        it = ActiveToHandled(it);
      } else if (!cur_active->Covers(position)) {
        it = ActiveToInactive(it, position);
      } else {
        next_active_ranges_change_ = std::min(
            next_active_ranges_change_, cur_active->NextEndAfter(position));
        ++it;
      }
    }
  }

  if (position >= next_inactive_ranges_change_) {
    next_inactive_ranges_change_ = LifetimePosition::MaxPosition();
    for (int reg = 0; reg < num_registers(); ++reg) {
      for (auto it = inactive_live_ranges(reg).begin();
           it != inactive_live_ranges(reg).end();) {
        LiveRange* cur_inactive = *it;
        if (cur_inactive->End() <= position) {
          it = InactiveToHandled(it);
        } else if (cur_inactive->Covers(position)) {
          it = InactiveToActive(it, position);
        } else {
          next_inactive_ranges_change_ = std::min(
              next_inactive_ranges_change_,
              // This modifies `cur_inactive.next_start_` and thus
              // invalidates the ordering of `inactive_live_ranges(reg)`.
              cur_inactive->NextStartAfter(position));
          ++it;
        }
      }
      std::sort(inactive_live_ranges(reg).begin(),
                inactive_live_ranges(reg).end(), InactiveLiveRangeOrdering());
    }
  }

  for (int reg = 0; reg < num_registers(); ++reg) {
    SlowDCheckInactiveLiveRangesIsSorted(reg);
  }
}

int LinearScanAllocator::LastDeferredInstructionIndex(InstructionBlock* start) {
  DCHECK(start->IsDeferred());
  RpoNumber last_block =
      RpoNumber::FromInt(code()->InstructionBlockCount() - 1);
  while ((start->rpo_number() < last_block)) {
    InstructionBlock* next =
        code()->InstructionBlockAt(start->rpo_number().Next());
    if (!next->IsDeferred()) break;
    start = next;
  }
  return start->last_instruction_index();
}

void LinearScanAllocator::GetFPRegisterSet(MachineRepresentation rep,
                                           int* num_regs, int* num_codes,
                                           const int** codes) const {
  DCHECK_EQ(kFPAliasing, AliasingKind::kCombine);
  if (rep == MachineRepresentation::kFloat32) {
    *num_regs = data()->config()->num_float_registers();
    *num_codes = data()->config()->num_allocatable_float_registers();
    *codes = data()->config()->allocatable_float_codes();
  } else if (rep == MachineRepresentation::kSimd128) {
    *num_regs = data()->config()->num_simd128_registers();
    *num_codes = data()->config()->num_allocatable_simd128_registers();
    *codes = data()->config()->allocatable_simd128_codes();
  } else if (rep == MachineRepresentation::kSimd256) {
    *num_regs = data()->config()->num_simd256_registers();
    *num_codes = data()->config()->num_allocatable_simd256_registers();
    *codes = data()->config()->allocatable_simd256_codes();
  } else {
    UNREACHABLE();
  }
}

void LinearScanAllocator::GetSIMD128RegisterSet(int* num_regs, int* num_codes,
                                                const int** codes) const {
  DCHECK_EQ(kFPAliasing, AliasingKind::kIndependent);

  *num_regs = data()->config()->num_simd128_registers();
  *num_codes = data()->config()->num_allocatable_simd128_registers();
  *codes = data()->config()->allocatable_simd128_codes();
}

void LinearScanAllocator::FindFreeRegistersForRange(
    LiveRange* range, base::Vector<LifetimePosition> positions) {
  int num_regs = num_registers();
  int num_codes = num_allocatable_registers();
  const int* codes = allocatable_register_codes();
  MachineRepresentation rep = range->representation();
  if (kFPAliasing == AliasingKind::kCombine &&
      (rep == MachineRepresentation::kFloat32 ||
       rep == MachineRepresentation::kSimd128)) {
    GetFPRegisterSet(rep, &num_regs, &num_codes, &codes);
  } else if (kFPAliasing == AliasingKind::kIndependent &&
             (rep == MachineRepresentation::kSimd128)) {
    GetSIMD128RegisterSet(&num_regs, &num_codes, &codes);
  }
  DCHECK_GE(positions.length(), num_regs);

  for (int i = 0; i < num_regs; ++i) {
    positions[i] = LifetimePosition::MaxPosition();
  }

  for (LiveRange* cur_active : active_live_ranges()) {
    int cur_reg = cur_active->assigned_register();
    if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
      positions[cur_reg] = LifetimePosition::GapFromInstructionIndex(0);
      TRACE("Register %s is free until pos %d (1) due to %d\n",
            RegisterName(cur_reg),
            LifetimePosition::GapFromInstructionIndex(0).value(),
            cur_active->TopLevel()->vreg());
    } else {
      int alias_base_index = -1;
      int aliases = data()->config()->GetAliases(
          cur_active->representation(), cur_reg, rep, &alias_base_index);
      DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
      while (aliases--) {
        int aliased_reg = alias_base_index + aliases;
        positions[aliased_reg] = LifetimePosition::GapFromInstructionIndex(0);
      }
    }
  }

  for (int cur_reg = 0; cur_reg < num_regs; ++cur_reg) {
    SlowDCheckInactiveLiveRangesIsSorted(cur_reg);
    for (LiveRange* cur_inactive : inactive_live_ranges(cur_reg)) {
      DCHECK_GT(cur_inactive->End(), range->Start());
      DCHECK_EQ(cur_inactive->assigned_register(), cur_reg);
      // No need to carry out intersections, when this register won't be
      // interesting to this range anyway.
      // TODO(mtrofin): extend to aliased ranges, too.
      if ((kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) &&
          (positions[cur_reg] <= cur_inactive->NextStart() ||
           range->End() <= cur_inactive->NextStart())) {
        break;
      }
      LifetimePosition next_intersection =
          cur_inactive->FirstIntersection(range);
      if (!next_intersection.IsValid()) continue;
      if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
        positions[cur_reg] = std::min(positions[cur_reg], next_intersection);
        TRACE("Register %s is free until pos %d (2)\n", RegisterName(cur_reg),
              positions[cur_reg].value());
      } else {
        int alias_base_index = -1;
        int aliases = data()->config()->GetAliases(
            cur_inactive->representation(), cur_reg, rep, &alias_base_index);
        DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
        while (aliases--) {
          int aliased_reg = alias_base_index + aliases;
          positions[aliased_reg] =
              std::min(positions[aliased_reg], next_intersection);
        }
      }
    }
  }
}

// High-level register allocation summary:
//
// We attempt to first allocate the preferred (hint) register. If that is not
// possible, we find a register that's free, and allocate that. If that's not
// possible, we search for a register to steal from a range that was allocated.
// The goal is to optimize for throughput by avoiding register-to-memory moves,
// which are expensive.
void LinearScanAllocator::ProcessCurrentRange(LiveRange* current,
                                              SpillMode spill_mode) {
  base::EmbeddedVector<LifetimePosition, RegisterConfiguration::kMaxRegisters>
      free_until_pos;
  FindFreeRegistersForRange(current, free_until_pos);
  if (!TryAllocatePreferredReg(current, free_until_pos)) {
    if (!TryAllocateFreeReg(current, free_until_pos)) {
      AllocateBlockedReg(current, spill_mode);
    }
  }
  if (current->HasRegisterAssigned()) {
    AddToActive(current);
  }
}

bool LinearScanAllocator::TryAllocatePreferredReg(
    LiveRange* current, base::Vector<const LifetimePosition> free_until_pos) {
  int hint_register;
  if (current->RegisterFromControlFlow(&hint_register) ||
      current->RegisterFromFirstHint(&hint_register) ||
      current->RegisterFromBundle(&hint_register)) {
    TRACE(
        "Found reg hint %s (free until [%d) for live range %d:%d (end %d[).\n",
        RegisterName(hint_register), free_until_pos[hint_register].value(),
        current->TopLevel()->vreg(), current->relative_id(),
        current->End().value());

    // The desired register is free until the end of the current live range.
    if (free_until_pos[hint_register] >= current->End()) {
      TRACE("Assigning preferred reg %s to live range %d:%d\n",
            RegisterName(hint_register), current->TopLevel()->vreg(),
            current->relative_id());
      SetLiveRangeAssignedRegister(current, hint_register);
      return true;
    }
  }
  return false;
}

int LinearScanAllocator::PickRegisterThatIsAvailableLongest(
    LiveRange* current, int hint_reg,
    base::Vector<const LifetimePosition> free_until_pos) {
  int num_regs = 0;  // used only for the call to GetFPRegisterSet.
  int num_codes = num_allocatable_registers();
  const int* codes = allocatable_register_codes();
  MachineRepresentation rep = current->representation();
  if (kFPAliasing == AliasingKind::kCombine &&
      (rep == MachineRepresentation::kFloat32 ||
       rep == MachineRepresentation::kSimd128)) {
    GetFPRegisterSet(rep, &num_regs, &num_codes, &codes);
  } else if (kFPAliasing == AliasingKind::kIndependent &&
             (rep == MachineRepresentation::kSimd128)) {
    GetSIMD128RegisterSet(&num_regs, &num_codes, &codes);
  }

  DCHECK_GE(free_until_pos.length(), num_codes);

  // Find the register which stays free for the longest time. Check for
  // the hinted register first, as we might want to use that one. Only
  // count full instructions for free ranges, as an instruction's internal
  // positions do not help but might shadow a hinted register. This is
  // typically the case for function calls, where all registered are
  // cloberred after the call except for the argument registers, which are
  // set before the call. Hence, the argument registers always get ignored,
  // as their available time is shorter.
  int reg = (hint_reg == kUnassignedRegister) ? codes[0] : hint_reg;
  int current_free = free_until_pos[reg].ToInstructionIndex();
  for (int i = 0; i < num_codes; ++i) {
    int code = codes[i];
    // Prefer registers that have no fixed uses to avoid blocking later hints.
    // We use the first register that has no fixed uses to ensure we use
    // byte addressable registers in ia32 first.
    int candidate_free = free_until_pos[code].ToInstructionIndex();
    TRACE("Register %s in free until %d\n", RegisterName(code), candidate_free);
    if ((candidate_free > current_free) ||
        (candidate_free == current_free && reg != hint_reg &&
         (data()->HasFixedUse(current->representation(), reg) &&
          !data()->HasFixedUse(current->representation(), code)))) {
      reg = code;
      current_free = candidate_free;
    }
  }

  return reg;
}

bool LinearScanAllocator::TryAllocateFreeReg(
    LiveRange* current, base::Vector<const LifetimePosition> free_until_pos) {
  // Compute register hint, if such exists.
  int hint_reg = kUnassignedRegister;
  current->RegisterFromControlFlow(&hint_reg) ||
      current->RegisterFromFirstHint(&hint_reg) ||
      current->RegisterFromBundle(&hint_reg);

  int reg =
      PickRegisterThatIsAvailableLongest(current, hint_reg, free_until_pos);

  LifetimePosition pos = free_until_pos[reg];

  if (pos <= current->Start()) {
    // All registers are blocked.
    return false;
  }

  if (pos < current->End()) {
    // Register reg is available at the range start but becomes blocked before
    // the range end. Split current before the position where it becomes
    // blocked. Shift the split position to the last gap position. This is to
    // ensure that if a connecting move is needed, that move coincides with the
    // start of the range that it defines. See crbug.com/1182985.
    LifetimePosition gap_pos =
        pos.IsGapPosition() ? pos : pos.FullStart().End();
    if (gap_pos <= current->Start()) return false;
    LiveRange* tail = SplitRangeAt(current, gap_pos);
    AddToUnhandled(tail);

    // Try to allocate preferred register once more.
    if (TryAllocatePreferredReg(current, free_until_pos)) return true;
  }

  // Register reg is available at the range start and is free until the range
  // end.
  DCHECK_GE(pos, current->End());
  TRACE("Assigning free reg %s to live range %d:%d\n", RegisterName(reg),
        current->TopLevel()->vreg(), current->relative_id());
  SetLiveRangeAssignedRegister(current, reg);

  return true;
}

void LinearScanAllocator::AllocateBlockedReg(LiveRange* current,
                                             SpillMode spill_mode) {
  UsePosition* register_use = current->NextRegisterPosition(current->Start());
  if (register_use == nullptr) {
    // There is no use in the current live range that requires a register.
    // We can just spill it.
    LiveRange* begin_spill = nullptr;
    LifetimePosition spill_pos = FindOptimalSpillingPos(
        current, current->Start(), spill_mode, &begin_spill);
    MaybeSpillPreviousRanges(begin_spill, spill_pos, current);
    Spill(current, spill_mode);
    return;
  }

  MachineRepresentation rep = current->representation();

  // use_pos keeps track of positions a register/alias is used at.
  // block_pos keeps track of positions where a register/alias is blocked
  // from.
  base::EmbeddedVector<LifetimePosition, RegisterConfiguration::kMaxRegisters>
      use_pos(LifetimePosition::MaxPosition());
  base::EmbeddedVector<LifetimePosition, RegisterConfiguration::kMaxRegisters>
      block_pos(LifetimePosition::MaxPosition());

  for (LiveRange* range : active_live_ranges()) {
    int cur_reg = range->assigned_register();
    bool is_fixed_or_cant_spill =
        range->TopLevel()->IsFixed() || !range->CanBeSpilled(current->Start());
    if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
      if (is_fixed_or_cant_spill) {
        block_pos[cur_reg] = use_pos[cur_reg] =
            LifetimePosition::GapFromInstructionIndex(0);
      } else {
        DCHECK_NE(LifetimePosition::GapFromInstructionIndex(0),
                  block_pos[cur_reg]);
        use_pos[cur_reg] =
            range->NextLifetimePositionRegisterIsBeneficial(current->Start());
      }
    } else {
      int alias_base_index = -1;
      int aliases = data()->config()->GetAliases(
          range->representation(), cur_reg, rep, &alias_base_index);
      DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
      while (aliases--) {
        int aliased_reg = alias_base_index + aliases;
        if (is_fixed_or_cant_spill) {
          block_pos[aliased_reg] = use_pos[aliased_reg] =
              LifetimePosition::GapFromInstructionIndex(0);
        } else {
          use_pos[aliased_reg] =
              std::min(block_pos[aliased_reg],
                       range->NextLifetimePositionRegisterIsBeneficial(
                           current->Start()));
        }
      }
    }
  }

  for (int cur_reg = 0; cur_reg < num_registers(); ++cur_reg) {
    SlowDCheckInactiveLiveRangesIsSorted(cur_reg);
    for (LiveRange* range : inactive_live_ranges(cur_reg)) {
      DCHECK(range->End() > current->Start());
      DCHECK_EQ(range->assigned_register(), cur_reg);
      bool is_fixed = range->TopLevel()->IsFixed();

      // Don't perform costly intersections if they are guaranteed to not update
      // block_pos or use_pos.
      // TODO(mtrofin): extend to aliased ranges, too.
      if ((kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing())) {
        DCHECK_LE(use_pos[cur_reg], block_pos[cur_reg]);
        if (block_pos[cur_reg] <= range->NextStart()) break;
        if (!is_fixed && use_pos[cur_reg] <= range->NextStart()) continue;
      }

      LifetimePosition next_intersection = range->FirstIntersection(current);
      if (!next_intersection.IsValid()) continue;

      if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
        if (is_fixed) {
          block_pos[cur_reg] = std::min(block_pos[cur_reg], next_intersection);
          use_pos[cur_reg] = std::min(block_pos[cur_reg], use_pos[cur_reg]);
        } else {
          use_pos[cur_reg] = std::min(use_pos[cur_reg], next_intersection);
        }
      } else {
        int alias_base_index = -1;
        int aliases = data()->config()->GetAliases(
            range->representation(), cur_reg, rep, &alias_base_index);
        DCHECK(aliases > 0 || (aliases == 0 && alias_base_index == -1));
        while (aliases--) {
          int aliased_reg = alias_base_index + aliases;
          if (is_fixed) {
            block_pos[aliased_reg] =
                std::min(block_pos[aliased_reg], next_intersection);
            use_pos[aliased_reg] =
                std::min(block_pos[aliased_reg], use_pos[aliased_reg]);
          } else {
            use_pos[aliased_reg] =
                std::min(use_pos[aliased_reg], next_intersection);
          }
        }
      }
    }
  }

  // Compute register hint if it exists.
  int hint_reg = kUnassignedRegister;
  current->RegisterFromControlFlow(&hint_reg) ||
      register_use->HintRegister(&hint_reg) ||
      current->RegisterFromBundle(&hint_reg);
  int reg = PickRegisterThatIsAvailableLongest(current, hint_reg, use_pos);

  if (use_pos[reg] < register_use->pos()) {
    // If there is a gap position before the next register use, we can
    // spill until there. The gap position will then fit the fill move.
    if (LifetimePosition::ExistsGapPositionBetween(current->Start(),
                                                   register_use->pos())) {
      SpillBetween(current, current->Start(), register_use->pos(), spill_mode);
      return;
    }
  }

  // When in deferred spilling mode avoid stealing registers beyond the current
  // deferred region. This is required as we otherwise might spill an inactive
  // range with a start outside of deferred code and that would not be reloaded.
  LifetimePosition new_end = current->End();
  if (spill_mode == SpillMode::kSpillDeferred) {
    InstructionBlock* deferred_block =
        code()->GetInstructionBlock(current->Start().ToInstructionIndex());
    new_end =
        std::min(new_end, LifetimePosition::GapFromInstructionIndex(
                              LastDeferredInstructionIndex(deferred_block)));
  }

  // We couldn't spill until the next register use. Split before the register
  // is blocked, if applicable.
  if (block_pos[reg] < new_end) {
    // Register becomes blocked before the current range end. Split before that
    // position.
    new_end = block_pos[reg].Start();
  }

  // If there is no register available at all, we can only spill this range.
  // Happens for instance on entry to deferred code where registers might
  // become blocked yet we aim to reload ranges.
  if (new_end == current->Start()) {
    SpillBetween(current, new_end, register_use->pos(), spill_mode);
    return;
  }

  // Split at the new end if we found one.
  if (new_end != current->End()) {
    LiveRange* tail = SplitBetween(current, current->Start(), new_end);
    AddToUnhandled(tail);
  }

  // Register reg is not blocked for the whole range.
  DCHECK(block_pos[reg] >= current->End());
  TRACE("Assigning blocked reg %s to live range %d:%d\n", RegisterName(reg),
        current->TopLevel()->vreg(), current->relative_id());
  SetLiveRangeAssignedRegister(current, reg);

  // This register was not free. Thus we need to find and spill
  // parts of active and inactive live regions that use the same register
  // at the same lifetime positions as current.
  SplitAndSpillIntersecting(current, spill_mode);
}

void LinearScanAllocator::SplitAndSpillIntersecting(LiveRange* current,
                                                    SpillMode spill_mode) {
  DCHECK(current->HasRegisterAssigned());
  int reg = current->assigned_register();
  LifetimePosition split_pos = current->Start();
  for (auto it = active_live_ranges().begin();
       it != active_live_ranges().end();) {
    LiveRange* range = *it;
    if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
      if (range->assigned_register() != reg) {
        ++it;
        continue;
      }
    } else {
      if (!data()->config()->AreAliases(current->representation(), reg,
                                        range->representation(),
                                        range->assigned_register())) {
        ++it;
        continue;
      }
    }

    UsePosition* next_pos = range->NextRegisterPosition(current->Start());
    LiveRange* begin_spill = nullptr;
    LifetimePosition spill_pos =
        FindOptimalSpillingPos(range, split_pos, spill_mode, &begin_spill);
    MaybeSpillPreviousRanges(begin_spill, spill_pos, range);
    if (next_pos == nullptr) {
      SpillAfter(range, spill_pos, spill_mode);
    } else {
      // When spilling between spill_pos and next_pos ensure that the range
      // remains spilled at least until the start of the current live range.
      // This guarantees that we will not introduce new unhandled ranges that
      // start before the current range as this violates allocation invariants
      // and will lead to an inconsistent state of active and inactive
      // live-ranges: ranges are allocated in order of their start positions,
      // ranges are retired from active/inactive when the start of the
      // current live-range is larger than their end.
      DCHECK(LifetimePosition::ExistsGapPositionBetween(current->Start(),
                                                        next_pos->pos()));
      SpillBetweenUntil(range, spill_pos, current->Start(), next_pos->pos(),
                        spill_mode);
    }
    it = ActiveToHandled(it);
  }

  for (int cur_reg = 0; cur_reg < num_registers(); ++cur_reg) {
    if (kFPAliasing != AliasingKind::kCombine || !check_fp_aliasing()) {
      if (cur_reg != reg) continue;
    }
    SlowDCheckInactiveLiveRangesIsSorted(cur_reg);
    for (auto it = inactive_live_ranges(cur_reg).begin();
         it != inactive_live_ranges(cur_reg).end();) {
      LiveRange* range = *it;
      if (kFPAliasing == AliasingKind::kCombine && check_fp_aliasing() &&
          !data()->config()->AreAliases(current->representation(), reg,
                                        range->representation(), cur_reg)) {
        ++it;
        continue;
      }
      DCHECK(range->End() > current->Start());
      if (range->TopLevel()->IsFixed()) {
        ++it;
        continue;
      }

      LifetimePosition next_intersection = range->FirstIntersection(current);
      if (next_intersection.IsValid()) {
        UsePosition* next_pos = range->NextRegisterPosition(current->Start());
        if (next_pos == nullptr) {
          SpillAfter(range, split_pos, spill_mode);
        } else {
          next_intersection = std::min(next_intersection, next_pos->pos());
          SpillBetween(range, split_pos, next_intersection, spill_mode);
        }
        it = InactiveToHandled(it);
      } else {
        ++it;
      }
    }
  }
}

bool LinearScanAllocator::TryReuseSpillForPhi(TopLevelLiveRange* range) {
  if (!range->is_phi()) return false;

  DCHECK(!range->HasSpillOperand());
  // Check how many operands belong to the same bundle as the output.
  LiveRangeBundle* out_bundle = range->get_bundle();
  RegisterAllocationData::PhiMapValue* phi_map_value =
      data()->GetPhiMapValueFor(range);
  const PhiInstruction* phi = phi_map_value->phi();
  const InstructionBlock* block = phi_map_value->block();
  // Count the number of spilled operands.
  size_t spilled_count = 0;
  for (size_t i = 0; i < phi->operands().size(); i++) {
    int op = phi->operands()[i];
    TopLevelLiveRange* op_range = data()->GetLiveRangeFor(op);
    if (!op_range->HasSpillRange() || op_range->get_bundle() != out_bundle)
      continue;
    const InstructionBlock* pred =
        code()->InstructionBlockAt(block->predecessors()[i]);
    LifetimePosition pred_end =
        LifetimePosition::InstructionFromInstructionIndex(
            pred->last_instruction_index());
    LiveRange* op_range_child = op_range->GetChildCovers(pred_end);
    if (op_range_child != nullptr && op_range_child->spilled()) {
      spilled_count++;
    }
  }

  // Only continue if more than half of the operands are spilled to the same
  // slot (because part of same bundle).
  if (spilled_count * 2 <= phi->operands().size()) {
    return false;
  }

  // If the range does not need register soon, spill it to the merged
  // spill range.
  LifetimePosition next_pos = range->Start();
  if (next_pos.IsGapPosition()) next_pos = next_pos.NextStart();
  UsePosition* pos = range->NextUsePositionRegisterIsBeneficial(next_pos);
  if (pos == nullptr) {
    Spill(range, SpillMode::kSpillAtDefinition);
    return true;
  } else if (pos->pos() > range->Start().NextStart()) {
    SpillBetween(range, range->Start(), pos->pos(),
                 SpillMode::kSpillAtDefinition);
    return true;
  }
  return false;
}

void LinearScanAllocator::SpillAfter(LiveRange* range, LifetimePosition pos,
                                     SpillMode spill_mode) {
  LiveRange* second_part = SplitRangeAt(range, pos);
  Spill(second_part, spill_mode);
}

void LinearScanAllocator::SpillBetween(LiveRange* range, LifetimePosition start,
                                       LifetimePosition end,
                                       SpillMode spill_mode) {
  SpillBetweenUntil(range, start, start, end, spill_mode);
}

void LinearScanAllocator::SpillBetweenUntil(LiveRange* range,
                                            LifetimePosition start,
                                            LifetimePosition until,
                                            LifetimePosition end,
                                            SpillMode spill_mode) {
  CHECK(start < end);
  LiveRange* second_part = SplitRangeAt(range, start);

  if (second_part->Start() < end) {
    // The split result intersects with [start, end[.
    // Split it at position between ]start+1, end[, spill the middle part
    // and put the rest to unhandled.

    // Make sure that the third part always starts after the start of the
    // second part, as that likely is the current position of the register
    // allocator and we cannot add ranges to unhandled that start before
    // the current position.
    LifetimePosition split_start = std::max(second_part->Start().End(), until);

    // If end is an actual use (which it typically is) we have to split
    // so that there is a gap before so that we have space for moving the
    // value into its position.
    // However, if we have no choice, split right where asked.
    LifetimePosition third_part_end =
        std::max(split_start, end.PrevStart().End());
    // Instead of spliting right after or even before the block boundary,
    // split on the boumndary to avoid extra moves.
    if (data()->IsBlockBoundary(end.Start())) {
      third_part_end = std::max(split_start, end.Start());
    }

    LiveRange* third_part =
        SplitBetween(second_part, split_start, third_part_end);
    if (GetInstructionBlock(data()->code(), second_part->Start())
            ->IsDeferred()) {
      // Try to use the same register as before.
      TRACE("Setting control flow hint for %d:%d to %s\n",
            third_part->TopLevel()->vreg(), third_part->relative_id(),
            RegisterName(range->controlflow_hint()));
      third_part->set_controlflow_hint(range->controlflow_hint());
    }

    AddToUnhandled(third_part);
    // This can happen, even if we checked for start < end above, as we fiddle
    // with the end location. However, we are guaranteed to be after or at
    // until, so this is fine.
    if (third_part != second_part) {
      Spill(second_part, spill_mode);
    }
  } else {
    // The split result does not intersect with [start, end[.
    // Nothing to spill. Just put it to unhandled as whole.
    AddToUnhandled(second_part);
  }
}

OperandAssigner::OperandAssigner(RegisterAllocationData* data) : data_(data) {}

void OperandAssigner::DecideSpillingMode() {
  for (auto range : data()->live_ranges()) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    DCHECK_NOT_NULL(range);
    if (range->IsSpilledOnlyInDeferredBlocks(data())) {
      // If the range is spilled only in deferred blocks and starts in
      // a non-deferred block, we transition its representation here so
      // that the LiveRangeConnector processes them correctly. If,
      // however, they start in a deferred block, we uograde them to
      // spill at definition, as that definition is in a deferred block
      // anyway. While this is an optimization, the code in LiveRangeConnector
      // relies on it!
      if (GetInstructionBlock(data()->code(), range->Start())->IsDeferred()) {
        TRACE("Live range %d is spilled and alive in deferred code only\n",
              range->vreg());
        range->TransitionRangeToSpillAtDefinition();
      } else {
        TRACE("Live range %d is spilled deferred code only but alive outside\n",
              range->vreg());
        range->TransitionRangeToDeferredSpill(data()->allocation_zone());
      }
    }
  }
}

void OperandAssigner::AssignSpillSlots() {
  ZoneVector<SpillRange*> spill_ranges(data()->allocation_zone());
  for (const TopLevelLiveRange* range : data()->live_ranges()) {
    DCHECK_NOT_NULL(range);
    if (range->HasSpillRange()) {
      DCHECK_NOT_NULL(range->GetSpillRange());
      spill_ranges.push_back(range->GetSpillRange());
    }
  }
  // At this point, the `SpillRange`s for all `TopLevelLiveRange`s should be
  // unique, since none have been merged yet.
  DCHECK_EQ(spill_ranges.size(),
            std::set(spill_ranges.begin(), spill_ranges.end()).size());

  // Merge all `SpillRange`s that belong to the same `LiveRangeBundle`.
  for (const TopLevelLiveRange* range : data()->live_ranges()) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    DCHECK_NOT_NULL(range);
    if (range->get_bundle() != nullptr) {
      range->get_bundle()->MergeSpillRangesAndClear();
    }
  }

  // Now merge *all* disjoint, non-empty `SpillRange`s.
  // Formerly, this merging was O(n^2) in the number of `SpillRange`s, which
  // then dominated compile time (>40%) for some pathological cases,
  // e.g., https://crbug.com/v8/14133.
  // Now, we allow only `kMaxRetries` unsuccessful merges with directly
  // following `SpillRange`s. After each `kMaxRetries`, we exponentially
  // increase the stride, which limits the inner loop to O(log n) and thus
  // the overall merging to O(n * log n).

  // The merging above may have left some `SpillRange`s empty, remove them.
  SpillRange** end_nonempty =
      std::remove_if(spill_ranges.begin(), spill_ranges.end(),
                     [](const SpillRange* range) { return range->IsEmpty(); });
  for (SpillRange** range_it = spill_ranges.begin(); range_it < end_nonempty;
       ++range_it) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    SpillRange* range = *range_it;
    DCHECK(!range->IsEmpty());
    constexpr size_t kMaxRetries = 1000;
    size_t retries = kMaxRetries;
    size_t stride = 1;
    for (SpillRange** other_it = range_it + 1; other_it < end_nonempty;
         other_it += stride) {
      SpillRange* other = *other_it;
      DCHECK(!other->IsEmpty());
      if (range->TryMerge(other)) {
        DCHECK(other->IsEmpty());
        std::iter_swap(other_it, --end_nonempty);
      } else if (--retries == 0) {
        retries = kMaxRetries;
        stride *= 2;
      }
    }
  }
  spill_ranges.erase(end_nonempty, spill_ranges.end());

  // Allocate slots for the merged spill ranges.
  for (SpillRange* range : spill_ranges) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    DCHECK(!range->IsEmpty());
    if (!range->HasSlot()) {
      // Allocate a new operand referring to the spill slot, aligned to the
      // operand size.
      int width = range->byte_width();
      int index = data()->frame()->AllocateSpillSlot(width, width);
      range->set_assigned_slot(index);
    }
  }
}

void OperandAssigner::CommitAssignment() {
  const size_t live_ranges_size = data()->live_ranges().size();
  for (TopLevelLiveRange* top_range : data()->live_ranges()) {
    data()->tick_counter()->TickAndMaybeEnterSafepoint();
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(top_range);
    if (top_range->IsEmpty()) continue;
    InstructionOperand spill_operand;
    if (top_range->HasSpillOperand()) {
      auto it = data()->slot_for_const_range().find(top_range);
      if (it != data()->slot_for_const_range().end()) {
        spill_operand = *it->second;
      } else {
        spill_operand = *top_range->GetSpillOperand();
      }
    } else if (top_range->HasSpillRange()) {
      spill_operand = top_range->GetSpillRangeOperand();
    }
    if (top_range->is_phi()) {
      data()->GetPhiMapValueFor(top_range)->CommitAssignment(
          top_range->GetAssignedOperand());
    }
    for (LiveRange* range = top_range; range != nullptr;
         range = range->next()) {
      InstructionOperand assigned = range->GetAssignedOperand();
      DCHECK(!assigned.IsUnallocated());
      range->ConvertUsesToOperand(assigned, spill_operand);
    }

    if (!spill_operand.IsInvalid()) {
      // If this top level range has a child spilled in a deferred block, we use
      // the range and control flow connection mechanism instead of spilling at
      // definition. Refer to the ConnectLiveRanges and ResolveControlFlow
      // phases. Normally, when we spill at definition, we do not insert a
      // connecting move when a successor child range is spilled - because the
      // spilled range picks up its value from the slot which was assigned at
      // definition. For ranges that are determined to spill only in deferred
      // blocks, we let ConnectLiveRanges and ResolveControlFlow find the blocks
      // where a spill operand is expected, and then finalize by inserting the
      // spills in the deferred blocks dominators.
      if (!top_range->IsSpilledOnlyInDeferredBlocks(data()) &&
          !top_range->HasGeneralSpillRange()) {
        // Spill at definition if the range isn't spilled in a way that will be
        // handled later.
        top_range->FilterSpillMoves(data(), spill_operand);
        top_range->CommitSpillMoves(data(), spill_operand);
      }
    }
  }
}

ReferenceMapPopulator::ReferenceMapPopulator(RegisterAllocationData* data)
    : data_(data) {}

bool ReferenceMapPopulator::SafePointsAreInOrder() const {
  int safe_point = 0;
  for (ReferenceMap* map : *data()->code()->reference_maps()) {
    if (safe_point > map->instruction_position()) return false;
    safe_point = map->instruction_position();
  }
  return true;
}

void ReferenceMapPopulator::PopulateReferenceMaps() {
  DCHECK(SafePointsAreInOrder());
  // Map all delayed references.
  for (RegisterAllocationData::DelayedReference& delayed_reference :
       data()->delayed_references()) {
    delayed_reference.map->RecordReference(
        AllocatedOperand::cast(*delayed_reference.operand));
  }
  // Iterate over all safe point positions and record a pointer
  // for all spilled live ranges at this point.
  int last_range_start = 0;
  const ReferenceMaps* reference_maps = data()->code()->reference_maps();
  ReferenceMaps::const_iterator first_it = reference_maps->begin();
  const size_t live_ranges_size = data()->live_ranges().size();
  // Select subset of `TopLevelLiveRange`s to process, sort them by their start.
  ZoneVector<TopLevelLiveRange*> candidate_ranges(data()->allocation_zone());
  candidate_ranges.reserve(data()->live_ranges().size());
  for (TopLevelLiveRange* range : data()->live_ranges()) {
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(range);
    // Skip non-reference values.
    if (!data()->code()->IsReference(range->vreg())) continue;
    // Skip empty live ranges.
    if (range->IsEmpty()) continue;
    if (range->has_preassigned_slot()) continue;
    candidate_ranges.push_back(range);
  }
  std::sort(candidate_ranges.begin(), candidate_ranges.end(),
            LiveRangeOrdering());
  for (TopLevelLiveRange* range : candidate_ranges) {
    // Find the extent of the range and its children.
    int start = range->Start().ToInstructionIndex();
    int end = range->Children().back()->End().ToInstructionIndex();

    // Ranges should be sorted, so that the first reference map in the current
    // live range has to be after {first_it}.
    DCHECK_LE(last_range_start, start);
    last_range_start = start;
    USE(last_range_start);

    // Step across all the safe points that are before the start of this range,
    // recording how far we step in order to save doing this for the next range.
    for (; first_it != reference_maps->end(); ++first_it) {
      ReferenceMap* map = *first_it;
      if (map->instruction_position() >= start) break;
    }

    InstructionOperand spill_operand;
    if (((range->HasSpillOperand() &&
          !range->GetSpillOperand()->IsConstant()) ||
         range->HasSpillRange())) {
      if (range->HasSpillOperand()) {
        spill_operand = *range->GetSpillOperand();
      } else {
        spill_operand = range->GetSpillRangeOperand();
      }
      DCHECK(spill_operand.IsStackSlot());
      DCHECK(CanBeTaggedOrCompressedPointer(
          AllocatedOperand::cast(spill_operand).representation()));
    }

    LiveRange* cur = nullptr;
    // Step through the safe points to see whether they are in the range.
    for (auto it = first_it; it != reference_maps->end(); ++it) {
      ReferenceMap* map = *it;
      int safe_point = map->instruction_position();

      // The safe points are sorted so we can stop searching here.
      if (safe_point - 1 > end) break;

      // Advance to the next active range that covers the current
      // safe point position.
      LifetimePosition safe_point_pos =
          LifetimePosition::InstructionFromInstructionIndex(safe_point);

      // Search for the child range (cur) that covers safe_point_pos. If we
      // don't find it before the children pass safe_point_pos, keep cur at
      // the last child, because the next safe_point_pos may be covered by cur.
      // This may happen if cur has more than one interval, and the current
      // safe_point_pos is in between intervals.
      // For that reason, cur may be at most the last child.
      // Use binary search for the first iteration, then linear search after.
      bool found = false;
      if (cur == nullptr) {
        cur = range->GetChildCovers(safe_point_pos);
        found = cur != nullptr;
      } else {
        while (!found) {
          if (cur->Covers(safe_point_pos)) {
            found = true;
          } else {
            LiveRange* next = cur->next();
            if (next == nullptr || next->Start() > safe_point_pos) {
              break;
            }
            cur = next;
          }
        }
      }

      if (!found) {
        continue;
      }

      // Check if the live range is spilled and the safe point is after
      // the spill position.
      int spill_index = range->IsSpilledOnlyInDeferredBlocks(data()) ||
                                range->LateSpillingSelected()
                            ? cur->Start().ToInstructionIndex()
                            : range->spill_start_index();

      if (!spill_operand.IsInvalid() && safe_point >= spill_index) {
        TRACE("Pointer for range %d (spilled at %d) at safe point %d\n",
              range->vreg(), spill_index, safe_point);
        map->RecordReference(AllocatedOperand::cast(spill_operand));
      }

      if (!cur->spilled()) {
        TRACE(
            "Pointer in register for range %d:%d (start at %d) "
            "at safe point %d\n",
            range->vreg(), cur->relative_id(), cur->Start().value(),
            safe_point);
        InstructionOperand operand = cur->GetAssignedOperand();
        DCHECK(!operand.IsStackSlot());
        DCHECK(CanBeTaggedOrCompressedPointer(
            AllocatedOperand::cast(operand).representation()));
        map->RecordReference(AllocatedOperand::cast(operand));
      }
    }
  }
}

LiveRangeConnector::LiveRangeConnector(RegisterAllocationData* data)
    : data_(data) {}

bool LiveRangeConnector::CanEagerlyResolveControlFlow(
    const InstructionBlock* block) const {
  if (block->PredecessorCount() != 1) return false;
  return block->predecessors()[0].IsNext(block->rpo_number());
}

void LiveRangeConnector::ResolveControlFlow(Zone* local_zone) {
  ZoneVector<SparseBitVector*>& live_in_sets = data()->live_in_sets();
  for (const InstructionBlock* block : code()->instruction_blocks()) {
    if (CanEagerlyResolveControlFlow(block)) continue;
    SparseBitVector* live = live_in_sets[block->rpo_number().ToInt()];
    for (int vreg : *live) {
      data()->tick_counter()->TickAndMaybeEnterSafepoint();
      TopLevelLiveRange* live_range = data()->live_ranges()[vreg];
      LifetimePosition cur_start = LifetimePosition::GapFromInstructionIndex(
          block->first_instruction_index());
      LiveRange* cur_range = live_range->GetChildCovers(cur_start);
      DCHECK_NOT_NULL(cur_range);
      if (cur_range->spilled()) continue;

      for (const RpoNumber& pred : block->predecessors()) {
        // Find ranges that may need to be connected.
        const InstructionBlock* pred_block = code()->InstructionBlockAt(pred);
        LifetimePosition pred_end =
            LifetimePosition::InstructionFromInstructionIndex(
                pred_block->last_instruction_index());
        // We don't need to perform the O(log n) search if we already know it
        // will be the same range.
        if (cur_range->CanCover(pred_end)) continue;
        LiveRange* pred_range = live_range->GetChildCovers(pred_end);
        // This search should always succeed because the `vreg` associated to
        // this `live_range` must be live out in all predecessor blocks.
        DCHECK_NOT_NULL(pred_range);
        // Since the `cur_range` did not cover `pred_end` earlier, the found
        // `pred_range` must be different.
        DCHECK_NE(cur_range, pred_range);

        InstructionOperand pred_op = pred_range->GetAssignedOperand();
        InstructionOperand cur_op = cur_range->GetAssignedOperand();
        if (pred_op.Equals(cur_op)) continue;

        if (!pred_op.IsAnyRegister() && cur_op.IsAnyRegister()) {
          // We're doing a reload.
          // We don't need to, if:
          // 1) there's no register use in this block, and
          // 2) the range ends before the block does, and
          // 3) we don't have a successor, or the successor is spilled.
          LifetimePosition block_start =
              LifetimePosition::GapFromInstructionIndex(block->code_start());
          LifetimePosition block_end =
              LifetimePosition::GapFromInstructionIndex(block->code_end());
          // Note that this is not the successor if we have control flow!
          // However, in the following condition, we only refer to it if it
          // begins in the current block, in which case we can safely declare it
          // to be the successor.
          const LiveRange* successor = cur_range->next();
          if (cur_range->End() < block_end &&
              (successor == nullptr || successor->spilled())) {
            // verify point 1: no register use. We can go to the end of the
            // range, since it's all within the block.

            bool uses_reg = false;
            for (UsePosition* const* use_pos_it =
                     cur_range->NextUsePosition(block_start);
                 use_pos_it != cur_range->positions().end(); ++use_pos_it) {
              if ((*use_pos_it)->operand()->IsAnyRegister()) {
                uses_reg = true;
                break;
              }
            }
            if (!uses_reg) continue;
          }
          if (cur_range->TopLevel()->IsSpilledOnlyInDeferredBlocks(data()) &&
              pred_block->IsDeferred()) {
            // The spill location should be defined in pred_block, so add
            // pred_block to the list of blocks requiring a spill operand.
            TRACE("Adding B%d to list of spill blocks for %d\n",
                  pred_block->rpo_number().ToInt(),
                  cur_range->TopLevel()->vreg());
            cur_range->TopLevel()
                ->GetListOfBlocksRequiringSpillOperands(data())
                ->Add(pred_block->rpo_number().ToInt());
          }
        }
        int move_loc = ResolveControlFlow(block, cur_op, pred_block, pred_op);
        USE(move_loc);
        DCHECK_IMPLIES(
            cur_range->TopLevel()->IsSpilledOnlyInDeferredBlocks(data()) &&
                !(pred_op.IsAnyRegister() && cur_op.IsAnyRegister()) &&
                move_loc != -1,
            code()->GetInstructionBlock(move_loc)->IsDeferred());
      }
    }
  }

  // At this stage, we collected blocks needing a spill operand due to reloads
  // from ConnectRanges and from ResolveControlFlow. Time to commit the spills
  // for deferred blocks. This is a convenient time to commit spills for general
  // spill ranges also, because they need to use the LiveRangeFinder.
  const size_t live_ranges_size = data()->live_ranges().size();
  SpillPlacer spill_placer(data(), local_zone);
  for (TopLevelLiveRange* top : data()->live_ranges()) {
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(top);
    if (top->IsEmpty()) continue;
    if (top->IsSpilledOnlyInDeferredBlocks(data())) {
      CommitSpillsInDeferredBlocks(top, local_zone);
    } else if (top->HasGeneralSpillRange()) {
      spill_placer.Add(top);
    }
  }
}

int LiveRangeConnector::ResolveControlFlow(const InstructionBlock* block,
                                           const InstructionOperand& cur_op,
                                           const InstructionBlock* pred,
                                           const InstructionOperand& pred_op) {
  DCHECK(!pred_op.Equals(cur_op));
  int gap_index;
  Instruction::GapPosition position;
  if (block->PredecessorCount() == 1) {
    gap_index = block->first_instruction_index();
    position = Instruction::START;
  } else {
    Instruction* last = code()->InstructionAt(pred->last_instruction_index());
    // The connecting move might invalidate uses of the destination operand in
    // the deoptimization call. See crbug.com/v8/12218. Omitting the move is
    // safe since the deopt call exits the current code.
    if (last->IsDeoptimizeCall()) {
      return -1;
    }
    // In every other case the last instruction should not participate in
    // register allocation, or it could interfere with the connecting move.
    for (size_t i = 0; i < last->InputCount(); ++i) {
      DCHECK(last->InputAt(i)->IsImmediate());
    }
    DCHECK_EQ(1, pred->SuccessorCount());
    DCHECK(!code()
                ->InstructionAt(pred->last_instruction_index())
                ->HasReferenceMap());
    gap_index = pred->last_instruction_index();
    position = Instruction::END;
  }
  data()->AddGapMove(gap_index, position, pred_op, cur_op);
  return gap_index;
}

void LiveRangeConnector::ConnectRanges(Zone* local_zone) {
  DelayedInsertionMap delayed_insertion_map(local_zone);
  const size_t live_ranges_size = data()->live_ranges().size();
  for (TopLevelLiveRange* top_range : data()->live_ranges()) {
    CHECK_EQ(live_ranges_size,
             data()->live_ranges().size());  // TODO(neis): crbug.com/831822
    DCHECK_NOT_NULL(top_range);
    bool connect_spilled = top_range->IsSpilledOnlyInDeferredBlocks(data());
    LiveRange* first_range = top_range;
    for (LiveRange *second_range = first_range->next(); second_range != nullptr;
         first_range = second_range, second_range = second_range->next()) {
      LifetimePosition pos = second_range->Start();
      // Add gap move if the two live ranges touch and there is no block
      // boundary.
      if (second_range->spilled()) continue;
      if (first_range->End() != pos) continue;
      if (data()->IsBlockBoundary(pos) &&
          !CanEagerlyResolveControlFlow(GetInstructionBlock(code(), pos))) {
        continue;
      }
      InstructionOperand prev_operand = first_range->GetAssignedOperand();
      InstructionOperand cur_operand = second_range->GetAssignedOperand();
      if (prev_operand.Equals(cur_operand)) continue;
      bool delay_insertion = false;
      Instruction::GapPosition gap_pos;
      int gap_index = pos.ToInstructionIndex();
      if (connect_spilled && !prev_operand.IsAnyRegister() &&
          cur_operand.IsAnyRegister()) {
        const InstructionBlock* block = code()->GetInstructionBlock(gap_index);
        DCHECK(block->IsDeferred());
        // Performing a reload in this block, meaning the spill operand must
        // be defined here.
        top_range->GetListOfBlocksRequiringSpillOperands(data())->Add(
            block->rpo_number().ToInt());
      }

      if (pos.IsGapPosition()) {
        gap_pos = pos.IsStart() ? Instruction::START : Instruction::END;
      } else {
        if (pos.IsStart()) {
          delay_insertion = true;
        } else {
          gap_index++;
        }
        gap_pos = delay_insertion ? Instruction::END : Instruction::START;
      }
      // Reloads or spills for spilled in deferred blocks ranges must happen
      // only in deferred blocks.
      DCHECK_IMPLIES(connect_spilled && !(prev_operand.IsAnyRegister() &&
                                          cur_operand.IsAnyRegister()),
                     code()->GetInstructionBlock(gap_index)->IsDeferred());

      ParallelMove* move =
          code()->InstructionAt(gap_index)->GetOrCreateParallelMove(
              gap_pos, code_zone());
      if (!delay_insertion) {
        move->AddMove(prev_operand, cur_operand);
      } else {
        delayed_insertion_map.insert(
            std::make_pair(std::make_pair(move, prev_operand), cur_operand));
      }
    }
  }
  if (delayed_insertion_map.empty()) return;
  // Insert all the moves which should occur after the stored move.
  ZoneVector<MoveOperands*> to_insert(local_zone);
  ZoneVector<MoveOperands*> to_eliminate(local_zone);
  to_insert.reserve(4);
  to_eliminate.reserve(4);
  ParallelMove* moves = delayed_insertion_map.begin()->first.first;
  for (auto it = delayed_insertion_map.begin();; ++it) {
    bool done = it == delayed_insertion_map.end();
    if (done || it->first.first != moves) {
      // Commit the MoveOperands for current ParallelMove.
      for (MoveOperands* move : to_eliminate) {
        move->Eliminate();
      }
      for (MoveOperands* move : to_insert) {
        moves->push_back(move);
      }
      if (done) break;
      // Reset state.
      to_eliminate.clear();
      to_insert.clear();
      moves = it->first.first;
    }
    // Gather all MoveOperands for a single ParallelMove.
    MoveOperands* move =
        code_zone()->New<MoveOperands>(it->first.second, it->second);
    moves->PrepareInsertAfter(move, &to_eliminate);
    to_insert.push_back(move);
  }
}

void LiveRangeConnector::CommitSpillsInDeferredBlocks(TopLevelLiveRange* range,
                                                      Zone* temp_zone) {
  DCHECK(range->IsSpilledOnlyInDeferredBlocks(data()));
  DCHECK(!range->spilled());

  InstructionSequence* code = data()->code();
  InstructionOperand spill_operand = range->GetSpillRangeOperand();

  TRACE("Live Range %d will be spilled only in deferred blocks.\n",
        range->vreg());
  // If we have ranges that aren't spilled but require the operand on the stack,
  // make sure we insert the spill.
  for (const LiveRange* child = range; child != nullptr;
       child = child->next()) {
    for (const UsePosition* pos : child->positions()) {
      if (pos->type() != UsePositionType::kRequiresSlot && !child->spilled())
        continue;
      range->AddBlockRequiringSpillOperand(
          code->GetInstructionBlock(pos->pos().ToInstructionIndex())
              ->rpo_number(),
          data());
    }
  }

  ZoneQueue<int> worklist(temp_zone);

  for (int block_id : *range->GetListOfBlocksRequiringSpillOperands(data())) {
    worklist.push(block_id);
  }

  ZoneSet<std::pair<RpoNumber, int>> done_moves(temp_zone);
  // Seek the deferred blocks that dominate locations requiring spill operands,
  // and spill there. We only need to spill at the start of such blocks.
  SparseBitVector done_blocks(temp_zone);
  while (!worklist.empty()) {
    int block_id = worklist.front();
    worklist.pop();
    if (done_blocks.Contains(block_id)) continue;
    done_blocks.Add(block_id);
    InstructionBlock* spill_block =
        code->InstructionBlockAt(RpoNumber::FromInt(block_id));

    for (const RpoNumber& pred : spill_block->predecessors()) {
      const InstructionBlock* pred_block = code->InstructionBlockAt(pred);

      if (pred_block->IsDeferred()) {
        worklist.push(pred_block->rpo_number().ToInt());
      } else {
        LifetimePosition pred_end =
            LifetimePosition::InstructionFromInstructionIndex(
                pred_block->last_instruction_index());

        LiveRange* child_range = range->GetChildCovers(pred_end);
        DCHECK_NOT_NULL(child_range);

        InstructionOperand pred_op = child_range->GetAssignedOperand();

        RpoNumber spill_block_number = spill_block->rpo_number();
        if (done_moves.find(std::make_pair(
                spill_block_number, range->vreg())) == done_moves.end()) {
          TRACE("Spilling deferred spill for range %d at B%d\n", range->vreg(),
                spill_block_number.ToInt());
          data()->AddGapMove(spill_block->first_instruction_index(),
                             Instruction::GapPosition::START, pred_op,
                             spill_operand);
          done_moves.insert(std::make_pair(spill_block_number, range->vreg()));
          spill_block->mark_needs_frame();
        }
      }
    }
  }
}

#undef TRACE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
