// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/simplified-lowering-reducer.h"

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class SimplifiedLoweringReducerTest : public ReducerTest {};

TEST_F(SimplifiedLoweringReducerTest, SafeIntegerAdd) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    OpIndex frame_state = Asm.BuildFrameState();
    V<Object> result = __ SpeculativeNumberBinop(
        Asm.GetParameter(0), __ NumberConstant(2), frame_state,
        SpeculativeNumberBinopOp::Kind::kSafeIntegerAdd);
    Asm.Capture(result, "SpeculativeNumberBinop");
    __ Return(result);
  });

  test.Run<SimplifiedLoweringReducer>();

  const auto& output = test.GetCapture("SpeculativeNumberBinop");
  ASSERT_TRUE(output.template Contains<Opmask::kOverflowCheckedWord32Add>());
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
