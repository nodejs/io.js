// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SCRIPT_DETAILS_H_
#define V8_CODEGEN_SCRIPT_DETAILS_H_

#include "include/v8-script.h"
#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

struct ScriptDetails {
  ScriptDetails()
      : line_offset(0), column_offset(0), repl_mode(REPLMode::kNo) {}
  explicit ScriptDetails(
      Handle<Object> script_name,
      ScriptOriginOptions origin_options = v8::ScriptOriginOptions())
      : line_offset(0),
        column_offset(0),
        name_obj(script_name),
        repl_mode(REPLMode::kNo),
        origin_options(origin_options) {}

  int line_offset;
  int column_offset;
  MaybeHandle<Object> name_obj;
  MaybeHandle<Object> source_map_url;
  MaybeHandle<Object> host_defined_options;
  MaybeHandle<FixedArray> wrapped_arguments;
  REPLMode repl_mode;
  const ScriptOriginOptions origin_options;
};

void SetScriptFieldsFromDetails(Isolate* isolate, Tagged<Script> script,
                                const ScriptDetails& script_details,
                                DisallowGarbageCollection* no_gc);
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_SCRIPT_DETAILS_H_
