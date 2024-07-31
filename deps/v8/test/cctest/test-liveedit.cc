// Copyright 2007-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/debug/liveedit.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace {
void CompareStringsOneWay(const char* s1, const char* s2,
                          int expected_diff_parameter,
                          std::vector<SourceChangeRange>* changes) {
  i::Isolate* isolate = CcTest::i_isolate();
  i::Handle<i::String> i_s1 = isolate->factory()->NewStringFromAsciiChecked(s1);
  i::Handle<i::String> i_s2 = isolate->factory()->NewStringFromAsciiChecked(s2);
  changes->clear();
  LiveEdit::CompareStrings(isolate, i_s1, i_s2, changes);

  int len1 = static_cast<int>(strlen(s1));
  int len2 = static_cast<int>(strlen(s2));

  int pos1 = 0;
  int pos2 = 0;

  int diff_parameter = 0;
  for (const auto& diff : *changes) {
    int diff_pos1 = diff.start_position;
    int similar_part_length = diff_pos1 - pos1;
    int diff_pos2 = pos2 + similar_part_length;

    CHECK_EQ(diff_pos2, diff.new_start_position);

    for (int j = 0; j < similar_part_length; j++) {
      CHECK(pos1 + j < len1);
      CHECK(pos2 + j < len2);
      CHECK_EQ(s1[pos1 + j], s2[pos2 + j]);
    }
    int diff_len1 = diff.end_position - diff.start_position;
    int diff_len2 = diff.new_end_position - diff.new_start_position;
    diff_parameter += diff_len1 + diff_len2;
    pos1 = diff_pos1 + diff_len1;
    pos2 = diff_pos2 + diff_len2;
  }
  {
    // After last chunk.
    int similar_part_length = len1 - pos1;
    CHECK_EQ(similar_part_length, len2 - pos2);
    USE(len2);
    for (int j = 0; j < similar_part_length; j++) {
      CHECK(pos1 + j < len1);
      CHECK(pos2 + j < len2);
      CHECK_EQ(s1[pos1 + j], s2[pos2 + j]);
    }
  }

  if (expected_diff_parameter != -1) {
    CHECK_EQ(expected_diff_parameter, diff_parameter);
  }
}

void CompareStringsOneWay(const char* s1, const char* s2,
                          int expected_diff_parameter = -1) {
  std::vector<SourceChangeRange> changes;
  CompareStringsOneWay(s1, s2, expected_diff_parameter, &changes);
}

void CompareStringsOneWay(const char* s1, const char* s2,
                          std::vector<SourceChangeRange>* changes) {
  CompareStringsOneWay(s1, s2, -1, changes);
}

void CompareStrings(const char* s1, const char* s2,
                    int expected_diff_parameter = -1) {
  CompareStringsOneWay(s1, s2, expected_diff_parameter);
  CompareStringsOneWay(s2, s1, expected_diff_parameter);
}

void CompareOneWayPlayWithLF(const char* s1, const char* s2) {
  std::string s1_one_line(s1);
  std::replace(s1_one_line.begin(), s1_one_line.end(), '\n', ' ');
  std::string s2_one_line(s2);
  std::replace(s2_one_line.begin(), s2_one_line.end(), '\n', ' ');
  CompareStringsOneWay(s1, s2, -1);
  CompareStringsOneWay(s1_one_line.c_str(), s2, -1);
  CompareStringsOneWay(s1, s2_one_line.c_str(), -1);
  CompareStringsOneWay(s1_one_line.c_str(), s2_one_line.c_str(), -1);
}

void CompareStringsPlayWithLF(const char* s1, const char* s2) {
  CompareOneWayPlayWithLF(s1, s2);
  CompareOneWayPlayWithLF(s2, s1);
}
}  // anonymous namespace

TEST(LiveEditDiffer) {
  v8::HandleScope handle_scope(CcTest::isolate());
  CompareStrings("zz1zzz12zz123zzz", "zzzzzzzzzz", 6);
  CompareStrings("zz1zzz12zz123zzz", "zz0zzz0zz0zzz", 9);
  CompareStrings("123456789", "987654321", 16);
  CompareStrings("zzz", "yyy", 6);
  CompareStrings("zzz", "zzz12", 2);
  CompareStrings("zzz", "21zzz", 2);
  CompareStrings("cat", "cut", 2);
  CompareStrings("ct", "cut", 1);
  CompareStrings("cat", "ct", 1);
  CompareStrings("cat", "cat", 0);
  CompareStrings("", "", 0);
  CompareStrings("cat", "", 3);
  CompareStrings("a cat", "a capybara", 7);
  CompareStrings("abbabababababaaabbabababababbabbbbbbbababa",
                 "bbbbabababbbabababbbabababababbabbababa");
  CompareStringsPlayWithLF("", "");
  CompareStringsPlayWithLF("a", "b");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseem\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "\nall\nmy\ntroubles\nseemed\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "all\nmy\ntroubles\nseemed\nso\nfar\naway");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway\n");
  CompareStringsPlayWithLF(
      "yesterday\nall\nmy\ntroubles\nseemed\nso\nfar\naway",
      "yesterday\nall\nmy\ntroubles\nseemed\nso\n");
}

TEST(LiveEditTranslatePosition) {
  v8::HandleScope handle_scope(CcTest::isolate());
  std::vector<SourceChangeRange> changes;
  CompareStringsOneWay("a", "a", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CompareStringsOneWay("a", "b", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CompareStringsOneWay("ababa", "aaa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 3);
  CompareStringsOneWay("ababa", "acaca", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 3);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 4);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 5);
  CompareStringsOneWay("aaa", "ababa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 4);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 5);
  CompareStringsOneWay("aabbaaaa", "aaaabbaa", &changes);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 0), 0);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 1), 1);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 2), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 3), 3);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 4), 2);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 5), 3);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 6), 6);
  CHECK_EQ(LiveEdit::TranslatePosition(changes, 8), 8);
}

namespace {
void PatchFunctions(v8::Local<v8::Context> context, const char* source_a,
                    const char* source_b,
                    v8::debug::LiveEditResult* result = nullptr) {
  v8::Isolate* isolate = context->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::EscapableHandleScope scope(isolate);
  v8::Local<v8::Script> script_a =
      v8::Script::Compile(context, v8_str(isolate, source_a)).ToLocalChecked();
  script_a->Run(context).ToLocalChecked();
  i::Handle<i::Script> i_script_a(
      i::Script::cast(
          v8::Utils::OpenDirectHandle(*script_a)->shared()->script()),
      i_isolate);

  if (result) {
    LiveEdit::PatchScript(
        i_isolate, i_script_a,
        i_isolate->factory()->NewStringFromAsciiChecked(source_b), false, false,
        result);
    if (result->status == v8::debug::LiveEditResult::COMPILE_ERROR) {
      result->message = scope.Escape(result->message);
    }
  } else {
    v8::debug::LiveEditResult r;
    LiveEdit::PatchScript(
        i_isolate, i_script_a,
        i_isolate->factory()->NewStringFromAsciiChecked(source_b), false, false,
        &r);
    CHECK_EQ(r.status, v8::debug::LiveEditResult::OK);
  }
}
}  // anonymous namespace

TEST(LiveEditPatchFunctions) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  // Check that function is removed from compilation cache.
  i::v8_flags.allow_natives_syntax = true;
  PatchFunctions(context, "42;", "%AbortJS('')");
  PatchFunctions(context, "42;", "239;");
  i::v8_flags.allow_natives_syntax = false;

  // Basic test cases.
  PatchFunctions(context, "42;", "2;");
  PatchFunctions(context, "42;", "  42;");
  PatchFunctions(context, "42;", "42;");
  // Trivial return value change.
  PatchFunctions(context, "function foo() { return 1; }",
                 "function foo() { return 42; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           42);
  // It is expected, we do not reevaluate top level function.
  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var a = 3; function foo() { return a; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           1);
  // Throw exception since var b is not defined in original source.
  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var b = 4; function foo() { return b; }");
  {
    v8::TryCatch try_catch(env->GetIsolate());
    CompileRun("foo()");
    CHECK(try_catch.HasCaught());
  }
  // But user always can add new variable to function and use it.
  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var b = 4; function foo() { var b = 5; return b; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           5);

  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var b = 4; function foo() { var a = 6; return a; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           6);

  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var d = (() => ({a:2}))(); function foo() { return d; }");
  {
    v8::TryCatch try_catch(env->GetIsolate());
    CompileRun("foo()");
    CHECK(try_catch.HasCaught());
  }

  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var b = 1; var a = 2; function foo() { return a; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           1);

  PatchFunctions(context, "var a = 1; function foo() { return a; }",
                 "var b = 1; var a = 2; function foo() { return b; }");
  {
    v8::TryCatch try_catch(env->GetIsolate());
    CompileRun("foo()");
    CHECK(try_catch.HasCaught());
  }

  PatchFunctions(context, "function foo() { var a = 1; return a; }",
                 "function foo() { var b = 1; return b; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           1);

  PatchFunctions(context, "var a = 3; function foo() { var a = 1; return a; }",
                 "function foo() { var b = 1; return a; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           3);

  PatchFunctions(context, "var a = 3; var c = 7; function foo() { return a; }",
                 "var b = 5; var a = 3; function foo() { return b; }");
  {
    v8::TryCatch try_catch(env->GetIsolate());
    CompileRun("foo()");
    CHECK(try_catch.HasCaught());
  }

  // Add argument.
  PatchFunctions(context, "function fooArgs(a1, b1) { return a1 + b1; }",
                 "function fooArgs(a2, b2, c2) { return a2 + b2 + c2; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "fooArgs(1,2,3)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           6);

  PatchFunctions(context, "function fooArgs(a1, b1) { return a1 + b1; }",
                 "function fooArgs(a1, b1, c1) { return a1 + b1 + c1; }");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "fooArgs(1,2,3)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           6);

  i::v8_flags.allow_natives_syntax = true;
  PatchFunctions(context,
                 "function foo(a, b) { return a + b; }; "
                 "%PrepareFunctionForOptimization(foo);"
                 "%OptimizeFunctionOnNextCall(foo); foo(1,2);",
                 "function foo(a, b) { return a * b; };");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo(5,7)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           35);
  i::v8_flags.allow_natives_syntax = false;

  // Check inner function.
  PatchFunctions(
      context,
      "function foo(a,b) { function op(a,b) { return a + b } return op(a,b); }",
      "function foo(a,b) { function op(a,b) { return a * b } return op(a,b); "
      "}");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "foo(8,9)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           72);

  // Update constructor.
  PatchFunctions(context,
                 "class Foo { constructor(a,b) { this.data = a + b; } };",
                 "class Foo { constructor(a,b) { this.data = a * b; } };");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "new Foo(4,5).data")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           20);
  // Change inner functions.
  PatchFunctions(
      context,
      "function f(evt) { function f2() {} f2(),f3(); function f3() {} } "
      "function f4() {}",
      "function f(evt) { function f2() { return 1; } return f2() + f3(); "
      "function f3() { return 2; }  } function f4() {}");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           3);
  // Change usage of outer scope.
  PatchFunctions(context,
                 "function ChooseAnimal(a, b) {\n "
                 "  if (a == 7 && b == 7) {\n"
                 "    return;\n"
                 "  }\n"
                 "  return function Chooser() {\n"
                 "    return 'Cat' + a;\n"
                 "  };\n"
                 "}\n"
                 "var old_closure = ChooseAnimal(2, 3);",
                 "function ChooseAnimal(a, b) {\n "
                 "  if (a == 7 && b == 7) {\n"
                 "    return;\n"
                 "  }\n"
                 "  return function Chooser() {\n"
                 "    return 'Capybara' + b;\n"
                 "  };\n"
                 "}\n");
  CompileRunChecked(env->GetIsolate(), "var new_closure = ChooseAnimal(3, 4);");

  {
    v8::Local<v8::String> call_result =
        CompileRunChecked(env->GetIsolate(), "new_closure()").As<v8::String>();
    v8::String::Utf8Value new_result_utf8(env->GetIsolate(), call_result);
    CHECK_NOT_NULL(strstr(*new_result_utf8, "Capybara4"));
    call_result =
        CompileRunChecked(env->GetIsolate(), "old_closure()").As<v8::String>();
    v8::String::Utf8Value old_result_utf8(env->GetIsolate(), call_result);
    CHECK_NOT_NULL(strstr(*old_result_utf8, "Cat2"));
  }

  // Update const literals.
  PatchFunctions(context, "function foo() { return 'a' + 'b'; }",
                 "function foo() { return 'c' + 'b'; }");
  {
    v8::Local<v8::String> result_str =
        CompileRunChecked(env->GetIsolate(), "foo()").As<v8::String>();
    v8::String::Utf8Value new_result_utf8(env->GetIsolate(), result_str);
    CHECK_NOT_NULL(strstr(*new_result_utf8, "cb"));
  }

  // TODO(kozyatinskiy): should work when we remove (.
  PatchFunctions(context, "f = () => 2", "f = a => a");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f(3)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           2);

  // Replace function with not a function.
  PatchFunctions(context, "f = () => 2", "f = a == 2");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f(3)")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           2);

  // TODO(kozyatinskiy): should work when we put function into (...).
  PatchFunctions(context, "f = a => 2", "f = (a => 5)()");
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           2);

  PatchFunctions(context,
                 "f2 = null;\n"
                 "f = () => {\n"
                 "  f2 = () => 5;\n"
                 "  return f2();\n"
                 "}\n"
                 "f()\n",
                 "f2 = null;\n"
                 "f = () => {\n"
                 "  for (var a = (() => 7)(), b = 0; a < 10; ++a,++b);\n"
                 "  return b;\n"
                 "}\n"
                 "f()\n");
  // TODO(kozyatinskiy): ditto.
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f2()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           5);
  CHECK_EQ(CompileRunChecked(env->GetIsolate(), "f()")
               ->ToInt32(context)
               .ToLocalChecked()
               ->Value(),
           3);
}

TEST(LiveEditCompileError) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  debug::LiveEditResult result;
  PatchFunctions(
      context,
      "var something1 = 25; \n"
      " function ChooseAnimal() { return          'Cat';          } \n"
      " ChooseAnimal.Helper = function() { return 'Help!'; }\n",
      "var something1 = 25; \n"
      " function ChooseAnimal() { return          'Cap' + ) + 'bara';          "
      "} \n"
      " ChooseAnimal.Helper = function() { return 'Help!'; }\n",
      &result);
  CHECK_EQ(result.status, debug::LiveEditResult::COMPILE_ERROR);
  CHECK_EQ(result.line_number, 2);
  CHECK_EQ(result.column_number, 51);
  v8::String::Utf8Value result_message(env->GetIsolate(), result.message);
  CHECK_NOT_NULL(
      strstr(*result_message, "Uncaught SyntaxError: Unexpected token ')'"));

  {
    v8::Local<v8::String> result_str =
        CompileRunChecked(env->GetIsolate(), "ChooseAnimal()").As<v8::String>();
    v8::String::Utf8Value new_result_utf8(env->GetIsolate(), result_str);
    CHECK_NOT_NULL(strstr(*new_result_utf8, "Cat"));
  }

  PatchFunctions(context, "function foo() {}",
                 "function foo() { return a # b; }", &result);
  CHECK_EQ(result.status, debug::LiveEditResult::COMPILE_ERROR);
  CHECK_EQ(result.line_number, 1);
  CHECK_EQ(result.column_number, 26);
}

TEST(LiveEditFunctionExpression) {
  const char* original_source =
      "(function() {\n "
      "  return 'Cat';\n"
      "})\n";
  const char* updated_source =
      "(function() {\n "
      "  return 'Capy' + 'bara';\n"
      "})\n";
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  v8::Isolate* isolate = context->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, v8_str(isolate, original_source))
          .ToLocalChecked();
  v8::Local<v8::Function> f =
      script->Run(context).ToLocalChecked().As<v8::Function>();
  i::Handle<i::Script> i_script(
      i::Script::cast(v8::Utils::OpenDirectHandle(*script)->shared()->script()),
      i_isolate);
  debug::LiveEditResult result;
  LiveEdit::PatchScript(
      i_isolate, i_script,
      i_isolate->factory()->NewStringFromAsciiChecked(updated_source), false,
      false, &result);
  CHECK_EQ(result.status, debug::LiveEditResult::OK);
  {
    v8::Local<v8::String> result_str =
        f->Call(context, context->Global(), 0, nullptr)
            .ToLocalChecked()
            .As<v8::String>();
    v8::String::Utf8Value new_result_utf8(env->GetIsolate(), result_str);
    CHECK_NOT_NULL(strstr(*new_result_utf8, "Capybara"));
  }
}
}  // namespace internal
}  // namespace v8
