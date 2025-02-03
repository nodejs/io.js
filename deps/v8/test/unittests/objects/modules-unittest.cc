// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "src/flags/flags.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using ModuleTest = v8::TestWithContext;

using v8::Context;
using v8::Data;
using v8::FixedArray;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::Location;
using v8::MaybeLocal;
using v8::Module;
using v8::ModuleRequest;
using v8::Promise;
using v8::ScriptCompiler;
using v8::ScriptOrigin;
using v8::String;
using v8::Value;

ScriptOrigin ModuleOrigin(Local<v8::Value> resource_name, Isolate* isolate) {
  ScriptOrigin origin(resource_name, 0, 0, false, -1, Local<v8::Value>(), false,
                      false, true);
  return origin;
}

static v8::Global<Module> dep1_global;
static v8::Global<Module> dep2_global;
MaybeLocal<Module> ResolveCallback(Local<Context> context,
                                   Local<String> specifier,
                                   Local<FixedArray> import_attributes,
                                   Local<Module> referrer) {
  CHECK_EQ(0, import_attributes->Length());
  Isolate* isolate = context->GetIsolate();
  if (specifier->StrictEquals(
          String::NewFromUtf8(isolate, "./dep1.js").ToLocalChecked())) {
    return dep1_global.Get(isolate);
  } else if (specifier->StrictEquals(
                 String::NewFromUtf8(isolate, "./dep2.js").ToLocalChecked())) {
    return dep2_global.Get(isolate);
  } else {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, "boom").ToLocalChecked());
    return MaybeLocal<Module>();
  }
}

TEST_F(ModuleTest, ModuleInstantiationFailures1) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<Module> module;
  {
    Local<String> source_text = NewString(
        "import './foo.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    Local<FixedArray> module_requests = module->GetModuleRequests();
    CHECK_EQ(2, module_requests->Length());
    Local<ModuleRequest> module_request_0 =
        module_requests->Get(context(), 0).As<ModuleRequest>();
    CHECK(
        NewString("./foo.js")->StrictEquals(module_request_0->GetSpecifier()));
    int offset = module_request_0->GetSourceOffset();
    CHECK_EQ(7, offset);
    Location loc = module->SourceOffsetToLocation(offset);
    CHECK_EQ(0, loc.GetLineNumber());
    CHECK_EQ(7, loc.GetColumnNumber());
    CHECK_EQ(0, module_request_0->GetImportAttributes()->Length());

    Local<ModuleRequest> module_request_1 =
        module_requests->Get(context(), 1).As<ModuleRequest>();
    CHECK(
        NewString("./bar.js")->StrictEquals(module_request_1->GetSpecifier()));
    offset = module_request_1->GetSourceOffset();
    CHECK_EQ(34, offset);
    loc = module->SourceOffsetToLocation(offset);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(15, loc.GetColumnNumber());
    CHECK_EQ(0, module_request_1->GetImportAttributes()->Length());
  }

  // Instantiation should fail.
  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(module->InstantiateModule(context(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(NewString("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  // Start over again...
  {
    Local<String> source_text = NewString(
        "import './dep1.js';\n"
        "export {} from './bar.js';");
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  }

  // dep1.js
  {
    Local<String> source_text = NewString("");
    ScriptOrigin origin = ModuleOrigin(NewString("dep1.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> dep1 =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    dep1_global.Reset(isolate(), dep1);
  }

  // Instantiation should fail because a sub-module fails to resolve.
  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(module->InstantiateModule(context(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(NewString("boom")));
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  }

  CHECK(!try_catch.HasCaught());

  dep1_global.Reset();
}

static v8::Global<Module> fooModule_global;
static v8::Global<Module> barModule_global;
MaybeLocal<Module> ResolveCallbackWithImportAttributes(
    Local<Context> context, Local<String> specifier,
    Local<FixedArray> import_attributes, Local<Module> referrer) {
  Isolate* isolate = context->GetIsolate();
  if (specifier->StrictEquals(
          String::NewFromUtf8(isolate, "./foo.js").ToLocalChecked())) {
    CHECK_EQ(0, import_attributes->Length());

    return fooModule_global.Get(isolate);
  } else if (specifier->StrictEquals(
                 String::NewFromUtf8(isolate, "./bar.js").ToLocalChecked())) {
    CHECK_EQ(3, import_attributes->Length());
    Local<String> assertion_key =
        import_attributes->Get(context, 0).As<Value>().As<String>();
    CHECK(String::NewFromUtf8(isolate, "a")
              .ToLocalChecked()
              ->StrictEquals(assertion_key));
    Local<String> assertion_value =
        import_attributes->Get(context, 1).As<Value>().As<String>();
    CHECK(String::NewFromUtf8(isolate, "b")
              .ToLocalChecked()
              ->StrictEquals(assertion_value));
    Local<Data> assertion_source_offset_object =
        import_attributes->Get(context, 2);
    Local<Int32> assertion_source_offset_int32 =
        assertion_source_offset_object.As<Value>()
            ->ToInt32(context)
            .ToLocalChecked();
    int32_t assertion_source_offset = assertion_source_offset_int32->Value();
    CHECK_EQ(65, assertion_source_offset);
    Location loc = referrer->SourceOffsetToLocation(assertion_source_offset);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(35, loc.GetColumnNumber());

    return barModule_global.Get(isolate);
  } else {
    isolate->ThrowException(
        String::NewFromUtf8(isolate, "boom").ToLocalChecked());
    return MaybeLocal<Module>();
  }
}

TEST_F(ModuleTest, ModuleInstantiationWithImportAssertions) {
  bool prev_import_assertions = i::v8_flags.harmony_import_assertions;
  i::v8_flags.harmony_import_assertions = true;
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<Module> module;
  {
    Local<String> source_text = NewString(
        "import './foo.js' assert { };\n"
        "export {} from './bar.js' assert { a: 'b' };");
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    module = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    Local<FixedArray> module_requests = module->GetModuleRequests();
    CHECK_EQ(2, module_requests->Length());
    Local<ModuleRequest> module_request_0 =
        module_requests->Get(context(), 0).As<ModuleRequest>();
    CHECK(
        NewString("./foo.js")->StrictEquals(module_request_0->GetSpecifier()));
    int offset = module_request_0->GetSourceOffset();
    CHECK_EQ(7, offset);
    Location loc = module->SourceOffsetToLocation(offset);
    CHECK_EQ(0, loc.GetLineNumber());
    CHECK_EQ(7, loc.GetColumnNumber());
    CHECK_EQ(0, module_request_0->GetImportAttributes()->Length());

    Local<ModuleRequest> module_request_1 =
        module_requests->Get(context(), 1).As<ModuleRequest>();
    CHECK(
        NewString("./bar.js")->StrictEquals(module_request_1->GetSpecifier()));
    offset = module_request_1->GetSourceOffset();
    CHECK_EQ(45, offset);
    loc = module->SourceOffsetToLocation(offset);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(15, loc.GetColumnNumber());

    Local<FixedArray> import_attributes_1 =
        module_request_1->GetImportAttributes();
    CHECK_EQ(3, import_attributes_1->Length());
    Local<String> assertion_key =
        import_attributes_1->Get(context(), 0).As<String>();
    CHECK(NewString("a")->StrictEquals(assertion_key));
    Local<String> assertion_value =
        import_attributes_1->Get(context(), 1).As<String>();
    CHECK(NewString("b")->StrictEquals(assertion_value));
    int32_t assertion_source_offset =
        import_attributes_1->Get(context(), 2).As<Int32>()->Value();
    CHECK_EQ(65, assertion_source_offset);
    loc = module->SourceOffsetToLocation(assertion_source_offset);
    CHECK_EQ(1, loc.GetLineNumber());
    CHECK_EQ(35, loc.GetColumnNumber());
  }

  // foo.js
  {
    Local<String> source_text = NewString("Object.expando = 40");
    ScriptOrigin origin = ModuleOrigin(NewString("foo.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> fooModule =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    fooModule_global.Reset(isolate(), fooModule);
  }

  // bar.js
  {
    Local<String> source_text = NewString("Object.expando += 2");
    ScriptOrigin origin = ModuleOrigin(NewString("bar.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> barModule =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    barModule_global.Reset(isolate(), barModule);
  }

  CHECK(
      module->InstantiateModule(context(), ResolveCallbackWithImportAttributes)
          .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  MaybeLocal<Value> result = module->Evaluate(context());
  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  Local<Promise> promise = Local<Promise>::Cast(result.ToLocalChecked());
  CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
  CHECK(promise->Result()->IsUndefined());

  // TODO(v8:12781): One IsInt32 matcher be added in
  // gmock-support.h, we could use IsInt32 to replace
  // this.
  {
    Local<Value> result = RunJS("Object.expando");
    CHECK(result->IsInt32());
    CHECK_EQ(42, result->Int32Value(context()).FromJust());
  }
  CHECK(!try_catch.HasCaught());
  i::v8_flags.harmony_import_assertions = prev_import_assertions;

  fooModule_global.Reset();
  barModule_global.Reset();
}

TEST_F(ModuleTest, ModuleInstantiationFailures2) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  // root1.js
  Local<Module> root;
  {
    Local<String> source_text =
        NewString("import './dep1.js'; import './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("root1.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    root = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  }

  // dep1.js
  Local<Module> dep1;
  {
    Local<String> source_text = NewString("export let x = 42");
    ScriptOrigin origin = ModuleOrigin(NewString("dep1.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep1 = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    dep1_global.Reset(isolate(), dep1);
  }

  // dep2.js
  Local<Module> dep2;
  {
    Local<String> source_text = NewString("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("dep2.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    dep2_global.Reset(isolate(), dep2);
  }

  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(root->InstantiateModule(context(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(NewString("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js
  {
    Local<String> source_text = NewString("import {foo} from './dep2.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("dep2.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    dep2_global.Reset(isolate(), dep2);
  }

  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(root->InstantiateModule(context(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(!inner_try_catch.Exception()->StrictEquals(NewString("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  // Change dep2.js again
  {
    Local<String> source_text = NewString("import {foo} from './dep3.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("dep2.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    dep2 = ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    dep2_global.Reset(isolate(), dep2);
  }

  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(root->InstantiateModule(context(), ResolveCallback).IsNothing());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()->StrictEquals(NewString("boom")));
    CHECK_EQ(Module::kUninstantiated, root->GetStatus());
    CHECK_EQ(Module::kInstantiated, dep1->GetStatus());
    CHECK_EQ(Module::kUninstantiated, dep2->GetStatus());
  }

  dep1_global.Reset();
  dep2_global.Reset();
}

static MaybeLocal<Module> CompileSpecifierAsModuleResolveCallback(
    Local<Context> context, Local<String> specifier,
    Local<FixedArray> import_attributes, Local<Module> referrer) {
  CHECK_EQ(0, import_attributes->Length());
  Isolate* isolate = context->GetIsolate();
  ScriptOrigin origin = ModuleOrigin(
      String::NewFromUtf8(isolate, "module.js").ToLocalChecked(), isolate);
  ScriptCompiler::Source source(specifier, origin);
  return ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
}

TEST_F(ModuleTest, ModuleEvaluation) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<String> source_text = NewString(
      "import 'Object.expando = 5';"
      "import 'Object.expando *= 2';");
  ScriptOrigin origin = ModuleOrigin(
      String::NewFromUtf8(isolate(), "file.js").ToLocalChecked(), isolate());

  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  MaybeLocal<Value> result = module->Evaluate(context());
  CHECK_EQ(Module::kEvaluated, module->GetStatus());
  Local<Promise> promise = result.ToLocalChecked().As<Promise>();
  CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
  CHECK(promise->Result()->IsUndefined());
  // TODO(v8:12781): One IsInt32 matcher be added in
  // gmock-support.h, we could use IsInt32 to replace
  // this.
  {
    Local<Value> result = RunJS("Object.expando");
    CHECK(result->IsInt32());
    CHECK_EQ(10, result->Int32Value(context()).FromJust());
  }
  CHECK(!try_catch.HasCaught());
}

TEST_F(ModuleTest, ModuleEvaluationError1) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<String> source_text =
      NewString("Object.x = (Object.x || 0) + 1; throw 'boom';");
  ScriptOrigin origin = ModuleOrigin(
      String::NewFromUtf8(isolate(), "file.js").ToLocalChecked(), isolate());

  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());

  {
    v8::TryCatch inner_try_catch(isolate());
    MaybeLocal<Value> result = module->Evaluate(context());
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(NewString("boom")));
    // TODO(v8:12781): One IsInt32 matcher be added in
    // gmock-support.h, we could use IsInt32 to replace
    // this.
    {
      Local<Value> result = RunJS("Object.x");
      CHECK(result->IsInt32());
      CHECK_EQ(1, result->Int32Value(context()).FromJust());
    }
    // With top level await, we do not throw and errored evaluation returns
    // a rejected promise with the exception.
    CHECK(!inner_try_catch.HasCaught());
    Local<Promise> promise = result.ToLocalChecked().As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK_EQ(promise->Result(), module->GetException());
  }

  {
    v8::TryCatch inner_try_catch(isolate());
    MaybeLocal<Value> result = module->Evaluate(context());
    CHECK_EQ(Module::kErrored, module->GetStatus());
    Local<Value> exception = module->GetException();
    CHECK(exception->StrictEquals(NewString("boom")));
    // TODO(v8:12781): One IsInt32 matcher be added in
    // gmock-support.h, we could use IsInt32 to replace
    // this.
    {
      Local<Value> result = RunJS("Object.x");
      CHECK(result->IsInt32());
      CHECK_EQ(1, result->Int32Value(context()).FromJust());
    }

    // With top level await, we do not throw and errored evaluation returns
    // a rejected promise with the exception.
    CHECK(!inner_try_catch.HasCaught());
    Local<Promise> promise = result.ToLocalChecked().As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK_EQ(promise->Result(), module->GetException());
  }

  CHECK(!try_catch.HasCaught());
}

static v8::Global<Module> failure_module_global;
static v8::Global<Module> dependent_module_global;
MaybeLocal<Module> ResolveCallbackForModuleEvaluationError2(
    Local<Context> context, Local<String> specifier,
    Local<FixedArray> import_attributes, Local<Module> referrer) {
  CHECK_EQ(0, import_attributes->Length());
  Isolate* isolate = context->GetIsolate();
  if (specifier->StrictEquals(
          String::NewFromUtf8(isolate, "./failure.js").ToLocalChecked())) {
    return failure_module_global.Get(isolate);
  } else {
    CHECK(specifier->StrictEquals(
        String::NewFromUtf8(isolate, "./dependent.js").ToLocalChecked()));
    return dependent_module_global.Get(isolate);
  }
}

TEST_F(ModuleTest, ModuleEvaluationError2) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<String> failure_text = NewString("throw 'boom';");
  ScriptOrigin failure_origin =
      ModuleOrigin(NewString("failure.js"), isolate());
  ScriptCompiler::Source failure_source(failure_text, failure_origin);
  Local<Module> failure_module =
      ScriptCompiler::CompileModule(isolate(), &failure_source)
          .ToLocalChecked();
  failure_module_global.Reset(isolate(), failure_module);
  CHECK_EQ(Module::kUninstantiated, failure_module->GetStatus());
  CHECK(failure_module
            ->InstantiateModule(context(),
                                ResolveCallbackForModuleEvaluationError2)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, failure_module->GetStatus());

  {
    v8::TryCatch inner_try_catch(isolate());
    MaybeLocal<Value> result = failure_module->Evaluate(context());
    CHECK_EQ(Module::kErrored, failure_module->GetStatus());
    Local<Value> exception = failure_module->GetException();
    CHECK(exception->StrictEquals(NewString("boom")));

    // With top level await, we do not throw and errored evaluation returns
    // a rejected promise with the exception.
    CHECK(!inner_try_catch.HasCaught());
    Local<Promise> promise = result.ToLocalChecked().As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK_EQ(promise->Result(), failure_module->GetException());
  }

  Local<String> dependent_text =
      NewString("import './failure.js'; export const c = 123;");
  ScriptOrigin dependent_origin =
      ModuleOrigin(NewString("dependent.js"), isolate());
  ScriptCompiler::Source dependent_source(dependent_text, dependent_origin);
  Local<Module> dependent_module =
      ScriptCompiler::CompileModule(isolate(), &dependent_source)
          .ToLocalChecked();
  dependent_module_global.Reset(isolate(), dependent_module);
  CHECK_EQ(Module::kUninstantiated, dependent_module->GetStatus());
  CHECK(dependent_module
            ->InstantiateModule(context(),
                                ResolveCallbackForModuleEvaluationError2)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, dependent_module->GetStatus());

  {
    v8::TryCatch inner_try_catch(isolate());
    MaybeLocal<Value> result = dependent_module->Evaluate(context());
    CHECK_EQ(Module::kErrored, dependent_module->GetStatus());
    Local<Value> exception = dependent_module->GetException();
    CHECK(exception->StrictEquals(NewString("boom")));
    CHECK_EQ(exception, failure_module->GetException());

    // With top level await, we do not throw and errored evaluation returns
    // a rejected promise with the exception.
    CHECK(!inner_try_catch.HasCaught());
    Local<Promise> promise = result.ToLocalChecked().As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK_EQ(promise->Result(), failure_module->GetException());
    CHECK(failure_module->GetException()->StrictEquals(NewString("boom")));
  }

  CHECK(!try_catch.HasCaught());

  failure_module_global.Reset();
  dependent_module_global.Reset();
}

TEST_F(ModuleTest, ModuleEvaluationCompletion1) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  const char* sources[] = {
      "",
      "var a = 1",
      "import '42'",
      "export * from '42'",
      "export {} from '42'",
      "export {}",
      "var a = 1; export {a}",
      "export function foo() {}",
      "export class C extends null {}",
      "export let a = 1",
      "export default 1",
      "export default function foo() {}",
      "export default function () {}",
      "export default (function () {})",
      "export default class C extends null {}",
      "export default (class C extends null {})",
      "for (var i = 0; i < 5; ++i) {}",
  };

  for (auto src : sources) {
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());

    // Evaluate twice.
    Local<Value> result_1 = module->Evaluate(context()).ToLocalChecked();
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    Local<Value> result_2 = module->Evaluate(context()).ToLocalChecked();
    CHECK_EQ(Module::kEvaluated, module->GetStatus());

    Local<Promise> promise = result_1.As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
    CHECK(promise->Result()->IsUndefined());

    // Second evaluation should return the same promise.
    Local<Promise> promise_too = result_2.As<Promise>();
    CHECK_EQ(promise, promise_too);
    CHECK_EQ(promise_too->State(), v8::Promise::kFulfilled);
    CHECK(promise_too->Result()->IsUndefined());
  }
  CHECK(!try_catch.HasCaught());
}

TEST_F(ModuleTest, ModuleEvaluationCompletion2) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  const char* sources[] = {
      "'gaga'; ",
      "'gaga'; var a = 1",
      "'gaga'; import '42'",
      "'gaga'; export * from '42'",
      "'gaga'; export {} from '42'",
      "'gaga'; export {}",
      "'gaga'; var a = 1; export {a}",
      "'gaga'; export function foo() {}",
      "'gaga'; export class C extends null {}",
      "'gaga'; export let a = 1",
      "'gaga'; export default 1",
      "'gaga'; export default function foo() {}",
      "'gaga'; export default function () {}",
      "'gaga'; export default (function () {})",
      "'gaga'; export default class C extends null {}",
      "'gaga'; export default (class C extends null {})",
  };

  for (auto src : sources) {
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());

    Local<Value> result_1 = module->Evaluate(context()).ToLocalChecked();
    CHECK_EQ(Module::kEvaluated, module->GetStatus());

    Local<Value> result_2 = module->Evaluate(context()).ToLocalChecked();
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    Local<Promise> promise = result_1.As<Promise>();
    CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
    CHECK(promise->Result()->IsUndefined());

    // Second Evaluation should return the same promise.
    Local<Promise> promise_too = result_2.As<Promise>();
    CHECK_EQ(promise, promise_too);
    CHECK_EQ(promise_too->State(), v8::Promise::kFulfilled);
    CHECK(promise_too->Result()->IsUndefined());
  }
  CHECK(!try_catch.HasCaught());
}

TEST_F(ModuleTest, ModuleNamespace) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  Local<v8::Object> ReferenceError =
      RunJS("ReferenceError")->ToObject(context()).ToLocalChecked();

  Local<String> source_text = NewString(
      "import {a, b} from 'export var a = 1; export let b = 2';"
      "export function geta() {return a};"
      "export function getb() {return b};"
      "export let radio = 3;"
      "export var gaga = 4;");
  ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, module->GetStatus());
  CHECK(module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  Local<Value> ns = module->GetModuleNamespace();
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  Local<v8::Object> nsobj = ns->ToObject(context()).ToLocalChecked();
  CHECK_EQ(nsobj->GetCreationContext().ToLocalChecked(), context());

  // a, b
  CHECK(nsobj->Get(context(), NewString("a")).ToLocalChecked()->IsUndefined());
  CHECK(nsobj->Get(context(), NewString("b")).ToLocalChecked()->IsUndefined());

  // geta
  {
    auto geta = nsobj->Get(context(), NewString("geta")).ToLocalChecked();
    auto a = geta.As<v8::Function>()
                 ->Call(context(), geta, 0, nullptr)
                 .ToLocalChecked();
    CHECK(a->IsUndefined());
  }

  // getb
  {
    v8::TryCatch inner_try_catch(isolate());
    auto getb = nsobj->Get(context(), NewString("getb")).ToLocalChecked();
    CHECK(getb.As<v8::Function>()->Call(context(), getb, 0, nullptr).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()
              ->InstanceOf(context(), ReferenceError)
              .FromJust());
  }

  // radio
  {
    v8::TryCatch inner_try_catch(isolate());
    CHECK(nsobj->Get(context(), NewString("radio")).IsEmpty());
    CHECK(inner_try_catch.HasCaught());
    CHECK(inner_try_catch.Exception()
              ->InstanceOf(context(), ReferenceError)
              .FromJust());
  }

  // gaga
  {
    auto gaga = nsobj->Get(context(), NewString("gaga")).ToLocalChecked();
    CHECK(gaga->IsUndefined());
  }

  CHECK(!try_catch.HasCaught());
  CHECK_EQ(Module::kInstantiated, module->GetStatus());
  module->Evaluate(context()).ToLocalChecked();
  CHECK_EQ(Module::kEvaluated, module->GetStatus());

  // geta
  {
    auto geta = nsobj->Get(context(), NewString("geta")).ToLocalChecked();
    auto a = geta.As<v8::Function>()
                 ->Call(context(), geta, 0, nullptr)
                 .ToLocalChecked();
    CHECK_EQ(1, a->Int32Value(context()).FromJust());
  }

  // getb
  {
    auto getb = nsobj->Get(context(), NewString("getb")).ToLocalChecked();
    auto b = getb.As<v8::Function>()
                 ->Call(context(), getb, 0, nullptr)
                 .ToLocalChecked();
    CHECK_EQ(2, b->Int32Value(context()).FromJust());
  }

  // radio
  {
    auto radio = nsobj->Get(context(), NewString("radio")).ToLocalChecked();
    CHECK_EQ(3, radio->Int32Value(context()).FromJust());
  }

  // gaga
  {
    auto gaga = nsobj->Get(context(), NewString("gaga")).ToLocalChecked();
    CHECK_EQ(4, gaga->Int32Value(context()).FromJust());
  }
  CHECK(!try_catch.HasCaught());
}

TEST_F(ModuleTest, ModuleEvaluationTopLevelAwait) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());
  const char* sources[] = {
      "await 42",
      "import 'await 42';",
      "import '42'; import 'await 42';",
  };

  for (auto src : sources) {
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    Local<Promise> promise =
        Local<Promise>::Cast(module->Evaluate(context()).ToLocalChecked());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
    CHECK(promise->Result()->IsUndefined());
    CHECK(!try_catch.HasCaught());
  }
}

TEST_F(ModuleTest, ModuleEvaluationTopLevelAwaitError) {
  HandleScope scope(isolate());
  const char* sources[] = {
      "await 42; throw 'boom';",
      "import 'await 42; throw \"boom\";';",
      "import '42'; import 'await 42; throw \"boom\";';",
  };

  for (auto src : sources) {
    v8::TryCatch try_catch(isolate());
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());
    Local<Promise> promise =
        Local<Promise>::Cast(module->Evaluate(context()).ToLocalChecked());
    CHECK_EQ(Module::kErrored, module->GetStatus());
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK(promise->Result()->StrictEquals(NewString("boom")));
    CHECK(module->GetException()->StrictEquals(NewString("boom")));

    // TODO(cbruni) I am not sure, but this might not be supposed to throw
    // because it is async.
    CHECK(!try_catch.HasCaught());
  }
}

namespace {
struct DynamicImportData {
  DynamicImportData(Isolate* isolate_, Local<Promise::Resolver> resolver_,
                    Local<Context> context_, bool should_resolve_)
      : isolate(isolate_), should_resolve(should_resolve_) {
    resolver.Reset(isolate, resolver_);
    context.Reset(isolate, context_);
  }

  Isolate* isolate;
  v8::Global<Promise::Resolver> resolver;
  v8::Global<Context> context;
  bool should_resolve;
};

void DoHostImportModuleDynamically(void* import_data) {
  std::unique_ptr<DynamicImportData> import_data_(
      static_cast<DynamicImportData*>(import_data));
  Isolate* isolate(import_data_->isolate);
  HandleScope handle_scope(isolate);

  Local<Promise::Resolver> resolver(import_data_->resolver.Get(isolate));
  Local<Context> realm(import_data_->context.Get(isolate));
  Context::Scope context_scope(realm);

  if (import_data_->should_resolve) {
    resolver->Resolve(realm, True(isolate)).ToChecked();
  } else {
    resolver
        ->Reject(realm, String::NewFromUtf8(isolate, "boom").ToLocalChecked())
        .ToChecked();
  }
}

v8::MaybeLocal<v8::Promise> HostImportModuleDynamicallyCallbackResolve(
    Local<Context> context, Local<Data> host_defined_options,
    Local<Value> resource_name, Local<String> specifier,
    Local<FixedArray> import_attributes) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Promise::Resolver> resolver =
      v8::Promise::Resolver::New(context).ToLocalChecked();
  DynamicImportData* data =
      new DynamicImportData(isolate, resolver, context, true);
  isolate->EnqueueMicrotask(DoHostImportModuleDynamically, data);
  return resolver->GetPromise();
}

v8::MaybeLocal<v8::Promise> HostImportModuleDynamicallyCallbackReject(
    Local<Context> context, Local<Data> host_defined_options,
    Local<Value> resource_name, Local<String> specifier,
    Local<FixedArray> import_attributes) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Promise::Resolver> resolver =
      v8::Promise::Resolver::New(context).ToLocalChecked();
  DynamicImportData* data =
      new DynamicImportData(isolate, resolver, context, false);
  isolate->EnqueueMicrotask(DoHostImportModuleDynamically, data);
  return resolver->GetPromise();
}

}  // namespace

TEST_F(ModuleTest, ModuleEvaluationTopLevelAwaitDynamicImport) {
  HandleScope scope(isolate());
  isolate()->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
  isolate()->SetHostImportModuleDynamicallyCallback(
      HostImportModuleDynamicallyCallbackResolve);
  v8::TryCatch try_catch(isolate());
  const char* sources[] = {
      "await import('foo');",
      "import 'await import(\"foo\");';",
      "import '42'; import 'await import(\"foo\");';",
  };

  for (auto src : sources) {
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());

    Local<Promise> promise =
        Local<Promise>::Cast(module->Evaluate(context()).ToLocalChecked());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK_EQ(promise->State(), v8::Promise::kPending);
    CHECK(!try_catch.HasCaught());

    isolate()->PerformMicrotaskCheckpoint();
    CHECK_EQ(promise->State(), v8::Promise::kFulfilled);
  }
}

TEST_F(ModuleTest, ModuleEvaluationTopLevelAwaitDynamicImportError) {
  HandleScope scope(isolate());
  isolate()->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
  isolate()->SetHostImportModuleDynamicallyCallback(
      HostImportModuleDynamicallyCallbackReject);
  v8::TryCatch try_catch(isolate());
  const char* sources[] = {
      "await import('foo');",
      "import 'await import(\"foo\");';",
      "import '42'; import 'await import(\"foo\");';",
  };

  for (auto src : sources) {
    Local<String> source_text = NewString(src);
    ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK_EQ(Module::kUninstantiated, module->GetStatus());
    CHECK(module
              ->InstantiateModule(context(),
                                  CompileSpecifierAsModuleResolveCallback)
              .FromJust());
    CHECK_EQ(Module::kInstantiated, module->GetStatus());

    Local<Promise> promise =
        Local<Promise>::Cast(module->Evaluate(context()).ToLocalChecked());
    CHECK_EQ(Module::kEvaluated, module->GetStatus());
    CHECK_EQ(promise->State(), v8::Promise::kPending);
    CHECK(!try_catch.HasCaught());

    isolate()->PerformMicrotaskCheckpoint();
    CHECK_EQ(Module::kErrored, module->GetStatus());
    CHECK_EQ(promise->State(), v8::Promise::kRejected);
    CHECK(promise->Result()->StrictEquals(NewString("boom")));
    CHECK(module->GetException()->StrictEquals(NewString("boom")));
    CHECK(!try_catch.HasCaught());
  }
}

TEST_F(ModuleTest, TerminateExecutionTopLevelAwaitSync) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  context()
      ->Global()
      ->Set(context(), NewString("terminate"),
            v8::Function::New(context(),
                              [](const v8::FunctionCallbackInfo<Value>& info) {
                                info.GetIsolate()->TerminateExecution();
                              })
                .ToLocalChecked())
      .ToChecked();

  Local<String> source_text = NewString("terminate(); while (true) {}");
  ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK(module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());

  CHECK(module->Evaluate(context()).IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK(try_catch.HasTerminated());
  CHECK_EQ(module->GetStatus(), Module::kErrored);
  CHECK_EQ(module->GetException(), v8::Null(isolate()));
}

TEST_F(ModuleTest, TerminateExecutionTopLevelAwaitAsync) {
  HandleScope scope(isolate());
  v8::TryCatch try_catch(isolate());

  context()
      ->Global()
      ->Set(context(), NewString("terminate"),
            v8::Function::New(context(),
                              [](const v8::FunctionCallbackInfo<Value>& info) {
                                info.GetIsolate()->TerminateExecution();
                              })
                .ToLocalChecked())
      .ToChecked();

  Local<Promise::Resolver> eval_promise =
      Promise::Resolver::New(context()).ToLocalChecked();
  context()
      ->Global()
      ->Set(context(), NewString("evalPromise"), eval_promise)
      .ToChecked();

  Local<String> source_text =
      NewString("await evalPromise; terminate(); while (true) {}");
  ScriptOrigin origin = ModuleOrigin(NewString("file.js"), isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK(module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());

  Local<Promise> promise =
      Local<Promise>::Cast(module->Evaluate(context()).ToLocalChecked());
  CHECK_EQ(module->GetStatus(), Module::kEvaluated);
  CHECK_EQ(promise->State(), Promise::PromiseState::kPending);
  CHECK(!try_catch.HasCaught());
  CHECK(!try_catch.HasTerminated());

  eval_promise->Resolve(context(), v8::Undefined(isolate())).ToChecked();

  CHECK(try_catch.HasCaught());
  CHECK(try_catch.HasTerminated());
  CHECK_EQ(promise->State(), Promise::PromiseState::kPending);

  // The termination exception doesn't trigger the module's
  // catch handler, so the module isn't transitioned to kErrored.
  CHECK_EQ(module->GetStatus(), Module::kEvaluated);
}

static v8::Global<Module> async_leaf_module_global;
static v8::Global<Module> sync_leaf_module_global;
static v8::Global<Module> cycle_self_module_global;
static v8::Global<Module> cycle_one_module_global;
static v8::Global<Module> cycle_two_module_global;
MaybeLocal<Module> ResolveCallbackForIsGraphAsyncTopLevelAwait(
    Local<Context> context, Local<String> specifier,
    Local<FixedArray> import_attributes, Local<Module> referrer) {
  CHECK_EQ(0, import_attributes->Length());
  Isolate* isolate = context->GetIsolate();
  if (specifier->StrictEquals(
          String::NewFromUtf8(isolate, "./async_leaf.js").ToLocalChecked())) {
    return async_leaf_module_global.Get(isolate);
  } else if (specifier->StrictEquals(
                 String::NewFromUtf8(isolate, "./sync_leaf.js")
                     .ToLocalChecked())) {
    return sync_leaf_module_global.Get(isolate);
  } else if (specifier->StrictEquals(
                 String::NewFromUtf8(isolate, "./cycle_self.js")
                     .ToLocalChecked())) {
    return cycle_self_module_global.Get(isolate);
  } else if (specifier->StrictEquals(
                 String::NewFromUtf8(isolate, "./cycle_one.js")
                     .ToLocalChecked())) {
    return cycle_one_module_global.Get(isolate);
  } else {
    CHECK(specifier->StrictEquals(
        String::NewFromUtf8(isolate, "./cycle_two.js").ToLocalChecked()));
    return cycle_two_module_global.Get(isolate);
  }
}

TEST_F(ModuleTest, IsGraphAsyncTopLevelAwait) {
  HandleScope scope(isolate());

  {
    Local<String> source_text = NewString("await notExecuted();");
    ScriptOrigin origin = ModuleOrigin(NewString("async_leaf.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> async_leaf_module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    async_leaf_module_global.Reset(isolate(), async_leaf_module);
    CHECK(async_leaf_module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(async_leaf_module->IsGraphAsync());
  }

  {
    Local<String> source_text = NewString("notExecuted();");
    ScriptOrigin origin = ModuleOrigin(NewString("sync_leaf.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> sync_leaf_module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    sync_leaf_module_global.Reset(isolate(), sync_leaf_module);
    CHECK(sync_leaf_module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(!sync_leaf_module->IsGraphAsync());
  }

  {
    Local<String> source_text = NewString("import './async_leaf.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("import_async.js"), isolate());
    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK(module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(module->IsGraphAsync());
  }

  {
    Local<String> source_text = NewString("import './sync_leaf.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("import_sync.js"), isolate());

    ScriptCompiler::Source source(source_text, origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    CHECK(module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(!module->IsGraphAsync());
  }

  {
    Local<String> source_text = NewString(
        "import './cycle_self.js'\n"
        "import './async_leaf.js'");
    ScriptOrigin origin = ModuleOrigin(NewString("cycle_self.js"), isolate());

    ScriptCompiler::Source source(source_text, origin);
    Local<Module> cycle_self_module =
        ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
    cycle_self_module_global.Reset(isolate(), cycle_self_module);
    CHECK(cycle_self_module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(cycle_self_module->IsGraphAsync());
  }

  {
    Local<String> source_text1 = NewString("import './cycle_two.js'");
    ScriptOrigin origin1 = ModuleOrigin(NewString("cycle_one.js"), isolate());

    ScriptCompiler::Source source1(source_text1, origin1);
    Local<Module> cycle_one_module =
        ScriptCompiler::CompileModule(isolate(), &source1).ToLocalChecked();
    cycle_one_module_global.Reset(isolate(), cycle_one_module);
    Local<String> source_text2 = NewString(
        "import './cycle_one.js'\n"
        "import './async_leaf.js'");
    ScriptOrigin origin2 = ModuleOrigin(NewString("cycle_two.js"), isolate());

    ScriptCompiler::Source source2(source_text2, origin2);
    Local<Module> cycle_two_module =
        ScriptCompiler::CompileModule(isolate(), &source2).ToLocalChecked();
    cycle_two_module_global.Reset(isolate(), cycle_two_module);
    CHECK(cycle_one_module
              ->InstantiateModule(context(),
                                  ResolveCallbackForIsGraphAsyncTopLevelAwait)
              .FromJust());
    CHECK(cycle_one_module->IsGraphAsync());
    CHECK(cycle_two_module->IsGraphAsync());
  }

  async_leaf_module_global.Reset();
  sync_leaf_module_global.Reset();
  cycle_self_module_global.Reset();
  cycle_one_module_global.Reset();
  cycle_two_module_global.Reset();
}

TEST_F(ModuleTest, AsyncEvaluatingInEvaluateEntryPoint) {
  // This test relies on v8::Module::Evaluate _not_ performing a microtask
  // checkpoint.
  isolate()->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

  Local<String> source_text = NewString("await 0;");
  ScriptOrigin origin = ModuleOrigin(NewString("async_leaf.js"), isolate());
  ScriptCompiler::Source source(source_text, origin);
  Local<Module> async_leaf_module =
      ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();
  CHECK_EQ(Module::kUninstantiated, async_leaf_module->GetStatus());
  CHECK(async_leaf_module
            ->InstantiateModule(context(),
                                CompileSpecifierAsModuleResolveCallback)
            .FromJust());
  CHECK_EQ(Module::kInstantiated, async_leaf_module->GetStatus());
  Local<Promise> promise1 = Local<Promise>::Cast(
      async_leaf_module->Evaluate(context()).ToLocalChecked());
  CHECK_EQ(Module::kEvaluated, async_leaf_module->GetStatus());
  Local<Promise> promise2 = Local<Promise>::Cast(
      async_leaf_module->Evaluate(context()).ToLocalChecked());
  CHECK_EQ(promise1, promise2);

  isolate()->PerformMicrotaskCheckpoint();

  CHECK_EQ(v8::Promise::kFulfilled, promise1->State());
}

}  // anonymous namespace
