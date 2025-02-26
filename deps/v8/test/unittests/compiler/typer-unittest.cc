// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "src/base/overflowing-math.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects/objects-inl.h"
#include "test/common/types-fuzz.h"
#include "test/unittests/compiler/graph-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

// TODO(titzer): generate a large set of deterministic inputs for these tests.
class TyperTest : public TypedGraphTest {
 public:
  TyperTest()
      : TypedGraphTest(3),
        current_broker_(broker()),
        operation_typer_(broker(), zone()),
        types_(zone(), isolate(), random_number_generator()),
        javascript_(zone()),
        simplified_(zone()) {
    context_node_ = graph()->NewNode(common()->Parameter(2), graph()->start());
    rng_ = random_number_generator();

    integers.push_back(0);
    integers.push_back(0);
    integers.push_back(-1);
    integers.push_back(+1);
    integers.push_back(-V8_INFINITY);
    integers.push_back(+V8_INFINITY);
    for (int i = 0; i < 5; ++i) {
      double x = rng_->NextInt();
      integers.push_back(x);
      x *= rng_->NextInt();
      if (!IsMinusZero(x)) integers.push_back(x);
    }

    int32s.push_back(0);
    int32s.push_back(0);
    int32s.push_back(-1);
    int32s.push_back(+1);
    int32s.push_back(kMinInt);
    int32s.push_back(kMaxInt);
    for (int i = 0; i < 10; ++i) {
      int32s.push_back(rng_->NextInt());
    }
  }

  const int kRepetitions = 50;

  CurrentHeapBrokerScope current_broker_;
  OperationTyper operation_typer_;
  Types types_;
  JSOperatorBuilder javascript_;
  SimplifiedOperatorBuilder simplified_;
  Node* context_node_;
  v8::base::RandomNumberGenerator* rng_;
  std::vector<double> integers;
  std::vector<double> int32s;

  Type TypeUnaryOp(const Operator* op, Type type0) {
    Node* p0 = Parameter(0);
    NodeProperties::SetType(p0, type0);
    std::vector<Node*> inputs;
    inputs.push_back(p0);
    if (OperatorProperties::HasContextInput(op)) {
      inputs.push_back(context_node_);
    }
    for (int i = 0; i < OperatorProperties::GetFrameStateInputCount(op); i++) {
      inputs.push_back(EmptyFrameState());
    }
    for (int i = 0; i < op->EffectInputCount(); i++) {
      inputs.push_back(graph()->start());
    }
    for (int i = 0; i < op->ControlInputCount(); i++) {
      inputs.push_back(graph()->start());
    }
    Node* n = graph()->NewNode(op, static_cast<int>(inputs.size()),
                               &(inputs.front()));
    return NodeProperties::GetType(n);
  }

  Node* UndefinedConstant() {
    Handle<HeapObject> value = isolate()->factory()->undefined_value();
    return graph()->NewNode(common()->HeapConstant(value));
  }

  Type TypeBinaryOp(const Operator* op, Type lhs, Type rhs) {
    Node* p0 = Parameter(0);
    Node* p1 = Parameter(1);
    NodeProperties::SetType(p0, lhs);
    NodeProperties::SetType(p1, rhs);
    std::vector<Node*> inputs;
    inputs.push_back(p0);
    inputs.push_back(p1);
    if (JSOperator::IsBinaryWithFeedback(op->opcode())) {
      inputs.push_back(UndefinedConstant());  // Feedback vector.
    }
    if (OperatorProperties::HasContextInput(op)) {
      inputs.push_back(context_node_);
    }
    for (int i = 0; i < OperatorProperties::GetFrameStateInputCount(op); i++) {
      inputs.push_back(EmptyFrameState());
    }
    for (int i = 0; i < op->EffectInputCount(); i++) {
      inputs.push_back(graph()->start());
    }
    for (int i = 0; i < op->ControlInputCount(); i++) {
      inputs.push_back(graph()->start());
    }
    Node* n = graph()->NewNode(op, static_cast<int>(inputs.size()),
                               &(inputs.front()));
    return NodeProperties::GetType(n);
  }

  Type RandomRange(bool int32 = false) {
    std::vector<double>& numbers = int32 ? int32s : integers;
    double i = numbers[rng_->NextInt(static_cast<int>(numbers.size()))];
    double j = numbers[rng_->NextInt(static_cast<int>(numbers.size()))];
    return NewRange(i, j);
  }

  Type NewRange(double i, double j) {
    if (i > j) std::swap(i, j);
    return Type::Range(i, j, zone());
  }

  double RandomInt(double min, double max) {
    switch (rng_->NextInt(4)) {
      case 0:
        return min;
      case 1:
        return max;
      default:
        break;
    }
    if (min == +V8_INFINITY) return +V8_INFINITY;
    if (max == -V8_INFINITY) return -V8_INFINITY;
    if (min == -V8_INFINITY && max == +V8_INFINITY) {
      return rng_->NextInt() * static_cast<double>(rng_->NextInt());
    }
    double result = nearbyint(min + (max - min) * rng_->NextDouble());
    if (IsMinusZero(result)) return 0;
    if (std::isnan(result)) return rng_->NextInt(2) ? min : max;
    DCHECK(min <= result && result <= max);
    return result;
  }

  double RandomInt(const RangeType* range) {
    return RandomInt(range->Min(), range->Max());
  }

  Type RandomSubtype(Type type) {
    Type subtype;
    do {
      subtype = types_.Fuzz();
    } while (!subtype.Is(type));
    return subtype;
  }

  // Careful, this function runs O(max_width^5) trials.
  template <class BinaryFunction>
  void TestBinaryArithOpCloseToZero(const Operator* op, BinaryFunction opfun,
                                    int max_width) {
    const int min_min = -2 - max_width / 2;
    const int max_min = 2 + max_width / 2;
    for (int width = 0; width < max_width; width++) {
      for (int lmin = min_min; lmin <= max_min; lmin++) {
        for (int rmin = min_min; rmin <= max_min; rmin++) {
          Type r1 = NewRange(lmin, lmin + width);
          Type r2 = NewRange(rmin, rmin + width);
          Type expected_type = TypeBinaryOp(op, r1, r2);

          for (int x1 = lmin; x1 < lmin + width; x1++) {
            for (int x2 = rmin; x2 < rmin + width; x2++) {
              double result_value = opfun(x1, x2);
              Type result_type = Type::Constant(result_value, zone());
              EXPECT_TRUE(result_type.Is(expected_type));
            }
          }
        }
      }
    }
  }

  template <class BinaryFunction>
  void TestBinaryArithOp(const Operator* op, BinaryFunction opfun) {
    TestBinaryArithOpCloseToZero(op, opfun, 8);
    for (int i = 0; i < 100; ++i) {
      Type r1 = RandomRange();
      Type r2 = RandomRange();
      Type expected_type = TypeBinaryOp(op, r1, r2);
      for (int j = 0; j < 10; j++) {
        double x1 = RandomInt(r1.AsRange());
        double x2 = RandomInt(r2.AsRange());
        double result_value = opfun(x1, x2);
        Type result_type = Type::Constant(result_value, zone());
        EXPECT_TRUE(result_type.Is(expected_type));
      }
    }
    // Test extreme cases.
    double x1 = +1e-308;
    double x2 = -1e-308;
    Type r1 = Type::Constant(x1, zone());
    Type r2 = Type::Constant(x2, zone());
    Type expected_type = TypeBinaryOp(op, r1, r2);
    double result_value = opfun(x1, x2);
    Type result_type = Type::Constant(result_value, zone());
    EXPECT_TRUE(result_type.Is(expected_type));
  }

  template <class BinaryFunction>
  void TestBinaryCompareOp(const Operator* op, BinaryFunction opfun) {
    for (int i = 0; i < 100; ++i) {
      Type r1 = RandomRange();
      Type r2 = RandomRange();
      Type expected_type = TypeBinaryOp(op, r1, r2);
      for (int j = 0; j < 10; j++) {
        double x1 = RandomInt(r1.AsRange());
        double x2 = RandomInt(r2.AsRange());
        bool result_value = opfun(x1, x2);
        Type result_type = Type::Constant(
            broker(),
            result_value ? broker()->true_value() : broker()->false_value(),
            zone());
        EXPECT_TRUE(result_type.Is(expected_type));
      }
    }
  }

  template <class BinaryFunction>
  void TestBinaryBitOp(const Operator* op, BinaryFunction opfun) {
    for (int i = 0; i < 100; ++i) {
      Type r1 = RandomRange(true);
      Type r2 = RandomRange(true);
      Type expected_type = TypeBinaryOp(op, r1, r2);
      for (int j = 0; j < 10; j++) {
        int32_t x1 = static_cast<int32_t>(RandomInt(r1.AsRange()));
        int32_t x2 = static_cast<int32_t>(RandomInt(r2.AsRange()));
        double result_value = opfun(x1, x2);
        Type result_type = Type::Constant(result_value, zone());
        EXPECT_TRUE(result_type.Is(expected_type));
      }
    }
  }

  using UnaryTyper = std::function<Type(Type)>;
  using BinaryTyper = std::function<Type(Type, Type)>;

  void TestUnaryMonotonicity(UnaryTyper typer, Type upper1 = Type::Any()) {
    Type type1 = Type::Intersect(types_.Fuzz(), upper1, zone());
    DCHECK(type1.Is(upper1));
    Type type = typer(type1);

    Type subtype1 = RandomSubtype(type1);
    Type subtype = typer(subtype1);

    EXPECT_TRUE(subtype.Is(type));
  }

  void TestBinaryMonotonicity(BinaryTyper typer, Type upper1 = Type::Any(),
                              Type upper2 = Type::Any()) {
    Type type1 = Type::Intersect(types_.Fuzz(), upper1, zone());
    DCHECK(type1.Is(upper1));
    Type type2 = Type::Intersect(types_.Fuzz(), upper2, zone());
    DCHECK(type2.Is(upper2));
    Type type = typer(type1, type2);

    Type subtype1 = RandomSubtype(type1);
    Type subtype2 = RandomSubtype(type2);
    Type subtype = typer(subtype1, subtype2);

    EXPECT_TRUE(subtype.Is(type));
  }

  void TestUnaryMonotonicity(const Operator* op, Type upper1 = Type::Any()) {
    UnaryTyper typer = [&](Type type1) { return TypeUnaryOp(op, type1); };
    for (int i = 0; i < kRepetitions; ++i) {
      TestUnaryMonotonicity(typer, upper1);
    }
  }

  void TestBinaryMonotonicity(const Operator* op, Type upper1 = Type::Any(),
                              Type upper2 = Type::Any()) {
    BinaryTyper typer = [&](Type type1, Type type2) {
      return TypeBinaryOp(op, type1, type2);
    };
    for (int i = 0; i < kRepetitions; ++i) {
      TestBinaryMonotonicity(typer, upper1, upper2);
    }
  }
};


namespace {

int32_t shift_left(int32_t x, int32_t y) {
  return static_cast<uint32_t>(x) << (y & 0x1F);
}
int32_t shift_right(int32_t x, int32_t y) { return x >> (y & 0x1F); }
int32_t bit_or(int32_t x, int32_t y) { return x | y; }
int32_t bit_and(int32_t x, int32_t y) { return x & y; }
int32_t bit_xor(int32_t x, int32_t y) { return x ^ y; }
double divide_double_double(double x, double y) { return base::Divide(x, y); }
double modulo_double_double(double x, double y) { return Modulo(x, y); }

FeedbackSource FeedbackSourceWithOneBinarySlot(TyperTest* R) {
  return FeedbackSource{
      FeedbackVector::NewWithOneBinarySlotForTesting(R->zone(), R->isolate()),
      FeedbackSlot{0}};
}

FeedbackSource FeedbackSourceWithOneCompareSlot(TyperTest* R) {
  return FeedbackSource{
      FeedbackVector::NewWithOneCompareSlotForTesting(R->zone(), R->isolate()),
      FeedbackSlot{0}};
}

}  // namespace


//------------------------------------------------------------------------------
// Soundness
//   For simplicity, we currently only test soundness on expression operators
//   that have a direct equivalent in C++.  Also, testing is currently limited
//   to ranges as input types.

TEST_F(TyperTest, TypeJSAdd) {
  TestBinaryArithOp(javascript_.Add(FeedbackSourceWithOneBinarySlot(this)),
                    std::plus<double>());
}

TEST_F(TyperTest, TypeJSSubtract) {
  TestBinaryArithOp(javascript_.Subtract(FeedbackSourceWithOneBinarySlot(this)),
                    std::minus<double>());
}

TEST_F(TyperTest, TypeJSMultiply) {
  TestBinaryArithOp(javascript_.Multiply(FeedbackSourceWithOneBinarySlot(this)),
                    std::multiplies<double>());
}

TEST_F(TyperTest, TypeJSDivide) {
  TestBinaryArithOp(javascript_.Divide(FeedbackSourceWithOneBinarySlot(this)),
                    divide_double_double);
}

TEST_F(TyperTest, TypeJSModulus) {
  TestBinaryArithOp(javascript_.Modulus(FeedbackSourceWithOneBinarySlot(this)),
                    modulo_double_double);
}

TEST_F(TyperTest, TypeJSBitwiseOr) {
  TestBinaryBitOp(javascript_.BitwiseOr(FeedbackSourceWithOneBinarySlot(this)),
                  bit_or);
}

TEST_F(TyperTest, TypeJSBitwiseAnd) {
  TestBinaryBitOp(javascript_.BitwiseAnd(FeedbackSourceWithOneBinarySlot(this)),
                  bit_and);
}

TEST_F(TyperTest, TypeJSBitwiseXor) {
  TestBinaryBitOp(javascript_.BitwiseXor(FeedbackSourceWithOneBinarySlot(this)),
                  bit_xor);
}

TEST_F(TyperTest, TypeJSShiftLeft) {
  TestBinaryBitOp(javascript_.ShiftLeft(FeedbackSourceWithOneBinarySlot(this)),
                  shift_left);
}

TEST_F(TyperTest, TypeJSShiftRight) {
  TestBinaryBitOp(javascript_.ShiftRight(FeedbackSourceWithOneBinarySlot(this)),
                  shift_right);
}

TEST_F(TyperTest, TypeJSLessThan) {
  TestBinaryCompareOp(
      javascript_.LessThan(FeedbackSourceWithOneCompareSlot(this)),
      std::less<double>());
}

TEST_F(TyperTest, TypeNumberLessThan) {
  TestBinaryCompareOp(simplified_.NumberLessThan(), std::less<double>());
}

TEST_F(TyperTest, TypeSpeculativeNumberLessThan) {
  TestBinaryCompareOp(simplified_.SpeculativeNumberLessThan(
                          NumberOperationHint::kNumberOrOddball),
                      std::less<double>());
}

TEST_F(TyperTest, TypeJSLessThanOrEqual) {
  TestBinaryCompareOp(
      javascript_.LessThanOrEqual(FeedbackSourceWithOneCompareSlot(this)),
      std::less_equal<double>());
}

TEST_F(TyperTest, TypeNumberLessThanOrEqual) {
  TestBinaryCompareOp(simplified_.NumberLessThanOrEqual(),
                      std::less_equal<double>());
}

TEST_F(TyperTest, TypeSpeculativeNumberLessThanOrEqual) {
  TestBinaryCompareOp(simplified_.SpeculativeNumberLessThanOrEqual(
                          NumberOperationHint::kNumberOrOddball),
                      std::less_equal<double>());
}

TEST_F(TyperTest, TypeJSGreaterThan) {
  TestBinaryCompareOp(
      javascript_.GreaterThan(FeedbackSourceWithOneCompareSlot(this)),
      std::greater<double>());
}


TEST_F(TyperTest, TypeJSGreaterThanOrEqual) {
  TestBinaryCompareOp(
      javascript_.GreaterThanOrEqual(FeedbackSourceWithOneCompareSlot(this)),
      std::greater_equal<double>());
}

TEST_F(TyperTest, TypeJSEqual) {
  TestBinaryCompareOp(javascript_.Equal(FeedbackSourceWithOneCompareSlot(this)),
                      std::equal_to<double>());
}

TEST_F(TyperTest, TypeNumberEqual) {
  TestBinaryCompareOp(simplified_.NumberEqual(), std::equal_to<double>());
}

TEST_F(TyperTest, TypeSpeculativeNumberEqual) {
  TestBinaryCompareOp(
      simplified_.SpeculativeNumberEqual(NumberOperationHint::kNumberOrOddball),
      std::equal_to<double>());
}

// For numbers there's no difference between strict and non-strict equality.
TEST_F(TyperTest, TypeJSStrictEqual) {
  TestBinaryCompareOp(
      javascript_.StrictEqual(FeedbackSourceWithOneCompareSlot(this)),
      std::equal_to<double>());
}

//------------------------------------------------------------------------------
// Typer Monotonicity

// JS UNOPs without hint
#define TEST_MONOTONICITY(name)                \
  TEST_F(TyperTest, Monotonicity_##name) {     \
    TestUnaryMonotonicity(javascript_.name()); \
  }
TEST_MONOTONICITY(ToLength)
TEST_MONOTONICITY(ToName)
TEST_MONOTONICITY(ToNumber)
TEST_MONOTONICITY(ToObject)
TEST_MONOTONICITY(ToString)
#undef TEST_MONOTONICITY

// JS compare ops.
#define TEST_MONOTONICITY(name)                                    \
  TEST_F(TyperTest, Monotonicity_##name) {                         \
    TestBinaryMonotonicity(                                        \
        javascript_.name(FeedbackSourceWithOneCompareSlot(this))); \
  }
TEST_MONOTONICITY(Equal)
TEST_MONOTONICITY(StrictEqual)
TEST_MONOTONICITY(LessThan)
TEST_MONOTONICITY(GreaterThan)
TEST_MONOTONICITY(LessThanOrEqual)
TEST_MONOTONICITY(GreaterThanOrEqual)
#undef TEST_MONOTONICITY

// JS binary ops.
#define TEST_MONOTONICITY(name)                                   \
  TEST_F(TyperTest, Monotonicity_##name) {                        \
    TestBinaryMonotonicity(                                       \
        javascript_.name(FeedbackSourceWithOneBinarySlot(this))); \
  }
TEST_MONOTONICITY(Add)
TEST_MONOTONICITY(BitwiseAnd)
TEST_MONOTONICITY(BitwiseOr)
TEST_MONOTONICITY(BitwiseXor)
TEST_MONOTONICITY(Divide)
TEST_MONOTONICITY(Modulus)
TEST_MONOTONICITY(Multiply)
TEST_MONOTONICITY(ShiftLeft)
TEST_MONOTONICITY(ShiftRight)
TEST_MONOTONICITY(ShiftRightLogical)
TEST_MONOTONICITY(Subtract)
#undef TEST_MONOTONICITY

TEST_F(TyperTest, Monotonicity_InstanceOf) {
  TestBinaryMonotonicity(javascript_.InstanceOf(FeedbackSource()));
}

TEST_F(TyperTest, Monotonicity_OrdinaryHasInstance) {
  TestBinaryMonotonicity(javascript_.OrdinaryHasInstance());
}

// SIMPLIFIED UNOPs without hint
#define TEST_MONOTONICITY(name)                \
  TEST_F(TyperTest, Monotonicity_##name) {     \
    TestUnaryMonotonicity(simplified_.name()); \
  }
TEST_MONOTONICITY(ObjectIsDetectableCallable)
TEST_MONOTONICITY(ObjectIsNaN)
TEST_MONOTONICITY(ObjectIsNonCallable)
TEST_MONOTONICITY(ObjectIsNumber)
TEST_MONOTONICITY(ObjectIsReceiver)
TEST_MONOTONICITY(ObjectIsSmi)
TEST_MONOTONICITY(ObjectIsString)
TEST_MONOTONICITY(ObjectIsSymbol)
TEST_MONOTONICITY(ObjectIsUndetectable)
TEST_MONOTONICITY(TypeOf)
TEST_MONOTONICITY(ToBoolean)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs without hint, with Number input restriction
#define TEST_MONOTONICITY(name)                                \
  TEST_F(TyperTest, Monotonicity_##name) {                     \
    TestBinaryMonotonicity(simplified_.name(), Type::Number(), \
                           Type::Number());                    \
  }
SIMPLIFIED_NUMBER_BINOP_LIST(TEST_MONOTONICITY)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs without hint, without input restriction
#define TEST_MONOTONICITY(name)                 \
  TEST_F(TyperTest, Monotonicity_##name) {      \
    TestBinaryMonotonicity(simplified_.name()); \
  }
TEST_MONOTONICITY(NumberLessThan)
TEST_MONOTONICITY(NumberLessThanOrEqual)
TEST_MONOTONICITY(NumberEqual)
TEST_MONOTONICITY(ReferenceEqual)
TEST_MONOTONICITY(StringEqual)
TEST_MONOTONICITY(StringLessThan)
TEST_MONOTONICITY(StringLessThanOrEqual)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs with NumberOperationHint, without input restriction
#define TEST_MONOTONICITY(name)                                             \
  TEST_F(TyperTest, Monotonicity_##name) {                                  \
    TestBinaryMonotonicity(simplified_.name(NumberOperationHint::kNumber)); \
  }
TEST_MONOTONICITY(SpeculativeNumberEqual)
TEST_MONOTONICITY(SpeculativeNumberLessThan)
TEST_MONOTONICITY(SpeculativeNumberLessThanOrEqual)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs with NumberOperationHint, without input restriction
#define TEST_MONOTONICITY(name)                                             \
  TEST_F(TyperTest, Monotonicity_##name) {                                  \
    TestBinaryMonotonicity(simplified_.name(NumberOperationHint::kNumber)); \
  }
SIMPLIFIED_SPECULATIVE_NUMBER_BINOP_LIST(TEST_MONOTONICITY)
#undef TEST_MONOTONICITY

//------------------------------------------------------------------------------
// OperationTyper Monotonicity

// SIMPLIFIED UNOPs with Number input restriction
#define TEST_MONOTONICITY(name)                      \
  TEST_F(TyperTest, Monotonicity_Operation_##name) { \
    UnaryTyper typer = [&](Type type1) {             \
      return operation_typer_.name(type1);           \
    };                                               \
    for (int i = 0; i < kRepetitions; ++i) {         \
      TestUnaryMonotonicity(typer, Type::Number());  \
    }                                                \
  }
SIMPLIFIED_NUMBER_UNOP_LIST(TEST_MONOTONICITY)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs with Number input restriction
#define TEST_MONOTONICITY(name)                                      \
  TEST_F(TyperTest, Monotonicity_Operation_##name) {                 \
    BinaryTyper typer = [&](Type type1, Type type2) {                \
      return operation_typer_.name(type1, type2);                    \
    };                                                               \
    for (int i = 0; i < kRepetitions; ++i) {                         \
      TestBinaryMonotonicity(typer, Type::Number(), Type::Number()); \
    }                                                                \
  }
SIMPLIFIED_NUMBER_BINOP_LIST(TEST_MONOTONICITY)
#undef TEST_MONOTONICITY

// SIMPLIFIED BINOPs without input restriction
#define TEST_MONOTONICITY(name)                       \
  TEST_F(TyperTest, Monotonicity_Operation_##name) {  \
    BinaryTyper typer = [&](Type type1, Type type2) { \
      return operation_typer_.name(type1, type2);     \
    };                                                \
    for (int i = 0; i < kRepetitions; ++i) {          \
      TestBinaryMonotonicity(typer);                  \
    }                                                 \
  }
SIMPLIFIED_SPECULATIVE_NUMBER_BINOP_LIST(TEST_MONOTONICITY)
#undef TEST_MONOTONICITY

TEST_F(TyperTest, Manual_Operation_NumberMax) {
  BinaryTyper t = [&](Type type1, Type type2) {
    return operation_typer_.NumberMax(type1, type2);
  };

  Type zero = Type::Constant(0, zone());
  Type zero_or_minuszero = Type::Union(zero, Type::MinusZero(), zone());
  Type dot_five = Type::Constant(0.5, zone());

  Type a = t(Type::MinusZero(), Type::MinusZero());
  CHECK(Type::MinusZero().Is(a));

  Type b = t(Type::MinusZero(), zero_or_minuszero);
  CHECK(Type::MinusZero().Is(b));
  CHECK(zero.Is(b));
  CHECK(a.Is(b));  // Monotonicity.

  Type c = t(zero_or_minuszero, Type::MinusZero());
  CHECK(Type::MinusZero().Is(c));
  CHECK(zero.Is(c));
  CHECK(a.Is(c));  // Monotonicity.

  Type d = t(zero_or_minuszero, zero_or_minuszero);
  CHECK(Type::MinusZero().Is(d));
  CHECK(zero.Is(d));
  CHECK(b.Is(d));  // Monotonicity.
  CHECK(c.Is(d));  // Monotonicity.

  Type e =
      t(Type::MinusZero(), Type::Union(Type::MinusZero(), dot_five, zone()));
  CHECK(Type::MinusZero().Is(e));
  CHECK(dot_five.Is(e));
  CHECK(a.Is(e));  // Monotonicity.

  Type f = t(Type::MinusZero(), zero);
  CHECK(zero.Is(f));
  CHECK(f.Is(b));  // Monotonicity.

  Type g = t(zero, Type::MinusZero());
  CHECK(zero.Is(g));
  CHECK(g.Is(c));  // Monotonicity.

  Type h = t(Type::Signed32(), Type::MinusZero());
  CHECK(Type::MinusZero().Is(h));

  Type i = t(Type::Signed32(), zero_or_minuszero);
  CHECK(h.Is(i));  // Monotonicity.
}

TEST_F(TyperTest, Manual_Operation_NumberMin) {
  BinaryTyper t = [&](Type type1, Type type2) {
    return operation_typer_.NumberMin(type1, type2);
  };

  Type zero = Type::Constant(0, zone());
  Type zero_or_minuszero = Type::Union(zero, Type::MinusZero(), zone());
  Type one = Type::Constant(1, zone());
  Type minus_dot_five = Type::Constant(-0.5, zone());

  Type a = t(Type::MinusZero(), Type::MinusZero());
  CHECK(Type::MinusZero().Is(a));

  Type b = t(Type::MinusZero(), zero_or_minuszero);
  CHECK(Type::MinusZero().Is(b));
  CHECK(zero.Is(b));
  CHECK(a.Is(b));  // Monotonicity.

  Type c = t(zero_or_minuszero, Type::MinusZero());
  CHECK(Type::MinusZero().Is(c));
  CHECK(zero.Is(c));
  CHECK(a.Is(c));  // Monotonicity.

  Type d = t(zero_or_minuszero, zero_or_minuszero);
  CHECK(Type::MinusZero().Is(d));
  CHECK(zero.Is(d));
  CHECK(b.Is(d));  // Monotonicity.
  CHECK(c.Is(d));  // Monotonicity.

  Type e = t(Type::MinusZero(),
             Type::Union(Type::MinusZero(), minus_dot_five, zone()));
  CHECK(Type::MinusZero().Is(e));
  CHECK(minus_dot_five.Is(e));
  CHECK(a.Is(e));  // Monotonicity.

  Type f = t(Type::MinusZero(), zero);
  CHECK(Type::MinusZero().Is(f));
  CHECK(f.Is(b));  // Monotonicity.

  Type g = t(zero, Type::MinusZero());
  CHECK(Type::MinusZero().Is(g));
  CHECK(g.Is(c));  // Monotonicity.

  Type h = t(one, Type::MinusZero());
  CHECK(Type::MinusZero().Is(h));

  Type i = t(Type::Signed32(), Type::MinusZero());
  CHECK(Type::MinusZero().Is(i));

  Type j = t(Type::Signed32(), zero_or_minuszero);
  CHECK(i.Is(j));  // Monotonicity.
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
