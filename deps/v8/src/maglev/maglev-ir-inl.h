// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_IR_INL_H_
#define V8_MAGLEV_MAGLEV_IR_INL_H_

#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

inline const VirtualObject::List& GetVirtualObjects(
    const DeoptFrame& deopt_frame) {
  if (deopt_frame.type() == DeoptFrame::FrameType::kInterpretedFrame) {
    return deopt_frame.as_interpreted().frame_state()->virtual_objects();
  }
  DCHECK_NOT_NULL(deopt_frame.parent());
  return GetVirtualObjects(*deopt_frame.parent());
}

namespace detail {

enum class DeoptFrameVisitMode {
  kDefault,
  kRemoveIdentities,
};

template <DeoptFrameVisitMode mode, typename T>
using const_if_default =
    std::conditional_t<mode == DeoptFrameVisitMode::kDefault, const T, T>;

template <DeoptFrameVisitMode mode>
using ValueNodeT =
    std::conditional_t<mode == DeoptFrameVisitMode::kDefault, ValueNode*,
                       ValueNode*&>;

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputSingleFrameImpl(
    const_if_default<mode, DeoptFrame>& frame, InputLocation*& input_location,
    Function&& f,
    std::function<bool(interpreter::Register)> is_result_register) {
  switch (frame.type()) {
    case DeoptFrame::FrameType::kInterpretedFrame:
      f(frame.as_interpreted().closure(), input_location);
      frame.as_interpreted().frame_state()->ForEachValue(
          frame.as_interpreted().unit(),
          [&](ValueNodeT<mode> node, interpreter::Register reg) {
            // Skip over the result location for lazy deopts, since it is
            // irrelevant for lazy deopts (unoptimized code will recreate the
            // result).
            if (is_result_register(reg)) return;
            f(node, input_location);
          });
      break;
    case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
      // The inlined arguments frame can never be the top frame.
      f(frame.as_inlined_arguments().closure(), input_location);
      for (ValueNodeT<mode> node : frame.as_inlined_arguments().arguments()) {
        f(node, input_location);
      }
      break;
    }
    case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
      f(frame.as_construct_stub().receiver(), input_location);
      f(frame.as_construct_stub().context(), input_location);
      break;
    }
    case DeoptFrame::FrameType::kBuiltinContinuationFrame:
      for (ValueNodeT<mode> node :
           frame.as_builtin_continuation().parameters()) {
        f(node, input_location);
      }
      f(frame.as_builtin_continuation().context(), input_location);
      break;
  }
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForVirtualObject(VirtualObject* vobject,
                          InputLocation*& input_location,
                          const VirtualObject::List& virtual_objects,
                          Function&& f) {
  if (vobject->type() != VirtualObject::kDefault) return;
  for (uint32_t i = 0; i < vobject->slot_count(); i++) {
    ValueNode* value = vobject->get_by_index(i);
    if (IsConstantNode(value->opcode())) {
      // No location assigned to constants.
      continue;
    }
    if constexpr (mode == DeoptFrameVisitMode::kRemoveIdentities) {
      if (value->Is<Identity>()) {
        value = value->input(0).node();
        vobject->set_by_index(i, value);
      }
    }
    // Special nodes.
    switch (value->opcode()) {
      case Opcode::kArgumentsElements:
      case Opcode::kArgumentsLength:
      case Opcode::kRestLength:
        // No location assigned to these opcodes.
        break;
      case Opcode::kVirtualObject:
        UNREACHABLE();
      case Opcode::kInlinedAllocation: {
        InlinedAllocation* alloc = value->Cast<InlinedAllocation>();
        // Check if it has escaped.
        if (alloc->HasBeenAnalysed() && alloc->HasBeenElided()) {
          VirtualObject* vobject = virtual_objects.FindAllocatedWith(alloc);
          CHECK_NOT_NULL(vobject);
          input_location++;  // Reserved for the inlined allocation.
          DeepForVirtualObject<mode>(vobject, input_location, virtual_objects,
                                     f);
        } else {
          f(alloc, input_location);
          input_location +=
              alloc->object()->InputLocationSizeNeeded(virtual_objects) + 1;
        }
        break;
      }
      default:
        f(value, input_location);
        input_location++;
        break;
    }
  }
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputAndVirtualObject(
    const_if_default<mode, DeoptFrame>& frame, InputLocation*& input_location,
    Function&& f,
    std::function<bool(interpreter::Register)> is_result_register =
        [](interpreter::Register) { return false; }) {
  const VirtualObject::List& virtual_objects = GetVirtualObjects(frame);
  auto update_node = [&f, &virtual_objects](ValueNodeT<mode> node,
                                            InputLocation*& input_location) {
    DCHECK(!node->template Is<VirtualObject>());
    if constexpr (mode == DeoptFrameVisitMode::kRemoveIdentities) {
      if (node->template Is<Identity>()) {
        node = node->input(0).node();
      }
    }
    if (auto alloc = node->template TryCast<InlinedAllocation>()) {
      VirtualObject* vobject = virtual_objects.FindAllocatedWith(alloc);
      CHECK_NOT_NULL(vobject);
      if (alloc->HasBeenAnalysed() && alloc->HasBeenElided()) {
        input_location++;  // Reserved for the inlined allocation.
        return DeepForVirtualObject<mode>(vobject, input_location,
                                          virtual_objects, f);
      } else {
        f(alloc, input_location);
        input_location += vobject->InputLocationSizeNeeded(virtual_objects) + 1;
      }
    } else {
      f(node, input_location);
      input_location++;
    }
  };
  DeepForEachInputSingleFrameImpl<mode>(frame, input_location, update_node,
                                        is_result_register);
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputImpl(const_if_default<mode, DeoptFrame>& frame,
                          InputLocation*& input_location, Function&& f) {
  if (frame.parent()) {
    DeepForEachInputImpl<mode>(*frame.parent(), input_location, f);
  }
  DeepForEachInputAndVirtualObject<mode>(frame, input_location, f);
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputForEager(
    const_if_default<mode, EagerDeoptInfo>* deopt_info, Function&& f) {
  InputLocation* input_location = deopt_info->input_locations();
  DeepForEachInputImpl<mode>(deopt_info->top_frame(), input_location,
                             std::forward<Function>(f));
}

template <DeoptFrameVisitMode mode, typename Function>
void DeepForEachInputForLazy(const_if_default<mode, LazyDeoptInfo>* deopt_info,
                             Function&& f) {
  InputLocation* input_location = deopt_info->input_locations();
  auto& top_frame = deopt_info->top_frame();
  if (top_frame.parent()) {
    DeepForEachInputImpl<mode>(*top_frame.parent(), input_location, f);
  }
  DeepForEachInputAndVirtualObject<mode>(
      top_frame, input_location, f, [deopt_info](interpreter::Register reg) {
        return deopt_info->IsResultRegister(reg);
      });
}

template <typename Function>
void DeepForEachInput(const EagerDeoptInfo* deopt_info, Function&& f) {
  return DeepForEachInputForEager<DeoptFrameVisitMode::kDefault>(deopt_info, f);
}

template <typename Function>
void DeepForEachInput(const LazyDeoptInfo* deopt_info, Function&& f) {
  return DeepForEachInputForLazy<DeoptFrameVisitMode::kDefault>(deopt_info, f);
}

template <typename Function>
void DeepForEachInputRemovingIdentities(EagerDeoptInfo* deopt_info,
                                        Function&& f) {
  return DeepForEachInputForEager<DeoptFrameVisitMode::kRemoveIdentities>(
      deopt_info, f);
}

template <typename Function>
void DeepForEachInputRemovingIdentities(LazyDeoptInfo* deopt_info,
                                        Function&& f) {
  return DeepForEachInputForLazy<DeoptFrameVisitMode::kRemoveIdentities>(
      deopt_info, f);
}

}  // namespace detail

#ifdef DEBUG
inline RegList GetGeneralRegistersUsedAsInputs(
    const EagerDeoptInfo* deopt_info) {
  RegList regs;
  detail::DeepForEachInput(deopt_info,
                           [&regs](ValueNode* value, InputLocation* input) {
                             if (input->IsGeneralRegister()) {
                               regs.set(input->AssignedGeneralRegister());
                             }
                           });
  return regs;
}
#endif  // DEBUG

// Helper macro for checking that a reglist is empty which prints the contents
// when non-empty.
#define DCHECK_REGLIST_EMPTY(...) DCHECK_EQ((__VA_ARGS__), RegList{})

// ---
// Value location constraint setting helpers.
// ---

static constexpr int kNoVreg = -1;

inline void DefineAsRegister(Node* node) {
  node->result().SetUnallocated(
      compiler::UnallocatedOperand::MUST_HAVE_REGISTER, kNoVreg);
}
inline void DefineAsConstant(Node* node) {
  node->result().SetUnallocated(compiler::UnallocatedOperand::NONE, kNoVreg);
}

inline void DefineAsFixed(Node* node, Register reg) {
  node->result().SetUnallocated(compiler::UnallocatedOperand::FIXED_REGISTER,
                                reg.code(), kNoVreg);
}

// TODO(v8:7700): Create generic DefineSameAs(..., int input).
inline void DefineSameAsFirst(Node* node) {
  node->result().SetUnallocated(kNoVreg, 0);
}

inline void UseRegister(Input& input) {
  input.SetUnallocated(compiler::UnallocatedOperand::MUST_HAVE_REGISTER,
                       compiler::UnallocatedOperand::USED_AT_END, kNoVreg);
}
inline void UseAndClobberRegister(Input& input) {
  input.SetUnallocated(compiler::UnallocatedOperand::MUST_HAVE_REGISTER,
                       compiler::UnallocatedOperand::USED_AT_START, kNoVreg);
}
inline void UseAny(Input& input) {
  input.SetUnallocated(
      compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT,
      compiler::UnallocatedOperand::USED_AT_END, kNoVreg);
}
inline void UseFixed(Input& input, Register reg) {
  input.SetUnallocated(compiler::UnallocatedOperand::FIXED_REGISTER, reg.code(),
                       kNoVreg);
  input.node()->SetHint(input.operand());
}
inline void UseFixed(Input& input, DoubleRegister reg) {
  input.SetUnallocated(compiler::UnallocatedOperand::FIXED_FP_REGISTER,
                       reg.code(), kNoVreg);
  input.node()->SetHint(input.operand());
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_IR_INL_H_
