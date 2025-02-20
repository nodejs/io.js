// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/script-details.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/schedule.h"
#include "src/compiler/turbofan-graph.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/zone/zone.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

static Operator dummy_operator(IrOpcode::kParameter, Operator::kNoWrite,
                               "dummy", 0, 0, 0, 0, 0, 0);

// So we can get a real JS function.
static Handle<JSFunction> Compile(const char* source) {
  Isolate* isolate = CcTest::i_isolate();
  Handle<String> source_code = isolate->factory()
                                   ->NewStringFromUtf8(base::CStrVector(source))
                                   .ToHandleChecked();
  ScriptCompiler::CompilationDetails compilation_details;
  DirectHandle<SharedFunctionInfo> shared =
      Compiler::GetSharedFunctionInfoForScript(
          isolate, source_code, ScriptDetails(),
          v8::ScriptCompiler::kNoCompileOptions,
          ScriptCompiler::kNoCacheNoReason, NOT_NATIVES_CODE,
          &compilation_details)
          .ToHandleChecked();
  return Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
      .Build();
}


TEST(TestLinkageCreate) {
  HandleAndZoneScope handles;
  Handle<JSFunction> function = Compile("a + b");
  Handle<SharedFunctionInfo> shared(function->shared(), handles.main_isolate());
  OptimizedCompilationInfo info(handles.main_zone(), function->GetIsolate(),
                                shared, function, CodeKind::TURBOFAN_JS);
  auto call_descriptor = Linkage::ComputeIncoming(info.zone(), &info);
  CHECK(call_descriptor);
}


TEST(TestLinkageJSFunctionIncoming) {
  const char* sources[] = {"(function() { })", "(function(a) { })",
                           "(function(a,b) { })", "(function(a,b,c) { })"};

  for (int i = 0; i < 3; i++) {
    HandleAndZoneScope handles;
    Handle<JSFunction> function = Cast<JSFunction>(v8::Utils::OpenHandle(
        *v8::Local<v8::Function>::Cast(CompileRun(sources[i]))));
    Handle<SharedFunctionInfo> shared(function->shared(),
                                      handles.main_isolate());
    OptimizedCompilationInfo info(handles.main_zone(), function->GetIsolate(),
                                  shared, function, CodeKind::TURBOFAN_JS);
    auto call_descriptor = Linkage::ComputeIncoming(info.zone(), &info);
    CHECK(call_descriptor);

    CHECK_EQ(1 + i, static_cast<int>(call_descriptor->JSParameterCount()));
    CHECK_EQ(1, static_cast<int>(call_descriptor->ReturnCount()));
    CHECK_EQ(Operator::kNoProperties, call_descriptor->properties());
    CHECK_EQ(true, call_descriptor->IsJSFunctionCall());
  }
}


TEST(TestLinkageJSCall) {
  HandleAndZoneScope handles;
  Handle<JSFunction> function = Compile("a + c");
  Handle<SharedFunctionInfo> shared(function->shared(), handles.main_isolate());
  OptimizedCompilationInfo info(handles.main_zone(), function->GetIsolate(),
                                shared, function, CodeKind::TURBOFAN_JS);

  for (int i = 0; i < 32; i++) {
    auto call_descriptor = Linkage::GetJSCallDescriptor(
        info.zone(), false, i, CallDescriptor::kNoFlags);
    CHECK(call_descriptor);
    CHECK_EQ(i, static_cast<int>(call_descriptor->JSParameterCount()));
    CHECK_EQ(1, static_cast<int>(call_descriptor->ReturnCount()));
    CHECK_EQ(Operator::kNoProperties, call_descriptor->properties());
    CHECK_EQ(true, call_descriptor->IsJSFunctionCall());
  }
}


TEST(TestLinkageRuntimeCall) {
  // TODO(titzer): test linkage creation for outgoing runtime calls.
}


TEST(TestLinkageStubCall) {
  // TODO(bbudge) Add tests for FP registers.
  Isolate* isolate = CcTest::InitIsolateOnce();
  Zone zone(isolate->allocator(), ZONE_NAME);
  Callable callable = Builtins::CallableFor(isolate, Builtin::kToNumber);
  OptimizedCompilationInfo info(base::ArrayVector("test"), &zone,
                                CodeKind::FOR_TESTING);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      &zone, callable.descriptor(), 0, CallDescriptor::kNoFlags,
      Operator::kNoProperties);
  CHECK(call_descriptor);
  CHECK_EQ(0, static_cast<int>(call_descriptor->ParameterSlotCount()));
  CHECK_EQ(1, static_cast<int>(call_descriptor->ReturnCount()));
  CHECK_EQ(Operator::kNoProperties, call_descriptor->properties());
  CHECK_EQ(false, call_descriptor->IsJSFunctionCall());

  CHECK_EQ(call_descriptor->GetParameterType(0), MachineType::AnyTagged());
  CHECK_EQ(call_descriptor->GetReturnType(0), MachineType::AnyTagged());
  // TODO(titzer): test linkage creation for outgoing stub calls.
}

#if V8_ENABLE_WEBASSEMBLY
TEST(TestFPLinkageStubCall) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  Zone zone(isolate->allocator(), ZONE_NAME);
  Callable callable =
      Builtins::CallableFor(isolate, Builtin::kWasmFloat64ToNumber);
  OptimizedCompilationInfo info(base::ArrayVector("test"), &zone,
                                CodeKind::FOR_TESTING);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      &zone, callable.descriptor(), 0, CallDescriptor::kNoFlags,
      Operator::kNoProperties);
  CHECK(call_descriptor);
  CHECK_EQ(0, static_cast<int>(call_descriptor->ParameterSlotCount()));
  CHECK_EQ(1, static_cast<int>(call_descriptor->ParameterCount()));
  CHECK_EQ(1, static_cast<int>(call_descriptor->ReturnCount()));
  CHECK_EQ(Operator::kNoProperties, call_descriptor->properties());
  CHECK_EQ(false, call_descriptor->IsJSFunctionCall());

  CHECK_EQ(call_descriptor->GetInputType(1), MachineType::Float64());
  CHECK(call_descriptor->GetInputLocation(1).IsRegister());
  CHECK_EQ(call_descriptor->GetReturnType(0), MachineType::AnyTagged());
  CHECK(call_descriptor->GetReturnLocation(0).IsRegister());
  CHECK_EQ(call_descriptor->GetReturnLocation(0).GetLocation(),
           kReturnRegister0.code());
}
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace compiler
}  // namespace internal
}  // namespace v8
