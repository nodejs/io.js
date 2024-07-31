// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/source-text-module.h"

#include "src/api/api-inl.h"
#include "src/ast/modules.h"
#include "src/builtins/accessors.h"
#include "src/common/assert-scope.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

struct StringHandleHash {
  V8_INLINE size_t operator()(Handle<String> string) const {
    return string->EnsureHash();
  }
};

struct StringHandleEqual {
  V8_INLINE bool operator()(Handle<String> lhs, Handle<String> rhs) const {
    return lhs->Equals(*rhs);
  }
};

class UnorderedStringSet
    : public std::unordered_set<Handle<String>, StringHandleHash,
                                StringHandleEqual,
                                ZoneAllocator<Handle<String>>> {
 public:
  explicit UnorderedStringSet(Zone* zone)
      : std::unordered_set<Handle<String>, StringHandleHash, StringHandleEqual,
                           ZoneAllocator<Handle<String>>>(
            2 /* bucket count */, StringHandleHash(), StringHandleEqual(),
            ZoneAllocator<Handle<String>>(zone)) {}
};

class UnorderedStringMap
    : public std::unordered_map<
          Handle<String>, Handle<Object>, StringHandleHash, StringHandleEqual,
          ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>> {
 public:
  explicit UnorderedStringMap(Zone* zone)
      : std::unordered_map<
            Handle<String>, Handle<Object>, StringHandleHash, StringHandleEqual,
            ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>>(
            2 /* bucket count */, StringHandleHash(), StringHandleEqual(),
            ZoneAllocator<std::pair<const Handle<String>, Handle<Object>>>(
                zone)) {}
};

class Module::ResolveSet
    : public std::unordered_map<
          Handle<Module>, UnorderedStringSet*, ModuleHandleHash,
          ModuleHandleEqual,
          ZoneAllocator<std::pair<const Handle<Module>, UnorderedStringSet*>>> {
 public:
  explicit ResolveSet(Zone* zone)
      : std::unordered_map<Handle<Module>, UnorderedStringSet*,
                           ModuleHandleHash, ModuleHandleEqual,
                           ZoneAllocator<std::pair<const Handle<Module>,
                                                   UnorderedStringSet*>>>(
            2 /* bucket count */, ModuleHandleHash(), ModuleHandleEqual(),
            ZoneAllocator<std::pair<const Handle<Module>, UnorderedStringSet*>>(
                zone)),
        zone_(zone) {}

  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
};

struct SourceTextModule::AsyncEvaluatingOrdinalCompare {
  bool operator()(Handle<SourceTextModule> lhs,
                  Handle<SourceTextModule> rhs) const {
    DCHECK(lhs->IsAsyncEvaluating());
    DCHECK(rhs->IsAsyncEvaluating());
    return lhs->async_evaluating_ordinal() < rhs->async_evaluating_ordinal();
  }
};

Tagged<SharedFunctionInfo> SourceTextModule::GetSharedFunctionInfo() const {
  DisallowGarbageCollection no_gc;
  switch (status()) {
    case kUnlinked:
    case kPreLinking:
      return SharedFunctionInfo::cast(code());
    case kLinking:
      return JSFunction::cast(code())->shared();
    case kLinked:
    case kEvaluating:
    case kEvaluatingAsync:
    case kEvaluated:
      return JSGeneratorObject::cast(code())->function()->shared();
    case kErrored:
      return SharedFunctionInfo::cast(code());
  }
  UNREACHABLE();
}

Tagged<Script> SourceTextModule::GetScript() const {
  DisallowGarbageCollection no_gc;
  return Script::cast(GetSharedFunctionInfo()->script());
}

int SourceTextModule::ExportIndex(int cell_index) {
  DCHECK_EQ(SourceTextModuleDescriptor::GetCellIndexKind(cell_index),
            SourceTextModuleDescriptor::kExport);
  return cell_index - 1;
}

int SourceTextModule::ImportIndex(int cell_index) {
  DCHECK_EQ(SourceTextModuleDescriptor::GetCellIndexKind(cell_index),
            SourceTextModuleDescriptor::kImport);
  return -cell_index - 1;
}

void SourceTextModule::CreateIndirectExport(
    Isolate* isolate, Handle<SourceTextModule> module, Handle<String> name,
    Handle<SourceTextModuleInfoEntry> entry) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  DCHECK(IsTheHole(exports->Lookup(name), isolate));
  exports = ObjectHashTable::Put(exports, name, entry);
  module->set_exports(*exports);
}

void SourceTextModule::CreateExport(Isolate* isolate,
                                    Handle<SourceTextModule> module,
                                    int cell_index, Handle<FixedArray> names) {
  DCHECK_LT(0, names->length());
  Handle<Cell> cell = isolate->factory()->NewCell();
  module->regular_exports()->set(ExportIndex(cell_index), *cell);

  Handle<ObjectHashTable> exports(module->exports(), isolate);
  for (int i = 0, n = names->length(); i < n; ++i) {
    Handle<String> name(String::cast(names->get(i)), isolate);
    DCHECK(IsTheHole(exports->Lookup(name), isolate));
    exports = ObjectHashTable::Put(exports, name, cell);
  }
  module->set_exports(*exports);
}

Tagged<Cell> SourceTextModule::GetCell(int cell_index) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> cell;
  switch (SourceTextModuleDescriptor::GetCellIndexKind(cell_index)) {
    case SourceTextModuleDescriptor::kImport:
      cell = regular_imports()->get(ImportIndex(cell_index));
      break;
    case SourceTextModuleDescriptor::kExport:
      cell = regular_exports()->get(ExportIndex(cell_index));
      break;
    case SourceTextModuleDescriptor::kInvalid:
      UNREACHABLE();
  }
  return Cell::cast(cell);
}

Handle<Object> SourceTextModule::LoadVariable(Isolate* isolate,
                                              Handle<SourceTextModule> module,
                                              int cell_index) {
  return handle(module->GetCell(cell_index)->value(), isolate);
}

void SourceTextModule::StoreVariable(Handle<SourceTextModule> module,
                                     int cell_index, Handle<Object> value) {
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(SourceTextModuleDescriptor::GetCellIndexKind(cell_index),
            SourceTextModuleDescriptor::kExport);
  module->GetCell(cell_index)->set_value(*value);
}

MaybeHandle<Cell> SourceTextModule::ResolveExport(
    Isolate* isolate, Handle<SourceTextModule> module,
    Handle<String> module_specifier, Handle<String> export_name,
    MessageLocation loc, bool must_resolve, Module::ResolveSet* resolve_set) {
  Handle<Object> object(module->exports()->Lookup(export_name), isolate);
  if (IsCell(*object)) {
    // Already resolved (e.g. because it's a local export).
    return Handle<Cell>::cast(object);
  }

  // Check for cycle before recursing.
  {
    // Attempt insertion with a null string set.
    auto result = resolve_set->insert({module, nullptr});
    UnorderedStringSet*& name_set = result.first->second;
    if (result.second) {
      // |module| wasn't in the map previously, so allocate a new name set.
      Zone* zone = resolve_set->zone();
      name_set = zone->New<UnorderedStringSet>(zone);
    } else if (name_set->count(export_name)) {
      // Cycle detected.
      if (must_resolve) {
        return isolate->ThrowAt<Cell>(
            isolate->factory()->NewSyntaxError(
                MessageTemplate::kCyclicModuleDependency, export_name,
                module_specifier),
            &loc);
      }
      return MaybeHandle<Cell>();
    }
    name_set->insert(export_name);
  }

  if (IsSourceTextModuleInfoEntry(*object)) {
    // Not yet resolved indirect export.
    Handle<SourceTextModuleInfoEntry> entry =
        Handle<SourceTextModuleInfoEntry>::cast(object);
    Handle<String> import_name(String::cast(entry->import_name()), isolate);
    Handle<Script> script(module->GetScript(), isolate);
    MessageLocation new_loc(script, entry->beg_pos(), entry->end_pos());

    Handle<Cell> cell;
    if (!ResolveImport(isolate, module, import_name, entry->module_request(),
                       new_loc, true, resolve_set)
             .ToHandle(&cell)) {
      DCHECK(isolate->has_exception());
      return MaybeHandle<Cell>();
    }

    // The export table may have changed but the entry in question should be
    // unchanged.
    Handle<ObjectHashTable> exports(module->exports(), isolate);
    DCHECK(IsSourceTextModuleInfoEntry(exports->Lookup(export_name)));

    exports = ObjectHashTable::Put(exports, export_name, cell);
    module->set_exports(*exports);
    return cell;
  }

  DCHECK(IsTheHole(*object, isolate));
  return SourceTextModule::ResolveExportUsingStarExports(
      isolate, module, module_specifier, export_name, loc, must_resolve,
      resolve_set);
}

MaybeHandle<Cell> SourceTextModule::ResolveImport(
    Isolate* isolate, Handle<SourceTextModule> module, Handle<String> name,
    int module_request_index, MessageLocation loc, bool must_resolve,
    Module::ResolveSet* resolve_set) {
  Handle<Module> requested_module(
      Module::cast(module->requested_modules()->get(module_request_index)),
      isolate);
  Handle<ModuleRequest> module_request(
      ModuleRequest::cast(
          module->info()->module_requests()->get(module_request_index)),
      isolate);
  Handle<String> module_specifier(String::cast(module_request->specifier()),
                                  isolate);
  MaybeHandle<Cell> result =
      Module::ResolveExport(isolate, requested_module, module_specifier, name,
                            loc, must_resolve, resolve_set);
  DCHECK_IMPLIES(isolate->has_exception(), result.is_null());
  return result;
}

MaybeHandle<Cell> SourceTextModule::ResolveExportUsingStarExports(
    Isolate* isolate, Handle<SourceTextModule> module,
    Handle<String> module_specifier, Handle<String> export_name,
    MessageLocation loc, bool must_resolve, Module::ResolveSet* resolve_set) {
  if (!export_name->Equals(ReadOnlyRoots(isolate).default_string())) {
    // Go through all star exports looking for the given name.  If multiple star
    // exports provide the name, make sure they all map it to the same cell.
    Handle<Cell> unique_cell;
    Handle<FixedArray> special_exports(module->info()->special_exports(),
                                       isolate);
    for (int i = 0, n = special_exports->length(); i < n; ++i) {
      i::Handle<i::SourceTextModuleInfoEntry> entry(
          i::SourceTextModuleInfoEntry::cast(special_exports->get(i)), isolate);
      if (!IsUndefined(entry->export_name(), isolate)) {
        continue;  // Indirect export.
      }

      Handle<Script> script(module->GetScript(), isolate);
      MessageLocation new_loc(script, entry->beg_pos(), entry->end_pos());

      Handle<Cell> cell;
      if (ResolveImport(isolate, module, export_name, entry->module_request(),
                        new_loc, false, resolve_set)
              .ToHandle(&cell)) {
        if (unique_cell.is_null()) unique_cell = cell;
        if (*unique_cell != *cell) {
          return isolate->ThrowAt<Cell>(isolate->factory()->NewSyntaxError(
                                            MessageTemplate::kAmbiguousExport,
                                            module_specifier, export_name),
                                        &loc);
        }
      } else if (isolate->has_exception()) {
        return MaybeHandle<Cell>();
      }
    }

    if (!unique_cell.is_null()) {
      // Found a unique star export for this name.
      Handle<ObjectHashTable> exports(module->exports(), isolate);
      DCHECK(IsTheHole(exports->Lookup(export_name), isolate));
      exports = ObjectHashTable::Put(exports, export_name, unique_cell);
      module->set_exports(*exports);
      return unique_cell;
    }
  }

  // Unresolvable.
  if (must_resolve) {
    return isolate->ThrowAt<Cell>(
        isolate->factory()->NewSyntaxError(MessageTemplate::kUnresolvableExport,
                                           module_specifier, export_name),
        &loc);
  }
  return MaybeHandle<Cell>();
}

bool SourceTextModule::PrepareInstantiate(
    Isolate* isolate, Handle<SourceTextModule> module,
    v8::Local<v8::Context> context, v8::Module::ResolveModuleCallback callback,
    Module::DeprecatedResolveCallback callback_without_import_assertions) {
  DCHECK_EQ(callback != nullptr, callback_without_import_assertions == nullptr);
  // Obtain requested modules.
  Handle<SourceTextModuleInfo> module_info(module->info(), isolate);
  Handle<FixedArray> module_requests(module_info->module_requests(), isolate);
  Handle<FixedArray> requested_modules(module->requested_modules(), isolate);
  for (int i = 0, length = module_requests->length(); i < length; ++i) {
    Handle<ModuleRequest> module_request(
        ModuleRequest::cast(module_requests->get(i)), isolate);
    Handle<String> specifier(module_request->specifier(), isolate);
    v8::Local<v8::Module> api_requested_module;
    if (callback) {
      Handle<FixedArray> import_attributes(module_request->import_attributes(),
                                           isolate);
      if (!callback(context, v8::Utils::ToLocal(specifier),
                    v8::Utils::FixedArrayToLocal(import_attributes),
                    v8::Utils::ToLocal(Handle<Module>::cast(module)))
               .ToLocal(&api_requested_module)) {
        return false;
      }
    } else {
      if (!callback_without_import_assertions(
               context, v8::Utils::ToLocal(specifier),
               v8::Utils::ToLocal(Handle<Module>::cast(module)))
               .ToLocal(&api_requested_module)) {
        return false;
      }
    }
    Handle<Module> requested_module = Utils::OpenHandle(*api_requested_module);
    requested_modules->set(i, *requested_module);
  }

  // Recurse.
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Module::cast(requested_modules->get(i)),
                                    isolate);
    if (!Module::PrepareInstantiate(isolate, requested_module, context,
                                    callback,
                                    callback_without_import_assertions)) {
      return false;
    }
  }

  // Set up local exports.
  // TODO(neis): Create regular_exports array here instead of in factory method?
  for (int i = 0, n = module_info->RegularExportCount(); i < n; ++i) {
    int cell_index = module_info->RegularExportCellIndex(i);
    Handle<FixedArray> export_names(module_info->RegularExportExportNames(i),
                                    isolate);
    CreateExport(isolate, module, cell_index, export_names);
  }

  // Partially set up indirect exports.
  // For each indirect export, we create the appropriate slot in the export
  // table and store its SourceTextModuleInfoEntry there.  When we later find
  // the correct Cell in the module that actually provides the value, we replace
  // the SourceTextModuleInfoEntry by that Cell (see ResolveExport).
  Handle<FixedArray> special_exports(module_info->special_exports(), isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    Handle<SourceTextModuleInfoEntry> entry(
        SourceTextModuleInfoEntry::cast(special_exports->get(i)), isolate);
    Handle<Object> export_name(entry->export_name(), isolate);
    if (IsUndefined(*export_name, isolate)) continue;  // Star export.
    CreateIndirectExport(isolate, module, Handle<String>::cast(export_name),
                         entry);
  }

  DCHECK_EQ(module->status(), kPreLinking);
  return true;
}

bool SourceTextModule::RunInitializationCode(Isolate* isolate,
                                             Handle<SourceTextModule> module) {
  DCHECK_EQ(module->status(), kLinking);
  Handle<JSFunction> function(JSFunction::cast(module->code()), isolate);
  DCHECK_EQ(MODULE_SCOPE, function->shared()->scope_info()->scope_type());
  Handle<Object> receiver = isolate->factory()->undefined_value();

  Handle<ScopeInfo> scope_info(function->shared()->scope_info(), isolate);
  Handle<Context> context = isolate->factory()->NewModuleContext(
      module, isolate->native_context(), scope_info);
  function->set_context(*context);

  MaybeHandle<Object> maybe_generator =
      Execution::Call(isolate, function, receiver, 0, {});
  Handle<Object> generator;
  if (!maybe_generator.ToHandle(&generator)) {
    DCHECK(isolate->has_exception());
    return false;
  }
  DCHECK_EQ(*function, Handle<JSGeneratorObject>::cast(generator)->function());
  module->set_code(JSGeneratorObject::cast(*generator));
  return true;
}

bool SourceTextModule::MaybeTransitionComponent(
    Isolate* isolate, Handle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, Status new_status) {
  DCHECK(new_status == kLinked || new_status == kEvaluated);
  SLOW_DCHECK(
      // {module} is on the {stack}.
      std::count_if(stack->begin(), stack->end(),
                    [&](Handle<Module> m) { return *m == *module; }) == 1);
  DCHECK_LE(module->dfs_ancestor_index(), module->dfs_index());
  if (module->dfs_ancestor_index() == module->dfs_index()) {
    // This is the root of its strongly connected component.
    Handle<SourceTextModule> cycle_root = module;
    Handle<SourceTextModule> ancestor;
    do {
      ancestor = stack->front();
      stack->pop_front();
      DCHECK_EQ(ancestor->status(),
                new_status == kLinked ? kLinking : kEvaluating);
      if (new_status == kLinked) {
        if (!SourceTextModule::RunInitializationCode(isolate, ancestor))
          return false;
      } else if (new_status == kEvaluated) {
        DCHECK(IsTheHole(ancestor->cycle_root(), isolate));
        ancestor->set_cycle_root(*cycle_root);
      }
      ancestor->SetStatus(new_status);
    } while (*ancestor != *module);
  }
  return true;
}

bool SourceTextModule::FinishInstantiate(
    Isolate* isolate, Handle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index,
    Zone* zone) {
  // Instantiate SharedFunctionInfo and mark module as instantiating for
  // the recursion.
  Handle<SharedFunctionInfo> shared(SharedFunctionInfo::cast(module->code()),
                                    isolate);
  Handle<JSFunction> function =
      Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
          .Build();
  module->set_code(*function);
  module->SetStatus(kLinking);
  module->set_dfs_index(*dfs_index);
  module->set_dfs_ancestor_index(*dfs_index);
  stack->push_front(module);
  (*dfs_index)++;

  // Recurse.
  Handle<FixedArray> requested_modules(module->requested_modules(), isolate);
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Module::cast(requested_modules->get(i)),
                                    isolate);
    if (!Module::FinishInstantiate(isolate, requested_module, stack, dfs_index,
                                   zone)) {
      return false;
    }

    DCHECK_NE(requested_module->status(), kEvaluating);
    DCHECK_GE(requested_module->status(), kLinking);
    SLOW_DCHECK(
        // {requested_module} is instantiating iff it's on the {stack}.
        (requested_module->status() == kLinking) ==
        std::count_if(stack->begin(), stack->end(), [&](Handle<Module> m) {
          return *m == *requested_module;
        }));

    if (requested_module->status() == kLinking) {
      // SyntheticModules go straight to kLinked so this must be a
      // SourceTextModule
      module->set_dfs_ancestor_index(std::min(
          module->dfs_ancestor_index(),
          SourceTextModule::cast(*requested_module)->dfs_ancestor_index()));
    }
  }

  Handle<Script> script(module->GetScript(), isolate);
  Handle<SourceTextModuleInfo> module_info(module->info(), isolate);

  // Resolve imports.
  Handle<FixedArray> regular_imports(module_info->regular_imports(), isolate);
  for (int i = 0, n = regular_imports->length(); i < n; ++i) {
    Handle<SourceTextModuleInfoEntry> entry(
        SourceTextModuleInfoEntry::cast(regular_imports->get(i)), isolate);
    Handle<String> name(String::cast(entry->import_name()), isolate);
    MessageLocation loc(script, entry->beg_pos(), entry->end_pos());
    ResolveSet resolve_set(zone);
    Handle<Cell> cell;
    if (!ResolveImport(isolate, module, name, entry->module_request(), loc,
                       true, &resolve_set)
             .ToHandle(&cell)) {
      return false;
    }
    module->regular_imports()->set(ImportIndex(entry->cell_index()), *cell);
  }

  // Resolve indirect exports.
  Handle<FixedArray> special_exports(module_info->special_exports(), isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    Handle<SourceTextModuleInfoEntry> entry(
        SourceTextModuleInfoEntry::cast(special_exports->get(i)), isolate);
    Handle<Object> name(entry->export_name(), isolate);
    if (IsUndefined(*name, isolate)) continue;  // Star export.
    MessageLocation loc(script, entry->beg_pos(), entry->end_pos());
    ResolveSet resolve_set(zone);
    if (ResolveExport(isolate, module, Handle<String>(),
                      Handle<String>::cast(name), loc, true, &resolve_set)
            .is_null()) {
      return false;
    }
  }

  return MaybeTransitionComponent(isolate, module, stack, kLinked);
}

void SourceTextModule::FetchStarExports(Isolate* isolate,
                                        Handle<SourceTextModule> module,
                                        Zone* zone,
                                        UnorderedModuleSet* visited) {
  DCHECK_GE(module->status(), Module::kLinking);

  if (IsJSModuleNamespace(module->module_namespace())) return;  // Shortcut.

  bool cycle = !visited->insert(module).second;
  if (cycle) return;
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  UnorderedStringMap more_exports(zone);

  // TODO(neis): Only allocate more_exports if there are star exports.
  // Maybe split special_exports into indirect_exports and star_exports.

  ReadOnlyRoots roots(isolate);
  Handle<FixedArray> special_exports(module->info()->special_exports(),
                                     isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    Handle<SourceTextModuleInfoEntry> entry(
        SourceTextModuleInfoEntry::cast(special_exports->get(i)), isolate);
    if (!IsUndefined(entry->export_name(), roots)) {
      continue;  // Indirect export.
    }

    Handle<Module> requested_module(
        Module::cast(module->requested_modules()->get(entry->module_request())),
        isolate);

    // Recurse.
    if (IsSourceTextModule(*requested_module))
      FetchStarExports(isolate,
                       Handle<SourceTextModule>::cast(requested_module), zone,
                       visited);

    // Collect all of [requested_module]'s exports that must be added to
    // [module]'s exports (i.e. to [exports]).  We record these in
    // [more_exports].  Ambiguities (conflicting exports) are marked by mapping
    // the name to undefined instead of a Cell.
    Handle<ObjectHashTable> requested_exports(requested_module->exports(),
                                              isolate);
    for (InternalIndex index : requested_exports->IterateEntries()) {
      Tagged<Object> key;
      if (!requested_exports->ToKey(roots, index, &key)) continue;
      Handle<String> name(String::cast(key), isolate);

      if (name->Equals(roots.default_string())) continue;
      if (!IsTheHole(exports->Lookup(name), roots)) continue;

      Handle<Cell> cell(Cell::cast(requested_exports->ValueAt(index)), isolate);
      auto insert_result = more_exports.insert(std::make_pair(name, cell));
      if (!insert_result.second) {
        auto it = insert_result.first;
        if (*it->second == *cell || IsUndefined(*it->second, roots)) {
          // We already recorded this mapping before, or the name is already
          // known to be ambiguous.  In either case, there's nothing to do.
        } else {
          DCHECK(IsCell(*it->second));
          // Different star exports provide different cells for this name, hence
          // mark the name as ambiguous.
          it->second = roots.undefined_value_handle();
        }
      }
    }
  }

  // Copy [more_exports] into [exports].
  for (const auto& elem : more_exports) {
    if (IsUndefined(*elem.second, isolate)) continue;  // Ambiguous export.
    DCHECK(!elem.first->Equals(ReadOnlyRoots(isolate).default_string()));
    DCHECK(IsCell(*elem.second));
    exports = ObjectHashTable::Put(exports, elem.first, elem.second);
  }
  module->set_exports(*exports);
}

void SourceTextModule::GatherAsyncParentCompletions(
    Isolate* isolate, Zone* zone, Handle<SourceTextModule> start,
    AsyncParentCompletionSet* exec_list) {
  // The spec algorithm is recursive. It is transformed to an equivalent
  // iterative one here.
  ZoneStack<Handle<SourceTextModule>> worklist(zone);
  worklist.push(start);

  while (!worklist.empty()) {
    Handle<SourceTextModule> module = worklist.top();
    worklist.pop();

    // 1. Assert: module.[[Status]] is evaluated.
    DCHECK_EQ(module->status(), kEvaluated);

    // 2. For each Module m of module.[[AsyncParentModules]], do
    for (int i = module->AsyncParentModuleCount(); i-- > 0;) {
      Handle<SourceTextModule> m = module->GetAsyncParentModule(isolate, i);

      // a. If execList does not contain m and
      //    m.[[CycleRoot]].[[EvaluationError]] is empty, then
      if (exec_list->find(m) == exec_list->end() &&
          m->GetCycleRoot(isolate)->status() != kErrored) {
        // i. Assert: m.[[EvaluationError]] is empty.
        DCHECK_NE(m->status(), kErrored);

        // ii. Assert: m.[[AsyncEvaluating]] is true.
        DCHECK(m->IsAsyncEvaluating());

        // iii. Assert: m.[[PendingAsyncDependencies]] > 0.
        DCHECK(m->HasPendingAsyncDependencies());

        // iv. Set m.[[PendingAsyncDependencies]] to
        //     m.[[PendingAsyncDependencies]] - 1.
        m->DecrementPendingAsyncDependencies();

        // v. If m.[[PendingAsyncDependencies]] is equal to 0, then
        if (!m->HasPendingAsyncDependencies()) {
          // 1. Append m to execList.
          exec_list->insert(m);

          // 2. If m.[[Async]] is false,
          //    perform ! GatherAsyncParentCompletions(m, execList).
          if (!m->async()) worklist.push(m);
        }
      }
    }
  }

  // 3. Return undefined.
}

Handle<JSModuleNamespace> SourceTextModule::GetModuleNamespace(
    Isolate* isolate, Handle<SourceTextModule> module, int module_request) {
  Handle<Module> requested_module(
      Module::cast(module->requested_modules()->get(module_request)), isolate);
  return Module::GetModuleNamespace(isolate, requested_module);
}

MaybeHandle<JSObject> SourceTextModule::GetImportMeta(
    Isolate* isolate, Handle<SourceTextModule> module) {
  Handle<HeapObject> import_meta(module->import_meta(kAcquireLoad), isolate);
  if (IsTheHole(*import_meta, isolate)) {
    if (!isolate->RunHostInitializeImportMetaObjectCallback(module).ToHandle(
            &import_meta)) {
      return {};
    }
    module->set_import_meta(*import_meta, kReleaseStore);
  }
  return Handle<JSObject>::cast(import_meta);
}

bool SourceTextModule::MaybeHandleEvaluationException(
    Isolate* isolate, ZoneForwardList<Handle<SourceTextModule>>* stack) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> exception = isolate->exception();
  if (isolate->is_catchable_by_javascript(exception)) {
    //  a. For each Cyclic Module Record m in stack, do
    for (Handle<SourceTextModule>& descendant : *stack) {
      //   i. Assert: m.[[Status]] is "evaluating".
      CHECK_EQ(descendant->status(), kEvaluating);
      //  ii. Set m.[[Status]] to "evaluated".
      // iii. Set m.[[EvaluationError]] to result.
      descendant->RecordError(isolate, exception);
    }
    return true;
  }
  // If the exception was a termination exception, rejecting the promise
  // would resume execution, and our API contract is to return an empty
  // handle. The module's status should be set to kErrored and the
  // exception field should be set to `null`.
  RecordError(isolate, exception);
  for (Handle<SourceTextModule>& descendant : *stack) {
    descendant->RecordError(isolate, exception);
  }
  CHECK_EQ(status(), kErrored);
  CHECK_EQ(this->exception(), *isolate->factory()->null_value());
  return false;
}

MaybeHandle<Object> SourceTextModule::Evaluate(
    Isolate* isolate, Handle<SourceTextModule> module) {
  CHECK(module->status() == kLinked || module->status() == kEvaluated);

  // 5. Let stack be a new empty List.
  Zone zone(isolate->allocator(), ZONE_NAME);
  ZoneForwardList<Handle<SourceTextModule>> stack(&zone);
  unsigned dfs_index = 0;

  // 6. Let capability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> capability = isolate->factory()->NewJSPromise();

  // 7. Set module.[[TopLevelCapability]] to capability.
  module->set_top_level_capability(*capability);
  DCHECK(IsJSPromise(module->top_level_capability()));

  // 8. Let result be InnerModuleEvaluation(module, stack, 0).
  // 9. If result is an abrupt completion, then
  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  try_catch.SetVerbose(false);
  try_catch.SetCaptureMessage(false);
  // TODO(verwaest): Return a bool from InnerModuleEvaluation instead?
  if (InnerModuleEvaluation(isolate, module, &stack, &dfs_index).is_null()) {
    if (!module->MaybeHandleEvaluationException(isolate, &stack)) return {};
    CHECK(try_catch.HasCaught());
    //  d. Perform ! Call(capability.[[Reject]], undefined,
    //                    «result.[[Value]]»).
    JSPromise::Reject(capability, handle(module->exception(), isolate));
  } else {
    // 10. Otherwise,
    //  a. Assert: module.[[Status]] is "evaluated"...
    CHECK_EQ(module->status(), kEvaluated);

    //  b. If module.[[AsyncEvaluating]] is false, then
    if (!module->IsAsyncEvaluating()) {
      //   i. Perform ! Call(capability.[[Resolve]], undefined,
      //                     «undefined»).
      JSPromise::Resolve(capability, isolate->factory()->undefined_value())
          .ToHandleChecked();
    }

    //  c. Assert: stack is empty.
    DCHECK(stack.empty());
  }

  // 11. Return capability.[[Promise]].
  return capability;
}

Maybe<bool> SourceTextModule::AsyncModuleExecutionFulfilled(
    Isolate* isolate, Handle<SourceTextModule> module) {
  // 1. If module.[[Status]] is evaluated, then
  if (module->status() == kErrored) {
    // a. Assert: module.[[EvaluationError]] is not empty.
    DCHECK(!IsTheHole(module->exception(), isolate));
    // b. Return.
    return Just(true);
  }
  // 3. Assert: module.[[AsyncEvaluating]] is true.
  DCHECK(module->IsAsyncEvaluating());
  // 4. Assert: module.[[EvaluationError]] is empty.
  CHECK_EQ(module->status(), kEvaluated);
  // 5. Set module.[[AsyncEvaluating]] to false.
  isolate->DidFinishModuleAsyncEvaluation(module->async_evaluating_ordinal());
  module->set_async_evaluating_ordinal(kAsyncEvaluateDidFinish);
  // TODO(cbruni): update to match spec.
  // 7. If module.[[TopLevelCapability]] is not empty, then
  if (!IsUndefined(module->top_level_capability(), isolate)) {
    //  a. Assert: module.[[CycleRoot]] is equal to module.
    DCHECK_EQ(*module->GetCycleRoot(isolate), *module);
    //   i. Perform ! Call(module.[[TopLevelCapability]].[[Resolve]], undefined,
    //                     «undefined»).
    Handle<JSPromise> capability(
        JSPromise::cast(module->top_level_capability()), isolate);
    JSPromise::Resolve(capability, isolate->factory()->undefined_value())
        .ToHandleChecked();
  }

  // 8. Let execList be a new empty List.
  Zone zone(isolate->allocator(), ZONE_NAME);
  AsyncParentCompletionSet exec_list(&zone);

  // 9. Perform ! GatherAsyncParentCompletions(module, execList).
  GatherAsyncParentCompletions(isolate, &zone, module, &exec_list);

  // 10. Let sortedExecList be a List of elements that are the elements of
  //    execList, in the order in which they had their [[AsyncEvaluating]]
  //    fields set to true in InnerModuleEvaluation.
  //
  // This step is implemented by AsyncParentCompletionSet, which is a set
  // ordered on async_evaluating_ordinal.

  // 11. Assert: All elements of sortedExecList have their [[AsyncEvaluating]]
  //    field set to true, [[PendingAsyncDependencies]] field set to 0 and
  //    [[EvaluationError]] field set to undefined.
#ifdef DEBUG
  for (Handle<SourceTextModule> m : exec_list) {
    DCHECK(m->IsAsyncEvaluating());
    DCHECK(!m->HasPendingAsyncDependencies());
    DCHECK_NE(m->status(), kErrored);
  }
#endif

  // 12. For each Module m of sortedExecList, do
  for (Handle<SourceTextModule> m : exec_list) {
    //  i. If m.[[AsyncEvaluating]] is false, then
    if (!m->IsAsyncEvaluating()) {
      //   a. Assert: m.[[EvaluatingError]] is not empty.
      DCHECK_EQ(m->status(), kErrored);
    } else if (m->async()) {
      //  ii. Otherwise, if m.[[Async]] is *true*, then
      //   a. Perform ! ExecuteAsyncModule(m).
      // The execution may have been terminated and can not be resumed, so just
      // raise the exception.
      MAYBE_RETURN(ExecuteAsyncModule(isolate, m), Nothing<bool>());
    } else {
      //  iii. Otherwise,
      //   a. Let _result_ be m.ExecuteModule().
      Handle<Object> unused_result;
      //   b. If _result_ is an abrupt completion,
      MaybeHandle<Object> exception;
      if (!ExecuteModule(isolate, m, &exception).ToHandle(&unused_result)) {
        //    1. Perform ! AsyncModuleExecutionRejected(m, result.[[Value]]).
        AsyncModuleExecutionRejected(isolate, m, exception.ToHandleChecked());
      } else {
        //   c. Otherwise,
        //    1. Set m.[[AsyncEvaluating]] to false.
        isolate->DidFinishModuleAsyncEvaluation(m->async_evaluating_ordinal());
        m->set_async_evaluating_ordinal(kAsyncEvaluateDidFinish);

        //    2. If m.[[TopLevelCapability]] is not empty, then
        if (!IsUndefined(m->top_level_capability(), isolate)) {
          //  i. Assert: m.[[CycleRoot]] is equal to m.
          DCHECK_EQ(*m->GetCycleRoot(isolate), *m);

          //  ii. Perform ! Call(m.[[TopLevelCapability]].[[Resolve]],
          //                     undefined, «undefined»).
          Handle<JSPromise> capability(
              JSPromise::cast(m->top_level_capability()), isolate);
          JSPromise::Resolve(capability, isolate->factory()->undefined_value())
              .ToHandleChecked();
        }
      }
    }
  }

  // 10. Return undefined.
  return Just(true);
}

void SourceTextModule::AsyncModuleExecutionRejected(
    Isolate* isolate, Handle<SourceTextModule> module,
    Handle<Object> exception) {
  // 1. If module.[[Status]] is evaluated, then
  if (module->status() == kErrored) {
    // a. Assert: module.[[EvaluationError]] is not empty.
    DCHECK(!IsTheHole(module->exception(), isolate));
    // b. Return.
    return;
  }

  // TODO(cbruni): update to match spec.
  DCHECK(isolate->is_catchable_by_javascript(*exception));
  // 1. Assert: module.[[Status]] is "evaluated".
  CHECK(module->status() == kEvaluated || module->status() == kErrored);
  // 2. If module.[[AsyncEvaluating]] is false,
  if (!module->IsAsyncEvaluating()) {
    //  a. Assert: module.[[EvaluationError]] is not empty.
    CHECK_EQ(module->status(), kErrored);
    //  b. Return undefined.
    return;
  }

  // 5. Set module.[[EvaluationError]] to ThrowCompletion(error).
  module->RecordError(isolate, *exception);

  // 6. Set module.[[AsyncEvaluating]] to false.
  isolate->DidFinishModuleAsyncEvaluation(module->async_evaluating_ordinal());
  module->set_async_evaluating_ordinal(kAsyncEvaluateDidFinish);

  // 7. For each Module m of module.[[AsyncParentModules]], do
  for (int i = 0; i < module->AsyncParentModuleCount(); i++) {
    Handle<SourceTextModule> m = module->GetAsyncParentModule(isolate, i);
    // TODO(cbruni): update to match spec.
    //  a. If module.[[DFSIndex]] is not equal to module.[[DFSAncestorIndex]],
    //     then
    if (module->dfs_index() != module->dfs_ancestor_index()) {
      //   i. Assert: m.[[DFSAncestorIndex]] is equal to
      //      module.[[DFSAncestorIndex]].
      DCHECK_EQ(m->dfs_ancestor_index(), module->dfs_ancestor_index());
    }
    //  b. Perform ! AsyncModuleExecutionRejected(m, error).
    AsyncModuleExecutionRejected(isolate, m, exception);
  }

  // 8. If module.[[TopLevelCapability]] is not empty, then
  if (!IsUndefined(module->top_level_capability(), isolate)) {
    //  a. Assert: module.[[CycleRoot]] is equal to module.
    DCHECK_EQ(*module->GetCycleRoot(isolate), *module);
    //  b. Perform ! Call(module.[[TopLevelCapability]].[[Reject]],
    //                    undefined, «error»).
    Handle<JSPromise> capability(
        JSPromise::cast(module->top_level_capability()), isolate);
    JSPromise::Reject(capability, exception);
  }
}

// static
Maybe<bool> SourceTextModule::ExecuteAsyncModule(
    Isolate* isolate, Handle<SourceTextModule> module) {
  // 1. Assert: module.[[Status]] is "evaluating" or "evaluated".
  CHECK(module->status() == kEvaluating || module->status() == kEvaluated);

  // 2. Assert: module.[[Async]] is true.
  DCHECK(module->async());

  // 3. Set module.[[AsyncEvaluating]] to true.
  module->set_async_evaluating_ordinal(
      isolate->NextModuleAsyncEvaluatingOrdinal());

  // 4. Let capability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> capability = isolate->factory()->NewJSPromise();

  Handle<Context> execute_async_module_context =
      isolate->factory()->NewBuiltinContext(
          isolate->native_context(),
          ExecuteAsyncModuleContextSlots::kContextLength);
  execute_async_module_context->set(ExecuteAsyncModuleContextSlots::kModule,
                                    *module);

  // 5. Let stepsFulfilled be the steps of a CallAsyncModuleFulfilled
  // 6. Let onFulfilled be CreateBuiltinFunction(stepsFulfilled,
  //                                             «[[Module]]»).
  // 7. Set onFulfilled.[[Module]] to module.
  Handle<JSFunction> on_fulfilled =
      Factory::JSFunctionBuilder{
          isolate,
          isolate->factory()
              ->source_text_module_execute_async_module_fulfilled_sfi(),
          execute_async_module_context}
          .Build();

  // 8. Let stepsRejected be the steps of a CallAsyncModuleRejected.
  // 9. Let onRejected be CreateBuiltinFunction(stepsRejected, «[[Module]]»).
  // 10. Set onRejected.[[Module]] to module.
  Handle<JSFunction> on_rejected =
      Factory::JSFunctionBuilder{
          isolate,
          isolate->factory()
              ->source_text_module_execute_async_module_rejected_sfi(),
          execute_async_module_context}
          .Build();

  // 11. Perform ! PerformPromiseThen(capability.[[Promise]],
  //                                  onFulfilled, onRejected).
  Handle<Object> argv[] = {on_fulfilled, on_rejected};
  Execution::CallBuiltin(isolate, isolate->promise_then(), capability,
                         arraysize(argv), argv)
      .ToHandleChecked();

  // 12. Perform ! module.ExecuteModule(capability).
  // Note: In V8 we have broken module.ExecuteModule into
  // ExecuteModule for synchronous module execution and
  // InnerExecuteAsyncModule for asynchronous execution.
  MaybeHandle<Object> ret =
      InnerExecuteAsyncModule(isolate, module, capability);
  if (ret.is_null()) {
    // The evaluation of async module can not throwing a JavaScript observable
    // exception.
    DCHECK_IMPLIES(v8_flags.strict_termination_checks,
                   isolate->is_execution_terminating());
    return Nothing<bool>();
  }

  // 13. Return.
  return Just<bool>(true);
}

MaybeHandle<Object> SourceTextModule::InnerExecuteAsyncModule(
    Isolate* isolate, Handle<SourceTextModule> module,
    Handle<JSPromise> capability) {
  // If we have an async module, then it has an associated
  // JSAsyncFunctionObject, which we then evaluate with the passed in promise
  // capability.
  Handle<JSAsyncFunctionObject> async_function_object(
      JSAsyncFunctionObject::cast(module->code()), isolate);
  async_function_object->set_promise(*capability);
  Handle<JSFunction> resume(
      isolate->native_context()->async_module_evaluate_internal(), isolate);
  Handle<Object> result;
  return Execution::TryCall(isolate, resume, async_function_object, 0, nullptr,
                            Execution::MessageHandling::kKeepPending, nullptr);
}

MaybeHandle<Object> SourceTextModule::ExecuteModule(
    Isolate* isolate, Handle<SourceTextModule> module,
    MaybeHandle<Object>* exception_out) {
  // Synchronous modules have an associated JSGeneratorObject.
  Handle<JSGeneratorObject> generator(JSGeneratorObject::cast(module->code()),
                                      isolate);
  Handle<JSFunction> resume(
      isolate->native_context()->generator_next_internal(), isolate);
  Handle<Object> result;

  if (!Execution::TryCall(isolate, resume, generator, 0, nullptr,
                          Execution::MessageHandling::kKeepPending,
                          exception_out)
           .ToHandle(&result)) {
    return {};
  }
  DCHECK(
      Object::BooleanValue(JSIteratorResult::cast(*result)->done(), isolate));
  return handle(JSIteratorResult::cast(*result)->value(), isolate);
}

MaybeHandle<Object> SourceTextModule::InnerModuleEvaluation(
    Isolate* isolate, Handle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index) {
  STACK_CHECK(isolate, MaybeHandle<Object>());
  int module_status = module->status();
  // InnerModuleEvaluation(module, stack, index)
  // 2. If module.[[Status]] is "evaluated", then
  //    a. If module.[[EvaluationError]] is undefined, return index.
  //       (We return undefined instead)
  if (module_status == kEvaluated || module_status == kEvaluating) {
    return isolate->factory()->undefined_value();
  }

  //    b. Otherwise return module.[[EvaluationError]].
  //       (We throw on isolate and return a MaybeHandle<Object>
  //        instead)
  if (module_status == kErrored) {
    isolate->Throw(module->exception());
    return MaybeHandle<Object>();
  }

  // 4. Assert: module.[[Status]] is "linked".
  CHECK_EQ(module_status, kLinked);

  Handle<FixedArray> requested_modules;

  {
    DisallowGarbageCollection no_gc;
    Tagged<SourceTextModule> raw_module = *module;
    // 5. Set module.[[Status]] to "evaluating".
    raw_module->SetStatus(kEvaluating);

    // 6. Set module.[[DFSIndex]] to index.
    raw_module->set_dfs_index(*dfs_index);

    // 7. Set module.[[DFSAncestorIndex]] to index.
    raw_module->set_dfs_ancestor_index(*dfs_index);

    // 8. Set module.[[PendingAsyncDependencies]] to 0.
    DCHECK(!raw_module->HasPendingAsyncDependencies());

    // 9. Set module.[[AsyncParentModules]] to a new empty List.
    raw_module->set_async_parent_modules(
        ReadOnlyRoots(isolate).empty_array_list());

    // 10. Set index to index + 1.
    (*dfs_index)++;

    // 11. Append module to stack.
    stack->push_front(module);

    // Recursion.
    requested_modules = handle(raw_module->requested_modules(), isolate);
  }

  // 12. For each String required that is an element of
  //     module.[[RequestedModules]], do
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Module::cast(requested_modules->get(i)),
                                    isolate);
    //   d. If requiredModule is a Cyclic Module Record, then
    if (IsSourceTextModule(*requested_module)) {
      Handle<SourceTextModule> required_module(
          SourceTextModule::cast(*requested_module), isolate);
      RETURN_ON_EXCEPTION(
          isolate,
          InnerModuleEvaluation(isolate, required_module, stack, dfs_index),
          Object);
      int required_module_status = required_module->status();

      //    i. Assert: requiredModule.[[Status]] is either "evaluating" or
      //       "evaluated".
      //       (We also assert the module cannot be errored, because if it was
      //        we would have already returned from InnerModuleEvaluation)
      CHECK_GE(required_module_status, kEvaluating);
      CHECK_NE(required_module_status, kErrored);

      //   ii.  Assert: requiredModule.[[Status]] is "evaluating" if and
      //        only if requiredModule is in stack.
      SLOW_DCHECK(
          (requested_module->status() == kEvaluating) ==
          std::count_if(stack->begin(), stack->end(), [&](Handle<Module> m) {
            return *m == *requested_module;
          }));

      //  iii.  If requiredModule.[[Status]] is "evaluating", then
      if (required_module_status == kEvaluating) {
        //      1. Set module.[[DFSAncestorIndex]] to
        //         min(
        //           module.[[DFSAncestorIndex]],
        //           requiredModule.[[DFSAncestorIndex]]).
        module->set_dfs_ancestor_index(
            std::min(module->dfs_ancestor_index(),
                     required_module->dfs_ancestor_index()));
      } else {
        //   iv. Otherwise,
        //      1. Set requiredModule to requiredModule.[[CycleRoot]].
        required_module = required_module->GetCycleRoot(isolate);
        required_module_status = required_module->status();

        //      2. Assert: requiredModule.[[Status]] is "evaluated".
        CHECK_GE(required_module_status, kEvaluated);

        //      3. If requiredModule.[[EvaluationError]] is not undefined,
        //         return module.[[EvaluationError]].
        //         (If there was an exception on the original required module
        //          we would have already returned. This check handles the case
        //          where the AsyncCycleRoot has an error. Instead of returning
        //          the exception, we throw on isolate and return a
        //          MaybeHandle<Object>)
        if (required_module_status == kErrored) {
          isolate->Throw(required_module->exception());
          return MaybeHandle<Object>();
        }
      }
      //     v. If requiredModule.[[AsyncEvaluating]] is true, then
      if (required_module->IsAsyncEvaluating()) {
        //      1. Set module.[[PendingAsyncDependencies]] to
        //         module.[[PendingAsyncDependencies]] + 1.
        module->IncrementPendingAsyncDependencies();

        //      2. Append module to requiredModule.[[AsyncParentModules]].
        AddAsyncParentModule(isolate, required_module, module);
      }
    } else {
      RETURN_ON_EXCEPTION(isolate, Module::Evaluate(isolate, requested_module),
                          Object);
    }
  }

  // The spec returns the module index for proper numbering of dependencies.
  // However, we pass the module index by pointer instead.
  //
  // Before async modules v8 returned the value result from calling next
  // on the module's implicit iterator. We preserve this behavior for
  // synchronous modules, but return undefined for AsyncModules.
  Handle<Object> result = isolate->factory()->undefined_value();

  // 14. If module.[[PendingAsyncDependencies]] > 0 or module.[[Async]] is
  //     true, then
  if (module->HasPendingAsyncDependencies() || module->async()) {
    // a. Assert: module.[[AsyncEvaluating]] is false and was never previously
    //     set to true.
    DCHECK_EQ(module->async_evaluating_ordinal(), kNotAsyncEvaluated);

    // b. Set module.[[AsyncEvaluating]] to true.
    // NOTE: The order in which modules transition to async evaluating is
    // significant.
    module->set_async_evaluating_ordinal(
        isolate->NextModuleAsyncEvaluatingOrdinal());

    // c. If module.[[PendingAsyncDependencies]] is 0,
    //    perform ! ExecuteAsyncModule(_module_).
    // The execution may have been terminated and can not be resumed, so just
    // raise the exception.
    if (!module->HasPendingAsyncDependencies()) {
      MAYBE_RETURN(SourceTextModule::ExecuteAsyncModule(isolate, module),
                   MaybeHandle<Object>());
    }
  } else {
    // 15. Otherwise, perform ? module.ExecuteModule().
    MaybeHandle<Object> exception;
    Handle<Object> result;
    if (!ExecuteModule(isolate, module, &exception).ToHandle(&result)) {
      if (!isolate->is_execution_terminating()) {
        isolate->Throw(*exception.ToHandleChecked());
      }
      return result;
    }
  }

  CHECK(MaybeTransitionComponent(isolate, module, stack, kEvaluated));
  return result;
}

void SourceTextModule::Reset(Isolate* isolate,
                             Handle<SourceTextModule> module) {
  Factory* factory = isolate->factory();

  DCHECK(IsTheHole(module->import_meta(kAcquireLoad), isolate));

  Handle<FixedArray> regular_exports =
      factory->NewFixedArray(module->regular_exports()->length());
  Handle<FixedArray> regular_imports =
      factory->NewFixedArray(module->regular_imports()->length());
  Handle<FixedArray> requested_modules =
      factory->NewFixedArray(module->requested_modules()->length());

  DisallowGarbageCollection no_gc;
  Tagged<SourceTextModule> raw_module = *module;
  if (raw_module->status() == kLinking) {
    raw_module->set_code(JSFunction::cast(raw_module->code())->shared());
  }
  raw_module->set_regular_exports(*regular_exports);
  raw_module->set_regular_imports(*regular_imports);
  raw_module->set_requested_modules(*requested_modules);
  raw_module->set_dfs_index(-1);
  raw_module->set_dfs_ancestor_index(-1);
}

std::vector<std::tuple<Handle<SourceTextModule>, Handle<JSMessageObject>>>
SourceTextModule::GetStalledTopLevelAwaitMessages(Isolate* isolate) {
  Zone zone(isolate->allocator(), ZONE_NAME);
  UnorderedModuleSet visited(&zone);
  std::vector<std::tuple<Handle<SourceTextModule>, Handle<JSMessageObject>>>
      result;
  std::vector<Handle<SourceTextModule>> stalled_modules;
  InnerGetStalledTopLevelAwaitModule(isolate, &visited, &stalled_modules);
  size_t stalled_modules_size = stalled_modules.size();
  if (stalled_modules_size == 0) return result;

  result.reserve(stalled_modules_size);
  for (size_t i = 0; i < stalled_modules_size; ++i) {
    Handle<SourceTextModule> found = stalled_modules[i];
    CHECK(IsJSGeneratorObject(found->code()));
    Handle<JSGeneratorObject> code(JSGeneratorObject::cast(found->code()),
                                   isolate);
    Handle<SharedFunctionInfo> shared(found->GetSharedFunctionInfo(), isolate);
    Handle<Object> script(shared->script(), isolate);
    MessageLocation location = MessageLocation(Handle<Script>::cast(script),
                                               shared, code->code_offset());
    Handle<JSMessageObject> message = MessageHandler::MakeMessageObject(
        isolate, MessageTemplate::kTopLevelAwaitStalled, &location,
        isolate->factory()->null_value(), Handle<FixedArray>());
    result.push_back(std::make_tuple(found, message));
  }
  return result;
}

void SourceTextModule::InnerGetStalledTopLevelAwaitModule(
    Isolate* isolate, UnorderedModuleSet* visited,
    std::vector<Handle<SourceTextModule>>* result) {
  DisallowGarbageCollection no_gc;
  // If it's a module that is waiting for no other modules but itself,
  // it's what we are looking for. Add it to the results.
  if (!HasPendingAsyncDependencies() && IsAsyncEvaluating()) {
    result->push_back(handle(*this, isolate));
    return;
  }
  // The module isn't what we are looking for, continue looking in the graph.
  Tagged<FixedArray> requested = requested_modules();
  int length = requested->length();
  for (int i = 0; i < length; ++i) {
    Tagged<Module> requested_module = Module::cast(requested->get(i));
    if (IsSourceTextModule(requested_module) &&
        visited->insert(handle(requested_module, isolate)).second) {
      Tagged<SourceTextModule> source_text_module =
          SourceTextModule::cast(requested_module);
      source_text_module->InnerGetStalledTopLevelAwaitModule(isolate, visited,
                                                             result);
    }
  }
}

}  // namespace internal
}  // namespace v8
