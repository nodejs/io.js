// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-script.h"

#include <algorithm>

#include "include/v8-context.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/objects/objects-inl.h"
#include "test/common/flag-utils.h"
#include "test/common/streaming-helper.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

namespace {
bool ValueEqualsString(v8::Isolate* isolate, Local<Value> lhs,
                       const char* rhs) {
  CHECK(!lhs.IsEmpty());
  CHECK(lhs->IsString());
  String::Utf8Value utf8_lhs(isolate, lhs);
  return strcmp(rhs, *utf8_lhs) == 0;
}

std::string from_v8_string(Isolate* isolate, Local<String> str) {
  String::Utf8Value utf8(isolate, str);
  return *utf8;
}

v8::MaybeLocal<Module> ResolveToTopLevelAwait(Local<Context> context,
                                              Local<String> specifier,
                                              Local<FixedArray> assertions,
                                              Local<Module> referrer) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::ScriptOrigin origin(specifier, 0, 0, false, -1, Local<Value>(), false,
                          false, true);

  String::Utf8Value specifier_string(isolate, specifier);
  std::string source_string =
      "const promise = new Promise((resolve, reject) => {\n"
      "  if (should_resolve) {\n"
      "    resolve();\n"
      "  }\n"
      "});\n"
      "await promise;\n";
  if (strncmp(*specifier_string, "stall", strlen("stall")) == 0) {
    source_string = "const should_resolve = false;\n" + source_string;
  } else if (strncmp(*specifier_string, "resolve", strlen("resolve")) == 0) {
    source_string = "const should_resolve = true;\n" + source_string;
  } else {
    UNREACHABLE();
  }

  v8::ScriptCompiler::Source source(
      v8::String::NewFromUtf8(isolate, source_string.c_str()).ToLocalChecked(),
      origin);
  auto res = v8::ScriptCompiler::CompileModule(isolate, &source);
  return res;
}

class ScriptTest : public TestWithContext {
 protected:
  void TestGetStalledTopLevelAwaitMessage(
      const char* source_str, std::vector<std::string> expected_stalled) {
    v8::Isolate::Scope iscope(isolate());
    v8::HandleScope scope(isolate());
    v8::Local<v8::Context> context = v8::Context::New(isolate());
    v8::Context::Scope cscope(context);

    v8::ScriptOrigin origin(NewString("root.mjs"), 0, 0, false, -1,
                            Local<Value>(), false, false, true);
    v8::ScriptCompiler::Source source(NewString(source_str), origin);
    Local<Module> root =
        v8::ScriptCompiler::CompileModule(isolate(), &source).ToLocalChecked();

    CHECK(root->InstantiateModule(context, ResolveToTopLevelAwait)
              .FromMaybe(false));

    Local<v8::Promise> promise =
        root->Evaluate(context).ToLocalChecked().As<v8::Promise>();
    isolate()->PerformMicrotaskCheckpoint();
    CHECK_EQ(expected_stalled.size() > 0
                 ? v8::Promise::PromiseState::kPending
                 : v8::Promise::PromiseState::kFulfilled,
             promise->State());

    auto [stalled_modules, stalled_messages] =
        root->GetStalledTopLevelAwaitMessages(isolate());
    CHECK_EQ(expected_stalled.size(), stalled_modules.size());
    CHECK_EQ(expected_stalled.size(), stalled_messages.size());
    for (size_t i = 0; i < expected_stalled.size(); ++i) {
      Local<Message> message = stalled_messages[i];
      CHECK_EQ("Top-level await promise never resolved",
               from_v8_string(isolate(), message->Get()));
      CHECK_EQ(expected_stalled[i],
               from_v8_string(isolate(),
                              message->GetScriptResourceName().As<String>()));
      CHECK_EQ(
          "await promise;",
          from_v8_string(isolate(),
                         message->GetSourceLine(context).ToLocalChecked()));

      CHECK_EQ(7, message->GetLineNumber(context).ToChecked());
      CHECK_EQ(0, message->GetStartColumn(context).ToChecked());
      CHECK_EQ(1, message->GetEndColumn(context).ToChecked());
    }
  }
};

class CompileHintsTest : public ScriptTest {
 protected:
  std::vector<int> ProduceCompileHintsHelper(
      std::initializer_list<const char*> sources) {
    const char* url = "http://www.foo.com/foo.js";
    v8::ScriptOrigin origin(NewString(url), 13, 0);

    Local<Script> top_level_script;
    bool first = true;
    for (auto source : sources) {
      v8::ScriptCompiler::Source script_source(NewString(source), origin);
      Local<Script> script =
          v8::ScriptCompiler::Compile(
              v8_context(), &script_source,
              v8::ScriptCompiler::CompileOptions::kProduceCompileHints)
              .ToLocalChecked();
      if (first) {
        top_level_script = script;
        first = false;
      }

      v8::MaybeLocal<v8::Value> result = script->Run(v8_context());
      EXPECT_FALSE(result.IsEmpty());
    }
    return top_level_script->GetCompileHintsCollector()->GetCompileHints(
        v8_isolate());
  }

  bool FunctionIsCompiled(const char* name) {
    const char* url = "http://www.foo.com/foo.js";
    v8::ScriptOrigin origin(NewString(url), 13, 0);

    v8::ScriptCompiler::Source script_source(NewString(name), origin);

    Local<Script> script =
        v8::ScriptCompiler::Compile(v8_context(), &script_source)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result = script->Run(v8_context());

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    return builtin != i::Builtin::kCompileLazy;
  }
};

}  // namespace

TEST_F(ScriptTest, UnboundScriptPosition) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::ScriptCompiler::Source script_source(NewString("var foo;"), origin);

  Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &script_source)
          .ToLocalChecked();
  EXPECT_TRUE(ValueEqualsString(
      isolate(), script->GetUnboundScript()->GetScriptName(), url));
  Local<UnboundScript> unbound_script = script->GetUnboundScript();

  int line_number = unbound_script->GetLineNumber();
  EXPECT_EQ(13, line_number);
  int column_number = unbound_script->GetColumnNumber();
  EXPECT_EQ(0, column_number);
}

TEST_F(ScriptTest, GetSourceMappingUrlFromComment) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url));
  v8::ScriptCompiler::Source script_source(
      NewString("var foo;\n//# sourceMappingURL=foo.js.map"), origin);

  Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &script_source)
          .ToLocalChecked();
  EXPECT_EQ(
      "foo.js.map",
      from_v8_string(
          isolate(),
          script->GetUnboundScript()->GetSourceMappingURL().As<String>()));
}

TEST_F(ScriptTest, OriginSourceMapOverridesSourceMappingUrlComment) {
  const char* url = "http://www.foo.com/foo.js";
  const char* api_source_map = "http://override/foo.js.map";
  v8::ScriptOrigin origin(NewString(url), 13, 0, false, -1,
                          NewString(api_source_map));
  v8::ScriptCompiler::Source script_source(
      NewString("var foo;\n//# sourceMappingURL=foo.js.map"), origin);

  Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &script_source)
          .ToLocalChecked();
  EXPECT_EQ(
      api_source_map,
      from_v8_string(
          isolate(),
          script->GetUnboundScript()->GetSourceMappingURL().As<String>()));
}

TEST_F(ScriptTest, IgnoreOriginSourceMapEmptyString) {
  const char* url = "http://www.foo.com/foo.js";
  const char* api_source_map = "";
  v8::ScriptOrigin origin(NewString(url), 13, 0, false, -1,
                          NewString(api_source_map));
  v8::ScriptCompiler::Source script_source(
      NewString("var foo;\n//# sourceMappingURL=foo.js.map"), origin);

  Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &script_source)
          .ToLocalChecked();
  EXPECT_EQ(
      "foo.js.map",
      from_v8_string(
          isolate(),
          script->GetUnboundScript()->GetSourceMappingURL().As<String>()));
}

TEST_F(ScriptTest, GetSingleStalledTopLevelAwaitMessage) {
  TestGetStalledTopLevelAwaitMessage("import 'stall.mjs';", {"stall.mjs"});
}

TEST_F(ScriptTest, GetMultipleStalledTopLevelAwaitMessage) {
  TestGetStalledTopLevelAwaitMessage(
      "import 'stall.mjs';\n"
      "import 'stall_2.mjs';\n"
      "import 'stall_3.mjs';\n"
      "import 'stall_4.mjs';\n",
      {"stall.mjs", "stall_2.mjs", "stall_3.mjs", "stall_4.mjs"});
}

TEST_F(ScriptTest, GetMixedStalledTopLevelAwaitMessage) {
  TestGetStalledTopLevelAwaitMessage(
      "import 'stall.mjs';\n"
      "import 'resolve.mjs';\n"
      "import 'stall_2.mjs';\n"
      "import 'resolve.mjs';\n",
      {"stall.mjs", "stall_2.mjs"});
}

TEST_F(ScriptTest, GetEmptyStalledTopLevelAwaitMessage) {
  TestGetStalledTopLevelAwaitMessage(
      "import 'resolve.mjs';\n"
      "import 'resolve_2.mjs';\n"
      "import 'resolve_3.mjs';\n",
      {});
}

TEST_F(ScriptTest, ProduceCompileHints) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);

  const char* code = "function lazy1() {} function lazy2() {} lazy1();";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);

  // Test producing compile hints.
  {
    Local<Script> script =
        v8::ScriptCompiler::Compile(
            v8_context(), &script_source,
            v8::ScriptCompiler::CompileOptions::kProduceCompileHints)
            .ToLocalChecked();
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(0u, compile_hints.size());
    }

    v8::Local<v8::Context> context = v8::Context::New(isolate());
    v8::MaybeLocal<v8::Value> result = script->Run(context);
    EXPECT_FALSE(result.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(1u, compile_hints.size());
      EXPECT_EQ(14, compile_hints[0]);
    }

    // The previous data is still there if we retrieve compile hints again.
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(1u, compile_hints.size());
      EXPECT_EQ(14, compile_hints[0]);
    }

    // Call the other lazy function and retrieve compile hints again.
    const char* code2 = "lazy2();";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);
    EXPECT_FALSE(result2.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(2u, compile_hints.size());
      EXPECT_EQ(14, compile_hints[0]);
      EXPECT_EQ(34, compile_hints[1]);
    }
  }

  // Test that compile hints are not produced unless the relevant compile option
  // is set.
  {
    const char* nohints_code =
        "function nohints_lazy1() {} function nohints_lazy2() {} "
        "nohints_lazy1();";
    v8::ScriptCompiler::Source nohints_script_source(NewString(nohints_code),
                                                     origin);

    Local<Script> script =
        v8::ScriptCompiler::Compile(v8_context(), &nohints_script_source)
            .ToLocalChecked();
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(0u, compile_hints.size());
    }

    v8::Local<v8::Context> context = v8::Context::New(isolate());
    v8::MaybeLocal<v8::Value> result = script->Run(context);
    EXPECT_FALSE(result.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(0u, compile_hints.size());
    }
  }
}

TEST_F(ScriptTest, ProduceCompileHintsForArrowFunctions) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);

  const char* code = "lazy1 = () => {}; (() => { lazy2 = () => {} })()";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);

  // Test producing compile hints.
  {
    Local<Script> script =
        v8::ScriptCompiler::Compile(
            v8_context(), &script_source,
            v8::ScriptCompiler::CompileOptions::kProduceCompileHints)
            .ToLocalChecked();
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(0u, compile_hints.size());
    }

    v8::Local<v8::Context> context = v8::Context::New(isolate());
    v8::MaybeLocal<v8::Value> result = script->Run(context);
    EXPECT_FALSE(result.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(0u, compile_hints.size());
    }

    // Call one of the lazy functions and retrieve compile hints again.
    const char* code2 = "lazy1();";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);
    EXPECT_FALSE(result2.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(1u, compile_hints.size());
      EXPECT_EQ(8, compile_hints[0]);
    }

    // Call the other lazy function and retrieve the compile hints again.
    const char* code3 = "lazy2();";
    v8::ScriptCompiler::Source script_source3(NewString(code3), origin);

    Local<Script> script3 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source3)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result3 = script3->Run(context);
    EXPECT_FALSE(result3.IsEmpty());
    {
      auto compile_hints =
          script->GetCompileHintsCollector()->GetCompileHints(v8_isolate());
      EXPECT_EQ(2u, compile_hints.size());
      EXPECT_EQ(8, compile_hints[0]);
      EXPECT_EQ(35, compile_hints[1]);
    }
  }
}

namespace {
bool CompileHintsCallback(int position, void* data) {
  std::vector<int>* hints = reinterpret_cast<std::vector<int>*>(data);
  return std::find(hints->begin(), hints->end(), position) != hints->end();
}
}  // namespace

TEST_F(CompileHintsTest, ConsumeCompileHints) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Produce compile hints which we'll use as data later. The function positions
  // must match the script we're compiling later, but we'll change the script
  // source code to make sure that 1) the compile result is not coming from a
  // cache 2) we're querying the correct functions.
  std::vector<int> compile_hints = ProduceCompileHintsHelper(
      {"function lazy1() {} function lazy2() {}", "lazy1()"});

  {
    const char* code = "function func1() {} function func2() {}";
    v8::ScriptCompiler::Source script_source(
        NewString(code), origin, CompileHintsCallback,
        reinterpret_cast<void*>(&compile_hints));
    Local<Script> script =
        v8::ScriptCompiler::Compile(
            v8_context(), &script_source,
            v8::ScriptCompiler::CompileOptions::kConsumeCompileHints)
            .ToLocalChecked();

    v8::MaybeLocal<v8::Value> result = script->Run(context);
    EXPECT_FALSE(result.IsEmpty());
  }

  EXPECT_TRUE(FunctionIsCompiled("func1"));
  EXPECT_FALSE(FunctionIsCompiled("func2"));
}

TEST_F(CompileHintsTest, ConsumeCompileHintsForArrowFunctions) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Produce compile hints which we'll use as data later. The function positions
  // must match the script we're compiling later, but we'll change the script
  // source code to make sure that 1) the compile result is not coming from a
  // cache 2) we're querying the correct functions.
  std::vector<int> compile_hints = ProduceCompileHintsHelper(
      {"lazy1 = (a, b, c) => {}; lazy2 = () => {}", "lazy1()"});

  {
    const char* code = "func1 = (a, b, c) => {}; func2 = () => {}";
    v8::ScriptCompiler::Source script_source(
        NewString(code), origin, CompileHintsCallback,
        reinterpret_cast<void*>(&compile_hints));
    Local<Script> script =
        v8::ScriptCompiler::Compile(
            v8_context(), &script_source,
            v8::ScriptCompiler::CompileOptions::kConsumeCompileHints)
            .ToLocalChecked();

    v8::MaybeLocal<v8::Value> result = script->Run(context);
    EXPECT_FALSE(result.IsEmpty());
  }

  EXPECT_TRUE(FunctionIsCompiled("func1"));
  EXPECT_FALSE(FunctionIsCompiled("func2"));
}

TEST_F(CompileHintsTest, StreamingCompileHints) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);

  // Produce compile hints which we'll use as data later. The function positions
  // must match the script we're compiling later, but we'll change the script
  // source code to make sure that 1) the compile result is not coming from a
  // cache 2) we're querying the correct functions.
  std::vector<int> compile_hints = ProduceCompileHintsHelper(
      {"function lazy1() {} function lazy2() {}", "lazy1()"});

  // Consume compile hints.
  const char* chunks[] = {
      "function func1() {} function fu"
      "nc2() {}",
      nullptr};

  v8::ScriptCompiler::StreamedSource source(
      std::make_unique<i::TestSourceStream>(chunks),
      v8::ScriptCompiler::StreamedSource::ONE_BYTE);
  std::unique_ptr<v8::ScriptCompiler::ScriptStreamingTask> task(
      v8::ScriptCompiler::StartStreaming(isolate(), &source,
                                         v8::ScriptType::kClassic,
                                         ScriptCompiler::kConsumeCompileHints,
                                         CompileHintsCallback, &compile_hints));

  // TestSourceStream::GetMoreData won't block, so it's OK to just join the
  // background task.
  StreamerThread::StartThreadForTaskAndJoin(task.get());
  task.reset();

  std::unique_ptr<char[]> full_source(
      i::TestSourceStream::FullSourceString(chunks));

  v8::Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &source,
                                  NewString(full_source.get()), origin)
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(v8_context());
  EXPECT_FALSE(result.IsEmpty());

  EXPECT_TRUE(FunctionIsCompiled("func1"));
  EXPECT_FALSE(FunctionIsCompiled("func2"));
}

TEST_F(ScriptTest, CompileHintsMagicCommentBasic) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Run the top level code.
  const char* code =
      "//# eagerCompilation=all\n"
      "function f1() {}\n"
      "let f2 = function() { }";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);
  Local<Script> script =
      v8::ScriptCompiler::Compile(
          v8_context(), &script_source,
          v8::ScriptCompiler::CompileOptions(
              v8::ScriptCompiler::CompileOptions::kProduceCompileHints |
              v8::ScriptCompiler::CompileOptions::
                  kFollowCompileHintsMagicComment))
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(context);
  EXPECT_FALSE(result.IsEmpty());

  // Retrieve the function object for f1.
  {
    const char* code2 = "f1";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f1 was not compiled lazily.
    EXPECT_NE(i::Builtin::kCompileLazy, builtin);
  }

  // Retrieve the function object for f2.
  {
    const char* code2 = "f2";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f2 was not compiled lazily.
    EXPECT_NE(i::Builtin::kCompileLazy, builtin);
  }
}

TEST_F(ScriptTest, CompileHintsMagicCommentBetweenFunctions) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Run the top level code.
  const char* code =
      "function f1() {}\n"
      "//# eagerCompilation=all\n"
      "function f2() {}";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);
  Local<Script> script =
      v8::ScriptCompiler::Compile(
          v8_context(), &script_source,
          v8::ScriptCompiler::CompileOptions(
              v8::ScriptCompiler::CompileOptions::kProduceCompileHints |
              v8::ScriptCompiler::CompileOptions::
                  kFollowCompileHintsMagicComment))
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(context);
  EXPECT_FALSE(result.IsEmpty());

  // Retrieve the function object for f1.
  {
    const char* code2 = "f1";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f1 was compiled lazily.
    EXPECT_EQ(i::Builtin::kCompileLazy, builtin);
  }

  // Retrieve the function object for f2.
  {
    const char* code2 = "f2";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));

    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f2 was not compiled lazily.
    EXPECT_NE(i::Builtin::kCompileLazy, builtin);
  }
}

TEST_F(ScriptTest, CompileHintsMagicCommentInvalid) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Run the top level code.
  const char* code =
      "//# eagerCompilation=notAll\n"  // Not a valid compile hint.
      "function f1() {}";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);
  Local<Script> script =
      v8::ScriptCompiler::Compile(
          v8_context(), &script_source,
          v8::ScriptCompiler::CompileOptions(
              v8::ScriptCompiler::CompileOptions::kProduceCompileHints |
              v8::ScriptCompiler::CompileOptions::
                  kFollowCompileHintsMagicComment))
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(context);
  EXPECT_FALSE(result.IsEmpty());

  // Retrieve the function object for f1.
  {
    const char* code2 = "f1";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f1 was compiled lazily.
    EXPECT_EQ(i::Builtin::kCompileLazy, builtin);
  }
}

// Regression test for https://issues.chromium.org/issues/351876778 .
TEST_F(ScriptTest, CompileHintsMagicCommentInvalid2) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  const char* code =
      "//# eagerCompilation=\xCF\x80\n"  // Two byte character
      "function f1() {}";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);

  Local<Script> script =
      v8::ScriptCompiler::Compile(
          v8_context(), &script_source,
          v8::ScriptCompiler::CompileOptions(
              v8::ScriptCompiler::CompileOptions::kProduceCompileHints |
              v8::ScriptCompiler::CompileOptions::
                  kFollowCompileHintsMagicComment))
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(context);
  EXPECT_FALSE(result.IsEmpty());
}

TEST_F(ScriptTest, CompileHintsMagicCommentNotEnabledByCompileOptions) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);
  v8::Local<v8::Context> context = v8::Context::New(isolate());

  // Run the top level code.
  const char* code =
      "//# eagerCompilation=all\n"
      "function f1() {}";
  v8::ScriptCompiler::Source script_source(NewString(code), origin);
  Local<Script> script =
      v8::ScriptCompiler::Compile(
          v8_context(), &script_source,
          // Not enabling the magic comment with compile options!
          v8::ScriptCompiler::CompileOptions::kProduceCompileHints)
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(context);
  EXPECT_FALSE(result.IsEmpty());

  // Retrieve the function object for f1.
  {
    const char* code2 = "f1";
    v8::ScriptCompiler::Source script_source2(NewString(code2), origin);

    Local<Script> script2 =
        v8::ScriptCompiler::Compile(v8_context(), &script_source2)
            .ToLocalChecked();
    v8::MaybeLocal<v8::Value> result2 = script2->Run(context);

    auto function =
        i::Cast<i::JSFunction>(Utils::OpenHandle(*result2.ToLocalChecked()));
    i::Builtin builtin = function->code(i_isolate())->builtin_id();

    // f1 was compiled lazily.
    EXPECT_EQ(i::Builtin::kCompileLazy, builtin);
  }
}

TEST_F(CompileHintsTest, StreamingCompileHintsMagic) {
  const char* url = "http://www.foo.com/foo.js";
  v8::ScriptOrigin origin(NewString(url), 13, 0);

  // Consume compile hints.
  const char* chunks[] = {
      "//# eagerCompilati"
      "on=all\n"
      "function func1() {} function fu"
      "nc2() {}",
      nullptr};

  v8::ScriptCompiler::StreamedSource source(
      std::make_unique<i::TestSourceStream>(chunks),
      v8::ScriptCompiler::StreamedSource::ONE_BYTE);
  std::unique_ptr<v8::ScriptCompiler::ScriptStreamingTask> task(
      v8::ScriptCompiler::StartStreaming(
          isolate(), &source, v8::ScriptType::kClassic,
          v8::ScriptCompiler::CompileOptions(
              v8::ScriptCompiler::kProduceCompileHints |
              v8::ScriptCompiler::kFollowCompileHintsMagicComment)));

  // TestSourceStream::GetMoreData won't block, so it's OK to just join the
  // background task.
  StreamerThread::StartThreadForTaskAndJoin(task.get());
  task.reset();

  std::unique_ptr<char[]> full_source(
      i::TestSourceStream::FullSourceString(chunks));

  v8::Local<Script> script =
      v8::ScriptCompiler::Compile(v8_context(), &source,
                                  NewString(full_source.get()), origin)
          .ToLocalChecked();

  v8::MaybeLocal<v8::Value> result = script->Run(v8_context());
  EXPECT_FALSE(result.IsEmpty());

  EXPECT_TRUE(FunctionIsCompiled("func1"));
  EXPECT_TRUE(FunctionIsCompiled("func2"));
}

}  // namespace
}  // namespace v8
