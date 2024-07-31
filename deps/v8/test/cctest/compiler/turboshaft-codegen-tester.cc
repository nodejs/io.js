// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/compiler/turboshaft-codegen-tester.h"

#include "src/base/overflowing-math.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/common/value-helper.h"

namespace v8::internal::compiler::turboshaft {

void Int32BinopInputShapeTester::TestAllInputShapes() {
  base::Vector<const int32_t> inputs = ValueHelper::int32_vector();
  int num_int_inputs = static_cast<int>(inputs.size());
  if (num_int_inputs > 16) num_int_inputs = 16;  // limit to 16 inputs

  for (int i = -2; i < num_int_inputs; i++) {    // for all left shapes
    for (int j = -2; j < num_int_inputs; j++) {  // for all right shapes
      if (i >= 0 && j >= 0) break;               // No constant/constant combos
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                           MachineType::Int32());
      OpIndex p0 = m.Parameter(0);
      OpIndex p1 = m.Parameter(1);
      OpIndex n0;
      OpIndex n1;

      // left = Parameter | Load | Constant
      if (i == -2) {
        n0 = p0;
      } else if (i == -1) {
        n0 = m.LoadFromPointer(&input_a, MachineType::Int32());
      } else {
        n0 = m.Word32Constant(inputs[i]);
      }

      // right = Parameter | Load | Constant
      if (j == -2) {
        n1 = p1;
      } else if (j == -1) {
        n1 = m.LoadFromPointer(&input_b, MachineType::Int32());
      } else {
        n1 = m.Word32Constant(inputs[j]);
      }

      gen->gen(&m, n0, n1);

      if (i >= 0) {
        input_a = inputs[i];
        RunRight(&m);
      } else if (j >= 0) {
        input_b = inputs[j];
        RunLeft(&m);
      } else {
        Run(&m);
      }
    }
  }
}

void Int32BinopInputShapeTester::Run(RawMachineAssemblerTester<int32_t>* m) {
  FOR_INT32_INPUTS(pl) {
    FOR_INT32_INPUTS(pr) {
      input_a = pl;
      input_b = pr;
      int32_t expect = gen->expected(input_a, input_b);
      CHECK_EQ(expect, m->Call(input_a, input_b));
    }
  }
}

void Int32BinopInputShapeTester::RunLeft(
    RawMachineAssemblerTester<int32_t>* m) {
  FOR_UINT32_INPUTS(i) {
    input_a = i;
    int32_t expect = gen->expected(input_a, input_b);
    CHECK_EQ(expect, m->Call(input_a, input_b));
  }
}

void Int32BinopInputShapeTester::RunRight(
    RawMachineAssemblerTester<int32_t>* m) {
  FOR_UINT32_INPUTS(i) {
    input_b = i;
    int32_t expect = gen->expected(input_a, input_b);
    CHECK_EQ(expect, m->Call(input_a, input_b));
  }
}

}  // namespace v8::internal::compiler::turboshaft
