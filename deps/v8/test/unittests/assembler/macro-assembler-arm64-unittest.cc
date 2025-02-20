// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/arm64/macro-assembler-arm64-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "src/utils/ostreams.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {

#define __ masm.

const char* ErrorMessage(const char* msg) {
// If we are running on android and the output is not redirected (i.e. ends up
// in the android log) then we cannot find the error message in the output. This
// macro just returns the empty string in that case.
#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
  return "";
#else
  // We only print the abort reason if debug code is enabled. Otherwise we just
  // stop execution. Return an empty string in that case.
  return v8_flags.debug_code ? msg : "";
#endif
}

// Test the x64 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.

class MacroAssemblerTest : public TestWithIsolate {};

TEST_F(MacroAssemblerTest, TestHardAbort) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  {
    AssemblerBufferWriteScope rw_scope(*buffer);

    __ CodeEntry();

    __ Abort(AbortReason::kNoReason);

    CodeDesc desc;
    masm.GetCode(isolate(), &desc);
  }
  // We need an isolate here to execute in the simulator.
  auto f = GeneratedCode<void>::FromBuffer(isolate(), buffer->start());

  ASSERT_DEATH_IF_SUPPORTED({ f.Call(); }, ErrorMessage("abort: no reason"));
}

TEST_F(MacroAssemblerTest, TestCheck) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  {
    AssemblerBufferWriteScope rw_scope(*buffer);

    __ CodeEntry();

    // Fail if the first parameter is 17.
    __ Mov(w1, Immediate(17));
    __ Cmp(w0, w1);  // 1st parameter is in {w0}.
    __ Check(Condition::ne, AbortReason::kNoReason);
    __ Ret();

    CodeDesc desc;
    masm.GetCode(isolate(), &desc);
  }
  // We need an isolate here to execute in the simulator.
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
  f.Call(18);
  ASSERT_DEATH_IF_SUPPORTED({ f.Call(17); }, ErrorMessage("abort: no reason"));
}

TEST_F(MacroAssemblerTest, CompareAndBranch) {
  const int kTestCases[] = {-42, 0, 42};
  static_assert(Condition::eq == 0);
  static_assert(Condition::le == 13);
  TRACED_FORRANGE(int, cc, 0, 13) {  // All conds except al and nv
    Condition cond = static_cast<Condition>(cc);
    TRACED_FOREACH(int, imm, kTestCases) {
      auto buffer = AllocateAssemblerBuffer();
      MacroAssembler masm(isolate(), AssemblerOptions{},
                          CodeObjectRequired::kNo, buffer->CreateView());
      __ set_root_array_available(false);
      __ set_abort_hard(true);

      {
        AssemblerBufferWriteScope rw_scope(*buffer);

        __ CodeEntry();

        Label start, lab;
        __ Bind(&start);
        __ CompareAndBranch(x0, Immediate(imm), cond, &lab);
        if (imm == 0 &&
            ((cond == eq) || (cond == ne) || (cond == hi) || (cond == ls) ||
             (cond == lt) || (cond == ge))) {  // One instruction generated
          ASSERT_EQ(kInstrSize, __ SizeOfCodeGeneratedSince(&start));
        } else {  // Two instructions generated
          ASSERT_EQ(static_cast<uint8_t>(2 * kInstrSize),
                    __ SizeOfCodeGeneratedSince(&start));
        }
        __ Cmp(x0, Immediate(imm));
        __ Check(NegateCondition(cond),
                 AbortReason::kNoReason);  // cond must not hold
        __ Ret();
        __ Bind(&lab);  // Branch leads here
        __ Cmp(x0, Immediate(imm));
        __ Check(cond, AbortReason::kNoReason);  // cond must hold
        __ Ret();

        CodeDesc desc;
        masm.GetCode(isolate(), &desc);
      }
      // We need an isolate here to execute in the simulator.
      auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

      TRACED_FOREACH(int, n, kTestCases) { f.Call(n); }
    }
  }
}

struct MoveObjectAndSlotTestCase {
  const char* comment;
  Register dst_object;
  Register dst_slot;
  Register object;
  Register offset_register = no_reg;
};

const MoveObjectAndSlotTestCase kMoveObjectAndSlotTestCases[] = {
    {"no overlap", x0, x1, x2},
    {"no overlap", x0, x1, x2, x3},

    {"object == dst_object", x2, x1, x2},
    {"object == dst_object", x2, x1, x2, x3},

    {"object == dst_slot", x1, x2, x2},
    {"object == dst_slot", x1, x2, x2, x3},

    {"offset == dst_object", x0, x1, x2, x0},

    {"offset == dst_object && object == dst_slot", x0, x1, x1, x0},

    {"offset == dst_slot", x0, x1, x2, x1},

    {"offset == dst_slot && object == dst_object", x0, x1, x0, x1}};

// Make sure we include offsets that cannot be encoded in an add instruction.
const int kOffsets[] = {0, 42, kMaxRegularHeapObjectSize, 0x101001};

template <typename T>
class MacroAssemblerTestWithParam : public MacroAssemblerTest,
                                    public ::testing::WithParamInterface<T> {};

using MacroAssemblerTestMoveObjectAndSlot =
    MacroAssemblerTestWithParam<MoveObjectAndSlotTestCase>;

TEST_P(MacroAssemblerTestMoveObjectAndSlot, MoveObjectAndSlot) {
  const MoveObjectAndSlotTestCase test_case = GetParam();
  TRACED_FOREACH(int32_t, offset, kOffsets) {
    auto buffer = AllocateAssemblerBuffer();
    MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                        buffer->CreateView());

    {
      AssemblerBufferWriteScope rw_buffer_scope(*buffer);

      __ CodeEntry();
      __ Push(x0, padreg);
      __ Mov(test_case.object, x1);

      Register src_object = test_case.object;
      Register dst_object = test_case.dst_object;
      Register dst_slot = test_case.dst_slot;

      Operand offset_operand(0);
      if (test_case.offset_register == no_reg) {
        offset_operand = Operand(offset);
      } else {
        __ Mov(test_case.offset_register, Operand(offset));
        offset_operand = Operand(test_case.offset_register);
      }

      std::stringstream comment;
      comment << "-- " << test_case.comment << ": MoveObjectAndSlot("
              << dst_object << ", " << dst_slot << ", " << src_object << ", ";
      if (test_case.offset_register == no_reg) {
        comment << "#" << offset;
      } else {
        comment << test_case.offset_register;
      }
      comment << ") --";
      __ RecordComment(comment.str().c_str());
      __ MoveObjectAndSlot(dst_object, dst_slot, src_object, offset_operand);
      __ RecordComment("--");

      // The `result` pointer was saved on the stack.
      UseScratchRegisterScope temps(&masm);
      Register scratch = temps.AcquireX();
      __ Pop(padreg, scratch);
      __ Str(dst_object, MemOperand(scratch));
      __ Str(dst_slot, MemOperand(scratch, kSystemPointerSize));

      __ Ret();

      CodeDesc desc;
      masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);
      if (v8_flags.print_code) {
        Handle<Code> code =
            Factory::CodeBuilder(isolate(), desc, CodeKind::FOR_TESTING)
                .Build();
        StdoutStream os;
        Print(*code, os);
      }
    }

    // We need an isolate here to execute in the simulator.
    auto f = GeneratedCode<void, uint8_t**, uint8_t*>::FromBuffer(
        isolate(), buffer->start());

    uint8_t* object = new uint8_t[offset];
    uint8_t* result[] = {nullptr, nullptr};

    f.Call(result, object);

    // The first element must be the address of the object, and the second the
    // slot addressed by `offset`.
    EXPECT_EQ(result[0], &object[0]);
    EXPECT_EQ(result[1], &object[offset]);

    delete[] object;
  }
}

INSTANTIATE_TEST_SUITE_P(MacroAssemblerTest,
                         MacroAssemblerTestMoveObjectAndSlot,
                         ::testing::ValuesIn(kMoveObjectAndSlotTestCases));

#undef __

}  // namespace internal
}  // namespace v8
