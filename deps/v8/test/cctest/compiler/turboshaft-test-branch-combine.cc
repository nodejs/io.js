// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/overflowing-math.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/turboshaft-codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8::internal::compiler::turboshaft {

static TurboshaftComparison int32cmp_opcodes[] = {
    TurboshaftComparison::kWord32Equal, TurboshaftComparison::kInt32LessThan,
    TurboshaftComparison::kInt32LessThanOrEqual,
    TurboshaftComparison::kUint32LessThan,
    TurboshaftComparison::kUint32LessThanOrEqual};

TEST(BranchCombineWord32EqualZero_1) {
  // Test combining a branch with x == 0
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int32_t eq_constant = -1033;
  int32_t ne_constant = 825118;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Word32Equal(p0, m.Word32Constant(0)), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_INT32_INPUTS(a) {
    int32_t expect = a == 0 ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineWord32EqualZero_chain) {
  // Test combining a branch with a chain of x == 0 == 0 == 0 ...
  int32_t eq_constant = -1133;
  int32_t ne_constant = 815118;

  for (int k = 0; k < 6; k++) {
    RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
    OpIndex p0 = m.Parameter(0);
    Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
    V<Word32> cond = p0;
    for (int j = 0; j < k; j++) {
      cond = m.Word32Equal(cond, m.Word32Constant(0));
    }
    m.Branch(cond, blocka, blockb);
    m.Bind(blocka);
    m.Return(m.Word32Constant(eq_constant));
    m.Bind(blockb);
    m.Return(m.Word32Constant(ne_constant));

    FOR_INT32_INPUTS(a) {
      int32_t expect = (k & 1) == 1 ? (a == 0 ? eq_constant : ne_constant)
                                    : (a == 0 ? ne_constant : eq_constant);
      CHECK_EQ(expect, m.Call(a));
    }
  }
}

TEST(BranchCombineInt32LessThanZero_1) {
  // Test combining a branch with x < 0
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int32_t eq_constant = -1433;
  int32_t ne_constant = 845118;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Int32LessThan(p0, m.Word32Constant(0)), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_INT32_INPUTS(a) {
    int32_t expect = a < 0 ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineUint32LessThan100_1) {
  // Test combining a branch with x < 100
  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  int32_t eq_constant = 1471;
  int32_t ne_constant = 88845718;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Uint32LessThan(p0, m.Word32Constant(100)), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_UINT32_INPUTS(a) {
    int32_t expect = a < 100 ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineUint32LessThanOrEqual100_1) {
  // Test combining a branch with x <= 100
  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  int32_t eq_constant = 1479;
  int32_t ne_constant = 77845719;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Uint32LessThanOrEqual(p0, m.Word32Constant(100)), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_UINT32_INPUTS(a) {
    int32_t expect = a <= 100 ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineZeroLessThanInt32_1) {
  // Test combining a branch with 0 < x
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int32_t eq_constant = -2033;
  int32_t ne_constant = 225118;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Int32LessThan(m.Word32Constant(0), p0), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_INT32_INPUTS(a) {
    int32_t expect = 0 < a ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineInt32GreaterThanZero_1) {
  // Test combining a branch with x > 0
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int32_t eq_constant = -1073;
  int32_t ne_constant = 825178;
  OpIndex p0 = m.Parameter(0);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Int32GreaterThan(p0, m.Word32Constant(0)), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_INT32_INPUTS(a) {
    int32_t expect = a > 0 ? eq_constant : ne_constant;
    CHECK_EQ(expect, m.Call(a));
  }
}

TEST(BranchCombineWord32EqualP) {
  // Test combining a branch with an Word32Equal.
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  int32_t eq_constant = -1035;
  int32_t ne_constant = 825018;
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(m.Word32Equal(p0, p1), blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(eq_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(ne_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect = a == b ? eq_constant : ne_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineWord32EqualI) {
  int32_t eq_constant = -1135;
  int32_t ne_constant = 925718;

  for (int left = 0; left < 2; left++) {
    FOR_INT32_INPUTS(a) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());

      OpIndex p0 = m.Word32Constant(a);
      OpIndex p1 = m.Parameter(0);

      Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
      if (left == 1) m.Branch(m.Word32Equal(p0, p1), blocka, blockb);
      if (left == 0) m.Branch(m.Word32Equal(p1, p0), blocka, blockb);
      m.Bind(blocka);
      m.Return(m.Word32Constant(eq_constant));
      m.Bind(blockb);
      m.Return(m.Word32Constant(ne_constant));

      FOR_INT32_INPUTS(b) {
        int32_t expect = a == b ? eq_constant : ne_constant;
        CHECK_EQ(expect, m.Call(b));
      }
    }
  }
}

TEST(BranchCombineInt32CmpP) {
  int32_t eq_constant = -1235;
  int32_t ne_constant = 725018;

  for (int op = 0; op < 2; op++) {
    RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                         MachineType::Int32());
    OpIndex p0 = m.Parameter(0);
    OpIndex p1 = m.Parameter(1);

    Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
    if (op == 0) m.Branch(m.Int32LessThan(p0, p1), blocka, blockb);
    if (op == 1) m.Branch(m.Int32LessThanOrEqual(p0, p1), blocka, blockb);
    m.Bind(blocka);
    m.Return(m.Word32Constant(eq_constant));
    m.Bind(blockb);
    m.Return(m.Word32Constant(ne_constant));

    FOR_INT32_INPUTS(a) {
      FOR_INT32_INPUTS(b) {
        int32_t expect = 0;
        if (op == 0) expect = a < b ? eq_constant : ne_constant;
        if (op == 1) expect = a <= b ? eq_constant : ne_constant;
        CHECK_EQ(expect, m.Call(a, b));
      }
    }
  }
}

TEST(BranchCombineInt32CmpI) {
  int32_t eq_constant = -1175;
  int32_t ne_constant = 927711;

  for (int op = 0; op < 2; op++) {
    FOR_INT32_INPUTS(a) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      OpIndex p0 = m.Word32Constant(a);
      OpIndex p1 = m.Parameter(0);

      Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
      if (op == 0) m.Branch(m.Int32LessThan(p0, p1), blocka, blockb);
      if (op == 1) m.Branch(m.Int32LessThanOrEqual(p0, p1), blocka, blockb);
      m.Bind(blocka);
      m.Return(m.Word32Constant(eq_constant));
      m.Bind(blockb);
      m.Return(m.Word32Constant(ne_constant));

      FOR_INT32_INPUTS(b) {
        int32_t expect = 0;
        if (op == 0) expect = a < b ? eq_constant : ne_constant;
        if (op == 1) expect = a <= b ? eq_constant : ne_constant;
        CHECK_EQ(expect, m.Call(b));
      }
    }
  }
}

// Now come the sophisticated tests for many input shape combinations.

// Materializes a boolean (1 or 0) from a comparison.
class CmpMaterializeBoolGen : public BinopGen<int32_t> {
 public:
  CompareWrapper w;
  bool invert;

  CmpMaterializeBoolGen(TurboshaftComparison op, bool i) : w(op), invert(i) {}

  void gen(RawMachineAssemblerTester<int32_t>* m, OpIndex a,
           OpIndex b) override {
    OpIndex cond = w.MakeNode(m, a, b);
    if (invert) cond = m->Word32Equal(cond, m->Word32Constant(0));
    m->Return(cond);
  }
  int32_t expected(int32_t a, int32_t b) override {
    if (invert) return !w.Int32Compare(a, b) ? 1 : 0;
    return w.Int32Compare(a, b) ? 1 : 0;
  }
};

// Generates a branch and return one of two values from a comparison.
class CmpBranchGen : public BinopGen<int32_t> {
 public:
  CompareWrapper w;
  bool invert;
  bool true_first;
  int32_t eq_constant;
  int32_t ne_constant;

  CmpBranchGen(TurboshaftComparison op, bool i, bool t, int32_t eq, int32_t ne)
      : w(op), invert(i), true_first(t), eq_constant(eq), ne_constant(ne) {}

  void gen(RawMachineAssemblerTester<int32_t>* m, OpIndex a,
           OpIndex b) override {
    Block *blocka = m->NewBlock(), *blockb = m->NewBlock();
    V<Word32> cond = w.MakeNode(m, a, b);
    if (invert) cond = m->Word32Equal(cond, m->Word32Constant(0));
    m->Branch(cond, blocka, blockb);
    if (true_first) {
      m->Bind(blocka);
      m->Return(m->Word32Constant(eq_constant));
      m->Bind(blockb);
      m->Return(m->Word32Constant(ne_constant));
    } else {
      m->Bind(blockb);
      m->Return(m->Word32Constant(ne_constant));
      m->Bind(blocka);
      m->Return(m->Word32Constant(eq_constant));
    }
  }
  int32_t expected(int32_t a, int32_t b) override {
    if (invert) return !w.Int32Compare(a, b) ? eq_constant : ne_constant;
    return w.Int32Compare(a, b) ? eq_constant : ne_constant;
  }
};

TEST(BranchCombineInt32CmpAllInputShapes_materialized) {
  for (size_t i = 0; i < arraysize(int32cmp_opcodes); i++) {
    CmpMaterializeBoolGen gen(int32cmp_opcodes[i], false);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineInt32CmpAllInputShapes_inverted_materialized) {
  for (size_t i = 0; i < arraysize(int32cmp_opcodes); i++) {
    CmpMaterializeBoolGen gen(int32cmp_opcodes[i], true);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineInt32CmpAllInputShapes_branch_true) {
  for (int i = 0; i < static_cast<int>(arraysize(int32cmp_opcodes)); i++) {
    CmpBranchGen gen(int32cmp_opcodes[i], false, false, 995 + i, -1011 - i);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineInt32CmpAllInputShapes_branch_false) {
  for (int i = 0; i < static_cast<int>(arraysize(int32cmp_opcodes)); i++) {
    CmpBranchGen gen(int32cmp_opcodes[i], false, true, 795 + i, -2011 - i);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineInt32CmpAllInputShapes_inverse_branch_true) {
  for (int i = 0; i < static_cast<int>(arraysize(int32cmp_opcodes)); i++) {
    CmpBranchGen gen(int32cmp_opcodes[i], true, false, 695 + i, -3011 - i);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineInt32CmpAllInputShapes_inverse_branch_false) {
  for (int i = 0; i < static_cast<int>(arraysize(int32cmp_opcodes)); i++) {
    CmpBranchGen gen(int32cmp_opcodes[i], true, true, 595 + i, -4011 - i);
    Int32BinopInputShapeTester tester(&gen);
    tester.TestAllInputShapes();
  }
}

TEST(BranchCombineFloat64Compares) {
  double inf = V8_INFINITY;
  double nan = std::numeric_limits<double>::quiet_NaN();
  double inputs[] = {0.0, 1.0, -1.0, -inf, inf, nan};

  int32_t eq_constant = -1733;
  int32_t ne_constant = 915118;

  double input_a = 0.0;
  double input_b = 0.0;

  CompareWrapper cmps[] = {
      CompareWrapper(TurboshaftComparison::kFloat64Equal),
      CompareWrapper(TurboshaftComparison::kFloat64LessThan),
      CompareWrapper(TurboshaftComparison::kFloat64LessThanOrEqual)};

  for (size_t c = 0; c < arraysize(cmps); c++) {
    CompareWrapper cmp = cmps[c];
    for (int invert = 0; invert < 2; invert++) {
      RawMachineAssemblerTester<int32_t> m;
      OpIndex a = m.LoadFromPointer(&input_a, MachineType::Float64());
      OpIndex b = m.LoadFromPointer(&input_b, MachineType::Float64());

      Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
      V<Word32> cond = cmp.MakeNode(&m, a, b);
      if (invert) cond = m.Word32Equal(cond, m.Word32Constant(0));
      m.Branch(cond, blocka, blockb);
      m.Bind(blocka);
      m.Return(m.Word32Constant(eq_constant));
      m.Bind(blockb);
      m.Return(m.Word32Constant(ne_constant));

      for (size_t i = 0; i < arraysize(inputs); ++i) {
        for (size_t j = 0; j < arraysize(inputs); ++j) {
          input_a = inputs[i];
          input_b = inputs[j];
          int32_t expected =
              invert ? (cmp.Float64Compare(input_a, input_b) ? ne_constant
                                                             : eq_constant)
                     : (cmp.Float64Compare(input_a, input_b) ? eq_constant
                                                             : ne_constant);
          CHECK_EQ(expected, m.Call());
        }
      }
    }
  }
}

TEST(BranchCombineEffectLevel) {
  // Test that the load doesn't get folded into the branch, as there's a store
  // between them. See http://crbug.com/611976.
  int32_t input = 0;

  RawMachineAssemblerTester<int32_t> m;
  OpIndex a = m.LoadFromPointer(&input, MachineType::Int32());
  V<Word32> compare = m.Word32BitwiseAnd(a, m.Word32Constant(1));
  V<Word32> equal = m.Word32Equal(compare, m.Word32Constant(0));
  m.StoreToPointer(&input, MachineRepresentation::kWord32, m.Word32Constant(1));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(equal, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(42));
  m.Bind(blockb);
  m.Return(m.Word32Constant(0));

  CHECK_EQ(42, m.Call());
}

TEST(BranchCombineWord32AddLessThanZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Int32LessThan(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect =
          (base::AddWithWraparound(a, b) < 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineWord32AddGreaterThanOrEqualZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Int32GreaterThanOrEqual(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect =
          (base::AddWithWraparound(a, b) >= 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineInt32ZeroGreaterThanAdd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Int32GreaterThan(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect =
          (0 > base::AddWithWraparound(a, b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineInt32ZeroLessThanOrEqualAdd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Int32LessThanOrEqual(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect =
          (0 <= base::AddWithWraparound(a, b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUWord32AddLessThanOrEqualZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Uint32LessThanOrEqual(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (a + b <= 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUWord32AddGreaterThanZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Uint32GreaterThan(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (a + b > 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32ZeroGreaterThanOrEqualAdd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Uint32GreaterThanOrEqual(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (0 >= a + b) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32ZeroLessThanAdd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32Add(p0, p1);
  V<Word32> compare = m.Uint32LessThan(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (0 < a + b) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineWord32BitwiseAndLessThanZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Int32LessThan(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect = ((a & b) < 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineWord32BitwiseAndGreaterThanOrEqualZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Int32GreaterThanOrEqual(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect = ((a & b) >= 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineInt32ZeroGreaterThanAnd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Int32GreaterThan(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect = (0 > (a & b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineInt32ZeroLessThanOrEqualAnd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Int32LessThanOrEqual(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_INT32_INPUTS(a) {
    FOR_INT32_INPUTS(b) {
      int32_t expect = (0 <= (a & b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32AndLessThanOrEqualZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Uint32LessThanOrEqual(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = ((a & b) <= 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32AndGreaterThanZero) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Uint32GreaterThan(add, m.Word32Constant(0));

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = ((a & b) > 0) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32ZeroGreaterThanOrEqualAnd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Uint32GreaterThanOrEqual(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (0 >= (a & b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

TEST(BranchCombineUint32ZeroLessThanAnd) {
  int32_t t_constant = -1033;
  int32_t f_constant = 825118;

  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                       MachineType::Uint32());
  OpIndex p0 = m.Parameter(0);
  OpIndex p1 = m.Parameter(1);
  OpIndex add = m.Word32BitwiseAnd(p0, p1);
  V<Word32> compare = m.Uint32LessThan(m.Word32Constant(0), add);

  Block *blocka = m.NewBlock(), *blockb = m.NewBlock();
  m.Branch(compare, blocka, blockb);
  m.Bind(blocka);
  m.Return(m.Word32Constant(t_constant));
  m.Bind(blockb);
  m.Return(m.Word32Constant(f_constant));

  FOR_UINT32_INPUTS(a) {
    FOR_UINT32_INPUTS(b) {
      int32_t expect = (0 < (a & b)) ? t_constant : f_constant;
      CHECK_EQ(expect, m.Call(a, b));
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft
