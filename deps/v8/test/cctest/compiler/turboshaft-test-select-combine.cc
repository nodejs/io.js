// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/turboshaft-codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8::internal::compiler::turboshaft {

// Generates a binop arithmetic instruction, followed by an integer compare zero
// and select. This is to test a possible merge of the arithmetic op and the
// compare for use by the select. We test a matrix of configurations:
// - floating-point and integer select.
// - add, sub, mul, and, or and xor.
// - int32, uint32t, int64_t, uint64_t, float and double.
// - one or multiple users of the binary operation.
// - two different graph layouts (single block vs three blocks).

namespace {

enum GraphConfig { kOneUse, kTwoUsesOneBlock, kTwoUsesTwoBlocks };
constexpr GraphConfig graph_configs[] = {GraphConfig::kOneUse,
                                         GraphConfig::kTwoUsesOneBlock,
                                         GraphConfig::kTwoUsesTwoBlocks};

#define SELECT_OP_LIST(V) \
  V(Word32Select)         \
  V(Word64Select)         \
  V(Float32Select)        \
  V(Float64Select)

enum class SelectOperator {
#define DEF(kind) k##kind,
  SELECT_OP_LIST(DEF)
#undef DEF
};

bool SelectIsSupported(SelectOperator op) {
  // SupportedOperations::Initialize is usually called by the Turboshaft
  // Assembler, but some tests use this function before having created an
  // Assembler, so we manually call it here to make sure that the
  // SupportedOperations list is indeed initialized.
  SupportedOperations::Initialize();

  switch (op) {
    case SelectOperator::kWord32Select:
      return SupportedOperations::word32_select();
    case SelectOperator::kWord64Select:
      return SupportedOperations::word64_select();
    case SelectOperator::kFloat32Select:
      return SupportedOperations::float32_select();
    case SelectOperator::kFloat64Select:
      return SupportedOperations::float64_select();
  }
}

// kOneUse:
// (bin_res = binop lhs, rhs)
// (return (select (compare bin_res, zero, cond), tval, fval))
//
// kTwoUsesOneBlock:
// (bin_res = binop lhs, rhs)
// (return (add (select (compare bin_res, zero, cond), tval, fval), bin_res))
//
// kTwoUsesTwoBlocks:
// Same as above, but the final addition is conditionally executed in a
// different block.
// (bin_res = binop lhs, rhs)
// (select_res = (select (compare bin_res, zero, cond), tval, fval))
// (select_res >= tval)
//   ? (return select_res)
//   : (return (add select_res, bin_res))

template <typename CondType, typename ResultType>
class ConditionalSelectGen {
 public:
  ConditionalSelectGen(BufferedRawMachineAssemblerTester<ResultType>& m,
                       GraphConfig c, TurboshaftComparison icmp_op,
                       TurboshaftBinop bin_op)
      : m_(m),
        config_(c),
        cmpw_(icmp_op),
        binw_(bin_op),
        blocka_(m.NewBlock()),
        blockb_(m.NewBlock()) {}

  void BuildGraph(SelectOperator select_op, OpIndex lhs, OpIndex rhs,
                  OpIndex tval, OpIndex fval) {
    CompareAndSelect(select_op, lhs, rhs, tval, fval);

    switch (config()) {
      case GraphConfig::kOneUse:
        m().Return(select());
        break;
      case GraphConfig::kTwoUsesOneBlock:
        m().Return(AddBinopUse());
        break;
      case GraphConfig::kTwoUsesTwoBlocks:
        m().Return(AddBranchAndUse());
        break;
      default:
        UNREACHABLE();
    }
  }

  void CompareAndSelect(SelectOperator selectop, OpIndex lhs, OpIndex rhs,
                        OpIndex tval, OpIndex fval) {
    OpIndex zero = Is32()
                       ? OpIndex{m().Word32Constant(0)}
                       : OpIndex{m().Word64Constant(static_cast<uint64_t>(0))};
    bin_node_ = binw().MakeNode(m(), lhs, rhs);
    OpIndex cond = cmpw().MakeNode(m(), bin_node(), zero);
    select_ = MakeSelect(selectop, cond, tval, fval);
    select_op_ = selectop;
  }

  OpIndex AddBranchAndUse() {
    OpIndex cond_second_input =
        m().Get(select()).template Cast<SelectOp>().vtrue();
    V<Word32> cond;
    switch (select_op()) {
      case SelectOperator::kFloat32Select:
        cond = m().Float32LessThan(select(), cond_second_input);
        break;
      case SelectOperator::kFloat64Select:
        cond = m().Float64LessThan(select(), cond_second_input);
        break;
      case SelectOperator::kWord32Select:
        cond = m().Int32LessThan(select(), cond_second_input);
        break;
      case SelectOperator::kWord64Select:
        cond = m().Int64LessThan(select(), cond_second_input);
        break;
    }
    m().Branch(cond, blocka(), blockb());
    m().Bind(blocka());
    OpIndex res = AddBinopUse();
    m().Return(res);
    m().Bind(blockb());
    return select();
  }

  ResultType expected(CondType lhs, CondType rhs, ResultType tval,
                      ResultType fval) {
    CondType bin_node_res = binw().eval(lhs, rhs);
    ResultType res =
        Is32() ? cmpw().Int32Compare(static_cast<uint32_t>(bin_node_res), 0)
                     ? tval
                     : fval
        : cmpw().Int64Compare(static_cast<uint64_t>(bin_node_res), 0) ? tval
                                                                      : fval;
    if (config() == GraphConfig::kTwoUsesTwoBlocks && res >= tval) {
      return res;
    }
    if (config() != GraphConfig::kOneUse) {
      res += static_cast<ResultType>(bin_node_res);
    }
    return res;
  }

  BufferedRawMachineAssemblerTester<ResultType>& m() { return m_; }
  GraphConfig config() const { return config_; }
  IntBinopWrapper<CondType>& binw() { return binw_; }
  CompareWrapper& cmpw() { return cmpw_; }
  OpIndex select() const { return select_; }
  SelectOperator select_op() const { return select_op_; }
  OpIndex bin_node() const { return bin_node_; }
  Block* blocka() { return blocka_; }
  Block* blockb() { return blockb_; }

  virtual OpIndex AddBinopUse() = 0;
  virtual bool Is32() const = 0;

 private:
  OpIndex MakeSelect(SelectOperator op, OpIndex cond, OpIndex vtrue,
                     OpIndex vfalse) {
    switch (op) {
#define CASE(kind)              \
  case SelectOperator::k##kind: \
    return m().kind(cond, vtrue, vfalse);
      SELECT_OP_LIST(CASE)
#undef CASE
    }
  }

  BufferedRawMachineAssemblerTester<ResultType>& m_;
  GraphConfig config_;
  CompareWrapper cmpw_;
  IntBinopWrapper<CondType> binw_;
  OpIndex bin_node_;
  OpIndex select_;
  SelectOperator select_op_;
  Block *blocka_, *blockb_;
};

template <typename ResultType>
class UInt32ConditionalSelectGen
    : public ConditionalSelectGen<uint32_t, ResultType> {
 public:
  using ConditionalSelectGen<uint32_t, ResultType>::ConditionalSelectGen;

  OpIndex AddBinopUse() override {
    BufferedRawMachineAssemblerTester<ResultType>& m = this->m();
    switch (this->select_op()) {
      case SelectOperator::kFloat32Select:
        return m.Float32Add(this->select(),
                            m.ChangeUint32ToFloat32(this->bin_node()));
      case SelectOperator::kFloat64Select:
        return m.Float64Add(this->select(),
                            m.ChangeUint32ToFloat64(this->bin_node()));
      case SelectOperator::kWord32Select:
        return m.Word32Add(this->select(), this->bin_node());
      case SelectOperator::kWord64Select:
        return m.Word64Add(this->select(),
                           m.ChangeUint32ToUint64(this->bin_node()));
    }
  }

  bool Is32() const override { return true; }
};

template <typename ResultType>
class UInt64ConditionalSelectGen
    : public ConditionalSelectGen<uint64_t, ResultType> {
 public:
  using ConditionalSelectGen<uint64_t, ResultType>::ConditionalSelectGen;

  OpIndex AddBinopUse() override {
    BufferedRawMachineAssemblerTester<ResultType>& m = this->m();
    switch (this->select_op()) {
      case SelectOperator::kFloat32Select:
        return m.Float32Add(this->select(),
                            m.ChangeUint64ToFloat32(this->bin_node()));
      case SelectOperator::kFloat64Select:
        return m.Float64Add(this->select(),
                            m.ChangeUint64ToFloat64(this->bin_node()));
      case SelectOperator::kWord32Select:
        return m.Word32Add(this->select(),
                           m.TruncateWord64ToWord32(this->bin_node()));
      case SelectOperator::kWord64Select:
        return m.Word64Add(this->select(), this->bin_node());
    }
  }

  bool Is32() const override { return false; }
};

constexpr TurboshaftComparison int32_cmp_opcodes[] = {
    TurboshaftComparison::kWord32Equal, TurboshaftComparison::kInt32LessThan,
    TurboshaftComparison::kInt32LessThanOrEqual,
    TurboshaftComparison::kUint32LessThan,
    TurboshaftComparison::kUint32LessThanOrEqual};
constexpr TurboshaftBinop int32_bin_opcodes[] = {
    TurboshaftBinop::kWord32Add,       TurboshaftBinop::kWord32Sub,
    TurboshaftBinop::kWord32Mul,       TurboshaftBinop::kWord32BitwiseAnd,
    TurboshaftBinop::kWord32BitwiseOr, TurboshaftBinop::kWord32BitwiseXor,
};

TEST(Word32SelectCombineInt32CompareZero) {
  if (!SelectIsSupported(SelectOperator::kWord32Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint32_t> m(
            MachineType::Uint32(), MachineType::Uint32(), MachineType::Int32(),
            MachineType::Int32());
        UInt32ConditionalSelectGen<uint32_t> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kWord32Select, lhs, rhs, tval, fval);

        FOR_UINT32_INPUTS(a) {
          FOR_UINT32_INPUTS(b) {
            uint32_t expected = gen.expected(a, b, 2, 1);
            uint32_t actual = m.Call(a, b, 2, 1);
            CHECK_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Word64SelectCombineInt32CompareZero) {
  if (!SelectIsSupported(SelectOperator::kWord64Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint64_t> m(
            MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint64(),
            MachineType::Uint64());
        UInt32ConditionalSelectGen<uint64_t> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kWord64Select, lhs, rhs, tval, fval);

        FOR_UINT32_INPUTS(a) {
          FOR_UINT32_INPUTS(b) {
            uint64_t c = 2;
            uint64_t d = 1;
            uint64_t expected = gen.expected(a, b, c, d);
            uint64_t actual = m.Call(a, b, c, d);
            CHECK_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Float32SelectCombineInt32CompareZero) {
  if (!SelectIsSupported(SelectOperator::kFloat32Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<float> m(
            MachineType::Uint32(), MachineType::Uint32(),
            MachineType::Float32(), MachineType::Float32());
        UInt32ConditionalSelectGen<float> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kFloat32Select, lhs, rhs, tval, fval);

        FOR_UINT32_INPUTS(a) {
          FOR_UINT32_INPUTS(b) {
            float expected = gen.expected(a, b, 2.0f, 1.0f);
            float actual = m.Call(a, b, 2.0f, 1.0f);
            CHECK_FLOAT_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Float64SelectCombineInt32CompareZero) {
  if (!SelectIsSupported(SelectOperator::kFloat64Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<double> m(
            MachineType::Uint32(), MachineType::Uint32(),
            MachineType::Float64(), MachineType::Float64());
        UInt32ConditionalSelectGen<double> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kFloat64Select, lhs, rhs, tval, fval);

        FOR_UINT32_INPUTS(a) {
          FOR_UINT32_INPUTS(b) {
            double expected = gen.expected(a, b, 2.0, 1.0);
            double actual = m.Call(a, b, 2.0, 1.0);
            CHECK_DOUBLE_EQ(expected, actual);
          }
        }
      }
    }
  }
}

constexpr TurboshaftBinop int64_bin_opcodes[] = {
    TurboshaftBinop::kWord64Add,       TurboshaftBinop::kWord64Sub,
    TurboshaftBinop::kWord64Mul,       TurboshaftBinop::kWord64BitwiseAnd,
    TurboshaftBinop::kWord64BitwiseOr, TurboshaftBinop::kWord64BitwiseXor,
};
constexpr TurboshaftComparison int64_cmp_opcodes[] = {
    TurboshaftComparison::kWord64Equal, TurboshaftComparison::kInt64LessThan,
    TurboshaftComparison::kInt64LessThanOrEqual,
    TurboshaftComparison::kUint64LessThan,
    TurboshaftComparison::kUint64LessThanOrEqual};

TEST(Word32SelectCombineInt64CompareZero) {
  RawMachineAssemblerTester<int32_t> features(MachineType::Int32());
  if (!SelectIsSupported(SelectOperator::kWord32Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint32_t> m(
            MachineType::Uint64(), MachineType::Uint64(), MachineType::Int32(),
            MachineType::Int32());
        UInt64ConditionalSelectGen<uint32_t> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kWord32Select, lhs, rhs, tval, fval);

        FOR_UINT64_INPUTS(a) {
          FOR_UINT64_INPUTS(b) {
            uint32_t expected = gen.expected(a, b, 2, 1);
            uint32_t actual = m.Call(a, b, 2, 1);
            CHECK_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Word64SelectCombineInt64CompareZero) {
  RawMachineAssemblerTester<uint32_t> features(MachineType::Uint32());
  if (!SelectIsSupported(SelectOperator::kWord64Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint64_t> m(
            MachineType::Uint64(), MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Uint64());
        UInt64ConditionalSelectGen<uint64_t> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kWord64Select, lhs, rhs, tval, fval);

        FOR_UINT64_INPUTS(a) {
          FOR_UINT64_INPUTS(b) {
            uint64_t c = 2;
            uint64_t d = 1;
            uint64_t expected = gen.expected(a, b, c, d);
            uint64_t actual = m.Call(a, b, c, d);
            CHECK_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Float32SelectCombineInt64CompareZero) {
  RawMachineAssemblerTester<uint32_t> features(MachineType::Uint32());
  if (!SelectIsSupported(SelectOperator::kFloat32Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<float> m(
            MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Float32(), MachineType::Float32());
        UInt64ConditionalSelectGen<float> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kFloat32Select, lhs, rhs, tval, fval);

        FOR_UINT64_INPUTS(a) {
          FOR_UINT64_INPUTS(b) {
            float expected = gen.expected(a, b, 2.0f, 1.0f);
            float actual = m.Call(a, b, 2.0f, 1.0f);
            CHECK_FLOAT_EQ(expected, actual);
          }
        }
      }
    }
  }
}

TEST(Float64SelectCombineInt64CompareZero) {
  RawMachineAssemblerTester<uint32_t> features(MachineType::Uint32());
  if (!SelectIsSupported(SelectOperator::kFloat64Select)) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<double> m(
            MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Float64(), MachineType::Float64());
        UInt64ConditionalSelectGen<double> gen(m, config, cmp, bin);
        OpIndex lhs = m.Parameter(0);
        OpIndex rhs = m.Parameter(1);
        OpIndex tval = m.Parameter(2);
        OpIndex fval = m.Parameter(3);
        gen.BuildGraph(SelectOperator::kFloat64Select, lhs, rhs, tval, fval);

        FOR_UINT64_INPUTS(a) {
          FOR_UINT64_INPUTS(b) {
            double expected = gen.expected(a, b, 2.0, 1.0);
            double actual = m.Call(a, b, 2.0, 1.0);
            CHECK_DOUBLE_EQ(expected, actual);
          }
        }
      }
    }
  }
}

}  // end namespace

}  // namespace v8::internal::compiler::turboshaft
