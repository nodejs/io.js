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
  V8_INLINE size_t operator()(DirectHandle<String> string) const {
    return string->EnsureHash();
  }
};

struct StringHandleEqual {
  V8_INLINE bool operator()(DirectHandle<String> lhs,
                            DirectHandle<String> rhs) const {
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

struct SourceTextModule::AsyncEvaluationOrdinalCompare {
  bool operator()(DirectHandle<SourceTextModule> lhs,
                  DirectHandle<SourceTextModule> rhs) const {
    DCHECK(lhs->HasAsyncEvaluationOrdinal());
    DCHECK(rhs->HasAsyncEvaluationOrdinal());
    return lhs->async_evaluation_ordinal() < rhs->async_evaluation_ordinal();
  }
};

Tagged<SharedFunctionInfo> SourceTextModule::GetSharedFunctionInfo() const {
  DisallowGarbageCollection no_gc;
  switch (status()) {
    case kUnlinked:
    case kPreLinking:
      return Cast<SharedFunctionInfo>(code());
    case kLinking:
      return Cast<JSFunction>(code())->shared();
    case kLinked:
    case kEvaluating:
    case kEvaluatingAsync:
    case kEvaluated:
      return Cast<JSGeneratorObject>(code())->function()->shared();
    case kErrored:
      return Cast<SharedFunctionInfo>(code());
  }
  UNREACHABLE();
}

Tagged<Script> SourceTextModule::GetScript() const {
  DisallowGarbageCollection no_gc;
  return Cast<Script>(GetSharedFunctionInfo()->script());
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
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    Handle<String> name, Handle<SourceTextModuleInfoEntry> entry) {
  Handle<ObjectHashTable> exports(module->exports(), isolate);
  DCHECK(IsTheHole(exports->Lookup(name), isolate));
  exports = ObjectHashTable::Put(exports, name, entry);
  module->set_exports(*exports);
}

void SourceTextModule::CreateExport(Isolate* isolate,
                                    DirectHandle<SourceTextModule> module,
                                    int cell_index,
                                    DirectHandle<FixedArray> names) {
  DCHECK_LT(0, names->length());
  Handle<Cell> cell = isolate->factory()->NewCell();
  module->regular_exports()->set(ExportIndex(cell_index), *cell);

  Handle<ObjectHashTable> exports(module->exports(), isolate);
  for (int i = 0, n = names->length(); i < n; ++i) {
    Handle<String> name(Cast<String>(names->get(i)), isolate);
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
  return Cast<Cell>(cell);
}

Handle<Object> SourceTextModule::LoadVariable(
    Isolate* isolate, DirectHandle<SourceTextModule> module, int cell_index) {
  return handle(module->GetCell(cell_index)->value(), isolate);
}

void SourceTextModule::StoreVariable(DirectHandle<SourceTextModule> module,
                                     int cell_index,
                                     DirectHandle<Object> value) {
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
    return Cast<Cell>(object);
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
        isolate->ThrowAt(isolate->factory()->NewSyntaxError(
                             MessageTemplate::kCyclicModuleDependency,
                             export_name, module_specifier),
                         &loc);
        return MaybeHandle<Cell>();
      }
      return MaybeHandle<Cell>();
    }
    name_set->insert(export_name);
  }

  if (IsSourceTextModuleInfoEntry(*object)) {
    // Not yet resolved indirect export.
    auto entry = Cast<SourceTextModuleInfoEntry>(object);
    Handle<String> import_name(Cast<String>(entry->import_name()), isolate);
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
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    Handle<String> name, int module_request_index, MessageLocation loc,
    bool must_resolve, Module::ResolveSet* resolve_set) {
  Handle<Module> requested_module(
      Cast<Module>(module->requested_modules()->get(module_request_index)),
      isolate);
  DirectHandle<ModuleRequest> module_request(
      Cast<ModuleRequest>(
          module->info()->module_requests()->get(module_request_index)),
      isolate);
  Handle<String> module_specifier(Cast<String>(module_request->specifier()),
                                  isolate);
  MaybeHandle<Cell> result =
      Module::ResolveExport(isolate, requested_module, module_specifier, name,
                            loc, must_resolve, resolve_set);
  DCHECK_IMPLIES(isolate->has_exception(), result.is_null());
  return result;
}

MaybeHandle<Cell> SourceTextModule::ResolveExportUsingStarExports(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    Handle<String> module_specifier, Handle<String> export_name,
    MessageLocation loc, bool must_resolve, Module::ResolveSet* resolve_set) {
  if (!export_name->Equals(ReadOnlyRoots(isolate).default_string())) {
    // Go through all star exports looking for the given name.  If multiple star
    // exports provide the name, make sure they all map it to the same cell.
    Handle<Cell> unique_cell;
    DirectHandle<FixedArray> special_exports(module->info()->special_exports(),
                                             isolate);
    for (int i = 0, n = special_exports->length(); i < n; ++i) {
      i::DirectHandle<i::SourceTextModuleInfoEntry> entry(
          i::Cast<i::SourceTextModuleInfoEntry>(special_exports->get(i)),
          isolate);
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
          isolate->ThrowAt(isolate->factory()->NewSyntaxError(
                               MessageTemplate::kAmbiguousExport,
                               module_specifier, export_name),
                           &loc);
          return MaybeHandle<Cell>();
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
    isolate->ThrowAt(
        isolate->factory()->NewSyntaxError(MessageTemplate::kUnresolvableExport,
                                           module_specifier, export_name),
        &loc);
    return MaybeHandle<Cell>();
  }
  return MaybeHandle<Cell>();
}

bool SourceTextModule::PrepareInstantiate(
    Isolate* isolate, Handle<SourceTextModule> module,
    v8::Local<v8::Context> context,
    v8::Module::ResolveModuleCallback callback) {
  DCHECK_NE(callback, nullptr);
  // Obtain requested modules.
  DirectHandle<SourceTextModuleInfo> module_info(module->info(), isolate);
  DirectHandle<FixedArray> module_requests(module_info->module_requests(),
                                           isolate);
  DirectHandle<FixedArray> requested_modules(module->requested_modules(),
                                             isolate);
  for (int i = 0, length = module_requests->length(); i < length; ++i) {
    DirectHandle<ModuleRequest> module_request(
        Cast<ModuleRequest>(module_requests->get(i)), isolate);
    Handle<String> specifier(module_request->specifier(), isolate);
    v8::Local<v8::Module> api_requested_module;
    Handle<FixedArray> import_attributes(module_request->import_attributes(),
                                         isolate);
    if (!callback(context, v8::Utils::ToLocal(specifier),
                  v8::Utils::FixedArrayToLocal(import_attributes),
                  v8::Utils::ToLocal(Cast<Module>(module)))
             .ToLocal(&api_requested_module)) {
      return false;
    }
    DirectHandle<Module> requested_module =
        Utils::OpenDirectHandle(*api_requested_module);
    requested_modules->set(i, *requested_module);
  }

  // Recurse.
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Cast<Module>(requested_modules->get(i)),
                                    isolate);
    if (!Module::PrepareInstantiate(isolate, requested_module, context,
                                    callback)) {
      return false;
    }
  }

  // Set up local exports.
  // TODO(neis): Create regular_exports array here instead of in factory method?
  for (int i = 0, n = module_info->RegularExportCount(); i < n; ++i) {
    int cell_index = module_info->RegularExportCellIndex(i);
    DirectHandle<FixedArray> export_names(
        module_info->RegularExportExportNames(i), isolate);
    CreateExport(isolate, module, cell_index, export_names);
  }

  // Partially set up indirect exports.
  // For each indirect export, we create the appropriate slot in the export
  // table and store its SourceTextModuleInfoEntry there.  When we later find
  // the correct Cell in the module that actually provides the value, we replace
  // the SourceTextModuleInfoEntry by that Cell (see ResolveExport).
  DirectHandle<FixedArray> special_exports(module_info->special_exports(),
                                           isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    Handle<SourceTextModuleInfoEntry> entry(
        Cast<SourceTextModuleInfoEntry>(special_exports->get(i)), isolate);
    Handle<Object> export_name(entry->export_name(), isolate);
    if (IsUndefined(*export_name, isolate)) continue;  // Star export.
    CreateIndirectExport(isolate, module, Cast<String>(export_name), entry);
  }

  DCHECK_EQ(module->status(), kPreLinking);
  return true;
}

bool SourceTextModule::RunInitializationCode(
    Isolate* isolate, DirectHandle<SourceTextModule> module) {
  DCHECK_EQ(module->status(), kLinking);
  Handle<JSFunction> function(Cast<JSFunction>(module->code()), isolate);
  DCHECK_EQ(MODULE_SCOPE, function->shared()->scope_info()->scope_type());
  Handle<Object> receiver = isolate->factory()->undefined_value();

  DirectHandle<ScopeInfo> scope_info(function->shared()->scope_info(), isolate);
  DirectHandle<Context> context = isolate->factory()->NewModuleContext(
      module, isolate->native_context(), scope_info);
  function->set_context(*context);

  MaybeHandle<Object> maybe_generator =
      Execution::Call(isolate, function, receiver, 0, {});
  Handle<Object> generator;
  if (!maybe_generator.ToHandle(&generator)) {
    DCHECK(isolate->has_exception());
    return false;
  }
  DCHECK_EQ(*function, Cast<JSGeneratorObject>(generator)->function());
  module->set_code(Cast<JSGeneratorObject>(*generator));
  return true;
}

// ES#sec-innermoduleevaluation and ES#sec-innermodulelinking
bool SourceTextModule::MaybeTransitionComponent(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, Status new_status) {
  DCHECK(new_status == kLinked || new_status == kEvaluated);

#ifdef DEBUG
  if (v8_flags.trace_module_status) {
    StdoutStream os;
    os << "Transitioning strongly connected module graph component to "
       << Module::StatusString(new_status) << " {\n";
  }
#endif  // DEBUG

  // Below, N/M means step N in InnerModuleEvaluation and step M in
  // InnerModuleLinking.

  // 14/11. Assert: module occurs exactly once in stack.
  SLOW_DCHECK(
      // {module} is on the {stack}.
      std::count_if(stack->begin(), stack->end(), [&](DirectHandle<Module> m) {
        return *m == *module;
      }) == 1);

  // 15/12. Assert: module.[[DFSAncestorIndex]] ≤ module.[[DFSIndex]].
  DCHECK_LE(module->dfs_ancestor_index(), module->dfs_index());

  // 16/13. If module.[[DFSAncestorIndex]] = module.[[DFSIndex]], then
  if (module->dfs_ancestor_index() == module->dfs_index()) {
    // This is the root of its strongly connected component.
    DirectHandle<SourceTextModule> cycle_root = module;
    DirectHandle<SourceTextModule> ancestor;
    // This loop handles the loops in both InnerModuleEvaluation and
    // InnerModuleLinking.
    //
    // InnerModuleEvaluation
    //
    // a. Let done be false.
    // b. Repeat, while done is false,
    //     i. Let requiredModule be the last element of stack.
    //    ii. Remove the last element of stack.
    //   iii. Assert: requiredModule is a Cyclic Module Record.
    //    iv. If requiredModule.[[AsyncEvaluation]] is false, set
    //        requiredModule.[[Status]] to EVALUATED.
    //     v. Otherwise, set requiredModule.[[Status]] to EVALUATING-ASYNC.
    //    vi. If requiredModule and module are the same Module Record, set done
    //        to true.
    //   vii. Set requiredModule.[[CycleRoot]] to module.
    //
    // InnerModuleLinking
    //
    // a. Let done be false.
    // b. Repeat, while done is false,
    //     i. Let requiredModule be the last element of stack.
    //    ii. Remove the last element of stack.
    //   iii. Assert: requiredModule is a Cyclic Module Record.
    //    iv. Set requiredModule.[[Status]] to LINKED.
    //     v. If requiredModule and module are the same Module Record, set done
    //        to true.
    do {
      ancestor = stack->front();
      stack->pop_front();
      DCHECK_EQ(ancestor->status(),
                new_status == kLinked ? kLinking : kEvaluating);
      if (new_status == kLinked) {
        if (!SourceTextModule::RunInitializationCode(isolate, ancestor)) {
          return false;
        }
        ancestor->SetStatus(kLinked);
      } else {
        DCHECK(IsTheHole(ancestor->cycle_root(), isolate));
        ancestor->set_cycle_root(*cycle_root);
        ancestor->SetStatus(ancestor->HasAsyncEvaluationOrdinal()
                                ? kEvaluatingAsync
                                : kEvaluated);
      }
    } while (*ancestor != *module);
  }
#ifdef DEBUG
  if (v8_flags.trace_module_status) {
    StdoutStream os;
    os << "}\n";
  }
#endif  // DEBUG
  return true;
}

bool SourceTextModule::FinishInstantiate(
    Isolate* isolate, Handle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index,
    Zone* zone) {
  // Instantiate SharedFunctionInfo and mark module as instantiating for
  // the recursion.
  Handle<SharedFunctionInfo> shared(Cast<SharedFunctionInfo>(module->code()),
                                    isolate);
  DirectHandle<JSFunction> function =
      Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
          .Build();
  module->set_code(*function);
  module->SetStatus(kLinking);
  module->set_dfs_index(*dfs_index);
  module->set_dfs_ancestor_index(*dfs_index);
  stack->push_front(module);
  (*dfs_index)++;

  // Recurse.
  DirectHandle<FixedArray> requested_modules(module->requested_modules(),
                                             isolate);
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Cast<Module>(requested_modules->get(i)),
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
        std::count_if(
            stack->begin(), stack->end(),
            [&](DirectHandle<Module> m) { return *m == *requested_module; }));

    if (requested_module->status() == kLinking) {
      // SyntheticModules go straight to kLinked so this must be a
      // SourceTextModule
      module->set_dfs_ancestor_index(std::min(
          module->dfs_ancestor_index(),
          Cast<SourceTextModule>(*requested_module)->dfs_ancestor_index()));
    }
  }

  Handle<Script> script(module->GetScript(), isolate);
  DirectHandle<SourceTextModuleInfo> module_info(module->info(), isolate);

  // Resolve imports.
  DirectHandle<FixedArray> regular_imports(module_info->regular_imports(),
                                           isolate);
  for (int i = 0, n = regular_imports->length(); i < n; ++i) {
    DirectHandle<SourceTextModuleInfoEntry> entry(
        Cast<SourceTextModuleInfoEntry>(regular_imports->get(i)), isolate);
    Handle<String> name(Cast<String>(entry->import_name()), isolate);
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
  DirectHandle<FixedArray> special_exports(module_info->special_exports(),
                                           isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    DirectHandle<SourceTextModuleInfoEntry> entry(
        Cast<SourceTextModuleInfoEntry>(special_exports->get(i)), isolate);
    Handle<Object> name(entry->export_name(), isolate);
    if (IsUndefined(*name, isolate)) continue;  // Star export.
    MessageLocation loc(script, entry->beg_pos(), entry->end_pos());
    ResolveSet resolve_set(zone);
    if (ResolveExport(isolate, module, Handle<String>(), Cast<String>(name),
                      loc, true, &resolve_set)
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
  DirectHandle<FixedArray> special_exports(module->info()->special_exports(),
                                           isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    DirectHandle<SourceTextModuleInfoEntry> entry(
        Cast<SourceTextModuleInfoEntry>(special_exports->get(i)), isolate);
    if (!IsUndefined(entry->export_name(), roots)) {
      continue;  // Indirect export.
    }

    Handle<Module> requested_module(
        Cast<Module>(module->requested_modules()->get(entry->module_request())),
        isolate);

    // Recurse.
    if (IsSourceTextModule(*requested_module))
      FetchStarExports(isolate, Cast<SourceTextModule>(requested_module), zone,
                       visited);

    // Collect all of [requested_module]'s exports that must be added to
    // [module]'s exports (i.e. to [exports]).  We record these in
    // [more_exports].  Ambiguities (conflicting exports) are marked by mapping
    // the name to undefined instead of a Cell.
    DirectHandle<ObjectHashTable> requested_exports(requested_module->exports(),
                                                    isolate);
    for (InternalIndex index : requested_exports->IterateEntries()) {
      Tagged<Object> key;
      if (!requested_exports->ToKey(roots, index, &key)) continue;
      Handle<String> name(Cast<String>(key), isolate);

      if (name->Equals(roots.default_string())) continue;
      if (!IsTheHole(exports->Lookup(name), roots)) continue;

      Handle<Cell> cell(Cast<Cell>(requested_exports->ValueAt(index)), isolate);
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

void SourceTextModule::GatherAvailableAncestors(
    Isolate* isolate, Zone* zone, Handle<SourceTextModule> start,
    AvailableAncestorsSet* exec_list) {
  // The spec algorithm is recursive. It is transformed to an equivalent
  // iterative one here.
  ZoneStack<Handle<SourceTextModule>> worklist(zone);
  worklist.push(start);

  while (!worklist.empty()) {
    DirectHandle<SourceTextModule> module = worklist.top();
    worklist.pop();

    // 1. For each Module m of module.[[AsyncParentModules]], do
    for (int i = module->AsyncParentModuleCount(); i-- > 0;) {
      Handle<SourceTextModule> m = module->GetAsyncParentModule(isolate, i);

      // a. If execList does not contain m and
      //    m.[[CycleRoot]].[[EvaluationError]] is empty, then
      if (m->GetCycleRoot(isolate)->status() != kErrored &&
          exec_list->find(m) == exec_list->end()) {
        // i. Assert: m.[[Status]] is EVALUATING-ASYNC.
        // ii. Assert: m.[[EvaluationError]] is empty.
        DCHECK_EQ(m->status(), kEvaluatingAsync);

        // iii. Assert: m.[[AsyncEvaluation]] is true.
        DCHECK(m->HasAsyncEvaluationOrdinal());

        // iv. Assert: m.[[PendingAsyncDependencies]] > 0.
        DCHECK(m->HasPendingAsyncDependencies());

        // v. Set m.[[PendingAsyncDependencies]] to
        //    m.[[PendingAsyncDependencies]] - 1.
        m->DecrementPendingAsyncDependencies();

        // vi. If m.[[PendingAsyncDependencies]] = 0, then
        if (!m->HasPendingAsyncDependencies()) {
          // 1. Append m to execList.
          exec_list->insert(m);

          // 2. If m.[[HasTLA]] is false,
          //    perform ! GatherAvailableAncestors(m, execList).
          if (!m->has_toplevel_await()) worklist.push(m);
        }
      }
    }
  }

  // 2. Return UNUSED.
}

Handle<JSModuleNamespace> SourceTextModule::GetModuleNamespace(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    int module_request) {
  Handle<Module> requested_module(
      Cast<Module>(module->requested_modules()->get(module_request)), isolate);
  return Module::GetModuleNamespace(isolate, requested_module);
}

MaybeHandle<JSObject> SourceTextModule::GetImportMeta(
    Isolate* isolate, Handle<SourceTextModule> module) {
  Handle<UnionOf<JSObject, Hole>> import_meta(module->import_meta(kAcquireLoad),
                                              isolate);
  if (IsTheHole(*import_meta, isolate)) {
    if (!isolate->RunHostInitializeImportMetaObjectCallback(module).ToHandle(
            &import_meta)) {
      return {};
    }
    module->set_import_meta(*import_meta, kReleaseStore);
  }
  return Cast<JSObject>(import_meta);
}

// ES#sec-moduleevaluation
bool SourceTextModule::MaybeHandleEvaluationException(
    Isolate* isolate, ZoneForwardList<Handle<SourceTextModule>>* stack) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> exception = isolate->exception();
  // Step 9.
  if (isolate->is_catchable_by_javascript(exception)) {
    // a. For each Cyclic Module Record m in stack, do
    for (DirectHandle<SourceTextModule> descendant : *stack) {
      //   i. Assert: m.[[Status]] is EVALUATING.
      CHECK_EQ(descendant->status(), kEvaluating);
      //  ii. Set m.[[Status]] to EVALUATED.
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
  for (DirectHandle<SourceTextModule> descendant : *stack) {
    descendant->RecordError(isolate, exception);
  }
  CHECK_EQ(status(), kErrored);
  CHECK_EQ(this->exception(), *isolate->factory()->null_value());
  return false;
}

// ES#sec-moduleevaluation
MaybeHandle<Object> SourceTextModule::Evaluate(
    Isolate* isolate, Handle<SourceTextModule> module) {
  CHECK(module->status() == kLinked || module->status() == kEvaluatingAsync ||
        module->status() == kEvaluated);

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
    // d. Perform ! Call(capability.[[Reject]], undefined,
    //                   «result.[[Value]]»).
    JSPromise::Reject(capability, handle(module->exception(), isolate));
  } else {  // 10. Else,
    // a. Assert: module.[[Status]] is either EVALUATING-ASYNC or EVALUATED.
    CHECK_GE(module->status(), kEvaluatingAsync);

    // c. If module.[[AsyncEvaluation]] is false, then
    if (!module->HasAsyncEvaluationOrdinal()) {
      // i. Assert: module.[[Status]] is EVALUATED.
      DCHECK_EQ(module->status(), kEvaluated);

      // ii. Perform ! Call(capability.[[Resolve]], undefined,
      //                    «undefined»).
      JSPromise::Resolve(capability, isolate->factory()->undefined_value())
          .ToHandleChecked();
    }

    // d. Assert: stack is empty.
    DCHECK(stack.empty());
  }

  // 11. Return capability.[[Promise]].
  return capability;
}

// ES#sec-async-module-execution-fulfilled
Maybe<bool> SourceTextModule::AsyncModuleExecutionFulfilled(
    Isolate* isolate, Handle<SourceTextModule> module) {
  // 1. If module.[[Status]] is EVALUATED, then
  if (module->status() == kErrored) {
    // a. Assert: module.[[EvaluationError]] is not EMPTY.
    DCHECK(!IsTheHole(module->exception(), isolate));
    // b. Return UNUSED.
    return Just(true);
  }

  // 2. Assert: module.[[Status]] is EVALUATING-ASYNC.
  DCHECK_EQ(module->status(), kEvaluatingAsync);

  // 3. Assert: module.[[AsyncEvaluation]] is true.
  DCHECK(module->HasAsyncEvaluationOrdinal());

  // 4. Assert: module.[[EvaluationError]] is EMPTY.
  // (Done by step 2.)

  // 5. Set module.[[AsyncEvaluation]] to false.
  module->set_async_evaluation_ordinal(kAsyncEvaluateDidFinish);

  // 6. Set module.[[Status]] to EVALUATED.
  module->SetStatus(kEvaluated);

  // 7. If module.[[TopLevelCapability]] is not EMPTY, then
  if (!IsUndefined(module->top_level_capability(), isolate)) {
    //  a. Assert: module.[[CycleRoot]] is equal to module.
    DCHECK_EQ(*module->GetCycleRoot(isolate), *module);

    //   i. Perform ! Call(module.[[TopLevelCapability]].[[Resolve]], undefined,
    //                     «undefined»).
    Handle<JSPromise> capability(
        Cast<JSPromise>(module->top_level_capability()), isolate);
    JSPromise::Resolve(capability, isolate->factory()->undefined_value())
        .ToHandleChecked();
  }

  // 8. Let execList be a new empty List.
  Zone zone(isolate->allocator(), ZONE_NAME);
  AvailableAncestorsSet exec_list(&zone);

  // 9. Perform GatherAvailableAncestors(module, execList).
  GatherAvailableAncestors(isolate, &zone, module, &exec_list);

  // 10. Let sortedExecList be a List of elements that are the elements of
  //    execList, in the order in which they had their [[AsyncEvaluation]]
  //    fields set to true in InnerModuleEvaluation.
  //
  // This step is implemented by AvailableAncestorsSet, which is a set
  // ordered on async_evaluation_ordinal.

  // 11. Assert: All elements of sortedExecList have their [[AsyncEvaluation]]
  //    field set to true, [[PendingAsyncDependencies]] field set to 0 and
  //    [[EvaluationError]] field set to undefined.
#ifdef DEBUG
  for (DirectHandle<SourceTextModule> m : exec_list) {
    DCHECK(m->HasAsyncEvaluationOrdinal());
    DCHECK(!m->HasPendingAsyncDependencies());
    DCHECK_NE(m->status(), kErrored);
  }
#endif

  // 12. For each Module m of sortedExecList, do
  for (DirectHandle<SourceTextModule> m : exec_list) {
    // a. If m.[[Status]] is EVALUATED, then
    //   i. Assert: m.[[EvaluationError]] is not EMPTY.
    // (Holds by construction in V8 implementation.)

    // b. Else if m.[[HasTLA]] is true, then
    if (m->has_toplevel_await()) {
      // i. Perform ExecuteAsyncModule(m).
      //
      // The execution may have been terminated and can not be resumed, so just
      // raise the exception.
      MAYBE_RETURN(ExecuteAsyncModule(isolate, m), Nothing<bool>());
    } else {  // c. Else,
      // i. Let result be m.ExecuteModule().
      Handle<Object> unused_result;
      MaybeHandle<Object> exception;
      // ii. If result is an abrupt completion, then
      if (!ExecuteModule(isolate, m, &exception).ToHandle(&unused_result)) {
        // 1. Perform AsyncModuleExecutionRejected(m, result.[[Value]]).
        AsyncModuleExecutionRejected(isolate, m, exception.ToHandleChecked());
      } else {  // iii. Else,
        // 1. Set m.[[Status]] to EVALUATED.
        //
        // TODO(syg): Update comment after spec bug is fixed.
        // https://github.com/tc39/ecma262/issues/3356
        m->set_async_evaluation_ordinal(kAsyncEvaluateDidFinish);
        m->SetStatus(kEvaluated);

        // 2. If m.[[TopLevelCapability]] is not EMPTY, then
        if (!IsUndefined(m->top_level_capability(), isolate)) {
          // a. Assert: m.[[CycleRoot]] and m are the same Module Record.
          DCHECK_EQ(*m->GetCycleRoot(isolate), *m);

          // b. Perform ! Call(m.[[TopLevelCapability]].[[Resolve]], undefined,
          //    « undefined »).
          Handle<JSPromise> capability(
              Cast<JSPromise>(m->top_level_capability()), isolate);
          JSPromise::Resolve(capability, isolate->factory()->undefined_value())
              .ToHandleChecked();
        }
      }
    }
  }

  // Return UNUSED.
  return Just(true);
}

// ES#sec-async-module-execution-rejected
void SourceTextModule::AsyncModuleExecutionRejected(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    Handle<Object> exception) {
  // 1. If module.[[Status]] is EVALUATED, then
  if (module->status() == kErrored) {
    // a. Assert: module.[[EvaluationError]] is not empty.
    DCHECK(!IsTheHole(module->exception(), isolate));
    // b. Return UNUSED.
    return;
  }

  DCHECK(isolate->is_catchable_by_javascript(*exception));
  // 2. Assert: module.[[Status]] is EVALUATING-ASYNC.
  CHECK_EQ(module->status(), kEvaluatingAsync);
  // 3. Assert: module.[[AsyncEvaluation]] is true.
  DCHECK(module->HasAsyncEvaluationOrdinal());
  // 4. Assert: module.[[EvaluationError]] is EMPTY.
  DCHECK(IsTheHole(module->exception(), isolate));

  // 5. Set module.[[EvaluationError]] to ThrowCompletion(error).
  module->RecordError(isolate, *exception);

  // 6. Set module.[[Status]] to EVALUATED.
  // (We have a status for kErrored, so don't set to kEvaluated.)
  module->set_async_evaluation_ordinal(kAsyncEvaluateDidFinish);

  // 7. For each Cyclic Module Record m of module.[[AsyncParentModules]], do
  for (int i = 0; i < module->AsyncParentModuleCount(); i++) {
    // a. Perform AsyncModuleExecutionRejected(m, error).
    DirectHandle<SourceTextModule> m = module->GetAsyncParentModule(isolate, i);
    AsyncModuleExecutionRejected(isolate, m, exception);
  }

  // 8. If module.[[TopLevelCapability]] is not EMPTY, then
  if (!IsUndefined(module->top_level_capability(), isolate)) {
    // a. Assert: module.[[CycleRoot]] and module are the same Module Record.
    DCHECK_EQ(*module->GetCycleRoot(isolate), *module);

    //  b. Perform ! Call(module.[[TopLevelCapability]].[[Reject]],
    //                    undefined, «error»).
    Handle<JSPromise> capability(
        Cast<JSPromise>(module->top_level_capability()), isolate);
    JSPromise::Reject(capability, exception);
  }

  // 9. Return UNUSED.
}

// static
Maybe<bool> SourceTextModule::ExecuteAsyncModule(
    Isolate* isolate, DirectHandle<SourceTextModule> module) {
  // 1. Assert: module.[[Status]] is either EVALUATING or EVALUATING-ASYNC.
  CHECK(module->status() == kEvaluating ||
        module->status() == kEvaluatingAsync);

  // 2. Assert: module.[[HasTLA]] is true.
  DCHECK(module->has_toplevel_await());

  // 3. Let capability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> capability = isolate->factory()->NewJSPromise();

  Handle<Context> execute_async_module_context =
      isolate->factory()->NewBuiltinContext(
          isolate->native_context(),
          ExecuteAsyncModuleContextSlots::kContextLength);
  execute_async_module_context->set(ExecuteAsyncModuleContextSlots::kModule,
                                    *module);

  // 4. Let fulfilledClosure be a new Abstract Closure with no parameters that
  //    captures module and performs the following steps when called:
  //   a. Perform AsyncModuleExecutionFulfilled(module).
  //   b. Return undefined.
  // 5. Let onFulfilled be CreateBuiltinFunction(fulfilledClosure, 0, "", « »).
  Handle<JSFunction> on_fulfilled =
      Factory::JSFunctionBuilder{
          isolate,
          isolate->factory()
              ->source_text_module_execute_async_module_fulfilled_sfi(),
          execute_async_module_context}
          .Build();

  // 6. Let rejectedClosure be a new Abstract Closure with parameters (error)
  //    that captures module and performs the following steps when called:
  //   a. Perform AsyncModuleExecutionRejected(module, error).
  //   b. Return undefined.
  // 7. Let onRejected be CreateBuiltinFunction(rejectedClosure, 0, "", « »).
  Handle<JSFunction> on_rejected =
      Factory::JSFunctionBuilder{
          isolate,
          isolate->factory()
              ->source_text_module_execute_async_module_rejected_sfi(),
          execute_async_module_context}
          .Build();

  // 8. Perform ! PerformPromiseThen(capability.[[Promise]],
  //                                 onFulfilled, onRejected).
  Handle<Object> argv[] = {on_fulfilled, on_rejected};
  if (V8_UNLIKELY(Execution::CallBuiltin(isolate, isolate->promise_then(),
                                         capability, arraysize(argv), argv)
                      .is_null())) {
    // TODO(349961173): We assume the builtin call can only fail with a
    // termination exception. If this check fails in the wild investigate why
    // the call fails. Otherwise turn this into a DCHECK in the future.
    CHECK(isolate->is_execution_terminating());
    return Nothing<bool>();
  }

  // 9. Perform ! module.ExecuteModule(capability).
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

  // 10. Return UNUSED.
  return Just<bool>(true);
}

MaybeHandle<Object> SourceTextModule::InnerExecuteAsyncModule(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    DirectHandle<JSPromise> capability) {
  // If we have an async module, then it has an associated
  // JSAsyncFunctionObject, which we then evaluate with the passed in promise
  // capability.
  Handle<JSAsyncFunctionObject> async_function_object(
      Cast<JSAsyncFunctionObject>(module->code()), isolate);
  async_function_object->set_promise(*capability);
  Handle<JSFunction> resume(
      isolate->native_context()->async_module_evaluate_internal(), isolate);
  return Execution::TryCall(isolate, resume, async_function_object, 0, nullptr,
                            Execution::MessageHandling::kKeepPending, nullptr);
}

MaybeHandle<Object> SourceTextModule::ExecuteModule(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    MaybeHandle<Object>* exception_out) {
  // Synchronous modules have an associated JSGeneratorObject.
  Handle<JSGeneratorObject> generator(Cast<JSGeneratorObject>(module->code()),
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
      Object::BooleanValue(Cast<JSIteratorResult>(*result)->done(), isolate));
  return handle(Cast<JSIteratorResult>(*result)->value(), isolate);
}

MaybeHandle<Object> SourceTextModule::InnerModuleEvaluation(
    Isolate* isolate, Handle<SourceTextModule> module,
    ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index) {
  STACK_CHECK(isolate, MaybeHandle<Object>());
  int module_status = module->status();
  // InnerModuleEvaluation(module, stack, index)

  // 2. If module.[[Status]] is either EVALUATING-ASYNC or EVALUATED, then
  if (module_status == kEvaluatingAsync || module_status == kEvaluating ||
      module_status == kEvaluated) {
    // a. If module.[[EvaluationError]] is undefined, return index.
    // (We return undefined instead)
    //
    // 3. If module.[[Status]] is EVALUATING, return index.
    // (Out of order)
    return isolate->factory()->undefined_value();
  } else if (module_status == kErrored) {
    // b. Otherwise return module.[[EvaluationError]].
    // (We throw on isolate and return a MaybeHandle<Object> instead)
    isolate->Throw(module->exception());
    return MaybeHandle<Object>();
  }

  // 4. Assert: module.[[Status]] is LINKED.
  CHECK_EQ(module_status, kLinked);

  DirectHandle<FixedArray> requested_modules;

  {
    DisallowGarbageCollection no_gc;
    Tagged<SourceTextModule> raw_module = *module;
    // 5. Set module.[[Status]] to EVALUATING.
    raw_module->SetStatus(kEvaluating);

    // 6. Set module.[[DFSIndex]] to index.
    raw_module->set_dfs_index(*dfs_index);

    // 7. Set module.[[DFSAncestorIndex]] to index.
    raw_module->set_dfs_ancestor_index(*dfs_index);

    // 8. Set module.[[PendingAsyncDependencies]] to 0.
    DCHECK(!raw_module->HasPendingAsyncDependencies());

    // 9. Set index to index + 1.
    (*dfs_index)++;

    // 10. Append module to stack.
    stack->push_front(module);

    // Recursion.
    requested_modules = direct_handle(raw_module->requested_modules(), isolate);
  }

  // 11. For each String required of module.[[RequestedModules]], do
  for (int i = 0, length = requested_modules->length(); i < length; ++i) {
    Handle<Module> requested_module(Cast<Module>(requested_modules->get(i)),
                                    isolate);
    // c. If requiredModule is a Cyclic Module Record, then
    if (IsSourceTextModule(*requested_module)) {
      // b. Set index to ? InnerModuleEvaluation(requiredModule, stack, index).
      // (Out of order because InnerModuleEvaluation is type-driven.)
      Handle<SourceTextModule> required_module(
          Cast<SourceTextModule>(*requested_module), isolate);
      RETURN_ON_EXCEPTION(
          isolate,
          InnerModuleEvaluation(isolate, required_module, stack, dfs_index));
      int required_module_status = required_module->status();

      // i. Assert: requiredModule.[[Status]] is one of EVALUATING,
      //    EVALUATING-ASYNC, or EVALUATED.
      // (We also assert the module cannot be errored, because if it was
      //  we would have already returned from InnerModuleEvaluation)
      CHECK_GE(required_module_status, kEvaluating);
      CHECK_NE(required_module_status, kErrored);

      // ii. Assert: requiredModule.[[Status]] is EVALUATING if and only if
      //     requiredModule is in stack.
      SLOW_DCHECK((requested_module->status() == kEvaluating) ==
                  std::count_if(stack->begin(), stack->end(),
                                [&](DirectHandle<Module> m) {
                                  return *m == *requested_module;
                                }));

      // iii. If requiredModule.[[Status]] is EVALUATING, then
      if (required_module_status == kEvaluating) {
        // 1. Set module.[[DFSAncestorIndex]] to
        //    min(module.[[DFSAncestorIndex]],
        //        requiredModule.[[DFSAncestorIndex]]).
        module->set_dfs_ancestor_index(
            std::min(module->dfs_ancestor_index(),
                     required_module->dfs_ancestor_index()));
      } else {  // iv. Else,
        // 1. Set requiredModule to requiredModule.[[CycleRoot]].
        required_module = required_module->GetCycleRoot(isolate);
        required_module_status = required_module->status();

        // 2. Assert: requiredModule.[[Status]] is either EVALUATING-ASYNC or
        //    EVALUATED.
        CHECK_GE(required_module_status, kEvaluatingAsync);

        // 3. If requiredModule.[[EvaluationError]] is not EMPTY,
        //    return ? module.[[EvaluationError]].

        // (If there was an exception on the original required module we would
        // have already returned. This check handles the case where the
        // AsyncCycleRoot has an error. Instead of returning the exception, we
        // throw on isolate and return a MaybeHandle<Object>.)
        if (required_module_status == kErrored) {
          isolate->Throw(required_module->exception());
          return MaybeHandle<Object>();
        }
      }
      // v. If requiredModule.[[AsyncEvaluation]] is true, then
      if (required_module->HasAsyncEvaluationOrdinal()) {
        // 1. Set module.[[PendingAsyncDependencies]] to
        //    module.[[PendingAsyncDependencies]] + 1.
        module->IncrementPendingAsyncDependencies();

        // 2. Append module to requiredModule.[[AsyncParentModules]].
        AddAsyncParentModule(isolate, required_module, module);
      }
    } else {
      // b. Set index to ? InnerModuleEvaluation(requiredModule, stack, index).
      // (Out of order because InnerModuleEvaluation is type-driven.)
      RETURN_ON_EXCEPTION(isolate, Module::Evaluate(isolate, requested_module));
    }
  }

  // The spec returns the module index for proper numbering of dependencies.
  // However, we pass the module index by pointer instead.
  //
  // Before async modules v8 returned the value result from calling next
  // on the module's implicit iterator. We preserve this behavior for
  // synchronous modules, but return undefined for AsyncModules.
  Handle<Object> result = isolate->factory()->undefined_value();

  // 12. If module.[[PendingAsyncDependencies]] > 0 or module.[[HasTLA]] is
  //     true, then
  if (module->HasPendingAsyncDependencies() || module->has_toplevel_await()) {
    // a. Assert: module.[[AsyncEvaluation]] is false and was never previously
    //    set to true.
    DCHECK_EQ(module->async_evaluation_ordinal(), kNotAsyncEvaluated);

    // b. Set module.[[AsyncEvaluation]] to true.
    // c. NOTE: The order in which module records have their [[AsyncEvaluation]]
    //    fields transition to true is significant.
    module->set_async_evaluation_ordinal(
        isolate->NextModuleAsyncEvaluationOrdinal());

    // c. If module.[[PendingAsyncDependencies]] = 0, perform
    //    ExecuteAsyncModule(module).
    // The execution may have been terminated and can not be resumed, so just
    // raise the exception.
    if (!module->HasPendingAsyncDependencies()) {
      MAYBE_RETURN(SourceTextModule::ExecuteAsyncModule(isolate, module),
                   MaybeHandle<Object>());
    }
  } else {  // 13. Else,
    // a. Perform ? module.ExecuteModule().
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
                             DirectHandle<SourceTextModule> module) {
  Factory* factory = isolate->factory();

  DCHECK(IsTheHole(module->import_meta(kAcquireLoad), isolate));

  DirectHandle<FixedArray> regular_exports =
      factory->NewFixedArray(module->regular_exports()->length());
  DirectHandle<FixedArray> regular_imports =
      factory->NewFixedArray(module->regular_imports()->length());
  DirectHandle<FixedArray> requested_modules =
      factory->NewFixedArray(module->requested_modules()->length());

  DisallowGarbageCollection no_gc;
  Tagged<SourceTextModule> raw_module = *module;
  if (raw_module->status() == kLinking) {
    raw_module->set_code(Cast<JSFunction>(raw_module->code())->shared());
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
    DirectHandle<JSGeneratorObject> code(Cast<JSGeneratorObject>(found->code()),
                                         isolate);
    Handle<SharedFunctionInfo> shared(found->GetSharedFunctionInfo(), isolate);
    Handle<Object> script(shared->script(), isolate);
    MessageLocation location =
        MessageLocation(Cast<Script>(script), shared, code->code_offset());
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
  if (!HasPendingAsyncDependencies() && HasAsyncEvaluationOrdinal()) {
    DCHECK(HasAsyncEvaluationOrdinal());
    result->push_back(handle(*this, isolate));
    return;
  }
  // The module isn't what we are looking for, continue looking in the graph.
  Tagged<FixedArray> requested = requested_modules();
  int length = requested->length();
  for (int i = 0; i < length; ++i) {
    Tagged<Module> requested_module = Cast<Module>(requested->get(i));
    if (IsSourceTextModule(requested_module) &&
        visited->insert(handle(requested_module, isolate)).second) {
      Tagged<SourceTextModule> source_text_module =
          Cast<SourceTextModule>(requested_module);
      source_text_module->InnerGetStalledTopLevelAwaitModule(isolate, visited,
                                                             result);
    }
  }
}

}  // namespace internal
}  // namespace v8
