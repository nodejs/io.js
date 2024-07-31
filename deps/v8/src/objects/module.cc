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
    Print(SourceTextModule::cast(module)->GetScript()->GetNameOrSourceURL(),
          os);
  } else {
    Print(SyntheticModule::cast(module)->name(), os);
  }
#ifndef OBJECT_PRINT
  os << "\n";
#endif  // OBJECT_PRINT
}

void PrintStatusTransition(Tagged<Module> module, Module::Status old_status) {
  if (!v8_flags.trace_module_status) return;
  StdoutStream os;
  os << "Changing module status from " << old_status << " to "
     << module->status() << " for ";
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
    // Revert to minmal SFI in case we have already been instantiating or
    // evaluating.
    auto self = SourceTextModule::cast(*this);
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

void Module::ResetGraph(Isolate* isolate, Handle<Module> module) {
  DCHECK_NE(module->status(), kEvaluating);
  if (module->status() != kPreLinking && module->status() != kLinking) {
    return;
  }

  Handle<FixedArray> requested_modules =
      IsSourceTextModule(*module)
          ? Handle<FixedArray>(
                SourceTextModule::cast(*module)->requested_modules(), isolate)
          : Handle<FixedArray>();
  Reset(isolate, module);

  if (!IsSourceTextModule(*module)) {
    DCHECK(IsSyntheticModule(*module));
    return;
  }
  for (int i = 0; i < requested_modules->length(); ++i) {
    Handle<Object> descendant(requested_modules->get(i), isolate);
    if (IsModule(*descendant)) {
      ResetGraph(isolate, Handle<Module>::cast(descendant));
    } else {
      DCHECK(IsUndefined(*descendant, isolate));
    }
  }
}

void Module::Reset(Isolate* isolate, Handle<Module> module) {
  DCHECK(module->status() == kPreLinking || module->status() == kLinking);
  DCHECK(IsTheHole(module->exception(), isolate));
  // The namespace object cannot exist, because it would have been created
  // by RunInitializationCode, which is called only after this module's SCC
  // succeeds instantiation.
  DCHECK(!IsJSModuleNamespace(module->module_namespace()));
  const int export_count =
      IsSourceTextModule(*module)
          ? SourceTextModule::cast(*module)->regular_exports()->length()
          : SyntheticModule::cast(*module)->export_names()->length();
  Handle<ObjectHashTable> exports = ObjectHashTable::New(isolate, export_count);

  if (IsSourceTextModule(*module)) {
    SourceTextModule::Reset(isolate, Handle<SourceTextModule>::cast(module));
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
                                        Handle<String> module_specifier,
                                        Handle<String> export_name,
                                        MessageLocation loc, bool must_resolve,
                                        Module::ResolveSet* resolve_set) {
  DCHECK_GE(module->status(), kPreLinking);
  DCHECK_NE(module->status(), kEvaluating);

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::ResolveExport(
        isolate, Handle<SourceTextModule>::cast(module), module_specifier,
        export_name, loc, must_resolve, resolve_set);
  } else {
    return SyntheticModule::ResolveExport(
        isolate, Handle<SyntheticModule>::cast(module), module_specifier,
        export_name, loc, must_resolve);
  }
}

bool Module::Instantiate(
    Isolate* isolate, Handle<Module> module, v8::Local<v8::Context> context,
    v8::Module::ResolveModuleCallback callback,
    DeprecatedResolveCallback callback_without_import_assertions) {
#ifdef DEBUG
  PrintStatusMessage(*module, "Instantiating module ");
#endif  // DEBUG

  if (!PrepareInstantiate(isolate, module, context, callback,
                          callback_without_import_assertions)) {
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
         module->status() == kErrored);
  DCHECK(stack.empty());
  return true;
}

bool Module::PrepareInstantiate(
    Isolate* isolate, Handle<Module> module, v8::Local<v8::Context> context,
    v8::Module::ResolveModuleCallback callback,
    DeprecatedResolveCallback callback_without_import_assertions) {
  DCHECK_NE(module->status(), kEvaluating);
  DCHECK_NE(module->status(), kLinking);
  if (module->status() >= kPreLinking) return true;
  module->SetStatus(kPreLinking);
  STACK_CHECK(isolate, false);

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::PrepareInstantiate(
        isolate, Handle<SourceTextModule>::cast(module), context, callback,
        callback_without_import_assertions);
  } else {
    return SyntheticModule::PrepareInstantiate(
        isolate, Handle<SyntheticModule>::cast(module), context);
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
        isolate, Handle<SourceTextModule>::cast(module), stack, dfs_index,
        zone);
  } else {
    return SyntheticModule::FinishInstantiate(
        isolate, Handle<SyntheticModule>::cast(module));
  }
}

MaybeHandle<Object> Module::Evaluate(Isolate* isolate, Handle<Module> module) {
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
      Handle<JSPromise> top_level_capability(
          JSPromise::cast(module->top_level_capability()), isolate);
      DCHECK(top_level_capability->status() == Promise::kRejected &&
             top_level_capability->result() == module->exception());
      return top_level_capability;
    }
    Handle<JSPromise> capability = isolate->factory()->NewJSPromise();
    JSPromise::Reject(capability, handle(module->exception(), isolate));
    return capability;
  }

  // Start of Evaluate () Concrete Method
  // 2. Assert: module.[[Status]] is "linked" or "evaluated".
  CHECK(module_status == kLinked || module_status == kEvaluated);

  // 3. If module.[[Status]] is "evaluated", set module to
  //    module.[[CycleRoot]].
  // A Synthetic Module has no children so it is its own cycle root.
  if (module_status == kEvaluated && IsSourceTextModule(*module)) {
    module = Handle<SourceTextModule>::cast(module)->GetCycleRoot(isolate);
  }

  // 4. If module.[[TopLevelCapability]] is not undefined, then
  //    a. Return module.[[TopLevelCapability]].[[Promise]].
  if (IsJSPromise(module->top_level_capability())) {
    return handle(JSPromise::cast(module->top_level_capability()), isolate);
  }
  DCHECK(IsUndefined(module->top_level_capability()));

  if (IsSourceTextModule(*module)) {
    return SourceTextModule::Evaluate(isolate,
                                      Handle<SourceTextModule>::cast(module));
  } else {
    return SyntheticModule::Evaluate(isolate,
                                     Handle<SyntheticModule>::cast(module));
  }
}

Handle<JSModuleNamespace> Module::GetModuleNamespace(Isolate* isolate,
                                                     Handle<Module> module) {
  Handle<HeapObject> object(module->module_namespace(), isolate);
  ReadOnlyRoots roots(isolate);
  if (!IsUndefined(*object, roots)) {
    // Namespace object already exists.
    return Handle<JSModuleNamespace>::cast(object);
  }

  // Collect the export names.
  Zone zone(isolate->allocator(), ZONE_NAME);
  UnorderedModuleSet visited(&zone);

  if (IsSourceTextModule(*module)) {
    SourceTextModule::FetchStarExports(
        isolate, Handle<SourceTextModule>::cast(module), &zone, &visited);
  }

  Handle<ObjectHashTable> exports(module->exports(), isolate);
  ZoneVector<Handle<String>> names(&zone);
  names.reserve(exports->NumberOfElements());
  for (InternalIndex i : exports->IterateEntries()) {
    Tagged<Object> key;
    if (!exports->ToKey(roots, i, &key)) continue;
    names.push_back(handle(String::cast(key), isolate));
  }
  DCHECK_EQ(static_cast<int>(names.size()), exports->NumberOfElements());

  // Sort them alphabetically.
  std::sort(names.begin(), names.end(),
            [&isolate](Handle<String> a, Handle<String> b) {
              return String::Compare(isolate, a, b) ==
                     ComparisonResult::kLessThan;
            });

  // Create the namespace object (initially empty).
  Handle<JSModuleNamespace> ns = isolate->factory()->NewJSModuleNamespace();
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

  Handle<PrototypeInfo> proto_info =
      Map::GetOrCreatePrototypeInfo(Handle<JSObject>::cast(ns), isolate);
  proto_info->set_module_namespace(*ns);
  return ns;
}

bool JSModuleNamespace::HasExport(Isolate* isolate, Handle<String> name) {
  Handle<Object> object(module()->exports()->Lookup(name), isolate);
  return !IsTheHole(*object, isolate);
}

MaybeHandle<Object> JSModuleNamespace::GetExport(Isolate* isolate,
                                                 Handle<String> name) {
  Handle<Object> object(module()->exports()->Lookup(name), isolate);
  if (IsTheHole(*object, isolate)) {
    return isolate->factory()->undefined_value();
  }

  Handle<Object> value(Cell::cast(*object)->value(), isolate);
  if (IsTheHole(*value, isolate)) {
    // According to https://tc39.es/ecma262/#sec-InnerModuleLinking
    // step 10 and
    // https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment
    // step 8-25, variables must be declared in Link. And according to
    // https://tc39.es/ecma262/#sec-module-namespace-exotic-objects-get-p-receiver,
    // here accessing uninitialized variable error should be throwed.
    THROW_NEW_ERROR(isolate,
                    NewReferenceError(
                        MessageTemplate::kAccessedUninitializedVariable, name),
                    Object);
  }

  return value;
}

Maybe<PropertyAttributes> JSModuleNamespace::GetPropertyAttributes(
    LookupIterator* it) {
  Handle<JSModuleNamespace> object = it->GetHolder<JSModuleNamespace>();
  Handle<String> name = Handle<String>::cast(it->GetName());
  DCHECK_EQ(it->state(), LookupIterator::ACCESSOR);

  Isolate* isolate = it->isolate();

  Handle<Object> lookup(object->module()->exports()->Lookup(name), isolate);
  if (IsTheHole(*lookup, isolate)) return Just(ABSENT);

  Handle<Object> value(Handle<Cell>::cast(lookup)->value(), isolate);
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
    Isolate* isolate, Handle<JSModuleNamespace> object, Handle<Object> key,
    PropertyDescriptor* desc, Maybe<ShouldThrow> should_throw) {
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
  Tagged<SourceTextModule> root = SourceTextModule::cast(*this);

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

    if (current->async()) return true;
    Tagged<FixedArray> requested_modules = current->requested_modules();
    for (int i = 0, length = requested_modules->length(); i < length; ++i) {
      Tagged<Module> descendant = Module::cast(requested_modules->get(i));
      if (IsSourceTextModule(descendant)) {
        const bool cycle = !visited.insert(descendant).second;
        if (!cycle) worklist.push_back(SourceTextModule::cast(descendant));
      }
    }
  } while (!worklist.empty());

  return false;
}

}  // namespace internal
}  // namespace v8
