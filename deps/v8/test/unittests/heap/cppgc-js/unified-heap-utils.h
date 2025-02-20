// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_HEAP_CPPGC_JS_UNIFIED_HEAP_UTILS_H_
#define V8_UNITTESTS_HEAP_CPPGC_JS_UNIFIED_HEAP_UTILS_H_

#include "include/cppgc/heap.h"
#include "include/v8-cppgc.h"
#include "include/v8-local-handle.h"
#include "src/objects/js-objects.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8 {

class CppHeap;

namespace internal {

class CppHeap;

template <typename TMixin>
class WithUnifiedHeap : public TMixin {
 public:
  WithUnifiedHeap() = default;

  ~WithUnifiedHeap() override = default;

  void CollectGarbageWithEmbedderStack(cppgc::Heap::SweepingType sweeping_type =
                                           cppgc::Heap::SweepingType::kAtomic) {
    EmbedderStackStateScope stack_scope(
        TMixin::heap(), EmbedderStackStateOrigin::kExplicitInvocation,
        StackState::kMayContainHeapPointers);
    TMixin::InvokeMajorGC();
    if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
      cpp_heap().AsBase().sweeper().FinishIfRunning();
    }
  }

  void CollectGarbageWithoutEmbedderStack(
      cppgc::Heap::SweepingType sweeping_type =
          cppgc::Heap::SweepingType::kAtomic) {
    EmbedderStackStateScope stack_scope(
        TMixin::heap(), EmbedderStackStateOrigin::kExplicitInvocation,
        StackState::kNoHeapPointers);
    TMixin::InvokeMajorGC();
    if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
      cpp_heap().AsBase().sweeper().FinishIfRunning();
    }
  }

  void CollectYoungGarbageWithEmbedderStack(
      cppgc::Heap::SweepingType sweeping_type =
          cppgc::Heap::SweepingType::kAtomic) {
    EmbedderStackStateScope stack_scope(
        TMixin::heap(), EmbedderStackStateOrigin::kExplicitInvocation,
        StackState::kMayContainHeapPointers);
    TMixin::InvokeMinorGC();
    if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
      cpp_heap().AsBase().sweeper().FinishIfRunning();
    }
  }

  void CollectYoungGarbageWithoutEmbedderStack(
      cppgc::Heap::SweepingType sweeping_type =
          cppgc::Heap::SweepingType::kAtomic) {
    EmbedderStackStateScope stack_scope(
        TMixin::heap(), EmbedderStackStateOrigin::kExplicitInvocation,
        StackState::kNoHeapPointers);
    TMixin::InvokeMinorGC();
    if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
      cpp_heap().AsBase().sweeper().FinishIfRunning();
    }
  }

  CppHeap& cpp_heap() const {
    return *CppHeap::From(TMixin::isolate()->heap()->cpp_heap());
  }

  cppgc::AllocationHandle& allocation_handle() {
    return cpp_heap().object_allocator();
  }
};

using UnifiedHeapTest = WithUnifiedHeap<TestWithHeapInternalsAndContext>;

// Helpers for managed wrappers using a single header field.
class WrapperHelper {
 public:
  // Sets up a V8 API object so that it points back to a C++ object. The setup
  // used is recognized by the GC and references will be followed for liveness
  // analysis (marking) as well as tooling (snapshot).
  static v8::Local<v8::Object> CreateWrapper(v8::Local<v8::Context> context,
                                             void* wrappable_object,
                                             const char* class_name = nullptr);

  // Resets the connection of a wrapper (JS) to its wrappable (C++), meaning
  // that the wrappable object is not longer kept alive by the wrapper object.
  static void ResetWrappableConnection(v8::Isolate* isolate,
                                       v8::Local<v8::Object> api_object);

  // Sets up the connection of a wrapper (JS) to its wrappable (C++). Does not
  // emit any possibly needed write barrier.
  static void SetWrappableConnection(v8::Isolate* isolate,
                                     v8::Local<v8::Object> api_object, void*);

  template <typename T>
  static T* UnwrapAs(v8::Isolate* isolate, v8::Local<v8::Object> api_object) {
    return reinterpret_cast<T*>(ReadWrappablePointer(isolate, api_object));
  }

 private:
  static void* ReadWrappablePointer(v8::Isolate* isolate,
                                    v8::Local<v8::Object> api_object);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_HEAP_CPPGC_JS_UNIFIED_HEAP_UTILS_H_
