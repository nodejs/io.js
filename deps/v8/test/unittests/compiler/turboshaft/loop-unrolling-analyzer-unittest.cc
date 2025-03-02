// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/loop-unrolling-reducer.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class LoopUnrollingAnalyzerTest : public ReducerTest {};

template <typename T>
class LoopUnrollingAnalyzerTestWithParam
    : public LoopUnrollingAnalyzerTest,
      public ::testing::WithParamInterface<T> {};

size_t CountLoops(const Graph& graph) {
  size_t count = 0;
  for (const Block& block : graph.blocks()) {
    if (block.IsLoop()) count++;
  }
  return count;
}

const Block& GetFirstLoop(const Graph& graph) {
  DCHECK_GE(CountLoops(graph), 1u);
  for (const Block& block : graph.blocks()) {
    if (block.IsLoop()) return block;
  }
  UNREACHABLE();
}

#define BUILTIN_CMP_LIST(V) \
  V(Uint32LessThan)         \
  V(Uint32LessThanOrEqual)  \
  V(Int32LessThan)          \
  V(Int32LessThanOrEqual)   \
  V(Word32Equal)

#define CMP_GREATER_THAN_LIST(V) \
  V(Uint32GreaterThan)           \
  V(Uint32GreaterThanOrEqual)    \
  V(Int32GreaterThan)            \
  V(Int32GreaterThanOrEqual)

#define CMP_LIST(V)   \
  BUILTIN_CMP_LIST(V) \
  CMP_GREATER_THAN_LIST(V)

enum class Cmp {
#define DEF_CMP_OP(name) k##name,
  CMP_LIST(DEF_CMP_OP)
#undef DEF_CMP_OP
};
std::ostream& operator<<(std::ostream& os, const Cmp& cmp) {
  switch (cmp) {
    case Cmp::kUint32LessThan:
      return os << "<ᵘ";
    case Cmp::kUint32LessThanOrEqual:
      return os << "<=ᵘ";
    case Cmp::kInt32LessThan:
      return os << "<ˢ";
    case Cmp::kInt32LessThanOrEqual:
      return os << "<=ˢ";
    case Cmp::kUint32GreaterThan:
      return os << ">ᵘ";
    case Cmp::kUint32GreaterThanOrEqual:
      return os << ">=ᵘ";
    case Cmp::kInt32GreaterThan:
      return os << ">ˢ";
    case Cmp::kInt32GreaterThanOrEqual:
      return os << ">=ᵘ";
    case Cmp::kWord32Equal:
      return os << "!=";
  }
}

bool IsGreaterThan(Cmp cmp) {
  switch (cmp) {
#define GREATER_THAN_CASE(name) \
  case Cmp::k##name:            \
    return true;
    CMP_GREATER_THAN_LIST(GREATER_THAN_CASE)
    default:
      return false;
  }
}

Cmp GreaterThanToLessThan(Cmp cmp, ConstOrV<Word32>* left,
                          ConstOrV<Word32>* right) {
  if (IsGreaterThan(cmp)) std::swap(*left, *right);
  switch (cmp) {
    case Cmp::kUint32GreaterThan:
      return Cmp::kUint32LessThan;
    case Cmp::kUint32GreaterThanOrEqual:
      return Cmp::kUint32LessThanOrEqual;
    case Cmp::kInt32GreaterThan:
      return Cmp::kInt32LessThan;
    case Cmp::kInt32GreaterThanOrEqual:
      return Cmp::kInt32LessThanOrEqual;

    default:
      return cmp;
  }
}

#define NO_OVERFLOW_BINOP_LIST(V) \
  V(Word32Add)                    \
  V(Word32Sub)                    \
  V(Word32Mul)                    \
  V(Int32Div)                     \
  V(Uint32Div)

#define OVERFLOW_CHECKED_BINOP_LIST(V) \
  V(Int32AddCheckOverflow)             \
  V(Int32SubCheckOverflow)             \
  V(Int32MulCheckOverflow)

#define BINOP_LIST(V)       \
  NO_OVERFLOW_BINOP_LIST(V) \
  OVERFLOW_CHECKED_BINOP_LIST(V)

enum class Binop {
#define DEF_BINOP_OP(name) k##name,
  BINOP_LIST(DEF_BINOP_OP)
#undef DEF_BINOP_OP
};
std::ostream& operator<<(std::ostream& os, const Binop& binop) {
  switch (binop) {
    case Binop::kWord32Add:
      return os << "+";
    case Binop::kWord32Sub:
      return os << "-";
    case Binop::kWord32Mul:
      return os << "*";
    case Binop::kInt32Div:
      return os << "/ˢ";
    case Binop::kUint32Div:
      return os << "/ᵘ";
    case Binop::kInt32AddCheckOverflow:
      return os << "+ᵒ";
    case Binop::kInt32SubCheckOverflow:
      return os << "-ᵒ";
    case Binop::kInt32MulCheckOverflow:
      return os << "*ᵒ";
  }
}

V<Word32> EmitCmp(TestInstance& test_instance, Cmp cmp, ConstOrV<Word32> left,
                  ConstOrV<Word32> right) {
  cmp = GreaterThanToLessThan(cmp, &left, &right);
  switch (cmp) {
#define CASE(name)   \
  case Cmp::k##name: \
    return test_instance.Asm().name(left, right);
    BUILTIN_CMP_LIST(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}

V<Word32> EmitBinop(TestInstance& test_instance, Binop binop,
                    ConstOrV<Word32> left, ConstOrV<Word32> right) {
  switch (binop) {
#define CASE_NO_OVERFLOW(name) \
  case Binop::k##name:         \
    return test_instance.Asm().name(left, right);
    NO_OVERFLOW_BINOP_LIST(CASE_NO_OVERFLOW)
#undef CASE_NO_OVERFLOW

#define CASE_OVERFLOW(name)                   \
  case Binop::k##name:                        \
    return test_instance.Asm().Projection<0>( \
        test_instance.Asm().name(left, right));
    OVERFLOW_CHECKED_BINOP_LIST(CASE_OVERFLOW)
#undef CASE_OVERFLOW
  }
}

struct BoundedLoop {
  int init;
  Cmp cmp;
  int max;
  Binop binop;
  int increment;
  uint32_t expected_iter_count;
  const char* name;
  uint32_t expected_unroll_count = 0;
};
std::ostream& operator<<(std::ostream& os, const BoundedLoop& loop) {
  return os << loop.name;
}

static const BoundedLoop kSmallBoundedLoops[] = {
    // Increasing positive counter with add increment.
    {0, Cmp::kInt32LessThan, 3, Binop::kWord32Add, 1, 3,
     "for (int32_t i = 0;  i < 3;  i += 1)"},
    {0, Cmp::kInt32LessThanOrEqual, 3, Binop::kWord32Add, 1, 4,
     "for (int32_t i = 0;  i <= 3; i += 1)"},
    {0, Cmp::kUint32LessThan, 3, Binop::kWord32Add, 1, 3,
     "for (uint32_t i = 0; i < 3;  i += 1)"},
    {0, Cmp::kUint32LessThanOrEqual, 3, Binop::kWord32Add, 1, 4,
     "for (uint32_t i = 0; i <= 3; i += 1)"},

    // Decreasing counter with add/sub increment.
    {1, Cmp::kInt32GreaterThan, -2, Binop::kWord32Sub, 1, 3,
     "for (int32_t i = 1; i > -2; i -= 1)"},
    {1, Cmp::kInt32GreaterThan, -2, Binop::kWord32Add, -1, 3,
     "for (int32_t i = 1; i > -2; i += -1)"},
    {1, Cmp::kInt32GreaterThanOrEqual, -2, Binop::kWord32Sub, 1, 4,
     "for (int32_t i = 1; i >= -2; i -= 1)"},
    {1, Cmp::kInt32GreaterThanOrEqual, -2, Binop::kWord32Add, -1, 4,
     "for (int32_t i = 1; i >= -2; i += -1)"},

    // Increasing negative counter with add increment.
    {-5, Cmp::kInt32LessThan, -2, Binop::kWord32Add, 1, 3,
     "for (int32_t i = -5; i < -2; i += 1)"},
    {-5, Cmp::kInt32LessThanOrEqual, -2, Binop::kWord32Add, 1, 4,
     "for (int32_t i = -5; i <= -2; i += 1)"},

    // Increasing positive counter with mul increment.
    {3, Cmp::kInt32LessThan, 13, Binop::kWord32Mul, 2, 3,
     "for (int32_t i = 3; i < 13;  i *= 2)"},
    {3, Cmp::kInt32LessThanOrEqual, 13, Binop::kWord32Mul, 2, 3,
     "for (int32_t i = 3; i <= 13; i *= 2)"},
};

static const BoundedLoop kLargeBoundedLoops[] = {
    // Increasing positive counter with add increment.
    {0, Cmp::kInt32LessThan, 4500, Binop::kWord32Add, 1, 4500,
     "for (int32_t i = 0; i < 4500; i += 1)"},
    {0, Cmp::kInt32LessThan, 1000000, Binop::kWord32Add, 1, 1000000,
     "for (int32_t i = 0; i < 1000000; i += 1)"},
    {0, Cmp::kUint32LessThan, 4500, Binop::kWord32Add, 1, 4500,
     "for (uint32_t i = 0; i < 4500; i += 1)"},
    {0, Cmp::kUint32LessThan, 1000000, Binop::kWord32Add, 1, 1000000,
     "for (uint32_t i = 0; i < 1000000; i += 1)"},

    // Decreasing counter with add increment.
    {700, Cmp::kInt32GreaterThan, -1000, Binop::kWord32Add, -2, 850,
     "for (int32_t i = 700; i > -1000; i += -1)"},
    {700, Cmp::kInt32GreaterThanOrEqual, -1000, Binop::kWord32Add, -2, 851,
     "for (int32_t i = 700; i >= -1000; i += -1)"},
};

static const BoundedLoop kUnderOverflowBoundedLoops[] = {
    // Increasing positive to negative with add increment and signed overflow.
    // Small loop.
    {std::numeric_limits<int32_t>::max() - 2, Cmp::kInt32GreaterThan,
     std::numeric_limits<int32_t>::min() + 10, Binop::kWord32Add, 1, 3,
     "for (int32_i = MAX_INT-2; i > MIN_INT+10; i += 1)"},
    {std::numeric_limits<int32_t>::max() - 2, Cmp::kInt32GreaterThanOrEqual,
     std::numeric_limits<int32_t>::min() + 10, Binop::kWord32Add, 1, 3,
     "for (int32_i = MAX_INT-2; i >= MIN_INT+10; i += 1)"},
    // Larger loop.
    {std::numeric_limits<int32_t>::max() - 100, Cmp::kInt32GreaterThan,
     std::numeric_limits<int32_t>::min() + 100, Binop::kWord32Add, 1, 200,
     "for (int32_i = MAX_INT-100; i > MIN_INT+100; i += 1)"},
    {std::numeric_limits<int32_t>::max() - 100, Cmp::kInt32GreaterThanOrEqual,
     std::numeric_limits<int32_t>::min() + 100, Binop::kWord32Add, 1, 201,
     "for (int32_i = MAX_INT-100; i >= MIN_INT+100; i += 1)"},

    // Decreasing negative to positive with add/sub increment and signed
    // underflow.
    // Small loop.
    {std::numeric_limits<int32_t>::min() + 2, Cmp::kInt32LessThan,
     std::numeric_limits<int32_t>::max() - 10, Binop::kWord32Add, -1, 3,
     "for (int32_t i = MIN_INT+2; i < MAX_INT-10; i += -1)"},
    {std::numeric_limits<int32_t>::min() + 2, Cmp::kInt32LessThan,
     std::numeric_limits<int32_t>::max() - 10, Binop::kWord32Sub, 1, 3,
     "for (int32_t i = MIN_INT+2; i < MAX_INT-10; i -= 1)"},
    {std::numeric_limits<int32_t>::min() + 2, Cmp::kInt32LessThanOrEqual,
     std::numeric_limits<int32_t>::max() - 10, Binop::kWord32Add, -1, 3,
     "for (int32_t i = MIN_INT+2; i <= MAX_INT-10; i += -1)"},
    {std::numeric_limits<int32_t>::min() + 2, Cmp::kInt32LessThanOrEqual,
     std::numeric_limits<int32_t>::max() - 10, Binop::kWord32Sub, 1, 3,
     "for (int32_t i = MIN_INT+2; i <= MAX_INT-10; i -= 1)"},
    // Large loop.
    {std::numeric_limits<int32_t>::min() + 100, Cmp::kInt32LessThan,
     std::numeric_limits<int32_t>::max() - 100, Binop::kWord32Add, -1, 200,
     "for (int32_t i = MIN_INT+100; i < MAX_INT-100; i -= 1)"},
    {std::numeric_limits<int32_t>::min() + 100, Cmp::kInt32LessThanOrEqual,
     std::numeric_limits<int32_t>::max() - 100, Binop::kWord32Add, -1, 201,
     "for (int32_t i = MIN_INT+100; i <= MAX_INT-100; i -= 1)"},
};

using LoopUnrollingAnalyzerSmallLoopTest =
    LoopUnrollingAnalyzerTestWithParam<BoundedLoop>;

// Checking that the LoopUnrollingAnalyzer correctly computes the number of
// iterations of small loops.
TEST_P(LoopUnrollingAnalyzerSmallLoopTest, ExactLoopIterCount) {
  BoundedLoop params = GetParam();
  auto test = CreateFromGraph(1, [&params](auto& Asm) {
    using AssemblerT = std::remove_reference<decltype(Asm)>::type::Assembler;
    OpIndex cond = Asm.GetParameter(0);

    ScopedVar<Word32, AssemblerT> index(&Asm, params.init);

    WHILE(EmitCmp(Asm, params.cmp, index, params.max)) {
      __ JSLoopStackCheck(__ NoContextConstant(), Asm.BuildFrameState());

      // Advance the {index}.
      index = EmitBinop(Asm, params.binop, index, params.increment);
    }

    __ Return(index);
  });

  LoopUnrollingAnalyzer analyzer(test.zone(), &test.graph(), false);
  auto stack_checks_to_remove = test.graph().stack_checks_to_remove();

  const Block& loop = GetFirstLoop(test.graph());
  ASSERT_EQ(1u, stack_checks_to_remove.size());
  EXPECT_TRUE(stack_checks_to_remove.contains(loop.index().id()));

  IterationCount iter_count = analyzer.GetIterationCount(&loop);
  ASSERT_TRUE(iter_count.IsExact());
  EXPECT_EQ(params.expected_iter_count, iter_count.exact_count());
}

INSTANTIATE_TEST_SUITE_P(LoopUnrollingAnalyzerTest,
                         LoopUnrollingAnalyzerSmallLoopTest,
                         ::testing::ValuesIn(kSmallBoundedLoops));

using LoopUnrollingAnalyzerLargeLoopTest =
    LoopUnrollingAnalyzerTestWithParam<BoundedLoop>;

// Checking that the LoopUnrollingAnalyzer correctly computes the number of
// iterations of small loops.
TEST_P(LoopUnrollingAnalyzerLargeLoopTest, LargeLoopIterCount) {
  BoundedLoop params = GetParam();
  auto test = CreateFromGraph(1, [&params](auto& Asm) {
    using AssemblerT = std::remove_reference<decltype(Asm)>::type::Assembler;
    OpIndex cond = Asm.GetParameter(0);

    ScopedVar<Word32, AssemblerT> index(&Asm, params.init);

    WHILE(EmitCmp(Asm, params.cmp, index, params.max)) {
      __ JSLoopStackCheck(__ NoContextConstant(), Asm.BuildFrameState());

      // Advance the {index}.
      index = EmitBinop(Asm, params.binop, index, params.increment);
    }

    __ Return(index);
  });

  LoopUnrollingAnalyzer analyzer(test.zone(), &test.graph(), false);
  auto stack_checks_to_remove = test.graph().stack_checks_to_remove();

  const Block& loop = GetFirstLoop(test.graph());

  if (params.expected_iter_count <=
      LoopUnrollingAnalyzer::kMaxIterForStackCheckRemoval) {
    EXPECT_EQ(1u, stack_checks_to_remove.size());
    EXPECT_TRUE(stack_checks_to_remove.contains(loop.index().id()));

    IterationCount iter_count = analyzer.GetIterationCount(&loop);
    ASSERT_TRUE(iter_count.IsApprox());
    EXPECT_TRUE(iter_count.IsSmallerThan(
        LoopUnrollingAnalyzer::kMaxIterForStackCheckRemoval));
  } else {
    EXPECT_EQ(0u, stack_checks_to_remove.size());
    EXPECT_FALSE(stack_checks_to_remove.contains(loop.index().id()));

    IterationCount iter_count = analyzer.GetIterationCount(&loop);
    ASSERT_TRUE(iter_count.IsApprox());
    EXPECT_FALSE(iter_count.IsSmallerThan(
        LoopUnrollingAnalyzer::kMaxIterForStackCheckRemoval));
  }
}

INSTANTIATE_TEST_SUITE_P(LoopUnrollingAnalyzerTest,
                         LoopUnrollingAnalyzerLargeLoopTest,
                         ::testing::ValuesIn(kLargeBoundedLoops));

using LoopUnrollingAnalyzerOverflowTest =
    LoopUnrollingAnalyzerTestWithParam<BoundedLoop>;

// Checking that the LoopUnrollingAnalyzer correctly computes the number of
// iterations of small loops.
TEST_P(LoopUnrollingAnalyzerOverflowTest, LargeLoopIterCount) {
  BoundedLoop params = GetParam();
  auto test = CreateFromGraph(1, [&params](auto& Asm) {
    using AssemblerT = std::remove_reference<decltype(Asm)>::type::Assembler;
    OpIndex cond = Asm.GetParameter(0);

    ScopedVar<Word32, AssemblerT> index(&Asm, params.init);

    WHILE(EmitCmp(Asm, params.cmp, index, params.max)) {
      __ JSLoopStackCheck(__ NoContextConstant(), Asm.BuildFrameState());

      // Advance the {index}.
      index = EmitBinop(Asm, params.binop, index, params.increment);
    }

    __ Return(index);
  });

  LoopUnrollingAnalyzer analyzer(test.zone(), &test.graph(), false);
  auto stack_checks_to_remove = test.graph().stack_checks_to_remove();

  const Block& loop = GetFirstLoop(test.graph());
  EXPECT_EQ(0u, stack_checks_to_remove.size());
  EXPECT_FALSE(stack_checks_to_remove.contains(loop.index().id()));

  IterationCount iter_count = analyzer.GetIterationCount(&loop);
  EXPECT_TRUE(iter_count.IsUnknown());
}

INSTANTIATE_TEST_SUITE_P(LoopUnrollingAnalyzerTest,
                         LoopUnrollingAnalyzerOverflowTest,
                         ::testing::ValuesIn(kUnderOverflowBoundedLoops));

#ifdef V8_ENABLE_WEBASSEMBLY
struct BoundedPartialLoop {
  int init;
  Cmp cmp;
  Binop binop;
  int max;
  uint32_t loop_body_size;
  uint32_t expected_unroll_count;
  const char* name;
};
std::ostream& operator<<(std::ostream& os, const BoundedPartialLoop& loop) {
  return os << loop.name;
}

static const BoundedPartialLoop kPartiallyUnrolledLoops[] = {
    {0, Cmp::kInt32LessThan, Binop::kWord32Add, 80, 8, 4,
     "for (int32_t i = 0;  i < 80;  i += 8)"},
    {0, Cmp::kInt32LessThan, Binop::kWord32Add, 160, 16, 4,
     "for (int32_t i = 0;  i < 160;  i += 16)"},
    {0, Cmp::kInt32LessThan, Binop::kWord32Add, 240, 24, 4,
     "for (int32_t i = 0;  i < 240;  i += 24)"},
    {0, Cmp::kInt32LessThan, Binop::kWord32Add, 320, 32, 3,
     "for (int32_t i = 0;  i < 320;  i += 32)"},
    {0, Cmp::kInt32LessThan, Binop::kWord32Add, 400, 40, 0,
     "for (int32_t i = 0;  i < 400;  i += 40)"},
};

using LoopUnrollingAnalyzerPartialUnrollTest =
    LoopUnrollingAnalyzerTestWithParam<BoundedPartialLoop>;

// Checking that the LoopUnrollingAnalyzer determines the partial unroll count
// base upon the size of the loop.
TEST_P(LoopUnrollingAnalyzerPartialUnrollTest, PartialUnrollCount) {
  BoundedPartialLoop params = GetParam();
  auto test = CreateFromGraph(1, [&params](auto& Asm) {
    using AssemblerT = std::remove_reference<decltype(Asm)>::type::Assembler;
    OpIndex cond = Asm.GetParameter(0);

    ScopedVar<Word32, AssemblerT> index(&Asm, params.init);

    WHILE(EmitCmp(Asm, params.cmp, index, params.max)) {
      __ WasmStackCheck(WasmStackCheckOp::Kind::kLoop);

      // Advance the {index} a number of times.
      for (uint32_t i = 0; i < params.loop_body_size; ++i) {
        index = EmitBinop(Asm, params.binop, index, 1);
      }
    }

    __ Return(index);
  });

  constexpr bool is_wasm = true;
  LoopUnrollingAnalyzer analyzer(test.zone(), &test.graph(), is_wasm);

  const Block& loop = GetFirstLoop(test.graph());
  EXPECT_EQ(analyzer.ShouldPartiallyUnrollLoop(&loop),
            params.expected_unroll_count != 0);
  if (analyzer.ShouldPartiallyUnrollLoop(&loop)) {
    EXPECT_EQ(params.expected_unroll_count,
              analyzer.GetPartialUnrollCount(&loop));
  }
}

INSTANTIATE_TEST_SUITE_P(LoopUnrollingAnalyzerTest,
                         LoopUnrollingAnalyzerPartialUnrollTest,
                         ::testing::ValuesIn(kPartiallyUnrolledLoops));
#endif  // V8_ENABLE_WEBASSEMBLY

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
