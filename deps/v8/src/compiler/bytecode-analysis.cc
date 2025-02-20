// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-analysis.h"

#include <utility>

#include "src/compiler/bytecode-liveness-map.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-array-random-iterator.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/objects-inl.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {
namespace compiler {

using interpreter::Bytecode;
using interpreter::BytecodeOperands;
using interpreter::Bytecodes;
using interpreter::ImplicitRegisterUse;
using interpreter::OperandType;
using interpreter::Register;

BytecodeLoopAssignments::BytecodeLoopAssignments(int parameter_count,
                                                 int register_count, Zone* zone)
    : parameter_count_(parameter_count),
      bit_vector_(
          zone->New<BitVector>(parameter_count + register_count, zone)) {}

void BytecodeLoopAssignments::Add(interpreter::Register r) {
  if (r.is_parameter()) {
    bit_vector_->Add(r.ToParameterIndex());
  } else {
    bit_vector_->Add(parameter_count_ + r.index());
  }
}

void BytecodeLoopAssignments::AddList(interpreter::Register r, uint32_t count) {
  if (r.is_parameter()) {
    for (uint32_t i = 0; i < count; i++) {
      DCHECK(interpreter::Register(r.index() + i).is_parameter());
      bit_vector_->Add(r.ToParameterIndex() + i);
    }
  } else {
    for (uint32_t i = 0; i < count; i++) {
      DCHECK(!interpreter::Register(r.index() + i).is_parameter());
      bit_vector_->Add(parameter_count_ + r.index() + i);
    }
  }
}


void BytecodeLoopAssignments::Union(const BytecodeLoopAssignments& other) {
  bit_vector_->Union(*other.bit_vector_);
}

bool BytecodeLoopAssignments::ContainsParameter(int index) const {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, parameter_count());
  return bit_vector_->Contains(index);
}

bool BytecodeLoopAssignments::ContainsLocal(int index) const {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, local_count());
  return bit_vector_->Contains(parameter_count_ + index);
}

ResumeJumpTarget::ResumeJumpTarget(int suspend_id, int target_offset,
                                   int final_target_offset)
    : suspend_id_(suspend_id),
      target_offset_(target_offset),
      final_target_offset_(final_target_offset) {}

ResumeJumpTarget ResumeJumpTarget::Leaf(int suspend_id, int target_offset) {
  return ResumeJumpTarget(suspend_id, target_offset, target_offset);
}

ResumeJumpTarget ResumeJumpTarget::AtLoopHeader(int loop_header_offset,
                                                const ResumeJumpTarget& next) {
  return ResumeJumpTarget(next.suspend_id(), loop_header_offset,
                          next.target_offset());
}

namespace {

template <Bytecode bytecode, OperandType operand_type, size_t i>
void UpdateInLivenessForOutOperand(
    BytecodeLivenessState* in_liveness,
    const interpreter::BytecodeArrayIterator& iterator) {
  if constexpr (operand_type == OperandType::kRegOut ||
                operand_type == OperandType::kRegInOut) {
    Register r = iterator.GetRegisterOperand(i);
    if (!r.is_parameter()) {
      in_liveness->MarkRegisterDead(r.index());
    }
  } else if constexpr (operand_type == OperandType::kRegOutList) {
    Register r = iterator.GetRegisterOperand(i);
    uint32_t reg_count = iterator.GetRegisterCountOperand(i + 1);
    if (!r.is_parameter()) {
      for (uint32_t j = 0; j < reg_count; ++j) {
        DCHECK(!Register(r.index() + j).is_parameter());
        in_liveness->MarkRegisterDead(r.index() + j);
      }
    }
  } else if constexpr (operand_type == OperandType::kRegOutPair) {
    Register r = iterator.GetRegisterOperand(i);
    if (!r.is_parameter()) {
      DCHECK(!Register(r.index() + 1).is_parameter());
      in_liveness->MarkRegisterDead(r.index());
      in_liveness->MarkRegisterDead(r.index() + 1);
    }
  } else if constexpr (operand_type == OperandType::kRegOutTriple) {
    Register r = iterator.GetRegisterOperand(i);
    if (!r.is_parameter()) {
      DCHECK(!Register(r.index() + 1).is_parameter());
      DCHECK(!Register(r.index() + 2).is_parameter());
      in_liveness->MarkRegisterDead(r.index());
      in_liveness->MarkRegisterDead(r.index() + 1);
      in_liveness->MarkRegisterDead(r.index() + 2);
    }
  } else {
    DCHECK(!Bytecodes::IsRegisterOutputOperandType(operand_type));
  }
}

template <Bytecode bytecode, OperandType operand_type, size_t i>
void UpdateInLivenessForInOperand(
    BytecodeLivenessState* in_liveness,
    const interpreter::BytecodeArrayIterator& iterator) {
  if constexpr (operand_type == OperandType::kReg ||
                operand_type == OperandType::kRegInOut) {
    Register r = iterator.GetRegisterOperand(i);
    if (!r.is_parameter()) {
      in_liveness->MarkRegisterLive(r.index());
    }
  } else if constexpr (operand_type == OperandType::kRegPair) {
    Register r = iterator.GetRegisterOperand(i);
    if (!r.is_parameter()) {
      DCHECK(!Register(r.index() + 1).is_parameter());
      in_liveness->MarkRegisterLive(r.index());
      in_liveness->MarkRegisterLive(r.index() + 1);
    }
  } else if constexpr (operand_type == OperandType::kRegList) {
    Register r = iterator.GetRegisterOperand(i);
    uint32_t reg_count = iterator.GetRegisterCountOperand(i + 1);
    if (!r.is_parameter()) {
      for (uint32_t j = 0; j < reg_count; ++j) {
        DCHECK(!interpreter::Register(r.index() + j).is_parameter());
        in_liveness->MarkRegisterLive(r.index() + j);
      }
    }
  } else {
    DCHECK(!Bytecodes::IsRegisterInputOperandType(operand_type));
  }
}

template <Bytecode bytecode, ImplicitRegisterUse implicit_register_use,
          OperandType... operand_types, size_t... operand_index>
void UpdateInLiveness(BytecodeLivenessState* in_liveness,
                      const interpreter::BytecodeArrayIterator& iterator,
                      std::index_sequence<operand_index...>) {
  // Special case Suspend and Resume to just pass through liveness.
  if constexpr (bytecode == Bytecode::kSuspendGenerator) {
    // The generator object has to be live.
    in_liveness->MarkRegisterLive(iterator.GetRegisterOperand(0).index());
    // Suspend additionally reads and returns the accumulator
    DCHECK(Bytecodes::ReadsAccumulator(bytecode));
    in_liveness->MarkAccumulatorLive();
    return;
  } else if constexpr (bytecode == Bytecode::kResumeGenerator) {
    // The generator object has to be live.
    in_liveness->MarkRegisterLive(iterator.GetRegisterOperand(0).index());
    return;
  }

  // Otherwise, walk all accumulator and register writes and reads.
  if constexpr (BytecodeOperands::WritesAccumulator(implicit_register_use)) {
    in_liveness->MarkAccumulatorDead();
  }
  DCHECK_IMPLIES(BytecodeOperands::ClobbersAccumulator(implicit_register_use),
                 !in_liveness->AccumulatorIsLive());
  (UpdateInLivenessForOutOperand<bytecode, operand_types, operand_index>(
       in_liveness, iterator),
   ...);

  if constexpr (BytecodeOperands::WritesImplicitRegister(
                    implicit_register_use)) {
    in_liveness->MarkRegisterDead(Register::FromShortStar(bytecode).index());
  }

  if constexpr (BytecodeOperands::ReadsAccumulator(implicit_register_use)) {
    in_liveness->MarkAccumulatorLive();
  }
  (UpdateInLivenessForInOperand<bytecode, operand_types, operand_index>(
       in_liveness, iterator),
   ...);
}

template <Bytecode bytecode, ImplicitRegisterUse implicit_register_use,
          OperandType... operand_types>
void UpdateInLiveness(BytecodeLivenessState* in_liveness,
                      const interpreter::BytecodeArrayIterator& iterator) {
  UpdateInLiveness<bytecode, implicit_register_use, operand_types...>(
      in_liveness, iterator,
      std::make_index_sequence<sizeof...(operand_types)>());
}

#ifdef DEBUG
void UpdateInLiveness(Bytecode bytecode, BytecodeLivenessState* in_liveness,
                      const interpreter::BytecodeArrayIterator& iterator) {
  switch (bytecode) {
#define BYTECODE_UPDATE_IN_LIVENESS(Name, ...)                           \
  case Bytecode::k##Name:                                                \
    return UpdateInLiveness<Bytecode::k##Name, __VA_ARGS__>(in_liveness, \
                                                            iterator);
    BYTECODE_LIST(BYTECODE_UPDATE_IN_LIVENESS, BYTECODE_UPDATE_IN_LIVENESS)
#undef BYTECODE_UPDATE_IN_LIVENESS
  }
}
#endif  // DEBUG

template <bool IsFirstUpdate = false>
void EnsureOutLivenessIsNotAlias(
    BytecodeLiveness& liveness,
    BytecodeLivenessState* next_bytecode_in_liveness, Zone* zone) {
  if (!IsFirstUpdate) {
    // We should have copied the next bytecode's in liveness already in the
    // first pass, so on subsequent passes this should already not be an alias.
    DCHECK_NE(liveness.out, next_bytecode_in_liveness);
    return;
  }
  if (liveness.out == next_bytecode_in_liveness) {
    // If the out-liveness is aliasing the next bytecode's in-liveness,
    // reallocate it and copy the data to the newly allocated state.
    liveness.out =
        zone->New<BytecodeLivenessState>(*next_bytecode_in_liveness, zone);
  }
}

template <bool IsFirstUpdate, Bytecode bytecode>
void UpdateOutLiveness(BytecodeLiveness& liveness,
                       BytecodeLivenessState* next_bytecode_in_liveness,
                       const interpreter::BytecodeArrayIterator& iterator,
                       DirectHandle<BytecodeArray> bytecode_array,
                       const BytecodeLivenessMap& liveness_map, Zone* zone) {
  // On subsequent updates, only update out-liveness manually if it isn't
  // already aliasing the next bytecode's in-liveness.
  if (!IsFirstUpdate && liveness.out == next_bytecode_in_liveness) return;

  // Special case Suspend and Resume to just pass through liveness.
  if (bytecode == Bytecode::kSuspendGenerator ||
      bytecode == Bytecode::kResumeGenerator) {
    DCHECK_NOT_NULL(next_bytecode_in_liveness);
    if (IsFirstUpdate) {
      liveness.out = next_bytecode_in_liveness;
    } else {
      liveness.out->Union(*next_bytecode_in_liveness);
    }
    return;
  }

  // Special case SwitchOnGeneratorState to ignore resume liveness, since that's
  // a pass through. Instead, just consider the fallthrough live, plus the
  // generator register itself for the resumes.
  if (bytecode == Bytecode::kSwitchOnGeneratorState) {
    DCHECK_NOT_NULL(next_bytecode_in_liveness);
    if (IsFirstUpdate) {
      // The generator register won't be live in the fallthrough, so copy the
      // liveness and make it live here.
      int generator_reg_index = iterator.GetRegisterOperand(0).index();
      DCHECK(!next_bytecode_in_liveness->RegisterIsLive(generator_reg_index));
      liveness.out =
          zone->New<BytecodeLivenessState>(*next_bytecode_in_liveness, zone);
      liveness.out->MarkRegisterLive(generator_reg_index);
    } else {
      liveness.out->Union(*next_bytecode_in_liveness);
    }
    return;
  }

  // Update from next bytecode (unless there isn't one or this is an
  // unconditional jump).
  if (next_bytecode_in_liveness != nullptr &&
      !Bytecodes::IsUnconditionalJump(bytecode) &&
      !Bytecodes::Returns(bytecode) &&
      !Bytecodes::UnconditionallyThrows(bytecode)) {
    if (IsFirstUpdate) {
      // On first update, we can assume that this out-liveness is the same as
      // the next liveness, and can directly alias it -- we'll allocate a new
      // one using EnsureOutLivenessIsNotAlias if it needs to be mutated.
      DCHECK_NULL(liveness.out);
      liveness.out = next_bytecode_in_liveness;
    } else {
      liveness.out->Union(*next_bytecode_in_liveness);
    }
  } else if (IsFirstUpdate) {
    // Otherwise, on the first allocation we need to make sure that there is an
    // allocated out liveness.
    DCHECK_NULL(liveness.out);
    liveness.out = zone->New<BytecodeLivenessState>(
        bytecode_array->register_count(), zone);
  }

  DCHECK_NOT_NULL(liveness.out);

  // Update from jump target (if any). Skip loops, we update these manually in
  // the liveness iterations.
  if (Bytecodes::IsForwardJump(bytecode)) {
    int target_offset = iterator.GetJumpTargetOffset();
    EnsureOutLivenessIsNotAlias<IsFirstUpdate>(liveness,
                                               next_bytecode_in_liveness, zone);
    liveness.out->Union(*liveness_map.GetInLiveness(target_offset));
  } else if (Bytecodes::IsSwitch(bytecode)) {
    EnsureOutLivenessIsNotAlias<IsFirstUpdate>(liveness,
                                               next_bytecode_in_liveness, zone);
    for (interpreter::JumpTableTargetOffset entry :
         iterator.GetJumpTableTargetOffsets()) {
      liveness.out->Union(*liveness_map.GetInLiveness(entry.target_offset));
    }
  }

  // Update from exception handler (if any).
  if (!interpreter::Bytecodes::IsWithoutExternalSideEffects(bytecode)) {
    // TODO(leszeks): We should look up this range only once per entry.
    HandlerTable table(*bytecode_array);
    int handler_index =
        table.LookupHandlerIndexForRange(iterator.current_offset());

    if (handler_index != HandlerTable::kNoHandlerFound) {
      EnsureOutLivenessIsNotAlias<IsFirstUpdate>(
          liveness, next_bytecode_in_liveness, zone);
      bool was_accumulator_live = liveness.out->AccumulatorIsLive();
      liveness.out->Union(
          *liveness_map.GetInLiveness(table.GetRangeHandler(handler_index)));
      liveness.out->MarkRegisterLive(table.GetRangeData(handler_index));
      if (!was_accumulator_live) {
        // The accumulator is reset to the exception on entry into a handler,
        // and so shouldn't be considered live coming out of this bytecode just
        // because it's live coming into the handler. So, kill the accumulator
        // if the handler is the only thing that made it live.
        liveness.out->MarkAccumulatorDead();

        // TODO(leszeks): Ideally the accumulator wouldn't be considered live at
        // the start of the handler, but looking up if the current bytecode is
        // the start of a handler is not free, so we should only do it if we
        // decide it's necessary.
      }
    }
  }
}

template <bool IsFirstUpdate = false>
void UpdateOutLiveness(Bytecode bytecode, BytecodeLiveness& liveness,
                       BytecodeLivenessState* next_bytecode_in_liveness,
                       const interpreter::BytecodeArrayIterator& iterator,
                       DirectHandle<BytecodeArray> bytecode_array,
                       const BytecodeLivenessMap& liveness_map, Zone* zone) {
  switch (bytecode) {
#define BYTECODE_UPDATE_OUT_LIVENESS(Name, ...)                        \
  case Bytecode::k##Name:                                              \
    return UpdateOutLiveness<IsFirstUpdate, Bytecode::k##Name>(        \
        liveness, next_bytecode_in_liveness, iterator, bytecode_array, \
        liveness_map, zone);
    BYTECODE_LIST(BYTECODE_UPDATE_OUT_LIVENESS, BYTECODE_UPDATE_OUT_LIVENESS)
#undef BYTECODE_UPDATE_OUT_LIVENESS
  }
}

template <bool IsFirstUpdate, Bytecode bytecode,
          ImplicitRegisterUse implicit_register_use,
          OperandType... operand_types>
void UpdateLiveness(BytecodeLiveness& liveness,
                    BytecodeLivenessState** next_bytecode_in_liveness,
                    const interpreter::BytecodeArrayIterator& iterator,
                    DirectHandle<BytecodeArray> bytecode_array,
                    const BytecodeLivenessMap& liveness_map, Zone* zone) {
  UpdateOutLiveness<IsFirstUpdate, bytecode>(
      liveness, *next_bytecode_in_liveness, iterator, bytecode_array,
      liveness_map, zone);
  if (IsFirstUpdate) {
    // On the first update, allocate the in-liveness as a copy of the
    // out-liveness.
    DCHECK_NULL(liveness.in);
    liveness.in = zone->New<BytecodeLivenessState>(*liveness.out, zone);
  } else {
    // On subsequent updates, copy liveness from the out vector.
    // TODO(leszeks): If this copy doesn't change liveness, we could
    // opportunistically terminate early.
    liveness.in->CopyFrom(*liveness.out);
  }
  UpdateInLiveness<bytecode, implicit_register_use, operand_types...>(
      liveness.in, iterator);

  *next_bytecode_in_liveness = liveness.in;
}

template <bool IsFirstUpdate = false>
void UpdateLiveness(Bytecode bytecode, BytecodeLiveness& liveness,
                    BytecodeLivenessState** next_bytecode_in_liveness,
                    const interpreter::BytecodeArrayIterator& iterator,
                    DirectHandle<BytecodeArray> bytecode_array,
                    const BytecodeLivenessMap& liveness_map, Zone* zone) {
  switch (bytecode) {
#define BYTECODE_UPDATE_LIVENESS(Name, ...)                               \
  case Bytecode::k##Name:                                                 \
    return UpdateLiveness<IsFirstUpdate, Bytecode::k##Name, __VA_ARGS__>( \
        liveness, next_bytecode_in_liveness, iterator, bytecode_array,    \
        liveness_map, zone);
    BYTECODE_LIST(BYTECODE_UPDATE_LIVENESS, BYTECODE_UPDATE_LIVENESS)
#undef BYTECODE_UPDATE_LIVENESS
  }
}

void UpdateAssignments(Bytecode bytecode, BytecodeLoopAssignments* assignments,
                       const interpreter::BytecodeArrayIterator& iterator) {
  int num_operands = Bytecodes::NumberOfOperands(bytecode);
  const OperandType* operand_types = Bytecodes::GetOperandTypes(bytecode);

  for (int i = 0; i < num_operands; ++i) {
    switch (operand_types[i]) {
      case OperandType::kRegInOut:
      case OperandType::kRegOut: {
        assignments->Add(iterator.GetRegisterOperand(i));
        break;
      }
      case OperandType::kRegOutList: {
        interpreter::Register r = iterator.GetRegisterOperand(i++);
        uint32_t reg_count = iterator.GetRegisterCountOperand(i);
        assignments->AddList(r, reg_count);
        break;
      }
      case OperandType::kRegOutPair: {
        assignments->AddList(iterator.GetRegisterOperand(i), 2);
        break;
      }
      case OperandType::kRegOutTriple: {
        assignments->AddList(iterator.GetRegisterOperand(i), 3);
        break;
      }
      default:
        DCHECK(!Bytecodes::IsRegisterOutputOperandType(operand_types[i]));
        break;
    }
  }

  if (Bytecodes::WritesImplicitRegister(bytecode)) {
    assignments->Add(interpreter::Register::FromShortStar(bytecode));
  }
}

}  // namespace

class BytecodeAnalysis::BytecodeAnalysisImpl {
 public:
  std::ostream& PrintLivenessTo(std::ostream& os) const;

  BytecodeAnalysisImpl(BytecodeAnalysis& res,
                       Handle<BytecodeArray> bytecode_array, Zone* zone)
      : res_(res),
        zone_(zone),
        bytecode_array_(bytecode_array),
        loop_stack_(zone),
        loop_end_index_queue_(zone),
        iterator_(bytecode_array, zone) {}

  void Analyze();

 private:
  template <Bytecode BC>
  inline void AnalyzeBCInLoop(int current_offset, LoopInfo* current_loop_info) {
  }

  void PushLoop(int loop_header, int loop_end) {
    DCHECK_LT(loop_header, loop_end);
    DCHECK_LT(loop_stack_.top().header_offset, loop_header);
    DCHECK_EQ(res_.end_to_header_.find(loop_end), res_.end_to_header_.end());
    DCHECK_EQ(res_.header_to_info_.find(loop_header),
              res_.header_to_info_.end());

    int parent_offset = loop_stack_.top().header_offset;

    res_.end_to_header_.insert({loop_end, loop_header});
    auto it = res_.header_to_info_.insert(
        {loop_header, LoopInfo(parent_offset, loop_header, loop_end,
                               bytecode_array()->parameter_count(),
                               bytecode_array()->register_count(), zone())});
    // Get the loop info pointer from the output of insert.
    LoopInfo* loop_info = &it.first->second;

    if (loop_stack_.top().loop_info) {
      loop_stack_.top().loop_info->mark_not_innermost();
    }
    loop_stack_.push({loop_header, loop_info});
  }

#if DEBUG
  bool ResumeJumpTargetsAreValid();
  bool ResumeJumpTargetLeavesResolveSuspendIds(
      int parent_offset,
      const ZoneVector<ResumeJumpTarget>& resume_jump_targets,
      std::map<int, int>* unresolved_suspend_ids);

  bool LivenessIsValid();
#endif

  bool analyze_liveness() const { return res_.analyze_liveness_; }
  Zone* zone() const { return zone_; }
  DirectHandle<BytecodeArray> bytecode_array() const { return bytecode_array_; }
  BytecodeLivenessMap& liveness_map() { return *res_.liveness_map_; }

  struct LoopStackEntry {
    int header_offset;
    LoopInfo* loop_info;
  };

  BytecodeAnalysis& res_;
  Zone* zone_;
  Handle<BytecodeArray> const bytecode_array_;
  ZoneStack<LoopStackEntry> loop_stack_;
  ZoneVector<int> loop_end_index_queue_;
  interpreter::BytecodeArrayRandomIterator iterator_;
};

template <>
inline void BytecodeAnalysis::BytecodeAnalysisImpl::AnalyzeBCInLoop<
    Bytecode::kSuspendGenerator>(int current_offset,
                                 LoopInfo* current_loop_info) {
  int suspend_id = iterator_.GetUnsignedImmediateOperand(3);
  int resume_offset = current_offset + iterator_.current_bytecode_size();
  current_loop_info->AddResumeTarget(
      ResumeJumpTarget::Leaf(suspend_id, resume_offset));
}

template <>
inline void BytecodeAnalysis::BytecodeAnalysisImpl::AnalyzeBCInLoop<
    Bytecode::kResumeGenerator>(int current_offset,
                                LoopInfo* current_loop_info) {
  current_loop_info->mark_resumable();
}

void BytecodeAnalysis::BytecodeAnalysisImpl::Analyze() {
  DCHECK_EQ(res_.bytecode_count_, -1);
  res_.bytecode_count_ = iterator_.size();

  loop_stack_.push({-1, nullptr});

  BytecodeLivenessState* next_bytecode_in_liveness = nullptr;
  int osr_loop_end_offset_ = res_.osr_bailout_id_.ToInt();
  DCHECK_EQ(osr_loop_end_offset_ < 0, res_.osr_bailout_id_.IsNone());

  if (analyze_liveness()) {
    res_.liveness_map_.emplace(bytecode_array()->length(), zone());
  }

  for (iterator_.GoToEnd(); iterator_.IsValid(); --iterator_) {
    Bytecode bytecode = iterator_.current_bytecode();
    int current_offset = iterator_.current_offset();

    if (bytecode == Bytecode::kJumpLoop) {
      // Every byte up to and including the last byte within the backwards jump
      // instruction is considered part of the loop, set loop end accordingly.
      int loop_end = current_offset + iterator_.current_bytecode_size();
      int loop_header = iterator_.GetJumpTargetOffset();
      PushLoop(loop_header, loop_end);

      if (current_offset == osr_loop_end_offset_) {
        res_.osr_entry_point_ = loop_header;
      } else if (current_offset < osr_loop_end_offset_) {
        // Assert that we've found the osr_entry_point if we've gone past the
        // osr_loop_end_offset. Note, we are iterating the bytecode in reverse,
        // so the less-than in the above condition is correct.
        DCHECK_LE(0, res_.osr_entry_point_);
      }

      // Save the index so that we can do another pass later.
      if (analyze_liveness()) {
        loop_end_index_queue_.push_back(iterator_.current_index());
      }
    }

    // We have to pop from loop_stack_ if:
    // 1) We entered the body of the loop
    // 2) If we have a JumpLoop that jumps to itself (i.e an empty loop)
    bool in_loop = loop_stack_.size() > 1 &&
                   (bytecode != Bytecode::kJumpLoop ||
                    iterator_.GetJumpTargetOffset() == current_offset);

    if (in_loop) {
      LoopStackEntry& current_loop = loop_stack_.top();
      LoopInfo* current_loop_info = current_loop.loop_info;

      // TODO(leszeks): Ideally, we'd only set values that were assigned in
      // the loop *and* are live when the loop exits. However, this requires
      // tracking the out-liveness of *all* loop exits, which is not
      // information we currently have.
      UpdateAssignments(bytecode, &current_loop_info->assignments(), iterator_);

      switch (bytecode) {
#define CASE(BC, ...)                                                    \
  case Bytecode::k##BC:                                                  \
    AnalyzeBCInLoop<Bytecode::k##BC>(current_offset, current_loop_info); \
    break;
        BYTECODE_LIST(CASE, CASE)
#undef CASE
      }

      // If we've reached the header of the loop, pop it off the stack.
      if (current_offset == current_loop.header_offset) {
        loop_stack_.pop();
        if (loop_stack_.size() > 1) {
          // If there is still an outer loop, propagate inner loop assignments.
          LoopInfo* parent_loop_info = loop_stack_.top().loop_info;

          if (current_loop_info->resumable()) {
            parent_loop_info->mark_resumable();
          }

          parent_loop_info->assignments().Union(
              current_loop_info->assignments());

          // Also, propagate resume targets. Instead of jumping to the target
          // itself, the outer loop will jump to this loop header for any
          // targets that are inside the current loop, so that this loop stays
          // reducible. Hence, a nested loop of the form:
          //
          //                switch (#1 -> suspend1, #2 -> suspend2)
          //                loop {
          //     suspend1:    suspend #1
          //                  loop {
          //     suspend2:      suspend #2
          //                  }
          //                }
          //
          // becomes:
          //
          //                switch (#1 -> loop1, #2 -> loop1)
          //     loop1:     loop {
          //                  switch (#1 -> suspend1, #2 -> loop2)
          //     suspend1:    suspend #1
          //     loop2:       loop {
          //                    switch (#2 -> suspend2)
          //     suspend2:      suspend #2
          //                  }
          //                }
          for (const auto& target : current_loop_info->resume_jump_targets()) {
            parent_loop_info->AddResumeTarget(
                ResumeJumpTarget::AtLoopHeader(current_offset, target));
          }

        } else {
          // Otherwise, just propagate inner loop suspends to top-level.
          for (const auto& target : current_loop_info->resume_jump_targets()) {
            res_.resume_jump_targets_.push_back(
                ResumeJumpTarget::AtLoopHeader(current_offset, target));
          }
        }
      }
    } else if (bytecode == Bytecode::kSuspendGenerator) {
      // If we're not in a loop, we still need to look for suspends.
      // TODO(leszeks): It would be nice to de-duplicate this with the in-loop
      // case
      int suspend_id = iterator_.GetUnsignedImmediateOperand(3);
      int resume_offset = current_offset + iterator_.current_bytecode_size();
      res_.resume_jump_targets_.push_back(
          ResumeJumpTarget::Leaf(suspend_id, resume_offset));
    }

    if (analyze_liveness()) {
      BytecodeLiveness& liveness =
          liveness_map().InsertNewLiveness(current_offset);
      UpdateLiveness<true>(bytecode, liveness, &next_bytecode_in_liveness,
                           iterator_, bytecode_array(), liveness_map(), zone());
    }
  }

  DCHECK_EQ(loop_stack_.size(), 1u);
  DCHECK_EQ(loop_stack_.top().header_offset, -1);

  DCHECK(ResumeJumpTargetsAreValid());

  if (!analyze_liveness()) return;

  // At this point, every bytecode has a valid in and out liveness, except for
  // propagating liveness across back edges (i.e. JumpLoop). Subsequent liveness
  // analysis iterations can only add additional liveness bits that are pulled
  // across these back edges.
  //
  // Furthermore, a loop header's in-liveness can only change based on any
  // bytecodes *after* the loop end --  it cannot change as a result of the
  // JumpLoop liveness being updated, as the only liveness bits than can be
  // added to the loop body are those of the loop header.
  //
  // So, if we know that the liveness of bytecodes after a loop header won't
  // change (e.g. because there are no loops in them, or we have already ensured
  // those loops are valid), we can safely update the loop end and pass over the
  // loop body, and then never have to pass over that loop end again, because we
  // have shown that its target, the loop header, can't change from the entries
  // after the loop, and can't change from any loop body pass.
  //
  // This means that in a pass, we can iterate backwards over the bytecode
  // array, process any loops that we encounter, and on subsequent passes we can
  // skip processing those loops (though we still have to process inner loops).
  //
  // Equivalently, we can queue up loop ends from back to front, and pass over
  // the loops in that order, as this preserves both the bottom-to-top and
  // outer-to-inner requirements.

  for (int loop_end_index : loop_end_index_queue_) {
    iterator_.GoToIndex(loop_end_index);

    DCHECK_EQ(iterator_.current_bytecode(), Bytecode::kJumpLoop);

    int header_offset = iterator_.GetJumpTargetOffset();
    int end_offset = iterator_.current_offset();

    BytecodeLiveness& header_liveness =
        liveness_map().GetLiveness(header_offset);
    BytecodeLiveness& end_liveness = liveness_map().GetLiveness(end_offset);

    if (!end_liveness.out->UnionIsChanged(*header_liveness.in)) {
      // Only update the loop body if the loop end liveness changed.
      continue;
    }
    end_liveness.in->CopyFrom(*end_liveness.out);
    next_bytecode_in_liveness = end_liveness.in;

    // Advance into the loop body.
    --iterator_;
    for (; iterator_.current_offset() > header_offset; --iterator_) {
      Bytecode bytecode = iterator_.current_bytecode();
      int current_offset = iterator_.current_offset();
      BytecodeLiveness& liveness = liveness_map().GetLiveness(current_offset);
      UpdateLiveness(bytecode, liveness, &next_bytecode_in_liveness, iterator_,
                     bytecode_array(), liveness_map(), zone());
    }
    // Now we are at the loop header. Since the in-liveness of the header can't
    // change, we need only to update the out-liveness.
    UpdateOutLiveness(iterator_.current_bytecode(), header_liveness,
                      next_bytecode_in_liveness, iterator_, bytecode_array(),
                      liveness_map(), zone());
  }

  DCHECK(analyze_liveness());
  if (v8_flags.trace_environment_liveness) {
    StdoutStream of;
    PrintLivenessTo(of);
  }

  DCHECK(LivenessIsValid());
}

bool BytecodeAnalysis::IsLoopHeader(int offset) const {
  return header_to_info_.find(offset) != header_to_info_.end();
}

int BytecodeAnalysis::GetLoopOffsetFor(int offset) const {
  auto loop_end_to_header = end_to_header_.upper_bound(offset);
  // If there is no next end => offset is not in a loop.
  if (loop_end_to_header == end_to_header_.end()) {
    return -1;
  }
  // If the header precedes the offset, this is the loop
  //
  //   .> header  <--loop_end_to_header
  //   |
  //   |  <--offset
  //   |
  //   `- end
  if (loop_end_to_header->second <= offset) {
    return loop_end_to_header->second;
  }
  // Otherwise there is a (potentially nested) loop after this offset.
  //
  //    <--offset
  //
  //   .> header
  //   |
  //   | .> header  <--loop_end_to_header
  //   | |
  //   | `- end
  //   |
  //   `- end
  // We just return the parent of the next loop (might be -1).
  DCHECK(header_to_info_.upper_bound(offset) != header_to_info_.end());

  return header_to_info_.upper_bound(offset)->second.parent_offset();
}

int BytecodeAnalysis::GetLoopEndOffsetForInnermost(int header_offset) const {
  DCHECK(GetLoopInfoFor(header_offset).innermost());
  auto loop_end_to_header = end_to_header_.upper_bound(header_offset + 1);
  DCHECK_EQ(loop_end_to_header->second, header_offset);
  return loop_end_to_header->first;
}

const LoopInfo& BytecodeAnalysis::GetLoopInfoFor(int header_offset) const {
  DCHECK(IsLoopHeader(header_offset));

  return header_to_info_.find(header_offset)->second;
}

const LoopInfo* BytecodeAnalysis::TryGetLoopInfoFor(int header_offset) const {
  auto it = header_to_info_.find(header_offset);
  if (it == header_to_info_.end()) return nullptr;
  return &it->second;
}

const BytecodeLivenessState* BytecodeAnalysis::GetInLivenessFor(
    int offset) const {
  if (!analyze_liveness_) return nullptr;

  return liveness_map().GetInLiveness(offset);
}

const BytecodeLivenessState* BytecodeAnalysis::GetOutLivenessFor(
    int offset) const {
  if (!analyze_liveness_) return nullptr;

  return liveness_map().GetOutLiveness(offset);
}

std::ostream& BytecodeAnalysis::BytecodeAnalysisImpl::PrintLivenessTo(
    std::ostream& os) const {
  interpreter::BytecodeArrayIterator iterator(bytecode_array_);

  for (; !iterator.done(); iterator.Advance()) {
    int current_offset = iterator.current_offset();

    const BytecodeLivenessState* in_liveness =
        res_.GetInLivenessFor(current_offset);
    const BytecodeLivenessState* out_liveness =
        res_.GetOutLivenessFor(current_offset);

    os << ToString(*in_liveness) << " -> " << ToString(*out_liveness) << " | "
       << current_offset << ": ";
    iterator.PrintTo(os) << std::endl;
  }

  return os;
}

#if DEBUG
bool BytecodeAnalysis::BytecodeAnalysisImpl::ResumeJumpTargetsAreValid() {
  bool valid = true;

  // Find the generator switch.
  interpreter::BytecodeArrayRandomIterator iterator(bytecode_array_, zone());
  for (iterator.GoToStart(); iterator.IsValid(); ++iterator) {
    if (iterator.current_bytecode() == Bytecode::kSwitchOnGeneratorState) {
      break;
    }
  }

  // If the iterator is invalid, we've reached the end without finding the
  // generator switch. So, ensure there are no jump targets and exit.
  if (!iterator.IsValid()) {
    // Check top-level.
    if (!res_.resume_jump_targets().empty()) {
      PrintF(stderr,
             "Found %zu top-level resume targets but no resume switch\n",
             res_.resume_jump_targets().size());
      valid = false;
    }
    // Check loops.
    for (const std::pair<const int, LoopInfo>& loop_info :
         res_.header_to_info_) {
      if (!loop_info.second.resume_jump_targets().empty()) {
        PrintF(stderr,
               "Found %zu resume targets at loop at offset %d, but no resume "
               "switch\n",
               loop_info.second.resume_jump_targets().size(), loop_info.first);
        valid = false;
      }
    }

    return valid;
  }

  // Otherwise, we've found the resume switch. Check that the top level jumps
  // only to leaves and loop headers, then check that each loop header handles
  // all the unresolved jumps, also jumping only to leaves and inner loop
  // headers.

  // First collect all required suspend ids.
  std::map<int, int> unresolved_suspend_ids;
  for (interpreter::JumpTableTargetOffset offset :
       iterator.GetJumpTableTargetOffsets()) {
    int suspend_id = offset.case_value;
    int resume_offset = offset.target_offset;

    unresolved_suspend_ids[suspend_id] = resume_offset;
  }

  // Check top-level.
  if (!ResumeJumpTargetLeavesResolveSuspendIds(-1, res_.resume_jump_targets(),
                                               &unresolved_suspend_ids)) {
    valid = false;
  }
  // Check loops.
  for (const std::pair<const int, LoopInfo>& loop_info : res_.header_to_info_) {
    if (!ResumeJumpTargetLeavesResolveSuspendIds(
            loop_info.first, loop_info.second.resume_jump_targets(),
            &unresolved_suspend_ids)) {
      valid = false;
    }
  }

  // Check that everything is resolved.
  if (!unresolved_suspend_ids.empty()) {
    PrintF(stderr,
           "Found suspend ids that are not resolved by a final leaf resume "
           "jump:\n");

    for (const std::pair<const int, int>& target : unresolved_suspend_ids) {
      PrintF(stderr, "  %d -> %d\n", target.first, target.second);
    }
    valid = false;
  }

  return valid;
}

bool BytecodeAnalysis::BytecodeAnalysisImpl::
    ResumeJumpTargetLeavesResolveSuspendIds(
        int parent_offset,
        const ZoneVector<ResumeJumpTarget>& resume_jump_targets,
        std::map<int, int>* unresolved_suspend_ids) {
  bool valid = true;
  for (const ResumeJumpTarget& target : resume_jump_targets) {
    std::map<int, int>::iterator it =
        unresolved_suspend_ids->find(target.suspend_id());
    if (it == unresolved_suspend_ids->end()) {
      PrintF(
          stderr,
          "No unresolved suspend found for resume target with suspend id %d\n",
          target.suspend_id());
      valid = false;
      continue;
    }
    int expected_target = it->second;

    if (target.is_leaf()) {
      // Leaves should have the expected target as their target.
      if (target.target_offset() != expected_target) {
        PrintF(
            stderr,
            "Expected leaf resume target for id %d to have target offset %d, "
            "but had %d\n",
            target.suspend_id(), expected_target, target.target_offset());
        valid = false;
      } else {
        // Make sure we're resuming to a Resume bytecode
        interpreter::BytecodeArrayIterator iterator(bytecode_array_,
                                                    target.target_offset());
        if (iterator.current_bytecode() != Bytecode::kResumeGenerator) {
          PrintF(stderr,
                 "Expected resume target for id %d, offset %d, to be "
                 "ResumeGenerator, but found %s\n",
                 target.suspend_id(), target.target_offset(),
                 Bytecodes::ToString(iterator.current_bytecode()));

          valid = false;
        }
      }
      // We've resolved this suspend id, so erase it to make sure we don't
      // resolve it twice.
      unresolved_suspend_ids->erase(it);
    } else {
      // Non-leaves should have a direct inner loop header as their target.
      if (!res_.IsLoopHeader(target.target_offset())) {
        PrintF(stderr,
               "Expected non-leaf resume target for id %d to have a loop "
               "header at target offset %d\n",
               target.suspend_id(), target.target_offset());
        valid = false;
      } else {
        LoopInfo loop_info = res_.GetLoopInfoFor(target.target_offset());
        if (loop_info.parent_offset() != parent_offset) {
          PrintF(stderr,
                 "Expected non-leaf resume target for id %d to have a direct "
                 "inner loop at target offset %d\n",
                 target.suspend_id(), target.target_offset());
          valid = false;
        }
        // If the target loop is a valid inner loop, we'll check its validity
        // when we analyze its resume targets.
      }
    }
  }
  return valid;
}

bool BytecodeAnalysis::BytecodeAnalysisImpl::LivenessIsValid() {
  interpreter::BytecodeArrayRandomIterator iterator(bytecode_array_, zone());

  BytecodeLivenessState previous_liveness(bytecode_array_->register_count(),
                                          zone());

  int invalid_offset = -1;
  int which_invalid = -1;
  BytecodeLivenessState invalid_liveness(bytecode_array_->register_count(),
                                         zone());

  BytecodeLivenessState* next_bytecode_in_liveness = nullptr;

  // Ensure that there are no liveness changes if we iterate one more time.
  for (iterator.GoToEnd(); iterator.IsValid(); --iterator) {
    Bytecode bytecode = iterator.current_bytecode();

    int current_offset = iterator.current_offset();

    BytecodeLiveness& liveness = liveness_map().GetLiveness(current_offset);

    previous_liveness.CopyFrom(*liveness.out);

    UpdateOutLiveness(bytecode, liveness, next_bytecode_in_liveness, iterator,
                      bytecode_array_, liveness_map(), zone());
    // UpdateOutLiveness skips kJumpLoop, so we update it manually.
    if (bytecode == Bytecode::kJumpLoop) {
      int target_offset = iterator.GetJumpTargetOffset();
      liveness.out->Union(*liveness_map().GetInLiveness(target_offset));
    }

    if (!liveness.out->Equals(previous_liveness)) {
      invalid_liveness.CopyFrom(*liveness.out);
      // Reset the invalid liveness.
      liveness.out->CopyFrom(previous_liveness);
      invalid_offset = current_offset;
      which_invalid = 1;
      break;
    }

    previous_liveness.CopyFrom(*liveness.in);

    liveness.in->CopyFrom(*liveness.out);
    UpdateInLiveness(bytecode, liveness.in, iterator);

    if (!liveness.in->Equals(previous_liveness)) {
      invalid_liveness.CopyFrom(*liveness.in);
      // Reset the invalid liveness.
      liveness.in->CopyFrom(previous_liveness);
      invalid_offset = current_offset;
      which_invalid = 0;
      break;
    }

    next_bytecode_in_liveness = liveness.in;
  }

  // Ensure that the accumulator is not live when jumping out of a loop, or on
  // the back-edge of a loop.
  for (iterator.GoToStart(); iterator.IsValid() && invalid_offset == -1;
       ++iterator) {
    Bytecode bytecode = iterator.current_bytecode();
    int current_offset = iterator.current_offset();
    int loop_header = res_.GetLoopOffsetFor(current_offset);

    // We only care if we're inside a loop.
    if (loop_header == -1) continue;

    // We only care about jumps.
    if (!Bytecodes::IsJump(bytecode)) continue;

    int jump_target = iterator.GetJumpTargetOffset();

    // If this is a forward jump to somewhere else in the same loop, ignore it.
    if (Bytecodes::IsForwardJump(bytecode) &&
        res_.GetLoopOffsetFor(jump_target) == loop_header) {
      continue;
    }

    // The accumulator must be dead at the start of the target of the jump.
    if (liveness_map().GetLiveness(jump_target).in->AccumulatorIsLive()) {
      invalid_offset = jump_target;
      which_invalid = 0;
      break;
    }
  }

  if (invalid_offset != -1) {
    OFStream of(stderr);
    of << "Invalid liveness:" << std::endl;

    // Dump the bytecode, annotated with the liveness and marking loops.

    int loop_indent = 0;

    interpreter::BytecodeArrayIterator forward_iterator(bytecode_array_);
    for (; !forward_iterator.done(); forward_iterator.Advance()) {
      int current_offset = forward_iterator.current_offset();
      const BytecodeLivenessState* in_liveness =
          res_.GetInLivenessFor(current_offset);
      const BytecodeLivenessState* out_liveness =
          res_.GetOutLivenessFor(current_offset);

      std::string in_liveness_str = ToString(*in_liveness);
      std::string out_liveness_str = ToString(*out_liveness);

      of << in_liveness_str << " | " << out_liveness_str << " : "
         << current_offset << " : ";

      // Draw loop back edges by indentin everything between loop headers and
      // jump loop instructions.
      if (forward_iterator.current_bytecode() == Bytecode::kJumpLoop) {
        loop_indent--;
      }
      for (int i = 0; i < loop_indent; ++i) {
        of << "| ";
      }
      if (forward_iterator.current_bytecode() == Bytecode::kJumpLoop) {
        of << "`-";
      } else if (res_.IsLoopHeader(current_offset)) {
        of << ".>";
        loop_indent++;
      }
      forward_iterator.PrintTo(of);
      if (Bytecodes::IsJump(forward_iterator.current_bytecode())) {
        of << " (@" << forward_iterator.GetJumpTargetOffset() << ")";
      }
      of << std::endl;

      if (current_offset == invalid_offset) {
        // Underline the invalid liveness.
        char in_underline = which_invalid == 0 ? '^' : ' ';
        char out_underline = which_invalid == 0 ? ' ' : '^';
        of << std::string(in_liveness_str.size(), in_underline) << "   "
           << std::string(out_liveness_str.size(), out_underline);

        // Make sure to draw the loop indentation marks on this additional line.
        of << " : " << current_offset << " : ";
        for (int i = 0; i < loop_indent; ++i) {
          of << "| ";
        }

        of << std::endl;

        // Print the invalid liveness.
        if (which_invalid == 0) {
          of << ToString(invalid_liveness) << "   "
             << std::string(out_liveness_str.size(), ' ');
        } else {
          of << std::string(in_liveness_str.size(), ' ') << "   "
             << ToString(invalid_liveness);
        }

        // Make sure to draw the loop indentation marks on this additional line.
        of << " : " << current_offset << " : ";
        for (int i = 0; i < loop_indent; ++i) {
          of << "| ";
        }

        of << std::endl;
      }
    }
  }

  return invalid_offset == -1;
}
#endif

BytecodeAnalysis::BytecodeAnalysis(Handle<BytecodeArray> bytecode_array,
                                   Zone* zone, BytecodeOffset osr_bailout_id,
                                   bool analyze_liveness)
    : osr_bailout_id_(osr_bailout_id),
      analyze_liveness_(analyze_liveness),
      resume_jump_targets_(zone),
      end_to_header_(zone),
      header_to_info_(zone),
      osr_entry_point_(-1) {
  BytecodeAnalysisImpl analysis(*this, bytecode_array, zone);
  analysis.Analyze();
  DCHECK_IMPLIES(analyze_liveness_, liveness_map_.has_value());
  DCHECK_NE(bytecode_count_, -1);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
