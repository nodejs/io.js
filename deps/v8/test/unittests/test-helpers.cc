// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/test-helpers.h"

#include "src/api/api.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/scanner.h"

namespace v8 {
namespace internal {
namespace test {

Handle<String> CreateSource(Isolate* isolate,
                            ExternalOneByteString::Resource* maybe_resource) {
  if (!maybe_resource) {
    static const char test_script[] = "(x) { x*x; }";
    maybe_resource = new test::ScriptResource(test_script, strlen(test_script));
  }
  return isolate->factory()
      ->NewExternalStringFromOneByte(maybe_resource)
      .ToHandleChecked();
}

Handle<SharedFunctionInfo> CreateSharedFunctionInfo(
    Isolate* isolate,
    v8::String::ExternalOneByteStringResource* maybe_resource) {
  HandleScope scope(isolate);
  Handle<String> source = CreateSource(isolate, maybe_resource);
  Handle<Script> script = isolate->factory()->NewScript(source);
  Handle<WeakFixedArray> infos = isolate->factory()->NewWeakFixedArray(3);
  script->set_shared_function_infos(*infos);
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->NewStringFromAsciiChecked("f"),
          Builtin::kCompileLazy);
  int function_literal_id = 1;
  shared->set_function_literal_id(function_literal_id);
  // Ensure that the function can be compiled lazily.
  shared->set_uncompiled_data(
      *isolate->factory()->NewUncompiledDataWithoutPreparseDataWithJob(
          ReadOnlyRoots(isolate).empty_string_handle(), 0, source->length()));
  // Make sure we have an outer scope info, even though it's empty
  shared->set_raw_outer_scope_info_or_feedback_metadata(
      ScopeInfo::Empty(isolate));
  shared->SetScript(ReadOnlyRoots(isolate), *script, function_literal_id);
  return scope.CloseAndEscape(shared);
}

std::unique_ptr<Utf16CharacterStream> SourceCharacterStreamForShared(
    Isolate* isolate, Handle<SharedFunctionInfo> shared) {
  // Create a character stream to simulate the parser having done so for the
  // top-level ParseProgram.
  Tagged<Script> script = Script::cast(shared->script());
  Handle<String> source(String::cast(script->source()), isolate);
  std::unique_ptr<Utf16CharacterStream> stream(
      ScannerStream::For(isolate, source));
  return stream;
}

}  // namespace test
}  // namespace internal
}  // namespace v8
