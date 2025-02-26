// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <cctype>
#include <list>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/objects/string.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "test/fuzzer/fuzzer-support.h"

bool IsValidInput(const uint8_t* data, size_t size) {
  // Ignore too long inputs as they tend to find OOM or timeouts, not real bugs.
  if (size > 2048) return false;

  std::vector<char> parentheses;
  const char* ptr = reinterpret_cast<const char*>(data);

  for (size_t i = 0; i != size; ++i) {
    // Check that all characters in the data are valid.
    if (!std::isspace(ptr[i]) && !std::isprint(ptr[i])) return false;

    // Check balance of parentheses in the data.
    switch (ptr[i]) {
      case '(':
      case '[':
      case '{':
        parentheses.push_back(ptr[i]);
        break;
      case ')':
        if (parentheses.empty() || parentheses.back() != '(') return false;
        parentheses.pop_back();
        break;
      case ']':
        if (parentheses.empty() || parentheses.back() != '[') return false;
        parentheses.pop_back();
        break;
      case '}':
        if (parentheses.empty() || parentheses.back() != '{') return false;
        parentheses.pop_back();
        break;
      default:
        break;
    }
  }

  return parentheses.empty();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (!IsValidInput(data, size)) {
    return 0;
  }

  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());
  v8::TryCatch try_catch(isolate);

  v8::internal::Isolate* i_isolate =
      reinterpret_cast<v8::internal::Isolate*>(isolate);
  v8::internal::Factory* factory = i_isolate->factory();

  if (size > INT_MAX) return 0;
  v8::internal::MaybeDirectHandle<v8::internal::String> source =
      factory->NewStringFromOneByte(v8::base::VectorOf(data, size));
  if (source.is_null()) return 0;

  v8::internal::Handle<v8::internal::Script> script =
      factory->NewScript(source.ToHandleChecked());
  v8::internal::UnoptimizedCompileState state;
  v8::internal::ReusableUnoptimizedCompileState reusable_state(i_isolate);
  v8::internal::UnoptimizedCompileFlags flags =
      v8::internal::UnoptimizedCompileFlags::ForScriptCompile(i_isolate,
                                                              *script);
  v8::internal::ParseInfo info(i_isolate, flags, &state, &reusable_state);
  if (!v8::internal::parsing::ParseProgram(
          &info, script, i_isolate, i::parsing::ReportStatisticsMode::kYes)) {
    info.pending_error_handler()->PrepareErrors(i_isolate,
                                                info.ast_value_factory());
    info.pending_error_handler()->ReportErrors(i_isolate, script);
  }
  isolate->RequestGarbageCollectionForTesting(
      v8::Isolate::kFullGarbageCollection);
  return 0;
}
