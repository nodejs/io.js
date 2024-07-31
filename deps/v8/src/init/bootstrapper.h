// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INIT_BOOTSTRAPPER_H_
#define V8_INIT_BOOTSTRAPPER_H_

#include "include/v8-context.h"
#include "include/v8-local-handle.h"
#include "include/v8-snapshot.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/visitors.h"
#include "src/snapshot/serializer-deserializer.h"

namespace v8 {
namespace internal {

// A SourceCodeCache uses a FixedArray to store pairs of (OneByteString,
// SharedFunctionInfo), mapping names of native extensions code files to
// precompiled functions.
class SourceCodeCache final {
 public:
  explicit SourceCodeCache(Script::Type type) : type_(type) {}
  SourceCodeCache(const SourceCodeCache&) = delete;
  SourceCodeCache& operator=(const SourceCodeCache&) = delete;

  void Initialize(Isolate* isolate, bool create_heap_objects);

  void Iterate(RootVisitor* v);

  bool Lookup(Isolate* isolate, base::Vector<const char> name,
              Handle<SharedFunctionInfo>* handle);

  void Add(Isolate* isolate, base::Vector<const char> name,
           Handle<SharedFunctionInfo> shared);

 private:
  Script::Type type_;
  Tagged<FixedArray> cache_;
};

// The Boostrapper is the public interface for creating a JavaScript global
// context.
class Bootstrapper final {
 public:
  Bootstrapper(const Bootstrapper&) = delete;
  Bootstrapper& operator=(const Bootstrapper&) = delete;

  static void InitializeOncePerProcess();

  // Requires: Heap::SetUp has been called.
  void Initialize(bool create_heap_objects);
  void TearDown();

  // Creates a JavaScript Global Context with initial object graph.
  // The returned value is a global handle casted to V8Environment*.
  Handle<NativeContext> CreateEnvironment(
      MaybeHandle<JSGlobalProxy> maybe_global_proxy,
      v8::Local<v8::ObjectTemplate> global_object_template,
      v8::ExtensionConfiguration* extensions, size_t context_snapshot_index,
      DeserializeEmbedderFieldsCallback embedder_fields_deserializer,
      v8::MicrotaskQueue* microtask_queue);

  // Used for testing context deserialization. No code runs in the generated
  // context. It only needs to pass heap verification.
  Handle<NativeContext> CreateEnvironmentForTesting() {
    MaybeHandle<JSGlobalProxy> no_global_proxy;
    v8::Local<v8::ObjectTemplate> no_global_object_template;
    ExtensionConfiguration no_extensions;
    static constexpr int kDefaultContextIndex = 0;
    DeserializeEmbedderFieldsCallback no_callback;
    v8::MicrotaskQueue* no_microtask_queue = nullptr;
    return CreateEnvironment(no_global_proxy, no_global_object_template,
                             &no_extensions, kDefaultContextIndex, no_callback,
                             no_microtask_queue);
  }

  Handle<JSGlobalProxy> NewRemoteContext(
      MaybeHandle<JSGlobalProxy> maybe_global_proxy,
      v8::Local<v8::ObjectTemplate> global_object_template);

  // Traverses the pointers for memory management.
  void Iterate(RootVisitor* v);

  // Tells whether bootstrapping is active.
  bool IsActive() const { return nesting_ != 0; }

  // Support for thread preemption.
  static int ArchiveSpacePerThread();
  char* ArchiveState(char* to);
  char* RestoreState(char* from);
  void FreeThreadResources();

  // Used for new context creation.
  bool InstallExtensions(Handle<NativeContext> native_context,
                         v8::ExtensionConfiguration* extensions);

  SourceCodeCache* extensions_cache() { return &extensions_cache_; }

 private:
  // Log newly created Map objects if no snapshot was used.
  void LogAllMaps();

  Isolate* isolate_;
  using NestingCounterType = int;
  NestingCounterType nesting_;
  SourceCodeCache extensions_cache_;

  friend class BootstrapperActive;
  friend class Isolate;
  friend class NativesExternalStringResource;

  explicit Bootstrapper(Isolate* isolate);
};

class BootstrapperActive final {
 public:
  explicit BootstrapperActive(Bootstrapper* bootstrapper)
      : bootstrapper_(bootstrapper) {
    ++bootstrapper_->nesting_;
  }
  BootstrapperActive(const BootstrapperActive&) = delete;
  BootstrapperActive& operator=(const BootstrapperActive&) = delete;

  ~BootstrapperActive() { --bootstrapper_->nesting_; }

 private:
  Bootstrapper* bootstrapper_;
};

V8_NOINLINE Handle<JSFunction> SimpleInstallFunction(
    Isolate* isolate, Handle<JSObject> base, const char* name, Builtin call,
    int len, bool adapt, PropertyAttributes attrs = DONT_ENUM);

}  // namespace internal
}  // namespace v8

#endif  // V8_INIT_BOOTSTRAPPER_H_
