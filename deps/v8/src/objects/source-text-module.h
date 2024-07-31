// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SOURCE_TEXT_MODULE_H_
#define V8_OBJECTS_SOURCE_TEXT_MODULE_H_

#include "src/objects/contexts.h"
#include "src/objects/module.h"
#include "src/objects/promise.h"
#include "src/zone/zone-containers.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class UnorderedModuleSet;
class StructBodyDescriptor;

#include "torque-generated/src/objects/source-text-module-tq.inc"

// The runtime representation of an ECMAScript Source Text Module Record.
// https://tc39.github.io/ecma262/#sec-source-text-module-records
class SourceTextModule
    : public TorqueGeneratedSourceTextModule<SourceTextModule, Module> {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_VERIFIER(SourceTextModule)
  DECL_PRINTER(SourceTextModule)

  // The shared function info in case {status} is not kEvaluating, kEvaluated or
  // kErrored.
  Tagged<SharedFunctionInfo> GetSharedFunctionInfo() const;

  Tagged<Script> GetScript() const;

  // Whether or not this module is an async module. Set during module creation
  // and does not change afterwards.
  DECL_BOOLEAN_ACCESSORS(async)

  // Get the SourceTextModuleInfo associated with the code.
  inline Tagged<SourceTextModuleInfo> info() const;

  Tagged<Cell> GetCell(int cell_index);
  static Handle<Object> LoadVariable(Isolate* isolate,
                                     Handle<SourceTextModule> module,
                                     int cell_index);
  static void StoreVariable(Handle<SourceTextModule> module, int cell_index,
                            Handle<Object> value);

  static int ImportIndex(int cell_index);
  static int ExportIndex(int cell_index);

  // Used by builtins to fulfill or reject the promise associated
  // with async SourceTextModules. Return Nothing if the execution is
  // terminated.
  static Maybe<bool> AsyncModuleExecutionFulfilled(
      Isolate* isolate, Handle<SourceTextModule> module);
  static void AsyncModuleExecutionRejected(Isolate* isolate,
                                           Handle<SourceTextModule> module,
                                           Handle<Object> exception);

  // Get the namespace object for [module_request] of [module].  If it doesn't
  // exist yet, it is created.
  static Handle<JSModuleNamespace> GetModuleNamespace(
      Isolate* isolate, Handle<SourceTextModule> module, int module_request);

  // Get the import.meta object of [module].  If it doesn't exist yet, it is
  // created and passed to the embedder callback for initialization.
  V8_EXPORT_PRIVATE static MaybeHandle<JSObject> GetImportMeta(
      Isolate* isolate, Handle<SourceTextModule> module);

  using BodyDescriptor =
      SubclassBodyDescriptor<Module::BodyDescriptor,
                             FixedBodyDescriptor<kCodeOffset, kSize, kSize>>;

  static constexpr unsigned kFirstAsyncEvaluatingOrdinal = 2;

  enum ExecuteAsyncModuleContextSlots {
    kModule = Context::MIN_CONTEXT_SLOTS,
    kContextLength,
  };

  V8_EXPORT_PRIVATE
  std::vector<std::tuple<Handle<SourceTextModule>, Handle<JSMessageObject>>>
  GetStalledTopLevelAwaitMessages(Isolate* isolate);

 private:
  friend class Factory;
  friend class Module;

  struct AsyncEvaluatingOrdinalCompare;
  using AsyncParentCompletionSet =
      ZoneSet<Handle<SourceTextModule>, AsyncEvaluatingOrdinalCompare>;

  // Appends a tuple of module and generator to the async parent modules
  // ArrayList.
  inline static void AddAsyncParentModule(Isolate* isolate,
                                          Handle<SourceTextModule> module,
                                          Handle<SourceTextModule> parent);

  // Get the non-hole cycle root. Only valid when status >= kEvaluated.
  inline Handle<SourceTextModule> GetCycleRoot(Isolate* isolate) const;

  // Returns a SourceTextModule, the
  // ith parent in depth first traversal order of a given async child.
  inline Handle<SourceTextModule> GetAsyncParentModule(Isolate* isolate,
                                                       int index);

  // Returns the number of async parent modules for a given async child.
  inline int AsyncParentModuleCount();

  inline bool IsAsyncEvaluating() const;

  inline bool HasPendingAsyncDependencies();
  inline void IncrementPendingAsyncDependencies();
  inline void DecrementPendingAsyncDependencies();

  // Bits for flags.
  DEFINE_TORQUE_GENERATED_SOURCE_TEXT_MODULE_FLAGS()

  // async_evaluating_ordinal, top_level_capability, pending_async_dependencies,
  // and async_parent_modules are used exclusively during evaluation of async
  // modules and the modules which depend on them.
  //
  // If >1, this module is async and evaluating or currently evaluating an async
  // child. The integer is an ordinal for when this module first started async
  // evaluation and is used for sorting async parent modules when determining
  // which parent module can start executing after an async evaluation
  // completes.
  //
  // If 1, this module has finished async evaluating.
  //
  // If 0, this module is not async or has not been async evaluated.
  static constexpr unsigned kNotAsyncEvaluated = 0;
  static constexpr unsigned kAsyncEvaluateDidFinish = 1;
  static_assert(kNotAsyncEvaluated < kAsyncEvaluateDidFinish);
  static_assert(kAsyncEvaluateDidFinish < kFirstAsyncEvaluatingOrdinal);
  static_assert(kMaxModuleAsyncEvaluatingOrdinal ==
                AsyncEvaluatingOrdinalBits::kMax);
  DECL_PRIMITIVE_ACCESSORS(async_evaluating_ordinal, unsigned)

  // The parent modules of a given async dependency, use async_parent_modules()
  // to retrieve the ArrayList representation.
  DECL_ACCESSORS(async_parent_modules, Tagged<ArrayList>)

  // Helpers for Instantiate and Evaluate.
  static void CreateExport(Isolate* isolate, Handle<SourceTextModule> module,
                           int cell_index, Handle<FixedArray> names);
  static void CreateIndirectExport(Isolate* isolate,
                                   Handle<SourceTextModule> module,
                                   Handle<String> name,
                                   Handle<SourceTextModuleInfoEntry> entry);

  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveExport(
      Isolate* isolate, Handle<SourceTextModule> module,
      Handle<String> module_specifier, Handle<String> export_name,
      MessageLocation loc, bool must_resolve, ResolveSet* resolve_set);
  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveImport(
      Isolate* isolate, Handle<SourceTextModule> module, Handle<String> name,
      int module_request_index, MessageLocation loc, bool must_resolve,
      ResolveSet* resolve_set);

  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveExportUsingStarExports(
      Isolate* isolate, Handle<SourceTextModule> module,
      Handle<String> module_specifier, Handle<String> export_name,
      MessageLocation loc, bool must_resolve, ResolveSet* resolve_set);

  static V8_WARN_UNUSED_RESULT bool PrepareInstantiate(
      Isolate* isolate, Handle<SourceTextModule> module,
      v8::Local<v8::Context> context,
      v8::Module::ResolveModuleCallback callback,
      Module::DeprecatedResolveCallback callback_without_import_assertions);
  static V8_WARN_UNUSED_RESULT bool FinishInstantiate(
      Isolate* isolate, Handle<SourceTextModule> module,
      ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index,
      Zone* zone);
  static V8_WARN_UNUSED_RESULT bool RunInitializationCode(
      Isolate* isolate, Handle<SourceTextModule> module);

  static void FetchStarExports(Isolate* isolate,
                               Handle<SourceTextModule> module, Zone* zone,
                               UnorderedModuleSet* visited);

  static void GatherAsyncParentCompletions(Isolate* isolate, Zone* zone,
                                           Handle<SourceTextModule> start,
                                           AsyncParentCompletionSet* exec_list);

  // Implementation of spec concrete method Evaluate.
  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> Evaluate(
      Isolate* isolate, Handle<SourceTextModule> module);

  // Implementation of spec abstract operation InnerModuleEvaluation.
  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> InnerModuleEvaluation(
      Isolate* isolate, Handle<SourceTextModule> module,
      ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index);

  // Returns true if the evaluation exception was catchable by js, and false
  // for termination exceptions.
  bool MaybeHandleEvaluationException(
      Isolate* isolate, ZoneForwardList<Handle<SourceTextModule>>* stack);

  static V8_WARN_UNUSED_RESULT bool MaybeTransitionComponent(
      Isolate* isolate, Handle<SourceTextModule> module,
      ZoneForwardList<Handle<SourceTextModule>>* stack, Status new_status);

  // Implementation of spec ExecuteModule is broken up into
  // InnerExecuteAsyncModule for asynchronous modules and ExecuteModule
  // for synchronous modules.
  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> InnerExecuteAsyncModule(
      Isolate* isolate, Handle<SourceTextModule> module,
      Handle<JSPromise> capability);

  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> ExecuteModule(
      Isolate* isolate, Handle<SourceTextModule> module,
      MaybeHandle<Object>* exception_out);

  // Implementation of spec ExecuteAsyncModule. Return Nothing if the execution
  // is been terminated.
  static V8_WARN_UNUSED_RESULT Maybe<bool> ExecuteAsyncModule(
      Isolate* isolate, Handle<SourceTextModule> module);

  static void Reset(Isolate* isolate, Handle<SourceTextModule> module);

  V8_EXPORT_PRIVATE void InnerGetStalledTopLevelAwaitModule(
      Isolate* isolate, UnorderedModuleSet* visited,
      std::vector<Handle<SourceTextModule>>* result);

  TQ_OBJECT_CONSTRUCTORS(SourceTextModule)
};

// SourceTextModuleInfo is to SourceTextModuleDescriptor what ScopeInfo is to
// Scope.
class SourceTextModuleInfo : public FixedArray {
 public:
  DECL_CAST(SourceTextModuleInfo)

  template <typename IsolateT>
  static Handle<SourceTextModuleInfo> New(IsolateT* isolate, Zone* zone,
                                          SourceTextModuleDescriptor* descr);

  inline Tagged<FixedArray> module_requests() const;
  inline Tagged<FixedArray> special_exports() const;
  inline Tagged<FixedArray> regular_exports() const;
  inline Tagged<FixedArray> regular_imports() const;
  inline Tagged<FixedArray> namespace_imports() const;

  // Accessors for [regular_exports].
  int RegularExportCount() const;
  Tagged<String> RegularExportLocalName(int i) const;
  int RegularExportCellIndex(int i) const;
  Tagged<FixedArray> RegularExportExportNames(int i) const;

#ifdef DEBUG
  inline bool Equals(Tagged<SourceTextModuleInfo> other) const;
#endif

 private:
  template <typename Impl>
  friend class FactoryBase;
  friend class SourceTextModuleDescriptor;
  enum {
    kModuleRequestsIndex,
    kSpecialExportsIndex,
    kRegularExportsIndex,
    kNamespaceImportsIndex,
    kRegularImportsIndex,
    kLength
  };
  enum {
    kRegularExportLocalNameOffset,
    kRegularExportCellIndexOffset,
    kRegularExportExportNamesOffset,
    kRegularExportLength
  };

  OBJECT_CONSTRUCTORS(SourceTextModuleInfo, FixedArray);
};

class ModuleRequest
    : public TorqueGeneratedModuleRequest<ModuleRequest, Struct> {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_VERIFIER(ModuleRequest)

  template <typename IsolateT>
  static Handle<ModuleRequest> New(IsolateT* isolate, Handle<String> specifier,
                                   Handle<FixedArray> import_attributes,
                                   int position);

  // The number of entries in the import_attributes FixedArray that are used for
  // a single attribute.
  static const size_t kAttributeEntrySize = 3;

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(ModuleRequest)
};

class SourceTextModuleInfoEntry
    : public TorqueGeneratedSourceTextModuleInfoEntry<SourceTextModuleInfoEntry,
                                                      Struct> {
 public:
  DECL_VERIFIER(SourceTextModuleInfoEntry)

  template <typename IsolateT>
  static Handle<SourceTextModuleInfoEntry> New(
      IsolateT* isolate, Handle<PrimitiveHeapObject> export_name,
      Handle<PrimitiveHeapObject> local_name,
      Handle<PrimitiveHeapObject> import_name, int module_request,
      int cell_index, int beg_pos, int end_pos);

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(SourceTextModuleInfoEntry)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SOURCE_TEXT_MODULE_H_
