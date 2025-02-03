// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_REGISTER_H_
#define V8_INTERPRETER_BYTECODE_REGISTER_H_

#include <optional>

#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

constexpr int OffsetFromFPToRegisterIndex(int offset) {
  return (InterpreterFrameConstants::kRegisterFileFromFp - offset) /
         kSystemPointerSize;
}

// An interpreter Register which is located in the function's Register file
// in its stack-frame. Register hold parameters, this, and expression values.
class V8_EXPORT_PRIVATE Register final {
 public:
  constexpr explicit Register(int index = kInvalidIndex) : index_(index) {}

  constexpr int index() const { return index_; }
  constexpr bool is_parameter() const { return index() < 0; }
  constexpr bool is_valid() const { return index_ != kInvalidIndex; }

  static constexpr Register FromParameterIndex(int index);
  constexpr int ToParameterIndex() const;

  static constexpr Register receiver() { return FromParameterIndex(0); }
  constexpr bool is_receiver() const { return ToParameterIndex() == 0; }

  // Returns an invalid register.
  static constexpr Register invalid_value() { return Register(); }

  // Returns the register for the function's closure object.
  static constexpr Register function_closure();
  constexpr bool is_function_closure() const;

  // Returns the register which holds the current context object.
  static constexpr Register current_context();
  constexpr bool is_current_context() const;

  // Returns the register for the bytecode array.
  static constexpr Register bytecode_array();
  constexpr bool is_bytecode_array() const;

  // Returns the register for the saved bytecode offset.
  static constexpr Register bytecode_offset();
  constexpr bool is_bytecode_offset() const;

  // Returns the register for the cached feedback vector.
  static constexpr Register feedback_vector();
  constexpr bool is_feedback_vector() const;

  // Returns the register for the argument count.
  static constexpr Register argument_count();

  // Returns a register that can be used to represent the accumulator
  // within code in the interpreter, but should never be emitted in
  // bytecode.
  static constexpr Register virtual_accumulator();

  constexpr OperandSize SizeOfOperand() const;

  constexpr int32_t ToOperand() const {
    return kRegisterFileStartOffset - index_;
  }
  static constexpr Register FromOperand(int32_t operand) {
    return Register(kRegisterFileStartOffset - operand);
  }

  static constexpr Register FromShortStar(Bytecode bytecode) {
    DCHECK(Bytecodes::IsShortStar(bytecode));
    return Register(static_cast<int>(Bytecode::kStar0) -
                    static_cast<int>(bytecode));
  }

  constexpr std::optional<Bytecode> TryToShortStar() const {
    if (index() >= 0 && index() < Bytecodes::kShortStarCount) {
      Bytecode bytecode =
          static_cast<Bytecode>(static_cast<int>(Bytecode::kStar0) - index());
      DCHECK_GE(bytecode, Bytecode::kFirstShortStar);
      DCHECK_LE(bytecode, Bytecode::kLastShortStar);
      return bytecode;
    }
    return {};
  }

  std::string ToString() const;

  constexpr bool operator==(const Register& other) const {
    return index() == other.index();
  }
  constexpr bool operator!=(const Register& other) const {
    return index() != other.index();
  }
  constexpr bool operator<(const Register& other) const {
    return index() < other.index();
  }
  constexpr bool operator<=(const Register& other) const {
    return index() <= other.index();
  }
  constexpr bool operator>(const Register& other) const {
    return index() > other.index();
  }
  constexpr bool operator>=(const Register& other) const {
    return index() >= other.index();
  }

 private:
  DISALLOW_NEW_AND_DELETE()

  static constexpr int kInvalidIndex = kMaxInt;

  static constexpr int kRegisterFileStartOffset =
      OffsetFromFPToRegisterIndex(0);
  static constexpr int kFirstParamRegisterIndex =
      OffsetFromFPToRegisterIndex(InterpreterFrameConstants::kFirstParamFromFp);
  static constexpr int kFunctionClosureRegisterIndex =
      OffsetFromFPToRegisterIndex(StandardFrameConstants::kFunctionOffset);
  static constexpr int kCurrentContextRegisterIndex =
      OffsetFromFPToRegisterIndex(StandardFrameConstants::kContextOffset);
  static constexpr int kBytecodeArrayRegisterIndex =
      OffsetFromFPToRegisterIndex(
          InterpreterFrameConstants::kBytecodeArrayFromFp);
  static constexpr int kBytecodeOffsetRegisterIndex =
      OffsetFromFPToRegisterIndex(
          InterpreterFrameConstants::kBytecodeOffsetFromFp);
  static constexpr int kFeedbackVectorRegisterIndex =
      OffsetFromFPToRegisterIndex(
          InterpreterFrameConstants::kFeedbackVectorFromFp);
  static constexpr int kCallerPCOffsetRegisterIndex =
      OffsetFromFPToRegisterIndex(InterpreterFrameConstants::kCallerPCOffset);
  static constexpr int kArgumentCountRegisterIndex =
      OffsetFromFPToRegisterIndex(InterpreterFrameConstants::kArgCOffset);

  int index_;
};

class RegisterList {
 public:
  RegisterList()
      : first_reg_index_(Register::invalid_value().index()),
        register_count_(0) {}
  explicit RegisterList(Register r) : RegisterList(r.index(), 1) {}

  // Returns a new RegisterList which is a truncated version of this list, with
  // |count| registers.
  const RegisterList Truncate(int new_count) {
    DCHECK_GE(new_count, 0);
    DCHECK_LT(new_count, register_count_);
    return RegisterList(first_reg_index_, new_count);
  }
  const RegisterList PopLeft() const {
    DCHECK_GE(register_count_, 0);
    return RegisterList(first_reg_index_ + 1, register_count_ - 1);
  }

  const Register operator[](size_t i) const {
    DCHECK_LT(static_cast<int>(i), register_count_);
    return Register(first_reg_index_ + static_cast<int>(i));
  }

  const Register first_register() const {
    return (register_count() == 0) ? Register(0) : (*this)[0];
  }

  const Register last_register() const {
    return (register_count() == 0) ? Register(0) : (*this)[register_count_ - 1];
  }

  int register_count() const { return register_count_; }

 private:
  friend class BytecodeRegisterAllocator;
  friend class BytecodeDecoder;
  friend class InterpreterTester;
  friend class BytecodeUtils;
  friend class BytecodeArrayIterator;
  friend class CallArguments;

  RegisterList(int first_reg_index, int register_count)
      : first_reg_index_(first_reg_index), register_count_(register_count) {}

  // Increases the size of the register list by one.
  void IncrementRegisterCount() { register_count_++; }

  int first_reg_index_;
  int register_count_;
};

constexpr Register Register::FromParameterIndex(int index) {
  DCHECK_GE(index, 0);
  int register_index = kFirstParamRegisterIndex - index;
  DCHECK_LT(register_index, 0);
  return Register(register_index);
}

constexpr int Register::ToParameterIndex() const {
  DCHECK(is_parameter());
  return kFirstParamRegisterIndex - index();
}

constexpr Register Register::function_closure() {
  return Register(kFunctionClosureRegisterIndex);
}

constexpr bool Register::is_function_closure() const {
  return index() == kFunctionClosureRegisterIndex;
}

constexpr Register Register::current_context() {
  return Register(kCurrentContextRegisterIndex);
}

constexpr bool Register::is_current_context() const {
  return index() == kCurrentContextRegisterIndex;
}

constexpr Register Register::bytecode_array() {
  return Register(kBytecodeArrayRegisterIndex);
}

constexpr bool Register::is_bytecode_array() const {
  return index() == kBytecodeArrayRegisterIndex;
}

constexpr Register Register::bytecode_offset() {
  return Register(kBytecodeOffsetRegisterIndex);
}

constexpr bool Register::is_bytecode_offset() const {
  return index() == kBytecodeOffsetRegisterIndex;
}

constexpr Register Register::feedback_vector() {
  return Register(kFeedbackVectorRegisterIndex);
}

constexpr bool Register::is_feedback_vector() const {
  return index() == kFeedbackVectorRegisterIndex;
}

// static
constexpr Register Register::virtual_accumulator() {
  return Register(kCallerPCOffsetRegisterIndex);
}

// static
constexpr Register Register::argument_count() {
  return Register(kArgumentCountRegisterIndex);
}

constexpr OperandSize Register::SizeOfOperand() const {
  int32_t operand = ToOperand();
  if (operand >= kMinInt8 && operand <= kMaxInt8) {
    return OperandSize::kByte;
  } else if (operand >= kMinInt16 && operand <= kMaxInt16) {
    return OperandSize::kShort;
  } else {
    return OperandSize::kQuad;
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_REGISTER_H_
