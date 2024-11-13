// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/pipeline.h"
#include "src/debug/debug-interface.h"
#include "src/execution/execution.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

#define SHARD_TEST_BY_2(x)                                   \
  TEST_F(RunBytecodeGraphBuilderTest, x##_0) { Test##x(0); } \
  TEST_F(RunBytecodeGraphBuilderTest, x##_1) { Test##x(1); }
#define SHARD_TEST_BY_4(x)                                   \
  TEST_F(RunBytecodeGraphBuilderTest, x##_0) { Test##x(0); } \
  TEST_F(RunBytecodeGraphBuilderTest, x##_1) { Test##x(1); } \
  TEST_F(RunBytecodeGraphBuilderTest, x##_2) { Test##x(2); } \
  TEST_F(RunBytecodeGraphBuilderTest, x##_3) { Test##x(3); }

static const char kFunctionName[] = "f";

static const Token::Value kCompareOperators[] = {
    Token::kEq,          Token::kNotEq,        Token::kEqStrict,
    Token::kNotEqStrict, Token::kLessThan,     Token::kLessThanEq,
    Token::kGreaterThan, Token::kGreaterThanEq};

static const int SMI_MAX = (1 << 30) - 1;
static const int SMI_MIN = -(1 << 30);

static MaybeHandle<Object> CallFunction(Isolate* isolate,
                                        Handle<JSFunction> function) {
  return Execution::Call(isolate, function,
                         isolate->factory()->undefined_value(), 0, nullptr);
}

template <class... A>
static MaybeHandle<Object> CallFunction(Isolate* isolate,
                                        Handle<JSFunction> function,
                                        A... args) {
  Handle<Object> argv[] = {args...};
  return Execution::Call(isolate, function,
                         isolate->factory()->undefined_value(), sizeof...(args),
                         argv);
}

static v8::Local<v8::Value> CompileRun(v8::Isolate* isolate,
                                       const char* source) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context, v8::String::NewFromUtf8(isolate, source).ToLocalChecked())
          .ToLocalChecked();

  v8::Local<v8::Value> result;
  if (script->Run(context).ToLocal(&result)) {
    return result;
  } else {
    return v8::Local<v8::Value>();
  }
}

template <class... A>
class BytecodeGraphCallable {
 public:
  BytecodeGraphCallable(Isolate* isolate, Handle<JSFunction> function)
      : isolate_(isolate), function_(function) {}
  virtual ~BytecodeGraphCallable() = default;

  MaybeHandle<Object> operator()(A... args) {
    return CallFunction(isolate_, function_, args...);
  }

 private:
  Isolate* isolate_;
  Handle<JSFunction> function_;
};

class BytecodeGraphTester {
 public:
  BytecodeGraphTester(Isolate* isolate, const char* script,
                      const char* filter = kFunctionName)
      : isolate_(isolate), script_(script) {
    i::v8_flags.always_turbofan = false;
    i::v8_flags.allow_natives_syntax = true;
  }
  virtual ~BytecodeGraphTester() = default;
  BytecodeGraphTester(const BytecodeGraphTester&) = delete;
  BytecodeGraphTester& operator=(const BytecodeGraphTester&) = delete;

  template <class... A>
  BytecodeGraphCallable<A...> GetCallable(
      const char* functionName = kFunctionName) {
    return BytecodeGraphCallable<A...>(isolate_, GetFunction(functionName));
  }

  Local<Message> CheckThrowsReturnMessage() {
    TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate_));
    auto callable = GetCallable<>();
    MaybeHandle<Object> no_result = callable();
    CHECK(isolate_->has_exception());
    CHECK(try_catch.HasCaught());
    CHECK(no_result.is_null());
    CHECK(!try_catch.Message().IsEmpty());
    return try_catch.Message();
  }

 private:
  Isolate* isolate_;
  const char* script_;

  Handle<JSFunction> GetFunction(const char* functionName) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate_);
    v8::Local<v8::Context> context = v8_isolate->GetCurrentContext();

    CompileRun(v8_isolate, script_);

    Local<Function> api_function = Local<Function>::Cast(
        context->Global()
            ->Get(context, v8::String::NewFromUtf8(v8_isolate, functionName)
                               .ToLocalChecked())
            .ToLocalChecked());
    Handle<JSFunction> function =
        Cast<JSFunction>(v8::Utils::OpenHandle(*api_function));
    IsCompiledScope is_compiled_scope(
        function->shared()->is_compiled_scope(isolate_));
    JSFunction::EnsureFeedbackVector(isolate_, function, &is_compiled_scope);
    CHECK(function->shared()->HasBytecodeArray());

    Zone zone(isolate_->allocator(), ZONE_NAME);
    Handle<SharedFunctionInfo> shared(function->shared(), isolate_);
    OptimizedCompilationInfo compilation_info(&zone, isolate_, shared, function,
                                              CodeKind::TURBOFAN);

    DirectHandle<Code> code =
        Pipeline::GenerateCodeForTesting(&compilation_info, isolate_)
            .ToHandleChecked();
    function->set_code(*code, kReleaseStore);

    return function;
  }
};

#define SPACE()

#define REPEAT_2(SEP, ...) __VA_ARGS__ SEP() __VA_ARGS__
#define REPEAT_4(SEP, ...) \
  REPEAT_2(SEP, __VA_ARGS__) SEP() REPEAT_2(SEP, __VA_ARGS__)
#define REPEAT_8(SEP, ...) \
  REPEAT_4(SEP, __VA_ARGS__) SEP() REPEAT_4(SEP, __VA_ARGS__)
#define REPEAT_16(SEP, ...) \
  REPEAT_8(SEP, __VA_ARGS__) SEP() REPEAT_8(SEP, __VA_ARGS__)
#define REPEAT_32(SEP, ...) \
  REPEAT_16(SEP, __VA_ARGS__) SEP() REPEAT_16(SEP, __VA_ARGS__)
#define REPEAT_64(SEP, ...) \
  REPEAT_32(SEP, __VA_ARGS__) SEP() REPEAT_32(SEP, __VA_ARGS__)
#define REPEAT_128(SEP, ...) \
  REPEAT_64(SEP, __VA_ARGS__) SEP() REPEAT_64(SEP, __VA_ARGS__)
#define REPEAT_256(SEP, ...) \
  REPEAT_128(SEP, __VA_ARGS__) SEP() REPEAT_128(SEP, __VA_ARGS__)

#define REPEAT_127(SEP, ...)  \
  REPEAT_64(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_32(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_16(SEP, __VA_ARGS__) \
  SEP()                       \
  REPEAT_8(SEP, __VA_ARGS__)  \
  SEP()                       \
  REPEAT_4(SEP, __VA_ARGS__) SEP() REPEAT_2(SEP, __VA_ARGS__) SEP() __VA_ARGS__

template <int N, typename T = Handle<Object>>
struct ExpectedSnippet {
  const char* code_snippet;
  T return_value_and_parameters[N + 1];

  inline T return_value() const { return return_value_and_parameters[0]; }

  inline T parameter(int i) const {
    CHECK_GE(i, 0);
    CHECK_LT(i, N);
    return return_value_and_parameters[1 + i];
  }
};

class RunBytecodeGraphBuilderTest : public TestWithNativeContext {
 public:
  void TestBytecodeGraphBuilderNamedStore(size_t shard) {
    Factory* factory = i_isolate()->factory();

    ExpectedSnippet<1> snippets[] = {
        {"return p1.val = 20;",
         {factory->NewNumberFromInt(20), RunJS("({val : 10})")}},
        {"p1.type = 'int'; return p1.type;",
         {MakeString("int"), RunJS("({val : 10})")}},
        {"p1.name = 'def'; return p1[\"name\"];",
         {MakeString("def"), RunJS("({name : 'abc'})")}},
        {"'use strict'; p1.val = 20; return p1.val;",
         {factory->NewNumberFromInt(20), RunJS("({val : 10 })")}},
        {"'use strict'; return p1.type = 'int';",
         {MakeString("int"), RunJS("({val : 10})")}},
        {"'use strict'; p1.val = 20; return p1[\"val\"];",
         {factory->NewNumberFromInt(20), RunJS("({val : 10, name : 'abc'})")}},
        {"var b = 'abc';\n" REPEAT_127(
             SPACE, " p1.name = b; ") " p1.name = 'def'; return p1.name;\n",
         {MakeString("def"), RunJS("({name : 'abc'})")}},
        {"'use strict'; var b = 'def';\n" REPEAT_127(
             SPACE, " p1.name = 'abc'; ") "p1.name = b; return p1.name;\n",
         {MakeString("def"), RunJS("({ name : 'abc'})")}},
    };

    for (size_t i = 0; i < arraysize(snippets); i++) {
      if ((i % 2) != shard) continue;
      base::ScopedVector<char> script(3072);
      SNPrintF(script, "function %s(p1) { %s };\n%s({});", kFunctionName,
               snippets[i].code_snippet, kFunctionName);

      BytecodeGraphTester tester(i_isolate(), script.begin());
      auto callable = tester.GetCallable<Handle<Object>>();
      DirectHandle<Object> return_value =
          callable(snippets[i].parameter(0)).ToHandleChecked();
      CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
    }
  }

  void TestBytecodeGraphBuilderKeyedStore(size_t shard) {
    Isolate* isolate = i_isolate();
    Factory* factory = isolate->factory();

    ExpectedSnippet<2> snippets[] = {
        {"p1[p2] = 20; return p1[p2];",
         {factory->NewNumberFromInt(20), RunJS("({val : 10})"),
          MakeString("val")}},
        {"return p1[100] = 'def';",
         {MakeString("def"), RunJS("({100 : 'abc'})"),
          factory->NewNumberFromInt(0)}},
        {"var b = 100; p1[b] = 'def'; return p1[b];",
         {MakeString("def"), RunJS("({100 : 'abc'})"),
          factory->NewNumberFromInt(0)}},
        {"'use strict'; p1[p2] = 20; return p1[p2];",
         {factory->NewNumberFromInt(20), RunJS("({val : 10 })"),
          MakeString("val")}},
        {"'use strict'; return p1[100] = 20;",
         {factory->NewNumberFromInt(20), RunJS("({100 : 10})"),
          factory->NewNumberFromInt(0)}},
        {"'use strict'; var b = p2; p1[b] = 'def'; return p1[b];",
         {MakeString("def"), RunJS("({100 : 'abc'})"),
          factory->NewNumberFromInt(100)}},
        {"var b;\n" REPEAT_127(
             SPACE, " b = p1[p2]; ") " p1[p2] = 'def'; return p1[p2];\n",
         {MakeString("def"), RunJS("({100 : 'abc'})"),
          factory->NewNumberFromInt(100)}},
        {"'use strict'; var b;\n" REPEAT_127(
             SPACE, " b = p1[p2]; ") " p1[p2] = 'def'; return p1[p2];\n",
         {MakeString("def"), RunJS("({ 100 : 'abc'})"),
          factory->NewNumberFromInt(100)}},
    };

    for (size_t i = 0; i < arraysize(snippets); i++) {
      if ((i % 2) != shard) continue;
      base::ScopedVector<char> script(2048);
      SNPrintF(script, "function %s(p1, p2) { %s };\n%s({});", kFunctionName,
               snippets[i].code_snippet, kFunctionName);

      BytecodeGraphTester tester(isolate, script.begin());
      auto callable = tester.GetCallable<Handle<Object>>();
      DirectHandle<Object> return_value =
          callable(snippets[i].parameter(0)).ToHandleChecked();
      CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
    }
  }

  void TestBytecodeGraphBuilderGlobals(size_t shard) {
    Isolate* isolate = i_isolate();
    Factory* factory = isolate->factory();

    ExpectedSnippet<0> snippets[] = {
        {"var global = 321;\n function f() { return global; };\n f();",
         {factory->NewNumberFromInt(321)}},
        {"var global = 321;\n"
         "function f() { global = 123; return global };\n f();",
         {factory->NewNumberFromInt(123)}},
        {"var global = function() { return 'abc'};\n"
         "function f() { return global(); };\n f();",
         {MakeString("abc")}},
        {"var global = 456;\n"
         "function f() { 'use strict'; return global; };\n f();",
         {factory->NewNumberFromInt(456)}},
        {"var global = 987;\n"
         "function f() { 'use strict'; global = 789; return global };\n f();",
         {factory->NewNumberFromInt(789)}},
        {"var global = function() { return 'xyz'};\n"
         "function f() { 'use strict'; return global(); };\n f();",
         {MakeString("xyz")}},
        {"var global = 'abc'; var global_obj = {val:123};\n"
         "function f() {\n" REPEAT_127(
             SPACE, " var b = global_obj.name;\n") "return global; };\n f();\n",
         {MakeString("abc")}},
        {"var global = 'abc'; var global_obj = {val:123};\n"
         "function f() { 'use strict';\n" REPEAT_127(
             SPACE, " var b = global_obj.name;\n") "global = 'xyz'; return "
                                                   "global };\n f();\n",
         {MakeString("xyz")}},
        {"function f() { return typeof(undeclared_var); }\n; f();\n",
         {MakeString("undefined")}},
        {"var defined_var = 10; function f() { return typeof(defined_var); "
         "}\n; "
         "f();\n",
         {MakeString("number")}},
    };

    for (size_t i = 0; i < arraysize(snippets); i++) {
      if ((i % 2) != shard) continue;
      BytecodeGraphTester tester(isolate, snippets[i].code_snippet);
      auto callable = tester.GetCallable<>();
      DirectHandle<Object> return_value = callable().ToHandleChecked();
      CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
    }
  }

  void TestJumpWithConstantsAndWideConstants(size_t shard) {
    const int kStep = 46;
    int start = static_cast<int>(7 + 17 * shard);
    for (int constants = start; constants < 300; constants += kStep) {
      std::stringstream filler_os;
      // Generate a string that consumes constant pool entries and
      // spread out branch distances in script below.
      for (int i = 0; i < constants; i++) {
        filler_os << "var x_ = 'x_" << i << "';\n";
      }
      std::string filler(filler_os.str());

      std::stringstream script_os;
      script_os << "function " << kFunctionName << "(a) {\n";
      script_os << "  " << filler;
      script_os << "  for (var i = a; i < 2; i++) {\n";
      script_os << "  " << filler;
      script_os << "    if (i == 0) { " << filler << "i = 10; continue; }\n";
      script_os << "    else if (i == a) { " << filler << "i = 12; break; }\n";
      script_os << "    else { " << filler << " }\n";
      script_os << "  }\n";
      script_os << "  return i;\n";
      script_os << "}\n";
      script_os << kFunctionName << "(0);\n";
      std::string script(script_os.str());

      Isolate* isolate = i_isolate();
      Factory* factory = isolate->factory();
      BytecodeGraphTester tester(isolate, script.c_str());
      auto callable = tester.GetCallable<Handle<Object>>();
      for (int a = 0; a < 3; a++) {
        DirectHandle<Object> return_val =
            callable(factory->NewNumberFromInt(a)).ToHandleChecked();
        static const int results[] = {11, 12, 2};
        CHECK_EQ(Cast<Smi>(*return_val).value(), results[a]);
      }
    }
  }
};

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderReturnStatements) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return;", {factory->undefined_value()}},
      {"return null;", {factory->null_value()}},
      {"return true;", {factory->true_value()}},
      {"return false;", {factory->false_value()}},
      {"return 0;", {factory->NewNumberFromInt(0)}},
      {"return +1;", {factory->NewNumberFromInt(1)}},
      {"return -1;", {factory->NewNumberFromInt(-1)}},
      {"return +127;", {factory->NewNumberFromInt(127)}},
      {"return -128;", {factory->NewNumberFromInt(-128)}},
      {"return 0.001;", {factory->NewNumber(0.001)}},
      {"return 3.7e-60;", {factory->NewNumber(3.7e-60)}},
      {"return -3.7e60;", {factory->NewNumber(-3.7e60)}},
      {"return '';", {MakeString("")}},
      {"return 'catfood';", {MakeString("catfood")}},
      {"return NaN;", {factory->nan_value()}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderPrimitiveExpressions) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return 1 + 1;", {factory->NewNumberFromInt(2)}},
      {"return 20 - 30;", {factory->NewNumberFromInt(-10)}},
      {"return 4 * 100;", {factory->NewNumberFromInt(400)}},
      {"return 100 / 5;", {factory->NewNumberFromInt(20)}},
      {"return 25 % 7;", {factory->NewNumberFromInt(4)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTwoParameterTests) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      // Integers
      {"return p1 + p2;",
       {factory->NewNumberFromInt(-70), factory->NewNumberFromInt(3),
        factory->NewNumberFromInt(-73)}},
      {"return p1 + p2 + 3;",
       {factory->NewNumberFromInt(1139044), factory->NewNumberFromInt(300),
        factory->NewNumberFromInt(1138741)}},
      {"return p1 - p2;",
       {factory->NewNumberFromInt(1100), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 * p2;",
       {factory->NewNumberFromInt(-100000), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 / p2;",
       {factory->NewNumberFromInt(-10), factory->NewNumberFromInt(1000),
        factory->NewNumberFromInt(-100)}},
      {"return p1 % p2;",
       {factory->NewNumberFromInt(5), factory->NewNumberFromInt(373),
        factory->NewNumberFromInt(16)}},
      // Doubles
      {"return p1 + p2;",
       {factory->NewHeapNumber(9.999), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 - p2;",
       {factory->NewHeapNumber(-3.333), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 * p2;",
       {factory->NewHeapNumber(3.333 * 6.666), factory->NewHeapNumber(3.333),
        factory->NewHeapNumber(6.666)}},
      {"return p1 / p2;",
       {factory->NewHeapNumber(2.25), factory->NewHeapNumber(9),
        factory->NewHeapNumber(4)}},
      // Strings
      {"return p1 + p2;",
       {MakeString("abcdef"), MakeString("abc"), MakeString("def")}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s(0, 0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderNamedLoad) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1.val;",
       {factory->NewNumberFromInt(10), RunJS("({val : 10})")}},
      {"return p1[\"name\"];", {MakeString("abc"), RunJS("({name : 'abc'})")}},
      {"'use strict'; return p1.val;",
       {factory->NewNumberFromInt(10), RunJS("({val : 10 })")}},
      {"'use strict'; return p1[\"val\"];",
       {factory->NewNumberFromInt(10), RunJS("({val : 10, name : 'abc'})")}},
      {"var b;\n" REPEAT_127(SPACE, " b = p1.name; ") " return p1.name;\n",
       {MakeString("abc"), RunJS("({name : 'abc'})")}},
      {"'use strict'; var b;\n" REPEAT_127(
           SPACE, " b = p1.name; ") "return p1.name;\n",
       {MakeString("abc"), RunJS("({ name : 'abc'})")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderKeyedLoad) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      {"return p1[p2];",
       {factory->NewNumberFromInt(10), RunJS("({val : 10})"),
        MakeString("val")}},
      {"return p1[100];",
       {MakeString("abc"), RunJS("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"var b = 100; return p1[b];",
       {MakeString("abc"), RunJS("({100 : 'abc'})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; return p1[p2];",
       {factory->NewNumberFromInt(10), RunJS("({val : 10 })"),
        MakeString("val")}},
      {"'use strict'; return p1[100];",
       {factory->NewNumberFromInt(10), RunJS("({100 : 10})"),
        factory->NewNumberFromInt(0)}},
      {"'use strict'; var b = p2; return p1[b];",
       {MakeString("abc"), RunJS("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"var b;\n" REPEAT_127(SPACE, " b = p1[p2]; ") " return p1[p2];\n",
       {MakeString("abc"), RunJS("({100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
      {"'use strict'; var b;\n" REPEAT_127(SPACE,
                                           " b = p1[p2]; ") "return p1[p2];\n",
       {MakeString("abc"), RunJS("({ 100 : 'abc'})"),
        factory->NewNumberFromInt(100)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1, p2) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

SHARD_TEST_BY_2(BytecodeGraphBuilderNamedStore)

SHARD_TEST_BY_2(BytecodeGraphBuilderKeyedStore)

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderPropertyCall) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1.func();",
       {factory->NewNumberFromInt(25), RunJS("({func() { return 25; }})")}},
      {"return p1.func('abc');",
       {MakeString("abc"), RunJS("({func(a) { return a; }})")}},
      {"return p1.func(1, 2, 3, 4, 5, 6, 7, 8);",
       {factory->NewNumberFromInt(36),
        RunJS("({func(a, b, c, d, e, f, g, h) {\n"
              "  return a + b + c + d + e + f + g + h;}})")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s({func() {}});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCallNew) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"function counter() { this.count = 20; }\n"
       "function f() {\n"
       "  var c = new counter();\n"
       "  return c.count;\n"
       "}; f()",
       {factory->NewNumberFromInt(20)}},
      {"function counter(arg0) { this.count = 17; this.x = arg0; }\n"
       "function f() {\n"
       "  var c = new counter(6);\n"
       "  return c.count + c.x;\n"
       "}; f()",
       {factory->NewNumberFromInt(23)}},
      {"function counter(arg0, arg1) {\n"
       "  this.count = 17; this.x = arg0; this.y = arg1;\n"
       "}\n"
       "function f() {\n"
       "  var c = new counter(3, 5);\n"
       "  return c.count + c.x + c.y;\n"
       "}; f()",
       {factory->NewNumberFromInt(25)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    BytecodeGraphTester tester(isolate, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCreateClosure) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"function f() {\n"
       "  function counter() { this.count = 20; }\n"
       "  var c = new counter();\n"
       "  return c.count;\n"
       "}; f()",
       {factory->NewNumberFromInt(20)}},
      {"function f() {\n"
       "  function counter(arg0) { this.count = 17; this.x = arg0; }\n"
       "  var c = new counter(6);\n"
       "  return c.count + c.x;\n"
       "}; f()",
       {factory->NewNumberFromInt(23)}},
      {"function f() {\n"
       "  function counter(arg0, arg1) {\n"
       "    this.count = 17; this.x = arg0; this.y = arg1;\n"
       "  }\n"
       "  var c = new counter(3, 5);\n"
       "  return c.count + c.x + c.y;\n"
       "}; f()",
       {factory->NewNumberFromInt(25)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    BytecodeGraphTester tester(isolate, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCallRuntime) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"function f(arg0) { return %MaxSmi(); }\nf()",
       {factory->NewNumberFromInt(Smi::kMaxValue), factory->undefined_value()}},
      {"function f(arg0) { return %IsArray(arg0) }\nf(undefined)",
       {factory->true_value(), RunJS("[1, 2, 3]")}},
      {"function f(arg0) { return %Add(arg0, 2) }\nf(1)",
       {factory->NewNumberFromInt(5), factory->NewNumberFromInt(3)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    BytecodeGraphTester tester(isolate, snippets[i].code_snippet);
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

SHARD_TEST_BY_2(BytecodeGraphBuilderGlobals)

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderToObject) {
  // TODO(mythria): tests for ToObject. Needs ForIn.
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderToName) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var a = 'val'; var obj = {[a] : 10}; return obj.val;",
       {factory->NewNumberFromInt(10)}},
      {"var a = 20; var obj = {[a] : 10}; return obj['20'];",
       {factory->NewNumberFromInt(10)}},
      {"var a = 20; var obj = {[a] : 10}; return obj[20];",
       {factory->NewNumberFromInt(10)}},
      {"var a = {val:23}; var obj = {[a] : 10}; return obj[a];",
       {factory->NewNumberFromInt(10)}},
      {"var a = {val:23}; var obj = {[a] : 10}; return obj['[object Object]'];",
       {factory->NewNumberFromInt(10)}},
      {"var a = {toString : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       {factory->NewNumberFromInt(10)}},
      {"var a = {valueOf : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       {factory->undefined_value()}},
      {"var a = {[Symbol.toPrimitive] : function() { return 'x'}};\n"
       "var obj = {[a] : 10};\n"
       "return obj.x;",
       {factory->NewNumberFromInt(10)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLogicalNot) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return !p1;", {factory->false_value(), RunJS("({val : 10})")}},
      {"return !p1;", {factory->true_value(), factory->NewNumberFromInt(0)}},
      {"return !p1;", {factory->true_value(), factory->undefined_value()}},
      {"return !p1;", {factory->false_value(), factory->NewNumberFromInt(10)}},
      {"return !p1;", {factory->false_value(), factory->true_value()}},
      {"return !p1;", {factory->false_value(), MakeString("abc")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTypeOf) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return typeof p1;", {MakeString("object"), RunJS("({val : 10})")}},
      {"return typeof p1;",
       {MakeString("undefined"), factory->undefined_value()}},
      {"return typeof p1;",
       {MakeString("number"), factory->NewNumberFromInt(10)}},
      {"return typeof p1;", {MakeString("boolean"), factory->true_value()}},
      {"return typeof p1;", {MakeString("string"), MakeString("abc")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCompareTypeOf) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return typeof p1 === 'number';",
       {factory->true_value(), factory->NewNumber(1.1)}},
      {"return typeof p1 === 'string';",
       {factory->false_value(), factory->NewNumber(1.1)}},
      {"return typeof p1 === 'string';",
       {factory->true_value(), MakeString("string")}},
      {"return typeof p1 === 'string';",
       {factory->false_value(), factory->undefined_value()}},
      {"return typeof p1 === 'undefined';",
       {factory->true_value(), factory->undefined_value()}},
      {"return typeof p1 === 'object';",
       {factory->true_value(), factory->null_value()}},
      {"return typeof p1 === 'object';",
       {factory->true_value(), RunJS("({val : 10})")}},
      {"return typeof p1 === 'function';",
       {factory->false_value(), RunJS("({val : 10})")}},
      {"return typeof p1 === 'symbol';",
       {factory->true_value(), factory->NewSymbol()}},
      {"return typeof p1 === 'symbol';",
       {factory->false_value(), MakeString("string")}},
      {"return typeof p1 === 'other';",
       {factory->false_value(), factory->NewNumber(1.1)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCountOperation) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return ++p1;",
       {factory->NewNumberFromInt(11), factory->NewNumberFromInt(10)}},
      {"return p1++;",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(10)}},
      {"return p1++ + 10;",
       {factory->NewHeapNumber(15.23), factory->NewHeapNumber(5.23)}},
      {"return 20 + ++p1;",
       {factory->NewHeapNumber(27.23), factory->NewHeapNumber(6.23)}},
      {"return --p1;",
       {factory->NewHeapNumber(9.8), factory->NewHeapNumber(10.8)}},
      {"return p1--;",
       {factory->NewHeapNumber(10.8), factory->NewHeapNumber(10.8)}},
      {"return p1-- + 10;",
       {factory->NewNumberFromInt(20), factory->NewNumberFromInt(10)}},
      {"return 20 + --p1;",
       {factory->NewNumberFromInt(29), factory->NewNumberFromInt(10)}},
      {"return p1.val--;",
       {factory->NewNumberFromInt(10), RunJS("({val : 10})")}},
      {"return ++p1['val'];",
       {factory->NewNumberFromInt(11), RunJS("({val : 10})")}},
      {"return ++p1[1];", {factory->NewNumberFromInt(11), RunJS("({1 : 10})")}},
      {" function inner() { return p1 } return --p1;",
       {factory->NewNumberFromInt(9), factory->NewNumberFromInt(10)}},
      {" function inner() { return p1 } return p1--;",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(10)}},
      {"return ++p1;", {factory->nan_value(), MakeString("String")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderDelete) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return delete p1.val;", {factory->true_value(), RunJS("({val : 10})")}},
      {"delete p1.val; return p1.val;",
       {factory->undefined_value(), RunJS("({val : 10})")}},
      {"delete p1.name; return p1.val;",
       {factory->NewNumberFromInt(10), RunJS("({val : 10, name:'abc'})")}},
      {"'use strict'; return delete p1.val;",
       {factory->true_value(), RunJS("({val : 10})")}},
      {"'use strict'; delete p1.val; return p1.val;",
       {factory->undefined_value(), RunJS("({val : 10})")}},
      {"'use strict'; delete p1.name; return p1.val;",
       {factory->NewNumberFromInt(10), RunJS("({val : 10, name:'abc'})")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderDeleteGlobal) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var obj = {val : 10, type : 'int'};"
       "function f() {return delete obj;};",
       {factory->false_value()}},
      {"function f() {return delete this;};", {factory->true_value()}},
      {"var obj = {val : 10, type : 'int'};"
       "function f() {return delete obj.val;};",
       {factory->true_value()}},
      {"var obj = {val : 10, type : 'int'};"
       "function f() {'use strict'; return delete obj.val;};",
       {factory->true_value()}},
      {"var obj = {val : 10, type : 'int'};"
       "function f() {delete obj.val; return obj.val;};",
       {factory->undefined_value()}},
      {"var obj = {val : 10, type : 'int'};"
       "function f() {'use strict'; delete obj.val; return obj.val;};",
       {factory->undefined_value()}},
      {"var obj = {1 : 10, 2 : 20};"
       "function f() { return delete obj[1]; };",
       {factory->true_value()}},
      {"var obj = {1 : 10, 2 : 20};"
       "function f() { 'use strict';  return delete obj[1];};",
       {factory->true_value()}},
      {"obj = {1 : 10, 2 : 20};"
       "function f() { delete obj[1]; return obj[2];};",
       {factory->NewNumberFromInt(20)}},
      {"function f() {"
       "  var obj = {1 : 10, 2 : 20};"
       "  function inner() { return obj[1]; };"
       "  return delete obj[1];"
       "}",
       {factory->true_value()}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s %s({});", snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderDeleteLookupSlot) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  // TODO(mythria): Add more tests when we have support for LdaLookupSlot.
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
      "        }; f = t; t();\");"
      "}"
      "f1();";

  ExpectedSnippet<0> snippets[] = {
      {"return delete y;", {factory->true_value()}},
      {"return delete z;", {factory->false_value()}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s %s %s", function_prologue, snippets[i].code_snippet,
             function_epilogue);

    BytecodeGraphTester tester(isolate, script.begin(), "t");
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLookupSlot) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  const char* function_prologue =
      "var f;"
      "var x = 12;"
      "y = 10;"
      "var obj = {val:3.1414};"
      "var z = 30;"
      "function f1() {"
      "  var z = 20;"
      "  eval(\"function t() {";
  const char* function_epilogue =
      "        }; f = t; t();\");"
      "}"
      "f1();";

  ExpectedSnippet<0> snippets[] = {
      {"return x;", {factory->NewNumber(12)}},
      {"return obj.val;", {factory->NewNumber(3.1414)}},
      {"return typeof x;", {MakeString("number")}},
      {"return typeof dummy;", {MakeString("undefined")}},
      {"x = 23; return x;", {factory->NewNumber(23)}},
      {"'use strict'; obj.val = 23.456; return obj.val;",
       {factory->NewNumber(23.456)}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s %s %s", function_prologue, snippets[i].code_snippet,
             function_epilogue);

    BytecodeGraphTester tester(isolate, script.begin(), "t");
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLookupContextSlot) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  // Testing with eval called in the current context.
  const char* inner_eval_prologue = "var x = 0; function inner() {";
  const char* inner_eval_epilogue = "}; return inner();";

  ExpectedSnippet<0> inner_eval_snippets[] = {
      {"eval(''); return x;", {factory->NewNumber(0)}},
      {"eval('var x = 1'); return x;", {factory->NewNumber(1)}},
      {"'use strict'; eval('var x = 1'); return x;", {factory->NewNumber(0)}}};

  for (size_t i = 0; i < arraysize(inner_eval_snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s %s %s } ; %s() ;", kFunctionName,
             inner_eval_prologue, inner_eval_snippets[i].code_snippet,
             inner_eval_epilogue, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value,
                            *inner_eval_snippets[i].return_value()));
  }

  // Testing with eval called in a parent context.
  const char* outer_eval_prologue = "";
  const char* outer_eval_epilogue =
      "function inner() { return x; }; return inner();";

  ExpectedSnippet<0> outer_eval_snippets[] = {
      {"var x = 0; eval('');", {factory->NewNumber(0)}},
      {"var x = 0; eval('var x = 1');", {factory->NewNumber(1)}},
      {"'use strict'; var x = 0; eval('var x = 1');", {factory->NewNumber(0)}}};

  for (size_t i = 0; i < arraysize(outer_eval_snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s %s %s } ; %s() ;", kFunctionName,
             outer_eval_prologue, outer_eval_snippets[i].code_snippet,
             outer_eval_epilogue, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value,
                            *outer_eval_snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLookupGlobalSlot) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  // Testing with eval called in the current context.
  const char* inner_eval_prologue = "x = 0; function inner() {";
  const char* inner_eval_epilogue = "}; return inner();";

  ExpectedSnippet<0> inner_eval_snippets[] = {
      {"eval(''); return x;", {factory->NewNumber(0)}},
      {"eval('var x = 1'); return x;", {factory->NewNumber(1)}},
      {"'use strict'; eval('var x = 1'); return x;", {factory->NewNumber(0)}}};

  for (size_t i = 0; i < arraysize(inner_eval_snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s %s %s } ; %s() ;", kFunctionName,
             inner_eval_prologue, inner_eval_snippets[i].code_snippet,
             inner_eval_epilogue, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value,
                            *inner_eval_snippets[i].return_value()));
  }

  // Testing with eval called in a parent context.
  const char* outer_eval_prologue = "";
  const char* outer_eval_epilogue =
      "function inner() { return x; }; return inner();";

  ExpectedSnippet<0> outer_eval_snippets[] = {
      {"x = 0; eval('');", {factory->NewNumber(0)}},
      {"x = 0; eval('var x = 1');", {factory->NewNumber(1)}},
      {"'use strict'; x = 0; eval('var x = 1');", {factory->NewNumber(0)}}};

  for (size_t i = 0; i < arraysize(outer_eval_snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s %s %s } ; %s() ;", kFunctionName,
             outer_eval_prologue, outer_eval_snippets[i].code_snippet,
             outer_eval_epilogue, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value,
                            *outer_eval_snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLookupSlotWide) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  const char* function_prologue =
      "var f;"
      "var x = 12;"
      "y = 10;"
      "var obj = {val:3.1414};"
      "var z = 30;"
      "function f1() {"
      "  var z = 20;"
      "  eval(\"function t() {";
  const char* function_epilogue =
      "        }; f = t; t();\");"
      "}"
      "f1();";

  ExpectedSnippet<0> snippets[] = {
      {"var y = 2.3;" REPEAT_256(SPACE, "y = 2.3;") "return x;",
       {factory->NewNumber(12)}},
      {"var y = 2.3;" REPEAT_256(SPACE, "y = 2.3;") "return typeof x;",
       {MakeString("number")}},
      {"var y = 2.3;" REPEAT_256(SPACE, "y = 2.3;") "return x = 23;",
       {factory->NewNumber(23)}},
      {"'use strict';" REPEAT_256(SPACE, "y = 2.3;") "return obj.val = 23.456;",
       {factory->NewNumber(23.456)}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(3072);
    SNPrintF(script, "%s %s %s", function_prologue, snippets[i].code_snippet,
             function_epilogue);

    BytecodeGraphTester tester(isolate, script.begin(), "t");
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCallLookupSlot) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"g = function(){ return 2 }; eval(''); return g();",
       {handle(Smi::FromInt(2), isolate)}},
      {"g = function(){ return 2 }; eval('g = function() {return 3}');\n"
       "return g();",
       {handle(Smi::FromInt(3), isolate)}},
      {"g = { x: function(){ return this.y }, y: 20 };\n"
       "eval('g = { x: g.x, y: 30 }');\n"
       "return g.x();",
       {handle(Smi::FromInt(30), isolate)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);
    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderEval) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"return eval('1;');", {handle(Smi::FromInt(1), isolate)}},
      {"return eval('100 * 20;');", {handle(Smi::FromInt(2000), isolate)}},
      {"var x = 10; return eval('x + 20;');",
       {handle(Smi::FromInt(30), isolate)}},
      {"var x = 10; eval('x = 33;'); return x;",
       {handle(Smi::FromInt(33), isolate)}},
      {"'use strict'; var x = 20; var z = 0;\n"
       "eval('var x = 33; z = x;'); return x + z;",
       {handle(Smi::FromInt(53), isolate)}},
      {"eval('var x = 33;'); eval('var y = x + 20'); return x + y;",
       {handle(Smi::FromInt(86), isolate)}},
      {"var x = 1; eval('for(i = 0; i < 10; i++) x = x + 1;'); return x",
       {handle(Smi::FromInt(11), isolate)}},
      {"var x = 10; eval('var x = 20;'); return x;",
       {handle(Smi::FromInt(20), isolate)}},
      {"var x = 1; eval('\"use strict\"; var x = 2;'); return x;",
       {handle(Smi::FromInt(1), isolate)}},
      {"'use strict'; var x = 1; eval('var x = 2;'); return x;",
       {handle(Smi::FromInt(1), isolate)}},
      {"var x = 10; eval('x + 20;'); return typeof x;", {MakeString("number")}},
      {"eval('var y = 10;'); return typeof unallocated;",
       {MakeString("undefined")}},
      {"'use strict'; eval('var y = 10;'); return typeof unallocated;",
       {MakeString("undefined")}},
      {"eval('var x = 10;'); return typeof x;", {MakeString("number")}},
      {"var x = {}; eval('var x = 10;'); return typeof x;",
       {MakeString("number")}},
      {"'use strict'; var x = {}; eval('var x = 10;'); return typeof x;",
       {MakeString("object")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);
    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderEvalParams) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<1> snippets[] = {
      {"var x = 10; return eval('x + p1;');",
       {handle(Smi::FromInt(30), isolate), handle(Smi::FromInt(20), isolate)}},
      {"var x = 10; eval('p1 = x;'); return p1;",
       {handle(Smi::FromInt(10), isolate), handle(Smi::FromInt(20), isolate)}},
      {"var a = 10;"
       "function inner() { return eval('a + p1;');}"
       "return inner();",
       {handle(Smi::FromInt(30), isolate), handle(Smi::FromInt(20), isolate)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);
    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderEvalGlobal) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"function add_global() { eval('function f() { z = 33; }; f()'); };"
       "function f() { add_global(); return z; }; f();",
       {handle(Smi::FromInt(33), isolate)}},
      {"function add_global() {\n"
       " eval('\"use strict\"; function f() { y = 33; };"
       "      try { f() } catch(e) {}');\n"
       "}\n"
       "function f() { add_global(); return typeof y; } f();",
       {MakeString("undefined")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    BytecodeGraphTester tester(isolate, snippets[i].code_snippet);
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

bool get_compare_result(Isolate* isolate, Token::Value opcode,
                        Handle<Object> lhs_value, Handle<Object> rhs_value) {
  switch (opcode) {
    case Token::kEq:
      return Object::Equals(isolate, lhs_value, rhs_value).FromJust();
    case Token::kNotEq:
      return !Object::Equals(isolate, lhs_value, rhs_value).FromJust();
    case Token::kEqStrict:
      return Object::StrictEquals(*lhs_value, *rhs_value);
    case Token::kNotEqStrict:
      return !Object::StrictEquals(*lhs_value, *rhs_value);
    case Token::kLessThan:
      return Object::LessThan(isolate, lhs_value, rhs_value).FromJust();
    case Token::kLessThanEq:
      return Object::LessThanOrEqual(isolate, lhs_value, rhs_value).FromJust();
    case Token::kGreaterThan:
      return Object::GreaterThan(isolate, lhs_value, rhs_value).FromJust();
    case Token::kGreaterThanEq:
      return Object::GreaterThanOrEqual(isolate, lhs_value, rhs_value)
          .FromJust();
    default:
      UNREACHABLE();
  }
}

const char* get_code_snippet(Token::Value opcode) {
  switch (opcode) {
    case Token::kEq:
      return "return p1 == p2;";
    case Token::kNotEq:
      return "return p1 != p2;";
    case Token::kEqStrict:
      return "return p1 === p2;";
    case Token::kNotEqStrict:
      return "return p1 !== p2;";
    case Token::kLessThan:
      return "return p1 < p2;";
    case Token::kLessThanEq:
      return "return p1 <= p2;";
    case Token::kGreaterThan:
      return "return p1 > p2;";
    case Token::kGreaterThanEq:
      return "return p1 >= p2;";
    default:
      UNREACHABLE();
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCompare) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();
  Handle<Object> lhs_values[] = {
      factory->NewNumberFromInt(10), factory->NewHeapNumber(3.45),
      MakeString("abc"), factory->NewNumberFromInt(SMI_MAX),
      factory->NewNumberFromInt(SMI_MIN)};
  Handle<Object> rhs_values[] = {
      factory->NewNumberFromInt(10),     MakeString("10"),
      factory->NewNumberFromInt(20),     MakeString("abc"),
      factory->NewHeapNumber(3.45),      factory->NewNumberFromInt(SMI_MAX),
      factory->NewNumberFromInt(SMI_MIN)};

  for (size_t i = 0; i < arraysize(kCompareOperators); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s({}, {});", kFunctionName,
             get_code_snippet(kCompareOperators[i]), kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    for (size_t j = 0; j < arraysize(lhs_values); j++) {
      for (size_t k = 0; k < arraysize(rhs_values); k++) {
        DirectHandle<Object> return_value =
            callable(lhs_values[j], rhs_values[k]).ToHandleChecked();
        bool result = get_compare_result(isolate, kCompareOperators[i],
                                         lhs_values[j], rhs_values[k]);
        CHECK(Object::SameValue(*return_value, *factory->ToBoolean(result)));
      }
    }
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTestIn) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<2> snippets[] = {
      {"return p2 in p1;",
       {factory->true_value(), RunJS("({val : 10})"), MakeString("val")}},
      {"return p2 in p1;",
       {factory->true_value(), RunJS("[]"), MakeString("length")}},
      {"return p2 in p1;",
       {factory->true_value(), RunJS("[]"), MakeString("toString")}},
      {"return p2 in p1;",
       {factory->true_value(), RunJS("({val : 10})"), MakeString("toString")}},
      {"return p2 in p1;",
       {factory->false_value(), RunJS("({val : 10})"), MakeString("abc")}},
      {"return p2 in p1;",
       {factory->false_value(), RunJS("({val : 10})"),
        factory->NewNumberFromInt(10)}},
      {"return p2 in p1;",
       {factory->true_value(), RunJS("({10 : 'val'})"),
        factory->NewNumberFromInt(10)}},
      {"return p2 in p1;",
       {factory->false_value(), RunJS("({10 : 'val'})"),
        factory->NewNumberFromInt(1)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1, p2) { %s }\n%s({}, {});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTestInstanceOf) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return p1 instanceof Object;",
       {factory->true_value(), RunJS("({val : 10})")}},
      {"return p1 instanceof String;",
       {factory->false_value(), MakeString("string")}},
      {"var cons = function() {};"
       "var obj = new cons();"
       "return obj instanceof cons;",
       {factory->true_value(), factory->undefined_value()}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s(p1) { %s }\n%s({});", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTryCatch) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"var a = 1; try { a = 2 } catch(e) { a = 3 }; return a;",
       {handle(Smi::FromInt(2), isolate)}},
      {"var a; try { undef.x } catch(e) { a = 2 }; return a;",
       {handle(Smi::FromInt(2), isolate)}},
      {"var a; try { throw 1 } catch(e) { a = e + 2 }; return a;",
       {handle(Smi::FromInt(3), isolate)}},
      {"var a; try { throw 1 } catch(e) { a = e + 2 };"
       "       try { throw a } catch(e) { a = e + 3 }; return a;",
       {handle(Smi::FromInt(6), isolate)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTryFinally1) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"var a = 1; try { a = a + 1; } finally { a = a + 2; }; return a;",
       {handle(Smi::FromInt(4), isolate)}},
      {"var a = 1; try { a = 2; return 23; } finally { a = 3 }; return a;",
       {handle(Smi::FromInt(23), isolate)}},
      {"var a = 1; try { a = 2; throw 23; } finally { return a; };",
       {handle(Smi::FromInt(2), isolate)}},
      {"var a = 1; for (var i = 10; i < 20; i += 5) {"
       "  try { a = 2; break; } finally { a = 3; }"
       "} return a + i;",
       {handle(Smi::FromInt(13), isolate)}},
      {"var a = 1; for (var i = 10; i < 20; i += 5) {"
       "  try { a = 2; continue; } finally { a = 3; }"
       "} return a + i;",
       {handle(Smi::FromInt(23), isolate)}},
      {"var a = 1; try { a = 2;"
       "  try { a = 3; throw 23; } finally { a = 4; }"
       "} catch(e) { a = a + e; } return a;",
       {handle(Smi::FromInt(27), isolate)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderTryFinally2) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0, const char*> snippets[] = {
      {"var a = 1; try { a = 2; throw 23; } finally { a = 3 }; return a;",
       {"Uncaught 23"}},
      {"var a = 1; try { a = 2; throw 23; } finally { throw 42; };",
       {"Uncaught 42"}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string =
        NewString(snippets[i].return_value());
    CHECK(message->Equals(v8_isolate()->GetCurrentContext(), expected_string)
              .FromJust());
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderThrow) {
  Isolate* isolate = i_isolate();

  // TODO(mythria): Add more tests when real try-catch and deoptimization
  // information are supported.
  ExpectedSnippet<0, const char*> snippets[] = {
      {"throw undefined;", {"Uncaught undefined"}},
      {"throw 1;", {"Uncaught 1"}},
      {"throw 'Error';", {"Uncaught Error"}},
      {"throw 'Error1'; throw 'Error2'", {"Uncaught Error1"}},
      {"var a = true; if (a) { throw 'Error'; }", {"Uncaught Error"}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string =
        NewString(snippets[i].return_value());
    CHECK(message->Equals(v8_isolate()->GetCurrentContext(), expected_string)
              .FromJust());
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderContext) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner';"
       "   (function() {x});"
       " }"
       "return(x);"
       "}"
       "f();",
       {MakeString("outer")}},
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner ';"
       "   var innerFunc = function() {return x};"
       " }"
       "return(innerFunc() + x);"
       "}"
       "f();",
       {MakeString("inner outer")}},
      {"var x = 'outer';"
       "function f() {"
       " 'use strict';"
       " {"
       "   let x = 'inner ';"
       "   var innerFunc = function() {return x;};"
       "   {"
       "     let x = 'innermost ';"
       "     var innerMostFunc = function() {return x + innerFunc();};"
       "   }"
       "   x = 'inner_changed ';"
       " }"
       " return(innerMostFunc() + x);"
       "}"
       "f();",
       {MakeString("innermost inner_changed outer")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s", snippets[i].code_snippet);

    BytecodeGraphTester tester(isolate, script.begin(), "f");
    auto callable = tester.GetCallable<>("f");
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderLoadContext) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"function Outer() {"
       "  var outerVar = 2;"
       "  function Inner(innerArg) {"
       "    this.innerFunc = function () {"
       "     return outerVar * innerArg;"
       "    };"
       "  };"
       "  this.getInnerFunc = function GetInner() {"
       "     return new Inner(3).innerFunc;"
       "   }"
       "}"
       "var f = new Outer().getInnerFunc();"
       "f();",
       {factory->NewNumberFromInt(6), factory->undefined_value()}},
      {"function Outer() {"
       "  var outerVar = 2;"
       "  function Inner(innerArg) {"
       "    this.innerFunc = function () {"
       "     outerVar = innerArg; return outerVar;"
       "    };"
       "  };"
       "  this.getInnerFunc = function GetInner() {"
       "     return new Inner(10).innerFunc;"
       "   }"
       "}"
       "var f = new Outer().getInnerFunc();"
       "f();",
       {factory->NewNumberFromInt(10), factory->undefined_value()}},
      {"function testOuter(outerArg) {"
       " this.testinnerFunc = function testInner(innerArg) {"
       "   return innerArg + outerArg;"
       " }"
       "}"
       "var f = new testOuter(10).testinnerFunc;"
       "f(0);",
       {factory->NewNumberFromInt(14), factory->NewNumberFromInt(4)}},
      {"function testOuter(outerArg) {"
       " var outerVar = outerArg * 2;"
       " this.testinnerFunc = function testInner(innerArg) {"
       "   outerVar = outerVar + innerArg; return outerVar;"
       " }"
       "}"
       "var f = new testOuter(10).testinnerFunc;"
       "f(0);",
       {factory->NewNumberFromInt(24), factory->NewNumberFromInt(4)}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s", snippets[i].code_snippet);

    BytecodeGraphTester tester(isolate, script.begin(), "*");
    auto callable = tester.GetCallable<Handle<Object>>("f");
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest,
       BytecodeGraphBuilderCreateArgumentsNoParameters) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"function f() {return arguments[0];}", {factory->undefined_value()}},
      {"function f(a) {return arguments[0];}", {factory->undefined_value()}},
      {"function f() {'use strict'; return arguments[0];}",
       {factory->undefined_value()}},
      {"function f(a) {'use strict'; return arguments[0];}",
       {factory->undefined_value()}},
      {"function f(...restArgs) {return restArgs[0];}",
       {factory->undefined_value()}},
      {"function f(a, ...restArgs) {return restArgs[0];}",
       {factory->undefined_value()}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s\n%s();", snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCreateArguments) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<3> snippets[] = {
      {"function f(a, b, c) {return arguments[0];}",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, c) {return arguments[3];}",
       {factory->undefined_value(), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, c) { b = c; return arguments[1];}",
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, c) {'use strict'; return arguments[0];}",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, c) {'use strict'; return arguments[3];}",
       {factory->undefined_value(), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, c) {'use strict'; b = c; return arguments[1];}",
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function inline_func(a, b) { return arguments[0] }"
       "function f(a, b, c) {return inline_func(b, c) + arguments[0];}",
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(30)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s\n%s();", snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable =
        tester.GetCallable<Handle<Object>, Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1),
                 snippets[i].parameter(2))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderCreateRestArguments) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<3> snippets[] = {
      {"function f(...restArgs) {return restArgs[0];}",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, ...restArgs) {return restArgs[0];}",
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, b, ...restArgs) {return arguments[2];}",
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, ...restArgs) { return restArgs[2];}",
       {factory->undefined_value(), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function f(a, ...restArgs) { return arguments[0] + restArgs[1];}",
       {factory->NewNumberFromInt(4), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {"function inline_func(a, ...restArgs) { return restArgs[0] }"
       "function f(a, b, c) {return inline_func(b, c) + arguments[0];}",
       {factory->NewNumberFromInt(31), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2), factory->NewNumberFromInt(30)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s\n%s();", snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable =
        tester.GetCallable<Handle<Object>, Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1),
                 snippets[i].parameter(2))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderRegExpLiterals) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return /abd/.exec('cccabbdd');", {factory->null_value()}},
      {"return /ab+d/.exec('cccabbdd')[0];", {MakeString("abbd")}},
      {"var a = 3.1414;" REPEAT_256(
           SPACE, "a = 3.1414;") "return /ab+d/.exec('cccabbdd')[0];",
       {MakeString("abbd")}},
      {"return /ab+d/.exec('cccabbdd')[1];", {factory->undefined_value()}},
      {"return /AbC/i.exec('ssaBC')[0];", {MakeString("aBC")}},
      {"return 'ssaBC'.match(/AbC/i)[0];", {MakeString("aBC")}},
      {"return 'ssaBCtAbC'.match(/(AbC)/gi)[1];", {MakeString("AbC")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(4096);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderArrayLiterals) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return [][0];", {factory->undefined_value()}},
      {"return [1, 3, 2][1];", {factory->NewNumberFromInt(3)}},
      {"var a;" REPEAT_256(SPACE, "a = 9.87;") "return [1, 3, 2][1];",
       {factory->NewNumberFromInt(3)}},
      {"return ['a', 'b', 'c'][2];", {MakeString("c")}},
      {"var a = 100; return [a, a++, a + 2, a + 3][2];",
       {factory->NewNumberFromInt(103)}},
      {"var a = 100; return [a, ++a, a + 2, a + 3][1];",
       {factory->NewNumberFromInt(101)}},
      {"var a = 9.2;" REPEAT_256(
           SPACE, "a = 9.34;") "return [a, ++a, a + 2, a + 3][2];",
       {factory->NewHeapNumber(12.34)}},
      {"return [[1, 2, 3], ['a', 'b', 'c']][1][0];", {MakeString("a")}},
      {"var t = 't'; return [[t, t + 'est'], [1 + t]][0][1];",
       {MakeString("test")}},
      {"var t = 't'; return [[t, t + 'est'], [1 + t]][1][0];",
       {MakeString("1t")}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(4096);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderObjectLiterals) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"return { }.name;", {factory->undefined_value()}},
      {"return { name: 'string', val: 9.2 }.name;", {MakeString("string")}},
      {"var a;\n" REPEAT_256(
           SPACE, "a = 1.23;\n") "return { name: 'string', val: 9.2 }.name;",
       {MakeString("string")}},
      {"return { name: 'string', val: 9.2 }['name'];", {MakeString("string")}},
      {"var a = 15; return { name: 'string', val: a }.val;",
       {factory->NewNumberFromInt(15)}},
      {"var a;" REPEAT_256(
           SPACE, "a = 1.23;") "return { name: 'string', val: a }.val;",
       {factory->NewHeapNumber(1.23)}},
      {"var a = 15; var b = 'val'; return { name: 'string', val: a }[b];",
       {factory->NewNumberFromInt(15)}},
      {"var a = 5; return { val: a, val: a + 1 }.val;",
       {factory->NewNumberFromInt(6)}},
      {"return { func: function() { return 'test' } }.func();",
       {MakeString("test")}},
      {"return { func(a) { return a + 'st'; } }.func('te');",
       {MakeString("test")}},
      {"return { get a() { return 22; } }.a;", {factory->NewNumberFromInt(22)}},
      {"var a = { get b() { return this.x + 't'; },\n"
       "          set b(val) { this.x = val + 's' } };\n"
       "a.b = 'te';\n"
       "return a.b;",
       {MakeString("test")}},
      {"var a = 123; return { 1: a }[1];", {factory->NewNumberFromInt(123)}},
      {"return Object.getPrototypeOf({ __proto__: null });",
       {factory->null_value()}},
      {"var a = 'test'; return { [a]: 1 }.test;",
       {factory->NewNumberFromInt(1)}},
      {"var a = 'test'; return { b: a, [a]: a + 'ing' }['test']",
       {MakeString("testing")}},
      {"var a = 'proto_str';\n"
       "var b = { [a]: 1, __proto__: { var : a } };\n"
       "return Object.getPrototypeOf(b).var",
       {MakeString("proto_str")}},
      {"var n = 'name';\n"
       "return { [n]: 'val', get a() { return 987 } }['a'];",
       {factory->NewNumberFromInt(987)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(4096);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);
    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderIf) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"if (p1 > 1) return 1;\n"
       "return -1;",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(2)}},
      {"if (p1 > 1) return 1;\n"
       "return -1;",
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(1)}},
      {"if (p1 > 1) { return 1; } else { return -1; }",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(2)}},
      {"if (p1 > 1) { return 1; } else { return -1; }",
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(1)}},
      {"if (p1 > 50) {\n"
       "  return 1;\n"
       "} else if (p1 < 10) {\n"
       "   return 10;\n"
       "} else {\n"
       "   return -10;\n"
       "}",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(51)}},
      {"if (p1 > 50) {\n"
       "  return 1;\n"
       "} else if (p1 < 10) {\n"
       "   return 10;\n"
       "} else {\n"
       "   return 100;\n"
       "}",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(9)}},
      {"if (p1 > 50) {\n"
       "  return 1;\n"
       "} else if (p1 < 10) {\n"
       "   return 10;\n"
       "} else {\n"
       "   return 100;\n"
       "}",
       {factory->NewNumberFromInt(100), factory->NewNumberFromInt(10)}},
      {"if (p1 >= 0) {\n"
       "   if (p1 > 10) { return 2; } else { return 1; }\n"
       "} else {\n"
       "   if (p1 < -10) { return -2; } else { return -1; }\n"
       "}",
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(100)}},
      {"if (p1 >= 0) {\n"
       "   if (p1 > 10) { return 2; } else { return 1; }\n"
       "} else {\n"
       "   if (p1 < -10) { return -2; } else { return -1; }\n"
       "}",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(10)}},
      {"if (p1 >= 0) {\n"
       "   if (p1 > 10) { return 2; } else { return 1; }\n"
       "} else {\n"
       "   if (p1 < -10) { return -2; } else { return -1; }\n"
       "}",
       {factory->NewNumberFromInt(-2), factory->NewNumberFromInt(-11)}},
      {"if (p1 >= 0) {\n"
       "   if (p1 > 10) { return 2; } else { return 1; }\n"
       "} else {\n"
       "   if (p1 < -10) { return -2; } else { return -1; }\n"
       "}",
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(-10)}},
      {"var b = 20, c;"
       "if (p1 >= 0) {\n"
       "   if (b > 0) { c = 2; } else { c = 3; }\n"
       "} else {\n"
       "   if (b < -10) { c = -2; } else { c = -1; }\n"
       "}"
       "return c;",
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(-1)}},
      {"var b = 20, c = 10;"
       "if (p1 >= 0) {\n"
       "   if (b < 0) { c = 2; }\n"
       "} else {\n"
       "   if (b < -10) { c = -2; } else { c = -1; }\n"
       "}"
       "return c;",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(1)}},
      {"var x = 2, a = 10, b = 20, c, d;"
       "x = 0;"
       "if (a) {\n"
       "   b = x;"
       "   if (b > 0) { c = 2; } else { c = 3; }\n"
       "   x = 4; d = 2;"
       "} else {\n"
       "   d = 3;\n"
       "}"
       "x = d;"
       "function f1() {x}"
       "return x + c;",
       {factory->NewNumberFromInt(5), factory->NewNumberFromInt(-1)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderConditionalOperator) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<1> snippets[] = {
      {"return (p1 > 1) ? 1 : -1;",
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(2)}},
      {"return (p1 > 1) ? 1 : -1;",
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(0)}},
      {"return (p1 > 50) ? 1 : ((p1 < 10) ? 10 : -10);",
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(2)}},
      {"return (p1 > 50) ? 1 : ((p1 < 10) ? 10 : -10);",
       {factory->NewNumberFromInt(-10), factory->NewNumberFromInt(20)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderSwitch) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  const char* switch_code =
      "switch (p1) {\n"
      "  case 1: return 0;\n"
      "  case 2: return 1;\n"
      "  case 3:\n"
      "  case 4: return 2;\n"
      "  case 9: break;\n"
      "  default: return 3;\n"
      "}\n"
      "return 9;";

  ExpectedSnippet<1> snippets[] = {
      {switch_code,
       {factory->NewNumberFromInt(0), factory->NewNumberFromInt(1)}},
      {switch_code,
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(2)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(4)}},
      {switch_code,
       {factory->NewNumberFromInt(9), factory->NewNumberFromInt(9)}},
      {switch_code,
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(5)}},
      {switch_code,
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(6)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderSwitchMerge) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  const char* switch_code =
      "var x = 10;"
      "switch (p1) {\n"
      "  case 1: x = 0;\n"
      "  case 2: x = 1;\n"
      "  case 3:\n"
      "  case 4: x = 2; break;\n"
      "  case 5: x = 3;\n"
      "  case 9: break;\n"
      "  default: x = 4;\n"
      "}\n"
      "return x;";

  ExpectedSnippet<1> snippets[] = {
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(1)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(2)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(3)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(4)}},
      {switch_code,
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(5)}},
      {switch_code,
       {factory->NewNumberFromInt(10), factory->NewNumberFromInt(9)}},
      {switch_code,
       {factory->NewNumberFromInt(4), factory->NewNumberFromInt(6)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1) { %s };\n%s(0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0)).ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderNestedSwitch) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  const char* switch_code =
      "switch (p1) {\n"
      "  case 0: {"
      "    switch (p2) { case 0: return 0; case 1: return 1; case 2: break; }\n"
      "    return -1;"
      "  }\n"
      "  case 1: {"
      "    switch (p2) { case 0: return 2; case 1: return 3; }\n"
      "  }\n"
      "  case 2: break;"
      "  }\n"
      "return -2;";

  ExpectedSnippet<2> snippets[] = {
      {switch_code,
       {factory->NewNumberFromInt(0), factory->NewNumberFromInt(0),
        factory->NewNumberFromInt(0)}},
      {switch_code,
       {factory->NewNumberFromInt(1), factory->NewNumberFromInt(0),
        factory->NewNumberFromInt(1)}},
      {switch_code,
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(0),
        factory->NewNumberFromInt(2)}},
      {switch_code,
       {factory->NewNumberFromInt(-1), factory->NewNumberFromInt(0),
        factory->NewNumberFromInt(3)}},
      {switch_code,
       {factory->NewNumberFromInt(2), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(0)}},
      {switch_code,
       {factory->NewNumberFromInt(3), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(1)}},
      {switch_code,
       {factory->NewNumberFromInt(-2), factory->NewNumberFromInt(1),
        factory->NewNumberFromInt(2)}},
      {switch_code,
       {factory->NewNumberFromInt(-2), factory->NewNumberFromInt(2),
        factory->NewNumberFromInt(0)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(2048);
    SNPrintF(script, "function %s(p1, p2) { %s };\n%s(0, 0);", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<Handle<Object>, Handle<Object>>();
    DirectHandle<Object> return_value =
        callable(snippets[i].parameter(0), snippets[i].parameter(1))
            .ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderBreakableBlocks) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var x = 0;\n"
       "my_heart: {\n"
       "  x = x + 1;\n"
       "  break my_heart;\n"
       "  x = x + 2;\n"
       "}\n"
       "return x;\n",
       {factory->NewNumberFromInt(1)}},
      {"var sum = 0;\n"
       "outta_here: {\n"
       "  for (var x = 0; x < 10; ++x) {\n"
       "    for (var y = 0; y < 3; ++y) {\n"
       "      ++sum;\n"
       "      if (x + y == 12) { break outta_here; }\n"
       "    }\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumber(30)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderWhile) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var x = 1; while (x < 1) { x *= 100; } return x;",
       {factory->NewNumberFromInt(1)}},
      {"var x = 1, y = 0; while (x < 7) { y += x * x; x += 1; } return y;",
       {factory->NewNumberFromInt(91)}},
      {"var x = 1; while (true) { x += 1; if (x == 10) break; } return x;",
       {factory->NewNumberFromInt(10)}},
      {"var x = 1; while (false) { x += 1; } return x;",
       {factory->NewNumberFromInt(1)}},
      {"var x = 0;\n"
       "while (true) {\n"
       "  while (x < 10) {\n"
       "    x = x * x + 1;\n"
       "  }"
       "  x += 1;\n"
       "  break;\n"
       "}\n"
       "return x;",
       {factory->NewNumberFromInt(27)}},
      {"var x = 1, y = 0;\n"
       "while (x < 7) {\n"
       "  x += 1;\n"
       "  if (x == 2) continue;\n"
       "  if (x == 3) continue;\n"
       "  y += x * x;\n"
       "  if (x == 4) break;\n"
       "}\n"
       "return y;",
       {factory->NewNumberFromInt(16)}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderDo) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"var x = 1; do { x *= 100; } while (x < 100); return x;",
       {factory->NewNumberFromInt(100)}},
      {"var x = 1; do { x = x * x + 1; } while (x < 7) return x;",
       {factory->NewNumberFromInt(26)}},
      {"var x = 1; do { x += 1; } while (false); return x;",
       {factory->NewNumberFromInt(2)}},
      {"var x = 1, y = 0;\n"
       "do {\n"
       "  x += 1;\n"
       "  if (x == 2) continue;\n"
       "  if (x == 3) continue;\n"
       "  y += x * x;\n"
       "  if (x == 4) break;\n"
       "} while (x < 7);\n"
       "return y;",
       {factory->NewNumberFromInt(16)}},
      {"var x = 0, sum = 0;\n"
       "do {\n"
       "  do {\n"
       "    ++sum;\n"
       "    ++x;\n"
       "  } while (sum < 1 || x < 2)\n"
       "  do {\n"
       "    ++x;\n"
       "  } while (x < 1)\n"
       "} while (sum < 3)\n"
       "return sum;",
       {factory->NewNumber(3)}}};

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderFor) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"for (var x = 0;; x = 2 * x + 1) { if (x > 10) return x; }",
       {factory->NewNumberFromInt(15)}},
      {"for (var x = 0; true; x = 2 * x + 1) { if (x > 100) return x; }",
       {factory->NewNumberFromInt(127)}},
      {"for (var x = 0; false; x = 2 * x + 1) { if (x > 100) return x; } "
       "return 0;",
       {factory->NewNumberFromInt(0)}},
      {"for (var x = 0; x < 200; x = 2 * x + 1) { x = x; } return x;",
       {factory->NewNumberFromInt(255)}},
      {"for (var x = 0; x < 200; x = 2 * x + 1) {} return x;",
       {factory->NewNumberFromInt(255)}},
      {"var sum = 0;\n"
       "for (var x = 0; x < 200; x += 1) {\n"
       "  if (x % 2) continue;\n"
       "  if (sum > 10) break;\n"
       "  sum += x;\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(12)}},
      {"var sum = 0;\n"
       "for (var w = 0; w < 2; w++) {\n"
       "  for (var x = 0; x < 200; x += 1) {\n"
       "    if (x % 2) continue;\n"
       "    if (x > 4) break;\n"
       "    sum += x + w;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(15)}},
      {"var sum = 0;\n"
       "for (var w = 0; w < 2; w++) {\n"
       "  if (w == 1) break;\n"
       "  for (var x = 0; x < 200; x += 1) {\n"
       "    if (x % 2) continue;\n"
       "    if (x > 4) break;\n"
       "    sum += x + w;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(6)}},
      {"var sum = 0;\n"
       "for (var w = 0; w < 3; w++) {\n"
       "  if (w == 1) continue;\n"
       "  for (var x = 0; x < 200; x += 1) {\n"
       "    if (x % 2) continue;\n"
       "    if (x > 4) break;\n"
       "    sum += x + w;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(18)}},
      {"var sum = 0;\n"
       "for (var x = 1; x < 10; x += 2) {\n"
       "  for (var y = x; y < x + 2; y++) {\n"
       "    sum += y * y;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(385)}},
      {"var sum = 0;\n"
       "for (var x = 0; x < 5; x++) {\n"
       "  for (var y = 0; y < 5; y++) {\n"
       "    ++sum;\n"
       "  }\n"
       "}\n"
       "for (var x = 0; x < 5; x++) {\n"
       "  for (var y = 0; y < 5; y++) {\n"
       "    ++sum;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(50)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderForIn) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();
  ExpectedSnippet<0> snippets[] = {
      {"var sum = 0;\n"
       "var empty = null;\n"
       "for (var x in empty) { sum++; }\n"
       "return sum;",
       {factory->NewNumberFromInt(0)}},
      {"var sum = 100;\n"
       "var empty = 1;\n"
       "for (var x in empty) { sum++; }\n"
       "return sum;",
       {factory->NewNumberFromInt(100)}},
      {"for (var x in [ 10, 20, 30 ]) {}\n"
       "return 2;",
       {factory->NewNumberFromInt(2)}},
      {"var last = 0;\n"
       "for (var x in [ 10, 20, 30 ]) {\n"
       "  last = x;\n"
       "}\n"
       "return +last;",
       {factory->NewNumberFromInt(2)}},
      {"var first = -1;\n"
       "for (var x in [ 10, 20, 30 ]) {\n"
       "  first = +x;\n"
       "  if (first > 0) break;\n"
       "}\n"
       "return first;",
       {factory->NewNumberFromInt(1)}},
      {"var first = -1;\n"
       "for (var x in [ 10, 20, 30 ]) {\n"
       "  if (first >= 0) continue;\n"
       "  first = x;\n"
       "}\n"
       "return +first;",
       {factory->NewNumberFromInt(0)}},
      {"var sum = 0;\n"
       "for (var x in [ 10, 20, 30 ]) {\n"
       "  for (var y in [ 11, 22, 33, 44, 55, 66, 77 ]) {\n"
       "    sum += 1;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(21)}},
      {"var sum = 0;\n"
       "for (var x in [ 10, 20, 30 ]) {\n"
       "  for (var y in [ 11, 22, 33, 44, 55, 66, 77 ]) {\n"
       "    if (sum == 7) break;\n"
       "    if (sum == 6) continue;\n"
       "    sum += 1;\n"
       "  }\n"
       "}\n"
       "return sum;",
       {factory->NewNumberFromInt(6)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderForOf) {
  Isolate* isolate = i_isolate();
  ExpectedSnippet<0> snippets[] = {
      {"  var r = 0;\n"
       "  for (var a of [0,6,7,9]) { r += a; }\n"
       "  return r;\n",
       {handle(Smi::FromInt(22), isolate)}},
      {"  var r = '';\n"
       "  for (var a of 'foobar') { r = a + r; }\n"
       "  return r;\n",
       {MakeString("raboof")}},
      {"  var a = [1, 2, 3];\n"
       "  a.name = 4;\n"
       "  var r = 0;\n"
       "  for (var x of a) { r += x; }\n"
       "  return r;\n",
       {handle(Smi::FromInt(6), isolate)}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data[0]; r += a; } return r;",
       {MakeString("123")}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data[2]; r += a; } return r;",
       {MakeString("12undefined")}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3]; \n"
       "  for (a of data) { delete data; r += a; } return r;",
       {MakeString("123")}},
      {"  var r = '';\n"
       "  var input = 'foobar';\n"
       "  for (var a of input) {\n"
       "    if (a == 'b') break;\n"
       "    r += a;\n"
       "  }\n"
       "  return r;\n",
       {MakeString("foo")}},
      {"  var r = '';\n"
       "  var input = 'foobar';\n"
       "  for (var a of input) {\n"
       "    if (a == 'b') continue;\n"
       "    r += a;\n"
       "  }\n"
       "  return r;\n",
       {MakeString("fooar")}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[2] = 567; r += a; }\n"
       "  return r;\n",
       {MakeString("125674")}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[4] = 567; r += a; }\n"
       "  return r;\n",
       {MakeString("1234567")}},
      {"  var r = '';\n"
       "  var data = [1, 2, 3, 4]; \n"
       "  for (a of data) { data[5] = 567; r += a; }\n"
       "  return r;\n",
       {MakeString("1234undefined567")}},
      {"  var r = '';\n"
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
       "  return r;\n",
       {MakeString("dcba")}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

SHARD_TEST_BY_4(JumpWithConstantsAndWideConstants)

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderWithStatement) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0> snippets[] = {
      {"with({x:42}) return x;", {handle(Smi::FromInt(42), isolate)}},
      {"with({}) { var y = 10; return y;}",
       {handle(Smi::FromInt(10), isolate)}},
      {"var y = {x:42};"
       " function inner() {"
       "   var x = 20;"
       "   with(y) return x;"
       "}"
       "return inner();",
       {handle(Smi::FromInt(42), isolate)}},
      {"var y = {x:42};"
       " function inner(o) {"
       "   var x = 20;"
       "   with(o) return x;"
       "}"
       "return inner(y);",
       {handle(Smi::FromInt(42), isolate)}},
  };

  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderConstDeclaration) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"const x = 3; return x;", {handle(Smi::FromInt(3), isolate)}},
      {"let x = 10; x = x + 20; return x;",
       {handle(Smi::FromInt(30), isolate)}},
      {"let x = 10; x = 20; return x;", {handle(Smi::FromInt(20), isolate)}},
      {"let x; x = 20; return x;", {handle(Smi::FromInt(20), isolate)}},
      {"let x; return x;", {factory->undefined_value()}},
      {"var x = 10; { let x = 30; } return x;",
       {handle(Smi::FromInt(10), isolate)}},
      {"let x = 10; { let x = 20; } return x;",
       {handle(Smi::FromInt(10), isolate)}},
      {"var x = 10; eval('let x = 20;'); return x;",
       {handle(Smi::FromInt(10), isolate)}},
      {"var x = 10; eval('const x = 20;'); return x;",
       {handle(Smi::FromInt(10), isolate)}},
      {"var x = 10; { const x = 20; } return x;",
       {handle(Smi::FromInt(10), isolate)}},
      {"var x = 10; { const x = 20; return x;} return -1;",
       {handle(Smi::FromInt(20), isolate)}},
      {"var a = 10;\n"
       "for (var i = 0; i < 10; ++i) {\n"
       " const x = i;\n"  // const declarations are block scoped.
       " a = a + x;\n"
       "}\n"
       "return a;\n",
       {handle(Smi::FromInt(55), isolate)}},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() {'use strict'; %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest,
       BytecodeGraphBuilderConstDeclarationLookupSlots) {
  Isolate* isolate = i_isolate();
  Factory* factory = isolate->factory();

  ExpectedSnippet<0> snippets[] = {
      {"const x = 3; function f1() {return x;}; return x;",
       {handle(Smi::FromInt(3), isolate)}},
      {"let x = 10; x = x + 20; function f1() {return x;}; return x;",
       {handle(Smi::FromInt(30), isolate)}},
      {"let x; x = 20; function f1() {return x;}; return x;",
       {handle(Smi::FromInt(20), isolate)}},
      {"let x; function f1() {return x;}; return x;",
       {factory->undefined_value()}},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(snippets); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() {'use strict'; %s }\n%s();", kFunctionName,
             snippets[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *snippets[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest,
       BytecodeGraphBuilderConstInLookupContextChain) {
  Isolate* isolate = i_isolate();

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

  // Tests for let / constant.
  ExpectedSnippet<0> const_decl[] = {
      {"return outerConst;", {handle(Smi::FromInt(10), isolate)}},
      {"return outerLet;", {handle(Smi::FromInt(20), isolate)}},
      {"outerLet = 30; return outerLet;", {handle(Smi::FromInt(30), isolate)}},
      {"var outerLet = 40; return outerLet;",
       {handle(Smi::FromInt(40), isolate)}},
      {"var outerConst = 50; return outerConst;",
       {handle(Smi::FromInt(50), isolate)}},
      {"try { outerConst = 30 } catch(e) { return -1; }",
       {handle(Smi::FromInt(-1), isolate)}}};

  for (size_t i = 0; i < arraysize(const_decl); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "%s %s %s", prologue, const_decl[i].code_snippet,
             epilogue);

    BytecodeGraphTester tester(isolate, script.begin(), "*");
    auto callable = tester.GetCallable<>();
    DirectHandle<Object> return_value = callable().ToHandleChecked();
    CHECK(Object::SameValue(*return_value, *const_decl[i].return_value()));
  }
}

TEST_F(RunBytecodeGraphBuilderTest,
       BytecodeGraphBuilderIllegalConstDeclaration) {
  Isolate* isolate = i_isolate();

  ExpectedSnippet<0, const char*> illegal_const_decl[] = {
      {"const x = x = 10 + 3; return x;",
       {"Uncaught ReferenceError: Cannot access 'x' before initialization"}},
      {"const x = 10; x = 20; return x;",
       {"Uncaught TypeError: Assignment to constant variable."}},
      {"const x = 10; { x = 20; } return x;",
       {"Uncaught TypeError: Assignment to constant variable."}},
      {"const x = 10; eval('x = 20;'); return x;",
       {"Uncaught TypeError: Assignment to constant variable."}},
      {"let x = x + 10; return x;",
       {"Uncaught ReferenceError: Cannot access 'x' before initialization"}},
      {"'use strict'; (function f1() { f1 = 123; })() ",
       {"Uncaught TypeError: Assignment to constant variable."}},
  };

  // Tests for sloppy mode.
  for (size_t i = 0; i < arraysize(illegal_const_decl); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() { %s }\n%s();", kFunctionName,
             illegal_const_decl[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string =
        NewString(illegal_const_decl[i].return_value());
    CHECK(message->Equals(v8_isolate()->GetCurrentContext(), expected_string)
              .FromJust());
  }

  // Tests for strict mode.
  for (size_t i = 0; i < arraysize(illegal_const_decl); i++) {
    base::ScopedVector<char> script(1024);
    SNPrintF(script, "function %s() {'use strict'; %s }\n%s();", kFunctionName,
             illegal_const_decl[i].code_snippet, kFunctionName);

    BytecodeGraphTester tester(isolate, script.begin());
    v8::Local<v8::String> message = tester.CheckThrowsReturnMessage()->Get();
    v8::Local<v8::String> expected_string =
        NewString(illegal_const_decl[i].return_value());
    CHECK(message->Equals(v8_isolate()->GetCurrentContext(), expected_string)
              .FromJust());
  }
}

class CountBreakDebugDelegate : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context> paused_context,
                             const std::vector<int>&,
                             v8::debug::BreakReasons break_reasons) override {
    debug_break_count++;
  }
  int debug_break_count = 0;
};

TEST_F(RunBytecodeGraphBuilderTest, BytecodeGraphBuilderDebuggerStatement) {
  CountBreakDebugDelegate delegate;
  Isolate* isolate = i_isolate();

  v8::debug::SetDebugDelegate(v8_isolate(), &delegate);

  ExpectedSnippet<0> snippet = {
      "function f() {"
      "  debugger;"
      "}"
      "f();",
      {isolate->factory()->undefined_value()}};

  BytecodeGraphTester tester(isolate, snippet.code_snippet);
  auto callable = tester.GetCallable<>();
  Handle<Object> return_value = callable().ToHandleChecked();

  v8::debug::SetDebugDelegate(v8_isolate(), nullptr);
  CHECK(return_value.is_identical_to(snippet.return_value()));
  CHECK_EQ(2, delegate.debug_break_count);
}

#undef SHARD_TEST_BY_2
#undef SHARD_TEST_BY_4
#undef SPACE
#undef REPEAT_2
#undef REPEAT_4
#undef REPEAT_8
#undef REPEAT_16
#undef REPEAT_32
#undef REPEAT_64
#undef REPEAT_128
#undef REPEAT_256
#undef REPEAT_127

}  // namespace compiler
}  // namespace internal
}  // namespace v8
