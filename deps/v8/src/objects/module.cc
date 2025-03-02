// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/module.h"

#include <unordered_map>
#include <unordered_set>

#include "src/api/api-inl.h"
#include "src/ast/modules.h"
#include "src/builtins/accessors.h"
#include "src/common/assert-scope.h"
#include "src/heap/heap-inl.h"
#include "src/objects/cell-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/source-text-module.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

namespace {
#ifdef DEBUG
void PrintModuleName(Tagged<Module> module, std::ostream& os) {
  if (IsSourceTextModule(module)) {
    Print(Cast<SourceTextModule>(module)->GetScript()->GetNameOrSourceURL(),
          os);
  } else {
    Print(Cast<SyntheticModule>(module)->name(), os);
  }
#ifndef OBJECT_PRINT
  os << "\n";
#endif  // OBJECT_PRINT
}

void PrintStatusTransition(Tagged<Module> module, Module::Status old_status) {
  if (!v8_flags.trace_module_status) return;
  StdoutStream os;
  os << "Changing module status from " << Module::StatusString(old_status)
     << " to "
     << Module::StatusString(static_cast<Module::Status>(module->status()))
     << " for ";
  PrintModuleName(module, os);
}

void PrintStatusMessage(Tagged<Module> module, const char* message) {
  if (!v8_flags.trace_module_status) return;
  StdoutStream os;
  os << "Instantiating module ";
  PrintModuleName(module, os);
}
#endif  // DEBUG

void SetStatusInternal(Tagged<Module> module, Module::Status new_status) {
  DisallowGarbageCollection no_gc;
#ifdef DEBUG
  Module::Status old_status = static_cast<Module::Status>(module->status());
  module->set_status(new_status);
  PrintStatusTransition(module, old_status);
#else
  module->set_status(new_status);
#endif  // DEBUG
}

}  // end namespace

#ifdef DEBUG
// static
const char* Module::StatusString(Module::Status status) {
  switch (status) {
    case Module::kUnlinked:
      return "Unlinked";
    case Module::kPreLinking:
      return "PreLinking";
    case Module::kLinking:
      return "Linking";
    case Module::kLinked:
      return "Linked";
    case Module::kEvaluating:
      return "Evaluating";
    case Module::kEvaluatingAsync:
      return "EvaluatingAsync";
    case Module::kEvaluated:
      return "Evaluated";
    case Module::kErrored:
      return "Errored";
  }
}
#endif  // DEBUG

void Module::SetStatus(Status new_status) {
  DisallowGarbageCollection no_gc;
  DCHECK_LE(status(), new_status);
  DCHECK_NE(new_status, Module::kErrored);
  SetStatusInternal(*this, new_status);
}

void Module::RecordError(Isolate* isolate, Tagged<Object> error) {
  DisallowGarbageCollection no_gc;
  // Allow overriding exceptions with termination exceptions.
  DCHECK_IMPLIES(isolate->is_catchable_by_javascript(error),
                 IsTheHole(exception(), isolate));
  DCHECK(!IsTheHole(error, isolate));
  if (IsSourceTextModule(*this)) {
    // Revert to minimal SFI in case we have already been instantiating or
    // evaluating.
    auto self = Cast<SourceTextModule>(*this);
    self->set_code(self->GetSharedFunctionInfo());
  }
  SetStatusInternal(*this, Module::kErrored);
  if (isolate->is_catchable_by_javascript(error)) {
    set_exception(error);
  } else {
    // v8::TryCatch uses `null` for termination exceptions.
    set_exception(ReadOnlyRoots(isolate).null_value());
  }
}

void Module::ResetGraph(Isolate* isolate, DirectHandle<Module> module) {
  DCHECK_NE(module->status(), kEvaluating);
  if (module->status() != kPreLinking && module->status() != kLinking) {
    return;
  }

  DirectHandle<FixedArray> requested_modules =
      IsSourceTextModule(*module)
          ? DirectHandle<FixedArray>(
                Cast<SourceTextModule>(*module)->requested_modules(), isolate)
          : DirectHandle<FixedArray>();
  Reset(isolate, module);

  if (!IsSourceTextModule(*module)) {
    DCHECK(IsSyntheticModule(*module));
    return;
  }
  for (int i = 0; i < requested_modules->length(); ++i) {
    DirectHandle<Object> descendant(requested_modules->get(i), isolate);
    if (IsModule(*descendant)) {
      ResetGraph(isolate, Cast<Module>(descendant));
    } else {
      // The requested module is either an undefined or a WasmModule object.
#if V8_ENABLE_WEBASSEMBLY
      DCHECK(IsUndefined(*descendant, isolate) ||
             IsWasmModuleObject(*descendant));
#else
      DCHECK(IsUndefined(*descendant, isolate));
#endif
    }
  }
}

void Module::Reset(Isolate* isolate, DirectHandle<Module> module) {
  DCHECK(module->status() == kPreLinking || module->status() == kLinking);
  DCHECK(IsTheHole(module->exception(), isolate));
  // The namespace object cannot exist, because it would have been created
  // by RunInitializationCode, which is called only after this module's SCC
  // succeeds instantiation.
  DCHECK(!IsJSModuleNamespace(module->module_namespace()));
  const int export_count =
      IsSourceTextModule(*module)
          ? Cast<SourceTextModule>(*module)->regular_exports()->length()
          : Cast<SyntheticModule>(*module)->export_names()->length();
  DirectHandle<ObjectHashTable> exports =
      ObjectHashTable::New(isolate, export_count);

  if (IsSourceTextModule(*module)) {
    SourceTextModule::Reset(isolate, Cast<SourceTextModule>(module));
  }

  module->set_exports(*exports);
  SetStatusInternal(*module, kUnlinked);
}

Tagged<Object> Module::GetException() {
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(status(), Module::kErrored);
  DCHECK(!IsTheHole(exception()));
  return exception();
}

MaybeHandle<Cell> Module::ResolveExport(Isolate* isolate, Handle<Module> module,
                                        DirectHandle<String> module_specifier,
                                        Handle<String> export_name,
                                        MessageLocation loc, bool must_resolve,
                                        Module::ResolveSet* resolve_set) {
  DCHECK_GE(module->status(), kPreLinking);
  DCHECK_NE(module->status(), kEvaluating);

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::ResolveExport(
        isolate, Cast<SourceTextModule>(module), module_specifier, export_name,
        loc, must_resolve, resolve_set);
  } else {
    return SyntheticModule::ResolveExport(
        isolate, Cast<SyntheticModule>(module), module_specifier, export_name,
        loc, must_resolve);
  }
}

bool Module::Instantiate(Isolate* isolate, Handle<Module> module,
                         v8::Local<v8::Context> context,
                         v8::Module::ResolveModuleCallback module_callback,
                         v8::Module::ResolveSourceCallback source_callback) {
#ifdef DEBUG
  PrintStatusMessage(*module, "Instantiating module ");
#endif  // DEBUG

  if (!PrepareInstantiate(isolate, module, context, module_callback,
                          source_callback)) {
    ResetGraph(isolate, module);
    DCHECK_EQ(module->status(), kUnlinked);
    return false;
  }
  Zone zone(isolate->allocator(), ZONE_NAME);
  ZoneForwardList<Handle<SourceTextModule>> stack(&zone);
  unsigned dfs_index = 0;
  if (!FinishInstantiate(isolate, module, &stack, &dfs_index, &zone)) {
    ResetGraph(isolate, module);
    DCHECK_EQ(module->status(), kUnlinked);
    return false;
  }
  DCHECK(module->status() == kLinked || module->status() == kEvaluated ||
         module->status() == kEvaluatingAsync || module->status() == kErrored);
  DCHECK(stack.empty());
  return true;
}

bool Module::PrepareInstantiate(
    Isolate* isolate, DirectHandle<Module> module,
    v8::Local<v8::Context> context,
    v8::Module::ResolveModuleCallback module_callback,
    v8::Module::ResolveSourceCallback source_callback) {
  DCHECK_NE(module->status(), kEvaluating);
  DCHECK_NE(module->status(), kLinking);
  if (module->status() >= kPreLinking) return true;
  module->SetStatus(kPreLinking);
  STACK_CHECK(isolate, false);

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::PrepareInstantiate(
        isolate, Cast<SourceTextModule>(module), context, module_callback,
        source_callback);
  } else {
    return SyntheticModule::PrepareInstantiate(
        isolate, Cast<SyntheticModule>(module), context);
  }
}

bool Module::FinishInstantiate(Isolate* isolate, Handle<Module> module,
                               ZoneForwardList<Handle<SourceTextModule>>* stack,
                               unsigned* dfs_index, Zone* zone) {
  DCHECK_NE(module->status(), kEvaluating);
  if (module->status() >= kLinking) return true;
  DCHECK_EQ(module->status(), kPreLinking);
  STACK_CHECK(isolate, false);

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::FinishInstantiate(
        isolate, Cast<SourceTextModule>(module), stack, dfs_index, zone);
  } else {
    return SyntheticModule::FinishInstantiate(isolate,
                                              Cast<SyntheticModule>(module));
  }
}

MaybeDirectHandle<Object> Module::Evaluate(Isolate* isolate,
                                           Handle<Module> module) {
#ifdef DEBUG
  PrintStatusMessage(*module, "Evaluating module ");
#endif  // DEBUG
  int module_status = module->status();

  // In the event of errored evaluation, return a rejected promise.
  if (module_status == kErrored) {
    // If we have a top level capability we assume it has already been
    // rejected, and return it here. Otherwise create a new promise and
    // reject it with the module's exception.
    if (IsJSPromise(module->top_level_capability())) {
      DirectHandle<JSPromise> top_level_capability(
          Cast<JSPromise>(module->top_level_capability()), isolate);
      DCHECK(top_level_capability->status() == Promise::kRejected &&
             top_level_capability->result() == module->exception());
      return top_level_capability;
    }
    DirectHandle<JSPromise> capability = isolate->factory()->NewJSPromise();
    JSPromise::Reject(capability, direct_handle(module->exception(), isolate));
    return capability;
  }

  // Start of Evaluate () Concrete Method
  // 2. Assert: module.[[Status]] is one of LINKED, EVALUATING-ASYNC, or
  //    EVALUATED.
  CHECK(module_status == kLinked || module_status == kEvaluatingAsync ||
        module_status == kEvaluated);

  // 3. If module.[[Status]] is either EVALUATING-ASYNC or EVALUATED, set module
  //    to module.[[CycleRoot]].
  // A Synthetic Module has no children so it is its own cycle root.
  if (module_status >= kEvaluatingAsync && IsSourceTextModule(*module)) {
    module = Cast<SourceTextModule>(module)->GetCycleRoot(isolate);
  }

  // 4. If module.[[TopLevelCapability]] is not EMPTY, then
  //    a. Return module.[[TopLevelCapability]].[[Promise]].
  if (IsJSPromise(module->top_level_capability())) {
    return direct_handle(Cast<JSPromise>(module->top_level_capability()),
                         isolate);
  }
  DCHECK(IsUndefined(module->top_level_capability()));

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::Evaluate(isolate, Cast<SourceTextModule>(module));
  } else {
    return SyntheticModule::Evaluate(isolate, Cast<SyntheticModule>(module));
  }
}

DirectHandle<JSModuleNamespace> Module::GetModuleNamespace(
    Isolate* isolate, Handle<Module> module) {
  DirectHandle<HeapObject> object(module->module_namespace(), isolate);
  ReadOnlyRoots roots(isolate);
  if (!IsUndefined(*object, roots)) {
    // Namespace object already exists.
    return Cast<JSModuleNamespace>(object);
  }

  // Collect the export names.
  Zone zone(isolate->allocator(), ZONE_NAME);
  UnorderedModuleSet visited(&zone);

  if (IsSourceTextModule(*module)) {
    SourceTextModule::FetchStarExports(isolate, Cast<SourceTextModule>(module),
                                       &zone, &visited);
  }

  DirectHandle<ObjectHashTable> exports(module->exports(), isolate);
  ZoneVector<IndirectHandle<String>> names(&zone);
  names.reserve(exports->NumberOfElements());
  for (InternalIndex i : exports->IterateEntries()) {
    Tagged<Object> key;
    if (!exports->ToKey(roots, i, &key)) continue;
    names.push_back(handle(Cast<String>(key), isolate));
  }
  DCHECK_EQ(static_cast<int>(names.size()), exports->NumberOfElements());

  // Sort them alphabetically.
  std::sort(names.begin(), names.end(),
            [&isolate](IndirectHandle<String> a, IndirectHandle<String> b) {
              return String::Compare(isolate, a, b) ==
                     ComparisonResult::kLessThan;
            });

  // Create the namespace object (initially empty).
  DirectHandle<JSModuleNamespace> ns =
      isolate->factory()->NewJSModuleNamespace();
  ns->set_module(*module);
  module->set_module_namespace(*ns);

  // Create the properties in the namespace object. Transition the object
  // to dictionary mode so that property addition is faster.
  PropertyAttributes attr = DONT_DELETE;
  JSObject::NormalizeProperties(isolate, ns, CLEAR_INOBJECT_PROPERTIES,
                                static_cast<int>(names.size()),
                                "JSModuleNamespace");
  JSObject::NormalizeElements(ns);
  for (const auto& name : names) {
    uint32_t index = 0;
    if (name->AsArrayIndex(&index)) {
      JSObject::SetNormalizedElement(
          ns, index, Accessors::MakeModuleNamespaceEntryInfo(isolate, name),
          PropertyDetails(PropertyKind::kAccessor, attr,
                          PropertyCellType::kMutable));
    } else {
      JSObject::SetNormalizedProperty(
          ns, name, Accessors::MakeModuleNamespaceEntryInfo(isolate, name),
          PropertyDetails(PropertyKind::kAccessor, attr,
                          PropertyCellType::kMutable));
    }
  }
  JSObject::PreventExtensions(isolate, ns, kThrowOnError).ToChecked();

  // Optimize the namespace object as a prototype, for two reasons:
  // - The object's map is guaranteed not to be shared. ICs rely on this.
  // - We can store a pointer from the map back to the namespace object.
  //   Turbofan can use this for inlining the access.
  JSObject::OptimizeAsPrototype(ns);

  DirectHandle<PrototypeInfo> proto_info =
      Map::GetOrCreatePrototypeInfo(ns, isolate);
  proto_info->set_module_namespace(*ns);
  return ns;
}

bool JSModuleNamespace::HasExport(Isolate* isolate, DirectHandle<String> name) {
  DirectHandle<Object> object(module()->exports()->Lookup(name), isolate);
  return !IsTheHole(*object, isolate);
}

MaybeDirectHandle<Object> JSModuleNamespace::GetExport(
    Isolate* isolate, DirectHandle<String> name) {
  DirectHandle<Object> object(module()->exports()->Lookup(name), isolate);
  if (IsTheHole(*object, isolate)) {
    return isolate->factory()->undefined_value();
  }

  DirectHandle<Object> value(Cast<Cell>(*object)->value(), isolate);
  if (IsTheHole(*value, isolate)) {
    // According to https://tc39.es/ecma262/#sec-InnerModuleLinking
    // step 10 and
    // https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment
    // step 8-25, variables must be declared in Link. And according to
    // https://tc39.es/ecma262/#sec-module-namespace-exotic-objects-get-p-receiver,
    // here accessing uninitialized variable error should be thrown.
    THROW_NEW_ERROR(isolate,
                    NewReferenceError(
                        MessageTemplate::kAccessedUninitializedVariable, name));
  }

  return value;
}

Maybe<PropertyAttributes> JSModuleNamespace::GetPropertyAttributes(
    LookupIterator* it) {
  DirectHandle<JSModuleNamespace> object = it->GetHolder<JSModuleNamespace>();
  DirectHandle<String> name = Cast<String>(it->GetName());
  DCHECK_EQ(it->state(), LookupIterator::ACCESSOR);

  Isolate* isolate = it->isolate();

  DirectHandle<Object> lookup(object->module()->exports()->Lookup(name),
                              isolate);
  if (IsTheHole(*lookup, isolate)) return Just(ABSENT);

  DirectHandle<Object> value(Cast<Cell>(lookup)->value(), isolate);
  if (IsTheHole(*value, isolate)) {
    isolate->Throw(*isolate->factory()->NewReferenceError(
        MessageTemplate::kNotDefined, name));
    return Nothing<PropertyAttributes>();
  }

  return Just(it->property_attributes());
}

// ES
// https://tc39.es/ecma262/#sec-module-namespace-exotic-objects-defineownproperty-p-desc
// static
Maybe<bool> JSModuleNamespace::DefineOwnProperty(
    Isolate* isolate, DirectHandle<JSModuleNamespace> object,
    DirectHandle<Object> key, PropertyDescriptor* desc,
    Maybe<ShouldThrow> should_throw) {
  // 1. If Type(P) is Symbol, return OrdinaryDefineOwnProperty(O, P, Desc).
  if (IsSymbol(*key)) {
    return OrdinaryDefineOwnProperty(isolate, object, key, desc, should_throw);
  }

  // 2. Let current be ? O.[[GetOwnProperty]](P).
  PropertyKey lookup_key(isolate, key);
  LookupIterator it(isolate, object, lookup_key, LookupIterator::OWN);
  PropertyDescriptor current;
  Maybe<bool> has_own = GetOwnPropertyDescriptor(&it, &current);
  MAYBE_RETURN(has_own, Nothing<bool>());

  // 3. If current is undefined, return false.
  // 4. If Desc.[[Configurable]] is present and has value true, return false.
  // 5. If Desc.[[Enumerable]] is present and has value false, return false.
  // 6. If ! IsAccessorDescriptor(Desc) is true, return false.
  // 7. If Desc.[[Writable]] is present and has value false, return false.
  // 8. If Desc.[[Value]] is present, return
  //    SameValue(Desc.[[Value]], current.[[Value]]).
  if (!has_own.FromJust() ||
      (desc->has_configurable() && desc->configurable()) ||
      (desc->has_enumerable() && !desc->enumerable()) ||
      PropertyDescriptor::IsAccessorDescriptor(desc) ||
      (desc->has_writable() && !desc->writable()) ||
      (desc->has_value() &&
       !Object::SameValue(*desc->value(), *current.value()))) {
    RETURN_FAILURE(isolate, GetShouldThrow(isolate, should_throw),
                   NewTypeError(MessageTemplate::kRedefineDisallowed, key));
  }

  return Just(true);
}

bool Module::IsGraphAsync(Isolate* isolate) const {
  DisallowGarbageCollection no_gc;

  // Only SourceTextModules may be async.
  if (!IsSourceTextModule(*this)) return false;
  Tagged<SourceTextModule> root = Cast<SourceTextModule>(*this);

  Zone zone(isolate->allocator(), ZONE_NAME);
  const size_t bucket_count = 2;
  ZoneUnorderedSet<Tagged<Module>, Module::Hash> visited(&zone, bucket_count);
  ZoneVector<Tagged<SourceTextModule>> worklist(&zone);
  visited.insert(root);
  worklist.push_back(root);

  do {
    Tagged<SourceTextModule> current = worklist.back();
    worklist.pop_back();
    DCHECK_GE(current->status(), kLinked);

    if (current->has_toplevel_await()) return true;
    Tagged<FixedArray> requested_modules = current->requested_modules();
    for (int i = 0, length = requested_modules->length(); i < length; ++i) {
      Tagged<Module> descendant = Cast<Module>(requested_modules->get(i));
      if (IsSourceTextModule(descendant)) {
        const bool cycle = !visited.insert(descendant).second;
        if (!cycle) worklist.push_back(Cast<SourceTextModule>(descendant));
      }
    }
  } while (!worklist.empty());

  return false;
}

}  // namespace internal
}  // namespace v8
