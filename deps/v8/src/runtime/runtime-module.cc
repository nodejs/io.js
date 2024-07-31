// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/objects/js-promise.h"
#include "src/objects/source-text-module.h"

namespace v8 {
namespace internal {

namespace {
Handle<Script> GetEvalOrigin(Isolate* isolate, Tagged<Script> origin_script) {
  DisallowGarbageCollection no_gc;
  while (origin_script->has_eval_from_shared()) {
    Tagged<HeapObject> maybe_script =
        origin_script->eval_from_shared()->script();
    CHECK(IsScript(maybe_script));
    origin_script = Script::cast(maybe_script);
  }
  return handle(origin_script, isolate);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_DynamicImportCall) {
  HandleScope scope(isolate);
  DCHECK_LE(2, args.length());
  DCHECK_GE(3, args.length());
  Handle<JSFunction> function = args.at<JSFunction>(0);
  Handle<Object> specifier = args.at(1);

  MaybeHandle<Object> import_options;
  if (args.length() == 3) {
    import_options = args.at<Object>(2);
  }

  Handle<Script> referrer_script =
      GetEvalOrigin(isolate, Script::cast(function->shared()->script()));
  RETURN_RESULT_OR_FAILURE(isolate,
                           isolate->RunHostImportModuleDynamicallyCallback(
                               referrer_script, specifier, import_options));
}

RUNTIME_FUNCTION(Runtime_GetModuleNamespace) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int module_request = args.smi_value_at(0);
  Handle<SourceTextModule> module(isolate->context()->module(), isolate);
  return *SourceTextModule::GetModuleNamespace(isolate, module, module_request);
}

RUNTIME_FUNCTION(Runtime_GetImportMetaObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  Handle<SourceTextModule> module(isolate->context()->module(), isolate);
  RETURN_RESULT_OR_FAILURE(isolate,
                           SourceTextModule::GetImportMeta(isolate, module));
}

RUNTIME_FUNCTION(Runtime_GetModuleNamespaceExport) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<JSModuleNamespace> module_namespace = args.at<JSModuleNamespace>(0);
  Handle<String> name = args.at<String>(1);
  if (!module_namespace->HasExport(isolate, name)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewReferenceError(MessageTemplate::kNotDefined, name));
  }
  RETURN_RESULT_OR_FAILURE(isolate, module_namespace->GetExport(isolate, name));
}

}  // namespace internal
}  // namespace v8
