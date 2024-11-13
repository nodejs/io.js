// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_CODE_GEN_STATE_H_
#define V8_MAGLEV_MAGLEV_CODE_GEN_STATE_H_

#include "src/codegen/assembler.h"
#include "src/codegen/label.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/maglev-safepoint-table.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/js-heap-broker.h"
#include "src/execution/frame-constants.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

class InterpreterFrameState;
class MaglevAssembler;

class DeferredCodeInfo {
 public:
  virtual void Generate(MaglevAssembler* masm) = 0;
  Label deferred_code_label;
};

class MaglevCodeGenState {
 public:
  MaglevCodeGenState(MaglevCompilationInfo* compilation_info,
                     MaglevSafepointTableBuilder* safepoint_table_builder)
      : compilation_info_(compilation_info),
        safepoint_table_builder_(safepoint_table_builder) {}

  void set_tagged_slots(int slots) { tagged_slots_ = slots; }
  void set_untagged_slots(int slots) { untagged_slots_ = slots; }

  void PushDeferredCode(DeferredCodeInfo* deferred_code) {
    deferred_code_.push_back(deferred_code);
  }
  const std::vector<DeferredCodeInfo*>& deferred_code() const {
    return deferred_code_;
  }
  std::vector<DeferredCodeInfo*> TakeDeferredCode() {
    return std::exchange(deferred_code_, std::vector<DeferredCodeInfo*>());
  }
  void PushEagerDeopt(EagerDeoptInfo* info) { eager_deopts_.push_back(info); }
  void PushLazyDeopt(LazyDeoptInfo* info) { lazy_deopts_.push_back(info); }
  const std::vector<EagerDeoptInfo*>& eager_deopts() const {
    return eager_deopts_;
  }
  const std::vector<LazyDeoptInfo*>& lazy_deopts() const {
    return lazy_deopts_;
  }

  void PushHandlerInfo(NodeBase* node) { handlers_.push_back(node); }
  const std::vector<NodeBase*>& handlers() const { return handlers_; }

  compiler::NativeContextRef native_context() const {
    return broker()->target_native_context();
  }
  compiler::JSHeapBroker* broker() const { return compilation_info_->broker(); }
  MaglevGraphLabeller* graph_labeller() const {
    return compilation_info_->graph_labeller();
  }
  int stack_slots() const { return untagged_slots_ + tagged_slots_; }
  int tagged_slots() const { return tagged_slots_; }

  uint16_t parameter_count() const {
    return compilation_info_->toplevel_compilation_unit()->parameter_count();
  }

  MaglevSafepointTableBuilder* safepoint_table_builder() const {
    return safepoint_table_builder_;
  }
  MaglevCompilationInfo* compilation_info() const { return compilation_info_; }

  Label* entry_label() { return &entry_label_; }

  void set_max_deopted_stack_size(uint32_t max_deopted_stack_size) {
    max_deopted_stack_size_ = max_deopted_stack_size;
  }

  void set_max_call_stack_args_(uint32_t max_call_stack_args) {
    max_call_stack_args_ = max_call_stack_args;
  }

  uint32_t stack_check_offset() {
    int32_t parameter_slots =
        compilation_info_->toplevel_compilation_unit()->parameter_count();
    uint32_t stack_slots = tagged_slots_ + untagged_slots_;
    DCHECK(is_int32(stack_slots));
    int32_t optimized_frame_height = parameter_slots * kSystemPointerSize +
                                     StandardFrameConstants::kFixedFrameSize +
                                     stack_slots * kSystemPointerSize;
    DCHECK(is_int32(max_deopted_stack_size_));
    int32_t signed_max_unoptimized_frame_height =
        static_cast<int32_t>(max_deopted_stack_size_);

    // The offset is either the delta between the optimized frames and the
    // interpreted frame, or the maximal number of bytes pushed to the stack
    // while preparing for function calls, whichever is bigger.
    uint32_t frame_height_delta = static_cast<uint32_t>(std::max(
        signed_max_unoptimized_frame_height - optimized_frame_height, 0));
    uint32_t max_pushed_argument_bytes =
        static_cast<uint32_t>(max_call_stack_args_ * kSystemPointerSize);
    return std::max(frame_height_delta, max_pushed_argument_bytes);
  }

  Label* osr_entry() { return &osr_entry_; }

 private:
  MaglevCompilationInfo* const compilation_info_;
  MaglevSafepointTableBuilder* const safepoint_table_builder_;

  std::vector<DeferredCodeInfo*> deferred_code_;
  std::vector<EagerDeoptInfo*> eager_deopts_;
  std::vector<LazyDeoptInfo*> lazy_deopts_;
  std::vector<NodeBase*> handlers_;

  int untagged_slots_ = 0;
  int tagged_slots_ = 0;
  uint32_t max_deopted_stack_size_ = kMaxUInt32;
  uint32_t max_call_stack_args_ = kMaxUInt32;

  // Entry point label for recursive calls.
  Label entry_label_;
  Label osr_entry_;
};

// Some helpers for codegen.
// TODO(leszeks): consider moving this to a separate header.

inline int GetSafepointIndexForStackSlot(int i) {
  // Safepoint tables also contain slots for all fixed frame slots (both
  // above and below the fp).
  return StandardFrameConstants::kFixedSlotCount + i;
}

inline Register ToRegister(const compiler::InstructionOperand& operand) {
  return compiler::AllocatedOperand::cast(operand).GetRegister();
}

inline DoubleRegister ToDoubleRegister(
    const compiler::InstructionOperand& operand) {
  return compiler::AllocatedOperand::cast(operand).GetDoubleRegister();
}

template <typename RegisterT>
inline auto ToRegisterT(const compiler::InstructionOperand& operand) {
  if constexpr (std::is_same_v<RegisterT, Register>) {
    return ToRegister(operand);
  } else {
    return ToDoubleRegister(operand);
  }
}

inline Register ToRegister(const ValueLocation& location) {
  return ToRegister(location.operand());
}

inline DoubleRegister ToDoubleRegister(const ValueLocation& location) {
  return ToDoubleRegister(location.operand());
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_CODE_GEN_STATE_H_
