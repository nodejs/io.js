// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/synthetic-module.h"

#include "src/api/api-inl.h"
#include "src/builtins/accessors.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

// Implements SetSyntheticModuleBinding:
// https://heycam.github.io/webidl/#setsyntheticmoduleexport
Maybe<bool> SyntheticModule::SetExport(Isolate* isolate,
                                       Handle<SyntheticModule> module,
                                       Handle<String> export_name,
                                       Handle<Object> export_value) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  Handle<Object> export_object(exports->Lookup(export_name), isolate);

  if (!IsCell(*export_object)) {
    isolate->Throw(*isolate->factory()->NewReferenceError(
        MessageTemplate::kModuleExportUndefined, export_name));
    return Nothing<bool>();
  }

  // Spec step 2: Set the mutable binding of export_name to export_value
  Cell::cast(*export_object)->set_value(*export_value);

  return Just(true);
}

void SyntheticModule::SetExportStrict(Isolate* isolate,
                                      Handle<SyntheticModule> module,
                                      Handle<String> export_name,
                                      Handle<Object> export_value) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  Handle<Object> export_object(exports->Lookup(export_name), isolate);
  CHECK(IsCell(*export_object));
  Maybe<bool> set_export_result =
      SetExport(isolate, module, export_name, export_value);
  CHECK(set_export_result.FromJust());
}

// Implements Synthetic Module Record's ResolveExport concrete method:
// https://heycam.github.io/webidl/#smr-resolveexport
MaybeHandle<Cell> SyntheticModule::ResolveExport(
    Isolate* isolate, Handle<SyntheticModule> module,
    Handle<String> module_specifier, Handle<String> export_name,
    MessageLocation loc, bool must_resolve) {
  Handle<Object> object(module->exports()->Lookup(export_name), isolate);
  if (IsCell(*object)) return Handle<Cell>::cast(object);

  if (!must_resolve) return MaybeHandle<Cell>();

  return isolate->ThrowAt<Cell>(
      isolate->factory()->NewSyntaxError(MessageTemplate::kUnresolvableExport,
                                         module_specifier, export_name),
      &loc);
}

// Implements Synthetic Module Record's Instantiate concrete method :
// https://heycam.github.io/webidl/#smr-instantiate
bool SyntheticModule::PrepareInstantiate(Isolate* isolate,
                                         Handle<SyntheticModule> module,
                                         v8::Local<v8::Context> context) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  Handle<FixedArray> export_names(module->export_names(), isolate);
  // Spec step 7: For each export_name in module->export_names...
  for (int i = 0, n = export_names->length(); i < n; ++i) {
    // Spec step 7.1: Create a new mutable binding for export_name.
    // Spec step 7.2: Initialize the new mutable binding to undefined.
    Handle<Cell> cell = isolate->factory()->NewCell();
    Handle<String> name(String::cast(export_names->get(i)), isolate);
    CHECK(IsTheHole(exports->Lookup(name), isolate));
    exports = ObjectHashTable::Put(exports, name, cell);
  }
  module->set_exports(*exports);
  return true;
}

// Second step of module instantiation.  No real work to do for SyntheticModule
// as there are no imports or indirect exports to resolve;
// just update status.
bool SyntheticModule::FinishInstantiate(Isolate* isolate,
                                        Handle<SyntheticModule> module) {
  module->SetStatus(kLinked);
  return true;
}

// Implements Synthetic Module Record's Evaluate concrete method:
// https://heycam.github.io/webidl/#smr-evaluate
MaybeHandle<Object> SyntheticModule::Evaluate(Isolate* isolate,
                                              Handle<SyntheticModule> module) {
  module->SetStatus(kEvaluating);

  v8::Module::SyntheticModuleEvaluationSteps evaluation_steps =
      FUNCTION_CAST<v8::Module::SyntheticModuleEvaluationSteps>(
          module->evaluation_steps()->foreign_address());
  v8::Local<v8::Value> result;
  if (!evaluation_steps(Utils::ToLocal(isolate->native_context()),
                        Utils::ToLocal(Handle<Module>::cast(module)))
           .ToLocal(&result)) {
    module->RecordError(isolate, isolate->exception());
    return MaybeHandle<Object>();
  }

  module->SetStatus(kEvaluated);

  Handle<Object> result_from_callback = Utils::OpenHandle(*result);

  Handle<JSPromise> capability;
  if (IsJSPromise(*result_from_callback)) {
    capability = Handle<JSPromise>::cast(result_from_callback);
  } else {
    // The host's evaluation steps should have returned a resolved Promise,
    // but as an allowance to hosts that have not yet finished the migration
    // to top-level await, create a Promise if the callback result didn't give
    // us one.
    capability = isolate->factory()->NewJSPromise();
    JSPromise::Resolve(capability, isolate->factory()->undefined_value())
        .ToHandleChecked();
  }

  module->set_top_level_capability(*capability);

  return result_from_callback;
}

}  // namespace internal
}  // namespace v8
