// Copyright 2008-2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-bytecode-generator.h"

#include "src/ast/ast.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecode-generator-inl.h"
#include "src/regexp/regexp-bytecode-peephole.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/regexp/regexp-macro-assembler.h"

namespace v8 {
namespace internal {

RegExpBytecodeGenerator::RegExpBytecodeGenerator(Isolate* isolate, Zone* zone)
    : RegExpMacroAssembler(isolate, zone),
      buffer_(kInitialBufferSize, zone),
      pc_(0),
      advance_current_end_(kInvalidPC),
      jump_edges_(zone),
      isolate_(isolate) {}

RegExpBytecodeGenerator::~RegExpBytecodeGenerator() {
  if (backtrack_.is_linked()) backtrack_.Unuse();
}

RegExpBytecodeGenerator::IrregexpImplementation
RegExpBytecodeGenerator::Implementation() {
  return kBytecodeImplementation;
}

void RegExpBytecodeGenerator::Bind(Label* l) {
  advance_current_end_ = kInvalidPC;
  DCHECK(!l->is_bound());
  if (l->is_linked()) {
    int pos = l->pos();
    while (pos != 0) {
      int fixup = pos;
      pos = *reinterpret_cast<int32_t*>(buffer_.data() + fixup);
      *reinterpret_cast<uint32_t*>(buffer_.data() + fixup) = pc_;
      jump_edges_.emplace(fixup, pc_);
    }
  }
  l->bind_to(pc_);
}

void RegExpBytecodeGenerator::EmitOrLink(Label* l) {
  if (l == nullptr) l = &backtrack_;
  int pos = 0;
  if (l->is_bound()) {
    pos = l->pos();
    jump_edges_.emplace(pc_, pos);
  } else {
    if (l->is_linked()) {
      pos = l->pos();
    }
    l->link_to(pc_);
  }
  Emit32(pos);
}

void RegExpBytecodeGenerator::PopRegister(int register_index) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_POP_REGISTER, register_index);
}

void RegExpBytecodeGenerator::PushRegister(int register_index,
                                           StackCheckFlag check_stack_limit) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_PUSH_REGISTER, register_index);
}

void RegExpBytecodeGenerator::WriteCurrentPositionToRegister(int register_index,
                                                             int cp_offset) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_SET_REGISTER_TO_CP, register_index);
  Emit32(cp_offset);  // Current position offset.
}

void RegExpBytecodeGenerator::ClearRegisters(int reg_from, int reg_to) {
  DCHECK(reg_from <= reg_to);
  for (int reg = reg_from; reg <= reg_to; reg++) {
    SetRegister(reg, -1);
  }
}

void RegExpBytecodeGenerator::ReadCurrentPositionFromRegister(
    int register_index) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_SET_CP_TO_REGISTER, register_index);
}

void RegExpBytecodeGenerator::WriteStackPointerToRegister(int register_index) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_SET_REGISTER_TO_SP, register_index);
}

void RegExpBytecodeGenerator::ReadStackPointerFromRegister(int register_index) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_SET_SP_TO_REGISTER, register_index);
}

void RegExpBytecodeGenerator::SetCurrentPositionFromEnd(int by) {
  DCHECK(is_uint24(by));
  Emit(BC_SET_CURRENT_POSITION_FROM_END, by);
}

void RegExpBytecodeGenerator::SetRegister(int register_index, int to) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_SET_REGISTER, register_index);
  Emit32(to);
}

void RegExpBytecodeGenerator::AdvanceRegister(int register_index, int by) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_ADVANCE_REGISTER, register_index);
  Emit32(by);
}

void RegExpBytecodeGenerator::PopCurrentPosition() { Emit(BC_POP_CP, 0); }

void RegExpBytecodeGenerator::PushCurrentPosition() { Emit(BC_PUSH_CP, 0); }

void RegExpBytecodeGenerator::Backtrack() {
  int error_code =
      can_fallback() ? RegExp::RE_FALLBACK_TO_EXPERIMENTAL : RegExp::RE_FAILURE;
  Emit(BC_POP_BT, error_code);
}

void RegExpBytecodeGenerator::GoTo(Label* l) {
  if (advance_current_end_ == pc_) {
    // Combine advance current and goto.
    pc_ = advance_current_start_;
    Emit(BC_ADVANCE_CP_AND_GOTO, advance_current_offset_);
    EmitOrLink(l);
    advance_current_end_ = kInvalidPC;
  } else {
    // Regular goto.
    Emit(BC_GOTO, 0);
    EmitOrLink(l);
  }
}

void RegExpBytecodeGenerator::PushBacktrack(Label* l) {
  Emit(BC_PUSH_BT, 0);
  EmitOrLink(l);
}

bool RegExpBytecodeGenerator::Succeed() {
  Emit(BC_SUCCEED, 0);
  return false;  // Restart matching for global regexp not supported.
}

void RegExpBytecodeGenerator::Fail() { Emit(BC_FAIL, 0); }

void RegExpBytecodeGenerator::AdvanceCurrentPosition(int by) {
  // TODO(chromium:1166138): Turn back into DCHECKs once the underlying issue
  // is fixed.
  CHECK_LE(kMinCPOffset, by);
  CHECK_GE(kMaxCPOffset, by);
  advance_current_start_ = pc_;
  advance_current_offset_ = by;
  Emit(BC_ADVANCE_CP, by);
  advance_current_end_ = pc_;
}

void RegExpBytecodeGenerator::CheckGreedyLoop(
    Label* on_tos_equals_current_position) {
  Emit(BC_CHECK_GREEDY, 0);
  EmitOrLink(on_tos_equals_current_position);
}

void RegExpBytecodeGenerator::LoadCurrentCharacterImpl(int cp_offset,
                                                       Label* on_failure,
                                                       bool check_bounds,
                                                       int characters,
                                                       int eats_at_least) {
  DCHECK_GE(eats_at_least, characters);
  if (eats_at_least > characters && check_bounds) {
    DCHECK(is_int24(cp_offset + eats_at_least));
    Emit(BC_CHECK_CURRENT_POSITION, cp_offset + eats_at_least);
    EmitOrLink(on_failure);
    check_bounds = false;  // Load below doesn't need to check.
  }

  DCHECK_LE(kMinCPOffset, cp_offset);
  DCHECK_GE(kMaxCPOffset, cp_offset);
  int bytecode;
  if (check_bounds) {
    if (characters == 4) {
      bytecode = BC_LOAD_4_CURRENT_CHARS;
    } else if (characters == 2) {
      bytecode = BC_LOAD_2_CURRENT_CHARS;
    } else {
      DCHECK_EQ(1, characters);
      bytecode = BC_LOAD_CURRENT_CHAR;
    }
  } else {
    if (characters == 4) {
      bytecode = BC_LOAD_4_CURRENT_CHARS_UNCHECKED;
    } else if (characters == 2) {
      bytecode = BC_LOAD_2_CURRENT_CHARS_UNCHECKED;
    } else {
      DCHECK_EQ(1, characters);
      bytecode = BC_LOAD_CURRENT_CHAR_UNCHECKED;
    }
  }
  Emit(bytecode, cp_offset);
  if (check_bounds) EmitOrLink(on_failure);
}

void RegExpBytecodeGenerator::CheckCharacterLT(base::uc16 limit,
                                               Label* on_less) {
  Emit(BC_CHECK_LT, limit);
  EmitOrLink(on_less);
}

void RegExpBytecodeGenerator::CheckCharacterGT(base::uc16 limit,
                                               Label* on_greater) {
  Emit(BC_CHECK_GT, limit);
  EmitOrLink(on_greater);
}

void RegExpBytecodeGenerator::CheckCharacter(uint32_t c, Label* on_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit(BC_CHECK_4_CHARS, 0);
    Emit32(c);
  } else {
    Emit(BC_CHECK_CHAR, c);
  }
  EmitOrLink(on_equal);
}

void RegExpBytecodeGenerator::CheckAtStart(int cp_offset, Label* on_at_start) {
  Emit(BC_CHECK_AT_START, cp_offset);
  EmitOrLink(on_at_start);
}

void RegExpBytecodeGenerator::CheckNotAtStart(int cp_offset,
                                              Label* on_not_at_start) {
  Emit(BC_CHECK_NOT_AT_START, cp_offset);
  EmitOrLink(on_not_at_start);
}

void RegExpBytecodeGenerator::CheckNotCharacter(uint32_t c,
                                                Label* on_not_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit(BC_CHECK_NOT_4_CHARS, 0);
    Emit32(c);
  } else {
    Emit(BC_CHECK_NOT_CHAR, c);
  }
  EmitOrLink(on_not_equal);
}

void RegExpBytecodeGenerator::CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                                                     Label* on_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit(BC_AND_CHECK_4_CHARS, 0);
    Emit32(c);
  } else {
    Emit(BC_AND_CHECK_CHAR, c);
  }
  Emit32(mask);
  EmitOrLink(on_equal);
}

void RegExpBytecodeGenerator::CheckNotCharacterAfterAnd(uint32_t c,
                                                        uint32_t mask,
                                                        Label* on_not_equal) {
  if (c > MAX_FIRST_ARG) {
    Emit(BC_AND_CHECK_NOT_4_CHARS, 0);
    Emit32(c);
  } else {
    Emit(BC_AND_CHECK_NOT_CHAR, c);
  }
  Emit32(mask);
  EmitOrLink(on_not_equal);
}

void RegExpBytecodeGenerator::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  Emit(BC_MINUS_AND_CHECK_NOT_CHAR, c);
  Emit16(minus);
  Emit16(mask);
  EmitOrLink(on_not_equal);
}

void RegExpBytecodeGenerator::CheckCharacterInRange(base::uc16 from,
                                                    base::uc16 to,
                                                    Label* on_in_range) {
  Emit(BC_CHECK_CHAR_IN_RANGE, 0);
  Emit16(from);
  Emit16(to);
  EmitOrLink(on_in_range);
}

void RegExpBytecodeGenerator::CheckCharacterNotInRange(base::uc16 from,
                                                       base::uc16 to,
                                                       Label* on_not_in_range) {
  Emit(BC_CHECK_CHAR_NOT_IN_RANGE, 0);
  Emit16(from);
  Emit16(to);
  EmitOrLink(on_not_in_range);
}

void RegExpBytecodeGenerator::CheckBitInTable(Handle<ByteArray> table,
                                              Label* on_bit_set) {
  Emit(BC_CHECK_BIT_IN_TABLE, 0);
  EmitOrLink(on_bit_set);
  for (int i = 0; i < kTableSize; i += kBitsPerByte) {
    int byte = 0;
    for (int j = 0; j < kBitsPerByte; j++) {
      if (table->get(i + j) != 0) byte |= 1 << j;
    }
    Emit8(byte);
  }
}

void RegExpBytecodeGenerator::CheckNotBackReference(int start_reg,
                                                    bool read_backward,
                                                    Label* on_not_equal) {
  DCHECK_LE(0, start_reg);
  DCHECK_GE(kMaxRegister, start_reg);
  Emit(read_backward ? BC_CHECK_NOT_BACK_REF_BACKWARD : BC_CHECK_NOT_BACK_REF,
       start_reg);
  EmitOrLink(on_not_equal);
}

void RegExpBytecodeGenerator::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_not_equal) {
  DCHECK_LE(0, start_reg);
  DCHECK_GE(kMaxRegister, start_reg);
  Emit(read_backward ? (unicode ? BC_CHECK_NOT_BACK_REF_NO_CASE_UNICODE_BACKWARD
                                : BC_CHECK_NOT_BACK_REF_NO_CASE_BACKWARD)
                     : (unicode ? BC_CHECK_NOT_BACK_REF_NO_CASE_UNICODE
                                : BC_CHECK_NOT_BACK_REF_NO_CASE),
       start_reg);
  EmitOrLink(on_not_equal);
}

void RegExpBytecodeGenerator::IfRegisterLT(int register_index, int comparand,
                                           Label* on_less_than) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_CHECK_REGISTER_LT, register_index);
  Emit32(comparand);
  EmitOrLink(on_less_than);
}

void RegExpBytecodeGenerator::IfRegisterGE(int register_index, int comparand,
                                           Label* on_greater_or_equal) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_CHECK_REGISTER_GE, register_index);
  Emit32(comparand);
  EmitOrLink(on_greater_or_equal);
}

void RegExpBytecodeGenerator::IfRegisterEqPos(int register_index,
                                              Label* on_eq) {
  DCHECK_LE(0, register_index);
  DCHECK_GE(kMaxRegister, register_index);
  Emit(BC_CHECK_REGISTER_EQ_POS, register_index);
  EmitOrLink(on_eq);
}

Handle<HeapObject> RegExpBytecodeGenerator::GetCode(Handle<String> source) {
  Bind(&backtrack_);
  Backtrack();

  Handle<ByteArray> array;
  if (v8_flags.regexp_peephole_optimization) {
    array = RegExpBytecodePeepholeOptimization::OptimizeBytecode(
        isolate_, zone(), source, buffer_.data(), length(), jump_edges_);
  } else {
    array = isolate_->factory()->NewByteArray(length());
    Copy(array->begin());
  }

  return array;
}

int RegExpBytecodeGenerator::length() { return pc_; }

void RegExpBytecodeGenerator::Copy(uint8_t* a) {
  MemCopy(a, buffer_.data(), length());
}

void RegExpBytecodeGenerator::ExpandBuffer() {
  // TODO(jgruber): The growth strategy could be smarter for large sizes.
  // TODO(jgruber): It's not necessary to default-initialize new elements.
  buffer_.resize(buffer_.size() * 2);
}

}  // namespace internal
}  // namespace v8
