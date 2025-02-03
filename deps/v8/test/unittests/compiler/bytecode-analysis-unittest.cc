// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-analysis.h"

#include <iomanip>

#include "src/compiler/bytecode-liveness-map.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/control-flow-builders.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/interpreter/bytecode-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

using ToBooleanMode = interpreter::BytecodeArrayBuilder::ToBooleanMode;

class BytecodeAnalysisTest : public TestWithIsolateAndZone {
 public:
  BytecodeAnalysisTest() = default;
  ~BytecodeAnalysisTest() override = default;
  BytecodeAnalysisTest(const BytecodeAnalysisTest&) = delete;
  BytecodeAnalysisTest& operator=(const BytecodeAnalysisTest&) = delete;

  static void SetUpTestSuite() {
    CHECK_NULL(save_flags_);
    save_flags_ = new SaveFlags();
    i::v8_flags.ignition_elide_noneffectful_bytecodes = false;
    i::v8_flags.ignition_reo = false;

    TestWithIsolateAndZone::SetUpTestSuite();
  }

  static void TearDownTestSuite() {
    TestWithIsolateAndZone::TearDownTestSuite();
    delete save_flags_;
    save_flags_ = nullptr;
  }

  void EnsureLivenessMatches(
      Handle<BytecodeArray> bytecode,
      const std::vector<std::pair<std::string, std::string>>&
          expected_liveness) {
    BytecodeAnalysis analysis(bytecode, zone(), BytecodeOffset::None(), true);

    interpreter::BytecodeArrayIterator iterator(bytecode);
    for (auto liveness : expected_liveness) {
      std::stringstream ss;
      ss << std::setw(4) << iterator.current_offset() << " : ";
      iterator.PrintTo(ss);

      EXPECT_EQ(liveness.first,
                ToString(*analysis.GetInLivenessFor(iterator.current_offset())))
          << " at bytecode " << ss.str();

      EXPECT_EQ(
          liveness.second,
          ToString(*analysis.GetOutLivenessFor(iterator.current_offset())))
          << " at bytecode " << ss.str();

      iterator.Advance();
    }

    EXPECT_TRUE(iterator.done());
  }

 private:
  static SaveFlags* save_flags_;
};

SaveFlags* BytecodeAnalysisTest::save_flags_ = nullptr;

TEST_F(BytecodeAnalysisTest, EmptyBlock) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, SimpleLoad) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);

  builder.LoadAccumulatorWithRegister(reg_0);
  expected_liveness.emplace_back("L...", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, StoreThenLoad) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);

  builder.StoreAccumulatorInRegister(reg_0);
  expected_liveness.emplace_back("...L", "L...");

  builder.LoadAccumulatorWithRegister(reg_0);
  expected_liveness.emplace_back("L...", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, DiamondLoad) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_1(1);
  interpreter::Register reg_2(2);

  interpreter::BytecodeLabel ld1_label;
  interpreter::BytecodeLabel end_label;

  builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean, &ld1_label);
  expected_liveness.emplace_back("LLLL", "LLL.");

  builder.LoadAccumulatorWithRegister(reg_0);
  expected_liveness.emplace_back("L.L.", "..L.");

  builder.Jump(&end_label);
  expected_liveness.emplace_back("..L.", "..L.");

  builder.Bind(&ld1_label);
  builder.LoadAccumulatorWithRegister(reg_1);
  expected_liveness.emplace_back(".LL.", "..L.");

  builder.Bind(&end_label);

  builder.LoadAccumulatorWithRegister(reg_2);
  expected_liveness.emplace_back("..L.", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, DiamondLookupsAndBinds) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_1(1);
  interpreter::Register reg_2(2);

  interpreter::BytecodeLabel ld1_label;
  interpreter::BytecodeLabel end_label;

  builder.StoreAccumulatorInRegister(reg_0);
  expected_liveness.emplace_back(".LLL", "LLLL");

  builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean, &ld1_label);
  expected_liveness.emplace_back("LLLL", "LLL.");

  {
    builder.LoadAccumulatorWithRegister(reg_0);
    expected_liveness.emplace_back("L...", "...L");

    builder.StoreAccumulatorInRegister(reg_2);
    expected_liveness.emplace_back("...L", "..L.");

    builder.Jump(&end_label);
    expected_liveness.emplace_back("..L.", "..L.");
  }

  builder.Bind(&ld1_label);
  {
    builder.LoadAccumulatorWithRegister(reg_1);
    expected_liveness.emplace_back(".LL.", "..L.");
  }

  builder.Bind(&end_label);

  builder.LoadAccumulatorWithRegister(reg_2);
  expected_liveness.emplace_back("..L.", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, SimpleLoop) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  FeedbackVectorSpec spec(zone());
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_2(2);

  // Kill r0.
  builder.StoreAccumulatorInRegister(reg_0);
  expected_liveness.emplace_back("..LL", "L.L.");

  {
    interpreter::LoopBuilder loop_builder(&builder, nullptr, nullptr, &spec);
    loop_builder.LoopHeader();

    builder.LoadUndefined();
    expected_liveness.emplace_back("L.L.", "L.LL");

    builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean,
                       loop_builder.break_labels()->New());
    expected_liveness.emplace_back("L.LL", "L.L.");

    // Gen r0.
    builder.LoadAccumulatorWithRegister(reg_0);
    expected_liveness.emplace_back("L...", "L..L");

    // Kill r2.
    builder.StoreAccumulatorInRegister(reg_2);
    expected_liveness.emplace_back("L..L", "L.L.");

    loop_builder.BindContinueTarget();
    loop_builder.JumpToHeader(0, nullptr);
    expected_liveness.emplace_back("L.L.", "L.L.");
  }

  // Gen r2.
  builder.LoadAccumulatorWithRegister(reg_2);
  expected_liveness.emplace_back("..L.", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, TryCatch) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_1(1);
  interpreter::Register reg_context(2);

  // Kill r0.
  builder.StoreAccumulatorInRegister(reg_0);
  expected_liveness.emplace_back(".LLL", "LLL.");

  interpreter::TryCatchBuilder try_builder(&builder, nullptr, nullptr,
                                           HandlerTable::CAUGHT);
  try_builder.BeginTry(reg_context);
  {
    // Gen r0.
    builder.LoadAccumulatorWithRegister(reg_0);
    expected_liveness.emplace_back("LLL.", ".LLL");

    // Kill r0.
    builder.StoreAccumulatorInRegister(reg_0);
    expected_liveness.emplace_back(".LLL", ".LL.");

    builder.CallRuntime(Runtime::kThrow);
    expected_liveness.emplace_back(".LL.", ".LLL");

    builder.StoreAccumulatorInRegister(reg_0);
    // Star can't throw, so doesn't take handler liveness
    expected_liveness.emplace_back("...L", "...L");
  }
  try_builder.EndTry();
  expected_liveness.emplace_back("...L", "...L");

  // Catch
  {
    builder.LoadAccumulatorWithRegister(reg_1);
    expected_liveness.emplace_back(".L..", "...L");
  }
  try_builder.EndCatch();

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, DiamondInLoop) {
  // For a logic diamond inside a loop, the liveness down one path of the
  // diamond should eventually propagate up the other path when the loop is
  // reprocessed.

  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  FeedbackVectorSpec spec(zone());
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);

  {
    interpreter::LoopBuilder loop_builder(&builder, nullptr, nullptr, &spec);
    loop_builder.LoopHeader();

    builder.LoadUndefined();
    expected_liveness.emplace_back("L...", "L..L");
    builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean,
                       loop_builder.break_labels()->New());
    expected_liveness.emplace_back("L..L", "L..L");

    interpreter::BytecodeLabel ld1_label;
    interpreter::BytecodeLabel end_label;
    builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean, &ld1_label);
    expected_liveness.emplace_back("L..L", "L...");

    {
      builder.Jump(&end_label);
      expected_liveness.emplace_back("L...", "L...");
    }

    builder.Bind(&ld1_label);
    {
      // Gen r0.
      builder.LoadAccumulatorWithRegister(reg_0);
      expected_liveness.emplace_back("L...", "L...");
    }

    builder.Bind(&end_label);

    loop_builder.BindContinueTarget();
    loop_builder.JumpToHeader(0, nullptr);
    expected_liveness.emplace_back("L...", "L...");
  }

  builder.LoadUndefined();
  expected_liveness.emplace_back("....", "...L");
  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, KillingLoopInsideLoop) {
  // For a loop inside a loop, the inner loop has to be processed after the
  // outer loop has been processed, to ensure that it can propagate the
  // information in its header. Consider
  //
  //     0: do {
  //     1:   acc = r0;
  //     2:   acc = r1;
  //     3:   do {
  //     4:     r0 = acc;
  //     5:     break;
  //     6:   } while(true);
  //     7: } while(true);
  //
  // r0 should should be dead at 3 and 6, while r1 is live throughout. On the
  // initial pass, r1 is dead from 3-7. On the outer loop pass, it becomes live
  // in 3 and 7 (but not 4-6 because 6 only reads liveness from 3). Only after
  // the inner loop pass does it become live in 4-6. It's necessary, however, to
  // still process the inner loop when processing the outer loop, to ensure that
  // r1 becomes live in 3 (via 5), but r0 stays dead (because of 4).

  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  FeedbackVectorSpec spec(zone());
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_1(1);

  {
    interpreter::LoopBuilder loop_builder(&builder, nullptr, nullptr, &spec);
    loop_builder.LoopHeader();

    // Gen r0.
    builder.LoadAccumulatorWithRegister(reg_0);
    expected_liveness.emplace_back("LL..", ".L..");

    // Gen r1.
    builder.LoadAccumulatorWithRegister(reg_1);
    expected_liveness.emplace_back(".L..", ".L.L");

    builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean,
                       loop_builder.break_labels()->New());
    expected_liveness.emplace_back(".L.L", ".L..");

    {
      interpreter::LoopBuilder inner_loop_builder(&builder, nullptr, nullptr,
                                                  &spec);
      inner_loop_builder.LoopHeader();

      // Kill r0.
      builder.LoadUndefined();
      expected_liveness.emplace_back(".L..", ".L.L");
      builder.StoreAccumulatorInRegister(reg_0);
      expected_liveness.emplace_back(".L.L", "LL.L");

      builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean,
                         inner_loop_builder.break_labels()->New());
      expected_liveness.emplace_back("LL.L", "LL..");

      inner_loop_builder.BindContinueTarget();
      inner_loop_builder.JumpToHeader(1, &loop_builder);
      expected_liveness.emplace_back(".L..", ".L..");
    }

    loop_builder.BindContinueTarget();
    loop_builder.JumpToHeader(0, nullptr);
    expected_liveness.emplace_back("LL..", "LL..");
  }

  builder.LoadUndefined();
  expected_liveness.emplace_back("....", "...L");
  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

TEST_F(BytecodeAnalysisTest, SuspendPoint) {
  interpreter::BytecodeArrayBuilder builder(zone(), 3, 3);
  std::vector<std::pair<std::string, std::string>> expected_liveness;

  interpreter::Register reg_0(0);
  interpreter::Register reg_1(1);
  interpreter::Register reg_gen(2);
  interpreter::BytecodeJumpTable* gen_jump_table =
      builder.AllocateJumpTable(1, 0);

  builder.SwitchOnGeneratorState(reg_gen, gen_jump_table);
  expected_liveness.emplace_back("..L.", "..L.");

  builder.LoadUndefined();
  expected_liveness.emplace_back("....", "...L");

  // Store some arbitrary value into the generator register so that this
  // register is dead by the time we reach SwitchOnGeneratorState (this matches
  // real generator bytecode and is DCHECKed in the bytecode analysis).
  builder.StoreAccumulatorInRegister(reg_gen);
  expected_liveness.emplace_back("...L", "..L.");

  builder.LoadUndefined();
  expected_liveness.emplace_back("..L.", "..LL");

  // Reg 0 is read after the resume, so should be live up to here (and is killed
  // here).
  builder.StoreAccumulatorInRegister(reg_0);
  expected_liveness.emplace_back("..LL", "L.LL");

  // Reg 1 is never read, so should be dead already and this store shouldn't
  // change it.
  builder.StoreAccumulatorInRegister(reg_1);
  expected_liveness.emplace_back("L.LL", "L.LL");

  builder.SuspendGenerator(
      reg_gen, interpreter::BytecodeUtils::NewRegisterList(0, 3), 0);
  expected_liveness.emplace_back("L.LL", "L.L.");

  builder.Bind(gen_jump_table, 0);

  builder.ResumeGenerator(reg_gen,
                          interpreter::BytecodeUtils::NewRegisterList(0, 1));
  expected_liveness.emplace_back("L.L.", "L...");

  builder.LoadAccumulatorWithRegister(reg_0);
  expected_liveness.emplace_back("L...", "...L");

  builder.Return();
  expected_liveness.emplace_back("...L", "....");

  Handle<BytecodeArray> bytecode = builder.ToBytecodeArray(isolate());

  EnsureLivenessMatches(bytecode, expected_liveness);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
