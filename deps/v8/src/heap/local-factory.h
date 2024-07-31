// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_FACTORY_H_
#define V8_HEAP_LOCAL_FACTORY_H_

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/heap/factory-base.h"
#include "src/heap/heap.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/spaces.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class AstValueFactory;
class AstRawString;
class AstConsString;
class LocalIsolate;

class V8_EXPORT_PRIVATE LocalFactory : public FactoryBase<LocalFactory> {
 public:
  explicit LocalFactory(Isolate* isolate);

  ReadOnlyRoots read_only_roots() const { return roots_; }

#define ROOT_ACCESSOR(Type, name, CamelName) inline Handle<Type> name();
  // AccessorInfos appear mutable, but they're actually not mutated once they
  // finish initializing. In particular, the root accessors are not mutated and
  // are safe to access (as long as the off-thread job doesn't try to mutate
  // them).
  ACCESSOR_INFO_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  // The parser shouldn't allow the LocalFactory to get into a state where
  // it generates errors.
  Handle<Object> NewInvalidStringLengthError() { UNREACHABLE(); }
  Handle<Object> NewRangeError(MessageTemplate template_index) {
    UNREACHABLE();
  }

  // The LocalFactory does not have access to the number_string_cache (since
  // it's a mutable root), but it still needs to define some cache-related
  // method that are used by FactoryBase. Those method do basically nothing in
  // the case of the LocalFactory.
  int NumberToStringCacheHash(Tagged<Smi> number);
  int NumberToStringCacheHash(double number);
  void NumberToStringCacheSet(DirectHandle<Object> number, int hash,
                              DirectHandle<String> js_string);
  Handle<Object> NumberToStringCacheGet(Tagged<Object> number, int hash);

 private:
  friend class FactoryBase<LocalFactory>;

  // ------
  // Customization points for FactoryBase.
  Tagged<HeapObject> AllocateRaw(
      int size, AllocationType allocation,
      AllocationAlignment alignment = kTaggedAligned);

  LocalIsolate* isolate() {
    // Downcast to the privately inherited sub-class using c-style casts to
    // avoid undefined behavior (as static_cast cannot cast across private
    // bases).
    // NOLINTNEXTLINE (google-readability-casting)
    return (LocalIsolate*)this;  // NOLINT(readability/casting)
  }

  inline bool CanAllocateInReadOnlySpace() { return false; }
  inline bool EmptyStringRootIsInitialized() { return true; }
  inline AllocationType AllocationTypeForInPlaceInternalizableString();
  // ------

  void ProcessNewScript(DirectHandle<Script> script,
                        ScriptEventType script_event_type);
  // ------

  ReadOnlyRoots roots_;
#ifdef DEBUG
  bool a_script_was_added_to_the_script_list_ = false;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_FACTORY_H_
