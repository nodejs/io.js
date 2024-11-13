// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MODULE_INL_H_
#define V8_OBJECTS_MODULE_INL_H_

#include "src/objects/module.h"
#include "src/objects/objects-inl.h"  // Needed for write barriers
#include "src/objects/scope-info.h"
#include "src/objects/source-text-module.h"
#include "src/objects/string-inl.h"
#include "src/objects/synthetic-module.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/module-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(Module)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSModuleNamespace)
TQ_OBJECT_CONSTRUCTORS_IMPL(ScriptOrModule)

NEVER_READ_ONLY_SPACE_IMPL(Module)
NEVER_READ_ONLY_SPACE_IMPL(ModuleRequest)
NEVER_READ_ONLY_SPACE_IMPL(SourceTextModule)
NEVER_READ_ONLY_SPACE_IMPL(SyntheticModule)

BOOL_ACCESSORS(SourceTextModule, flags, has_toplevel_await,
               HasToplevelAwaitBit::kShift)
BIT_FIELD_ACCESSORS(SourceTextModule, flags, async_evaluation_ordinal,
                    SourceTextModule::AsyncEvaluationOrdinalBits)
ACCESSORS(SourceTextModule, async_parent_modules, Tagged<ArrayList>,
          kAsyncParentModulesOffset)

BIT_FIELD_ACCESSORS(ModuleRequest, flags, position, ModuleRequest::PositionBits)

inline void ModuleRequest::set_phase(ModuleImportPhase phase) {
  DCHECK_GE(PhaseBit::kMax, phase);
  int hints = flags();
  hints = PhaseBit::update(hints, phase);
  set_flags(hints);
}

inline ModuleImportPhase ModuleRequest::phase() const {
  return PhaseBit::decode(flags());
}

struct Module::Hash {
  V8_INLINE size_t operator()(Tagged<Module> module) const {
    return module->hash();
  }
};

Tagged<SourceTextModuleInfo> SourceTextModule::info() const {
  return GetSharedFunctionInfo()->scope_info()->ModuleDescriptorInfo();
}

OBJECT_CONSTRUCTORS_IMPL(SourceTextModuleInfo, FixedArray)

Tagged<FixedArray> SourceTextModuleInfo::module_requests() const {
  return Cast<FixedArray>(get(kModuleRequestsIndex));
}

Tagged<FixedArray> SourceTextModuleInfo::special_exports() const {
  return Cast<FixedArray>(get(kSpecialExportsIndex));
}

Tagged<FixedArray> SourceTextModuleInfo::regular_exports() const {
  return Cast<FixedArray>(get(kRegularExportsIndex));
}

Tagged<FixedArray> SourceTextModuleInfo::regular_imports() const {
  return Cast<FixedArray>(get(kRegularImportsIndex));
}

Tagged<FixedArray> SourceTextModuleInfo::namespace_imports() const {
  return Cast<FixedArray>(get(kNamespaceImportsIndex));
}

#ifdef DEBUG
bool SourceTextModuleInfo::Equals(Tagged<SourceTextModuleInfo> other) const {
  return regular_exports() == other->regular_exports() &&
         regular_imports() == other->regular_imports() &&
         special_exports() == other->special_exports() &&
         namespace_imports() == other->namespace_imports() &&
         module_requests() == other->module_requests();
}
#endif

struct ModuleHandleHash {
  V8_INLINE size_t operator()(DirectHandle<Module> module) const {
    return module->hash();
  }
};

struct ModuleHandleEqual {
  V8_INLINE bool operator()(DirectHandle<Module> lhs,
                            DirectHandle<Module> rhs) const {
    return *lhs == *rhs;
  }
};

class UnorderedModuleSet
    : public std::unordered_set<Handle<Module>, ModuleHandleHash,
                                ModuleHandleEqual,
                                ZoneAllocator<Handle<Module>>> {
 public:
  explicit UnorderedModuleSet(Zone* zone)
      : std::unordered_set<Handle<Module>, ModuleHandleHash, ModuleHandleEqual,
                           ZoneAllocator<Handle<Module>>>(
            2 /* bucket count */, ModuleHandleHash(), ModuleHandleEqual(),
            ZoneAllocator<Handle<Module>>(zone)) {}
};

Handle<SourceTextModule> SourceTextModule::GetCycleRoot(
    Isolate* isolate) const {
  CHECK_GE(status(), kEvaluatingAsync);
  DCHECK(!IsTheHole(cycle_root(), isolate));
  Handle<SourceTextModule> root(Cast<SourceTextModule>(cycle_root()), isolate);
  return root;
}

void SourceTextModule::AddAsyncParentModule(
    Isolate* isolate, DirectHandle<SourceTextModule> module,
    DirectHandle<SourceTextModule> parent) {
  Handle<ArrayList> async_parent_modules(module->async_parent_modules(),
                                         isolate);
  DirectHandle<ArrayList> new_array_list =
      ArrayList::Add(isolate, async_parent_modules, parent);
  module->set_async_parent_modules(*new_array_list);
}

Handle<SourceTextModule> SourceTextModule::GetAsyncParentModule(
    Isolate* isolate, int index) {
  Handle<SourceTextModule> module(
      Cast<SourceTextModule>(async_parent_modules()->get(index)), isolate);
  return module;
}

int SourceTextModule::AsyncParentModuleCount() {
  return async_parent_modules()->length();
}

bool SourceTextModule::HasAsyncEvaluationOrdinal() const {
  return async_evaluation_ordinal() >= kFirstAsyncEvaluationOrdinal;
}

bool SourceTextModule::HasPendingAsyncDependencies() {
  DCHECK_GE(pending_async_dependencies(), 0);
  return pending_async_dependencies() > 0;
}

void SourceTextModule::IncrementPendingAsyncDependencies() {
  set_pending_async_dependencies(pending_async_dependencies() + 1);
}

void SourceTextModule::DecrementPendingAsyncDependencies() {
  set_pending_async_dependencies(pending_async_dependencies() - 1);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MODULE_INL_H_
