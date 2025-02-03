// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SYNTHETIC_MODULE_H_
#define V8_OBJECTS_SYNTHETIC_MODULE_H_

#include "src/objects/module.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/synthetic-module-tq.inc"

// The runtime representation of a Synthetic Module Record, a module that can be
// instantiated by an embedder with embedder-defined exports and evaluation
// steps.
// https://heycam.github.io/webidl/#synthetic-module-records
class SyntheticModule
    : public TorqueGeneratedSyntheticModule<SyntheticModule, Module> {
 public:
  NEVER_READ_ONLY_SPACE
  DECL_VERIFIER(SyntheticModule)

  // Set module's exported value for the specified export_name to the specified
  // export_value.  An error will be thrown if export_name is not one
  // of the export_names that were supplied during module construction.
  // Returns Just(true) on success, Nothing<bool>() if an error was thrown.
  static Maybe<bool> SetExport(Isolate* isolate,
                               DirectHandle<SyntheticModule> module,
                               Handle<String> export_name,
                               DirectHandle<Object> export_value);
  // The following redundant method should be deleted when the deprecated
  // version of v8::SetSyntheticModuleExport is removed.  It differs from
  // SetExport in that it crashes rather than throwing an error if the caller
  // attempts to set an export_name that was not present during construction of
  // the module.
  static void SetExportStrict(Isolate* isolate,
                              DirectHandle<SyntheticModule> module,
                              Handle<String> export_name,
                              DirectHandle<Object> export_value);

  using BodyDescriptor =
      SubclassBodyDescriptor<Module::BodyDescriptor,
                             FixedBodyDescriptor<kNameOffset, kSize, kSize>>;

 private:
  friend class Module;

  static V8_WARN_UNUSED_RESULT MaybeHandle<Cell> ResolveExport(
      Isolate* isolate, DirectHandle<SyntheticModule> module,
      Handle<String> module_specifier, Handle<String> export_name,
      MessageLocation loc, bool must_resolve);

  static V8_WARN_UNUSED_RESULT bool PrepareInstantiate(
      Isolate* isolate, DirectHandle<SyntheticModule> module,
      v8::Local<v8::Context> context);
  static V8_WARN_UNUSED_RESULT bool FinishInstantiate(
      Isolate* isolate, DirectHandle<SyntheticModule> module);

  static V8_WARN_UNUSED_RESULT MaybeHandle<Object> Evaluate(
      Isolate* isolate, Handle<SyntheticModule> module);

  TQ_OBJECT_CONSTRUCTORS(SyntheticModule)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SYNTHETIC_MODULE_H_
