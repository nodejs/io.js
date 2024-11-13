// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter.h"

#include <tuple>

#include "src/api/api-inl.h"
#include "src/base/overflowing-math.h"
#include "src/codegen/compiler.h"
#include "src/execution/execution.h"
#include "src/handles/handles.h"
#include "src/heap/heap-inl.h"
#include "src/init/v8.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/interpreter/bytecode-label.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "test/unittests/interpreter/interpreter-tester.h"

namespace v8 {
namespace internal {
namespace interpreter {

class InterpreterTest : public WithContextMixin<TestWithIsolateAndZone> {
 public:
  Handle<Object> RunBytecode(Handle<BytecodeArray> bytecode_array,
                             MaybeHandle<FeedbackMetadata> feedback_metadata =
                                 MaybeHandle<FeedbackMetadata>()) {
    InterpreterTester tester(i_isolate(), bytecode_array, feedback_metadata);
    auto callable = tester.GetCallable<>();
    return callable().ToHandleChecked();
  }
};

static int GetIndex(FeedbackSlot slot) {
  return FeedbackVector::GetIndex(slot);
}

using ToBooleanMode = BytecodeArrayBuilder::ToBooleanMode;

TEST_F(InterpreterTest, InterpreterReturn) {
  Handle<Object> undefined_value = i_isolate()->factory()->undefined_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(undefined_value));
}

TEST_F(InterpreterTest, InterpreterLoadUndefined) {
  Handle<Object> undefined_value = i_isolate()->factory()->undefined_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.LoadUndefined().Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(undefined_value));
}

TEST_F(InterpreterTest, InterpreterLoadNull) {
  Handle<Object> null_value = i_isolate()->factory()->null_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.LoadNull().Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(null_value));
}

TEST_F(InterpreterTest, InterpreterLoadTheHole) {
  Handle<Object> the_hole_value = i_isolate()->factory()->the_hole_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.LoadTheHole().Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(the_hole_value));
}

TEST_F(InterpreterTest, InterpreterLoadTrue) {
  Handle<Object> true_value = i_isolate()->factory()->true_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.LoadTrue().Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(true_value));
}

TEST_F(InterpreterTest, InterpreterLoadFalse) {
  Handle<Object> false_value = i_isolate()->factory()->false_value();

  BytecodeArrayBuilder builder(zone(), 1, 0);
  builder.LoadFalse().Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> return_val = RunBytecode(bytecode_array);
  CHECK(return_val.is_identical_to(false_value));
}

TEST_F(InterpreterTest, InterpreterLoadLiteral) {
  // Small Smis.
  for (int i = -128; i < 128; i++) {
    BytecodeArrayBuilder builder(zone(), 1, 0);
    builder.LoadLiteral(Smi::FromInt(i)).Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    DirectHandle<Object> return_val = RunBytecode(bytecode_array);
    CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(i));
  }

  // Large Smis.
  {
    BytecodeArrayBuilder builder(zone(), 1, 0);

    builder.LoadLiteral(Smi::FromInt(0x12345678)).Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    DirectHandle<Object> return_val = RunBytecode(bytecode_array);
    CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(0x12345678));
  }

  // Heap numbers.
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));

    BytecodeArrayBuilder builder(zone(), 1, 0);

    builder.LoadLiteral(-2.1e19).Return();

    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    DirectHandle<Object> return_val = RunBytecode(bytecode_array);
    CHECK_EQ(i::Cast<i::HeapNumber>(*return_val)->value(), -2.1e19);
  }

  // Strings.
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));

    BytecodeArrayBuilder builder(zone(), 1, 0);

    const AstRawString* raw_string = ast_factory.GetOneByteString("String");
    builder.LoadLiteral(raw_string).Return();

    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    DirectHandle<Object> return_val = RunBytecode(bytecode_array);
    CHECK(i::Cast<i::String>(*return_val)->Equals(*raw_string->string()));
  }
}

TEST_F(InterpreterTest, InterpreterLoadStoreRegisters) {
  Handle<Object> true_value = i_isolate()->factory()->true_value();
  for (int i = 0; i <= kMaxInt8; i++) {
    BytecodeArrayBuilder builder(zone(), 1, i + 1);

    Register reg(i);
    builder.LoadTrue()
        .StoreAccumulatorInRegister(reg)
        .LoadFalse()
        .LoadAccumulatorWithRegister(reg)
        .Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    Handle<Object> return_val = RunBytecode(bytecode_array);
    CHECK(return_val.is_identical_to(true_value));
  }
}

static const Token::Value kShiftOperators[] = {Token::kShl, Token::kSar,
                                               Token::kShr};

static const Token::Value kArithmeticOperators[] = {
    Token::kBitOr, Token::kBitXor, Token::kBitAnd, Token::kShl,
    Token::kSar,   Token::kShr,    Token::kAdd,    Token::kSub,
    Token::kMul,   Token::kDiv,    Token::kMod};

static double BinaryOpC(Token::Value op, double lhs, double rhs) {
  switch (op) {
    case Token::kAdd:
      return lhs + rhs;
    case Token::kSub:
      return lhs - rhs;
    case Token::kMul:
      return lhs * rhs;
    case Token::kDiv:
      return base::Divide(lhs, rhs);
    case Token::kMod:
      return Modulo(lhs, rhs);
    case Token::kBitOr:
      return (v8::internal::DoubleToInt32(lhs) |
              v8::internal::DoubleToInt32(rhs));
    case Token::kBitXor:
      return (v8::internal::DoubleToInt32(lhs) ^
              v8::internal::DoubleToInt32(rhs));
    case Token::kBitAnd:
      return (v8::internal::DoubleToInt32(lhs) &
              v8::internal::DoubleToInt32(rhs));
    case Token::kShl: {
      return base::ShlWithWraparound(DoubleToInt32(lhs), DoubleToInt32(rhs));
    }
    case Token::kSar: {
      int32_t val = v8::internal::DoubleToInt32(lhs);
      uint32_t count = v8::internal::DoubleToUint32(rhs) & 0x1F;
      int32_t result = val >> count;
      return result;
    }
    case Token::kShr: {
      uint32_t val = v8::internal::DoubleToUint32(lhs);
      uint32_t count = v8::internal::DoubleToUint32(rhs) & 0x1F;
      uint32_t result = val >> count;
      return result;
    }
    default:
      UNREACHABLE();
  }
}

TEST_F(InterpreterTest, InterpreterShiftOpsSmi) {
  int lhs_inputs[] = {0, -17, -182, 1073741823, -1};
  int rhs_inputs[] = {5, 2, 1, -1, -2, 0, 31, 32, -32, 64, 37};
  for (size_t l = 0; l < arraysize(lhs_inputs); l++) {
    for (size_t r = 0; r < arraysize(rhs_inputs); r++) {
      for (size_t o = 0; o < arraysize(kShiftOperators); o++) {
        Factory* factory = i_isolate()->factory();
        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register reg(0);
        int lhs = lhs_inputs[l];
        int rhs = rhs_inputs[r];
        builder.LoadLiteral(Smi::FromInt(lhs))
            .StoreAccumulatorInRegister(reg)
            .LoadLiteral(Smi::FromInt(rhs))
            .BinaryOperation(kShiftOperators[o], reg, GetIndex(slot))
            .Return();
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());

        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        DirectHandle<Object> expected_value =
            factory->NewNumber(BinaryOpC(kShiftOperators[o], lhs, rhs));
        CHECK(Object::SameValue(*return_value, *expected_value));
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterBinaryOpsSmi) {
  int lhs_inputs[] = {3266, 1024, 0, -17, -18000};
  int rhs_inputs[] = {3266, 5, 4, 3, 2, 1, -1, -2};
  for (size_t l = 0; l < arraysize(lhs_inputs); l++) {
    for (size_t r = 0; r < arraysize(rhs_inputs); r++) {
      for (size_t o = 0; o < arraysize(kArithmeticOperators); o++) {
        Factory* factory = i_isolate()->factory();
        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register reg(0);
        int lhs = lhs_inputs[l];
        int rhs = rhs_inputs[r];
        builder.LoadLiteral(Smi::FromInt(lhs))
            .StoreAccumulatorInRegister(reg)
            .LoadLiteral(Smi::FromInt(rhs))
            .BinaryOperation(kArithmeticOperators[o], reg, GetIndex(slot))
            .Return();
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());

        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        DirectHandle<Object> expected_value =
            factory->NewNumber(BinaryOpC(kArithmeticOperators[o], lhs, rhs));
        CHECK(Object::SameValue(*return_value, *expected_value));
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterBinaryOpsHeapNumber) {
  double lhs_inputs[] = {3266.101, 1024.12, 0.01, -17.99, -18000.833, 9.1e17};
  double rhs_inputs[] = {3266.101, 5.999, 4.778, 3.331,  2.643,
                         1.1,      -1.8,  -2.9,  8.3e-27};
  for (size_t l = 0; l < arraysize(lhs_inputs); l++) {
    for (size_t r = 0; r < arraysize(rhs_inputs); r++) {
      for (size_t o = 0; o < arraysize(kArithmeticOperators); o++) {
        Factory* factory = i_isolate()->factory();
        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register reg(0);
        double lhs = lhs_inputs[l];
        double rhs = rhs_inputs[r];
        builder.LoadLiteral(lhs)
            .StoreAccumulatorInRegister(reg)
            .LoadLiteral(rhs)
            .BinaryOperation(kArithmeticOperators[o], reg, GetIndex(slot))
            .Return();
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());

        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        DirectHandle<Object> expected_value =
            factory->NewNumber(BinaryOpC(kArithmeticOperators[o], lhs, rhs));
        CHECK(Object::SameValue(*return_value, *expected_value));
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterBinaryOpsBigInt) {
  // This test only checks that the recorded type feedback is kBigInt.
  AstBigInt inputs[] = {AstBigInt("1"), AstBigInt("-42"), AstBigInt("0xFFFF")};
  for (size_t l = 0; l < arraysize(inputs); l++) {
    for (size_t r = 0; r < arraysize(inputs); r++) {
      for (size_t o = 0; o < arraysize(kArithmeticOperators); o++) {
        // Skip over unsigned right shift.
        if (kArithmeticOperators[o] == Token::kShr) continue;

        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register reg(0);
        auto lhs = inputs[l];
        auto rhs = inputs[r];
        builder.LoadLiteral(lhs)
            .StoreAccumulatorInRegister(reg)
            .LoadLiteral(rhs)
            .BinaryOperation(kArithmeticOperators[o], reg, GetIndex(slot))
            .Return();
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());

        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        CHECK(IsBigInt(*return_value));
        if (tester.HasFeedbackMetadata()) {
          Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
          CHECK(IsSmi(feedback));
          // TODO(panq): Create a standalone unit test for kBigInt64.
          CHECK(BinaryOperationFeedback::kBigInt64 ==
                    feedback.ToSmi().value() ||
                BinaryOperationFeedback::kBigInt == feedback.ToSmi().value());
        }
      }
    }
  }
}

namespace {

struct LiteralForTest {
  enum Type { kString, kHeapNumber, kSmi, kTrue, kFalse, kUndefined, kNull };

  explicit LiteralForTest(const AstRawString* string)
      : type(kString), string(string) {}
  explicit LiteralForTest(double number) : type(kHeapNumber), number(number) {}
  explicit LiteralForTest(int smi) : type(kSmi), smi(smi) {}
  explicit LiteralForTest(Type type) : type(type) {}

  Type type;
  union {
    const AstRawString* string;
    double number;
    int smi;
  };
};

void LoadLiteralForTest(BytecodeArrayBuilder* builder,
                        const LiteralForTest& value) {
  switch (value.type) {
    case LiteralForTest::kString:
      builder->LoadLiteral(value.string);
      return;
    case LiteralForTest::kHeapNumber:
      builder->LoadLiteral(value.number);
      return;
    case LiteralForTest::kSmi:
      builder->LoadLiteral(Smi::FromInt(value.smi));
      return;
    case LiteralForTest::kTrue:
      builder->LoadTrue();
      return;
    case LiteralForTest::kFalse:
      builder->LoadFalse();
      return;
    case LiteralForTest::kUndefined:
      builder->LoadUndefined();
      return;
    case LiteralForTest::kNull:
      builder->LoadNull();
      return;
  }
  UNREACHABLE();
}

}  // anonymous namespace

TEST_F(InterpreterTest, InterpreterStringAdd) {
  Factory* factory = i_isolate()->factory();
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  struct TestCase {
    const AstRawString* lhs;
    LiteralForTest rhs;
    Handle<Object> expected_value;
    int32_t expected_feedback;
  } test_cases[] = {
      {ast_factory.GetOneByteString("a"),
       LiteralForTest(ast_factory.GetOneByteString("b")),
       factory->NewStringFromStaticChars("ab"),
       BinaryOperationFeedback::kString},
      {ast_factory.GetOneByteString("aaaaaa"),
       LiteralForTest(ast_factory.GetOneByteString("b")),
       factory->NewStringFromStaticChars("aaaaaab"),
       BinaryOperationFeedback::kString},
      {ast_factory.GetOneByteString("aaa"),
       LiteralForTest(ast_factory.GetOneByteString("bbbbb")),
       factory->NewStringFromStaticChars("aaabbbbb"),
       BinaryOperationFeedback::kString},
      {ast_factory.GetOneByteString(""),
       LiteralForTest(ast_factory.GetOneByteString("b")),
       factory->NewStringFromStaticChars("b"),
       BinaryOperationFeedback::kString},
      {ast_factory.GetOneByteString("a"),
       LiteralForTest(ast_factory.GetOneByteString("")),
       factory->NewStringFromStaticChars("a"),
       BinaryOperationFeedback::kString},
      {ast_factory.GetOneByteString("1.11"), LiteralForTest(2.5),
       factory->NewStringFromStaticChars("1.112.5"),
       BinaryOperationFeedback::kAny},
      {ast_factory.GetOneByteString("-1.11"), LiteralForTest(2.56),
       factory->NewStringFromStaticChars("-1.112.56"),
       BinaryOperationFeedback::kAny},
      {ast_factory.GetOneByteString(""), LiteralForTest(2.5),
       factory->NewStringFromStaticChars("2.5"), BinaryOperationFeedback::kAny},
  };
  ast_factory.Internalize(i_isolate());

  for (size_t i = 0; i < arraysize(test_cases); i++) {
    FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);
    FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
    Handle<i::FeedbackMetadata> metadata =
        FeedbackMetadata::New(i_isolate(), &feedback_spec);

    Register reg(0);
    builder.LoadLiteral(test_cases[i].lhs).StoreAccumulatorInRegister(reg);
    LoadLiteralForTest(&builder, test_cases[i].rhs);
    builder.BinaryOperation(Token::kAdd, reg, GetIndex(slot)).Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *test_cases[i].expected_value));

    if (tester.HasFeedbackMetadata()) {
      Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
      CHECK(IsSmi(feedback));
      CHECK_EQ(test_cases[i].expected_feedback, feedback.ToSmi().value());
    }
  }
}

TEST_F(InterpreterTest, InterpreterReceiverParameter) {
  BytecodeArrayBuilder builder(zone(), 1, 0);

  builder.LoadAccumulatorWithRegister(builder.Receiver()).Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  Handle<Object> object = InterpreterTester::NewObject("({ val : 123 })");

  InterpreterTester tester(i_isolate(), bytecode_array);
  auto callable = tester.GetCallableWithReceiver<>();
  Handle<Object> return_val = callable(object).ToHandleChecked();

  CHECK(return_val.is_identical_to(object));
}

TEST_F(InterpreterTest, InterpreterParameter0) {
  BytecodeArrayBuilder builder(zone(), 2, 0);

  builder.LoadAccumulatorWithRegister(builder.Parameter(0)).Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array);
  auto callable = tester.GetCallable<Handle<Object>>();

  // Check for heap objects.
  Handle<Object> true_value = i_isolate()->factory()->true_value();
  Handle<Object> return_val = callable(true_value).ToHandleChecked();
  CHECK(return_val.is_identical_to(true_value));

  // Check for Smis.
  return_val =
      callable(Handle<Smi>(Smi::FromInt(3), i_isolate())).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(3));
}

TEST_F(InterpreterTest, InterpreterParameter8) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));
  FeedbackVectorSpec feedback_spec(zone());
  BytecodeArrayBuilder builder(zone(), 8, 0, &feedback_spec);

  FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot2 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot3 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot4 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot5 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot6 = feedback_spec.AddBinaryOpICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  builder.LoadAccumulatorWithRegister(builder.Receiver())
      .BinaryOperation(Token::kAdd, builder.Parameter(0), GetIndex(slot))
      .BinaryOperation(Token::kAdd, builder.Parameter(1), GetIndex(slot1))
      .BinaryOperation(Token::kAdd, builder.Parameter(2), GetIndex(slot2))
      .BinaryOperation(Token::kAdd, builder.Parameter(3), GetIndex(slot3))
      .BinaryOperation(Token::kAdd, builder.Parameter(4), GetIndex(slot4))
      .BinaryOperation(Token::kAdd, builder.Parameter(5), GetIndex(slot5))
      .BinaryOperation(Token::kAdd, builder.Parameter(6), GetIndex(slot6))
      .Return();
  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array, metadata);
  using H = Handle<Object>;
  auto callable = tester.GetCallableWithReceiver<H, H, H, H, H, H, H>();

  Handle<Smi> arg1 = Handle<Smi>(Smi::FromInt(1), i_isolate());
  Handle<Smi> arg2 = Handle<Smi>(Smi::FromInt(2), i_isolate());
  Handle<Smi> arg3 = Handle<Smi>(Smi::FromInt(3), i_isolate());
  Handle<Smi> arg4 = Handle<Smi>(Smi::FromInt(4), i_isolate());
  Handle<Smi> arg5 = Handle<Smi>(Smi::FromInt(5), i_isolate());
  Handle<Smi> arg6 = Handle<Smi>(Smi::FromInt(6), i_isolate());
  Handle<Smi> arg7 = Handle<Smi>(Smi::FromInt(7), i_isolate());
  Handle<Smi> arg8 = Handle<Smi>(Smi::FromInt(8), i_isolate());
  // Check for Smis.
  DirectHandle<Object> return_val =
      callable(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)
          .ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(36));
}

TEST_F(InterpreterTest, InterpreterBinaryOpTypeFeedback) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  struct BinaryOpExpectation {
    Token::Value op;
    LiteralForTest arg1;
    LiteralForTest arg2;
    Handle<Object> result;
    int32_t feedback;
  };

  BinaryOpExpectation const kTestCases[] = {
      // ADD
      {Token::kAdd, LiteralForTest(2), LiteralForTest(3),
       Handle<Smi>(Smi::FromInt(5), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kAdd, LiteralForTest(Smi::kMaxValue), LiteralForTest(1),
       i_isolate()->factory()->NewHeapNumber(Smi::kMaxValue + 1.0),
       BinaryOperationFeedback::kNumber},
      {Token::kAdd, LiteralForTest(3.1415), LiteralForTest(3),
       i_isolate()->factory()->NewHeapNumber(3.1415 + 3),
       BinaryOperationFeedback::kNumber},
      {Token::kAdd, LiteralForTest(3.1415), LiteralForTest(1.4142),
       i_isolate()->factory()->NewHeapNumber(3.1415 + 1.4142),
       BinaryOperationFeedback::kNumber},
      {Token::kAdd, LiteralForTest(ast_factory.GetOneByteString("foo")),
       LiteralForTest(ast_factory.GetOneByteString("bar")),
       i_isolate()->factory()->NewStringFromAsciiChecked("foobar"),
       BinaryOperationFeedback::kString},
      {Token::kAdd, LiteralForTest(2),
       LiteralForTest(ast_factory.GetOneByteString("2")),
       i_isolate()->factory()->NewStringFromAsciiChecked("22"),
       BinaryOperationFeedback::kAny},
      // SUB
      {Token::kSub, LiteralForTest(2), LiteralForTest(3),
       Handle<Smi>(Smi::FromInt(-1), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kSub, LiteralForTest(Smi::kMinValue), LiteralForTest(1),
       i_isolate()->factory()->NewHeapNumber(Smi::kMinValue - 1.0),
       BinaryOperationFeedback::kNumber},
      {Token::kSub, LiteralForTest(3.1415), LiteralForTest(3),
       i_isolate()->factory()->NewHeapNumber(3.1415 - 3),
       BinaryOperationFeedback::kNumber},
      {Token::kSub, LiteralForTest(3.1415), LiteralForTest(1.4142),
       i_isolate()->factory()->NewHeapNumber(3.1415 - 1.4142),
       BinaryOperationFeedback::kNumber},
      {Token::kSub, LiteralForTest(2),
       LiteralForTest(ast_factory.GetOneByteString("1")),
       Handle<Smi>(Smi::FromInt(1), i_isolate()),
       BinaryOperationFeedback::kAny},
      // MUL
      {Token::kMul, LiteralForTest(2), LiteralForTest(3),
       Handle<Smi>(Smi::FromInt(6), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kMul, LiteralForTest(Smi::kMinValue), LiteralForTest(2),
       i_isolate()->factory()->NewHeapNumber(Smi::kMinValue * 2.0),
       BinaryOperationFeedback::kNumber},
      {Token::kMul, LiteralForTest(3.1415), LiteralForTest(3),
       i_isolate()->factory()->NewHeapNumber(3 * 3.1415),
       BinaryOperationFeedback::kNumber},
      {Token::kMul, LiteralForTest(3.1415), LiteralForTest(1.4142),
       i_isolate()->factory()->NewHeapNumber(3.1415 * 1.4142),
       BinaryOperationFeedback::kNumber},
      {Token::kMul, LiteralForTest(2),
       LiteralForTest(ast_factory.GetOneByteString("1")),
       Handle<Smi>(Smi::FromInt(2), i_isolate()),
       BinaryOperationFeedback::kAny},
      // DIV
      {Token::kDiv, LiteralForTest(6), LiteralForTest(3),
       Handle<Smi>(Smi::FromInt(2), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kDiv, LiteralForTest(3), LiteralForTest(2),
       i_isolate()->factory()->NewHeapNumber(3.0 / 2.0),
       BinaryOperationFeedback::kSignedSmallInputs},
      {Token::kDiv, LiteralForTest(3.1415), LiteralForTest(3),
       i_isolate()->factory()->NewHeapNumber(3.1415 / 3),
       BinaryOperationFeedback::kNumber},
      {Token::kDiv, LiteralForTest(3.1415),
       LiteralForTest(-std::numeric_limits<double>::infinity()),
       i_isolate()->factory()->NewHeapNumber(-0.0),
       BinaryOperationFeedback::kNumber},
      {Token::kDiv, LiteralForTest(2),
       LiteralForTest(ast_factory.GetOneByteString("1")),
       Handle<Smi>(Smi::FromInt(2), i_isolate()),
       BinaryOperationFeedback::kAny},
      // MOD
      {Token::kMod, LiteralForTest(5), LiteralForTest(3),
       Handle<Smi>(Smi::FromInt(2), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kMod, LiteralForTest(-4), LiteralForTest(2),
       i_isolate()->factory()->NewHeapNumber(-0.0),
       BinaryOperationFeedback::kNumber},
      {Token::kMod, LiteralForTest(3.1415), LiteralForTest(3),
       i_isolate()->factory()->NewHeapNumber(fmod(3.1415, 3.0)),
       BinaryOperationFeedback::kNumber},
      {Token::kMod, LiteralForTest(-3.1415), LiteralForTest(-1.4142),
       i_isolate()->factory()->NewHeapNumber(fmod(-3.1415, -1.4142)),
       BinaryOperationFeedback::kNumber},
      {Token::kMod, LiteralForTest(3),
       LiteralForTest(ast_factory.GetOneByteString("-2")),
       Handle<Smi>(Smi::FromInt(1), i_isolate()),
       BinaryOperationFeedback::kAny}};
  ast_factory.Internalize(i_isolate());

  for (const BinaryOpExpectation& test_case : kTestCases) {
    i::FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

    i::FeedbackSlot slot0 = feedback_spec.AddBinaryOpICSlot();

    Handle<i::FeedbackMetadata> metadata =
        i::FeedbackMetadata::New(i_isolate(), &feedback_spec);

    Register reg(0);
    LoadLiteralForTest(&builder, test_case.arg1);
    builder.StoreAccumulatorInRegister(reg);
    LoadLiteralForTest(&builder, test_case.arg2);
    builder.BinaryOperation(test_case.op, reg, GetIndex(slot0)).Return();

    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallable<>();

    Handle<Object> return_val = callable().ToHandleChecked();
    Tagged<MaybeObject> feedback0 = callable.vector()->Get(slot0);
    CHECK(IsSmi(feedback0));
    CHECK_EQ(test_case.feedback, feedback0.ToSmi().value());
    CHECK(
        Object::Equals(i_isolate(), test_case.result, return_val).ToChecked());
  }
}

TEST_F(InterpreterTest, InterpreterBinaryOpSmiTypeFeedback) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  struct BinaryOpExpectation {
    Token::Value op;
    LiteralForTest arg1;
    int32_t arg2;
    Handle<Object> result;
    int32_t feedback;
  };

  BinaryOpExpectation const kTestCases[] = {
      // ADD
      {Token::kAdd, LiteralForTest(2), 42,
       Handle<Smi>(Smi::FromInt(44), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kAdd, LiteralForTest(2), Smi::kMaxValue,
       i_isolate()->factory()->NewHeapNumber(Smi::kMaxValue + 2.0),
       BinaryOperationFeedback::kNumber},
      {Token::kAdd, LiteralForTest(3.1415), 2,
       i_isolate()->factory()->NewHeapNumber(3.1415 + 2.0),
       BinaryOperationFeedback::kNumber},
      {Token::kAdd, LiteralForTest(ast_factory.GetOneByteString("2")), 2,
       i_isolate()->factory()->NewStringFromAsciiChecked("22"),
       BinaryOperationFeedback::kAny},
      // SUB
      {Token::kSub, LiteralForTest(2), 42,
       Handle<Smi>(Smi::FromInt(-40), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kSub, LiteralForTest(Smi::kMinValue), 1,
       i_isolate()->factory()->NewHeapNumber(Smi::kMinValue - 1.0),
       BinaryOperationFeedback::kNumber},
      {Token::kSub, LiteralForTest(3.1415), 2,
       i_isolate()->factory()->NewHeapNumber(3.1415 - 2.0),
       BinaryOperationFeedback::kNumber},
      {Token::kSub, LiteralForTest(ast_factory.GetOneByteString("2")), 2,
       Handle<Smi>(Smi::zero(), i_isolate()), BinaryOperationFeedback::kAny},
      // BIT_OR
      {Token::kBitOr, LiteralForTest(4), 1,
       Handle<Smi>(Smi::FromInt(5), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kBitOr, LiteralForTest(3.1415), 8,
       Handle<Smi>(Smi::FromInt(11), i_isolate()),
       BinaryOperationFeedback::kNumber},
      {Token::kBitOr, LiteralForTest(ast_factory.GetOneByteString("2")), 1,
       Handle<Smi>(Smi::FromInt(3), i_isolate()),
       BinaryOperationFeedback::kAny},
      // BIT_AND
      {Token::kBitAnd, LiteralForTest(3), 1,
       Handle<Smi>(Smi::FromInt(1), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kBitAnd, LiteralForTest(3.1415), 2,
       Handle<Smi>(Smi::FromInt(2), i_isolate()),
       BinaryOperationFeedback::kNumber},
      {Token::kBitAnd, LiteralForTest(ast_factory.GetOneByteString("2")), 1,
       Handle<Smi>(Smi::zero(), i_isolate()), BinaryOperationFeedback::kAny},
      // SHL
      {Token::kShl, LiteralForTest(3), 1,
       Handle<Smi>(Smi::FromInt(6), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kShl, LiteralForTest(3.1415), 2,
       Handle<Smi>(Smi::FromInt(12), i_isolate()),
       BinaryOperationFeedback::kNumber},
      {Token::kShl, LiteralForTest(ast_factory.GetOneByteString("2")), 1,
       Handle<Smi>(Smi::FromInt(4), i_isolate()),
       BinaryOperationFeedback::kAny},
      // SAR
      {Token::kSar, LiteralForTest(3), 1,
       Handle<Smi>(Smi::FromInt(1), i_isolate()),
       BinaryOperationFeedback::kSignedSmall},
      {Token::kSar, LiteralForTest(3.1415), 2,
       Handle<Smi>(Smi::zero(), i_isolate()), BinaryOperationFeedback::kNumber},
      {Token::kSar, LiteralForTest(ast_factory.GetOneByteString("2")), 1,
       Handle<Smi>(Smi::FromInt(1), i_isolate()),
       BinaryOperationFeedback::kAny}};
  ast_factory.Internalize(i_isolate());

  for (const BinaryOpExpectation& test_case : kTestCases) {
    i::FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

    i::FeedbackSlot slot0 = feedback_spec.AddBinaryOpICSlot();

    Handle<i::FeedbackMetadata> metadata =
        i::FeedbackMetadata::New(i_isolate(), &feedback_spec);

    Register reg(0);
    LoadLiteralForTest(&builder, test_case.arg1);
    builder.StoreAccumulatorInRegister(reg)
        .LoadLiteral(Smi::FromInt(test_case.arg2))
        .BinaryOperation(test_case.op, reg, GetIndex(slot0))
        .Return();

    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallable<>();

    Handle<Object> return_val = callable().ToHandleChecked();
    Tagged<MaybeObject> feedback0 = callable.vector()->Get(slot0);
    CHECK(IsSmi(feedback0));
    CHECK_EQ(test_case.feedback, feedback0.ToSmi().value());
    CHECK(
        Object::Equals(i_isolate(), test_case.result, return_val).ToChecked());
  }
}

TEST_F(InterpreterTest, InterpreterUnaryOpFeedback) {
  Handle<Smi> smi_one = Handle<Smi>(Smi::FromInt(1), i_isolate());
  Handle<Smi> smi_max = Handle<Smi>(Smi::FromInt(Smi::kMaxValue), i_isolate());
  Handle<Smi> smi_min = Handle<Smi>(Smi::FromInt(Smi::kMinValue), i_isolate());
  Handle<HeapNumber> number = i_isolate()->factory()->NewHeapNumber(2.1);
  Handle<BigInt> bigint =
      BigInt::FromNumber(i_isolate(), smi_max).ToHandleChecked();
  Handle<String> str = i_isolate()->factory()->NewStringFromAsciiChecked("42");

  struct TestCase {
    Token::Value op;
    Handle<Smi> smi_feedback_value;
    Handle<Smi> smi_to_number_feedback_value;
    Handle<HeapNumber> number_feedback_value;
    Handle<BigInt> bigint_feedback_value;
    Handle<Object> any_feedback_value;
  };
  TestCase const kTestCases[] = {
      // Testing ADD and BIT_NOT would require generalizing the test setup.
      {Token::kSub, smi_one, smi_min, number, bigint, str},
      {Token::kInc, smi_one, smi_max, number, bigint, str},
      {Token::kDec, smi_one, smi_min, number, bigint, str}};
  for (TestCase const& test_case : kTestCases) {
    i::FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 6, 0, &feedback_spec);

    i::FeedbackSlot slot0 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot2 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot3 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot4 = feedback_spec.AddBinaryOpICSlot();

    Handle<i::FeedbackMetadata> metadata =
        i::FeedbackMetadata::New(i_isolate(), &feedback_spec);

    builder.LoadAccumulatorWithRegister(builder.Parameter(0))
        .UnaryOperation(test_case.op, GetIndex(slot0))
        .LoadAccumulatorWithRegister(builder.Parameter(1))
        .UnaryOperation(test_case.op, GetIndex(slot1))
        .LoadAccumulatorWithRegister(builder.Parameter(2))
        .UnaryOperation(test_case.op, GetIndex(slot2))
        .LoadAccumulatorWithRegister(builder.Parameter(3))
        .UnaryOperation(test_case.op, GetIndex(slot3))
        .LoadAccumulatorWithRegister(builder.Parameter(4))
        .UnaryOperation(test_case.op, GetIndex(slot4))
        .Return();

    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    using H = Handle<Object>;
    auto callable = tester.GetCallable<H, H, H, H, H>();

    Handle<Object> return_val =
        callable(test_case.smi_feedback_value,
                 test_case.smi_to_number_feedback_value,
                 test_case.number_feedback_value,
                 test_case.bigint_feedback_value, test_case.any_feedback_value)
            .ToHandleChecked();
    USE(return_val);
    Tagged<MaybeObject> feedback0 = callable.vector()->Get(slot0);
    CHECK(IsSmi(feedback0));
    CHECK_EQ(BinaryOperationFeedback::kSignedSmall, feedback0.ToSmi().value());

    Tagged<MaybeObject> feedback1 = callable.vector()->Get(slot1);
    CHECK(IsSmi(feedback1));
    CHECK_EQ(BinaryOperationFeedback::kNumber, feedback1.ToSmi().value());

    Tagged<MaybeObject> feedback2 = callable.vector()->Get(slot2);
    CHECK(IsSmi(feedback2));
    CHECK_EQ(BinaryOperationFeedback::kNumber, feedback2.ToSmi().value());

    Tagged<MaybeObject> feedback3 = callable.vector()->Get(slot3);
    CHECK(IsSmi(feedback3));
    CHECK_EQ(BinaryOperationFeedback::kBigInt, feedback3.ToSmi().value());

    Tagged<MaybeObject> feedback4 = callable.vector()->Get(slot4);
    CHECK(IsSmi(feedback4));
    CHECK_EQ(BinaryOperationFeedback::kAny, feedback4.ToSmi().value());
  }
}

TEST_F(InterpreterTest, InterpreterBitwiseTypeFeedback) {
  const Token::Value kBitwiseBinaryOperators[] = {
      Token::kBitOr, Token::kBitXor, Token::kBitAnd,
      Token::kShl,   Token::kShr,    Token::kSar};

  for (Token::Value op : kBitwiseBinaryOperators) {
    i::FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 5, 0, &feedback_spec);

    i::FeedbackSlot slot0 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
    i::FeedbackSlot slot2 = feedback_spec.AddBinaryOpICSlot();

    Handle<i::FeedbackMetadata> metadata =
        i::FeedbackMetadata::New(i_isolate(), &feedback_spec);

    builder.LoadAccumulatorWithRegister(builder.Parameter(0))
        .BinaryOperation(op, builder.Parameter(1), GetIndex(slot0))
        .BinaryOperation(op, builder.Parameter(2), GetIndex(slot1))
        .BinaryOperation(op, builder.Parameter(3), GetIndex(slot2))
        .Return();

    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    using H = Handle<Object>;
    auto callable = tester.GetCallable<H, H, H, H>();

    Handle<Smi> arg1 = Handle<Smi>(Smi::FromInt(2), i_isolate());
    Handle<Smi> arg2 = Handle<Smi>(Smi::FromInt(2), i_isolate());
    Handle<HeapNumber> arg3 = i_isolate()->factory()->NewHeapNumber(2.2);
    Handle<String> arg4 =
        i_isolate()->factory()->NewStringFromAsciiChecked("2");

    Handle<Object> return_val =
        callable(arg1, arg2, arg3, arg4).ToHandleChecked();
    USE(return_val);
    Tagged<MaybeObject> feedback0 = callable.vector()->Get(slot0);
    CHECK(IsSmi(feedback0));
    CHECK_EQ(BinaryOperationFeedback::kSignedSmall, feedback0.ToSmi().value());

    Tagged<MaybeObject> feedback1 = callable.vector()->Get(slot1);
    CHECK(IsSmi(feedback1));
    CHECK_EQ(BinaryOperationFeedback::kNumber, feedback1.ToSmi().value());

    Tagged<MaybeObject> feedback2 = callable.vector()->Get(slot2);
    CHECK(IsSmi(feedback2));
    CHECK_EQ(BinaryOperationFeedback::kAny, feedback2.ToSmi().value());
  }
}

TEST_F(InterpreterTest, InterpreterParameter1Assign) {
  BytecodeArrayBuilder builder(zone(), 1, 0);

  builder.LoadLiteral(Smi::FromInt(5))
      .StoreAccumulatorInRegister(builder.Receiver())
      .LoadAccumulatorWithRegister(builder.Receiver())
      .Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array);
  auto callable = tester.GetCallableWithReceiver<>();

  DirectHandle<Object> return_val =
      callable(Handle<Smi>(Smi::FromInt(3), i_isolate())).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(5));
}

TEST_F(InterpreterTest, InterpreterLoadGlobal) {
  // Test loading a global.
  std::string source(
      "var global = 321;\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  return global;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(321));
}

TEST_F(InterpreterTest, InterpreterStoreGlobal) {
  Factory* factory = i_isolate()->factory();

  // Test storing to a global.
  std::string source(
      "var global = 321;\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  global = 999;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  callable().ToHandleChecked();
  Handle<i::String> name = factory->InternalizeUtf8String("global");
  DirectHandle<i::Object> global_obj =
      Object::GetProperty(i_isolate(), i_isolate()->global_object(), name)
          .ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*global_obj), Smi::FromInt(999));
}

TEST_F(InterpreterTest, InterpreterCallGlobal) {
  // Test calling a global function.
  std::string source(
      "function g_add(a, b) { return a + b; }\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  return g_add(5, 10);\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(15));
}

TEST_F(InterpreterTest, InterpreterLoadUnallocated) {
  // Test loading an unallocated global.
  std::string source(
      "unallocated = 123;\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  return unallocated;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(123));
}

TEST_F(InterpreterTest, InterpreterStoreUnallocated) {
  Factory* factory = i_isolate()->factory();

  // Test storing to an unallocated global.
  std::string source(
      "unallocated = 321;\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  unallocated = 999;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  callable().ToHandleChecked();
  Handle<i::String> name = factory->InternalizeUtf8String("unallocated");
  DirectHandle<i::Object> global_obj =
      Object::GetProperty(i_isolate(), i_isolate()->global_object(), name)
          .ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*global_obj), Smi::FromInt(999));
}

TEST_F(InterpreterTest, InterpreterLoadNamedProperty) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  FeedbackVectorSpec feedback_spec(zone());
  FeedbackSlot slot = feedback_spec.AddLoadICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  const AstRawString* name = ast_factory.GetOneByteString("val");

  BytecodeArrayBuilder builder(zone(), 1, 0, &feedback_spec);

  builder.LoadNamedProperty(builder.Receiver(), name, GetIndex(slot)).Return();
  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array, metadata);
  auto callable = tester.GetCallableWithReceiver<>();

  Handle<Object> object = InterpreterTester::NewObject("({ val : 123 })");
  // Test IC miss.
  DirectHandle<Object> return_val = callable(object).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(123));

  // Test transition to monomorphic IC.
  return_val = callable(object).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(123));

  // Test transition to polymorphic IC.
  Handle<Object> object2 =
      InterpreterTester::NewObject("({ val : 456, other : 123 })");
  return_val = callable(object2).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(456));

  // Test transition to megamorphic IC.
  Handle<Object> object3 =
      InterpreterTester::NewObject("({ val : 789, val2 : 123 })");
  callable(object3).ToHandleChecked();
  Handle<Object> object4 =
      InterpreterTester::NewObject("({ val : 789, val3 : 123 })");
  callable(object4).ToHandleChecked();
  Handle<Object> object5 =
      InterpreterTester::NewObject("({ val : 789, val4 : 123 })");
  return_val = callable(object5).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(789));
}

TEST_F(InterpreterTest, InterpreterLoadKeyedProperty) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  FeedbackVectorSpec feedback_spec(zone());
  FeedbackSlot slot = feedback_spec.AddKeyedLoadICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  const AstRawString* key = ast_factory.GetOneByteString("key");

  BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

  builder.LoadLiteral(key)
      .LoadKeyedProperty(builder.Receiver(), GetIndex(slot))
      .Return();
  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array, metadata);
  auto callable = tester.GetCallableWithReceiver<>();

  Handle<Object> object = InterpreterTester::NewObject("({ key : 123 })");
  // Test IC miss.
  DirectHandle<Object> return_val = callable(object).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(123));

  // Test transition to monomorphic IC.
  return_val = callable(object).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(123));

  // Test transition to megamorphic IC.
  Handle<Object> object3 =
      InterpreterTester::NewObject("({ key : 789, val2 : 123 })");
  return_val = callable(object3).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(789));
}

TEST_F(InterpreterTest, InterpreterSetNamedProperty) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  FeedbackVectorSpec feedback_spec(zone());
  FeedbackSlot slot = feedback_spec.AddStoreICSlot(LanguageMode::kStrict);

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  const AstRawString* name = ast_factory.GetOneByteString("val");

  BytecodeArrayBuilder builder(zone(), 1, 0, &feedback_spec);

  builder.LoadLiteral(Smi::FromInt(999))
      .SetNamedProperty(builder.Receiver(), name, GetIndex(slot),
                        LanguageMode::kStrict)
      .Return();
  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array, metadata);
  auto callable = tester.GetCallableWithReceiver<>();
  Handle<Object> object = InterpreterTester::NewObject("({ val : 123 })");
  // Test IC miss.
  Handle<Object> result;
  callable(object).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));

  // Test transition to monomorphic IC.
  callable(object).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));

  // Test transition to polymorphic IC.
  Handle<Object> object2 =
      InterpreterTester::NewObject("({ val : 456, other : 123 })");
  callable(object2).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object2, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));

  // Test transition to megamorphic IC.
  Handle<Object> object3 =
      InterpreterTester::NewObject("({ val : 789, val2 : 123 })");
  callable(object3).ToHandleChecked();
  Handle<Object> object4 =
      InterpreterTester::NewObject("({ val : 789, val3 : 123 })");
  callable(object4).ToHandleChecked();
  Handle<Object> object5 =
      InterpreterTester::NewObject("({ val : 789, val4 : 123 })");
  callable(object5).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object5, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));
}

TEST_F(InterpreterTest, InterpreterSetKeyedProperty) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  FeedbackVectorSpec feedback_spec(zone());
  FeedbackSlot slot = feedback_spec.AddKeyedStoreICSlot(LanguageMode::kSloppy);

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  const AstRawString* name = ast_factory.GetOneByteString("val");

  BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

  builder.LoadLiteral(name)
      .StoreAccumulatorInRegister(Register(0))
      .LoadLiteral(Smi::FromInt(999))
      .SetKeyedProperty(builder.Receiver(), Register(0), GetIndex(slot),
                        i::LanguageMode::kSloppy)
      .Return();
  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  InterpreterTester tester(i_isolate(), bytecode_array, metadata);
  auto callable = tester.GetCallableWithReceiver<>();
  Handle<Object> object = InterpreterTester::NewObject("({ val : 123 })");
  // Test IC miss.
  Handle<Object> result;
  callable(object).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));

  // Test transition to monomorphic IC.
  callable(object).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));

  // Test transition to megamorphic IC.
  Handle<Object> object2 =
      InterpreterTester::NewObject("({ val : 456, other : 123 })");
  callable(object2).ToHandleChecked();
  CHECK(Runtime::GetObjectProperty(i_isolate(), object2, name->string())
            .ToHandle(&result));
  CHECK_EQ(Cast<Smi>(*result), Smi::FromInt(999));
}

TEST_F(InterpreterTest, InterpreterCall) {
  Factory* factory = i_isolate()->factory();

  FeedbackVectorSpec feedback_spec(zone());
  FeedbackSlot slot = feedback_spec.AddLoadICSlot();
  FeedbackSlot call_slot = feedback_spec.AddCallICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);
  int slot_index = GetIndex(slot);
  int call_slot_index = -1;
  call_slot_index = GetIndex(call_slot);

  // Check with no args.
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));
    const AstRawString* name = ast_factory.GetOneByteString("func");

    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);
    Register reg = builder.register_allocator()->NewRegister();
    RegisterList args = builder.register_allocator()->NewRegisterList(1);
    builder.LoadNamedProperty(builder.Receiver(), name, slot_index)
        .StoreAccumulatorInRegister(reg)
        .MoveRegister(builder.Receiver(), args[0]);

    builder.CallProperty(reg, args, call_slot_index);

    builder.Return();
    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallableWithReceiver<>();

    Handle<Object> object = InterpreterTester::NewObject(
        "new (function Obj() { this.func = function() { return 0x265; }})()");
    DirectHandle<Object> return_val = callable(object).ToHandleChecked();
    CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(0x265));
  }

  // Check that receiver is passed properly.
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));
    const AstRawString* name = ast_factory.GetOneByteString("func");

    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);
    Register reg = builder.register_allocator()->NewRegister();
    RegisterList args = builder.register_allocator()->NewRegisterList(1);
    builder.LoadNamedProperty(builder.Receiver(), name, slot_index)
        .StoreAccumulatorInRegister(reg)
        .MoveRegister(builder.Receiver(), args[0]);
    builder.CallProperty(reg, args, call_slot_index);
    builder.Return();
    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallableWithReceiver<>();

    Handle<Object> object = InterpreterTester::NewObject(
        "new (function Obj() {"
        "  this.val = 1234;"
        "  this.func = function() { return this.val; };"
        "})()");
    DirectHandle<Object> return_val = callable(object).ToHandleChecked();
    CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(1234));
  }

  // Check with two parameters (+ receiver).
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));
    const AstRawString* name = ast_factory.GetOneByteString("func");

    BytecodeArrayBuilder builder(zone(), 1, 4, &feedback_spec);
    Register reg = builder.register_allocator()->NewRegister();
    RegisterList args = builder.register_allocator()->NewRegisterList(3);

    builder.LoadNamedProperty(builder.Receiver(), name, slot_index)
        .StoreAccumulatorInRegister(reg)
        .LoadAccumulatorWithRegister(builder.Receiver())
        .StoreAccumulatorInRegister(args[0])
        .LoadLiteral(Smi::FromInt(51))
        .StoreAccumulatorInRegister(args[1])
        .LoadLiteral(Smi::FromInt(11))
        .StoreAccumulatorInRegister(args[2]);

    builder.CallProperty(reg, args, call_slot_index);

    builder.Return();

    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallableWithReceiver<>();

    Handle<Object> object = InterpreterTester::NewObject(
        "new (function Obj() { "
        "  this.func = function(a, b) { return a - b; }"
        "})()");
    DirectHandle<Object> return_val = callable(object).ToHandleChecked();
    CHECK(Object::SameValue(*return_val, Smi::FromInt(40)));
  }

  // Check with 10 parameters (+ receiver).
  {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));
    const AstRawString* name = ast_factory.GetOneByteString("func");

    BytecodeArrayBuilder builder(zone(), 1, 12, &feedback_spec);
    Register reg = builder.register_allocator()->NewRegister();
    RegisterList args = builder.register_allocator()->NewRegisterList(11);

    builder.LoadNamedProperty(builder.Receiver(), name, slot_index)
        .StoreAccumulatorInRegister(reg)
        .LoadAccumulatorWithRegister(builder.Receiver())
        .StoreAccumulatorInRegister(args[0])
        .LoadLiteral(ast_factory.GetOneByteString("a"))
        .StoreAccumulatorInRegister(args[1])
        .LoadLiteral(ast_factory.GetOneByteString("b"))
        .StoreAccumulatorInRegister(args[2])
        .LoadLiteral(ast_factory.GetOneByteString("c"))
        .StoreAccumulatorInRegister(args[3])
        .LoadLiteral(ast_factory.GetOneByteString("d"))
        .StoreAccumulatorInRegister(args[4])
        .LoadLiteral(ast_factory.GetOneByteString("e"))
        .StoreAccumulatorInRegister(args[5])
        .LoadLiteral(ast_factory.GetOneByteString("f"))
        .StoreAccumulatorInRegister(args[6])
        .LoadLiteral(ast_factory.GetOneByteString("g"))
        .StoreAccumulatorInRegister(args[7])
        .LoadLiteral(ast_factory.GetOneByteString("h"))
        .StoreAccumulatorInRegister(args[8])
        .LoadLiteral(ast_factory.GetOneByteString("i"))
        .StoreAccumulatorInRegister(args[9])
        .LoadLiteral(ast_factory.GetOneByteString("j"))
        .StoreAccumulatorInRegister(args[10]);

    builder.CallProperty(reg, args, call_slot_index);

    builder.Return();

    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

    InterpreterTester tester(i_isolate(), bytecode_array, metadata);
    auto callable = tester.GetCallableWithReceiver<>();

    Handle<Object> object = InterpreterTester::NewObject(
        "new (function Obj() { "
        "  this.prefix = \"prefix_\";"
        "  this.func = function(a, b, c, d, e, f, g, h, i, j) {"
        "      return this.prefix + a + b + c + d + e + f + g + h + i + j;"
        "  }"
        "})()");
    DirectHandle<Object> return_val = callable(object).ToHandleChecked();
    DirectHandle<i::String> expected =
        factory->NewStringFromAsciiChecked("prefix_abcdefghij");
    CHECK(i::Cast<i::String>(*return_val)->Equals(*expected));
  }
}

static BytecodeArrayBuilder& SetRegister(BytecodeArrayBuilder* builder,
                                         Register reg, int value,
                                         Register scratch) {
  return builder->StoreAccumulatorInRegister(scratch)
      .LoadLiteral(Smi::FromInt(value))
      .StoreAccumulatorInRegister(reg)
      .LoadAccumulatorWithRegister(scratch);
}

static BytecodeArrayBuilder& IncrementRegister(BytecodeArrayBuilder* builder,
                                               Register reg, int value,
                                               Register scratch,
                                               int slot_index) {
  return builder->StoreAccumulatorInRegister(scratch)
      .LoadLiteral(Smi::FromInt(value))
      .BinaryOperation(Token::kAdd, reg, slot_index)
      .StoreAccumulatorInRegister(reg)
      .LoadAccumulatorWithRegister(scratch);
}

TEST_F(InterpreterTest, InterpreterJumps) {
  FeedbackVectorSpec feedback_spec(zone());
  BytecodeArrayBuilder builder(zone(), 1, 2, &feedback_spec);

  FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot2 = feedback_spec.AddJumpLoopSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  Register reg(0), scratch(1);
  BytecodeLoopHeader loop_header;
  BytecodeLabel label[2];

  builder.LoadLiteral(Smi::zero())
      .StoreAccumulatorInRegister(reg)
      .Jump(&label[0]);
  SetRegister(&builder, reg, 1024, scratch).Bind(&label[0]).Bind(&loop_header);
  IncrementRegister(&builder, reg, 1, scratch, GetIndex(slot)).Jump(&label[1]);
  SetRegister(&builder, reg, 2048, scratch)
      .JumpLoop(&loop_header, 0, 0, slot2.ToInt());
  SetRegister(&builder, reg, 4096, scratch).Bind(&label[1]);
  IncrementRegister(&builder, reg, 2, scratch, GetIndex(slot1))
      .LoadAccumulatorWithRegister(reg)
      .Return();

  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
  DirectHandle<Object> return_value = RunBytecode(bytecode_array, metadata);
  CHECK_EQ(Smi::ToInt(*return_value), 3);
}

TEST_F(InterpreterTest, InterpreterConditionalJumps) {
  FeedbackVectorSpec feedback_spec(zone());
  BytecodeArrayBuilder builder(zone(), 1, 2, &feedback_spec);

  FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot2 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot3 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot4 = feedback_spec.AddBinaryOpICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  Register reg(0), scratch(1);
  BytecodeLabel label[2];
  BytecodeLabel done, done1;

  builder.LoadLiteral(Smi::zero())
      .StoreAccumulatorInRegister(reg)
      .LoadFalse()
      .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &label[0]);
  IncrementRegister(&builder, reg, 1024, scratch, GetIndex(slot))
      .Bind(&label[0])
      .LoadTrue()
      .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &done);
  IncrementRegister(&builder, reg, 1, scratch, GetIndex(slot1))
      .LoadTrue()
      .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &label[1]);
  IncrementRegister(&builder, reg, 2048, scratch, GetIndex(slot2))
      .Bind(&label[1]);
  IncrementRegister(&builder, reg, 2, scratch, GetIndex(slot3))
      .LoadFalse()
      .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &done1);
  IncrementRegister(&builder, reg, 4, scratch, GetIndex(slot4))
      .LoadAccumulatorWithRegister(reg)
      .Bind(&done)
      .Bind(&done1)
      .Return();

  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
  DirectHandle<Object> return_value = RunBytecode(bytecode_array, metadata);
  CHECK_EQ(Smi::ToInt(*return_value), 7);
}

TEST_F(InterpreterTest, InterpreterConditionalJumps2) {
  // TODO(oth): Add tests for all conditional jumps near and far.

  FeedbackVectorSpec feedback_spec(zone());
  BytecodeArrayBuilder builder(zone(), 1, 2, &feedback_spec);

  FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot1 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot2 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot3 = feedback_spec.AddBinaryOpICSlot();
  FeedbackSlot slot4 = feedback_spec.AddBinaryOpICSlot();

  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  Register reg(0), scratch(1);
  BytecodeLabel label[2];
  BytecodeLabel done, done1;

  builder.LoadLiteral(Smi::zero())
      .StoreAccumulatorInRegister(reg)
      .LoadFalse()
      .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &label[0]);
  IncrementRegister(&builder, reg, 1024, scratch, GetIndex(slot))
      .Bind(&label[0])
      .LoadTrue()
      .JumpIfFalse(ToBooleanMode::kAlreadyBoolean, &done);
  IncrementRegister(&builder, reg, 1, scratch, GetIndex(slot1))
      .LoadTrue()
      .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &label[1]);
  IncrementRegister(&builder, reg, 2048, scratch, GetIndex(slot2))
      .Bind(&label[1]);
  IncrementRegister(&builder, reg, 2, scratch, GetIndex(slot3))
      .LoadFalse()
      .JumpIfTrue(ToBooleanMode::kAlreadyBoolean, &done1);
  IncrementRegister(&builder, reg, 4, scratch, GetIndex(slot4))
      .LoadAccumulatorWithRegister(reg)
      .Bind(&done)
      .Bind(&done1)
      .Return();

  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
  DirectHandle<Object> return_value = RunBytecode(bytecode_array, metadata);
  CHECK_EQ(Smi::ToInt(*return_value), 7);
}

TEST_F(InterpreterTest, InterpreterJumpConstantWith16BitOperand) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));
  FeedbackVectorSpec feedback_spec(zone());
  BytecodeArrayBuilder builder(zone(), 1, 257, &feedback_spec);

  FeedbackSlot slot = feedback_spec.AddBinaryOpICSlot();
  Handle<i::FeedbackMetadata> metadata =
      FeedbackMetadata::New(i_isolate(), &feedback_spec);

  Register reg(0), scratch(256);
  BytecodeLabel done, fake;

  builder.LoadLiteral(Smi::zero());
  builder.StoreAccumulatorInRegister(reg);
  // Conditional jump to the fake label, to force both basic blocks to be live.
  builder.JumpIfTrue(ToBooleanMode::kConvertToBoolean, &fake);
  // Consume all 8-bit operands
  for (int i = 1; i <= 256; i++) {
    builder.LoadLiteral(i + 0.5);
    builder.BinaryOperation(Token::kAdd, reg, GetIndex(slot));
    builder.StoreAccumulatorInRegister(reg);
  }
  builder.Jump(&done);

  // Emit more than 16-bit immediate operands worth of code to jump over.
  builder.Bind(&fake);
  for (int i = 0; i < 6600; i++) {
    builder.LoadLiteral(Smi::zero());  // 1-byte
    builder.BinaryOperation(Token::kAdd, scratch,
                            GetIndex(slot));      // 6-bytes
    builder.StoreAccumulatorInRegister(scratch);  // 4-bytes
    builder.MoveRegister(scratch, reg);           // 6-bytes
  }
  builder.Bind(&done);
  builder.LoadAccumulatorWithRegister(reg);
  builder.Return();

  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
  {
    BytecodeArrayIterator iterator(bytecode_array);

    bool found_16bit_constant_jump = false;
    while (!iterator.done()) {
      if (iterator.current_bytecode() == Bytecode::kJumpConstant &&
          iterator.current_operand_scale() == OperandScale::kDouble) {
        found_16bit_constant_jump = true;
        break;
      }
      iterator.Advance();
    }
    CHECK(found_16bit_constant_jump);
  }

  Handle<Object> return_value = RunBytecode(bytecode_array, metadata);
  CHECK_EQ(Cast<HeapNumber>(return_value)->value(), 256.0 / 2 * (1.5 + 256.5));
}

TEST_F(InterpreterTest, InterpreterJumpWith32BitOperand) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));
  BytecodeArrayBuilder builder(zone(), 1, 1);
  Register reg(0);
  BytecodeLabel done;

  builder.LoadLiteral(Smi::zero());
  builder.StoreAccumulatorInRegister(reg);
  // Consume all 16-bit constant pool entries. Make sure to use doubles so that
  // the jump can't re-use an integer.
  for (int i = 1; i <= 65536; i++) {
    builder.LoadLiteral(i + 0.5);
  }
  builder.Jump(&done);
  builder.LoadLiteral(Smi::zero());
  builder.Bind(&done);
  builder.Return();

  ast_factory.Internalize(i_isolate());
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
  {
    BytecodeArrayIterator iterator(bytecode_array);

    bool found_32bit_jump = false;
    while (!iterator.done()) {
      if (iterator.current_bytecode() == Bytecode::kJump &&
          iterator.current_operand_scale() == OperandScale::kQuadruple) {
        found_32bit_jump = true;
        break;
      }
      iterator.Advance();
    }
    CHECK(found_32bit_jump);
  }

  Handle<Object> return_value = RunBytecode(bytecode_array);
  CHECK_EQ(Cast<HeapNumber>(return_value)->value(), 65536.5);
}

static const Token::Value kComparisonTypes[] = {
    Token::kEq,         Token::kEqStrict,    Token::kLessThan,
    Token::kLessThanEq, Token::kGreaterThan, Token::kGreaterThanEq};

template <typename T>
bool CompareC(Token::Value op, T lhs, T rhs, bool types_differed = false) {
  switch (op) {
    case Token::kEq:
      return lhs == rhs;
    case Token::kNotEq:
      return lhs != rhs;
    case Token::kEqStrict:
      return (lhs == rhs) && !types_differed;
    case Token::kNotEqStrict:
      return (lhs != rhs) || types_differed;
    case Token::kLessThan:
      return lhs < rhs;
    case Token::kLessThanEq:
      return lhs <= rhs;
    case Token::kGreaterThan:
      return lhs > rhs;
    case Token::kGreaterThanEq:
      return lhs >= rhs;
    default:
      UNREACHABLE();
  }
}

TEST_F(InterpreterTest, InterpreterSmiComparisons) {
  // NB Constants cover 31-bit space.
  int inputs[] = {v8::internal::kMinInt / 2,
                  v8::internal::kMinInt / 4,
                  -108733832,
                  -999,
                  -42,
                  -2,
                  -1,
                  0,
                  +1,
                  +2,
                  42,
                  12345678,
                  v8::internal::kMaxInt / 4,
                  v8::internal::kMaxInt / 2};

  for (size_t c = 0; c < arraysize(kComparisonTypes); c++) {
    Token::Value comparison = kComparisonTypes[c];
    for (size_t i = 0; i < arraysize(inputs); i++) {
      for (size_t j = 0; j < arraysize(inputs); j++) {
        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddCompareICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register r0(0);
        builder.LoadLiteral(Smi::FromInt(inputs[i]))
            .StoreAccumulatorInRegister(r0)
            .LoadLiteral(Smi::FromInt(inputs[j]))
            .CompareOperation(comparison, r0, GetIndex(slot))
            .Return();

        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());
        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        CHECK(IsBoolean(*return_value));
        CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
                 CompareC(comparison, inputs[i], inputs[j]));
        if (tester.HasFeedbackMetadata()) {
          Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
          CHECK(IsSmi(feedback));
          CHECK_EQ(CompareOperationFeedback::kSignedSmall,
                   feedback.ToSmi().value());
        }
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterHeapNumberComparisons) {
  double inputs[] = {std::numeric_limits<double>::min(),
                     std::numeric_limits<double>::max(),
                     -0.001,
                     0.01,
                     0.1000001,
                     1e99,
                     -1e-99};
  for (size_t c = 0; c < arraysize(kComparisonTypes); c++) {
    Token::Value comparison = kComparisonTypes[c];
    for (size_t i = 0; i < arraysize(inputs); i++) {
      for (size_t j = 0; j < arraysize(inputs); j++) {
        AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                    HashSeed(i_isolate()));

        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddCompareICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register r0(0);
        builder.LoadLiteral(inputs[i])
            .StoreAccumulatorInRegister(r0)
            .LoadLiteral(inputs[j])
            .CompareOperation(comparison, r0, GetIndex(slot))
            .Return();

        ast_factory.Internalize(i_isolate());
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());
        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        CHECK(IsBoolean(*return_value));
        CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
                 CompareC(comparison, inputs[i], inputs[j]));
        if (tester.HasFeedbackMetadata()) {
          Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
          CHECK(IsSmi(feedback));
          CHECK_EQ(CompareOperationFeedback::kNumber, feedback.ToSmi().value());
        }
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterBigIntComparisons) {
  // This test only checks that the recorded type feedback is kBigInt.
  AstBigInt inputs[] = {AstBigInt("0"), AstBigInt("-42"),
                        AstBigInt("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")};
  for (size_t c = 0; c < arraysize(kComparisonTypes); c++) {
    Token::Value comparison = kComparisonTypes[c];
    for (size_t i = 0; i < arraysize(inputs); i++) {
      for (size_t j = 0; j < arraysize(inputs); j++) {
        AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                    HashSeed(i_isolate()));

        FeedbackVectorSpec feedback_spec(zone());
        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

        FeedbackSlot slot = feedback_spec.AddCompareICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        Register r0(0);
        builder.LoadLiteral(inputs[i])
            .StoreAccumulatorInRegister(r0)
            .LoadLiteral(inputs[j])
            .CompareOperation(comparison, r0, GetIndex(slot))
            .Return();

        ast_factory.Internalize(i_isolate());
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());
        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        CHECK(IsBoolean(*return_value));
        if (tester.HasFeedbackMetadata()) {
          Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
          CHECK(IsSmi(feedback));
          // TODO(panq): Create a standalone unit test for kBigInt64.
          CHECK(CompareOperationFeedback::kBigInt64 ==
                    feedback.ToSmi().value() ||
                CompareOperationFeedback::kBigInt == feedback.ToSmi().value());
        }
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterStringComparisons) {
  std::string inputs[] = {"A", "abc", "z", "", "Foo!", "Foo"};

  for (size_t c = 0; c < arraysize(kComparisonTypes); c++) {
    Token::Value comparison = kComparisonTypes[c];
    for (size_t i = 0; i < arraysize(inputs); i++) {
      for (size_t j = 0; j < arraysize(inputs); j++) {
        AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                    HashSeed(i_isolate()));

        const char* lhs = inputs[i].c_str();
        const char* rhs = inputs[j].c_str();

        FeedbackVectorSpec feedback_spec(zone());
        FeedbackSlot slot = feedback_spec.AddCompareICSlot();
        Handle<i::FeedbackMetadata> metadata =
            FeedbackMetadata::New(i_isolate(), &feedback_spec);

        BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);
        Register r0(0);
        builder.LoadLiteral(ast_factory.GetOneByteString(lhs))
            .StoreAccumulatorInRegister(r0)
            .LoadLiteral(ast_factory.GetOneByteString(rhs))
            .CompareOperation(comparison, r0, GetIndex(slot))
            .Return();

        ast_factory.Internalize(i_isolate());
        Handle<BytecodeArray> bytecode_array =
            builder.ToBytecodeArray(i_isolate());
        InterpreterTester tester(i_isolate(), bytecode_array, metadata);
        auto callable = tester.GetCallable<>();
        DirectHandle<Object> return_value = callable().ToHandleChecked();
        CHECK(IsBoolean(*return_value));
        CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
                 CompareC(comparison, inputs[i], inputs[j]));
        if (tester.HasFeedbackMetadata()) {
          Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
          CHECK(IsSmi(feedback));
          int const expected_feedback =
              Token::IsOrderedRelationalCompareOp(comparison)
                  ? CompareOperationFeedback::kString
                  : CompareOperationFeedback::kInternalizedString;
          CHECK_EQ(expected_feedback, feedback.ToSmi().value());
        }
      }
    }
  }
}

static void LoadStringAndAddSpace(BytecodeArrayBuilder* builder,
                                  AstValueFactory* ast_factory,
                                  const char* cstr,
                                  FeedbackSlot string_add_slot) {
  Register string_reg = builder->register_allocator()->NewRegister();

  (*builder)
      .LoadLiteral(ast_factory->GetOneByteString(cstr))
      .StoreAccumulatorInRegister(string_reg)
      .LoadLiteral(ast_factory->GetOneByteString(" "))
      .BinaryOperation(Token::kAdd, string_reg, GetIndex(string_add_slot));
}

TEST_F(InterpreterTest, InterpreterMixedComparisons) {
  // This test compares a HeapNumber with a String. The latter is
  // convertible to a HeapNumber so comparison will be between numeric
  // values except for the strict comparisons where no conversion is
  // performed.
  const char* inputs[] = {"-1.77", "-40.333", "0.01", "55.77e50", "2.01"};

  enum WhichSideString { kLhsIsString, kRhsIsString };

  enum StringType { kInternalizedStringConstant, kComputedString };

  for (size_t c = 0; c < arraysize(kComparisonTypes); c++) {
    Token::Value comparison = kComparisonTypes[c];
    for (size_t i = 0; i < arraysize(inputs); i++) {
      for (size_t j = 0; j < arraysize(inputs); j++) {
        // We test the case where either the lhs or the rhs is a string...
        for (WhichSideString which_side : {kLhsIsString, kRhsIsString}) {
          // ... and the case when the string is internalized or computed.
          for (StringType string_type :
               {kInternalizedStringConstant, kComputedString}) {
            const char* lhs_cstr = inputs[i];
            const char* rhs_cstr = inputs[j];
            double lhs = StringToDouble(lhs_cstr, NO_CONVERSION_FLAG);
            double rhs = StringToDouble(rhs_cstr, NO_CONVERSION_FLAG);

            AstValueFactory ast_factory(zone(),
                                        i_isolate()->ast_string_constants(),
                                        HashSeed(i_isolate()));
            FeedbackVectorSpec feedback_spec(zone());
            BytecodeArrayBuilder builder(zone(), 1, 0, &feedback_spec);

            FeedbackSlot string_add_slot = feedback_spec.AddBinaryOpICSlot();
            FeedbackSlot slot = feedback_spec.AddCompareICSlot();
            Handle<i::FeedbackMetadata> metadata =
                FeedbackMetadata::New(i_isolate(), &feedback_spec);

            // lhs is in a register, rhs is in the accumulator.
            Register lhs_reg = builder.register_allocator()->NewRegister();

            if (which_side == kRhsIsString) {
              // Comparison with HeapNumber on the lhs and String on the rhs.

              builder.LoadLiteral(lhs).StoreAccumulatorInRegister(lhs_reg);

              if (string_type == kInternalizedStringConstant) {
                // rhs string is internalized.
                builder.LoadLiteral(ast_factory.GetOneByteString(rhs_cstr));
              } else {
                CHECK_EQ(string_type, kComputedString);
                // rhs string is not internalized (append a space to the end).
                LoadStringAndAddSpace(&builder, &ast_factory, rhs_cstr,
                                      string_add_slot);
              }
            } else {
              CHECK_EQ(which_side, kLhsIsString);
              // Comparison with String on the lhs and HeapNumber on the rhs.

              if (string_type == kInternalizedStringConstant) {
                // lhs string is internalized
                builder.LoadLiteral(ast_factory.GetOneByteString(lhs_cstr));
              } else {
                CHECK_EQ(string_type, kComputedString);
                // lhs string is not internalized (append a space to the end).
                LoadStringAndAddSpace(&builder, &ast_factory, lhs_cstr,
                                      string_add_slot);
              }
              builder.StoreAccumulatorInRegister(lhs_reg);

              builder.LoadLiteral(rhs);
            }

            builder.CompareOperation(comparison, lhs_reg, GetIndex(slot))
                .Return();

            ast_factory.Internalize(i_isolate());
            Handle<BytecodeArray> bytecode_array =
                builder.ToBytecodeArray(i_isolate());
            InterpreterTester tester(i_isolate(), bytecode_array, metadata);
            auto callable = tester.GetCallable<>();
            DirectHandle<Object> return_value = callable().ToHandleChecked();
            CHECK(IsBoolean(*return_value));
            CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
                     CompareC(comparison, lhs, rhs, true));
            if (tester.HasFeedbackMetadata()) {
              Tagged<MaybeObject> feedback = callable.vector()->Get(slot);
              CHECK(IsSmi(feedback));
              if (kComparisonTypes[c] == Token::kEq) {
                // For sloppy equality, we have more precise feedback.
                CHECK_EQ(
                    CompareOperationFeedback::kNumber |
                        (string_type == kInternalizedStringConstant
                             ? CompareOperationFeedback::kInternalizedString
                             : CompareOperationFeedback::kString),
                    feedback.ToSmi().value());
              } else {
                // Comparison with a number and string collects kAny feedback.
                CHECK_EQ(CompareOperationFeedback::kAny,
                         feedback.ToSmi().value());
              }
            }
          }
        }
      }
    }
  }
}

TEST_F(InterpreterTest, InterpreterStrictNotEqual) {
  Factory* factory = i_isolate()->factory();
  const char* code_snippet =
      "function f(lhs, rhs) {\n"
      "  return lhs !== rhs;\n"
      "}\n"
      "f(0, 0);\n";
  InterpreterTester tester(i_isolate(), code_snippet);
  auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();

  // Test passing different types.
  const char* inputs[] = {"-1.77", "-40.333", "0.01", "55.77e5", "2.01"};
  for (size_t i = 0; i < arraysize(inputs); i++) {
    for (size_t j = 0; j < arraysize(inputs); j++) {
      double lhs = StringToDouble(inputs[i], NO_CONVERSION_FLAG);
      double rhs = StringToDouble(inputs[j], NO_CONVERSION_FLAG);
      Handle<Object> lhs_obj = factory->NewNumber(lhs);
      Handle<Object> rhs_obj = factory->NewStringFromAsciiChecked(inputs[j]);

      DirectHandle<Object> return_value =
          callable(lhs_obj, rhs_obj).ToHandleChecked();
      CHECK(IsBoolean(*return_value));
      CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
               CompareC(Token::kNotEqStrict, lhs, rhs, true));
    }
  }

  // Test passing string types.
  const char* inputs_str[] = {"A", "abc", "z", "", "Foo!", "Foo"};
  for (size_t i = 0; i < arraysize(inputs_str); i++) {
    for (size_t j = 0; j < arraysize(inputs_str); j++) {
      Handle<Object> lhs_obj =
          factory->NewStringFromAsciiChecked(inputs_str[i]);
      Handle<Object> rhs_obj =
          factory->NewStringFromAsciiChecked(inputs_str[j]);

      DirectHandle<Object> return_value =
          callable(lhs_obj, rhs_obj).ToHandleChecked();
      CHECK(IsBoolean(*return_value));
      CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
               CompareC(Token::kNotEqStrict, inputs_str[i], inputs_str[j]));
    }
  }

  // Test passing doubles.
  double inputs_number[] = {std::numeric_limits<double>::min(),
                            std::numeric_limits<double>::max(),
                            -0.001,
                            0.01,
                            0.1000001,
                            1e99,
                            -1e-99};
  for (size_t i = 0; i < arraysize(inputs_number); i++) {
    for (size_t j = 0; j < arraysize(inputs_number); j++) {
      Handle<Object> lhs_obj = factory->NewNumber(inputs_number[i]);
      Handle<Object> rhs_obj = factory->NewNumber(inputs_number[j]);

      DirectHandle<Object> return_value =
          callable(lhs_obj, rhs_obj).ToHandleChecked();
      CHECK(IsBoolean(*return_value));
      CHECK_EQ(
          Object::BooleanValue(*return_value, i_isolate()),
          CompareC(Token::kNotEqStrict, inputs_number[i], inputs_number[j]));
    }
  }
}

TEST_F(InterpreterTest, InterpreterCompareTypeOf) {
  using LiteralFlag = TestTypeOfFlags::LiteralFlag;

  Factory* factory = i_isolate()->factory();

  std::pair<Handle<Object>, LiteralFlag> inputs[] = {
      {handle(Smi::FromInt(24), i_isolate()), LiteralFlag::kNumber},
      {factory->NewNumber(2.5), LiteralFlag::kNumber},
      {factory->NewStringFromAsciiChecked("foo"), LiteralFlag::kString},
      {factory
           ->NewConsString(factory->NewStringFromAsciiChecked("foo"),
                           factory->NewStringFromAsciiChecked("bar"))
           .ToHandleChecked(),
       LiteralFlag::kString},
      {factory->prototype_string(), LiteralFlag::kString},
      {factory->NewSymbol(), LiteralFlag::kSymbol},
      {factory->true_value(), LiteralFlag::kBoolean},
      {factory->false_value(), LiteralFlag::kBoolean},
      {factory->undefined_value(), LiteralFlag::kUndefined},
      {InterpreterTester::NewObject(
           "(function() { return function() {}; })();"),
       LiteralFlag::kFunction},
      {InterpreterTester::NewObject("new Object();"), LiteralFlag::kObject},
      {factory->null_value(), LiteralFlag::kObject},
  };
  const LiteralFlag kLiterals[] = {
#define LITERAL_FLAG(name, _) LiteralFlag::k##name,
      TYPEOF_LITERAL_LIST(LITERAL_FLAG)
#undef LITERAL_FLAG
  };

  for (size_t l = 0; l < arraysize(kLiterals); l++) {
    LiteralFlag literal_flag = kLiterals[l];
    if (literal_flag == LiteralFlag::kOther) continue;

    BytecodeArrayBuilder builder(zone(), 2, 0);
    builder.LoadAccumulatorWithRegister(builder.Parameter(0))
        .CompareTypeOf(kLiterals[l])
        .Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
    InterpreterTester tester(i_isolate(), bytecode_array);
    auto callable = tester.GetCallable<Handle<Object>>();

    for (size_t i = 0; i < arraysize(inputs); i++) {
      DirectHandle<Object> return_value =
          callable(inputs[i].first).ToHandleChecked();
      CHECK(IsBoolean(*return_value));
      CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
               inputs[i].second == literal_flag);
    }
  }
}

TEST_F(InterpreterTest, InterpreterInstanceOf) {
  Factory* factory = i_isolate()->factory();
  DirectHandle<i::String> name = factory->NewStringFromAsciiChecked("cons");
  Handle<i::JSFunction> func = factory->NewFunctionForTesting(name);
  Handle<i::JSObject> instance = factory->NewJSObject(func);
  Handle<i::Object> other = factory->NewNumber(3.3333);
  Handle<i::Object> cases[] = {Cast<i::Object>(instance), other};
  for (size_t i = 0; i < arraysize(cases); i++) {
    bool expected_value = (i == 0);
    FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

    Register r0(0);
    size_t case_entry = builder.AllocateDeferredConstantPoolEntry();
    builder.SetDeferredConstantPoolEntry(case_entry, cases[i]);
    builder.LoadConstantPoolEntry(case_entry).StoreAccumulatorInRegister(r0);

    FeedbackSlot slot = feedback_spec.AddInstanceOfSlot();
    Handle<i::FeedbackMetadata> metadata =
        FeedbackMetadata::New(i_isolate(), &feedback_spec);

    size_t func_entry = builder.AllocateDeferredConstantPoolEntry();
    builder.SetDeferredConstantPoolEntry(func_entry, func);
    builder.LoadConstantPoolEntry(func_entry)
        .CompareOperation(Token::kInstanceOf, r0, GetIndex(slot))
        .Return();

    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
    DirectHandle<Object> return_value = RunBytecode(bytecode_array, metadata);
    CHECK(IsBoolean(*return_value));
    CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()), expected_value);
  }
}

TEST_F(InterpreterTest, InterpreterTestIn) {
  Factory* factory = i_isolate()->factory();
  // Allocate an array
  Handle<i::JSArray> array =
      factory->NewJSArray(0, i::ElementsKind::PACKED_SMI_ELEMENTS);
  // Check for these properties on the array object
  const char* properties[] = {"length", "fuzzle", "x", "0"};
  for (size_t i = 0; i < arraysize(properties); i++) {
    AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                                HashSeed(i_isolate()));

    bool expected_value = (i == 0);
    FeedbackVectorSpec feedback_spec(zone());
    BytecodeArrayBuilder builder(zone(), 1, 1, &feedback_spec);

    Register r0(0);
    builder.LoadLiteral(ast_factory.GetOneByteString(properties[i]))
        .StoreAccumulatorInRegister(r0);

    FeedbackSlot slot = feedback_spec.AddKeyedHasICSlot();
    Handle<i::FeedbackMetadata> metadata =
        FeedbackMetadata::New(i_isolate(), &feedback_spec);

    size_t array_entry = builder.AllocateDeferredConstantPoolEntry();
    builder.SetDeferredConstantPoolEntry(array_entry, array);
    builder.LoadConstantPoolEntry(array_entry)
        .CompareOperation(Token::kIn, r0, GetIndex(slot))
        .Return();

    ast_factory.Internalize(i_isolate());
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
    DirectHandle<Object> return_value = RunBytecode(bytecode_array, metadata);
    CHECK(IsBoolean(*return_value));
    CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()), expected_value);
  }
}

TEST_F(InterpreterTest, InterpreterUnaryNot) {
  for (size_t i = 1; i < 10; i++) {
    bool expected_value = ((i & 1) == 1);
    BytecodeArrayBuilder builder(zone(), 1, 0);

    builder.LoadFalse();
    for (size_t j = 0; j < i; j++) {
      builder.LogicalNot(ToBooleanMode::kAlreadyBoolean);
    }
    builder.Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
    DirectHandle<Object> return_value = RunBytecode(bytecode_array);
    CHECK(IsBoolean(*return_value));
    CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()), expected_value);
  }
}

TEST_F(InterpreterTest, InterpreterUnaryNotNonBoolean) {
  AstValueFactory ast_factory(zone(), i_isolate()->ast_string_constants(),
                              HashSeed(i_isolate()));

  std::pair<LiteralForTest, bool> object_type_tuples[] = {
      std::make_pair(LiteralForTest(LiteralForTest::kUndefined), true),
      std::make_pair(LiteralForTest(LiteralForTest::kNull), true),
      std::make_pair(LiteralForTest(LiteralForTest::kFalse), true),
      std::make_pair(LiteralForTest(LiteralForTest::kTrue), false),
      std::make_pair(LiteralForTest(9.1), false),
      std::make_pair(LiteralForTest(0), true),
      std::make_pair(LiteralForTest(ast_factory.GetOneByteString("hello")),
                     false),
      std::make_pair(LiteralForTest(ast_factory.GetOneByteString("")), true),
  };
  ast_factory.Internalize(i_isolate());

  for (size_t i = 0; i < arraysize(object_type_tuples); i++) {
    BytecodeArrayBuilder builder(zone(), 1, 0);

    LoadLiteralForTest(&builder, object_type_tuples[i].first);
    builder.LogicalNot(ToBooleanMode::kConvertToBoolean).Return();
    Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());
    DirectHandle<Object> return_value = RunBytecode(bytecode_array);
    CHECK(IsBoolean(*return_value));
    CHECK_EQ(Object::BooleanValue(*return_value, i_isolate()),
             object_type_tuples[i].second);
  }
}

TEST_F(InterpreterTest, InterpreterTypeof) {
  std::pair<const char*, const char*> typeof_vals[] = {
      std::make_pair("return typeof undefined;", "undefined"),
      std::make_pair("return typeof null;", "object"),
      std::make_pair("return typeof true;", "boolean"),
      std::make_pair("return typeof false;", "boolean"),
      std::make_pair("return typeof 9.1;", "number"),
      std::make_pair("return typeof 7771;", "number"),
      std::make_pair("return typeof 'hello';", "string"),
      std::make_pair("return typeof global_unallocated;", "undefined"),
  };

  for (size_t i = 0; i < arraysize(typeof_vals); i++) {
    std::string source(InterpreterTester::SourceForBody(typeof_vals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());

    auto callable = tester.GetCallable<>();
    DirectHandle<v8::internal::String> return_value =
        Cast<v8::internal::String>(callable().ToHandleChecked());
    auto actual = return_value->ToCString();
    CHECK_EQ(strcmp(&actual[0], typeof_vals[i].second), 0);
  }
}

TEST_F(InterpreterTest, InterpreterCallRuntime) {
  BytecodeArrayBuilder builder(zone(), 1, 2);
  RegisterList args = builder.register_allocator()->NewRegisterList(2);

  builder.LoadLiteral(Smi::FromInt(15))
      .StoreAccumulatorInRegister(args[0])
      .LoadLiteral(Smi::FromInt(40))
      .StoreAccumulatorInRegister(args[1])
      .CallRuntime(Runtime::kAdd, args)
      .Return();
  Handle<BytecodeArray> bytecode_array = builder.ToBytecodeArray(i_isolate());

  DirectHandle<Object> return_val = RunBytecode(bytecode_array);
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(55));
}

TEST_F(InterpreterTest, InterpreterFunctionLiteral) {
  // Test calling a function literal.
  std::string source("function " + InterpreterTester::function_name() +
                     "(a) {\n"
                     "  return (function(x){ return x + 2; })(a);\n"
                     "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<Handle<Object>>();

  DirectHandle<i::Object> return_val =
      callable(Handle<Smi>(Smi::FromInt(3), i_isolate())).ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(5));
}

TEST_F(InterpreterTest, InterpreterRegExpLiterals) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("return /abd/.exec('cccabbdd');\n", factory->null_value()),
      std::make_pair("return /ab+d/.exec('cccabbdd')[0];\n",
                     factory->NewStringFromStaticChars("abbd")),
      std::make_pair("return /AbC/i.exec('ssaBC')[0];\n",
                     factory->NewStringFromStaticChars("aBC")),
      std::make_pair("return 'ssaBC'.match(/AbC/i)[0];\n",
                     factory->NewStringFromStaticChars("aBC")),
      std::make_pair("return 'ssaBCtAbC'.match(/(AbC)/gi)[1];\n",
                     factory->NewStringFromStaticChars("AbC")),
  };

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterArrayLiterals) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("return [][0];\n", factory->undefined_value()),
      std::make_pair("return [1, 3, 2][1];\n",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("return ['a', 'b', 'c'][2];\n",
                     factory->NewStringFromStaticChars("c")),
      std::make_pair("var a = 100; return [a, a + 1, a + 2, a + 3][2];\n",
                     handle(Smi::FromInt(102), i_isolate())),
      std::make_pair("return [[1, 2, 3], ['a', 'b', 'c']][1][0];\n",
                     factory->NewStringFromStaticChars("a")),
      std::make_pair("var t = 't'; return [[t, t + 'est'], [1 + t]][0][1];\n",
                     factory->NewStringFromStaticChars("test"))};

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterObjectLiterals) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("return { }.name;", factory->undefined_value()),
      std::make_pair("return { name: 'string', val: 9.2 }.name;",
                     factory->NewStringFromStaticChars("string")),
      std::make_pair("var a = 15; return { name: 'string', val: a }.val;",
                     handle(Smi::FromInt(15), i_isolate())),
      std::make_pair("var a = 5; return { val: a, val: a + 1 }.val;",
                     handle(Smi::FromInt(6), i_isolate())),
      std::make_pair("return { func: function() { return 'test' } }.func();",
                     factory->NewStringFromStaticChars("test")),
      std::make_pair("return { func(a) { return a + 'st'; } }.func('te');",
                     factory->NewStringFromStaticChars("test")),
      std::make_pair("return { get a() { return 22; } }.a;",
                     handle(Smi::FromInt(22), i_isolate())),
      std::make_pair("var a = { get b() { return this.x + 't'; },\n"
                     "          set b(val) { this.x = val + 's' } };\n"
                     "a.b = 'te';\n"
                     "return a.b;",
                     factory->NewStringFromStaticChars("test")),
      std::make_pair("var a = 123; return { 1: a }[1];",
                     handle(Smi::FromInt(123), i_isolate())),
      std::make_pair("return Object.getPrototypeOf({ __proto__: null });",
                     factory->null_value()),
      std::make_pair("var a = 'test'; return { [a]: 1 }.test;",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var a = 'test'; return { b: a, [a]: a + 'ing' }['test']",
                     factory->NewStringFromStaticChars("testing")),
      std::make_pair("var a = 'proto_str';\n"
                     "var b = { [a]: 1, __proto__: { var : a } };\n"
                     "return Object.getPrototypeOf(b).var",
                     factory->NewStringFromStaticChars("proto_str")),
      std::make_pair("var n = 'name';\n"
                     "return { [n]: 'val', get a() { return 987 } }['a'];",
                     handle(Smi::FromInt(987), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterConstruct) {
  std::string source(
      "function counter() { this.count = 0; }\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  var c = new counter();\n"
      "  return c.count;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::zero());
}

TEST_F(InterpreterTest, InterpreterConstructWithArgument) {
  std::string source(
      "function counter(arg0) { this.count = 17; this.x = arg0; }\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  var c = new counter(3);\n"
      "  return c.x;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(3));
}

TEST_F(InterpreterTest, InterpreterConstructWithArguments) {
  std::string source(
      "function counter(arg0, arg1) {\n"
      "  this.count = 7; this.x = arg0; this.y = arg1;\n"
      "}\n"
      "function " +
      InterpreterTester::function_name() +
      "() {\n"
      "  var c = new counter(3, 5);\n"
      "  return c.count + c.x + c.y;\n"
      "}");
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<>();

  DirectHandle<Object> return_val = callable().ToHandleChecked();
  CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(15));
}

TEST_F(InterpreterTest, InterpreterContextVariables) {
  std::ostringstream unique_vars;
  for (int i = 0; i < 250; i++) {
    unique_vars << "var a" << i << " = 0;";
  }
  std::pair<std::string, Handle<Object>> context_vars[] = {
      std::make_pair("var a; (function() { a = 1; })(); return a;",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var a = 10; (function() { a; })(); return a;",
                     handle(Smi::FromInt(10), i_isolate())),
      std::make_pair("var a = 20; var b = 30;\n"
                     "return (function() { return a + b; })();",
                     handle(Smi::FromInt(50), i_isolate())),
      std::make_pair("'use strict'; let a = 1;\n"
                     "{ let b = 2; return (function() { return a + b; })(); }",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("'use strict'; let a = 10;\n"
                     "{ let b = 20; var c = function() { [a, b] };\n"
                     "  return a + b; }",
                     handle(Smi::FromInt(30), i_isolate())),
      std::make_pair("'use strict';" + unique_vars.str() +
                         "eval(); var b = 100; return b;",
                     handle(Smi::FromInt(100), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(context_vars); i++) {
    std::string source(
        InterpreterTester::SourceForBody(context_vars[i].first.c_str()));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *context_vars[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterContextParameters) {
  std::pair<const char*, Handle<Object>> context_params[] = {
      std::make_pair("return (function() { return arg1; })();",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("(function() { arg1 = 4; })(); return arg1;",
                     handle(Smi::FromInt(4), i_isolate())),
      std::make_pair("(function() { arg3 = arg2 - arg1; })(); return arg3;",
                     handle(Smi::FromInt(1), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(context_params); i++) {
    std::string source = "function " + InterpreterTester::function_name() +
                         "(arg1, arg2, arg3) {" + context_params[i].first + "}";
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable =
        tester.GetCallable<Handle<Object>, Handle<Object>, Handle<Object>>();

    Handle<Object> a1 = handle(Smi::FromInt(1), i_isolate());
    Handle<Object> a2 = handle(Smi::FromInt(2), i_isolate());
    Handle<Object> a3 = handle(Smi::FromInt(3), i_isolate());
    DirectHandle<i::Object> return_value =
        callable(a1, a2, a3).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *context_params[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterOuterContextVariables) {
  std::pair<const char*, Handle<Object>> context_vars[] = {
      std::make_pair("return outerVar * innerArg;",
                     handle(Smi::FromInt(200), i_isolate())),
      std::make_pair("outerVar = innerArg; return outerVar",
                     handle(Smi::FromInt(20), i_isolate())),
  };

  std::string header(
      "function Outer() {"
      "  var outerVar = 10;"
      "  function Inner(innerArg) {"
      "    this.innerFunc = function() { ");
  std::string footer(
      "  }}"
      "  this.getInnerFunc = function() { return new Inner(20).innerFunc; }"
      "}"
      "var f = new Outer().getInnerFunc();");

  for (size_t i = 0; i < arraysize(context_vars); i++) {
    std::string source = header + context_vars[i].first + footer;
    InterpreterTester tester(i_isolate(), source.c_str(), "*");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *context_vars[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterComma) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("var a; return 0, a;\n", factory->undefined_value()),
      std::make_pair("return 'a', 2.2, 3;\n",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("return 'a', 'b', 'c';\n",
                     factory->NewStringFromStaticChars("c")),
      std::make_pair("return 3.2, 2.3, 4.5;\n", factory->NewNumber(4.5)),
      std::make_pair("var a = 10; return b = a, b = b+1;\n",
                     handle(Smi::FromInt(11), i_isolate())),
      std::make_pair("var a = 10; return b = a, b = b+1, b + 10;\n",
                     handle(Smi::FromInt(21), i_isolate()))};

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterLogicalOr) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("var a, b; return a || b;\n", factory->undefined_value()),
      std::make_pair("var a, b = 10; return a || b;\n",
                     handle(Smi::FromInt(10), i_isolate())),
      std::make_pair("var a = '0', b = 10; return a || b;\n",
                     factory->NewStringFromStaticChars("0")),
      std::make_pair("return 0 || 3.2;\n", factory->NewNumber(3.2)),
      std::make_pair("return 'a' || 0;\n",
                     factory->NewStringFromStaticChars("a")),
      std::make_pair("var a = '0', b = 10; return (a == 0) || b;\n",
                     factory->true_value())};

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterLogicalAnd) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> literals[] = {
      std::make_pair("var a, b = 10; return a && b;\n",
                     factory->undefined_value()),
      std::make_pair("var a = 0, b = 10; return a && b / a;\n",
                     handle(Smi::zero(), i_isolate())),
      std::make_pair("var a = '0', b = 10; return a && b;\n",
                     handle(Smi::FromInt(10), i_isolate())),
      std::make_pair("return 0.0 && 3.2;\n", handle(Smi::zero(), i_isolate())),
      std::make_pair("return 'a' && 'b';\n",
                     factory->NewStringFromStaticChars("b")),
      std::make_pair("return 'a' && 0 || 'b', 'c';\n",
                     factory->NewStringFromStaticChars("c")),
      std::make_pair("var x = 1, y = 3; return x && 0 + 1 || y;\n",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var x = 1, y = 3; return (x == 1) && (3 == 3) || y;\n",
                     factory->true_value())};

  for (size_t i = 0; i < arraysize(literals); i++) {
    std::string source(InterpreterTester::SourceForBody(literals[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *literals[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterTryCatch) {
  std::pair<const char*, Handle<Object>> catches[] = {
      std::make_pair("var a = 1; try { a = 2 } catch(e) { a = 3 }; return a;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a; try { undef.x } catch(e) { a = 2 }; return a;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a; try { throw 1 } catch(e) { a = e + 2 }; return a;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a; try { throw 1 } catch(e) { a = e + 2 };"
                     "       try { throw a } catch(e) { a = e + 3 }; return a;",
                     handle(Smi::FromInt(6), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(catches); i++) {
    std::string source(InterpreterTester::SourceForBody(catches[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *catches[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterTryFinally) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> finallies[] = {
      std::make_pair(
          "var a = 1; try { a = a + 1; } finally { a = a + 2; }; return a;",
          factory->NewStringFromStaticChars("R4")),
      std::make_pair(
          "var a = 1; try { a = 2; return 23; } finally { a = 3 }; return a;",
          factory->NewStringFromStaticChars("R23")),
      std::make_pair(
          "var a = 1; try { a = 2; throw 23; } finally { a = 3 }; return a;",
          factory->NewStringFromStaticChars("E23")),
      std::make_pair(
          "var a = 1; try { a = 2; throw 23; } finally { return a; };",
          factory->NewStringFromStaticChars("R2")),
      std::make_pair(
          "var a = 1; try { a = 2; throw 23; } finally { throw 42; };",
          factory->NewStringFromStaticChars("E42")),
      std::make_pair("var a = 1; for (var i = 10; i < 20; i += 5) {"
                     "  try { a = 2; break; } finally { a = 3; }"
                     "} return a + i;",
                     factory->NewStringFromStaticChars("R13")),
      std::make_pair("var a = 1; for (var i = 10; i < 20; i += 5) {"
                     "  try { a = 2; continue; } finally { a = 3; }"
                     "} return a + i;",
                     factory->NewStringFromStaticChars("R23")),
      std::make_pair("var a = 1; try { a = 2;"
                     "  try { a = 3; throw 23; } finally { a = 4; }"
                     "} catch(e) { a = a + e; } return a;",
                     factory->NewStringFromStaticChars("R27")),
      std::make_pair("var func_name;"
                     "function tcf2(a) {"
                     "  try { throw new Error('boom');} "
                     "  catch(e) {return 153; } "
                     "  finally {func_name = tcf2.name;}"
                     "}"
                     "tcf2();"
                     "return func_name;",
                     factory->NewStringFromStaticChars("Rtcf2")),
  };

  const char* try_wrapper =
      "(function() { try { return 'R' + f() } catch(e) { return 'E' + e }})()";

  for (size_t i = 0; i < arraysize(finallies); i++) {
    std::string source(InterpreterTester::SourceForBody(finallies[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    tester.GetCallable<>();
    DirectHandle<Object> wrapped =
        v8::Utils::OpenDirectHandle(*CompileRun(try_wrapper));
    CHECK(Object::SameValue(*wrapped, *finallies[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterThrow) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> throws[] = {
      std::make_pair("throw undefined;\n", factory->undefined_value()),
      std::make_pair("throw 1;\n", handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("throw 'Error';\n",
                     factory->NewStringFromStaticChars("Error")),
      std::make_pair("var a = true; if (a) { throw 'Error'; }\n",
                     factory->NewStringFromStaticChars("Error")),
      std::make_pair("var a = false; if (a) { throw 'Error'; }\n",
                     factory->undefined_value()),
      std::make_pair("throw 'Error1'; throw 'Error2'\n",
                     factory->NewStringFromStaticChars("Error1")),
  };

  const char* try_wrapper =
      "(function() { try { f(); } catch(e) { return e; }})()";

  for (size_t i = 0; i < arraysize(throws); i++) {
    std::string source(InterpreterTester::SourceForBody(throws[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    tester.GetCallable<>();
    DirectHandle<Object> thrown_obj =
        v8::Utils::OpenDirectHandle(*CompileRun(try_wrapper));
    CHECK(Object::SameValue(*thrown_obj, *throws[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterCountOperators) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> count_ops[] = {
      std::make_pair("var a = 1; return ++a;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a = 1; return a++;",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var a = 5; return --a;",
                     handle(Smi::FromInt(4), i_isolate())),
      std::make_pair("var a = 5; return a--;",
                     handle(Smi::FromInt(5), i_isolate())),
      std::make_pair("var a = 5.2; return --a;", factory->NewHeapNumber(4.2)),
      std::make_pair("var a = 'string'; return ++a;", factory->nan_value()),
      std::make_pair("var a = 'string'; return a--;", factory->nan_value()),
      std::make_pair("var a = true; return ++a;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a = false; return a--;",
                     handle(Smi::zero(), i_isolate())),
      std::make_pair("var a = { val: 11 }; return ++a.val;",
                     handle(Smi::FromInt(12), i_isolate())),
      std::make_pair("var a = { val: 11 }; return a.val--;",
                     handle(Smi::FromInt(11), i_isolate())),
      std::make_pair("var a = { val: 11 }; return ++a.val;",
                     handle(Smi::FromInt(12), i_isolate())),
      std::make_pair("var name = 'val'; var a = { val: 22 }; return --a[name];",
                     handle(Smi::FromInt(21), i_isolate())),
      std::make_pair("var name = 'val'; var a = { val: 22 }; return a[name]++;",
                     handle(Smi::FromInt(22), i_isolate())),
      std::make_pair("var a = 1; (function() { a = 2 })(); return ++a;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 1; (function() { a = 2 })(); return a--;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var i = 5; while(i--) {}; return i;",
                     handle(Smi::FromInt(-1), i_isolate())),
      std::make_pair("var i = 1; if(i--) { return 1; } else { return 2; };",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var i = -2; do {} while(i++) {}; return i;",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var i = -1; for(; i++; ) {}; return i",
                     handle(Smi::FromInt(1), i_isolate())),
      std::make_pair("var i = 20; switch(i++) {\n"
                     "  case 20: return 1;\n"
                     "  default: return 2;\n"
                     "}",
                     handle(Smi::FromInt(1), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(count_ops); i++) {
    std::string source(InterpreterTester::SourceForBody(count_ops[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *count_ops[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterGlobalCountOperators) {
  std::pair<const char*, Handle<Object>> count_ops[] = {
      std::make_pair("var global = 100;function f(){ return ++global; }",
                     handle(Smi::FromInt(101), i_isolate())),
      std::make_pair("var global = 100; function f(){ return --global; }",
                     handle(Smi::FromInt(99), i_isolate())),
      std::make_pair("var global = 100; function f(){ return global++; }",
                     handle(Smi::FromInt(100), i_isolate())),
      std::make_pair("unallocated = 200; function f(){ return ++unallocated; }",
                     handle(Smi::FromInt(201), i_isolate())),
      std::make_pair("unallocated = 200; function f(){ return --unallocated; }",
                     handle(Smi::FromInt(199), i_isolate())),
      std::make_pair("unallocated = 200; function f(){ return unallocated++; }",
                     handle(Smi::FromInt(200), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(count_ops); i++) {
    InterpreterTester tester(i_isolate(), count_ops[i].first);
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *count_ops[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterCompoundExpressions) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> compound_expr[] = {
      std::make_pair("var a = 1; a += 2; return a;",
                     Handle<Object>(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 10; a /= 2; return a;",
                     Handle<Object>(Smi::FromInt(5), i_isolate())),
      std::make_pair("var a = 'test'; a += 'ing'; return a;",
                     factory->NewStringFromStaticChars("testing")),
      std::make_pair("var a = { val: 2 }; a.val *= 2; return a.val;",
                     Handle<Object>(Smi::FromInt(4), i_isolate())),
      std::make_pair("var a = 1; (function f() { a = 2; })(); a += 24;"
                     "return a;",
                     Handle<Object>(Smi::FromInt(26), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(compound_expr); i++) {
    std::string source(
        InterpreterTester::SourceForBody(compound_expr[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *compound_expr[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterGlobalCompoundExpressions) {
  std::pair<const char*, Handle<Object>> compound_expr[2] = {
      std::make_pair("var global = 100;"
                     "function f() { global += 20; return global; }",
                     Handle<Object>(Smi::FromInt(120), i_isolate())),
      std::make_pair("unallocated = 100;"
                     "function f() { unallocated -= 20; return unallocated; }",
                     Handle<Object>(Smi::FromInt(80), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(compound_expr); i++) {
    InterpreterTester tester(i_isolate(), compound_expr[i].first);
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *compound_expr[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterCreateArguments) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, int> create_args[] = {
      std::make_pair("function f() { return arguments[0]; }", 0),
      std::make_pair("function f(a) { return arguments[0]; }", 0),
      std::make_pair("function f() { return arguments[2]; }", 2),
      std::make_pair("function f(a) { return arguments[2]; }", 2),
      std::make_pair("function f(a, b, c, d) { return arguments[2]; }", 2),
      std::make_pair("function f(a) {"
                     "'use strict'; return arguments[0]; }",
                     0),
      std::make_pair("function f(a, b, c, d) {"
                     "'use strict'; return arguments[2]; }",
                     2),
      // Check arguments are mapped in sloppy mode and unmapped in strict.
      std::make_pair("function f(a, b, c, d) {"
                     "  c = b; return arguments[2]; }",
                     1),
      std::make_pair("function f(a, b, c, d) {"
                     "  'use strict'; c = b; return arguments[2]; }",
                     2),
      // Check arguments for duplicate parameters in sloppy mode.
      std::make_pair("function f(a, a, b) { return arguments[1]; }", 1),
      // check rest parameters
      std::make_pair("function f(...restArray) { return restArray[0]; }", 0),
      std::make_pair("function f(a, ...restArray) { return restArray[0]; }", 1),
      std::make_pair("function f(a, ...restArray) { return arguments[0]; }", 0),
      std::make_pair("function f(a, ...restArray) { return arguments[1]; }", 1),
      std::make_pair("function f(a, ...restArray) { return restArray[1]; }", 2),
      std::make_pair("function f(a, ...arguments) { return arguments[0]; }", 1),
      std::make_pair("function f(a, b, ...restArray) { return restArray[0]; }",
                     2),
  };

  // Test passing no arguments.
  for (size_t i = 0; i < arraysize(create_args); i++) {
    InterpreterTester tester(i_isolate(), create_args[i].first);
    auto callable = tester.GetCallable<>();
    Handle<Object> return_val = callable().ToHandleChecked();
    CHECK(return_val.is_identical_to(factory->undefined_value()));
  }

  // Test passing one argument.
  for (size_t i = 0; i < arraysize(create_args); i++) {
    InterpreterTester tester(i_isolate(), create_args[i].first);
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> return_val =
        callable(handle(Smi::FromInt(40), i_isolate())).ToHandleChecked();
    if (create_args[i].second == 0) {
      CHECK_EQ(Cast<Smi>(*return_val), Smi::FromInt(40));
    } else {
      CHECK(return_val.is_identical_to(factory->undefined_value()));
    }
  }

  // Test passing three argument.
  for (size_t i = 0; i < arraysize(create_args); i++) {
    Handle<Object> args[3] = {
        handle(Smi::FromInt(40), i_isolate()),
        handle(Smi::FromInt(60), i_isolate()),
        handle(Smi::FromInt(80), i_isolate()),
    };

    InterpreterTester tester(i_isolate(), create_args[i].first);
    auto callable =
        tester.GetCallable<Handle<Object>, Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_val =
        callable(args[0], args[1], args[2]).ToHandleChecked();
    CHECK(Object::SameValue(*return_val, *args[create_args[i].second]));
  }
}

TEST_F(InterpreterTest, InterpreterConditional) {
  std::pair<const char*, Handle<Object>> conditional[] = {
      std::make_pair("return true ? 2 : 3;",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("return false ? 2 : 3;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 1; return a ? 20 : 30;",
                     handle(Smi::FromInt(20), i_isolate())),
      std::make_pair("var a = 1; return a ? 20 : 30;",
                     handle(Smi::FromInt(20), i_isolate())),
      std::make_pair("var a = 'string'; return a ? 20 : 30;",
                     handle(Smi::FromInt(20), i_isolate())),
      std::make_pair("var a = undefined; return a ? 20 : 30;",
                     handle(Smi::FromInt(30), i_isolate())),
      std::make_pair("return 1 ? 2 ? 3 : 4 : 5;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("return 0 ? 2 ? 3 : 4 : 5;",
                     handle(Smi::FromInt(5), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(conditional); i++) {
    std::string source(InterpreterTester::SourceForBody(conditional[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *conditional[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterDelete) {
  Factory* factory = i_isolate()->factory();

  // Tests for delete for local variables that work both in strict
  // and sloppy modes
  std::pair<const char*, Handle<Object>> test_delete[] = {
      std::make_pair(
          "var a = { x:10, y:'abc', z:30.2}; delete a.x; return a.x;\n",
          factory->undefined_value()),
      std::make_pair(
          "var b = { x:10, y:'abc', z:30.2}; delete b.x; return b.y;\n",
          factory->NewStringFromStaticChars("abc")),
      std::make_pair("var c = { x:10, y:'abc', z:30.2}; var d = c; delete d.x; "
                     "return c.x;\n",
                     factory->undefined_value()),
      std::make_pair("var e = { x:10, y:'abc', z:30.2}; var g = e; delete g.x; "
                     "return e.y;\n",
                     factory->NewStringFromStaticChars("abc")),
      std::make_pair("var a = { x:10, y:'abc', z:30.2};\n"
                     "var b = a;"
                     "delete b.x;"
                     "return b.x;\n",
                     factory->undefined_value()),
      std::make_pair("var a = {1:10};\n"
                     "(function f1() {return a;});"
                     "return delete a[1];",
                     factory->ToBoolean(true)),
      std::make_pair("return delete this;", factory->ToBoolean(true)),
      std::make_pair("return delete 'test';", factory->ToBoolean(true))};

  // Test delete in sloppy mode
  for (size_t i = 0; i < arraysize(test_delete); i++) {
    std::string source(InterpreterTester::SourceForBody(test_delete[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *test_delete[i].second));
  }

  // Test delete in strict mode
  for (size_t i = 0; i < arraysize(test_delete); i++) {
    std::string strict_test =
        "'use strict'; " + std::string(test_delete[i].first);
    std::string source(InterpreterTester::SourceForBody(strict_test.c_str()));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *test_delete[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterDeleteSloppyUnqualifiedIdentifier) {
  Factory* factory = i_isolate()->factory();

  // These tests generate a syntax error for strict mode. We don't
  // test for it here.
  std::pair<const char*, Handle<Object>> test_delete[] = {
      std::make_pair("var sloppy_a = { x:10, y:'abc'};\n"
                     "var sloppy_b = delete sloppy_a;\n"
                     "if (delete sloppy_a) {\n"
                     "  return undefined;\n"
                     "} else {\n"
                     "  return sloppy_a.x;\n"
                     "}\n",
                     Handle<Object>(Smi::FromInt(10), i_isolate())),
      // TODO(mythria) When try-catch is implemented change the tests to check
      // if delete actually deletes
      std::make_pair("sloppy_a = { x:10, y:'abc'};\n"
                     "var sloppy_b = delete sloppy_a;\n"
                     // "try{return a.x;} catch(e) {return b;}\n"
                     "return sloppy_b;",
                     factory->ToBoolean(true)),
      std::make_pair("sloppy_a = { x:10, y:'abc'};\n"
                     "var sloppy_b = delete sloppy_c;\n"
                     "return sloppy_b;",
                     factory->ToBoolean(true))};

  for (size_t i = 0; i < arraysize(test_delete); i++) {
    std::string source(InterpreterTester::SourceForBody(test_delete[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *test_delete[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterGlobalDelete) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> test_global_delete[] = {
      std::make_pair("var a = { x:10, y:'abc', z:30.2 };\n"
                     "function f() {\n"
                     "  delete a.x;\n"
                     "  return a.x;\n"
                     "}\n"
                     "f();\n",
                     factory->undefined_value()),
      std::make_pair("var b = {1:10, 2:'abc', 3:30.2 };\n"
                     "function f() {\n"
                     "  delete b[2];\n"
                     "  return b[1];\n"
                     " }\n"
                     "f();\n",
                     Handle<Object>(Smi::FromInt(10), i_isolate())),
      std::make_pair("var c = { x:10, y:'abc', z:30.2 };\n"
                     "function f() {\n"
                     "   var d = c;\n"
                     "   delete d.y;\n"
                     "   return d.x;\n"
                     "}\n"
                     "f();\n",
                     Handle<Object>(Smi::FromInt(10), i_isolate())),
      std::make_pair("e = { x:10, y:'abc' };\n"
                     "function f() {\n"
                     "  return delete e;\n"
                     "}\n"
                     "f();\n",
                     factory->ToBoolean(true)),
      std::make_pair("var g = { x:10, y:'abc' };\n"
                     "function f() {\n"
                     "  return delete g;\n"
                     "}\n"
                     "f();\n",
                     factory->ToBoolean(false)),
      std::make_pair("function f() {\n"
                     "  var obj = {h:10, f1() {return delete this;}};\n"
                     "  return obj.f1();\n"
                     "}\n"
                     "f();",
                     factory->ToBoolean(true)),
      std::make_pair("function f() {\n"
                     "  var obj = {h:10,\n"
                     "             f1() {\n"
                     "              'use strict';\n"
                     "              return delete this.h;}};\n"
                     "  return obj.f1();\n"
                     "}\n"
                     "f();",
                     factory->ToBoolean(true))};

  for (size_t i = 0; i < arraysize(test_global_delete); i++) {
    InterpreterTester tester(i_isolate(), test_global_delete[i].first);
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *test_global_delete[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterBasicLoops) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> loops[] = {
      std::make_pair("var a = 10; var b = 1;\n"
                     "while (a) {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "};\n"
                     "return b;\n",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 1; var b = 1;\n"
                     "do {\n"
                     "  b = b * 2;\n"
                     "  --a;\n"
                     "} while(a);\n"
                     "return b;\n",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var b = 1;\n"
                     "for ( var a = 10; a; a--) {\n"
                     "  b *= 2;\n"
                     "}\n"
                     "return b;",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 10; var b = 1;\n"
                     "while (a > 0) {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "};\n"
                     "return b;\n",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 1; var b = 1;\n"
                     "do {\n"
                     "  b = b * 2;\n"
                     "  --a;\n"
                     "} while(a);\n"
                     "return b;\n",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var b = 1;\n"
                     "for ( var a = 10; a > 0; a--) {\n"
                     "  b *= 2;\n"
                     "}\n"
                     "return b;",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 10; var b = 1;\n"
                     "while (false) {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "}\n"
                     "return b;\n",
                     Handle<Object>(Smi::FromInt(1), i_isolate())),
      std::make_pair("var a = 10; var b = 1;\n"
                     "while (true) {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "  if (a == 0) break;"
                     "  continue;"
                     "}\n"
                     "return b;\n",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 10; var b = 1;\n"
                     "do {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "  if (a == 0) break;"
                     "} while(true);\n"
                     "return b;\n",
                     factory->NewHeapNumber(1024)),
      std::make_pair("var a = 10; var b = 1;\n"
                     "do {\n"
                     "  b = b * 2;\n"
                     "  a = a - 1;\n"
                     "  if (a == 0) break;"
                     "} while(false);\n"
                     "return b;\n",
                     Handle<Object>(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a = 10; var b = 1;\n"
                     "for ( a = 1, b = 30; false; ) {\n"
                     "  b = b * 2;\n"
                     "}\n"
                     "return b;\n",
                     Handle<Object>(Smi::FromInt(30), i_isolate()))};

  for (size_t i = 0; i < arraysize(loops); i++) {
    std::string source(InterpreterTester::SourceForBody(loops[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *loops[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterForIn) {
  std::pair<const char*, int> for_in_samples[] = {
      {"var r = -1;\n"
       "for (var a in null) { r = a; }\n"
       "return r;\n",
       -1},
      {"var r = -1;\n"
       "for (var a in undefined) { r = a; }\n"
       "return r;\n",
       -1},
      {"var r = 0;\n"
       "for (var a in [0,6,7,9]) { r = r + (1 << a); }\n"
       "return r;\n",
       0xF},
      {"var r = 0;\n"
       "for (var a in [0,6,7,9]) { r = r + (1 << a); }\n"
       "var r = 0;\n"
       "for (var a in [0,6,7,9]) { r = r + (1 << a); }\n"
       "return r;\n",
       0xF},
      {"var r = 0;\n"
       "for (var a in 'foobar') { r = r + (1 << a); }\n"
       "return r;\n",
       0x3F},
      {"var r = 0;\n"
       "for (var a in {1:0, 10:1, 100:2, 1000:3}) {\n"
       "  r = r + Number(a);\n"
       " }\n"
       " return r;\n",
       1111},
      {"var r = 0;\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (var a in data) {\n"
       "  if (a == 1) delete data[1];\n"
       "  r = r + Number(a);\n"
       " }\n"
       " return r;\n",
       1111},
      {"var r = 0;\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (var a in data) {\n"
       "  if (a == 10) delete data[100];\n"
       "  r = r + Number(a);\n"
       " }\n"
       " return r;\n",
       1011},
      {"var r = 0;\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (var a in data) {\n"
       "  if (a == 10) data[10000] = 4;\n"
       "  r = r + Number(a);\n"
       " }\n"
       " return r;\n",
       1111},
      {"var r = 0;\n"
       "var input = 'foobar';\n"
       "for (var a in input) {\n"
       "  if (input[a] == 'b') break;\n"
       "  r = r + (1 << a);\n"
       "}\n"
       "return r;\n",
       0x7},
      {"var r = 0;\n"
       "var input = 'foobar';\n"
       "for (var a in input) {\n"
       " if (input[a] == 'b') continue;\n"
       " r = r + (1 << a);\n"
       "}\n"
       "return r;\n",
       0x37},
      {"var r = 0;\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (var a in data) {\n"
       "  if (a == 10) {\n"
       "     data[10000] = 4;\n"
       "  }\n"
       "  r = r + Number(a);\n"
       "}\n"
       "return r;\n",
       1111},
      {"var r = [ 3 ];\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (r[10] in data) {\n"
       "}\n"
       "return Number(r[10]);\n",
       1000},
      {"var r = [ 3 ];\n"
       "var data = {1:0, 10:1, 100:2, 1000:3};\n"
       "for (r['100'] in data) {\n"
       "}\n"
       "return Number(r['100']);\n",
       1000},
      {"var obj = {}\n"
       "var descObj = new Boolean(false);\n"
       "var accessed = 0;\n"
       "descObj.enumerable = true;\n"
       "Object.defineProperties(obj, { prop:descObj });\n"
       "for (var p in obj) {\n"
       "  if (p === 'prop') { accessed = 1; }\n"
       "}\n"
       "return accessed;",
       1},
      {"var appointment = {};\n"
       "Object.defineProperty(appointment, 'startTime', {\n"
       "    value: 1001,\n"
       "    writable: false,\n"
       "    enumerable: false,\n"
       "    configurable: true\n"
       "});\n"
       "Object.defineProperty(appointment, 'name', {\n"
       "    value: 'NAME',\n"
       "    writable: false,\n"
       "    enumerable: false,\n"
       "    configurable: true\n"
       "});\n"
       "var meeting = Object.create(appointment);\n"
       "Object.defineProperty(meeting, 'conferenceCall', {\n"
       "    value: 'In-person meeting',\n"
       "    writable: false,\n"
       "    enumerable: false,\n"
       "    configurable: true\n"
       "});\n"
       "\n"
       "var teamMeeting = Object.create(meeting);\n"
       "\n"
       "var flags = 0;\n"
       "for (var p in teamMeeting) {\n"
       "    if (p === 'startTime') {\n"
       "        flags |= 1;\n"
       "    }\n"
       "    if (p === 'name') {\n"
       "        flags |= 2;\n"
       "    }\n"
       "    if (p === 'conferenceCall') {\n"
       "        flags |= 4;\n"
       "    }\n"
       "}\n"
       "\n"
       "var hasOwnProperty = !teamMeeting.hasOwnProperty('name') &&\n"
       "    !teamMeeting.hasOwnProperty('startTime') &&\n"
       "    !teamMeeting.hasOwnProperty('conferenceCall');\n"
       "if (!hasOwnProperty) {\n"
       "    flags |= 8;\n"
       "}\n"
       "return flags;\n",
       0},
      {"var data = {x:23, y:34};\n"
       " var result = 0;\n"
       "var o = {};\n"
       "var arr = [o];\n"
       "for (arr[0].p in data)\n"       // This is to test if value is loaded
       "  result += data[arr[0].p];\n"  // back from accumulator before storing
       "return result;\n",              // named properties.
       57},
      {"var data = {x:23, y:34};\n"
       "var result = 0;\n"
       "var o = {};\n"
       "var i = 0;\n"
       "for (o[i++] in data)\n"       // This is to test if value is loaded
       "  result += data[o[i-1]];\n"  // back from accumulator before
       "return result;\n",            // storing keyed properties.
       57}};

  // Two passes are made for this test. On the first, 8-bit register
  // operands are employed, and on the 16-bit register operands are
  // used.
  for (int pass = 0; pass < 2; pass++) {
    std::ostringstream wide_os;
    if (pass == 1) {
      for (int i = 0; i < 200; i++) {
        wide_os << "var local" << i << " = 0;\n";
      }
    }

    for (size_t i = 0; i < arraysize(for_in_samples); i++) {
      std::ostringstream body_os;
      body_os << wide_os.str() << for_in_samples[i].first;
      std::string body(body_os.str());
      std::string function = InterpreterTester::SourceForBody(body.c_str());
      InterpreterTester tester(i_isolate(), function.c_str());
      auto callable = tester.GetCallable<>();
      DirectHandle<Object> return_val = callable().ToHandleChecked();
      CHECK_EQ(Cast<Smi>(*return_val).value(), for_in_samples[i].second);
    }
  }
}

TEST_F(InterpreterTest, InterpreterForOf) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> for_of[] = {
      {"function f() {\n"
       "  var r = 0;\n"
       "  for (var a of [0,6,7,9]) { r += a; }\n"
       "  return r;\n"
       "}",
       handle(Smi::FromInt(22), i_isolate())},
      {"function f() {\n"
       "  var r = '';\n"
       "  for (var a of 'foobar') { r = a + r; }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("raboof")},
      {"function f() {\n"
       "  var a = [1, 2, 3];\n"
       "  a.name = 4;\n"
       "  var r = 0;\n"
       "  for (var x of a) { r += x; }\n"
       "  return r;\n"
       "}",
       handle(Smi::FromInt(6), i_isolate())},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data[0]; r += a; } return r; }",
       factory->NewStringFromStaticChars("123")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data[2]; r += a; } return r; }",
       factory->NewStringFromStaticChars("12undefined")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data; r += a; } return r; }",
       factory->NewStringFromStaticChars("123")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var input = 'foobar';\n"
       "  for (var a of input) {\n"
       "    if (a == 'b') break;\n"
       "    r += a;\n"
       "  }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("foo")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var input = 'foobar';\n"
       "  for (var a of input) {\n"
       "    if (a == 'b') continue;\n"
       "    r += a;\n"
       "  }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("fooar")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[2] = 567; r += a; }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("125674")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[4] = 567; r += a; }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("1234567")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[5] = 567; r += a; }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("1234undefined567")},
      {"function f() {\n"
       "  var r = '';\n"
       "  var obj = new Object();\n"
       "  obj[Symbol.iterator] = function() { return {\n"
       "    index: 3,\n"
       "    data: ['a', 'b', 'c', 'd'],"
       "    next: function() {"
       "      return {"
       "        done: this.index == -1,\n"
       "        value: this.index < 0 ? undefined : this.data[this.index--]\n"
       "      }\n"
       "    }\n"
       "    }}\n"
       "  for (a of obj) { r += a }\n"
       "  return r;\n"
       "}",
       factory->NewStringFromStaticChars("dcba")},
  };

  for (size_t i = 0; i < arraysize(for_of); i++) {
    InterpreterTester tester(i_isolate(), for_of[i].first);
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_val = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_val, *for_of[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterSwitch) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> switch_ops[] = {
      std::make_pair("var a = 1;\n"
                     "switch(a) {\n"
                     " case 1: return 2;\n"
                     " case 2: return 3;\n"
                     "}\n",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a = 1;\n"
                     "switch(a) {\n"
                     " case 2: a = 2; break;\n"
                     " case 1: a = 3; break;\n"
                     "}\n"
                     "return a;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 1;\n"
                     "switch(a) {\n"
                     " case 1: a = 2; // fall-through\n"
                     " case 2: a = 3; break;\n"
                     "}\n"
                     "return a;",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 100;\n"
                     "switch(a) {\n"
                     " case 1: return 100;\n"
                     " case 2: return 200;\n"
                     "}\n"
                     "return undefined;",
                     factory->undefined_value()),
      std::make_pair("var a = 100;\n"
                     "switch(a) {\n"
                     " case 1: return 100;\n"
                     " case 2: return 200;\n"
                     " default: return 300;\n"
                     "}\n"
                     "return undefined;",
                     handle(Smi::FromInt(300), i_isolate())),
      std::make_pair("var a = 100;\n"
                     "switch(typeof(a)) {\n"
                     " case 'string': return 1;\n"
                     " case 'number': return 2;\n"
                     " default: return 3;\n"
                     "}\n",
                     handle(Smi::FromInt(2), i_isolate())),
      std::make_pair("var a = 100;\n"
                     "switch(a) {\n"
                     " case a += 20: return 1;\n"
                     " case a -= 10: return 2;\n"
                     " case a -= 10: return 3;\n"
                     " default: return 3;\n"
                     "}\n",
                     handle(Smi::FromInt(3), i_isolate())),
      std::make_pair("var a = 1;\n"
                     "switch(a) {\n"
                     " case 1: \n"
                     "   switch(a + 1) {\n"
                     "      case 2 : a += 1; break;\n"
                     "      default : a += 2; break;\n"
                     "   }  // fall-through\n"
                     " case 2: a += 3;\n"
                     "}\n"
                     "return a;",
                     handle(Smi::FromInt(5), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(switch_ops); i++) {
    std::string source(InterpreterTester::SourceForBody(switch_ops[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *switch_ops[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterSloppyThis) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> sloppy_this[] = {
      std::make_pair("var global_val = 100;\n"
                     "function f() { return this.global_val; }\n",
                     handle(Smi::FromInt(100), i_isolate())),
      std::make_pair("var global_val = 110;\n"
                     "function g() { return this.global_val; };"
                     "function f() { return g(); }\n",
                     handle(Smi::FromInt(110), i_isolate())),
      std::make_pair("var global_val = 110;\n"
                     "function g() { return this.global_val };"
                     "function f() { 'use strict'; return g(); }\n",
                     handle(Smi::FromInt(110), i_isolate())),
      std::make_pair("function f() { 'use strict'; return this; }\n",
                     factory->undefined_value()),
      std::make_pair("function g() { 'use strict'; return this; };"
                     "function f() { return g(); }\n",
                     factory->undefined_value()),
  };

  for (size_t i = 0; i < arraysize(sloppy_this); i++) {
    InterpreterTester tester(i_isolate(), sloppy_this[i].first);
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *sloppy_this[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterThisFunction) {
  Factory* factory = i_isolate()->factory();

  InterpreterTester tester(i_isolate(),
                           "var f;\n f = function f() { return f.name; }");
  auto callable = tester.GetCallable<>();

  DirectHandle<i::Object> return_value = callable().ToHandleChecked();
  CHECK(Object::SameValue(*return_value,
                          *factory->NewStringFromStaticChars("f")));
}

TEST_F(InterpreterTest, InterpreterNewTarget) {
  Factory* factory = i_isolate()->factory();

  // TODO(rmcilroy): Add tests that we get the original constructor for
  // superclass constructors once we have class support.
  InterpreterTester tester(i_isolate(),
                           "function f() { this.a = new.target; }");
  auto callable = tester.GetCallable<>();
  callable().ToHandleChecked();

  DirectHandle<Object> new_target_name = v8::Utils::OpenDirectHandle(
      *CompileRun("(function() { return (new f()).a.name; })();"));
  CHECK(Object::SameValue(*new_target_name,
                          *factory->NewStringFromStaticChars("f")));
}

TEST_F(InterpreterTest, InterpreterAssignmentInExpressions) {
  std::pair<const char*, int> samples[] = {
      {"function f() {\n"
       "  var x = 7;\n"
       "  var y = x + (x = 1) + (x = 2);\n"
       "  return y;\n"
       "}",
       10},
      {"function f() {\n"
       "  var x = 7;\n"
       "  var y = x + (x = 1) + (x = 2);\n"
       "  return x;\n"
       "}",
       2},
      {"function f() {\n"
       "  var x = 55;\n"
       "  x = x + (x = 100) + (x = 101);\n"
       "  return x;\n"
       "}",
       256},
      {"function f() {\n"
       "  var x = 7;\n"
       "  return ++x + x + x++;\n"
       "}",
       24},
      {"function f() {\n"
       "  var x = 7;\n"
       "  var y = 1 + ++x + x + x++;\n"
       "  return x;\n"
       "}",
       9},
      {"function f() {\n"
       "  var x = 7;\n"
       "  var y = ++x + x + x++;\n"
       "  return x;\n"
       "}",
       9},
      {"function f() {\n"
       "  var x = 7, y = 100, z = 1000;\n"
       "  return x + (x += 3) + y + (y *= 10) + (z *= 7) + z;\n"
       "}",
       15117},
      {"function f() {\n"
       "  var inner = function (x) { return x + (x = 2) + (x = 4) + x; };\n"
       "  return inner(1);\n"
       "}",
       11},
      {"function f() {\n"
       "  var x = 1, y = 2;\n"
       "  x = x + (x = 3) + y + (y = 4), y = y + (y = 5) + y + x;\n"
       "  return x + y;\n"
       "}",
       10 + 24},
      {"function f() {\n"
       "  var x = 0;\n"
       "  var y = x | (x = 1) | (x = 2);\n"
       "  return x;\n"
       "}",
       2},
      {"function f() {\n"
       "  var x = 0;\n"
       "  var y = x || (x = 1);\n"
       "  return x;\n"
       "}",
       1},
      {"function f() {\n"
       "  var x = 1;\n"
       "  var y = x && (x = 2) && (x = 3);\n"
       "  return x;\n"
       "}",
       3},
      {"function f() {\n"
       "  var x = 1;\n"
       "  var y = x || (x = 2);\n"
       "  return x;\n"
       "}",
       1},
      {"function f() {\n"
       "  var x = 1;\n"
       "  x = (x << (x = 3)) | (x = 16);\n"
       "  return x;\n"
       "}",
       24},
      {"function f() {\n"
       "  var r = 7;\n"
       "  var s = 11;\n"
       "  var t = 13;\n"
       "  var u = r + s + t + (r = 10) + (s = 20) +"
       "          (t = (r + s)) + r + s + t;\n"
       "  return r + s + t + u;\n"
       "}",
       211},
      {"function f() {\n"
       "  var r = 7;\n"
       "  var s = 11;\n"
       "  var t = 13;\n"
       "  return r > (3 * s * (s = 1)) ? (t + (t += 1)) : (r + (r = 4));\n"
       "}",
       11},
      {"function f() {\n"
       "  var r = 7;\n"
       "  var s = 11;\n"
       "  var t = 13;\n"
       "  return r > (3 * s * (s = 0)) ? (t + (t += 1)) : (r + (r = 4));\n"
       "}",
       27},
      {"function f() {\n"
       "  var r = 7;\n"
       "  var s = 11;\n"
       "  var t = 13;\n"
       "  return (r + (r = 5)) > s ? r : t;\n"
       "}",
       5},
      {"function f(a) {\n"
       "  return a + (arguments[0] = 10);\n"
       "}",
       50},
      {"function f(a) {\n"
       "  return a + (arguments[0] = 10) + a;\n"
       "}",
       60},
      {"function f(a) {\n"
       "  return a + (arguments[0] = 10) + arguments[0];\n"
       "}",
       60},
  };

  const int arg_value = 40;
  for (size_t i = 0; i < arraysize(samples); i++) {
    InterpreterTester tester(i_isolate(), samples[i].first);
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_val =
        callable(handle(Smi::FromInt(arg_value), i_isolate()))
            .ToHandleChecked();
    CHECK_EQ(Cast<Smi>(*return_val).value(), samples[i].second);
  }
}

TEST_F(InterpreterTest, InterpreterToName) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> to_name_tests[] = {
      {"var a = 'val'; var obj = {[a] : 10}; return obj.val;",
       factory->NewNumberFromInt(10)},
      {"var a = 20; var obj = {[a] : 10}; return obj['20'];",
       factory->NewNumberFromInt(10)},
      {"var a = 20; var obj = {[a] : 10}; return obj[20];",
       factory->NewNumberFromInt(10)},
      {"var a = {val:23}; var obj = {[a] : 10}; return obj[a];",
       factory->NewNumberFromInt(10)},
      {"var a = {val:23}; var obj = {[a] : 10};\n"
       "return obj['[object Object]'];",
       factory->NewNumberFromInt(10)},
      {"var a = {toString : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       factory->NewNumberFromInt(10)},
      {"var a = {valueOf : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       factory->undefined_value()},
      {"var a = {[Symbol.toPrimitive] : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       factory->NewNumberFromInt(10)},
  };

  for (size_t i = 0; i < arraysize(to_name_tests); i++) {
    std::string source(
        InterpreterTester::SourceForBody(to_name_tests[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *to_name_tests[i].second));
  }
}

TEST_F(InterpreterTest, TemporaryRegisterAllocation) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> reg_tests[] = {
      {"function add(a, b, c) {"
       "   return a + b + c;"
       "}"
       "function f() {"
       "  var a = 10, b = 10;"
       "   return add(a, b++, b);"
       "}",
       factory->NewNumberFromInt(31)},
      {"function add(a, b, c, d) {"
       "  return a + b + c + d;"
       "}"
       "function f() {"
       "  var x = 10, y = 20, z = 30;"
       "  return x + add(x, (y= x++), x, z);"
       "}",
       factory->NewNumberFromInt(71)},
  };

  for (size_t i = 0; i < arraysize(reg_tests); i++) {
    InterpreterTester tester(i_isolate(), reg_tests[i].first);
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *reg_tests[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterLookupSlot) {
  Factory* factory = i_isolate()->factory();

  // TODO(mythria): Add more tests when we have support for eval/with.
  const char* function_prologue =
      "var f;"
      "var x = 1;"
      "function f1() {"
      "  eval(\"function t() {";
  const char* function_epilogue =
      "        }; f = t;\");"
      "}"
      "f1();";

  std::pair<const char*, Handle<Object>> lookup_slot[] = {
      {"return x;", handle(Smi::FromInt(1), i_isolate())},
      {"return typeof x;", factory->NewStringFromStaticChars("number")},
      {"return typeof dummy;", factory->NewStringFromStaticChars("undefined")},
      {"x = 10; return x;", handle(Smi::FromInt(10), i_isolate())},
      {"'use strict'; x = 20; return x;",
       handle(Smi::FromInt(20), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(lookup_slot); i++) {
    std::string script = std::string(function_prologue) +
                         std::string(lookup_slot[i].first) +
                         std::string(function_epilogue);

    InterpreterTester tester(i_isolate(), script.c_str(), "t");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *lookup_slot[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterLookupContextSlot) {
  const char* inner_function_prologue = "function inner() {";
  const char* inner_function_epilogue = "};";
  const char* outer_function_epilogue = "return inner();";

  std::tuple<const char*, const char*, Handle<Object>> lookup_slot[] = {
      // Eval in inner context.
      std::make_tuple("var x = 0;", "eval(''); return x;",
                      handle(Smi::zero(), i_isolate())),
      std::make_tuple("var x = 0;", "eval('var x = 1'); return x;",
                      handle(Smi::FromInt(1), i_isolate())),
      std::make_tuple("var x = 0;",
                      "'use strict'; eval('var x = 1'); return x;",
                      handle(Smi::zero(), i_isolate())),
      // Eval in outer context.
      std::make_tuple("var x = 0; eval('');", "return x;",
                      handle(Smi::zero(), i_isolate())),
      std::make_tuple("var x = 0; eval('var x = 1');", "return x;",
                      handle(Smi::FromInt(1), i_isolate())),
      std::make_tuple("'use strict'; var x = 0; eval('var x = 1');",
                      "return x;", handle(Smi::zero(), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(lookup_slot); i++) {
    std::string body = std::string(std::get<0>(lookup_slot[i])) +
                       std::string(inner_function_prologue) +
                       std::string(std::get<1>(lookup_slot[i])) +
                       std::string(inner_function_epilogue) +
                       std::string(outer_function_epilogue);
    std::string script = InterpreterTester::SourceForBody(body.c_str());

    InterpreterTester tester(i_isolate(), script.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *std::get<2>(lookup_slot[i])));
  }
}

TEST_F(InterpreterTest, InterpreterLookupGlobalSlot) {
  const char* inner_function_prologue = "function inner() {";
  const char* inner_function_epilogue = "};";
  const char* outer_function_epilogue = "return inner();";

  std::tuple<const char*, const char*, Handle<Object>> lookup_slot[] = {
      // Eval in inner context.
      std::make_tuple("x = 0;", "eval(''); return x;",
                      handle(Smi::zero(), i_isolate())),
      std::make_tuple("x = 0;", "eval('var x = 1'); return x;",
                      handle(Smi::FromInt(1), i_isolate())),
      std::make_tuple("x = 0;", "'use strict'; eval('var x = 1'); return x;",
                      handle(Smi::zero(), i_isolate())),
      // Eval in outer context.
      std::make_tuple("x = 0; eval('');", "return x;",
                      handle(Smi::zero(), i_isolate())),
      std::make_tuple("x = 0; eval('var x = 1');", "return x;",
                      handle(Smi::FromInt(1), i_isolate())),
      std::make_tuple("'use strict'; x = 0; eval('var x = 1');", "return x;",
                      handle(Smi::zero(), i_isolate())),
  };

  for (size_t i = 0; i < arraysize(lookup_slot); i++) {
    std::string body = std::string(std::get<0>(lookup_slot[i])) +
                       std::string(inner_function_prologue) +
                       std::string(std::get<1>(lookup_slot[i])) +
                       std::string(inner_function_epilogue) +
                       std::string(outer_function_epilogue);
    std::string script = InterpreterTester::SourceForBody(body.c_str());

    InterpreterTester tester(i_isolate(), script.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *std::get<2>(lookup_slot[i])));
  }
}

TEST_F(InterpreterTest, InterpreterCallLookupSlot) {
  std::pair<const char*, Handle<Object>> call_lookup[] = {
      {"g = function(){ return 2 }; eval(''); return g();",
       handle(Smi::FromInt(2), i_isolate())},
      {"g = function(){ return 2 }; eval('g = function() {return 3}');\n"
       "return g();",
       handle(Smi::FromInt(3), i_isolate())},
      {"g = { x: function(){ return this.y }, y: 20 };\n"
       "eval('g = { x: g.x, y: 30 }');\n"
       "return g.x();",
       handle(Smi::FromInt(30), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(call_lookup); i++) {
    std::string source(InterpreterTester::SourceForBody(call_lookup[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *call_lookup[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterLookupSlotWide) {
  Factory* factory = i_isolate()->factory();

  const char* function_prologue =
      "var f;"
      "var x = 1;"
      "function f1() {"
      "  eval(\"function t() {";
  const char* function_epilogue =
      "        }; f = t;\");"
      "}"
      "f1();";
  std::ostringstream str;
  str << "var y = 2.3;";
  for (int i = 1; i < 256; i++) {
    str << "y = " << 2.3 + i << ";";
  }
  std::string init_function_body = str.str();

  std::pair<std::string, Handle<Object>> lookup_slot[] = {
      {init_function_body + "return x;", handle(Smi::FromInt(1), i_isolate())},
      {init_function_body + "return typeof x;",
       factory->NewStringFromStaticChars("number")},
      {init_function_body + "return x = 10;",
       handle(Smi::FromInt(10), i_isolate())},
      {"'use strict';" + init_function_body + "x = 20; return x;",
       handle(Smi::FromInt(20), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(lookup_slot); i++) {
    std::string script = std::string(function_prologue) + lookup_slot[i].first +
                         std::string(function_epilogue);

    InterpreterTester tester(i_isolate(), script.c_str(), "t");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *lookup_slot[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterDeleteLookupSlot) {
  Factory* factory = i_isolate()->factory();

  // TODO(mythria): Add more tests when we have support for eval/with.
  const char* function_prologue =
      "var f;"
      "var x = 1;"
      "y = 10;"
      "var obj = {val:10};"
      "var z = 30;"
      "function f1() {"
      "  var z = 20;"
      "  eval(\"function t() {";
  const char* function_epilogue =
      "        }; f = t;\");"
      "}"
      "f1();";

  std::pair<const char*, Handle<Object>> delete_lookup_slot[] = {
      {"return delete x;", factory->false_value()},
      {"return delete y;", factory->true_value()},
      {"return delete z;", factory->false_value()},
      {"return delete obj.val;", factory->true_value()},
      {"'use strict'; return delete obj.val;", factory->true_value()},
  };

  for (size_t i = 0; i < arraysize(delete_lookup_slot); i++) {
    std::string script = std::string(function_prologue) +
                         std::string(delete_lookup_slot[i].first) +
                         std::string(function_epilogue);

    InterpreterTester tester(i_isolate(), script.c_str(), "t");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *delete_lookup_slot[i].second));
  }
}

TEST_F(InterpreterTest, JumpWithConstantsAndWideConstants) {
  Factory* factory = i_isolate()->factory();
  const int kStep = 13;
  for (int constants = 11; constants < 256 + 3 * kStep; constants += kStep) {
    std::ostringstream filler_os;
    // Generate a string that consumes constant pool entries and
    // spread out branch distances in script below.
    for (int i = 0; i < constants; i++) {
      filler_os << "var x_ = 'x_" << i << "';\n";
    }
    std::string filler(filler_os.str());
    std::ostringstream script_os;
    script_os << "function " << InterpreterTester::function_name() << "(a) {\n";
    script_os << "  " << filler;
    script_os << "  for (var i = a; i < 2; i++) {\n";
    script_os << "  " << filler;
    script_os << "    if (i == 0) { " << filler << "i = 10; continue; }\n";
    script_os << "    else if (i == a) { " << filler << "i = 12; break; }\n";
    script_os << "    else { " << filler << " }\n";
    script_os << "  }\n";
    script_os << "  return i;\n";
    script_os << "}\n";
    std::string script(script_os.str());
    for (int a = 0; a < 3; a++) {
      InterpreterTester tester(i_isolate(), script.c_str());
      auto callable = tester.GetCallable<Handle<Object>>();
      Handle<Object> argument = factory->NewNumberFromInt(a);
      DirectHandle<Object> return_val = callable(argument).ToHandleChecked();
      static const int results[] = {11, 12, 2};
      CHECK_EQ(Cast<Smi>(*return_val).value(), results[a]);
    }
  }
}

TEST_F(InterpreterTest, InterpreterEval) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> eval[] = {
      {"return eval('1;');", handle(Smi::FromInt(1), i_isolate())},
      {"return eval('100 * 20;');", handle(Smi::FromInt(2000), i_isolate())},
      {"var x = 10; return eval('x + 20;');",
       handle(Smi::FromInt(30), i_isolate())},
      {"var x = 10; eval('x = 33;'); return x;",
       handle(Smi::FromInt(33), i_isolate())},
      {"'use strict'; var x = 20; var z = 0;\n"
       "eval('var x = 33; z = x;'); return x + z;",
       handle(Smi::FromInt(53), i_isolate())},
      {"eval('var x = 33;'); eval('var y = x + 20'); return x + y;",
       handle(Smi::FromInt(86), i_isolate())},
      {"var x = 1; eval('for(i = 0; i < 10; i++) x = x + 1;'); return x",
       handle(Smi::FromInt(11), i_isolate())},
      {"var x = 10; eval('var x = 20;'); return x;",
       handle(Smi::FromInt(20), i_isolate())},
      {"var x = 1; eval('\"use strict\"; var x = 2;'); return x;",
       handle(Smi::FromInt(1), i_isolate())},
      {"'use strict'; var x = 1; eval('var x = 2;'); return x;",
       handle(Smi::FromInt(1), i_isolate())},
      {"var x = 10; eval('x + 20;'); return typeof x;",
       factory->NewStringFromStaticChars("number")},
      {"eval('var y = 10;'); return typeof unallocated;",
       factory->NewStringFromStaticChars("undefined")},
      {"'use strict'; eval('var y = 10;'); return typeof unallocated;",
       factory->NewStringFromStaticChars("undefined")},
      {"eval('var x = 10;'); return typeof x;",
       factory->NewStringFromStaticChars("number")},
      {"var x = {}; eval('var x = 10;'); return typeof x;",
       factory->NewStringFromStaticChars("number")},
      {"'use strict'; var x = {}; eval('var x = 10;'); return typeof x;",
       factory->NewStringFromStaticChars("object")},
  };

  for (size_t i = 0; i < arraysize(eval); i++) {
    std::string source(InterpreterTester::SourceForBody(eval[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();
    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *eval[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterEvalParams) {
  std::pair<const char*, Handle<Object>> eval_params[] = {
      {"var x = 10; return eval('x + p1;');",
       handle(Smi::FromInt(30), i_isolate())},
      {"var x = 10; eval('p1 = x;'); return p1;",
       handle(Smi::FromInt(10), i_isolate())},
      {"var a = 10;"
       "function inner() { return eval('a + p1;');}"
       "return inner();",
       handle(Smi::FromInt(30), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(eval_params); i++) {
    std::string source = "function " + InterpreterTester::function_name() +
                         "(p1) {" + eval_params[i].first + "}";
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<Handle<Object>>();

    DirectHandle<i::Object> return_value =
        callable(handle(Smi::FromInt(20), i_isolate())).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *eval_params[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterEvalGlobal) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> eval_global[] = {
      {"function add_global() { eval('function test() { z = 33; }; test()'); };"
       "function f() { add_global(); return z; }; f();",
       handle(Smi::FromInt(33), i_isolate())},
      {"function add_global() {\n"
       " eval('\"use strict\"; function test() { y = 33; };"
       "      try { test() } catch(e) {}');\n"
       "}\n"
       "function f() { add_global(); return typeof y; } f();",
       factory->NewStringFromStaticChars("undefined")},
  };

  for (size_t i = 0; i < arraysize(eval_global); i++) {
    InterpreterTester tester(i_isolate(), eval_global[i].first, "test");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *eval_global[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterEvalVariableDecl) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> eval_global[] = {
      {"function f() { eval('var x = 10; x++;'); return x; }",
       handle(Smi::FromInt(11), i_isolate())},
      {"function f() { var x = 20; eval('var x = 10; x++;'); return x; }",
       handle(Smi::FromInt(11), i_isolate())},
      {"function f() {"
       " var x = 20;"
       " eval('\"use strict\"; var x = 10; x++;');"
       " return x; }",
       handle(Smi::FromInt(20), i_isolate())},
      {"function f() {"
       " var y = 30;"
       " eval('var x = {1:20}; x[2]=y;');"
       " return x[2]; }",
       handle(Smi::FromInt(30), i_isolate())},
      {"function f() {"
       " eval('var x = {name:\"test\"};');"
       " return x.name; }",
       factory->NewStringFromStaticChars("test")},
      {"function f() {"
       "  eval('var x = [{name:\"test\"}, {type:\"cc\"}];');"
       "  return x[1].type+x[0].name; }",
       factory->NewStringFromStaticChars("cctest")},
      {"function f() {\n"
       " var x = 3;\n"
       " var get_eval_x;\n"
       " eval('\"use strict\"; "
       "      var x = 20; "
       "      get_eval_x = function func() {return x;};');\n"
       " return get_eval_x() + x;\n"
       "}",
       handle(Smi::FromInt(23), i_isolate())},
      // TODO(mythria): Add tests with const declarations.
  };

  for (size_t i = 0; i < arraysize(eval_global); i++) {
    InterpreterTester tester(i_isolate(), eval_global[i].first, "*");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *eval_global[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterEvalFunctionDecl) {
  std::pair<const char*, Handle<Object>> eval_func_decl[] = {
      {"function f() {\n"
       " var x = 3;\n"
       " eval('var x = 20;"
       "       function get_x() {return x;};');\n"
       " return get_x() + x;\n"
       "}",
       handle(Smi::FromInt(40), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(eval_func_decl); i++) {
    InterpreterTester tester(i_isolate(), eval_func_decl[i].first, "*");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *eval_func_decl[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterWideRegisterArithmetic) {
  static const size_t kMaxRegisterForTest = 150;
  std::ostringstream os;
  os << "function " << InterpreterTester::function_name() << "(arg) {\n";
  os << "  var retval = -77;\n";
  for (size_t i = 0; i < kMaxRegisterForTest; i++) {
    os << "  var x" << i << " = " << i << ";\n";
  }
  for (size_t i = 0; i < kMaxRegisterForTest / 2; i++) {
    size_t j = kMaxRegisterForTest - i - 1;
    os << "  var tmp = x" << j << ";\n";
    os << "  var x" << j << " = x" << i << ";\n";
    os << "  var x" << i << " = tmp;\n";
  }
  for (size_t i = 0; i < kMaxRegisterForTest / 2; i++) {
    size_t j = kMaxRegisterForTest - i - 1;
    os << "  var tmp = x" << j << ";\n";
    os << "  var x" << j << " = x" << i << ";\n";
    os << "  var x" << i << " = tmp;\n";
  }
  for (size_t i = 0; i < kMaxRegisterForTest; i++) {
    os << "  if (arg == " << i << ") {\n"  //
       << "    retval = x" << i << ";\n"   //
       << "  }\n";                         //
  }
  os << "  return retval;\n";
  os << "}\n";

  std::string source = os.str();
  InterpreterTester tester(i_isolate(), source.c_str());
  auto callable = tester.GetCallable<Handle<Object>>();
  for (size_t i = 0; i < kMaxRegisterForTest; i++) {
    Handle<Object> arg = handle(Smi::FromInt(static_cast<int>(i)), i_isolate());
    DirectHandle<Object> return_value = callable(arg).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *arg));
  }
}

TEST_F(InterpreterTest, InterpreterCallWideRegisters) {
  static const int kPeriod = 25;
  static const int kLength = 512;
  static const int kStartChar = 65;

  for (int pass = 0; pass < 3; pass += 1) {
    std::ostringstream os;
    for (int i = 0; i < pass * 97; i += 1) {
      os << "var x" << i << " = " << i << "\n";
    }
    os << "return String.fromCharCode(";
    os << kStartChar;
    for (int i = 1; i < kLength; i += 1) {
      os << "," << kStartChar + (i % kPeriod);
    }
    os << ");";
    std::string source = InterpreterTester::SourceForBody(os.str().c_str());
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable();
    Handle<Object> return_val = callable().ToHandleChecked();
    DirectHandle<String> return_string = Cast<String>(return_val);
    CHECK_EQ(return_string->length(), kLength);
    for (int i = 0; i < kLength; i += 1) {
      CHECK_EQ(return_string->Get(i), 65 + (i % kPeriod));
    }
  }
}

TEST_F(InterpreterTest, InterpreterWideParametersPickOne) {
  static const int kParameterCount = 130;
  for (int parameter = 0; parameter < 10; parameter++) {
    std::ostringstream os;
    os << "function " << InterpreterTester::function_name() << "(arg) {\n";
    os << "  function selector(i";
    for (int i = 0; i < kParameterCount; i++) {
      os << ","
         << "a" << i;
    }
    os << ") {\n";
    os << "  return a" << parameter << ";\n";
    os << "  };\n";
    os << "  return selector(arg";
    for (int i = 0; i < kParameterCount; i++) {
      os << "," << i;
    }
    os << ");";
    os << "}\n";

    std::string source = os.str();
    InterpreterTester tester(i_isolate(), source.c_str(), "*");
    auto callable = tester.GetCallable<Handle<Object>>();
    Handle<Object> arg = handle(Smi::FromInt(0xAA55), i_isolate());
    DirectHandle<Object> return_value = callable(arg).ToHandleChecked();
    Tagged<Smi> actual = Cast<Smi>(*return_value);
    CHECK_EQ(actual.value(), parameter);
  }
}

TEST_F(InterpreterTest, InterpreterWideParametersSummation) {
  static int kParameterCount = 200;
  static int kBaseValue = 17000;

  std::ostringstream os;
  os << "function " << InterpreterTester::function_name() << "(arg) {\n";
  os << "  function summation(i";
  for (int i = 0; i < kParameterCount; i++) {
    os << ","
       << "a" << i;
  }
  os << ") {\n";
  os << "    var sum = " << kBaseValue << ";\n";
  os << "    switch(i) {\n";
  for (int i = 0; i < kParameterCount; i++) {
    int j = kParameterCount - i - 1;
    os << "      case " << j << ": sum += a" << j << ";\n";
  }
  os << "  }\n";
  os << "    return sum;\n";
  os << "  };\n";
  os << "  return summation(arg";
  for (int i = 0; i < kParameterCount; i++) {
    os << "," << i;
  }
  os << ");";
  os << "}\n";

  std::string source = os.str();
  InterpreterTester tester(i_isolate(), source.c_str(), "*");
  auto callable = tester.GetCallable<Handle<Object>>();
  for (int i = 0; i < kParameterCount; i++) {
    Handle<Object> arg = handle(Smi::FromInt(i), i_isolate());
    DirectHandle<Object> return_value = callable(arg).ToHandleChecked();
    int expected = kBaseValue + i * (i + 1) / 2;
    Tagged<Smi> actual = Cast<Smi>(*return_value);
    CHECK_EQ(actual.value(), expected);
  }
}

TEST_F(InterpreterTest, InterpreterWithStatement) {
  std::pair<const char*, Handle<Object>> with_stmt[] = {
      {"with({x:42}) return x;", handle(Smi::FromInt(42), i_isolate())},
      {"with({}) { var y = 10; return y;}",
       handle(Smi::FromInt(10), i_isolate())},
      {"var y = {x:42};"
       " function inner() {"
       "   var x = 20;"
       "   with(y) return x;"
       "}"
       "return inner();",
       handle(Smi::FromInt(42), i_isolate())},
      {"var y = {x:42};"
       " function inner(o) {"
       "   var x = 20;"
       "   with(o) return x;"
       "}"
       "return inner(y);",
       handle(Smi::FromInt(42), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(with_stmt); i++) {
    std::string source(InterpreterTester::SourceForBody(with_stmt[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *with_stmt[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterClassLiterals) {
  std::pair<const char*, Handle<Object>> examples[] = {
      {"class C {\n"
       "  constructor(x) { this.x_ = x; }\n"
       "  method() { return this.x_; }\n"
       "}\n"
       "return new C(99).method();",
       handle(Smi::FromInt(99), i_isolate())},
      {"class C {\n"
       "  constructor(x) { this.x_ = x; }\n"
       "  static static_method(x) { return x; }\n"
       "}\n"
       "return C.static_method(101);",
       handle(Smi::FromInt(101), i_isolate())},
      {"class C {\n"
       "  get x() { return 102; }\n"
       "}\n"
       "return new C().x",
       handle(Smi::FromInt(102), i_isolate())},
      {"class C {\n"
       "  static get x() { return 103; }\n"
       "}\n"
       "return C.x",
       handle(Smi::FromInt(103), i_isolate())},
      {"class C {\n"
       "  constructor() { this.x_ = 0; }"
       "  set x(value) { this.x_ = value; }\n"
       "  get x() { return this.x_; }\n"
       "}\n"
       "var c = new C();"
       "c.x = 104;"
       "return c.x;",
       handle(Smi::FromInt(104), i_isolate())},
      {"var x = 0;"
       "class C {\n"
       "  static set x(value) { x = value; }\n"
       "  static get x() { return x; }\n"
       "}\n"
       "C.x = 105;"
       "return C.x;",
       handle(Smi::FromInt(105), i_isolate())},
      {"var method = 'f';"
       "class C {\n"
       "  [method]() { return 106; }\n"
       "}\n"
       "return new C().f();",
       handle(Smi::FromInt(106), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(examples); ++i) {
    std::string source(InterpreterTester::SourceForBody(examples[i].first));
    InterpreterTester tester(i_isolate(), source.c_str(), "*");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *examples[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterClassAndSuperClass) {
  std::pair<const char*, Handle<Object>> examples[] = {
      {"class A {\n"
       "  constructor(x) { this.x_ = x; }\n"
       "  method() { return this.x_; }\n"
       "}\n"
       "class B extends A {\n"
       "   constructor(x, y) { super(x); this.y_ = y; }\n"
       "   method() { return super.method() + 1; }\n"
       "}\n"
       "return new B(998, 0).method();\n",
       handle(Smi::FromInt(999), i_isolate())},
      {"class A {\n"
       "  constructor() { this.x_ = 2; this.y_ = 3; }\n"
       "}\n"
       "class B extends A {\n"
       "  constructor() { super(); }"
       "  method() { this.x_++; this.y_++; return this.x_ + this.y_; }\n"
       "}\n"
       "return new B().method();\n",
       handle(Smi::FromInt(7), i_isolate())},
      {"var calls = 0;\n"
       "class B {}\n"
       "B.prototype.x = 42;\n"
       "class C extends B {\n"
       "  constructor() {\n"
       "    super();\n"
       "    calls++;\n"
       "  }\n"
       "}\n"
       "new C;\n"
       "return calls;\n",
       handle(Smi::FromInt(1), i_isolate())},
      {"class A {\n"
       "  method() { return 1; }\n"
       "  get x() { return 2; }\n"
       "}\n"
       "class B extends A {\n"
       "  method() { return super.x === 2 ? super.method() : -1; }\n"
       "}\n"
       "return new B().method();\n",
       handle(Smi::FromInt(1), i_isolate())},
      {"var object = { setY(v) { super.y = v; }};\n"
       "object.setY(10);\n"
       "return object.y;\n",
       handle(Smi::FromInt(10), i_isolate())},
  };

  for (size_t i = 0; i < arraysize(examples); ++i) {
    std::string source(InterpreterTester::SourceForBody(examples[i].first));
    InterpreterTester tester(i_isolate(), source.c_str(), "*");
    auto callable = tester.GetCallable<>();
    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *examples[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterConstDeclaration) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> const_decl[] = {
      {"const x = 3; return x;", handle(Smi::FromInt(3), i_isolate())},
      {"let x = 10; x = x + 20; return x;",
       handle(Smi::FromInt(30), i_isolate())},
      {"let x = 10; x = 20; return x;", handle(Smi::FromInt(20), i_isolate())},
      {"let x; x = 20; return x;", handle(Smi::FromInt(20), i_isolate())},
      {"let x; return x;", factory->undefined_value()},
      {"var x = 10; { let x = 30; } return x;",
       handle(Smi::FromInt(10), i_isolate())},
      {"let x = 10; { let x = 20; } return x;",
       handle(Smi::FromInt(10), i_isolate())},
      {"var x = 10; eval('let x = 20;'); return x;",
       handle(Smi::FromInt(10), i_isolate())},
      {"var x = 10; eval('const x = 20;'); return x;",
       handle(Smi::FromInt(10), i_isolate())},
      {"var x = 10; { const x = 20; } return x;",
       handle(Smi::FromInt(10), i_isolate())},
      {"var x = 10; { const x = 20; return x;} return -1;",
       handle(Smi::FromInt(20), i_isolate())},
      {"var a = 10;\n"
       "for (var i = 0; i < 10; ++i) {\n"
       " const x = i;\n"  // const declarations are block scoped.
       " a = a + x;\n"
       "}\n"
       "return a;\n",
       handle(Smi::FromInt(55), i_isolate())},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string source(InterpreterTester::SourceForBody(const_decl[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].second));
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string strict_body =
        "'use strict'; " + std::string(const_decl[i].first);
    std::string source(InterpreterTester::SourceForBody(strict_body.c_str()));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterConstDeclarationLookupSlots) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> const_decl[] = {
      {"const x = 3; function f1() {return x;}; return x;",
       handle(Smi::FromInt(3), i_isolate())},
      {"let x = 10; x = x + 20; function f1() {return x;}; return x;",
       handle(Smi::FromInt(30), i_isolate())},
      {"let x; x = 20; function f1() {return x;}; return x;",
       handle(Smi::FromInt(20), i_isolate())},
      {"let x; function f1() {return x;}; return x;",
       factory->undefined_value()},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string source(InterpreterTester::SourceForBody(const_decl[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].second));
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string strict_body =
        "'use strict'; " + std::string(const_decl[i].first);
    std::string source(InterpreterTester::SourceForBody(strict_body.c_str()));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterConstInLookupContextChain) {
  const char* prologue =
      "function OuterMost() {\n"
      "  const outerConst = 10;\n"
      "  let outerLet = 20;\n"
      "  function Outer() {\n"
      "    function Inner() {\n"
      "      this.innerFunc = function() { ";
  const char* epilogue =
      "      }\n"
      "    }\n"
      "    this.getInnerFunc ="
      "         function() {return new Inner().innerFunc;}\n"
      "  }\n"
      "  this.getOuterFunc ="
      "     function() {return new Outer().getInnerFunc();}"
      "}\n"
      "var f = new OuterMost().getOuterFunc();\n"
      "f();\n";
  std::pair<const char*, Handle<Object>> const_decl[] = {
      {"return outerConst;", handle(Smi::FromInt(10), i_isolate())},
      {"return outerLet;", handle(Smi::FromInt(20), i_isolate())},
      {"outerLet = 30; return outerLet;",
       handle(Smi::FromInt(30), i_isolate())},
      {"var outerLet = 40; return outerLet;",
       handle(Smi::FromInt(40), i_isolate())},
      {"var outerConst = 50; return outerConst;",
       handle(Smi::FromInt(50), i_isolate())},
      {"try { outerConst = 30 } catch(e) { return -1; }",
       handle(Smi::FromInt(-1), i_isolate())}};

  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string script = std::string(prologue) +
                         std::string(const_decl[i].first) +
                         std::string(epilogue);
    InterpreterTester tester(i_isolate(), script.c_str(), "*");
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].second));
  }
}

TEST_F(InterpreterTest, InterpreterIllegalConstDeclaration) {
  std::pair<const char*, const char*> const_decl[] = {
      {"const x = x = 10 + 3; return x;",
       "Uncaught ReferenceError: Cannot access 'x' before initialization"},
      {"const x = 10; x = 20; return x;",
       "Uncaught TypeError: Assignment to constant variable."},
      {"const x = 10; { x = 20; } return x;",
       "Uncaught TypeError: Assignment to constant variable."},
      {"const x = 10; eval('x = 20;'); return x;",
       "Uncaught TypeError: Assignment to constant variable."},
      {"let x = x + 10; return x;",
       "Uncaught ReferenceError: Cannot access 'x' before initialization"},
      {"'use strict'; (function f1() { f1 = 123; })() ",
       "Uncaught TypeError: Assignment to constant variable."},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string source(InterpreterTester::SourceForBody(const_decl[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string = NewString(const_decl[i].second);
    CHECK(message->Equals(context(), expected_string).FromJust());
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(const_decl); i++) {
    std::string strict_body =
        "'use strict'; " + std::string(const_decl[i].first);
    std::string source(InterpreterTester::SourceForBody(strict_body.c_str()));
    InterpreterTester tester(i_isolate(), source.c_str());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string = NewString(const_decl[i].second);
    CHECK(message->Equals(context(), expected_string).FromJust());
  }
}

TEST_F(InterpreterTest, InterpreterGenerators) {
  Factory* factory = i_isolate()->factory();

  std::pair<const char*, Handle<Object>> tests[] = {
      {"function* f() { }; return f().next().value",
       factory->undefined_value()},
      {"function* f() { yield 42 }; return f().next().value",
       factory->NewNumberFromInt(42)},
      {"function* f() { for (let x of [42]) yield x}; return f().next().value",
       factory->NewNumberFromInt(42)},
  };

  for (size_t i = 0; i < arraysize(tests); i++) {
    std::string source(InterpreterTester::SourceForBody(tests[i].first));
    InterpreterTester tester(i_isolate(), source.c_str());
    auto callable = tester.GetCallable<>();

    DirectHandle<i::Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *tests[i].second));
  }
}

#ifndef V8_TARGET_ARCH_ARM
TEST_F(InterpreterTest, InterpreterWithNativeStack) {
  // "Always sparkplug" messes with this test.
  if (v8_flags.always_sparkplug) return;

  i::FakeCodeEventLogger code_event_logger(i_isolate());
  i::v8_flags.interpreted_frames_native_stack = true;
  CHECK(i_isolate()->logger()->AddListener(&code_event_logger));

  const char* source_text =
      "function testInterpreterWithNativeStack(a,b) { return a + b };";

  i::DirectHandle<i::Object> o = v8::Utils::OpenDirectHandle(
      *v8::Script::Compile(context(), v8::String::NewFromUtf8(
                                          context()->GetIsolate(), source_text)
                                          .ToLocalChecked())
           .ToLocalChecked());

  i::DirectHandle<i::JSFunction> f = i::Cast<i::JSFunction>(o);

  CHECK(f->shared()->HasBytecodeArray());
  i::Tagged<i::Code> code = f->shared()->GetCode(i_isolate());
  i::DirectHandle<i::Code> interpreter_entry_trampoline =
      BUILTIN_CODE(i_isolate(), InterpreterEntryTrampoline);

  CHECK(IsCode(code));
  CHECK(code->is_interpreter_trampoline_builtin());
  CHECK_NE(code.address(), interpreter_entry_trampoline->address());

  CHECK(i_isolate()->logger()->RemoveListener(&code_event_logger));
}
#endif  // V8_TARGET_ARCH_ARM

TEST_F(InterpreterTest, InterpreterGetBytecodeHandler) {
  Interpreter* interpreter = i_isolate()->interpreter();

  // Test that single-width bytecode handlers deserializer correctly.
  Tagged<Code> wide_handler =
      interpreter->GetBytecodeHandler(Bytecode::kWide, OperandScale::kSingle);

  CHECK_EQ(wide_handler->builtin_id(), Builtin::kWideHandler);

  Tagged<Code> add_handler =
      interpreter->GetBytecodeHandler(Bytecode::kAdd, OperandScale::kSingle);

  CHECK_EQ(add_handler->builtin_id(), Builtin::kAddHandler);

  // Test that double-width bytecode handlers deserializer correctly, including
  // an illegal bytecode handler since there is no Wide.Wide handler.
  Tagged<Code> wide_wide_handler =
      interpreter->GetBytecodeHandler(Bytecode::kWide, OperandScale::kDouble);

  CHECK_EQ(wide_wide_handler->builtin_id(), Builtin::kIllegalHandler);

  Tagged<Code> add_wide_handler =
      interpreter->GetBytecodeHandler(Bytecode::kAdd, OperandScale::kDouble);

  CHECK_EQ(add_wide_handler->builtin_id(), Builtin::kAddWideHandler);
}

TEST_F(InterpreterTest, InterpreterCollectSourcePositions) {
  v8_flags.enable_lazy_source_positions = true;
  v8_flags.stress_lazy_source_positions = false;

  const char* source =
      "(function () {\n"
      "  return 1;\n"
      "})";

  DirectHandle<JSFunction> function =
      Cast<JSFunction>(v8::Utils::OpenDirectHandle(
          *v8::Local<v8::Function>::Cast(CompileRun(source))));

  Handle<SharedFunctionInfo> sfi(function->shared(), i_isolate());
  DirectHandle<BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate()),
                                             i_isolate());
  CHECK(!bytecode_array->HasSourcePositionTable());

  Compiler::CollectSourcePositions(i_isolate(), sfi);

  Tagged<TrustedByteArray> source_position_table =
      bytecode_array->SourcePositionTable();
  CHECK(bytecode_array->HasSourcePositionTable());
  CHECK_GT(source_position_table->length(), 0);
}

TEST_F(InterpreterTest, InterpreterCollectSourcePositions_StackOverflow) {
  v8_flags.enable_lazy_source_positions = true;
  v8_flags.stress_lazy_source_positions = false;

  const char* source =
      "(function () {\n"
      "  return 1;\n"
      "})";

  DirectHandle<JSFunction> function =
      Cast<JSFunction>(v8::Utils::OpenDirectHandle(
          *v8::Local<v8::Function>::Cast(CompileRun(source))));

  Handle<SharedFunctionInfo> sfi(function->shared(), i_isolate());
  DirectHandle<BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate()),
                                             i_isolate());
  CHECK(!bytecode_array->HasSourcePositionTable());

  // Make the stack limit the same as the current position so recompilation
  // overflows.
  uint64_t previous_limit = i_isolate()->stack_guard()->real_climit();
  i_isolate()->stack_guard()->SetStackLimit(GetCurrentStackPosition());
  Compiler::CollectSourcePositions(i_isolate(), sfi);
  // Stack overflowed so source position table can be returned but is empty.
  Tagged<TrustedByteArray> source_position_table =
      bytecode_array->SourcePositionTable();
  CHECK(!bytecode_array->HasSourcePositionTable());
  CHECK_EQ(source_position_table->length(), 0);

  // Reset the stack limit and try again.
  i_isolate()->stack_guard()->SetStackLimit(previous_limit);
  Compiler::CollectSourcePositions(i_isolate(), sfi);
  source_position_table = bytecode_array->SourcePositionTable();
  CHECK(bytecode_array->HasSourcePositionTable());
  CHECK_GT(source_position_table->length(), 0);
}

TEST_F(InterpreterTest, InterpreterCollectSourcePositions_ThrowFrom1stFrame) {
  v8_flags.enable_lazy_source_positions = true;
  v8_flags.stress_lazy_source_positions = false;

  const char* source =
      R"javascript(
      (function () {
        throw new Error();
      });
      )javascript";

  Handle<JSFunction> function = Cast<JSFunction>(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));

  DirectHandle<SharedFunctionInfo> sfi(function->shared(), i_isolate());
  // This is the bytecode for the top-level iife.
  DirectHandle<BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate()),
                                             i_isolate());
  CHECK(!bytecode_array->HasSourcePositionTable());

  {
    v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(i_isolate()));
    MaybeHandle<Object> result = Execution::Call(
        i_isolate(), function,
        ReadOnlyRoots(i_isolate()).undefined_value_handle(), 0, nullptr);
    CHECK(result.is_null());
    CHECK(try_catch.HasCaught());
  }

  // The exception was caught but source positions were not retrieved from it so
  // there should be no source position table.
  CHECK(!bytecode_array->HasSourcePositionTable());
}

TEST_F(InterpreterTest, InterpreterCollectSourcePositions_ThrowFrom2ndFrame) {
  v8_flags.enable_lazy_source_positions = true;
  v8_flags.stress_lazy_source_positions = false;

  const char* source =
      R"javascript(
      (function () {
        (function () {
          throw new Error();
        })();
      });
      )javascript";

  Handle<JSFunction> function = Cast<JSFunction>(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));

  DirectHandle<SharedFunctionInfo> sfi(function->shared(), i_isolate());
  // This is the bytecode for the top-level iife.
  DirectHandle<BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate()),
                                             i_isolate());
  CHECK(!bytecode_array->HasSourcePositionTable());

  {
    v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(i_isolate()));
    MaybeHandle<Object> result = Execution::Call(
        i_isolate(), function,
        ReadOnlyRoots(i_isolate()).undefined_value_handle(), 0, nullptr);
    CHECK(result.is_null());
    CHECK(try_catch.HasCaught());
  }

  // The exception was caught but source positions were not retrieved from it so
  // there should be no source position table.
  CHECK(!bytecode_array->HasSourcePositionTable());
}

namespace {

void CheckStringEqual(const char* expected_ptr, const char* actual_ptr) {
  CHECK_NOT_NULL(expected_ptr);
  CHECK_NOT_NULL(actual_ptr);
  std::string expected(expected_ptr);
  std::string actual(actual_ptr);
  CHECK_EQ(expected, actual);
}

void CheckStringEqual(const char* expected_ptr, Handle<Object> actual_handle) {
  v8::String::Utf8Value utf8(v8::Isolate::GetCurrent(),
                             v8::Utils::ToLocal(Cast<String>(actual_handle)));
  CheckStringEqual(expected_ptr, *utf8);
}

}  // namespace

TEST_F(InterpreterTest, InterpreterCollectSourcePositions_GenerateStackTrace) {
  v8_flags.enable_lazy_source_positions = true;
  v8_flags.stress_lazy_source_positions = false;

  const char* source =
      R"javascript(
      (function () {
        try {
          throw new Error();
        } catch (e) {
          return e.stack;
        }
      });
      )javascript";

  Handle<JSFunction> function = Cast<JSFunction>(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));

  DirectHandle<SharedFunctionInfo> sfi(function->shared(), i_isolate());
  DirectHandle<BytecodeArray> bytecode_array(sfi->GetBytecodeArray(i_isolate()),
                                             i_isolate());
  CHECK(!bytecode_array->HasSourcePositionTable());

  {
    Handle<Object> result =
        Execution::Call(i_isolate(), function,
                        ReadOnlyRoots(i_isolate()).undefined_value_handle(), 0,
                        nullptr)
            .ToHandleChecked();
    CheckStringEqual("Error\n    at <anonymous>:4:17", result);
  }

  CHECK(bytecode_array->HasSourcePositionTable());
  Tagged<TrustedByteArray> source_position_table =
      bytecode_array->SourcePositionTable();
  CHECK_GT(source_position_table->length(), 0);
}

TEST_F(InterpreterTest, InterpreterLookupNameOfBytecodeHandler) {
  Interpreter* interpreter = i_isolate()->interpreter();
  Tagged<Code> ldaLookupSlot = interpreter->GetBytecodeHandler(
      Bytecode::kLdaLookupSlot, OperandScale::kSingle);
  CheckStringEqual("LdaLookupSlotHandler",
                   Builtins::name(ldaLookupSlot->builtin_id()));
  Tagged<Code> wideLdaLookupSlot = interpreter->GetBytecodeHandler(
      Bytecode::kLdaLookupSlot, OperandScale::kDouble);
  CheckStringEqual("LdaLookupSlotWideHandler",
                   Builtins::name(wideLdaLookupSlot->builtin_id()));
  Tagged<Code> extraWideLdaLookupSlot = interpreter->GetBytecodeHandler(
      Bytecode::kLdaLookupSlot, OperandScale::kQuadruple);
  CheckStringEqual("LdaLookupSlotExtraWideHandler",
                   Builtins::name(extraWideLdaLookupSlot->builtin_id()));
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
