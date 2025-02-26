// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-inl.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/execution/isolate.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-function.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/common/code-assembler-tester.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

template <class T>
using TVariable = TypedCodeAssemblerVariable<T>;

TNode<Smi> SmiTag(CodeAssembler* m, TNode<IntPtrT> value) {
  int32_t constant_value;
  if (m->TryToInt32Constant(value, &constant_value) &&
      Smi::IsValid(constant_value)) {
    return m->SmiConstant(Smi::FromInt(constant_value));
  }
  return m->BitcastWordToTaggedSigned(
      m->WordShl(value, m->IntPtrConstant(kSmiShiftSize + kSmiTagSize)));
}

Node* UndefinedConstant(CodeAssembler* m) {
  return m->LoadRoot(RootIndex::kUndefinedValue);
}

Node* LoadObjectField(CodeAssembler* m, Node* object, int offset,
                      MachineType type = MachineType::AnyTagged()) {
  return m->Load(type, object, m->IntPtrConstant(offset - kHeapObjectTag));
}

Node* LoadMap(CodeAssembler* m, Node* object) {
  return LoadObjectField(m, object, JSObject::kMapOffset);
}

}  // namespace

TEST(SimpleSmiReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  m.Return(SmiTag(&m, m.IntPtrConstant(37)));
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK_EQ(37, (*ft.CallChecked<Smi>()).value());
}

TEST(SimpleIntPtrReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  int test;
  m.Return(m.BitcastWordToTagged(
      m.IntPtrConstant(reinterpret_cast<intptr_t>(&test))));
  FunctionTester ft(asm_tester.GenerateCode());
  MaybeDirectHandle<Object> result = ft.Call();
  CHECK_EQ(reinterpret_cast<Address>(&test), (*result.ToHandleChecked()).ptr());
}

TEST(SimpleDoubleReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  m.Return(m.NumberConstant(0.5));
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK_EQ(0.5, ft.CallChecked<HeapNumber>()->value());
}

TEST(SimpleCallRuntime1Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TNode<Context> context =
      m.HeapConstantNoHole(Handle<Context>(isolate->native_context()));
  TNode<Smi> b = SmiTag(&m, m.IntPtrConstant(0));
  m.Return(m.CallRuntime(Runtime::kIsSmi, context, b));
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK(ft.CallChecked<Boolean>().is_identical_to(
      isolate->factory()->true_value()));
}

TEST(SimpleTailCallRuntime1Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TNode<Context> context =
      m.HeapConstantNoHole(Handle<Context>(isolate->native_context()));
  TNode<Smi> b = SmiTag(&m, m.IntPtrConstant(0));
  m.TailCallRuntime(Runtime::kIsSmi, context, b);
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK(ft.CallChecked<Boolean>().is_identical_to(
      isolate->factory()->true_value()));
}

TEST(SimpleCallRuntime2Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TNode<Context> context =
      m.HeapConstantNoHole(Handle<Context>(isolate->native_context()));
  TNode<Smi> a = SmiTag(&m, m.IntPtrConstant(2));
  TNode<Smi> b = SmiTag(&m, m.IntPtrConstant(4));
  m.Return(m.CallRuntime(Runtime::kAdd, context, a, b));
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK_EQ(6, (*ft.CallChecked<Smi>()).value());
}

TEST(SimpleTailCallRuntime2Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TNode<Context> context =
      m.HeapConstantNoHole(Handle<Context>(isolate->native_context()));
  TNode<Smi> a = SmiTag(&m, m.IntPtrConstant(2));
  TNode<Smi> b = SmiTag(&m, m.IntPtrConstant(4));
  m.TailCallRuntime(Runtime::kAdd, context, a, b);
  FunctionTester ft(asm_tester.GenerateCode());
  CHECK_EQ(6, (*ft.CallChecked<Smi>()).value());
}

namespace {

Handle<JSFunction> CreateSumAllArgumentsFunction(FunctionTester* ft) {
  const char* source =
      "(function() {\n"
      "  var sum = 0 + this;\n"
      "  for (var i = 0; i < arguments.length; i++) {\n"
      "    sum += arguments[i];\n"
      "  }\n"
      "  return sum;\n"
      "})";
  return ft->NewFunction(source);
}

}  // namespace

TEST(SimpleCallJSFunction0Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 1;
  CodeAssemblerTester asm_tester(isolate, JSParameterCount(kNumParams));
  CodeAssembler m(asm_tester.state());
  {
    auto function = m.Parameter<JSFunction>(1);
    auto context = m.GetJSContextParameter();

    auto receiver = SmiTag(&m, m.IntPtrConstant(42));

    TNode<Object> result =
        m.CallJS(Builtins::Call(), context, function, receiver);
    m.Return(result);
  }
  FunctionTester ft(asm_tester.GenerateCode(), kNumParams);

  Handle<JSFunction> sum = CreateSumAllArgumentsFunction(&ft);
  MaybeDirectHandle<Object> result = ft.Call(sum);
  CHECK_EQ(Smi::FromInt(42), *result.ToHandleChecked());
}

TEST(SimpleCallJSFunction1Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 1;
  CodeAssemblerTester asm_tester(isolate, JSParameterCount(kNumParams));
  CodeAssembler m(asm_tester.state());
  {
    auto function = m.Parameter<JSFunction>(1);
    auto context = m.GetJSContextParameter();

    auto receiver = SmiTag(&m, m.IntPtrConstant(42));
    auto a = SmiTag(&m, m.IntPtrConstant(13));

    TNode<Object> result =
        m.CallJS(Builtins::Call(), context, function, receiver, a);
    m.Return(result);
  }
  FunctionTester ft(asm_tester.GenerateCode(), kNumParams);

  Handle<JSFunction> sum = CreateSumAllArgumentsFunction(&ft);
  MaybeDirectHandle<Object> result = ft.Call(sum);
  CHECK_EQ(Smi::FromInt(55), *result.ToHandleChecked());
}

TEST(SimpleCallJSFunction2Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 2;
  CodeAssemblerTester asm_tester(isolate, JSParameterCount(kNumParams));
  CodeAssembler m(asm_tester.state());
  {
    auto function = m.Parameter<JSFunction>(1);
    auto context = m.GetJSContextParameter();

    auto receiver = SmiTag(&m, m.IntPtrConstant(42));
    auto a = SmiTag(&m, m.IntPtrConstant(13));
    auto b = SmiTag(&m, m.IntPtrConstant(153));

    TNode<Object> result =
        m.CallJS(Builtins::Call(), context, function, receiver, a, b);
    m.Return(result);
  }
  FunctionTester ft(asm_tester.GenerateCode(), kNumParams);

  Handle<JSFunction> sum = CreateSumAllArgumentsFunction(&ft);
  MaybeDirectHandle<Object> result = ft.Call(sum);
  CHECK_EQ(Smi::FromInt(208), *result.ToHandleChecked());
}

TEST(VariableMerge1) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TVariable<Int32T> var1(&m);
  CodeAssemblerLabel l1(&m), l2(&m), merge(&m);
  TNode<Int32T> temp = m.Int32Constant(0);
  var1 = temp;
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_EQ(var1.value(), temp);
}

TEST(VariableMerge2) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TVariable<Int32T> var1(&m);
  CodeAssemblerLabel l1(&m), l2(&m), merge(&m);
  TNode<Int32T> temp = m.Int32Constant(0);
  var1 = temp;
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  TNode<Int32T> temp2 = m.Int32Constant(2);
  var1 = temp2;
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_NE(var1.value(), temp);
}

TEST(VariableMerge3) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TVariable<Int32T> var1(&m);
  TVariable<Int32T> var2(&m);
  CodeAssemblerLabel l1(&m), l2(&m), merge(&m);
  TNode<Int32T> temp = m.Int32Constant(0);
  var1 = temp;
  var2 = temp;
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  TNode<Int32T> temp2 = m.Int32Constant(2);
  var1 = temp2;
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_NE(var1.value(), temp);
  CHECK_NE(var1.value(), temp2);
  CHECK_EQ(var2.value(), temp);
}

TEST(VariableMergeBindFirst) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TVariable<Int32T> var1(&m);
  CodeAssemblerLabel l1(&m), l2(&m), merge(&m, &var1), end(&m);
  TNode<Int32T> temp = m.Int32Constant(0);
  var1 = temp;
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK(var1.value() != temp);
  CHECK_NOT_NULL(var1.value());
  m.Goto(&end);
  m.Bind(&l2);
  TNode<Int32T> temp2 = m.Int32Constant(2);
  var1 = temp2;
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&end);
  CHECK(var1.value() != temp);
  CHECK_NOT_NULL(var1.value());
}

TEST(VariableMergeSwitch) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  TVariable<Smi> var1(&m);
  CodeAssemblerLabel l1(&m), l2(&m), default_label(&m);
  CodeAssemblerLabel* labels[] = {&l1, &l2};
  int32_t values[] = {1, 2};
  TNode<Smi> temp1 = m.SmiConstant(0);
  var1 = temp1;
  m.Switch(m.Int32Constant(2), &default_label, values, labels, 2);
  m.Bind(&l1);
  CHECK_EQ(temp1, var1.value());
  m.Return(temp1);
  m.Bind(&l2);
  CHECK_EQ(temp1, var1.value());
  TNode<Smi> temp2 = m.SmiConstant(7);
  var1 = temp2;
  m.Goto(&default_label);
  m.Bind(&default_label);
  CHECK_EQ(IrOpcode::kPhi, (*var1.value()).opcode());
  CHECK_EQ(2, (*var1.value()).op()->ValueInputCount());
  CHECK_EQ(temp1, NodeProperties::GetValueInput(var1.value(), 0));
  CHECK_EQ(temp2, NodeProperties::GetValueInput(var1.value(), 1));
  m.Return(temp1);
}

TEST(SplitEdgeBranchMerge) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  CodeAssemblerLabel l1(&m), merge(&m);
  m.Branch(m.Int32Constant(1), &l1, &merge);
  m.Bind(&l1);
  m.Goto(&merge);
  m.Bind(&merge);
  USE(asm_tester.GenerateCode());
}

TEST(SplitEdgeSwitchMerge) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  CodeAssemblerLabel l1(&m), l2(&m), l3(&m), default_label(&m);
  CodeAssemblerLabel* labels[] = {&l1, &l2};
  int32_t values[] = {1, 2};
  m.Branch(m.Int32Constant(1), &l3, &l1);
  m.Bind(&l3);
  m.Switch(m.Int32Constant(2), &default_label, values, labels, 2);
  m.Bind(&l1);
  m.Goto(&l2);
  m.Bind(&l2);
  m.Goto(&default_label);
  m.Bind(&default_label);
  USE(asm_tester.GenerateCode());
}

TEST(TestToConstant) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  int32_t value32;
  int64_t value64;
  TNode<Int32T> a = m.Int32Constant(5);
  CHECK(m.TryToInt32Constant(a, &value32));
  CHECK(m.TryToInt64Constant(a, &value64));

  TNode<Int64T> b = m.Int64Constant(static_cast<int64_t>(1) << 32);
  CHECK(!m.TryToInt32Constant(b, &value32));
  CHECK(m.TryToInt64Constant(b, &value64));

  b = m.Int64Constant(13);
  CHECK(m.TryToInt32Constant(b, &value32));
  CHECK(m.TryToInt64Constant(b, &value64));

  TNode<Int32T> c = m.Word32Shl(m.Int32Constant(13), m.Int32Constant(14));
  CHECK(!m.TryToInt32Constant(c, &value32));
  CHECK(!m.TryToInt64Constant(c, &value64));

  TNode<IntPtrT> d = m.ReinterpretCast<IntPtrT>(UndefinedConstant(&m));
  CHECK(!m.TryToInt32Constant(d, &value32));
  CHECK(!m.TryToInt64Constant(d, &value64));
}

TEST(DeferredCodePhiHints) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  CodeAssemblerLabel block1(&m, CodeAssemblerLabel::kDeferred);
  m.Goto(&block1);
  m.Bind(&block1);
  {
    TVariable<Map> var_object(&m);
    CodeAssemblerLabel loop(&m, &var_object);
    var_object = m.CAST(LoadMap(&m, m.SmiConstant(0)));
    m.Goto(&loop);
    m.Bind(&loop);
    {
      TNode<Map> map = m.CAST(LoadMap(&m, var_object.value()));
      var_object = map;
      m.Goto(&loop);
    }
  }
  CHECK(!asm_tester.GenerateCode().is_null());
}

TEST(TestOutOfScopeVariable) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  CodeAssemblerLabel block1(&m);
  CodeAssemblerLabel block2(&m);
  CodeAssemblerLabel block3(&m);
  CodeAssemblerLabel block4(&m);
  m.Branch(m.WordEqual(m.BitcastTaggedToWordForTagAndSmiBits(
                           m.UncheckedParameter<AnyTaggedT>(0)),
                       m.IntPtrConstant(0)),
           &block1, &block4);
  m.Bind(&block4);
  {
    TVariable<IntPtrT> var_object(&m);
    m.Branch(m.WordEqual(m.BitcastTaggedToWordForTagAndSmiBits(
                             m.UncheckedParameter<AnyTaggedT>(0)),
                         m.IntPtrConstant(0)),
             &block2, &block3);

    m.Bind(&block2);
    var_object = m.IntPtrConstant(55);
    m.Goto(&block1);

    m.Bind(&block3);
    var_object = m.IntPtrConstant(66);
    m.Goto(&block1);
  }
  m.Bind(&block1);
  CHECK(!asm_tester.GenerateCode().is_null());
}

TEST(ExceptionHandler) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 0;
  CodeAssemblerTester asm_tester(isolate, JSParameterCount(kNumParams));
  CodeAssembler m(asm_tester.state());

  TVariable<Object> var(m.SmiConstant(0), &m);
  CodeAssemblerLabel exception(&m, {&var}, CodeAssemblerLabel::kDeferred);
  {
    ScopedExceptionHandler handler(&m, &exception, &var);
    TNode<Context> context =
        m.HeapConstantNoHole(Handle<Context>(isolate->native_context()));
    m.CallRuntime(Runtime::kThrow, context, m.SmiConstant(2));
  }
  m.Return(m.SmiConstant(1));

  m.Bind(&exception);
  m.Return(var.value());

  FunctionTester ft(asm_tester.GenerateCode(), kNumParams);
  CHECK_EQ(2, (*ft.CallChecked<Smi>()).value());
}

TEST(TestCodeAssemblerCodeComment) {
#ifdef V8_CODE_COMMENTS
  i::v8_flags.code_comments = true;
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int kNumParams = 0;
  CodeAssemblerTester asm_tester(isolate, JSParameterCount(kNumParams));
  CodeAssembler m(asm_tester.state());

  m.Comment("Comment1");
  m.Return(m.SmiConstant(1));

  DirectHandle<Code> code = asm_tester.GenerateCode();
  CHECK_NE(code->code_comments(), kNullAddress);
  CodeCommentsIterator it(code->code_comments(), code->code_comments_size());
  CHECK(it.HasCurrent());
  bool found_comment = false;
  while (it.HasCurrent()) {
    if (strncmp(it.GetComment(), "Comment1", strlen("Comment1")) == 0) {
      found_comment = true;
    }
    it.Next();
  }
  CHECK(found_comment);
#endif  // V8_CODE_COMMENTS
}

TEST(StaticAssert) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeAssemblerTester asm_tester(isolate);
  CodeAssembler m(asm_tester.state());
  m.StaticAssert(m.ReinterpretCast<BoolT>(m.Int32Constant(1)));
  USE(asm_tester.GenerateCode());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
