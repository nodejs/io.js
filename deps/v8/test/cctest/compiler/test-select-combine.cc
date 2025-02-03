// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8::internal::compiler {

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
                       GraphConfig c, IrOpcode::Value icmp_op,
                       IrOpcode::Value bin_op)
      : m_(m), config_(c), cmpw_(icmp_op), binw_(bin_op) {}

  void BuildGraph(const Operator* select_op, Node* lhs, Node* rhs, Node* tval,
                  Node* fval) {
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

  void CompareAndSelect(const Operator* selectop, Node* lhs, Node* rhs,
                        Node* tval, Node* fval) {
    Node* zero = Is32() ? m().Int32Constant(0) : m().Int64Constant(0);
    bin_node_ = m().AddNode(binw().op(m().machine()), lhs, rhs);
    Node* cond = m().AddNode(cmpw().op(m().machine()), bin_node(), zero);
    select_ = m().AddNode(selectop, cond, tval, fval);
  }

  Node* AddBranchAndUse() {
    const Operator* cond_op = nullptr;
    switch (select()->opcode()) {
      case IrOpcode::kFloat32Select:
        cond_op = m().machine()->Float32LessThan();
        break;
      case IrOpcode::kFloat64Select:
        cond_op = m().machine()->Float64LessThan();
        break;
      case IrOpcode::kWord32Select:
        cond_op = m().machine()->Int32LessThan();
        break;
      case IrOpcode::kWord64Select:
        cond_op = m().machine()->Int64LessThan();
        break;
      default:
        UNREACHABLE();
    }
    DCHECK_NOT_NULL(cond_op);
    Node* cond = m().AddNode(cond_op, select(), select()->InputAt(1));
    m().Branch(cond, &blocka(), &blockb());
    m().Bind(&blocka());
    Node* res = AddBinopUse();
    m().Return(res);
    m().Bind(&blockb());
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
  const IntBinopWrapper<CondType>& binw() const { return binw_; }
  const CompareWrapper& cmpw() const { return cmpw_; }
  Node* select() const { return select_; }
  Node* bin_node() const { return bin_node_; }
  RawMachineLabel& blocka() { return blocka_; }
  RawMachineLabel& blockb() { return blockb_; }

  virtual Node* AddBinopUse() = 0;
  virtual bool Is32() const = 0;

 private:
  BufferedRawMachineAssemblerTester<ResultType>& m_;
  GraphConfig config_;
  CompareWrapper cmpw_;
  IntBinopWrapper<CondType> binw_;
  Node* bin_node_;
  Node* select_;
  RawMachineLabel blocka_, blockb_;
};

template <typename ResultType>
class UInt32ConditionalSelectGen
    : public ConditionalSelectGen<uint32_t, ResultType> {
 public:
  using ConditionalSelectGen<uint32_t, ResultType>::ConditionalSelectGen;

  Node* AddBinopUse() override {
    BufferedRawMachineAssemblerTester<ResultType>& m = this->m();
    Node* bin_node = this->bin_node();
    Node* bin_node_use = nullptr;
    Node* select = this->select();
    const Operator* add_op = nullptr;
    switch (select->opcode()) {
      case IrOpcode::kFloat32Select:
        bin_node_use = m.RoundUint32ToFloat32(bin_node);
        add_op = m.machine()->Float32Add();
        break;
      case IrOpcode::kFloat64Select:
        bin_node_use = m.ChangeUint32ToFloat64(bin_node);
        add_op = m.machine()->Float64Add();
        break;
      case IrOpcode::kWord32Select:
        bin_node_use = bin_node;
        add_op = m.machine()->Int32Add();
        break;
      case IrOpcode::kWord64Select:
        bin_node_use = m.ChangeUint32ToUint64(bin_node);
        add_op = m.machine()->Int64Add();
        break;
      default:
        UNREACHABLE();
    }
    DCHECK_NOT_NULL(bin_node_use);
    DCHECK_NOT_NULL(add_op);
    return m.AddNode(add_op, select, bin_node_use);
  }

  bool Is32() const override { return true; }
};

template <typename ResultType>
class UInt64ConditionalSelectGen
    : public ConditionalSelectGen<uint64_t, ResultType> {
 public:
  using ConditionalSelectGen<uint64_t, ResultType>::ConditionalSelectGen;

  Node* AddBinopUse() override {
    BufferedRawMachineAssemblerTester<ResultType>& m = this->m();
    Node* bin_node = this->bin_node();
    Node* bin_node_use = nullptr;
    Node* select = this->select();
    const Operator* add_op;
    switch (select->opcode()) {
      case IrOpcode::kFloat32Select:
        bin_node_use = m.RoundUint64ToFloat32(bin_node);
        add_op = m.machine()->Float32Add();
        break;
      case IrOpcode::kFloat64Select:
        bin_node_use = m.RoundUint64ToFloat64(bin_node);
        add_op = m.machine()->Float64Add();
        break;
      case IrOpcode::kWord32Select:
        bin_node_use = m.TruncateInt64ToInt32(bin_node);
        add_op = m.machine()->Int32Add();
        break;
      case IrOpcode::kWord64Select:
        bin_node_use = bin_node;
        add_op = m.machine()->Int64Add();
        break;
      default:
        UNREACHABLE();
    }
    DCHECK(bin_node_use);
    DCHECK(add_op);
    return m.AddNode(add_op, select, bin_node_use);
  }

  bool Is32() const override { return false; }
};

constexpr IrOpcode::Value int32_cmp_opcodes[] = {
    IrOpcode::kWord32Equal, IrOpcode::kInt32LessThan,
    IrOpcode::kInt32LessThanOrEqual, IrOpcode::kUint32LessThan,
    IrOpcode::kUint32LessThanOrEqual};
constexpr IrOpcode::Value int32_bin_opcodes[] = {
    IrOpcode::kInt32Add,  IrOpcode::kInt32Sub, IrOpcode::kInt32Mul,
    IrOpcode::kWord32And, IrOpcode::kWord32Or, IrOpcode::kWord32Xor,
};

TEST(Word32SelectCombineInt32CompareZero) {
  RawMachineAssemblerTester<int32_t> features(MachineType::Uint32());
  if (!features.machine()->Word32Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint32_t> m(
            MachineType::Uint32(), MachineType::Uint32(), MachineType::Int32(),
            MachineType::Int32());
        UInt32ConditionalSelectGen<uint32_t> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Word32Select().op(), lhs, rhs, tval, fval);

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
  RawMachineAssemblerTester<int32_t> features(MachineType::Int32());
  if (!features.machine()->Word64Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint64_t> m(
            MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint64(),
            MachineType::Uint64());
        UInt32ConditionalSelectGen<uint64_t> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Word64Select().op(), lhs, rhs, tval, fval);

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
  RawMachineAssemblerTester<uint32_t> features(MachineType::Uint32());
  if (!features.machine()->Float32Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<float> m(
            MachineType::Uint32(), MachineType::Uint32(),
            MachineType::Float32(), MachineType::Float32());
        UInt32ConditionalSelectGen<float> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Float32Select().op(), lhs, rhs, tval, fval);

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
  RawMachineAssemblerTester<uint32_t> features(MachineType::Uint32());
  if (!features.machine()->Float64Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int32_cmp_opcodes) {
      for (auto bin : int32_bin_opcodes) {
        BufferedRawMachineAssemblerTester<double> m(
            MachineType::Uint32(), MachineType::Uint32(),
            MachineType::Float64(), MachineType::Float64());
        UInt32ConditionalSelectGen<double> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Float64Select().op(), lhs, rhs, tval, fval);

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

constexpr IrOpcode::Value int64_bin_opcodes[] = {
    IrOpcode::kInt64Add,  IrOpcode::kInt64Sub, IrOpcode::kInt64Mul,
    IrOpcode::kWord64And, IrOpcode::kWord64Or, IrOpcode::kWord64Xor,
};
constexpr IrOpcode::Value int64_cmp_opcodes[] = {
    IrOpcode::kWord64Equal, IrOpcode::kInt64LessThan,
    IrOpcode::kInt64LessThanOrEqual, IrOpcode::kUint64LessThan,
    IrOpcode::kUint64LessThanOrEqual};

TEST(Word32SelectCombineInt64CompareZero) {
  RawMachineAssemblerTester<int32_t> features(MachineType::Int32());
  if (!features.machine()->Word32Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint32_t> m(
            MachineType::Uint64(), MachineType::Uint64(), MachineType::Int32(),
            MachineType::Int32());
        UInt64ConditionalSelectGen<uint32_t> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Word32Select().op(), lhs, rhs, tval, fval);

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
  if (!features.machine()->Word64Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<uint64_t> m(
            MachineType::Uint64(), MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Uint64());
        UInt64ConditionalSelectGen<uint64_t> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Word64Select().op(), lhs, rhs, tval, fval);

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
  if (!features.machine()->Float32Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<float> m(
            MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Float32(), MachineType::Float32());
        UInt64ConditionalSelectGen<float> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Float32Select().op(), lhs, rhs, tval, fval);

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
  if (!features.machine()->Float64Select().IsSupported()) {
    return;
  }

  for (auto config : graph_configs) {
    for (auto cmp : int64_cmp_opcodes) {
      for (auto bin : int64_bin_opcodes) {
        BufferedRawMachineAssemblerTester<double> m(
            MachineType::Uint64(), MachineType::Uint64(),
            MachineType::Float64(), MachineType::Float64());
        UInt64ConditionalSelectGen<double> gen(m, config, cmp, bin);
        Node* lhs = m.Parameter(0);
        Node* rhs = m.Parameter(1);
        Node* tval = m.Parameter(2);
        Node* fval = m.Parameter(3);
        gen.BuildGraph(m.machine()->Float64Select().op(), lhs, rhs, tval, fval);

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

}  // namespace v8::internal::compiler
