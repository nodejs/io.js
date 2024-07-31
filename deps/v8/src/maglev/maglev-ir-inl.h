// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_IR_INL_H_
#define V8_MAGLEV_MAGLEV_IR_INL_H_

#include <type_traits>

#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

namespace detail {

// A little bit of template magic to allow DeepForEachInput to either take a
// `const DeoptInfo*` or a `DeoptInfo*`, depending on whether the Function
// processes read-only `ValueNode*` or read-write `ValueNode*&`.
template <typename F, typename Ret, typename A, typename... Rest>
A first_argument_helper(Ret (F::*)(A, Rest...));

template <typename F, typename Ret, typename A, typename... Rest>
A first_argument_helper(Ret (F::*)(A, Rest...) const);

template <typename Function>
using first_argument = decltype(first_argument_helper(
    &std::remove_reference_t<Function>::operator()));

template <typename T, typename Function>
using const_if_function_first_arg_not_reference =
    std::conditional_t<std::is_reference_v<first_argument<Function>>, T,
                       const T>;

template <typename Function>
void DeepForEachInputImpl(
    const_if_function_first_arg_not_reference<DeoptFrame, Function>& frame,
    InputLocation* input_locations, int& index, Function&& f) {
  if (frame.parent()) {
    DeepForEachInputImpl(*frame.parent(), input_locations, index, f);
  }
  switch (frame.type()) {
    case DeoptFrame::FrameType::kInterpretedFrame:
      f(frame.as_interpreted().closure(), &input_locations[index++]);
      frame.as_interpreted().frame_state()->ForEachValue(
          frame.as_interpreted().unit(),
          [&](first_argument<Function> node, interpreter::Register reg) {
            f(node, &input_locations[index++]);
          });
      break;
    case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
      f(frame.as_inlined_arguments().closure(), &input_locations[index++]);
      for (first_argument<Function> node :
           frame.as_inlined_arguments().arguments()) {
        f(node, &input_locations[index++]);
      }
      break;
    }
    case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
      f(frame.as_construct_stub().receiver(), &input_locations[index++]);
      f(frame.as_construct_stub().context(), &input_locations[index++]);
      break;
    }
    case DeoptFrame::FrameType::kBuiltinContinuationFrame:
      for (first_argument<Function> node :
           frame.as_builtin_continuation().parameters()) {
        f(node, &input_locations[index++]);
      }
      f(frame.as_builtin_continuation().context(), &input_locations[index++]);
      break;
  }
}

template <typename Function>
void DeepForEachInput(const_if_function_first_arg_not_reference<
                          EagerDeoptInfo, Function>* deopt_info,
                      Function&& f) {
  int index = 0;
  DeepForEachInputImpl(deopt_info->top_frame(), deopt_info->input_locations(),
                       index, std::forward<Function>(f));
}

template <typename Function>
void DeepForEachInput(const_if_function_first_arg_not_reference<
                          LazyDeoptInfo, Function>* deopt_info,
                      Function&& f) {
  int index = 0;
  InputLocation* input_locations = deopt_info->input_locations();
  auto& top_frame = deopt_info->top_frame();
  if (top_frame.parent()) {
    DeepForEachInputImpl(*top_frame.parent(), input_locations, index, f);
  }
  // Handle the top-of-frame info separately, since we have to skip the result
  // location.
  switch (top_frame.type()) {
    case DeoptFrame::FrameType::kInterpretedFrame:
      f(top_frame.as_interpreted().closure(), &input_locations[index++]);
      top_frame.as_interpreted().frame_state()->ForEachValue(
          top_frame.as_interpreted().unit(),
          [&](first_argument<Function> node, interpreter::Register reg) {
            // Skip over the result location since it is irrelevant for lazy
            // deopts (unoptimized code will recreate the result).
            if (deopt_info->IsResultRegister(reg)) return;
            f(node, &input_locations[index++]);
          });
      break;
    case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
      f(top_frame.as_construct_stub().receiver(), &input_locations[index++]);
      f(top_frame.as_construct_stub().context(), &input_locations[index++]);
      break;
    }
    case DeoptFrame::FrameType::kInlinedArgumentsFrame:
      // The inlined arguments frame can never be the top frame.
      UNREACHABLE();
    case DeoptFrame::FrameType::kBuiltinContinuationFrame:
      for (first_argument<Function> node :
           top_frame.as_builtin_continuation().parameters()) {
        f(node, &input_locations[index++]);
      }
      f(top_frame.as_builtin_continuation().context(),
        &input_locations[index++]);
      break;
  }
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
