// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cctype>

#include "src/codegen/tick-counter.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/js-call-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/simplified-operator.h"
#include "src/execution/isolate.h"
#include "src/execution/protectors.h"
#include "src/heap/factory.h"
#include "src/objects/feedback-vector.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSCallReducerTest : public TypedGraphTest {
 public:
  JSCallReducerTest()
      : TypedGraphTest(3), javascript_(zone()), deps_(broker(), zone()) {
  }
  ~JSCallReducerTest() override = default;

 protected:
  Reduction Reduce(Node* node) {
    MachineOperatorBuilder machine(zone());
    SimplifiedOperatorBuilder simplified(zone());
    JSGraph jsgraph(isolate(), graph(), common(), javascript(), &simplified,
                    &machine);
    GraphReducer graph_reducer(zone(), graph(), tick_counter(), broker());
    JSCallReducer reducer(&graph_reducer, &jsgraph, broker(), zone(),
                          JSCallReducer::kNoFlags);
    return reducer.Reduce(node);
  }

  JSOperatorBuilder* javascript() { return &javascript_; }

  Node* GlobalFunction(const char* name) {
    Handle<JSFunction> f = Cast<JSFunction>(
        Object::GetProperty(
            isolate(), isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked(name))
            .ToHandleChecked());
    return HeapConstantNoHole(CanonicalHandle(f));
  }

  Node* MathFunction(const std::string& name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate(), isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("Math"))
            .ToHandleChecked();
    Handle<JSFunction> f = Cast<JSFunction>(
        Object::GetProperty(
            isolate(), m,
            isolate()->factory()->NewStringFromAsciiChecked(name.c_str()))
            .ToHandleChecked());
    return HeapConstantNoHole(CanonicalHandle(f));
  }

  Node* StringFunction(const char* name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate(), isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("String"))
            .ToHandleChecked();
    Handle<JSFunction> f = Cast<JSFunction>(
        Object::GetProperty(
            isolate(), m, isolate()->factory()->NewStringFromAsciiChecked(name))
            .ToHandleChecked());
    return HeapConstantNoHole(CanonicalHandle(f));
  }

  Node* NumberFunction(const char* name) {
    Handle<Object> m =
        JSObject::GetProperty(
            isolate(), isolate()->global_object(),
            isolate()->factory()->NewStringFromAsciiChecked("Number"))
            .ToHandleChecked();
    Handle<JSFunction> f = Cast<JSFunction>(
        Object::GetProperty(
            isolate(), m, isolate()->factory()->NewStringFromAsciiChecked(name))
            .ToHandleChecked());
    return HeapConstantNoHole(CanonicalHandle(f));
  }

  std::string op_name_for(const char* fnc) {
    std::string string_fnc(fnc);
    char initial = std::toupper(fnc[0]);
    return std::string("Number") + initial +
           string_fnc.substr(1, std::string::npos);
  }

  const Operator* Call(int arity) {
    FeedbackVectorSpec spec(zone());
    spec.AddCallICSlot();
    Handle<FeedbackVector> vector =
        FeedbackVector::NewForTesting(isolate(), &spec);
    FeedbackSource feedback(vector, FeedbackSlot(0));
    return javascript()->Call(JSCallNode::ArityForArgc(arity), CallFrequency(),
                              feedback, ConvertReceiverMode::kAny,
                              SpeculationMode::kAllowSpeculation,
                              CallFeedbackRelation::kTarget);
  }

  Node* DummyFrameState() {
    return graph()->NewNode(
        common()->FrameState(BytecodeOffset{42},
                             OutputFrameStateCombine::Ignore(), nullptr),
        graph()->start(), graph()->start(), graph()->start(), graph()->start(),
        graph()->start(), graph()->start());
  }

 private:
  JSOperatorBuilder javascript_;
  CompilationDependencies deps_;
};

TEST_F(JSCallReducerTest, PromiseConstructorNoArgs) {
  Node* promise =
      HeapConstantNoHole(CanonicalHandle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();

  Node* construct = graph()->NewNode(
      javascript()->Construct(JSConstructNode::ArityForArgc(0)), promise,
      promise, feedback, context, frame_state, effect, control);

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

TEST_F(JSCallReducerTest, PromiseConstructorSubclass) {
  Node* promise =
      HeapConstantNoHole(CanonicalHandle(native_context()->promise_function()));
  Node* new_target =
      HeapConstantNoHole(CanonicalHandle(native_context()->array_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();

  Node* executor = UndefinedConstant();
  Node* construct = graph()->NewNode(
      javascript()->Construct(JSConstructNode::ArityForArgc(1)), promise,
      new_target, executor, feedback, context, frame_state, effect, control);

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

TEST_F(JSCallReducerTest, PromiseConstructorBasic) {
  Node* promise =
      HeapConstantNoHole(CanonicalHandle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();

  Node* executor = UndefinedConstant();
  Node* construct = graph()->NewNode(
      javascript()->Construct(JSConstructNode::ArityForArgc(1)), promise,
      promise, executor, feedback, context, frame_state, effect, control);

  Reduction r = Reduce(construct);
  ASSERT_TRUE(r.Changed());
}

// Exactly the same as PromiseConstructorBasic which expects a reduction,
// except that we invalidate the protector cell.
TEST_F(JSCallReducerTest, PromiseConstructorWithHook) {
  Node* promise =
      HeapConstantNoHole(CanonicalHandle(native_context()->promise_function()));
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();

  Node* executor = UndefinedConstant();
  Node* construct = graph()->NewNode(
      javascript()->Construct(JSConstructNode::ArityForArgc(1)), promise,
      promise, executor, feedback, context, frame_state, effect, control);

  Protectors::InvalidatePromiseHook(isolate());

  Reduction r = Reduce(construct);

  ASSERT_FALSE(r.Changed());
}

// -----------------------------------------------------------------------------
// Math unaries

namespace {

const char* kMathUnaries[] = {
    "abs",  "acos",  "acosh", "asin", "asinh", "atan",  "cbrt",
    "ceil", "cos",   "cosh",  "exp",  "expm1", "floor", "fround",
    "log",  "log1p", "log10", "log2", "round", "sign",  "sin",
    "sinh", "sqrt",  "tan",   "tanh", "trunc"};

}  // namespace

TEST_F(JSCallReducerTest, MathUnaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathUnaries) {
    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = DummyFrameState();
    Node* jsfunction = MathFunction(fnc);
    Node* p0 = Parameter(Type::Any(), 0);
    Node* feedback = UndefinedConstant();
    Node* call =
        graph()->NewNode(Call(1), jsfunction, UndefinedConstant(), p0, feedback,
                         context, frame_state, effect, control);
    Reduction r = Reduce(call);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math binaries

namespace {

const char* kMathBinaries[] = {"atan2", "pow"};

}  // namespace

TEST_F(JSCallReducerTest, MathBinaryWithNumber) {
  TRACED_FOREACH(const char*, fnc, kMathBinaries) {
    Node* jsfunction = MathFunction(fnc);

    Node* effect = graph()->start();
    Node* control = graph()->start();
    Node* context = UndefinedConstant();
    Node* frame_state = DummyFrameState();
    Node* p0 = Parameter(Type::Any(), 0);
    Node* p1 = Parameter(Type::Any(), 0);
    Node* feedback = UndefinedConstant();
    Node* call =
        graph()->NewNode(Call(2), jsfunction, UndefinedConstant(), p0, p1,
                         feedback, context, frame_state, effect, control);
    Reduction r = Reduce(call);

    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
                op_name_for(fnc));
  }
}

// -----------------------------------------------------------------------------
// Math.clz32

TEST_F(JSCallReducerTest, MathClz32WithUnsigned32) {
  Node* jsfunction = MathFunction("clz32");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();

  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), jsfunction, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(),
              IsNumberClz32(IsNumberToUint32(IsSpeculativeToNumber(p0))));
}

TEST_F(JSCallReducerTest, MathClz32WithUnsigned32NoArg) {
  Node* jsfunction = MathFunction("clz32");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();

  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(0), jsfunction, UndefinedConstant(), feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(32));
}

// -----------------------------------------------------------------------------
// Math.imul

TEST_F(JSCallReducerTest, MathImulWithUnsigned32) {
  Node* jsfunction = MathFunction("imul");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Unsigned32(), 0);
  Node* p1 = Parameter(Type::Unsigned32(), 1);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(2), jsfunction, UndefinedConstant(), p0, p1,
                       feedback, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(std::string(IrOpcode::Mnemonic(r.replacement()->opcode())),
              op_name_for("imul"));
}

// -----------------------------------------------------------------------------
// Math.min

TEST_F(JSCallReducerTest, MathMinWithNoArguments) {
  Node* jsfunction = MathFunction("min");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(0), jsfunction, UndefinedConstant(), feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(V8_INFINITY));
}

TEST_F(JSCallReducerTest, MathMinWithNumber) {
  Node* jsfunction = MathFunction("min");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), jsfunction, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSCallReducerTest, MathMinWithTwoArguments) {
  Node* jsfunction = MathFunction("min");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(2), jsfunction, UndefinedConstant(), p0, p1,
                       feedback, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMin(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

// -----------------------------------------------------------------------------
// Math.max

TEST_F(JSCallReducerTest, MathMaxWithNoArguments) {
  Node* jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(0), jsfunction, UndefinedConstant(), feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberConstant(-V8_INFINITY));
}

TEST_F(JSCallReducerTest, MathMaxWithNumber) {
  Node* jsfunction = MathFunction("max");
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), jsfunction, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsSpeculativeToNumber(p0));
}

TEST_F(JSCallReducerTest, MathMaxWithTwoArguments) {
  Node* jsfunction = MathFunction("max");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(2), jsfunction, UndefinedConstant(), p0, p1,
                       feedback, context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberMax(IsSpeculativeToNumber(p0),
                                           IsSpeculativeToNumber(p1)));
}

// -----------------------------------------------------------------------------
// String.fromCharCode

TEST_F(JSCallReducerTest, StringFromSingleCharCodeWithNumber) {
  Node* function = StringFunction("fromCharCode");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(),
              IsStringFromSingleCharCode(IsSpeculativeToNumber(p0)));
}

TEST_F(JSCallReducerTest, StringFromSingleCharCodeWithPlainPrimitive) {
  Node* function = StringFunction("fromCharCode");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::PlainPrimitive(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(),
              IsStringFromSingleCharCode(IsSpeculativeToNumber(p0)));
}

// -----------------------------------------------------------------------------
// Number.isFinite

TEST_F(JSCallReducerTest, NumberIsFinite) {
  Node* function = NumberFunction("isFinite");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsObjectIsFiniteNumber(p0));
}

// -----------------------------------------------------------------------------
// Number.isInteger

TEST_F(JSCallReducerTest, NumberIsIntegerWithNumber) {
  Node* function = NumberFunction("isInteger");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsObjectIsInteger(p0));
}

// -----------------------------------------------------------------------------
// Number.isNaN

TEST_F(JSCallReducerTest, NumberIsNaNWithNumber) {
  Node* function = NumberFunction("isNaN");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsObjectIsNaN(p0));
}

// -----------------------------------------------------------------------------
// Number.isSafeInteger

TEST_F(JSCallReducerTest, NumberIsSafeIntegerWithIntegral32) {
  Node* function = NumberFunction("isSafeInteger");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsObjectIsSafeInteger(p0));
}

// -----------------------------------------------------------------------------
// isFinite

TEST_F(JSCallReducerTest, GlobalIsFiniteWithNumber) {
  Node* function = GlobalFunction("isFinite");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberIsFinite(IsSpeculativeToNumber(p0)));
}

// -----------------------------------------------------------------------------
// isNaN

TEST_F(JSCallReducerTest, GlobalIsNaN) {
  Node* function = GlobalFunction("isNaN");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(1), function, UndefinedConstant(), p0, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsNumberIsNaN(IsSpeculativeToNumber(p0)));
}

// -----------------------------------------------------------------------------
// Number.parseInt

TEST_F(JSCallReducerTest, NumberParseInt) {
  Node* function = NumberFunction("parseInt");

  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* context = UndefinedConstant();
  Node* frame_state = DummyFrameState();
  Node* p0 = Parameter(Type::Any(), 0);
  Node* p1 = Parameter(Type::Any(), 1);
  Node* feedback = UndefinedConstant();
  Node* call =
      graph()->NewNode(Call(2), function, UndefinedConstant(), p0, p1, feedback,
                       context, frame_state, effect, control);
  Reduction r = Reduce(call);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsJSParseInt(p0, p1));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
