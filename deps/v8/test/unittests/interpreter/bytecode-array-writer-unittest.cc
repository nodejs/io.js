// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include "src/api/api.h"
#include "src/codegen/source-position-table.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/interpreter/bytecode-array-writer.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-node.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecode-source-info.h"
#include "src/interpreter/constant-array-builder.h"
#include "src/utils/utils.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/interpreter/bytecode-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {
namespace bytecode_array_writer_unittest {

#define B(Name) static_cast<uint8_t>(Bytecode::k##Name)
#define R(i) static_cast<uint32_t>(Register(i).ToOperand())

class BytecodeArrayWriterUnittest : public TestWithIsolateAndZone {
 public:
  BytecodeArrayWriterUnittest()
      : constant_array_builder_(zone()),
        bytecode_array_writer_(
            zone(), &constant_array_builder_,
            SourcePositionTableBuilder::RECORD_SOURCE_POSITIONS) {}
  ~BytecodeArrayWriterUnittest() override = default;

  void Write(Bytecode bytecode, BytecodeSourceInfo info = BytecodeSourceInfo());
  void Write(Bytecode bytecode, uint32_t operand0,
             BytecodeSourceInfo info = BytecodeSourceInfo());
  void Write(Bytecode bytecode, uint32_t operand0, uint32_t operand1,
             BytecodeSourceInfo info = BytecodeSourceInfo());
  void Write(Bytecode bytecode, uint32_t operand0, uint32_t operand1,
             uint32_t operand2, BytecodeSourceInfo info = BytecodeSourceInfo());
  void Write(Bytecode bytecode, uint32_t operand0, uint32_t operand1,
             uint32_t operand2, uint32_t operand3,
             BytecodeSourceInfo info = BytecodeSourceInfo());

  void WriteJump(Bytecode bytecode, BytecodeLabel* label,
                 BytecodeSourceInfo info = BytecodeSourceInfo());
  void WriteJump(Bytecode bytecode, BytecodeLabel* label, uint32_t operand1,
                 uint32_t operand2,
                 BytecodeSourceInfo info = BytecodeSourceInfo());
  void WriteJumpLoop(Bytecode bytecode, BytecodeLoopHeader* loop_header,
                     int depth, int feedback_index,
                     BytecodeSourceInfo info = BytecodeSourceInfo());

  BytecodeArrayWriter* writer() { return &bytecode_array_writer_; }
  ZoneVector<unsigned char>* bytecodes() { return writer()->bytecodes(); }
  SourcePositionTableBuilder* source_position_table_builder() {
    return writer()->source_position_table_builder();
  }

 private:
  ConstantArrayBuilder constant_array_builder_;
  BytecodeArrayWriter bytecode_array_writer_;
};

void BytecodeArrayWriterUnittest::Write(Bytecode bytecode,
                                        BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, info);
  writer()->Write(&node);
}

void BytecodeArrayWriterUnittest::Write(Bytecode bytecode, uint32_t operand0,
                                        BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, operand0, info);
  writer()->Write(&node);
}

void BytecodeArrayWriterUnittest::Write(Bytecode bytecode, uint32_t operand0,
                                        uint32_t operand1,
                                        BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, operand0, operand1, info);
  writer()->Write(&node);
}

void BytecodeArrayWriterUnittest::Write(Bytecode bytecode, uint32_t operand0,
                                        uint32_t operand1, uint32_t operand2,
                                        BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, operand0, operand1, operand2, info);
  writer()->Write(&node);
}

void BytecodeArrayWriterUnittest::Write(Bytecode bytecode, uint32_t operand0,
                                        uint32_t operand1, uint32_t operand2,
                                        uint32_t operand3,
                                        BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, operand0, operand1, operand2, operand3, info);
  writer()->Write(&node);
}

void BytecodeArrayWriterUnittest::WriteJump(Bytecode bytecode,
                                            BytecodeLabel* label,
                                            BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, 0, info);
  writer()->WriteJump(&node, label);
}

void BytecodeArrayWriterUnittest::WriteJump(Bytecode bytecode,
                                            BytecodeLabel* label,
                                            uint32_t operand1,
                                            uint32_t operand2,
                                            BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, 0, operand1, operand2, info);
  writer()->WriteJump(&node, label);
}

void BytecodeArrayWriterUnittest::WriteJumpLoop(Bytecode bytecode,
                                                BytecodeLoopHeader* loop_header,
                                                int depth, int feedback_index,
                                                BytecodeSourceInfo info) {
  BytecodeNode node(bytecode, 0, depth, feedback_index, info);
  writer()->WriteJumpLoop(&node, loop_header);
}

TEST_F(BytecodeArrayWriterUnittest, SimpleExample) {
  CHECK_EQ(bytecodes()->size(), 0u);

  Write(Bytecode::kLdaSmi, 127, {55, true});
  CHECK_EQ(bytecodes()->size(), 2u);

  Write(Bytecode::kStar, Register(20).ToOperand());
  CHECK_EQ(bytecodes()->size(), 4u);

  Write(Bytecode::kLdar, Register(200).ToOperand());
  CHECK_EQ(bytecodes()->size(), 8u);

  Write(Bytecode::kReturn, {70, true});
  CHECK_EQ(bytecodes()->size(), 9u);

  static const uint8_t expected_bytes[] = {
      // clang-format off
      /*  0 55 S> */ B(LdaSmi), U8(127),
      /*  2       */ B(Star), R8(20),
      /*  4       */ B(Wide), B(Ldar), R16(200),
      /*  8 70 S> */ B(Return),
      // clang-format on
  };
  CHECK_EQ(bytecodes()->size(), arraysize(expected_bytes));
  for (size_t i = 0; i < arraysize(expected_bytes); ++i) {
    CHECK_EQ(bytecodes()->at(i), expected_bytes[i]);
  }

  DirectHandle<BytecodeArray> bytecode_array = writer()->ToBytecodeArray(
      isolate(), 0, 0, 0, factory()->empty_trusted_byte_array());
  bytecode_array->set_source_position_table(
      *writer()->ToSourcePositionTable(isolate()), kReleaseStore);
  CHECK_EQ(bytecodes()->size(), arraysize(expected_bytes));

  PositionTableEntry expected_positions[] = {{0, 55, true}, {8, 70, true}};
  SourcePositionTableIterator source_iterator(
      bytecode_array->SourcePositionTable());
  for (size_t i = 0; i < arraysize(expected_positions); ++i) {
    const PositionTableEntry& expected = expected_positions[i];
    CHECK_EQ(source_iterator.code_offset(), expected.code_offset);
    CHECK_EQ(source_iterator.source_position().ScriptOffset(),
             expected.source_position);
    CHECK_EQ(source_iterator.is_statement(), expected.is_statement);
    source_iterator.Advance();
  }
  CHECK(source_iterator.done());
}

TEST_F(BytecodeArrayWriterUnittest, ComplexExample) {
  static const uint8_t expected_bytes[] = {
      // clang-format off
      /*  0 42 S> */ B(LdaConstant), U8(0),
      /*  2 42 E> */ B(Add), R8(1), U8(1),
      /*  4 68 S> */ B(JumpIfUndefined), U8(36),
      /*  6       */ B(JumpIfNull), U8(34),
      /*  8       */ B(ToObject), R8(3),
      /* 10       */ B(ForInPrepare), R8(3), U8(4),
      /* 13       */ B(LdaZero),
      /* 14       */ B(Star), R8(7),
      /* 16 63 S> */ B(JumpIfForInDone), U8(24), R8(7), R8(6),
      /* 21       */ B(ForInNext), R8(3), R8(7), R8(4), U8(1),
      /* 26       */ B(JumpIfUndefined), U8(9),
      /* 28       */ B(Star), R8(0),
      /* 30       */ B(Ldar), R8(0),
      /* 32       */ B(Star), R8(2),
      /* 34 85 S> */ B(Return),
      /* 35       */ B(ForInStep), R8(7),
      /* 39       */ B(JumpLoop), U8(20), U8(0), U8(0),
      /* 43       */ B(LdaUndefined),
      /* 44 85 S> */ B(Return),
      // clang-format on
  };

  static const PositionTableEntry expected_positions[] = {
      {0, 42, true},  {2, 42, false}, {5, 68, true},
      {17, 63, true}, {34, 85, true}, {42, 85, true}};

  BytecodeLoopHeader loop_header;
  BytecodeLabel jump_for_in, jump_end_1, jump_end_2, jump_end_3;

  Write(Bytecode::kLdaConstant, U8(0), {42, true});
  Write(Bytecode::kAdd, R(1), U8(1), {42, false});
  WriteJump(Bytecode::kJumpIfUndefined, &jump_end_1, {68, true});
  WriteJump(Bytecode::kJumpIfNull, &jump_end_2);
  Write(Bytecode::kToObject, R(3));
  Write(Bytecode::kForInPrepare, R(3), U8(4));
  Write(Bytecode::kLdaZero);
  Write(Bytecode::kStar, R(7));
  writer()->BindLoopHeader(&loop_header);
  WriteJump(Bytecode::kJumpIfForInDone, &jump_end_3, R(7), R(6), {63, true});
  Write(Bytecode::kForInNext, R(3), R(7), R(4), U8(1));
  WriteJump(Bytecode::kJumpIfUndefined, &jump_for_in);
  Write(Bytecode::kStar, R(0));
  Write(Bytecode::kLdar, R(0));
  Write(Bytecode::kStar, R(2));
  Write(Bytecode::kReturn, {85, true});
  writer()->BindLabel(&jump_for_in);
  Write(Bytecode::kForInStep, R(7));
  WriteJumpLoop(Bytecode::kJumpLoop, &loop_header, 0, 0);
  writer()->BindLabel(&jump_end_1);
  writer()->BindLabel(&jump_end_2);
  writer()->BindLabel(&jump_end_3);
  Write(Bytecode::kLdaUndefined);
  Write(Bytecode::kReturn, {85, true});

  CHECK_EQ(bytecodes()->size(), arraysize(expected_bytes));
  for (size_t i = 0; i < arraysize(expected_bytes); ++i) {
    CHECK_EQ(static_cast<int>(bytecodes()->at(i)),
             static_cast<int>(expected_bytes[i]));
  }

  DirectHandle<BytecodeArray> bytecode_array = writer()->ToBytecodeArray(
      isolate(), 0, 0, 0, factory()->empty_trusted_byte_array());
  bytecode_array->set_source_position_table(
      *writer()->ToSourcePositionTable(isolate()), kReleaseStore);
  SourcePositionTableIterator source_iterator(
      bytecode_array->SourcePositionTable());
  for (size_t i = 0; i < arraysize(expected_positions); ++i) {
    const PositionTableEntry& expected = expected_positions[i];
    CHECK_EQ(source_iterator.code_offset(), expected.code_offset);
    CHECK_EQ(source_iterator.source_position().ScriptOffset(),
             expected.source_position);
    CHECK_EQ(source_iterator.is_statement(), expected.is_statement);
    source_iterator.Advance();
  }
  CHECK(source_iterator.done());
}

TEST_F(BytecodeArrayWriterUnittest, ElideNoneffectfulBytecodes) {
  if (!i::v8_flags.ignition_elide_noneffectful_bytecodes) return;

  static const uint8_t expected_bytes[] = {
      // clang-format off
      /*  0  55 S> */ B(Ldar), R8(20),
      /*  2        */ B(Star), R8(20),
      /*  4        */ B(CreateMappedArguments),
      /*  5  60 S> */ B(LdaSmi), U8(127),
      /*  7  70 S> */ B(Ldar), R8(20),
      /*  9 75 S> */ B(Return),
      // clang-format on
  };

  static const PositionTableEntry expected_positions[] = {
      {0, 55, true}, {5, 60, false}, {7, 70, true}, {9, 75, true}};

  Write(Bytecode::kLdaSmi, 127, {55, true});  // Should be elided.
  Write(Bytecode::kLdar, Register(20).ToOperand());
  Write(Bytecode::kStar, Register(20).ToOperand());
  Write(Bytecode::kLdar, Register(20).ToOperand());  // Should be elided.
  Write(Bytecode::kCreateMappedArguments);
  Write(Bytecode::kLdaSmi, 127, {60, false});  // Not elided due to source info.
  Write(Bytecode::kLdar, Register(20).ToOperand(), {70, true});
  Write(Bytecode::kReturn, {75, true});

  CHECK_EQ(bytecodes()->size(), arraysize(expected_bytes));
  for (size_t i = 0; i < arraysize(expected_bytes); ++i) {
    CHECK_EQ(static_cast<int>(bytecodes()->at(i)),
             static_cast<int>(expected_bytes[i]));
  }

  DirectHandle<BytecodeArray> bytecode_array = writer()->ToBytecodeArray(
      isolate(), 0, 0, 0, factory()->empty_trusted_byte_array());
  bytecode_array->set_source_position_table(
      *writer()->ToSourcePositionTable(isolate()), kReleaseStore);
  SourcePositionTableIterator source_iterator(
      bytecode_array->SourcePositionTable());
  for (size_t i = 0; i < arraysize(expected_positions); ++i) {
    const PositionTableEntry& expected = expected_positions[i];
    CHECK_EQ(source_iterator.code_offset(), expected.code_offset);
    CHECK_EQ(source_iterator.source_position().ScriptOffset(),
             expected.source_position);
    CHECK_EQ(source_iterator.is_statement(), expected.is_statement);
    source_iterator.Advance();
  }
  CHECK(source_iterator.done());
}

TEST_F(BytecodeArrayWriterUnittest, DeadcodeElimination) {
  static const uint8_t expected_bytes[] = {
      // clang-format off
      /*  0  55 S> */ B(LdaSmi), U8(127),
      /*  2        */ B(Jump), U8(2),
      /*  4  65 S> */ B(LdaSmi), U8(127),
      /*  6        */ B(JumpIfFalse), U8(3),
      /*  8  75 S> */ B(Return),
      /*  9       */ B(JumpIfFalse), U8(3),
      /*  11       */ B(Throw),
      /*  12       */ B(JumpIfFalse), U8(3),
      /*  14       */ B(ReThrow),
      /*  15       */ B(Return),
      // clang-format on
  };

  static const PositionTableEntry expected_positions[] = {
      {0, 55, true}, {4, 65, true}, {8, 75, true}};

  BytecodeLabel after_jump, after_conditional_jump, after_return, after_throw,
      after_rethrow;

  Write(Bytecode::kLdaSmi, 127, {55, true});
  WriteJump(Bytecode::kJump, &after_jump);
  Write(Bytecode::kLdaSmi, 127);                               // Dead code.
  WriteJump(Bytecode::kJumpIfFalse, &after_conditional_jump);  // Dead code.
  writer()->BindLabel(&after_jump);
  // We would bind the after_conditional_jump label here, but the jump to it is
  // dead.
  CHECK(!after_conditional_jump.has_referrer_jump());
  Write(Bytecode::kLdaSmi, 127, {65, true});
  WriteJump(Bytecode::kJumpIfFalse, &after_return);
  Write(Bytecode::kReturn, {75, true});
  Write(Bytecode::kLdaSmi, 127, {100, true});  // Dead code.
  writer()->BindLabel(&after_return);
  WriteJump(Bytecode::kJumpIfFalse, &after_throw);
  Write(Bytecode::kThrow);
  Write(Bytecode::kLdaSmi, 127);  // Dead code.
  writer()->BindLabel(&after_throw);
  WriteJump(Bytecode::kJumpIfFalse, &after_rethrow);
  Write(Bytecode::kReThrow);
  Write(Bytecode::kLdaSmi, 127);  // Dead code.
  writer()->BindLabel(&after_rethrow);
  Write(Bytecode::kReturn);

  CHECK_EQ(bytecodes()->size(), arraysize(expected_bytes));
  for (size_t i = 0; i < arraysize(expected_bytes); ++i) {
    CHECK_EQ(static_cast<int>(bytecodes()->at(i)),
             static_cast<int>(expected_bytes[i]));
  }

  DirectHandle<BytecodeArray> bytecode_array = writer()->ToBytecodeArray(
      isolate(), 0, 0, 0, factory()->empty_trusted_byte_array());
  bytecode_array->set_source_position_table(
      *writer()->ToSourcePositionTable(isolate()), kReleaseStore);
  SourcePositionTableIterator source_iterator(
      bytecode_array->SourcePositionTable());
  for (size_t i = 0; i < arraysize(expected_positions); ++i) {
    const PositionTableEntry& expected = expected_positions[i];
    CHECK_EQ(source_iterator.code_offset(), expected.code_offset);
    CHECK_EQ(source_iterator.source_position().ScriptOffset(),
             expected.source_position);
    CHECK_EQ(source_iterator.is_statement(), expected.is_statement);
    source_iterator.Advance();
  }
  CHECK(source_iterator.done());
}

#undef B
#undef R

}  // namespace bytecode_array_writer_unittest
}  // namespace interpreter
}  // namespace internal
}  // namespace v8
