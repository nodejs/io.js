// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#ifdef V8_ENABLE_MAGLEV

#include "src/execution/simulator.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-assembler.h"
#include "test/unittests/maglev/maglev-test.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevAssemblerTest : public MaglevTest {
 public:
  MaglevAssemblerTest()
      : MaglevTest(),
        codegen_state(nullptr, nullptr),
        as(isolate(), &codegen_state) {}

  void FinalizeAndRun(Label* pass, Label* fail) {
    as.bind(pass);
    as.Ret();
    as.bind(fail);
    as.AssertUnreachable(AbortReason::kNoReason);
    CodeDesc desc;
    as.GetCode(isolate(), &desc);
    Factory::CodeBuilder build(isolate(), desc, CodeKind::FOR_TESTING);
    auto res = build.TryBuild().ToHandleChecked();
    using Function = GeneratedCode<Address()>;
    auto fun = Function::FromAddress(isolate(), res->instruction_start());
    fun.Call();
  }

  MaglevCodeGenState codegen_state;
  MaglevAssembler as;
};

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32One) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 1.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.Cmp(kReturnRegister0, 1);
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32Zero) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 0.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.Cmp(kReturnRegister0, 0);
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32Large) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, std::numeric_limits<uint32_t>::max());
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.Cmp(kReturnRegister0, std::numeric_limits<uint32_t>::max());
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32TooLarge) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0,
          static_cast<double>(std::numeric_limits<uint32_t>::max()) + 1.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32Negative) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, -1.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32NegativeZero) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, -0.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToUint32NotItegral) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 1.1);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToUint32(kReturnRegister0, kFPReturnRegister0,
                               &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32One) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 1.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.Cmp(kReturnRegister0, 1);
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32MinusOne) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, -1.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.Cmp(kReturnRegister0, static_cast<uint32_t>(-1));
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32Zero) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 0.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.Cmp(kReturnRegister0, 0);
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32Large) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, std::numeric_limits<int32_t>::max());
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.Cmp(kReturnRegister0, std::numeric_limits<int32_t>::max());
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32Small) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, std::numeric_limits<int32_t>::min());
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.Cmp(kReturnRegister0,
         static_cast<uint32_t>(std::numeric_limits<int32_t>::min()));
  as.Assert(Condition::kEqual, AbortReason::kNoReason);
  as.jmp(&can_convert);
  FinalizeAndRun(&can_convert, &cannot_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32NegativeZero) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, -0.0);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32NotItegral) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0, 1.1);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32TooLarge) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0,
          static_cast<double>(std::numeric_limits<int32_t>::max()) + 1);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

TEST_F(MaglevAssemblerTest, TryTruncateDoubleToInt32TooSmall) {
  as.CodeEntry();
  as.Move(kFPReturnRegister0,
          static_cast<double>(std::numeric_limits<int32_t>::min()) - 1);
  Label can_convert, cannot_convert;
  as.TryTruncateDoubleToInt32(kReturnRegister0, kFPReturnRegister0,
                              &cannot_convert);
  as.jmp(&can_convert);
  FinalizeAndRun(&cannot_convert, &can_convert);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV
