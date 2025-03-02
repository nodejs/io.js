// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MODULE_H_
#define V8_OBJECTS_MODULE_H_

#include "include/v8-script.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class JSModuleNamespace;
class SourceTextModuleDescriptor;
class SourceTextModuleInfo;
class SourceTextModuleInfoEntry;
class Zone;
template <typename T>
class ZoneForwardList;

#include "torque-generated/src/objects/module-tq.inc"

// Module is the base class for ECMAScript module types, roughly corresponding
// to Abstract Module Record.
// https://tc39.github.io/ecma262/#sec-abstract-module-records
class Module : public TorqueGeneratedModule<Module, HeapObject> {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_VERIFIER(Module)
  DECL_PRINTER(Module)

  enum Status {
    // Order matters!
    kUnlinked,
    kPreLinking,
    kLinking,
    kLinked,
    kEvaluating,
    kEvaluatingAsync,
    kEvaluated,
    kErrored
  };

#ifdef DEBUG
  static const char* StatusString(Module::Status status);
#endif  // DEBUG

  // The exception in the case {status} is kErrored.
  Tagged<Object> GetException();

  // Returns if this module or any transitively requested module is [[Async]],
  // i.e. has a top-level await.
  V8_WARN_UNUSED_RESULT bool IsGraphAsync(Isolate* isolate) const;

  // Implementation of spec operation ModuleDeclarationInstantiation.
  // Returns false if an exception occurred during instantiation, true
  // otherwise. (In the case where the callback throws an exception, that
  // exception is propagated.)
  static V8_WARN_UNUSED_RESULT bool Instantiate(
      Isolate* isolate, Handle<Module> module, v8::Local<v8::Context> context,
      v8::Module::ResolveModuleCallback module_callback,
      v8::Module::ResolveSourceCallback source_callback);

  // Implementation of spec operation ModuleEvaluation.
  static V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> Evaluate(
      Isolate* isolate, Handle<Module> module);

  // Get the namespace object for [module].  If it doesn't exist yet, it is
  // created.
  static DirectHandle<JSModuleNamespace> GetModuleNamespace(
      Isolate* isolate, Handle<Module> module);

  using BodyDescriptor =
      FixedBodyDescriptor<kExportsOffset, kHeaderSize, kHeaderSize>;

  struct Hash;

 protected:
  friend class Factory;

  // The [must_resolve] argument indicates whether or not an exception should be
  // thrown in case the module does not provide an export named [name]
  // (including when a cycle is detected).  An exception is always thrown in the
  // case of conflicting star exports.
  //
  // If [must_resolve] is true, a null result indicates an exception. If
  // [must_resolve] is false, a null result may or may not indicate an
  // exception (so check manually!).
  class ResolveSet;
  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveExport(
      Isolate* isolate, Handle<Module> module,
      DirectHandle<String> module_specifier, Handle<String> export_name,
      MessageLocation loc, bool must_resolve, ResolveSet* resolve_set);

  static V8_WARN_UNUSED_RESULT bool PrepareInstantiate(
      Isolate* isolate, DirectHandle<Module> module,
      v8::Local<v8::Context> context,
      v8::Module::ResolveModuleCallback module_callback,
      v8::Module::ResolveSourceCallback source_callback);
  static V8_WARN_UNUSED_RESULT bool FinishInstantiate(
      Isolate* isolate, Handle<Module> module,
      ZoneForwardList<Handle<SourceTextModule>>* stack, unsigned* dfs_index,
      Zone* zone);

  // Set module's status back to kUnlinked and reset other internal state.
  // This is used when instantiation fails.
  static void Reset(Isolate* isolate, DirectHandle<Module> module);
  static void ResetGraph(Isolate* isolate, DirectHandle<Module> module);

  // To set status to kErrored, RecordError should be used.
  void SetStatus(Status status);
  void RecordError(Isolate* isolate, Tagged<Object> error);

  TQ_OBJECT_CONSTRUCTORS(Module)
};

// When importing a module namespace (import * as foo from "bar"), a
// JSModuleNamespace object (representing module "bar") is created and bound to
// the declared variable (foo).  A module can have at most one namespace object.
class JSModuleNamespace
    : public TorqueGeneratedJSModuleNamespace<JSModuleNamespace,
                                              JSSpecialObject> {
 public:
  DECL_PRINTER(JSModuleNamespace)

  // Retrieve the value exported by [module] under the given [name]. If there is
  // no such export, return Just(undefined). If the export is uninitialized,
  // schedule an exception and return Nothing.
  V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> GetExport(
      Isolate* isolate, DirectHandle<String> name);

  bool HasExport(Isolate* isolate, DirectHandle<String> name);

  // Return the (constant) property attributes for the referenced property,
  // which is assumed to correspond to an export. If the export is
  // uninitialized, schedule an exception and return Nothing.
  static V8_WARN_UNUSED_RESULT Maybe<PropertyAttributes> GetPropertyAttributes(
      LookupIterator* it);

  static V8_WARN_UNUSED_RESULT Maybe<bool> DefineOwnProperty(
      Isolate* isolate, DirectHandle<JSModuleNamespace> o,
      DirectHandle<Object> key, PropertyDescriptor* desc,
      Maybe<ShouldThrow> should_throw);

  // In-object fields.
  enum {
    kToStringTagFieldIndex,
    kInObjectFieldCount,
  };

  // We need to include in-object fields
  // TODO(v8:8944): improve handling of in-object fields
  static constexpr int kSize =
      kHeaderSize + (kTaggedSize * kInObjectFieldCount);

  TQ_OBJECT_CONSTRUCTORS(JSModuleNamespace)
};

class ScriptOrModule
    : public TorqueGeneratedScriptOrModule<ScriptOrModule, Struct> {
 public:
  DECL_PRINTER(ScriptOrModule)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(ScriptOrModule)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_MODULE_H_
