// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_ITERATOR_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_ITERATOR_H_

#include <memory>

#include "include/v8-callbacks.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace interpreter {

class BytecodeArrayIterator;

struct V8_EXPORT_PRIVATE JumpTableTargetOffset {
  int case_value;
  int target_offset;
};

class V8_EXPORT_PRIVATE JumpTableTargetOffsets final {
 public:
  // Minimal iterator implementation for use in ranged-for.
  class V8_EXPORT_PRIVATE iterator final {
   public:
    iterator(int case_value, int table_offset, int table_end,
             const BytecodeArrayIterator* iterator);

    JumpTableTargetOffset operator*();
    iterator& operator++();
    bool operator!=(const iterator& other);

   private:
    void UpdateAndAdvanceToValid();

    const BytecodeArrayIterator* iterator_;
    Tagged<Smi> current_;
    int index_;
    int table_offset_;
    int table_end_;
  };

  JumpTableTargetOffsets(const BytecodeArrayIterator* iterator, int table_start,
                         int table_size, int case_value_base);

  iterator begin() const;
  iterator end() const;

  int size() const;

 private:
  const BytecodeArrayIterator* iterator_;
  int table_start_;
  int table_size_;
  int case_value_base_;
};

class V8_EXPORT_PRIVATE BytecodeArrayIterator {
 public:
  explicit BytecodeArrayIterator(Handle<BytecodeArray> bytecode_array,
                                 int initial_offset = 0);
  BytecodeArrayIterator(Handle<BytecodeArray> bytecode_array,
                        int initial_offset, DisallowGarbageCollection& no_gc);
  ~BytecodeArrayIterator();

  BytecodeArrayIterator(const BytecodeArrayIterator&) = delete;
  BytecodeArrayIterator& operator=(const BytecodeArrayIterator&) = delete;

  inline void Advance() {
    cursor_ += current_bytecode_size_without_prefix();
    UpdateOperandScale();
  }
  void SetOffset(int offset);
  void Reset() { SetOffset(0); }

  // Whether the given offset is reachable in this bytecode array.
  static bool IsValidOffset(Handle<BytecodeArray> bytecode_array, int offset);

  void ApplyDebugBreak();

  inline Bytecode current_bytecode() const {
    DCHECK(!done());
    uint8_t current_byte = *cursor_;
    Bytecode current_bytecode = Bytecodes::FromByte(current_byte);
    DCHECK(!Bytecodes::IsPrefixScalingBytecode(current_bytecode));
    return current_bytecode;
  }
  int current_bytecode_size() const {
    return prefix_size_ + current_bytecode_size_without_prefix();
  }
  int current_bytecode_size_without_prefix() const {
    return Bytecodes::Size(current_bytecode(), current_operand_scale());
  }
  int current_offset() const {
    return static_cast<int>(cursor_ - start_ - prefix_size_);
  }
  uint8_t* current_address() const { return cursor_ - prefix_size_; }
  int next_offset() const { return current_offset() + current_bytecode_size(); }
  Bytecode next_bytecode() const {
    uint8_t* next_cursor = cursor_ + current_bytecode_size_without_prefix();
    if (next_cursor == end_) return Bytecode::kIllegal;
    Bytecode next_bytecode = Bytecodes::FromByte(*next_cursor);
    if (Bytecodes::IsPrefixScalingBytecode(next_bytecode)) {
      next_bytecode = Bytecodes::FromByte(*(next_cursor + 1));
    }
    return next_bytecode;
  }
  OperandScale current_operand_scale() const { return operand_scale_; }
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }

  uint32_t GetFlag8Operand(int operand_index) const;
  uint32_t GetFlag16Operand(int operand_index) const;
  uint32_t GetUnsignedImmediateOperand(int operand_index) const;
  int32_t GetImmediateOperand(int operand_index) const;
  uint32_t GetIndexOperand(int operand_index) const;
  FeedbackSlot GetSlotOperand(int operand_index) const;
  Register GetParameter(int parameter_index) const;
  uint32_t GetRegisterCountOperand(int operand_index) const;
  Register GetRegisterOperand(int operand_index) const;
  Register GetStarTargetRegister() const;
  std::pair<Register, Register> GetRegisterPairOperand(int operand_index) const;
  RegisterList GetRegisterListOperand(int operand_index) const;
  int GetRegisterOperandRange(int operand_index) const;
  Runtime::FunctionId GetRuntimeIdOperand(int operand_index) const;
  Runtime::FunctionId GetIntrinsicIdOperand(int operand_index) const;
  uint32_t GetNativeContextIndexOperand(int operand_index) const;
  template <typename IsolateT>
  Handle<Object> GetConstantAtIndex(int offset, IsolateT* isolate) const;
  bool IsConstantAtIndexSmi(int offset) const;
  Tagged<Smi> GetConstantAtIndexAsSmi(int offset) const;
  template <typename IsolateT>
  Handle<Object> GetConstantForIndexOperand(int operand_index,
                                            IsolateT* isolate) const;

  // Returns the relative offset of the branch target at the current bytecode.
  // It is an error to call this method if the bytecode is not for a jump or
  // conditional jump. Returns a negative offset for backward jumps.
  int GetRelativeJumpTargetOffset() const;
  // Returns the absolute offset of the branch target at the current bytecode.
  // It is an error to call this method if the bytecode is not for a jump or
  // conditional jump.
  int GetJumpTargetOffset() const;
  // Returns an iterator over the absolute offsets of the targets of the current
  // switch bytecode's jump table. It is an error to call this method if the
  // bytecode is not a switch.
  JumpTableTargetOffsets GetJumpTableTargetOffsets() const;

  // Returns the absolute offset of the bytecode at the given relative offset
  // from the current bytecode.
  int GetAbsoluteOffset(int relative_offset) const;

  std::ostream& PrintTo(std::ostream& os) const;

  static void UpdatePointersCallback(void* iterator) {
    reinterpret_cast<BytecodeArrayIterator*>(iterator)->UpdatePointers();
  }

  void UpdatePointers();

  inline bool done() const { return cursor_ >= end_; }

  bool operator==(const BytecodeArrayIterator& other) const {
    return cursor_ == other.cursor_;
  }
  bool operator!=(const BytecodeArrayIterator& other) const {
    return cursor_ != other.cursor_;
  }

 private:
  uint32_t GetUnsignedOperand(int operand_index,
                              OperandType operand_type) const;
  int32_t GetSignedOperand(int operand_index, OperandType operand_type) const;

  inline void UpdateOperandScale() {
    if (done()) return;
    uint8_t current_byte = *cursor_;
    Bytecode current_bytecode = Bytecodes::FromByte(current_byte);
    if (Bytecodes::IsPrefixScalingBytecode(current_bytecode)) {
      operand_scale_ =
          Bytecodes::PrefixBytecodeToOperandScale(current_bytecode);
      ++cursor_;
      prefix_size_ = 1;
    } else {
      operand_scale_ = OperandScale::kSingle;
      prefix_size_ = 0;
    }
  }

  Handle<BytecodeArray> bytecode_array_;
  uint8_t* start_;
  uint8_t* end_;
  // The cursor always points to the active bytecode. If there's a prefix, the
  // prefix is at (cursor - 1).
  uint8_t* cursor_;
  OperandScale operand_scale_;
  int prefix_size_;
  LocalHeap* const local_heap_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_ARRAY_ITERATOR_H_
