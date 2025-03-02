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
                                       DirectHandle<SyntheticModule> module,
                                       DirectHandle<String> export_name,
                                       DirectHandle<Object> export_value) {
  DirectHandle<ObjectHashTable> exports(module->exports(), isolate);
  DirectHandle<Object> export_object(exports->Lookup(export_name), isolate);

  if (!IsCell(*export_object)) {
    isolate->Throw(*isolate->factory()->NewReferenceError(
        MessageTemplate::kModuleExportUndefined, export_name));
    return Nothing<bool>();
  }

  // Spec step 2: Set the mutable binding of export_name to export_value
  Cast<Cell>(*export_object)->set_value(*export_value);

  return Just(true);
}

void SyntheticModule::SetExportStrict(Isolate* isolate,
                                      DirectHandle<SyntheticModule> module,
                                      DirectHandle<String> export_name,
                                      DirectHandle<Object> export_value) {
  DirectHandle<ObjectHashTable> exports(module->exports(), isolate);
  DirectHandle<Object> export_object(exports->Lookup(export_name), isolate);
  CHECK(IsCell(*export_object));
  Maybe<bool> set_export_result =
      SetExport(isolate, module, export_name, export_value);
  CHECK(set_export_result.FromJust());
}

// Implements Synthetic Module Record's ResolveExport concrete method:
// https://heycam.github.io/webidl/#smr-resolveexport
MaybeHandle<Cell> SyntheticModule::ResolveExport(
    Isolate* isolate, DirectHandle<SyntheticModule> module,
    DirectHandle<String> module_specifier, DirectHandle<String> export_name,
    MessageLocation loc, bool must_resolve) {
  Handle<Object> object(module->exports()->Lookup(export_name), isolate);
  if (IsCell(*object)) return Cast<Cell>(object);

  if (!must_resolve) return kNullMaybeHandle;

  isolate->ThrowAt(
      isolate->factory()->NewSyntaxError(MessageTemplate::kUnresolvableExport,
                                         module_specifier, export_name),
      &loc);
  return kNullMaybeHandle;
}

// Implements Synthetic Module Record's Instantiate concrete method :
// https://heycam.github.io/webidl/#smr-instantiate
bool SyntheticModule::PrepareInstantiate(Isolate* isolate,
                                         DirectHandle<SyntheticModule> module,
                                         v8::Local<v8::Context> context) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  DirectHandle<FixedArray> export_names(module->export_names(), isolate);
  // Spec step 7: For each export_name in module->export_names...
  for (int i = 0, n = export_names->length(); i < n; ++i) {
    // Spec step 7.1: Create a new mutable binding for export_name.
    // Spec step 7.2: Initialize the new mutable binding to undefined.
    DirectHandle<Cell> cell = isolate->factory()->NewCell();
    DirectHandle<String> name(Cast<String>(export_names->get(i)), isolate);
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
                                        DirectHandle<SyntheticModule> module) {
  module->SetStatus(kLinked);
  return true;
}

// Implements Synthetic Module Record's Evaluate concrete method:
// https://heycam.github.io/webidl/#smr-evaluate
MaybeDirectHandle<Object> SyntheticModule::Evaluate(
    Isolate* isolate, DirectHandle<SyntheticModule> module) {
  module->SetStatus(kEvaluating);

  v8::Module::SyntheticModuleEvaluationSteps evaluation_steps =
      FUNCTION_CAST<v8::Module::SyntheticModuleEvaluationSteps>(
          module->evaluation_steps()->foreign_address<kSyntheticModuleTag>());
  v8::Local<v8::Value> result;
  if (!evaluation_steps(Utils::ToLocal(isolate->native_context()),
                        Utils::ToLocal(Cast<Module>(module)))
           .ToLocal(&result)) {
    module->RecordError(isolate, isolate->exception());
    return MaybeDirectHandle<Object>();
  }

  module->SetStatus(kEvaluated);

  DirectHandle<Object> result_from_callback = Utils::OpenDirectHandle(*result);

  DirectHandle<JSPromise> capability;
  if (IsJSPromise(*result_from_callback)) {
    capability = Cast<JSPromise>(result_from_callback);
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
