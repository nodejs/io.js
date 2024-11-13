// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test specific cases of the lazy/eager-parse decision.
//
// Note that presently most unit tests for parsing are found in
// parsing-unittest.cc.

#include <unordered_map>

#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "src/api/api-inl.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/utils/utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class ParseDecisionTest : public TestWithContext {
 public:
  Local<v8::Script> Compile(const char* source) {
    return v8::Script::Compile(
               context(),
               v8::String::NewFromUtf8(isolate(), source).ToLocalChecked())
        .ToLocalChecked();
  }
};

namespace {

// Record the 'compiled' state of all top level functions.
void GetTopLevelFunctionInfo(
    v8::Local<v8::Script> script,
    std::unordered_map<std::string, bool>* is_compiled) {
  // Get the v8::internal::Script object from the API v8::Script.
  // The API object 'wraps' the compiled top-level function, not the i::Script.
  DirectHandle<JSFunction> toplevel_fn = v8::Utils::OpenDirectHandle(*script);
  SharedFunctionInfo::ScriptIterator iterator(
      toplevel_fn->GetIsolate(), Cast<Script>(toplevel_fn->shared()->script()));

  for (Tagged<SharedFunctionInfo> shared = iterator.Next(); !shared.is_null();
       shared = iterator.Next()) {
    std::unique_ptr<char[]> name = Cast<String>(shared->Name())->ToCString();
    is_compiled->insert(std::make_pair(name.get(), shared->is_compiled()));
  }
}

}  // anonymous namespace

TEST_F(ParseDecisionTest, GetTopLevelFunctionInfo) {
  if (!v8_flags.lazy) return;

  HandleScope scope(i_isolate());

  const char src[] = "function foo() { var a; }\n";
  std::unordered_map<std::string, bool> is_compiled;
  GetTopLevelFunctionInfo(Compile(src), &is_compiled);

  // Test that our helper function GetTopLevelFunctionInfo does what it claims:
  DCHECK(is_compiled.find("foo") != is_compiled.end());
  DCHECK(is_compiled.find("bar") == is_compiled.end());
}

TEST_F(ParseDecisionTest, EagerlyCompileImmediateUseFunctions) {
  if (!v8_flags.lazy) return;

  HandleScope scope(i_isolate());

  // Test parenthesized, exclaimed, and regular functions. Make sure these
  // occur both intermixed and after each other, to make sure the 'reset'
  // mechanism works.
  const char src[] =
      "function normal() { var a; }\n"             // Normal: Should lazy parse.
      "(function parenthesized() { var b; })()\n"  // Parenthesized: Pre-parse.
      "!function exclaimed() { var c; }() \n"      // Exclaimed: Pre-parse.
      "function normal2() { var d; }\n"
      "(function parenthesized2() { var e; })()\n"
      "function normal3() { var f; }\n"
      "!function exclaimed2() { var g; }() \n"
      "function normal4() { var h; }\n";

  std::unordered_map<std::string, bool> is_compiled;
  GetTopLevelFunctionInfo(Compile(src), &is_compiled);

  DCHECK(is_compiled["parenthesized"]);
  DCHECK(is_compiled["parenthesized2"]);
  DCHECK(is_compiled["exclaimed"]);
  DCHECK(is_compiled["exclaimed2"]);
  DCHECK(!is_compiled["normal"]);
  DCHECK(!is_compiled["normal2"]);
  DCHECK(!is_compiled["normal3"]);
  DCHECK(!is_compiled["normal4"]);
}

TEST_F(ParseDecisionTest, CommaFunctionSequence) {
  if (!v8_flags.lazy) return;

  HandleScope scope(i_isolate());

  const char src[] = "!function a(){}(),function b(){}(),function c(){}();";
  std::unordered_map<std::string, bool> is_compiled;
  GetTopLevelFunctionInfo(Compile(src), &is_compiled);

  DCHECK(is_compiled["a"]);
  DCHECK(is_compiled["b"]);
  DCHECK(is_compiled["c"]);
}

}  // namespace internal
}  // namespace v8
