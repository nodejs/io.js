// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/json/json-parser.h"
#include "src/json/json-stringifier.h"
#include "src/logging/counters.h"
#include "src/objects/js-raw-json.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// ES6 section 24.3.1 JSON.parse.
BUILTIN(JsonParse) {
  HandleScope scope(isolate);
  Handle<Object> source = args.atOrUndefined(isolate, 1);
  Handle<Object> reviver = args.atOrUndefined(isolate, 2);
  Handle<String> string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, string,
                                     Object::ToString(isolate, source));
  string = String::Flatten(isolate, string);
  RETURN_RESULT_OR_FAILURE(
      isolate, String::IsOneByteRepresentationUnderneath(*string)
                   ? JsonParser<uint8_t>::Parse(isolate, string, reviver)
                   : JsonParser<uint16_t>::Parse(isolate, string, reviver));
}

// ES6 section 24.3.2 JSON.stringify.
BUILTIN(JsonStringify) {
  HandleScope scope(isolate);
  Handle<Object> object = args.atOrUndefined(isolate, 1);
  Handle<Object> replacer = args.atOrUndefined(isolate, 2);
  Handle<Object> indent = args.atOrUndefined(isolate, 3);
  RETURN_RESULT_OR_FAILURE(isolate,
                           JsonStringify(isolate, object, replacer, indent));
}

// https://tc39.es/proposal-json-parse-with-source/#sec-json.rawjson
BUILTIN(JsonRawJson) {
  HandleScope scope(isolate);
  Handle<Object> text = args.atOrUndefined(isolate, 1);
  RETURN_RESULT_OR_FAILURE(isolate, JSRawJson::Create(isolate, text));
}

// https://tc39.es/proposal-json-parse-with-source/#sec-json.israwjson
BUILTIN(JsonIsRawJson) {
  HandleScope scope(isolate);
  DirectHandle<Object> text = args.atOrUndefined(isolate, 1);
  return isolate->heap()->ToBoolean(IsJSRawJson(*text));
}

}  // namespace internal
}  // namespace v8
