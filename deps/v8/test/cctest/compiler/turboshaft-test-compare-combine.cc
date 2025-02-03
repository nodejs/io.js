// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/turboshaft-codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8::internal::compiler::turboshaft {

namespace {

constexpr TurboshaftBinop kLogicOpcodes[] = {TurboshaftBinop::kWord32BitwiseAnd,
                                             TurboshaftBinop::kWord32BitwiseOr};
constexpr std::array kInt32CmpOpcodes = {
    TurboshaftComparison::kWord32Equal, TurboshaftComparison::kInt32LessThan,
    TurboshaftComparison::kInt32LessThanOrEqual,
    TurboshaftComparison::kUint32LessThan,
    TurboshaftComparison::kUint32LessThanOrEqual};
#if V8_TARGET_ARCH_64_BIT
constexpr std::array kInt64CmpOpcodes = {
    TurboshaftComparison::kWord64Equal, TurboshaftComparison::kInt64LessThan,
    TurboshaftComparison::kInt64LessThanOrEqual,
    TurboshaftComparison::kUint64LessThan,
    TurboshaftComparison::kUint64LessThanOrEqual};
#endif

enum GraphShape { kBalanced, kUnbalanced };
enum InvertPattern {
  kNoInvert,
  kInvertCompare,
  kInvertLogic,
  kInvertCompareAndLogic,
  kInvertCompareDouble,
  kInvertLogicDouble
};
enum BranchPattern { kNone, kDirect, kEqualZero, kNotEqualZero };

constexpr GraphShape kGraphShapes[] = {kBalanced, kUnbalanced};
constexpr InvertPattern kInvertPatterns[] = {kNoInvert,
                                             kInvertCompare,
                                             kInvertLogic,
                                             kInvertCompareAndLogic,
                                             kInvertCompareDouble,
                                             kInvertLogicDouble};
constexpr BranchPattern kBranchPatterns[] = {kNone, kDirect, kEqualZero,
                                             kNotEqualZero};

// These are shorter versions of ValueHelper::uint32_vector() and
// ValueHelper::uint64_vector() (which are used by FOR_UINT32_INPUTS and
// FOR_UINT64_INPUTS).
static constexpr uint32_t uint32_test_array[] = {
    0x00000000, 0x00000001, 0xFFFFFFFF, 0x1B09788B, 0x00000005,
    0x00000008, 0x273A798E, 0x56123761, 0xFFFFFFFD, 0x001FFFFF,
    0x0007FFFF, 0x7FC00000, 0x7F876543};
static constexpr auto uint32_test_vector = base::VectorOf(uint32_test_array);
#ifdef V8_TARGET_ARCH_64_BIT
static constexpr uint64_t uint64_test_array[] = {
    0x00000000,         0x00000001,         0xFFFFFFFF,
    0x1B09788B,         0x00000008,         0xFFFFFFFFFFFFFFFF,
    0xFFFFFFFFFFFFFFFE, 0x0000000100000000, 0x1B09788B00000000,
    0x273A798E187937A3, 0xECE3AF835495A16B, 0x80000000EEEEEEEE,
    0x007FFFFFDDDDDDDD, 0x8000000000000000, 0x7FF8000000000000,
    0x7FF7654321FEDCBA};
static constexpr auto uint64_test_vector = base::VectorOf(uint64_test_array);
#endif

// Given kGraphShapes and kInvertPatterns, defined above, the graphs produced by
// the test framework, with four compares, are illustrated below. In the cases
// where we insert a branch, this takes the final logic node as the input.

// kBalanced - kNoInvert
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//           ---------> logic <--------
//

// kBalanced - kInvertCompare
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//           |                         |
//           ---------> logic <--------

// kBalanced - kInvertCompareDouble
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//           |                         |
//           ---------> logic <--------

// kBalanced - kInvertLogic
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//          not                        |
//           ---------> logic <--------

// kBalanced - kInvertLogicDouble
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//          not                        |
//           |                         |
//          not                        |
//           ---------> logic <--------

// kBalanced - kInvertCompareAndLogic
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             --> logic <--
//           |                         |
//          not                        |
//           ---------> logic <--------

// kUnbalanced - kNoInvert
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//            --------> logic <--             |
//                        |                   |
//                         -----> logic <-----

// kUnbalanced - kInvertCompare
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//            --------> logic <--             |
//                        |                   |
//                         -----> logic <-----

// kUnbalanced - kInvertCompareDouble
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//            --------> logic <--             |
//                        |                   |
//                         -----> logic <-----

// kUnbalanced - kInvertLogic
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//          not                  |            |
//            --------> logic <--             |
//                        |                   |
//                       not                  |
//                        |                   |
//                         -----> logic <-----

// kUnbalanced - kInvertLogicDouble
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//          not                  |            |
//           |                   |            |
//          not                  |            |
//            --------> logic <--             |
//                        |                   |
//                       not                  |
//                        |                   |
//                       not                  |
//                        |                   |
//                         -----> logic <-----

// kUnbalanced - kInvertCompareAndLogic
// a       b    c       d    a        b   c       d
// |       |    |       |    |        |   |       |
// |       |    |       |    |        |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-
//     |            |            |            |
//    not           |           not           |
//     |            |            |            |
//     --> logic <--             |            |
//           |                   |            |
//          not                  |            |
//            --------> logic <--             |
//                        |                   |
//                       not                  |
//                        |                   |
//                         -----> logic <-----

template <uint32_t NumLogic, typename CompareType>
class CombineCompares {
  static constexpr uint32_t NumInputs = 4;
  static constexpr uint32_t NumCompares = NumLogic + 1;
  static_assert(NumLogic > 0);

  // a       b    c       d    a        b       NumInputs = 4
  // |       |    |       |    |        |
  // |       |    |       |    |        |
  // -> cmp <-    -> cmp <-    -> cmp <-        NumCompares = 3
  //     |            |            |
  //     --> logic <--             |            ---------
  //           |                   |            NumLogic = 2
  //           ------> logic <-----             ---------

 public:
  CombineCompares(RawMachineAssemblerTester<uint32_t>& m, GraphShape shape,
                  InvertPattern invert_pattern, BranchPattern branch_pattern,
                  std::array<TurboshaftBinop, NumLogic> logic_ops,
                  std::array<TurboshaftComparison, NumCompares> compare_ops)
      : m_(m),
        graph_shape_(shape),
        invert_pattern_(invert_pattern),
        branch_pattern_(branch_pattern),
        logic_ops_(logic_ops),
        compare_ops_(compare_ops) {}

  void GenerateReturn(V<Word32> combine) {
    if (branch_pattern() == kNone) {
      m().Return(combine);
    } else {
      blocka_ = m().NewBlock();
      blockb_ = m().NewBlock();
      if (branch_pattern() == kDirect) {
        m().Branch(static_cast<V<Word32>>(combine), blocka(), blockb());
      } else if (branch_pattern() == kEqualZero) {
        m().Branch(m().Word32Equal(combine, m().Word32Constant(0)), blocka(),
                   blockb());
      } else {
        auto cond = static_cast<V<Word32>>(
            MakeNot(m().Word32Equal(combine, m().Word32Constant(0))));
        m().Branch(cond, blocka(), blockb());
      }
      m().Bind(blocka());
      m().Return(m().Word32Constant(1));
      m().Bind(blockb());
      m().Return(m().Word32Constant(0));
    }
  }

  V<Word32> MakeBinop(TurboshaftBinop op, V<Word32> lhs, V<Word32> rhs) {
    switch (op) {
      case TurboshaftBinop::kWord32BitwiseAnd:
        return m().Word32BitwiseAnd(lhs, rhs);
      case TurboshaftBinop::kWord32BitwiseOr:
        return m().Word32BitwiseOr(lhs, rhs);
      default:
        UNREACHABLE();
    }
  }

  V<Word32> MakeCompare(TurboshaftComparison op, OpIndex lhs, OpIndex rhs) {
    switch (op) {
      default:
        UNREACHABLE();
      case TurboshaftComparison::kWord32Equal:
        return m().Word32Equal(lhs, rhs);
      case TurboshaftComparison::kInt32LessThan:
        return m().Int32LessThan(lhs, rhs);
      case TurboshaftComparison::kInt32LessThanOrEqual:
        return m().Int32LessThanOrEqual(lhs, rhs);
      case TurboshaftComparison::kUint32LessThan:
        return m().Uint32LessThan(lhs, rhs);
      case TurboshaftComparison::kUint32LessThanOrEqual:
        return m().Uint32LessThanOrEqual(lhs, rhs);
      case TurboshaftComparison::kWord64Equal:
        return m().Word64Equal(lhs, rhs);
      case TurboshaftComparison::kInt64LessThan:
        return m().Int64LessThan(lhs, rhs);
      case TurboshaftComparison::kInt64LessThanOrEqual:
        return m().Int64LessThanOrEqual(lhs, rhs);
      case TurboshaftComparison::kUint64LessThan:
        return m().Uint64LessThan(lhs, rhs);
      case TurboshaftComparison::kUint64LessThanOrEqual:
        return m().Uint64LessThanOrEqual(lhs, rhs);
    }
  }

  V<Word32> MakeNot(V<Word32> node) {
    return m().Word32Equal(node, m().Word32Constant(0));
  }

  V<Word32> MakeNotCompare(V<Word32> node) {
    V<Word32> inverted = MakeNot(node);
    if (invert_pattern() == kInvertCompareDouble) {
      return MakeNot(inverted);
    } else {
      return inverted;
    }
  }

  V<Word32> MakeNotLogic(V<Word32> node) {
    V<Word32> inverted = MakeNot(node);
    if (invert_pattern() == kInvertLogicDouble) {
      return MakeNot(inverted);
    } else {
      return inverted;
    }
  }

  bool ShouldDoubleInvert() const {
    return invert_pattern() == kInvertLogicDouble ||
           invert_pattern() == kInvertCompareDouble;
  }

  bool ShouldInvertLogic() const {
    return invert_pattern() == kInvertLogic ||
           invert_pattern() == kInvertCompareAndLogic ||
           invert_pattern() == kInvertLogicDouble;
  }

  bool ShouldInvertCompare() const {
    return invert_pattern() == kInvertCompare ||
           invert_pattern() == kInvertCompareAndLogic ||
           invert_pattern() == kInvertCompareDouble;
  }

  void BuildGraph(std::array<OpIndex, NumInputs>& inputs) {
    std::array<V<Word32>, NumCompares> compares;

    for (unsigned i = 0; i < NumCompares; ++i) {
      OpIndex a = inputs.at((2 * i) % NumInputs);
      OpIndex b = inputs.at((2 * i + 1) % NumInputs);
      V<Word32> cmp = MakeCompare(CompareOpcode(i), a, b);
      // Invert every other compare, starting with the first.
      if (ShouldInvertCompare() && (i % 1)) {
        compares[i] = MakeNotCompare(cmp);
      } else {
        compares[i] = cmp;
      }
    }

    V<Word32> first_combine =
        MakeBinop(LogicOpcode(0), compares[0], compares[1]);
    if (NumLogic == 1) {
      if (ShouldInvertLogic()) {
        return GenerateReturn(MakeNotLogic(first_combine));
      }
      return GenerateReturn(first_combine);
    }

    if (graph_shape() == kUnbalanced) {
      V<Word32> combine = first_combine;
      for (unsigned i = 1; i < NumLogic; ++i) {
        // Invert every other logic operation, beginning with the first.
        if (ShouldInvertLogic() && (i % 1)) {
          combine = MakeNotLogic(combine);
        }
        combine = MakeBinop(LogicOpcode(i), compares.at(i + 1), combine);
      }
      return GenerateReturn(combine);
    } else {
      constexpr uint32_t NumFirstLayerLogic = NumCompares / 2;
      std::array<V<Word32>, NumFirstLayerLogic> first_layer_logic{
          first_combine};
      for (unsigned i = 1; i < NumFirstLayerLogic; ++i) {
        first_layer_logic[i] = MakeBinop(LogicOpcode(i), compares.at(2 * i),
                                         compares.at(2 * i + 1));
      }
      V<Word32> combine = first_combine;
      // Invert every other first layer logic operation, beginning with the
      // first.
      if (ShouldInvertLogic()) {
        combine = MakeNotLogic(combine);
      }
      for (unsigned i = 1; i < NumFirstLayerLogic; ++i) {
        V<Word32> logic_node = first_layer_logic.at(i);
        if (ShouldInvertLogic() && !(i % 2)) {
          logic_node = MakeNotLogic(logic_node);
        }
        uint32_t logic_idx = NumFirstLayerLogic + i - 1;
        combine = MakeBinop(LogicOpcode(logic_idx), logic_node, combine);
      }
      GenerateReturn(combine);
    }
  }

  uint32_t ExpectedReturn(uint32_t combine) const {
    if (branch_pattern() == kNone) {
      return combine;
    } else if (branch_pattern() == kDirect) {
      return combine == 0 ? 0 : 1;
    } else if (branch_pattern() == kEqualZero) {
      return combine == 0 ? 1 : 0;
    } else {
      return combine != 0 ? 1 : 0;
    }
  }

  uint32_t InvertCompare(uint32_t v) const {
    return invert_pattern() == kInvertCompareDouble ? v : !v;
  }

  uint32_t InvertLogic(uint32_t v) const {
    return invert_pattern() == kInvertLogicDouble ? v : !v;
  }

  uint32_t Expected(std::array<CompareType, NumInputs>& inputs) {
    std::array<uint32_t, NumCompares> compare_results;
    for (unsigned i = 0; i < NumCompares; ++i) {
      CompareType cmp_lhs = inputs.at((2 * i) % NumInputs);
      CompareType cmp_rhs = inputs.at((2 * i + 1) % NumInputs);
      CompareWrapper cmpw = CompareWrapper(CompareOpcode(i));
      uint32_t cmp_res = EvalCompare(cmpw, cmp_lhs, cmp_rhs);
      // Invert every other compare, starting with the first.
      if (ShouldInvertCompare() && (i % 1)) {
        compare_results[i] = InvertCompare(cmp_res);
      } else {
        compare_results[i] = cmp_res;
      }
    }

    auto logicw = IntBinopWrapper<uint32_t>(LogicOpcode(0));
    uint32_t first_combine =
        logicw.eval(compare_results[0], compare_results[1]);
    if (NumLogic == 1) {
      if (ShouldInvertLogic()) {
        first_combine = InvertLogic(first_combine);
      }
      return ExpectedReturn(first_combine);
    }

    if (graph_shape() == kUnbalanced) {
      uint32_t combine = first_combine;
      for (unsigned i = 1; i < NumLogic; ++i) {
        // Invert every other logic operation, beginning with the first.
        if (ShouldInvertLogic() && (i % 1)) {
          combine = InvertLogic(combine);
        }
        logicw = IntBinopWrapper<uint32_t>(LogicOpcode(i));
        combine = logicw.eval(compare_results.at(i + 1), combine);
      }
      return ExpectedReturn(combine);
    } else {
      constexpr uint32_t NumFirstLayerLogic = NumCompares / 2;
      std::array<uint32_t, NumFirstLayerLogic> first_layer_logic{first_combine};
      for (unsigned i = 1; i < NumFirstLayerLogic; ++i) {
        logicw = IntBinopWrapper<uint32_t>(LogicOpcode(i));
        first_layer_logic[i] = logicw.eval(compare_results.at(2 * i),
                                           compare_results.at(2 * i + 1));
      }
      uint32_t combine = first_combine;
      // Invert every other first layer logic operation, beginning with the
      // first.
      if (ShouldInvertLogic()) {
        combine = InvertLogic(combine);
      }
      for (unsigned i = 1; i < NumFirstLayerLogic; ++i) {
        uint32_t logic_res = first_layer_logic.at(i);
        if (ShouldInvertLogic() && !(i % 2)) {
          logic_res = InvertLogic(logic_res);
        }
        uint32_t logic_idx = NumFirstLayerLogic + i - 1;
        logicw = IntBinopWrapper<uint32_t>(LogicOpcode(logic_idx));
        combine = logicw.eval(logic_res, combine);
      }
      return ExpectedReturn(combine);
    }
  }

  virtual uint32_t EvalCompare(CompareWrapper& cmpw, CompareType lhs,
                               CompareType rhs) const = 0;
  virtual OpIndex Zero() const = 0;
  virtual OpIndex One() const = 0;
  virtual OpIndex ThirtyTwo() const = 0;

  RawMachineAssemblerTester<uint32_t>& m() const { return m_; }
  GraphShape graph_shape() const { return graph_shape_; }
  InvertPattern invert_pattern() const { return invert_pattern_; }
  BranchPattern branch_pattern() const { return branch_pattern_; }
  TurboshaftBinop LogicOpcode(uint32_t i) const { return logic_ops_.at(i); }
  TurboshaftComparison CompareOpcode(uint32_t i) const {
    return compare_ops_.at(i);
  }
  Block* blocka() { return blocka_; }
  Block* blockb() { return blockb_; }

 private:
  RawMachineAssemblerTester<uint32_t>& m_;
  GraphShape graph_shape_;
  InvertPattern invert_pattern_;
  BranchPattern branch_pattern_;
  Block* blocka_;
  Block* blockb_;
  std::array<TurboshaftBinop, NumLogic> logic_ops_;
  std::array<TurboshaftComparison, NumCompares> compare_ops_;
};

template <uint32_t NumLogic>
class CombineCompareWord32 : public CombineCompares<NumLogic, uint32_t> {
 public:
  using CombineCompares<NumLogic, uint32_t>::CombineCompares;
  uint32_t EvalCompare(CompareWrapper& cmpw, uint32_t lhs,
                       uint32_t rhs) const override {
    return cmpw.Int32Compare(lhs, rhs);
  }
  OpIndex Zero() const override { return this->m().Word32Constant(0); }
  OpIndex One() const override { return this->m().Word32Constant(1); }
  OpIndex ThirtyTwo() const override { return this->m().Word32Constant(32); }
};

template <uint32_t NumLogic>
class CombineCompareWord64 : public CombineCompares<NumLogic, uint64_t> {
 public:
  using CombineCompares<NumLogic, uint64_t>::CombineCompares;
  uint32_t EvalCompare(CompareWrapper& cmpw, uint64_t lhs,
                       uint64_t rhs) const override {
    return cmpw.Int64Compare(lhs, rhs);
  }
  OpIndex Zero() const override {
    return this->m().Word64Constant(static_cast<uint64_t>(0));
  }
  OpIndex One() const override {
    return this->m().Word64Constant(static_cast<uint64_t>(1));
  }
  OpIndex ThirtyTwo() const override {
    return this->m().Word64Constant(static_cast<uint64_t>(32));
  }
};

template <typename Combiner, typename InputType>
void CombineCompareLogic1(
    const std::array<TurboshaftComparison, 5>& cmp_opcodes,
    MachineType (*input_type)(void),
    const base::Vector<const InputType>& input_vector) {
  constexpr GraphShape shape = kBalanced;
  for (auto cmp0 : cmp_opcodes) {
    for (auto cmp1 : cmp_opcodes) {
      for (auto logic : kLogicOpcodes) {
        for (auto invert_pattern : kInvertPatterns) {
          for (auto branch_pattern : kBranchPatterns) {
            RawMachineAssemblerTester<uint32_t> m(input_type(), input_type(),
                                                  input_type(), input_type());
            std::array logic_ops = {logic};
            std::array compare_ops = {cmp0, cmp1};
            Combiner gen(m, shape, invert_pattern, branch_pattern, logic_ops,
                         compare_ops);
            std::array inputs = {
                m.Parameter(0),
                m.Parameter(1),
                m.Parameter(2),
                m.Parameter(3),
            };
            gen.BuildGraph(inputs);

            for (auto a : input_vector) {
              for (auto b : input_vector) {
                std::array<InputType, 4> inputs{a, b, b, a};
                uint32_t expected = gen.Expected(inputs);
                uint32_t actual = m.Call(a, b, b, a);
                CHECK_EQ(expected, actual);
              }
            }
          }
        }
      }
    }
  }
}
TEST(CombineCompareWord32Logic1) {
  CombineCompareLogic1<CombineCompareWord32<1>, uint32_t>(
      kInt32CmpOpcodes, MachineType::Uint32, uint32_test_vector);
}
#if V8_TARGET_ARCH_64_BIT
TEST(CombineCompareWord64Logic1) {
  CombineCompareLogic1<CombineCompareWord64<1>, uint64_t>(
      kInt64CmpOpcodes, MachineType::Uint64, uint64_test_vector);
}
#endif

template <typename Combiner, typename InputType>
void CombineCompareLogic2(
    const std::array<TurboshaftComparison, 5>& cmp_opcodes,
    MachineType (*input_type)(void),
    const base::Vector<const InputType>& input_vector) {
  constexpr GraphShape shape = kUnbalanced;
  constexpr BranchPattern branch_pattern = kNone;
  auto cmp0 = cmp_opcodes[3];
  auto cmp1 = cmp_opcodes[2];
  auto cmp2 = cmp_opcodes[1];
  std::array compare_ops = {cmp0, cmp1, cmp2};
  for (auto logic0 : kLogicOpcodes) {
    for (auto logic1 : kLogicOpcodes) {
      for (auto invert_pattern : kInvertPatterns) {
        RawMachineAssemblerTester<uint32_t> m(input_type(), input_type(),
                                              input_type(), input_type());
        std::array logic_ops = {logic0, logic1};
        Combiner gen(m, shape, invert_pattern, branch_pattern, logic_ops,
                     compare_ops);
        std::array inputs = {
            m.Parameter(0),
            m.Parameter(1),
            m.Parameter(2),
            m.Parameter(3),
        };
        gen.BuildGraph(inputs);

        for (auto a : input_vector) {
          for (auto b : input_vector) {
            std::array<InputType, 4> inputs{a, b, b, a};
            uint32_t expected = gen.Expected(inputs);
            uint32_t actual = m.Call(a, b, b, a);
            CHECK_EQ(expected, actual);
          }
        }
      }
    }
  }
}
TEST(CombineCompareWord32Logic2) {
  CombineCompareLogic2<CombineCompareWord32<2>, uint32_t>(
      kInt32CmpOpcodes, MachineType::Uint32, uint32_test_vector);
}
#if V8_TARGET_ARCH_64_BIT
TEST(CombineCompareWord64Logic2) {
  CombineCompareLogic2<CombineCompareWord64<2>, uint64_t>(
      kInt64CmpOpcodes, MachineType::Uint64, uint64_test_vector);
}
#endif

template <typename Combiner, typename InputType>
void CombineCompareLogic3Zero(
    const std::array<TurboshaftComparison, 5>& cmp_opcodes,
    MachineType (*input_type)(void),
    const base::Vector<const InputType>& input_vector) {
  constexpr BranchPattern branch_pattern = kNone;
  auto cmp0 = cmp_opcodes[0];
  auto cmp1 = cmp_opcodes[1];
  auto cmp2 = cmp_opcodes[2];
  auto cmp3 = cmp_opcodes[3];
  std::array compare_ops = {cmp0, cmp1, cmp2, cmp3};
  for (auto logic0 : kLogicOpcodes) {
    for (auto logic1 : kLogicOpcodes) {
      for (auto logic2 : kLogicOpcodes) {
        for (auto shape : kGraphShapes) {
          for (auto invert_pattern : kInvertPatterns) {
            RawMachineAssemblerTester<uint32_t> m(input_type(), input_type(),
                                                  input_type(), input_type());
            std::array logic_ops = {logic0, logic1, logic2};
            Combiner gen(m, shape, invert_pattern, branch_pattern, logic_ops,
                         compare_ops);
            std::array inputs = {
                m.Parameter(0),
                m.Parameter(1),
                gen.Zero(),
                m.Parameter(3),
            };
            gen.BuildGraph(inputs);

            for (auto a : input_vector) {
              for (auto b : input_vector) {
                std::array<InputType, 4> inputs{a, b, 0, a};
                uint32_t expected = gen.Expected(inputs);
                uint32_t actual = m.Call(a, b, b, a);
                CHECK_EQ(expected, actual);
              }
            }
          }
        }
      }
    }
  }
}
TEST(CombineCompareWord32Logic3Zero) {
  CombineCompareLogic3Zero<CombineCompareWord32<3>, uint32_t>(
      kInt32CmpOpcodes, MachineType::Uint32, uint32_test_vector);
}
#if V8_TARGET_ARCH_64_BIT
TEST(CombineCompareWord64Logic3Zero) {
  CombineCompareLogic3Zero<CombineCompareWord64<3>, uint64_t>(
      kInt64CmpOpcodes, MachineType::Uint64, uint64_test_vector);
}
#endif

template <typename Combiner, typename InputType>
void CombineCompareLogic3One(
    const std::array<TurboshaftComparison, 5>& cmp_opcodes,
    MachineType (*input_type)(void),
    const base::Vector<const InputType>& input_vector) {
  constexpr BranchPattern branch_pattern = kNone;
  auto cmp0 = cmp_opcodes[4];
  auto cmp1 = cmp_opcodes[1];
  auto cmp2 = cmp_opcodes[2];
  auto cmp3 = cmp_opcodes[0];
  std::array compare_ops = {cmp0, cmp1, cmp2, cmp3};
  for (auto logic0 : kLogicOpcodes) {
    for (auto logic1 : kLogicOpcodes) {
      for (auto logic2 : kLogicOpcodes) {
        for (auto shape : kGraphShapes) {
          for (auto invert_pattern : kInvertPatterns) {
            RawMachineAssemblerTester<uint32_t> m(input_type(), input_type(),
                                                  input_type(), input_type());
            std::array logic_ops = {logic0, logic1, logic2};
            Combiner gen(m, shape, invert_pattern, branch_pattern, logic_ops,
                         compare_ops);
            std::array inputs = {
                gen.One(),
                m.Parameter(1),
                m.Parameter(2),
                m.Parameter(3),
            };
            gen.BuildGraph(inputs);

            for (auto a : input_vector) {
              for (auto b : input_vector) {
                std::array<InputType, 4> inputs{1, b, b, a};
                uint32_t expected = gen.Expected(inputs);
                uint32_t actual = m.Call(a, b, b, a);
                CHECK_EQ(expected, actual);
              }
            }
          }
        }
      }
    }
  }
}
TEST(CombineCompareWord32Logic3One) {
  CombineCompareLogic3One<CombineCompareWord32<3>, uint32_t>(
      kInt32CmpOpcodes, MachineType::Uint32, uint32_test_vector);
}
#if V8_TARGET_ARCH_64_BIT
TEST(CombineCompareWord64Logic3One) {
  CombineCompareLogic3One<CombineCompareWord64<3>, uint64_t>(
      kInt64CmpOpcodes, MachineType::Uint64, uint64_test_vector);
}
#endif

template <typename Combiner, typename InputType>
void CombineCompareLogic3ThirtyTwo(
    const std::array<TurboshaftComparison, 5>& cmp_opcodes,
    MachineType (*input_type)(void),
    const base::Vector<const InputType>& input_vector) {
  constexpr BranchPattern branch_pattern = kNone;
  auto cmp0 = cmp_opcodes[0];
  auto cmp1 = cmp_opcodes[3];
  auto cmp2 = cmp_opcodes[2];
  auto cmp3 = cmp_opcodes[4];
  std::array compare_ops = {cmp0, cmp1, cmp2, cmp3};
  for (auto logic0 : kLogicOpcodes) {
    for (auto logic1 : kLogicOpcodes) {
      for (auto logic2 : kLogicOpcodes) {
        for (auto shape : kGraphShapes) {
          for (auto invert_pattern : kInvertPatterns) {
            RawMachineAssemblerTester<uint32_t> m(input_type(), input_type(),
                                                  input_type(), input_type());
            std::array logic_ops = {logic0, logic1, logic2};
            Combiner gen(m, shape, invert_pattern, branch_pattern, logic_ops,
                         compare_ops);
            std::array inputs = {
                m.Parameter(0),
                gen.ThirtyTwo(),
                m.Parameter(2),
                m.Parameter(3),
            };
            gen.BuildGraph(inputs);

            for (auto a : input_vector) {
              for (auto b : input_vector) {
                std::array<InputType, 4> inputs{a, 32, b, a};
                uint32_t expected = gen.Expected(inputs);
                uint32_t actual = m.Call(a, b, b, a);
                CHECK_EQ(expected, actual);
              }
            }
          }
        }
      }
    }
  }
}
TEST(CombineCompareWord32Logic3ThirtyTwo) {
  CombineCompareLogic3ThirtyTwo<CombineCompareWord32<3>, uint32_t>(
      kInt32CmpOpcodes, MachineType::Uint32, uint32_test_vector);
}
#if V8_TARGET_ARCH_64_BIT
TEST(CombineCompareWord64Logic3ThirtyTwo) {
  CombineCompareLogic3ThirtyTwo<CombineCompareWord64<3>, uint64_t>(
      kInt64CmpOpcodes, MachineType::Uint64, uint64_test_vector);
}
#endif

constexpr uint32_t kMaxDepth = 4;
// a       b    b       a    a        b   b       a   a       b
// |       |    |       |    |        |   |       |   |       |
// |       |    |       |    |        |   |       |   |       |
// -> cmp <-    -> cmp <-    -> cmp <-    -> cmp <-   -> cmp <-
//     |            |            |            |           |
//     ---> and <---             |            |           |
//           |                   |            |           |
//            ---------> or <----             |           |
//                        |                   |           |
//                         ------> and <------            |
//                                  |                     |
//                                  --------> or <--------
TEST(CombineCompareMaxDepth) {
  constexpr GraphShape shape = kUnbalanced;
  constexpr BranchPattern branch_pattern = kNone;
  std::array logic_ops = {
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr,
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr};
  std::array compare_ops = {TurboshaftComparison::kWord32Equal,
                            TurboshaftComparison::kInt32LessThan,
                            TurboshaftComparison::kInt32LessThanOrEqual,
                            TurboshaftComparison::kUint32LessThan,
                            TurboshaftComparison::kUint32LessThanOrEqual};
  for (auto invert_pattern : kInvertPatterns) {
    RawMachineAssemblerTester<uint32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
        MachineType::Uint32());
    CombineCompareWord32<kMaxDepth> gen(m, shape, invert_pattern,
                                        branch_pattern, logic_ops, compare_ops);
    std::array inputs = {
        m.Parameter(0),
        m.Parameter(1),
        m.Parameter(2),
        m.Parameter(3),
    };
    gen.BuildGraph(inputs);

    FOR_UINT32_INPUTS(a) {
      FOR_UINT32_INPUTS(b) {
        std::array inputs{a, b, b, a};
        uint32_t expected = gen.Expected(inputs);
        uint32_t actual = m.Call(a, b, b, a);
        CHECK_EQ(expected, actual);
      }
    }
  }
}

TEST(CombineCompareBranchesMaxDepth) {
  constexpr GraphShape shape = kUnbalanced;
  std::array logic_ops = {
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr,
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr};
  std::array compare_ops = {TurboshaftComparison::kWord32Equal,
                            TurboshaftComparison::kInt32LessThan,
                            TurboshaftComparison::kInt32LessThanOrEqual,
                            TurboshaftComparison::kUint32LessThan,
                            TurboshaftComparison::kUint32LessThanOrEqual};
  for (auto branch_pattern : kBranchPatterns) {
    for (auto invert_pattern : kInvertPatterns) {
      RawMachineAssemblerTester<uint32_t> m(
          MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
          MachineType::Uint32());
      CombineCompareWord32<kMaxDepth> gen(
          m, shape, invert_pattern, branch_pattern, logic_ops, compare_ops);
      std::array inputs = {
          m.Parameter(0),
          m.Parameter(1),
          m.Parameter(2),
          m.Parameter(3),
      };
      gen.BuildGraph(inputs);

      FOR_UINT32_INPUTS(a) {
        FOR_UINT32_INPUTS(b) {
          std::array inputs{a, b, b, a};
          uint32_t expected = gen.Expected(inputs);
          uint32_t actual = m.Call(a, b, b, a);
          CHECK_EQ(expected, actual);
        }
      }
    }
  }
}

TEST(CombineCompareMaxDepthPlusOne) {
  std::array logic_ops = {
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr,
      TurboshaftBinop::kWord32BitwiseAnd, TurboshaftBinop::kWord32BitwiseOr,
      TurboshaftBinop::kWord32BitwiseAnd};
  std::array compare_ops = {
      TurboshaftComparison::kWord32Equal,
      TurboshaftComparison::kInt32LessThan,
      TurboshaftComparison::kInt32LessThanOrEqual,
      TurboshaftComparison::kUint32LessThan,
      TurboshaftComparison::kUint32LessThanOrEqual,
      TurboshaftComparison::kWord32Equal,
  };
  constexpr BranchPattern branch_pattern = kNone;
  for (auto shape : kGraphShapes) {
    for (auto invert_pattern : kInvertPatterns) {
      RawMachineAssemblerTester<uint32_t> m(
          MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
          MachineType::Uint32());
      CombineCompareWord32<kMaxDepth + 1> gen(
          m, shape, invert_pattern, branch_pattern, logic_ops, compare_ops);
      std::array inputs = {
          m.Parameter(0),
          m.Parameter(1),
          m.Parameter(2),
          m.Parameter(3),
      };
      gen.BuildGraph(inputs);

      FOR_UINT32_INPUTS(a) {
        FOR_UINT32_INPUTS(b) {
          std::array inputs{a, b, b, a};
          uint32_t expected = gen.Expected(inputs);
          uint32_t actual = m.Call(a, b, b, a);
          CHECK_EQ(expected, actual);
        }
      }
    }
  }
}

TEST(CombineCompareTwoLogicInputs) {
  // cmp cmp cmp cmp cmp cmp
  //  |   |   |   |   |   |
  //  logic   logic   logic
  //    |       |       |
  //     - cmp -        |
  //        |           |
  //         -- logic --
  auto run = [](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    bool cmp1 = static_cast<int32_t>(a) < static_cast<int32_t>(b);
    bool cmp2 = static_cast<int32_t>(a) <= 1024;
    bool cmp3 = static_cast<int32_t>(c) < static_cast<int32_t>(d);
    bool cmp4 = static_cast<int32_t>(c) < 4096;
    bool cmp5 = a < d;
    bool cmp6 = b <= c;
    bool logic1 = cmp1 && cmp2;
    bool logic2 = cmp3 || cmp4;
    bool logic3 = cmp5 && cmp6;
    bool cmp7 = logic1 == logic2;
    return static_cast<uint32_t>(cmp7 || logic3);
  };

  RawMachineAssemblerTester<uint32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  V<Word32> cmp1 = m.Int32LessThan(m.Parameter(0), m.Parameter(1));
  V<Word32> cmp2 =
      m.Int32LessThanOrEqual(m.Parameter(0), m.Word32Constant(1024));
  V<Word32> cmp3 = m.Int32LessThan(m.Parameter(2), m.Parameter(3));
  V<Word32> cmp4 =
      m.Int32LessThanOrEqual(m.Parameter(2), m.Word32Constant(4096));
  V<Word32> cmp5 = m.Uint32LessThan(m.Parameter(0), m.Parameter(3));
  V<Word32> cmp6 = m.Uint32LessThanOrEqual(m.Parameter(1), m.Parameter(2));

  V<Word32> logic1 = m.Word32BitwiseAnd(cmp1, cmp2);
  V<Word32> logic2 = m.Word32BitwiseOr(cmp3, cmp4);
  V<Word32> logic3 = m.Word32BitwiseAnd(cmp5, cmp6);

  V<Word32> cmp7 = m.Word32Equal(logic1, logic2);

  m.Return(m.Word32BitwiseOr(cmp7, logic3));

  for (uint32_t a : uint32_test_vector) {
    for (uint32_t b : uint32_test_vector) {
      for (uint32_t c : uint32_test_vector) {
        for (uint32_t d : uint32_test_vector) {
          uint32_t result = m.Call(a, b, c, d);
          uint32_t expected = run(a, b, c, d);
          CHECK_EQ(result, expected);
        }
      }
    }
  }
}

}  // end namespace

}  // namespace v8::internal::compiler::turboshaft
