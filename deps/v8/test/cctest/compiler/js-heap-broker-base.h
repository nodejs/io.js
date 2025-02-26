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
  IndirectHandle<T> Create(Tagged<T> object) {
    CHECK_NOT_NULL(canonical_handles_);
    auto find_result = canonical_handles_->FindOrInsert(object);
    if (!find_result.already_exists) {
      *find_result.entry = IndirectHandle<T>(object, isolate_).location();
    }
    return IndirectHandle<T>(*find_result.entry);
  }

  template <typename T>
  IndirectHandle<T> Create(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return Create(Tagged<T>(object));
  }

  template <typename T>
  IndirectHandle<T> Create(IndirectHandle<T> handle) {
    return Create(*handle);
  }

  template <typename T>
  IndirectHandle<T> Create(DirectHandle<T> handle) {
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
    if (!PersistentHandlesScope::IsActive(isolate)) {
      persistent_scope_.emplace(isolate);
    }
  }

  JSHeapBrokerTestBase(Isolate* isolate, Zone* zone, CanonicalHandles&& handles)
      : broker_(isolate, zone),
        broker_scope_(&broker_, handles.Detach()),
        current_broker_(&broker_) {
    if (!PersistentHandlesScope::IsActive(isolate)) {
      persistent_scope_.emplace(isolate);
    }
  }

  ~JSHeapBrokerTestBase() {
    if (persistent_scope_) {
      persistent_scope_->Detach();
    }
  }

  JSHeapBroker* broker() { return &broker_; }

  template <typename T>
  IndirectHandle<T> CanonicalHandle(Tagged<T> object) {
    return broker()->CanonicalPersistentHandle(object);
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return CanonicalHandle(Tagged<T>(object));
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(IndirectHandle<T> handle) {
    return CanonicalHandle(*handle);
  }
  template <typename T>
  IndirectHandle<T> CanonicalHandle(DirectHandle<T> handle) {
    return CanonicalHandle(*handle);
  }

 private:
  JSHeapBroker broker_;
  JSHeapBrokerScopeForTesting broker_scope_;
  CurrentHeapBrokerScope current_broker_;
  std::optional<PersistentHandlesScope> persistent_scope_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_JS_HEAP_BROKER_H_
