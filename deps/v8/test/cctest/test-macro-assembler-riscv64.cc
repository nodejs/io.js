// Copyright 2021 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include <iostream>

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/simulator.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/test-helper-riscv64.h"
#include "test/common/assembler-tester.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {

const float qnan_f = std::numeric_limits<float>::quiet_NaN();
const float snan_f = std::numeric_limits<float>::signaling_NaN();
const double qnan_d = std::numeric_limits<double>::quiet_NaN();
const double snan_d = std::numeric_limits<double>::signaling_NaN();

const float inf_f = std::numeric_limits<float>::infinity();
const double inf_d = std::numeric_limits<double>::infinity();
const float minf_f = -inf_f;
const double minf_d = -inf_d;

using FV = void*(int64_t x, int64_t y, int p2, int p3, int p4);
using F1 = void*(int x, int p1, int p2, int p3, int p4);
using F3 = void*(void* p, int p1, int p2, int p3, int p4);
using F4 = void*(void* p0, void* p1, int p2, int p3, int p4);

#define __ masm.

static uint64_t run_CalcScaledAddress(uint64_t rt, uint64_t rs, int8_t sa) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto fn = [sa](MacroAssembler& masm) {
    __ CalcScaledAddress(a0, a0, a1, sa);
  };
  auto f = AssembleCode<FV>(isolate, fn);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(rt, rs, 0, 0, 0));

  return res;
}

template <typename VTYPE, typename Func>
VTYPE run_Unaligned(char* memory_buffer, int32_t in_offset, int32_t out_offset,
                    VTYPE value, Func GenerateUnalignedInstructionFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto fn = [in_offset, out_offset,
             GenerateUnalignedInstructionFunc](MacroAssembler& masm) {
    GenerateUnalignedInstructionFunc(masm, in_offset, out_offset);
  };
  auto f = AssembleCode<int32_t(char*)>(isolate, fn);

  MemCopy(memory_buffer + in_offset, &value, sizeof(VTYPE));
  f.Call(memory_buffer);
  VTYPE res;
  MemCopy(&res, memory_buffer + out_offset, sizeof(VTYPE));

  return res;
}

static const std::vector<int32_t> unsigned_test_offset() {
  static const int32_t kValues[] = {// value, offset
                                    -132 * KB, -21 * KB, 0, 19 * KB, 135 * KB};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int32_t> unsigned_test_offset_increment() {
  static const int32_t kValues[] = {-7, -6, -5, -4, -3, -2, -1, 0,
                                    1,  2,  3,  4,  5,  6,  7};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

TEST(LoadConstants) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  int64_t refConstants[64];
  int64_t result[64];

  int64_t mask = 1;
  for (int i = 0; i < 64; i++) {
    refConstants[i] = ~(mask << i);
  }

  auto fn = [&refConstants](MacroAssembler& masm) {
    __ mv(a4, a0);
    for (int i = 0; i < 64; i++) {
      // Load constant.
      __ li(a5, Operand(refConstants[i]));
      __ Sd(a5, MemOperand(a4));
      __ Add64(a4, a4, Operand(kSystemPointerSize));
    }
  };
  auto f = AssembleCode<FV>(isolate, fn);

  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 64; i++) {
    CHECK(refConstants[i] == result[i]);
  }
}

TEST(LoadAddress) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label to_jump, skip;
  __ mv(a4, a0);

  __ Branch(&skip);
  __ bind(&to_jump);
  __ nop();
  __ nop();
  __ jr(ra);
  __ nop();
  __ bind(&skip);
  __ li(a4,
        Operand(masm.jump_address(&to_jump),
                RelocInfo::INTERNAL_REFERENCE_ENCODED),
        ADDRESS_LOAD);
  int check_size = masm.InstructionsGeneratedSince(&skip);
#ifdef RISCV_USE_SV39
  // NOTE (RISCV): current li generates 4 instructions, if the sequence is
  // changed, need to adjust the CHECK_EQ value too
  CHECK_EQ(4, check_size);
#else
  // NOTE (RISCV): current li generates 6 instructions, if the sequence is
  // changed, need to adjust the CHECK_EQ value too
  CHECK_EQ(6, check_size);
#endif
  __ jr(a4);
  __ nop();
  __ stop();
  __ stop();
  __ stop();
  __ stop();
  __ stop();

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<FV>::FromCode(isolate, *code);

  (void)f.Call(0, 0, 0, 0, 0);
  // Check results.
}

TEST(jump_tables4) {
  // Similar to test-assembler-mips jump_tables1, with extra test for branch
  // trampoline required before emission of the dd table (where trampolines are
  // blocked), and proper transition to long-branch mode.
  // Regression test for v8:4294.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 128;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];
  Label near_start, end, done;

  __ Push(ra);
  __ mv(a1, zero_reg);

  __ Branch(&end);
  __ bind(&near_start);

  // Generate slightly less than 32K instructions, which will soon require
  // trampoline for branch distance fixup.
  for (int i = 0; i < 32768 - 256; ++i) {
    __ addi(a1, a1, 1);
  }

  __ GenerateSwitchTable(a0, kNumCases,
                         [&labels](size_t i) { return labels + i; });

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ RV_li(a0, values[i]);
    __ Branch(&done);
  }

  __ bind(&done);
  __ Pop(ra);
  __ jr(ra);

  __ bind(&end);
  __ Branch(&near_start);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    // ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST(jump_tables6) {
  // Similar to test-assembler-mips jump_tables1, with extra test for branch
  // trampoline required after emission of the dd table (where trampolines are
  // blocked). This test checks if number of really generated instructions is
  // greater than number of counted instructions from code, as we are expecting
  // generation of trampoline in this case (when number of kFillInstr
  // instructions is close to 32K)
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kSwitchTableCases = 40;

  const int kMaxBranchOffset = Assembler::kMaxBranchOffset;
  const int kTrampolineSlotsSize = Assembler::kTrampolineSlotsSize;
  const int kSwitchTablePrologueSize = MacroAssembler::kSwitchTablePrologueSize;

  const int kMaxOffsetForTrampolineStart =
      kMaxBranchOffset - 16 * kTrampolineSlotsSize;
  const int kFillInstr = (kMaxOffsetForTrampolineStart / kInstrSize) -
                         (kSwitchTablePrologueSize + 2 * kSwitchTableCases) -
                         20;

  int values[kSwitchTableCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kSwitchTableCases];
  Label near_start, end, done;

  __ Push(ra);
  __ mv(a1, zero_reg);

  int offs1 = masm.pc_offset();
  int gen_insn = 0;

  __ Branch(&end);
  gen_insn += 1;
  __ bind(&near_start);

  // Generate slightly less than 32K instructions, which will soon require
  // trampoline for branch distance fixup.
  for (int i = 0; i < kFillInstr; ++i) {
    __ addi(a1, a1, 1);
  }
  gen_insn += kFillInstr;

  __ GenerateSwitchTable(a0, kSwitchTableCases,
                         [&labels](size_t i) { return labels + i; });
  gen_insn += (kSwitchTablePrologueSize + 2 * kSwitchTableCases);

  for (int i = 0; i < kSwitchTableCases; ++i) {
    __ bind(&labels[i]);
    __ li(a0, Operand(values[i]));
    __ Branch(&done);
  }
  gen_insn += 3 * kSwitchTableCases;

  // If offset from here to first branch instr is greater than max allowed
  // offset for trampoline ...
  CHECK_LT(kMaxOffsetForTrampolineStart, masm.pc_offset() - offs1);
  // ... number of generated instructions must be greater then "gen_insn",
  // as we are expecting trampoline generation
  CHECK_LT(gen_insn, (masm.pc_offset() - offs1) / kInstrSize);

  __ bind(&done);
  __ Pop(ra);
  __ jr(ra);
  __ nop();

  __ bind(&end);
  __ Branch(&near_start);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kSwitchTableCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    // ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST(CalcScaledAddress) {
  CcTest::InitializeVM();
  struct TestCaseLsa {
    int64_t rt;
    int64_t rs;
    uint8_t sa;
    uint64_t expected_res;
  };

  struct TestCaseLsa tc[] = {// rt, rs, sa, expected_res
                             {0x4, 0x1, 1, 0x6},
                             {0x4, 0x1, 2, 0x8},
                             {0x4, 0x1, 3, 0xC},
                             {0x4, 0x1, 4, 0x14},
                             {0x4, 0x1, 5, 0x24},
                             {0x0, 0x1, 1, 0x2},
                             {0x0, 0x1, 2, 0x4},
                             {0x0, 0x1, 3, 0x8},
                             {0x0, 0x1, 4, 0x10},
                             {0x0, 0x1, 5, 0x20},
                             {0x4, 0x0, 1, 0x4},
                             {0x4, 0x0, 2, 0x4},
                             {0x4, 0x0, 3, 0x4},
                             {0x4, 0x0, 4, 0x4},
                             {0x4, 0x0, 5, 0x4},

                             // Shift overflow.
                             {0x4, INT64_MAX, 1, 0x2},
                             {0x4, INT64_MAX >> 1, 2, 0x0},
                             {0x4, INT64_MAX >> 2, 3, 0xFFFFFFFFFFFFFFFC},
                             {0x4, INT64_MAX >> 3, 4, 0xFFFFFFFFFFFFFFF4},
                             {0x4, INT64_MAX >> 4, 5, 0xFFFFFFFFFFFFFFE4},

                             // Signed addition overflow.
                             {INT64_MAX - 1, 0x1, 1, 0x8000000000000000},
                             {INT64_MAX - 3, 0x1, 2, 0x8000000000000000},
                             {INT64_MAX - 7, 0x1, 3, 0x8000000000000000},
                             {INT64_MAX - 15, 0x1, 4, 0x8000000000000000},
                             {INT64_MAX - 31, 0x1, 5, 0x8000000000000000},

                             // Addition overflow.
                             {-2, 0x1, 1, 0x0},
                             {-4, 0x1, 2, 0x0},
                             {-8, 0x1, 3, 0x0},
                             {-16, 0x1, 4, 0x0},
                             {-32, 0x1, 5, 0x0}};

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLsa);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_CalcScaledAddress(tc[i].rt, tc[i].rs, tc[i].sa);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

static const std::vector<uint32_t> cvt_trunc_uint32_test_values() {
  static const uint32_t kValues[] = {0x00000000, 0x00000001, 0x00FFFF00,
                                     0x7FFFFFFF, 0x80000000, 0x80000001,
                                     0x80FFFF00, 0x8FFFFFFF};
  return std::vector<uint32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int32_t> cvt_trunc_int32_test_values() {
  static const int32_t kValues[] = {
      static_cast<int32_t>(0x00000000), static_cast<int32_t>(0x00000001),
      static_cast<int32_t>(0x00FFFF00), static_cast<int32_t>(0x7FFFFFFF),
      static_cast<int32_t>(0x80000000), static_cast<int32_t>(0x80000001),
      static_cast<int32_t>(0x80FFFF00), static_cast<int32_t>(0x8FFFFFFF),
      static_cast<int32_t>(0xFFFFFFFF)};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<uint64_t> cvt_trunc_uint64_test_values() {
  static const uint64_t kValues[] = {
      0x0000000000000000, 0x0000000000000001, 0x0000FFFFFFFF0000,
      0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0x8000000000000001,
      0x8000FFFFFFFF0000, 0x8FFFFFFFFFFFFFFF /*, 0xFFFFFFFFFFFFFFFF*/};
  return std::vector<uint64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int64_t> cvt_trunc_int64_test_values() {
  static const int64_t kValues[] = {static_cast<int64_t>(0x0000000000000000),
                                    static_cast<int64_t>(0x0000000000000001),
                                    static_cast<int64_t>(0x0000FFFFFFFF0000),
                                    // static_cast<int64_t>(0x7FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0x8000000000000000),
                                    static_cast<int64_t>(0x8000000000000001),
                                    static_cast<int64_t>(0x8000FFFFFFFF0000),
                                    static_cast<int64_t>(0x8FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)};
  return std::vector<int64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

#define FOR_INPUTS3(ctype, var, test_vector)    \
  std::vector<ctype> var##_vec = test_vector(); \
  for (ctype var : var##_vec)

#define FOR_INT32_INPUTS3(var, test_vector) \
  FOR_INPUTS3(int32_t, var, test_vector)
#define FOR_INT64_INPUTS3(var, test_vector) \
  FOR_INPUTS3(int64_t, var, test_vector)
#define FOR_UINT32_INPUTS3(var, test_vector) \
  FOR_INPUTS3(uint32_t, var, test_vector)
#define FOR_UINT64_INPUTS3(var, test_vector) \
  FOR_INPUTS3(uint64_t, var, test_vector)

#define FOR_TWO_INPUTS(ctype, var1, var2, test_vector)      \
  std::vector<ctype> var##_vec = test_vector();             \
  std::vector<ctype>::iterator var1;                        \
  std::vector<ctype>::reverse_iterator var2;                \
  for (var1 = var##_vec.begin(), var2 = var##_vec.rbegin(); \
       var1 != var##_vec.end(); ++var1, ++var2)

#define FOR_INT32_TWO_INPUTS(var1, var2, test_vector) \
  FOR_TWO_INPUTS(int32_t, var1, var2, test_vector)

TEST(Cvt_s_uw_Trunc_uw_s) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) {
    __ Cvt_s_uw(fa0, a0);
    __ Trunc_uw_s(a0, fa0);
  };
  FOR_UINT32_INPUTS3(i, cvt_trunc_uint32_test_values) {
    // some integers cannot be represented precisely in float,  input may
    // not directly match the return value of GenAndRunTest
    CHECK_EQ(static_cast<uint32_t>(static_cast<float>(i)),
             GenAndRunTest<uint32_t>(i, fn));
  }
}

TEST(Cvt_s_ul_Trunc_ul_s) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) {
    __ Cvt_s_ul(fa0, a0);
    __ Trunc_ul_s(a0, fa0);
  };
  FOR_UINT64_INPUTS3(i, cvt_trunc_uint64_test_values) {
    CHECK_EQ(static_cast<uint64_t>(static_cast<float>(i)),
             GenAndRunTest<uint64_t>(i, fn));
  }
}

TEST(Cvt_d_ul_Trunc_ul_d) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) {
    __ Cvt_d_ul(fa0, a0);
    __ Trunc_ul_d(a0, fa0);
  };
  FOR_UINT64_INPUTS3(i, cvt_trunc_uint64_test_values) {
    CHECK_EQ(static_cast<uint64_t>(static_cast<double>(i)),
             GenAndRunTest<uint64_t>(i, fn));
  }
}

TEST(cvt_d_l_Trunc_l_d) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) {
    __ fcvt_d_l(fa0, a0);
    __ Trunc_l_d(a0, fa0);
  };
  FOR_INT64_INPUTS3(i, cvt_trunc_int64_test_values) {
    CHECK_EQ(static_cast<int64_t>(static_cast<double>(i)),
             GenAndRunTest<int64_t>(i, fn));
  }
}

TEST(cvt_d_w_Trunc_w_d) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) {
    __ fcvt_d_w(fa0, a0);
    __ Trunc_w_d(a0, fa0);
  };
  FOR_INT32_INPUTS3(i, cvt_trunc_int32_test_values) {
    CHECK_EQ(static_cast<int32_t>(static_cast<double>(i)),
             GenAndRunTest<int32_t>(i, fn));
  }
}

static const std::vector<int64_t> overflow_int64_test_values() {
  static const int64_t kValues[] = {static_cast<int64_t>(0xF000000000000000),
                                    static_cast<int64_t>(0x0000000000000001),
                                    static_cast<int64_t>(0xFF00000000000000),
                                    static_cast<int64_t>(0x0000F00111111110),
                                    static_cast<int64_t>(0x0F00001000000000),
                                    static_cast<int64_t>(0x991234AB12A96731),
                                    static_cast<int64_t>(0xB0FFFF0F0F0F0F01),
                                    static_cast<int64_t>(0x00006FFFFFFFFFFF),
                                    static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)};
  return std::vector<int64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

TEST(OverflowInstructions) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  struct T {
    int64_t lhs;
    int64_t rhs;
    int64_t output_add;
    int64_t output_add2;
    int64_t output_sub;
    int64_t output_sub2;
    int64_t output_mul;
    int64_t output_mul2;
    int64_t overflow_add;
    int64_t overflow_add2;
    int64_t overflow_sub;
    int64_t overflow_sub2;
    int64_t overflow_mul;
    int64_t overflow_mul2;
  } t;

  FOR_INT64_INPUTS3(i, overflow_int64_test_values) {
    FOR_INT64_INPUTS3(j, overflow_int64_test_values) {
      auto ii = i;
      auto jj = j;
      int64_t expected_add, expected_sub;
      int32_t ii32 = static_cast<int32_t>(ii);
      int32_t jj32 = static_cast<int32_t>(jj);
      int32_t expected_mul;
      int64_t expected_add_ovf, expected_sub_ovf, expected_mul_ovf;

      auto fn = [](MacroAssembler& masm) {
        __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
        __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));

        __ AddOverflow64(t2, t0, Operand(t1), a1);
        __ Sd(t2, MemOperand(a0, offsetof(T, output_add)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_add)));
        __ mv(a1, zero_reg);
        __ AddOverflow64(t0, t0, Operand(t1), a1);
        __ Sd(t0, MemOperand(a0, offsetof(T, output_add2)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_add2)));

        __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
        __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));

        __ SubOverflow64(t2, t0, Operand(t1), a1);
        __ Sd(t2, MemOperand(a0, offsetof(T, output_sub)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_sub)));
        __ mv(a1, zero_reg);
        __ SubOverflow64(t0, t0, Operand(t1), a1);
        __ Sd(t0, MemOperand(a0, offsetof(T, output_sub2)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_sub2)));

        __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
        __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));
        __ SignExtendWord(t0, t0);
        __ SignExtendWord(t1, t1);
        __ MulOverflow32(t2, t0, Operand(t1), a1);
        __ Sd(t2, MemOperand(a0, offsetof(T, output_mul)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_mul)));
        __ mv(a1, zero_reg);
        __ MulOverflow32(t0, t0, Operand(t1), a1);
        __ Sd(t0, MemOperand(a0, offsetof(T, output_mul2)));
        __ Sd(a1, MemOperand(a0, offsetof(T, overflow_mul2)));
      };
      auto f = AssembleCode<F3>(isolate, fn);

      t.lhs = ii;
      t.rhs = jj;
      f.Call(&t, 0, 0, 0, 0);

      expected_add_ovf = base::bits::SignedAddOverflow64(ii, jj, &expected_add);
      expected_sub_ovf = base::bits::SignedSubOverflow64(ii, jj, &expected_sub);
      expected_mul_ovf =
          base::bits::SignedMulOverflow32(ii32, jj32, &expected_mul);

      CHECK_EQ(expected_add_ovf, t.overflow_add < 0);
      CHECK_EQ(expected_sub_ovf, t.overflow_sub < 0);
      CHECK_EQ(expected_mul_ovf, t.overflow_mul != 0);

      CHECK_EQ(t.overflow_add, t.overflow_add2);
      CHECK_EQ(t.overflow_sub, t.overflow_sub2);
      CHECK_EQ(t.overflow_mul, t.overflow_mul2);

      CHECK_EQ(expected_add, t.output_add);
      CHECK_EQ(expected_add, t.output_add2);
      CHECK_EQ(expected_sub, t.output_sub);
      CHECK_EQ(expected_sub, t.output_sub2);
      if (!expected_mul_ovf) {
        CHECK_EQ(expected_mul, t.output_mul);
        CHECK_EQ(expected_mul, t.output_mul2);
      }
    }
  }
}

TEST(min_max_nan) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct TestFloat {
    double a;
    double b;
    double c;
    double d;
    float e;
    float f;
    float g;
    float h;
  } test;

  const int kTableLength = 13;

  double inputsa[kTableLength] = {2.0,   3.0,    -0.0,  0.0,    42.0,
                                  inf_d, minf_d, inf_d, qnan_d, 3.0,
                                  inf_d, qnan_d, qnan_d};
  double inputsb[kTableLength] = {3.0,    2.0,   0.0,    -0.0, inf_d,
                                  42.0,   inf_d, minf_d, 3.0,  qnan_d,
                                  qnan_d, inf_d, qnan_d};
  double outputsdmin[kTableLength] = {2.0,    2.0,    -0.0,   -0.0,   42.0,
                                      42.0,   minf_d, minf_d, qnan_d, qnan_d,
                                      qnan_d, qnan_d, qnan_d};
  double outputsdmax[kTableLength] = {3.0,    3.0,    0.0,   0.0,    inf_d,
                                      inf_d,  inf_d,  inf_d, qnan_d, qnan_d,
                                      qnan_d, qnan_d, qnan_d};

  float inputse[kTableLength] = {2.0,   3.0,    -0.0,  0.0,    42.0,
                                 inf_f, minf_f, inf_f, qnan_f, 3.0,
                                 inf_f, qnan_f, qnan_f};
  float inputsf[kTableLength] = {3.0,    2.0,   0.0,    -0.0, inf_f,
                                 42.0,   inf_f, minf_f, 3.0,  qnan_f,
                                 qnan_f, inf_f, qnan_f};
  float outputsfmin[kTableLength] = {2.0,    2.0,    -0.0,   -0.0,   42.0,
                                     42.0,   minf_f, minf_f, qnan_f, qnan_f,
                                     qnan_f, qnan_f, qnan_f};
  float outputsfmax[kTableLength] = {3.0,    3.0,    0.0,   0.0,    inf_f,
                                     inf_f,  inf_f,  inf_f, qnan_f, qnan_f,
                                     qnan_f, qnan_f, qnan_f};

  auto fn = [](MacroAssembler& masm) {
    __ push(s6);
    __ push(s11);
    __ InitializeRootRegister();
    __ LoadDouble(fa3, MemOperand(a0, offsetof(TestFloat, a)));
    __ LoadDouble(fa4, MemOperand(a0, offsetof(TestFloat, b)));
    __ LoadFloat(fa1, MemOperand(a0, offsetof(TestFloat, e)));
    __ LoadFloat(fa2, MemOperand(a0, offsetof(TestFloat, f)));
    __ Float64Min(fa5, fa3, fa4);
    __ Float64Max(fa6, fa3, fa4);
    __ Float32Min(fa7, fa1, fa2);
    __ Float32Max(fa0, fa1, fa2);
    __ StoreDouble(fa5, MemOperand(a0, offsetof(TestFloat, c)));
    __ StoreDouble(fa6, MemOperand(a0, offsetof(TestFloat, d)));
    __ StoreFloat(fa7, MemOperand(a0, offsetof(TestFloat, g)));
    __ StoreFloat(fa0, MemOperand(a0, offsetof(TestFloat, h)));
    __ pop(s11);
    __ pop(s6);
  };
  auto f = AssembleCode<F3>(isolate, fn);

  for (int i = 0; i < kTableLength; i++) {
    test.a = inputsa[i];
    test.b = inputsb[i];
    test.e = inputse[i];
    test.f = inputsf[i];

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(0, memcmp(&test.c, &outputsdmin[i], sizeof(test.c)));
    CHECK_EQ(0, memcmp(&test.d, &outputsdmax[i], sizeof(test.d)));
    CHECK_EQ(0, memcmp(&test.g, &outputsfmin[i], sizeof(test.g)));
    CHECK_EQ(0, memcmp(&test.h, &outputsfmax[i], sizeof(test.h)));
  }
}

TEST(Ulh) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn1 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ Ulh(t0, MemOperand(a0, in_offset));
    __ Ush(t0, MemOperand(a0, out_offset));
  };

  auto fn2 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ mv(t0, a0);
    __ Ulh(a0, MemOperand(a0, in_offset));
    __ Ush(a0, MemOperand(t0, out_offset));
  };

  auto fn3 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ mv(t0, a0);
    __ Ulhu(a0, MemOperand(a0, in_offset));
    __ Ush(a0, MemOperand(t0, out_offset));
  };

  auto fn4 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ Ulhu(t0, MemOperand(a0, in_offset));
    __ Ush(t0, MemOperand(a0, out_offset));
  };

  FOR_UINT16_INPUTS(i) {
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn1));

        // test when loaded value overwrites base-register of load address
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn2));

        // test when loaded value overwrites base-register of load address
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn3));

        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn4));
      }
    }
  }
}

TEST(Ulh_bitextension) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    Label success, fail, end, different;
    __ Ulh(t0, MemOperand(a0, in_offset));
    __ Ulhu(t1, MemOperand(a0, in_offset));
    __ Branch(&different, ne, t0, Operand(t1));

    // If signed and unsigned values are same, check
    // the upper bits to see if they are zero
    __ sraiw(t0, t0, 15);
    __ Branch(&success, eq, t0, Operand(zero_reg));
    __ Branch(&fail);

    // If signed and unsigned values are different,
    // check that the upper bits are complementary
    __ bind(&different);
    __ sraiw(t1, t1, 15);
    __ Branch(&fail, ne, t1, Operand(1));
    __ sraiw(t0, t0, 15);
    __ addiw(t0, t0, 1);
    __ Branch(&fail, ne, t0, Operand(zero_reg));
    // Fall through to success

    __ bind(&success);
    __ Ulh(t0, MemOperand(a0, in_offset));
    __ Ush(t0, MemOperand(a0, out_offset));
    __ Branch(&end);
    __ bind(&fail);
    __ Ush(zero_reg, MemOperand(a0, out_offset));
    __ bind(&end);
  };

  FOR_UINT16_INPUTS(i) {
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn));
      }
    }
  }
}

TEST(Ulw) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn_1 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ Ulw(t0, MemOperand(a0, in_offset));
    __ Usw(t0, MemOperand(a0, out_offset));
  };

  auto fn_2 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ mv(t0, a0);
    __ Ulw(a0, MemOperand(a0, in_offset));
    __ Usw(a0, MemOperand(t0, out_offset));
  };

  auto fn_3 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ Ulwu(t0, MemOperand(a0, in_offset));
    __ Usw(t0, MemOperand(a0, out_offset));
  };

  auto fn_4 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ mv(t0, a0);
    __ Ulwu(a0, MemOperand(a0, in_offset));
    __ Usw(a0, MemOperand(t0, out_offset));
  };

  FOR_UINT32_INPUTS(i) {
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_1));
        // test when loaded value overwrites base-register of load address
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_2));
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_3));
        // test when loaded value overwrites base-register of load address
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_4));
      }
    }
  }
}

TEST(Ulw_extension) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    Label success, fail, end, different;
    __ Ulw(t0, MemOperand(a0, in_offset));
    __ Ulwu(t1, MemOperand(a0, in_offset));
    __ Branch(&different, ne, t0, Operand(t1));

    // If signed and unsigned values are same, check
    // the upper bits to see if they are zero
    __ srai(t0, t0, 31);
    __ Branch(&success, eq, t0, Operand(zero_reg));
    __ Branch(&fail);

    // If signed and unsigned values are different,
    // check that the upper bits are complementary
    __ bind(&different);
    __ srai(t1, t1, 31);
    __ Branch(&fail, ne, t1, Operand(1));
    __ srai(t0, t0, 31);
    __ addi(t0, t0, 1);
    __ Branch(&fail, ne, t0, Operand(zero_reg));
    // Fall through to success

    __ bind(&success);
    __ Ulw(t0, MemOperand(a0, in_offset));
    __ Usw(t0, MemOperand(a0, out_offset));
    __ Branch(&end);
    __ bind(&fail);
    __ Usw(zero_reg, MemOperand(a0, out_offset));
    __ bind(&end);
  };

  FOR_UINT32_INPUTS(i) {
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn));
      }
    }
  }
}

TEST(Uld) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn_1 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ Uld(t0, MemOperand(a0, in_offset));
    __ Usd(t0, MemOperand(a0, out_offset));
  };

  auto fn_2 = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ mv(t0, a0);
    __ Uld(a0, MemOperand(a0, in_offset));
    __ Usd(a0, MemOperand(t0, out_offset));
  };

  FOR_UINT64_INPUTS(i) {
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_1));

        // test when loaded value overwrites base-register of load address
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn_2));
      }
    }
  }
}

auto fn = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
  __ ULoadFloat(fa0, MemOperand(a0, in_offset), t0);
  __ UStoreFloat(fa0, MemOperand(a0, out_offset), t0);
};

TEST(ULoadFloat) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_FLOAT32_INPUTS(i) {
    // skip nan because CHECK_EQ cannot handle NaN
    if (std::isnan(i)) continue;
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn));
      }
    }
  }
}

TEST(ULoadDouble) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  auto fn = [](MacroAssembler& masm, int32_t in_offset, int32_t out_offset) {
    __ ULoadDouble(fa0, MemOperand(a0, in_offset), t0);
    __ UStoreDouble(fa0, MemOperand(a0, out_offset), t0);
  };

  FOR_FLOAT64_INPUTS(i) {
    // skip nan because CHECK_EQ cannot handle NaN
    if (std::isnan(i)) continue;
    FOR_INT32_TWO_INPUTS(j1, j2, unsigned_test_offset) {
      FOR_INT32_TWO_INPUTS(k1, k2, unsigned_test_offset_increment) {
        auto value = i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;
        CHECK_EQ(value, run_Unaligned(buffer_middle, in_offset, out_offset,
                                      value, fn));
      }
    }
  }
}

TEST(Sltu) {
  CcTest::InitializeVM();

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      // compare against immediate value
      auto fn_1 = [j](MacroAssembler& masm) { __ Sltu(a0, a0, Operand(j)); };
      CHECK_EQ(i < j, GenAndRunTest<int32_t>(i, fn_1));
      // compare against registers
      auto fn_2 = [](MacroAssembler& masm) { __ Sltu(a0, a0, a1); };
      CHECK_EQ(i < j, GenAndRunTest<int32_t>(i, j, fn_2));
    }
  }
}

template <typename T, typename Inputs, typename Results>
static void GenerateMacroFloat32MinMax(MacroAssembler& masm) {
  T a = T::from_code(5);  // ft5
  T b = T::from_code(6);  // ft6
  T c = T::from_code(7);  // ft7

#define FLOAT_MIN_MAX(fminmax, res, x, y, res_field)        \
  __ LoadFloat(x, MemOperand(a0, offsetof(Inputs, src1_))); \
  __ LoadFloat(y, MemOperand(a0, offsetof(Inputs, src2_))); \
  __ fminmax(res, x, y);                                    \
  __ StoreFloat(res, MemOperand(a1, offsetof(Results, res_field)))

  // a = min(b, c);
  FLOAT_MIN_MAX(Float32Min, a, b, c, min_abc_);
  // a = min(a, b);
  FLOAT_MIN_MAX(Float32Min, a, a, b, min_aab_);
  // a = min(b, a);
  FLOAT_MIN_MAX(Float32Min, a, b, a, min_aba_);

  // a = max(b, c);
  FLOAT_MIN_MAX(Float32Max, a, b, c, max_abc_);
  // a = max(a, b);
  FLOAT_MIN_MAX(Float32Max, a, a, b, max_aab_);
  // a = max(b, a);
  FLOAT_MIN_MAX(Float32Max, a, b, a, max_aba_);

#undef FLOAT_MIN_MAX
}

TEST(macro_float_minmax_f32) {
  // Test the Float32Min and Float32Max macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct Inputs {
    float src1_;
    float src2_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro masm.
    float min_abc_;
    float min_aab_;
    float min_aba_;
    float max_abc_;
    float max_aab_;
    float max_aba_;
  };

  auto f = AssembleCode<F4>(
      isolate, GenerateMacroFloat32MinMax<FPURegister, Inputs, Results>);

#define CHECK_MINMAX(src1, src2, min, max)                                \
  do {                                                                    \
    Inputs inputs = {src1, src2};                                         \
    Results results;                                                      \
    f.Call(&inputs, &results, 0, 0, 0);                                   \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                               \
             base::bit_cast<uint32_t>(results.min_abc_));                 \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                               \
             base::bit_cast<uint32_t>(results.min_aab_));                 \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                               \
             base::bit_cast<uint32_t>(results.min_aba_));                 \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                               \
             base::bit_cast<uint32_t>(results.max_abc_));                 \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                               \
             base::bit_cast<uint32_t>(results.max_aab_));                 \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                               \
             base::bit_cast<uint32_t>(                                    \
                 results.max_aba_)); /* Use a base::bit_cast to correctly \
                              identify -0.0 and NaNs. */                  \
  } while (0)

  float nan_a = std::numeric_limits<float>::quiet_NaN();
  float nan_b = std::numeric_limits<float>::quiet_NaN();

  CHECK_MINMAX(1.0f, -1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(-1.0f, 1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(0.0f, -1.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-1.0f, 0.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -1.0f, -1.0f, -0.0f);
  CHECK_MINMAX(-1.0f, -0.0f, -1.0f, -0.0f);
  CHECK_MINMAX(0.0f, 1.0f, 0.0f, 1.0f);
  CHECK_MINMAX(1.0f, 0.0f, 0.0f, 1.0f);

  CHECK_MINMAX(0.0f, 0.0f, 0.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -0.0f, -0.0f, -0.0f);
  CHECK_MINMAX(-0.0f, 0.0f, -0.0f, 0.0f);
  CHECK_MINMAX(0.0f, -0.0f, -0.0f, 0.0f);

  CHECK_MINMAX(0.0f, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0f, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

template <typename T, typename Inputs, typename Results>
static void GenerateMacroFloat64MinMax(MacroAssembler& masm) {
  T a = T::from_code(5);  // ft5
  T b = T::from_code(6);  // ft6
  T c = T::from_code(7);  // ft7

#define FLOAT_MIN_MAX(fminmax, res, x, y, res_field)         \
  __ LoadDouble(x, MemOperand(a0, offsetof(Inputs, src1_))); \
  __ LoadDouble(y, MemOperand(a0, offsetof(Inputs, src2_))); \
  __ fminmax(res, x, y);                                     \
  __ StoreDouble(res, MemOperand(a1, offsetof(Results, res_field)))

  // a = min(b, c);
  FLOAT_MIN_MAX(Float64Min, a, b, c, min_abc_);
  // a = min(a, b);
  FLOAT_MIN_MAX(Float64Min, a, a, b, min_aab_);
  // a = min(b, a);
  FLOAT_MIN_MAX(Float64Min, a, b, a, min_aba_);

  // a = max(b, c);
  FLOAT_MIN_MAX(Float64Max, a, b, c, max_abc_);
  // a = max(a, b);
  FLOAT_MIN_MAX(Float64Max, a, a, b, max_aab_);
  // a = max(b, a);
  FLOAT_MIN_MAX(Float64Max, a, b, a, max_aba_);

#undef FLOAT_MIN_MAX
}

TEST(macro_float_minmax_f64) {
  // Test the Float64Min and Float64Max macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct Inputs {
    double src1_;
    double src2_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro masm.
    double min_abc_;
    double min_aab_;
    double min_aba_;
    double max_abc_;
    double max_aab_;
    double max_aba_;
  };

  auto f = AssembleCode<F4>(
      isolate, GenerateMacroFloat64MinMax<DoubleRegister, Inputs, Results>);

#define CHECK_MINMAX(src1, src2, min, max)                          \
  do {                                                              \
    Inputs inputs = {src1, src2};                                   \
    Results results;                                                \
    f.Call(&inputs, &results, 0, 0, 0);                             \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_abc_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_aab_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_aba_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_abc_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_aab_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_aba_));           \
    /* Use a base::bit_cast to correctly identify -0.0 and NaNs. */ \
  } while (0)

  double nan_a = qnan_d;
  double nan_b = qnan_d;

  CHECK_MINMAX(1.0, -1.0, -1.0, 1.0);
  CHECK_MINMAX(-1.0, 1.0, -1.0, 1.0);
  CHECK_MINMAX(0.0, -1.0, -1.0, 0.0);
  CHECK_MINMAX(-1.0, 0.0, -1.0, 0.0);
  CHECK_MINMAX(-0.0, -1.0, -1.0, -0.0);
  CHECK_MINMAX(-1.0, -0.0, -1.0, -0.0);
  CHECK_MINMAX(0.0, 1.0, 0.0, 1.0);
  CHECK_MINMAX(1.0, 0.0, 0.0, 1.0);

  CHECK_MINMAX(0.0, 0.0, 0.0, 0.0);
  CHECK_MINMAX(-0.0, -0.0, -0.0, -0.0);
  CHECK_MINMAX(-0.0, 0.0, -0.0, 0.0);
  CHECK_MINMAX(0.0, -0.0, -0.0, 0.0);

  CHECK_MINMAX(0.0, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

template <typename T>
static bool CompareF(T input1, T input2, FPUCondition cond) {
  switch (cond) {
    case EQ:
      return (input1 == input2);
    case LT:
      return (input1 < input2);
    case LE:
      return (input1 <= input2);
    case NE:
      return (input1 != input2);
    case GT:
      return (input1 > input2);
    case GE:
      return (input1 >= input2);
    default:
      UNREACHABLE();
  }
}

static bool CompareU(uint64_t input1, uint64_t input2, Condition cond) {
  switch (cond) {
    case eq:
      return (input1 == input2);
    case ne:
      return (input1 != input2);

    case Uless:
      return (input1 < input2);
    case Uless_equal:
      return (input1 <= input2);
    case Ugreater:
      return (input1 > input2);
    case Ugreater_equal:
      return (input1 >= input2);

    case less:
      return (static_cast<int64_t>(input1) < static_cast<int64_t>(input2));
    case less_equal:
      return (static_cast<int64_t>(input1) <= static_cast<int64_t>(input2));
    case greater:
      return (static_cast<int64_t>(input1) > static_cast<int64_t>(input2));
    case greater_equal:
      return (static_cast<int64_t>(input1) >= static_cast<int64_t>(input2));

    default:
      UNREACHABLE();
  }
}

static void FCompare32Helper(FPUCondition cond) {
  auto fn = [cond](MacroAssembler& masm) { __ CompareF32(a0, cond, fa0, fa1); };
  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) {
      bool comp_res = CompareF(i, j, cond);
      CHECK_EQ(comp_res, GenAndRunTest<int32_t>(i, j, fn));
    }
  }
}

static void FCompare64Helper(FPUCondition cond) {
  auto fn = [cond](MacroAssembler& masm) { __ CompareF64(a0, cond, fa0, fa1); };
  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      bool comp_res = CompareF(i, j, cond);
      CHECK_EQ(comp_res, GenAndRunTest<int32_t>(i, j, fn));
    }
  }
}

TEST(FCompare32_Branch) {
  CcTest::InitializeVM();

  FCompare32Helper(EQ);
  FCompare32Helper(LT);
  FCompare32Helper(LE);
  FCompare32Helper(NE);
  FCompare32Helper(GT);
  FCompare32Helper(GE);

  // test CompareIsNanF32: return true if any operand isnan
  auto fn = [](MacroAssembler& masm) { __ CompareIsNanF32(a0, fa0, fa1); };
  CHECK_EQ(false, GenAndRunTest<int32_t>(1023.01f, -100.23f, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(1023.01f, snan_f, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(snan_f, -100.23f, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(snan_f, qnan_f, fn));
}

TEST(FCompare64_Branch) {
  CcTest::InitializeVM();
  FCompare64Helper(EQ);
  FCompare64Helper(LT);
  FCompare64Helper(LE);
  FCompare64Helper(NE);
  FCompare64Helper(GT);
  FCompare64Helper(GE);

  // test CompareIsNanF64: return true if any operand isnan
  auto fn = [](MacroAssembler& masm) { __ CompareIsNanF64(a0, fa0, fa1); };
  CHECK_EQ(false, GenAndRunTest<int32_t>(1023.01, -100.23, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(1023.01, snan_d, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(snan_d, -100.23, fn));
  CHECK_EQ(true, GenAndRunTest<int32_t>(snan_d, qnan_d, fn));
}

static void CompareIHelper(Condition cond) {
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      auto input1 = i;
      auto input2 = j;
      bool comp_res = CompareU(input1, input2, cond);
      // test compare against immediate value
      auto fn1 = [cond, input2](MacroAssembler& masm) {
        __ CompareI(a0, a0, Operand(input2), cond);
      };
      CHECK_EQ(comp_res, GenAndRunTest<int32_t>(input1, fn1));
      // test compare registers
      auto fn2 = [cond](MacroAssembler& masm) {
        __ CompareI(a0, a0, Operand(a1), cond);
      };
      CHECK_EQ(comp_res, GenAndRunTest<int32_t>(input1, input2, fn2));
    }
  }
}

TEST(CompareI) {
  CcTest::InitializeVM();
  CompareIHelper(eq);
  CompareIHelper(ne);

  CompareIHelper(greater);
  CompareIHelper(greater_equal);
  CompareIHelper(less);
  CompareIHelper(less_equal);

  CompareIHelper(Ugreater);
  CompareIHelper(Ugreater_equal);
  CompareIHelper(Uless);
  CompareIHelper(Uless_equal);
}

TEST(Clz32) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) { __ Clz32(a0, a0); };
  FOR_UINT32_INPUTS(i) {
    // __builtin_clzll(0) is undefined
    if (i == 0) continue;
    CHECK_EQ(__builtin_clz(i), GenAndRunTest<int>(i, fn));
  }
}

TEST(Ctz32) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) { __ Ctz32(a0, a0); };
  FOR_UINT32_INPUTS(i) {
    // __builtin_clzll(0) is undefined
    if (i == 0) continue;
    CHECK_EQ(__builtin_ctz(i), GenAndRunTest<int>(i, fn));
  }
}

TEST(Clz64) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) { __ Clz64(a0, a0); };
  FOR_UINT64_INPUTS(i) {
    // __builtin_clzll(0) is undefined
    if (i == 0) continue;
    CHECK_EQ(__builtin_clzll(i), GenAndRunTest<int>(i, fn));
  }
}

TEST(Ctz64) {
  CcTest::InitializeVM();
  auto fn = [](MacroAssembler& masm) { __ Ctz64(a0, a0); };
  FOR_UINT64_INPUTS(i) {
    // __builtin_clzll(0) is undefined
    if (i == 0) continue;
    CHECK_EQ(__builtin_ctzll(i), GenAndRunTest<int>(i, fn));
  }
}

template <int NBYTES, bool USE_SCRATCH>
static void ByteSwapHelper() {
  DCHECK(NBYTES == 4 || NBYTES == 8);
  Func fn;
  if (USE_SCRATCH) {
    fn = [](MacroAssembler& masm) { __ ByteSwap(a0, a0, NBYTES, t0); };
  } else {
    fn = [](MacroAssembler& masm) { __ ByteSwap(a0, a0, NBYTES); };
  }

  if (NBYTES == 4) {
    CHECK_EQ((int32_t)0x89ab'cdef, GenAndRunTest<int32_t>(0xefcd'ab89, fn));
  } else {
    CHECK_EQ((int64_t)0x0123'4567'89ab'cdef,
             GenAndRunTest<int64_t>(0xefcd'ab89'6745'2301, fn));
  }
}

TEST(ByteSwap) {
  CcTest::InitializeVM();
  ByteSwapHelper<4, true>();
  ByteSwapHelper<8, true>();
}

TEST(ByteSwap_no_scratch) {
  CcTest::InitializeVM();
  ByteSwapHelper<4, false>();
  ByteSwapHelper<8, false>();
}

TEST(Dpopcnt) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  uint64_t in[9];
  uint64_t out[9];
  uint64_t result[9];
  uint64_t val = 0xffffffffffffffffl;
  uint64_t cnt = 64;

  for (int i = 0; i < 7; i++) {
    in[i] = val;
    out[i] = cnt;
    cnt >>= 1;
    val >>= cnt;
  }

  in[7] = 0xaf1000000000000bl;
  out[7] = 10;
  in[8] = 0xe030000f00003000l;
  out[8] = 11;

  auto fn = [&in](MacroAssembler& masm) {
    __ mv(a4, a0);
    for (int i = 0; i < 7; i++) {
      // Load constant.
      __ li(a3, Operand(in[i]));
      __ Popcnt64(a5, a3, t0);
      __ Sd(a5, MemOperand(a4));
      __ Add64(a4, a4, Operand(kSystemPointerSize));
    }
    __ li(a3, Operand(in[7]));
    __ Popcnt64(a5, a3, t0);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kSystemPointerSize));

    __ li(a3, Operand(in[8]));
    __ Popcnt64(a5, a3, t0);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kSystemPointerSize));
  };
  auto f = AssembleCode<FV>(isolate, fn);

  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 9; i++) {
    CHECK(out[i] == result[i]);
  }
}

TEST(Popcnt) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  uint64_t in[8];
  uint64_t out[8];
  uint64_t result[8];
  uint64_t val = 0xffffffff;
  uint64_t cnt = 32;

  for (int i = 0; i < 6; i++) {
    in[i] = val;
    out[i] = cnt;
    cnt >>= 1;
    val >>= cnt;
  }

  in[6] = 0xaf10000b;
  out[6] = 10;
  in[7] = 0xe03f3000;
  out[7] = 11;

  auto fn = [&in](MacroAssembler& masm) {
    __ mv(a4, a0);
    for (int i = 0; i < 6; i++) {
      // Load constant.
      __ li(a3, Operand(in[i]));
      __ Popcnt32(a5, a3, t0);
      __ Sd(a5, MemOperand(a4));
      __ Add64(a4, a4, Operand(kSystemPointerSize));
    }

    __ li(a3, Operand(in[6]));
    __ Popcnt64(a5, a3, t0);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kSystemPointerSize));

    __ li(a3, Operand(in[7]));
    __ Popcnt64(a5, a3, t0);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kSystemPointerSize));
  };
  auto f = AssembleCode<FV>(isolate, fn);

  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 8; i++) {
    CHECK(out[i] == result[i]);
  }
}

TEST(Move) {
  CcTest::InitializeVM();
  union {
    double dval;
    int32_t ival[2];
  } t;

  {
    auto fn = [](MacroAssembler& masm) { __ ExtractHighWordFromF64(a0, fa0); };
    t.ival[0] = 256;
    t.ival[1] = -123;
    CHECK_EQ(static_cast<int64_t>(t.ival[1]),
             GenAndRunTest<int64_t>(t.dval, fn));
    t.ival[0] = 645;
    t.ival[1] = 127;
    CHECK_EQ(static_cast<int64_t>(t.ival[1]),
             GenAndRunTest<int64_t>(t.dval, fn));
  }

  {
    auto fn = [](MacroAssembler& masm) { __ ExtractLowWordFromF64(a0, fa0); };
    t.ival[0] = 256;
    t.ival[1] = -123;
    CHECK_EQ(static_cast<int64_t>(t.ival[0]),
             GenAndRunTest<int64_t>(t.dval, fn));
    t.ival[0] = -645;
    t.ival[1] = 127;
    CHECK_EQ(static_cast<int64_t>(t.ival[0]),
             GenAndRunTest<int64_t>(t.dval, fn));
  }
}

TEST(DeoptExitSizeIsFixed) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      buffer->CreateView());
  static_assert(static_cast<int>(kFirstDeoptimizeKind) == 0);
  for (int i = 0; i < kDeoptimizeKindCount; i++) {
    DeoptimizeKind kind = static_cast<DeoptimizeKind>(i);
    Label before_exit;
    Builtin target = Deoptimizer::GetDeoptimizationEntry(kind);
    // Mirroring logic in code-generator.cc.
    if (kind == DeoptimizeKind::kLazy) {
      // CFI emits an extra instruction here.
      masm.BindExceptionHandler(&before_exit);
    } else {
      masm.bind(&before_exit);
    }
    masm.CallForDeoptimization(target, 42, &before_exit, kind, &before_exit,
                               &before_exit);
    CHECK_EQ(masm.SizeOfCodeGeneratedSince(&before_exit),
             kind == DeoptimizeKind::kLazy ? Deoptimizer::kLazyDeoptExitSize
                                           : Deoptimizer::kEagerDeoptExitSize);
  }
}

TEST(AddWithImm) {
  CcTest::InitializeVM();
#define Test(Op, Input, Expected)                                       \
  {                                                                     \
    auto fn = [](MacroAssembler& masm) { __ Op(a0, zero_reg, Input); }; \
    CHECK_EQ(static_cast<int64_t>(Expected), GenAndRunTest(fn));        \
  }

  Test(Add64, 4095, 4095);
  Test(Add32, 4095, 4095);
  Test(Sub64, 4095, -4095);
  Test(Sub32, 4095, -4095);
#undef Test
}

#undef __

}  // namespace internal
}  // namespace v8
