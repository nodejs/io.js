// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_JS_HEAP_BROKER_H_
#define V8_CCTEST_COMPILER_JS_HEAP_BROKER_H_

namespace v8 {
namespace internal {
namespace compiler {

class CanonicalHandles {
 public:
  CanonicalHandles(Isolate* isolate, Zone* zone)
      : isolate_(isolate),
        canonical_handles_(std::make_unique<CanonicalHandlesMap>(
            isolate->heap(), ZoneAllocationPolicy(zone))) {}

  template <typename T>
  Handle<T> Create(Tagged<T> object) {
    CHECK_NOT_NULL(canonical_handles_);
    auto find_result = canonical_handles_->FindOrInsert(object);
    if (!find_result.already_exists) {
      *find_result.entry = Handle<T>(object, isolate_).location();
    }
    return Handle<T>(*find_result.entry);
  }

  template <typename T>
  Handle<T> Create(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return Create(Tagged<T>(object));
  }

  template <typename T>
  Handle<T> Create(Handle<T> handle) {
    return Create(*handle);
  }

  std::unique_ptr<CanonicalHandlesMap> Detach() {
    DCHECK_NOT_NULL(canonical_handles_);
    return std::move(canonical_handles_);
  }

 private:
  Isolate* isolate_;
  std::unique_ptr<CanonicalHandlesMap> canonical_handles_;
};

class JSHeapBrokerTestBase {
 public:
  JSHeapBrokerTestBase(Isolate* isolate, Zone* zone)
      : broker_(isolate, zone),
        broker_scope_(&broker_,
                      std::make_unique<CanonicalHandlesMap>(
                          isolate->heap(), ZoneAllocationPolicy(zone))),
        current_broker_(&broker_) {
    // PersistentHandlesScope currently requires an active handle before it can
    // be opened and they can't be nested.
    // TODO(v8:13897): Remove once PersistentHandlesScopes can be opened
    // uncontionally.
    if (!PersistentHandlesScope::IsActive(isolate)) {
      IndirectHandle<Object> dummy(
          ReadOnlyRoots(isolate->heap()).empty_string(), isolate);
      persistent_scope_ = std::make_unique<PersistentHandlesScope>(isolate);
    }
  }

  JSHeapBrokerTestBase(Isolate* isolate, Zone* zone, CanonicalHandles&& handles)
      : broker_(isolate, zone),
        broker_scope_(&broker_, handles.Detach()),
        current_broker_(&broker_) {
    // PersistentHandlesScope currently requires an active handle before it can
    // be opened and they can't be nested.
    // TODO(v8:13897): Remove once PersistentHandlesScopes can be opened
    // uncontionally.
    if (!PersistentHandlesScope::IsActive(isolate)) {
      IndirectHandle<Object> dummy(
          ReadOnlyRoots(isolate->heap()).empty_string(), isolate);
      persistent_scope_ = std::make_unique<PersistentHandlesScope>(isolate);
    }
  }

  ~JSHeapBrokerTestBase() {
    if (persistent_scope_ != nullptr) {
      persistent_scope_->Detach();
    }
  }

  JSHeapBroker* broker() { return &broker_; }

  template <typename T>
  Handle<T> CanonicalHandle(Tagged<T> object) {
    return broker()->CanonicalPersistentHandle(object);
  }
  template <typename T>
  Handle<T> CanonicalHandle(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return CanonicalHandle(Tagged<T>(object));
  }
  template <typename T>
  Handle<T> CanonicalHandle(Handle<T> handle) {
    return CanonicalHandle(*handle);
  }

 private:
  JSHeapBroker broker_;
  JSHeapBrokerScopeForTesting broker_scope_;
  CurrentHeapBrokerScope current_broker_;
  std::unique_ptr<PersistentHandlesScope> persistent_scope_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_JS_HEAP_BROKER_H_
