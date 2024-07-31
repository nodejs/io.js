// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/strings.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "test/fuzzer/fuzzer-support.h"

namespace i = v8::internal;

void Test(v8::Isolate* isolate, i::Handle<i::JSRegExp> regexp,
          i::Handle<i::String> subject,
          i::Handle<i::RegExpMatchInfo> results_array) {
  v8::TryCatch try_catch(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  // Exceptions will be swallowed by the try/catch above.
  USE(i::RegExp::Exec(i_isolate, regexp, subject, 0, results_array));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());
  v8::TryCatch try_catch(isolate);

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::Factory* factory = i_isolate->factory();

  CHECK(!i_isolate->has_exception());
  if (size > INT_MAX) return 0;
  i::MaybeHandle<i::String> maybe_source = factory->NewStringFromOneByte(
      v8::base::Vector<const uint8_t>(data, static_cast<int>(size)));
  i::Handle<i::String> source;
  if (!maybe_source.ToHandle(&source)) {
    i_isolate->clear_exception();
    return 0;
  }

  static const int kAllFlags = i::JSRegExp::kGlobal | i::JSRegExp::kIgnoreCase |
                               i::JSRegExp::kMultiline | i::JSRegExp::kSticky |
                               i::JSRegExp::kUnicode | i::JSRegExp::kDotAll;

  const uint8_t one_byte_array[6] = {'f', 'o', 'o', 'b', 'a', 'r'};
  const v8::base::uc16 two_byte_array[6] = {'f', 0xD83D, 0xDCA9,
                                            'b', 'a',    0x2603};

  CHECK(!i_isolate->has_exception());
  i::Handle<i::RegExpMatchInfo> results_array =
      i::RegExpMatchInfo::New(i_isolate, 2);
  i::Handle<i::String> one_byte =
      factory
          ->NewStringFromOneByte(
              v8::base::Vector<const uint8_t>(one_byte_array, 6))
          .ToHandleChecked();
  i::Handle<i::String> two_byte =
      factory
          ->NewStringFromTwoByte(
              v8::base::Vector<const v8::base::uc16>(two_byte_array, 6))
          .ToHandleChecked();

  i::Handle<i::JSRegExp> regexp;
  {
    CHECK(!i_isolate->has_exception());
    v8::TryCatch try_catch_inner(isolate);
    // Create a string so that we can calculate a hash from the input data.
    std::string str = std::string(reinterpret_cast<const char*>(data), size);
    i::JSRegExp::Flags flag = static_cast<i::JSRegExp::Flags>(
        std::hash<std::string>()(str) % (kAllFlags + 1));
    i::MaybeHandle<i::JSRegExp> maybe_regexp =
        i::JSRegExp::New(i_isolate, source, flag);
    if (!maybe_regexp.ToHandle(&regexp)) {
      i_isolate->clear_exception();
      return 0;
    }
  }
  Test(isolate, regexp, one_byte, results_array);
  Test(isolate, regexp, two_byte, results_array);
  Test(isolate, regexp, factory->empty_string(), results_array);
  Test(isolate, regexp, source, results_array);
  isolate->RequestGarbageCollectionForTesting(
      v8::Isolate::kFullGarbageCollection);
  CHECK(!i_isolate->has_exception());
  return 0;
}
