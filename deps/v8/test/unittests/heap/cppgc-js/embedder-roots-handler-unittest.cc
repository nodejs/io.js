// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-embedder-heap.h"
#include "src/handles/handles.h"
#include "src/handles/traced-handles.h"
#include "test/unittests/heap/cppgc-js/unified-heap-utils.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8::internal {

namespace {

using EmbedderRootsHandlerTest = TestWithHeapInternalsAndContext;

class V8_NODISCARD TemporaryEmbedderRootsHandleScope final {
 public:
  TemporaryEmbedderRootsHandleScope(v8::Isolate* isolate,
                                    v8::EmbedderRootsHandler* handler)
      : isolate_(isolate) {
    isolate_->SetEmbedderRootsHandler(handler);
  }

  ~TemporaryEmbedderRootsHandleScope() {
    isolate_->SetEmbedderRootsHandler(nullptr);
  }

 private:
  v8::Isolate* const isolate_;
};

// EmbedderRootsHandler that can optimize Scavenger handling when used with
// TracedReference.
class ClearingEmbedderRootsHandler final : public v8::EmbedderRootsHandler {
 public:
  ClearingEmbedderRootsHandler()
      : EmbedderRootsHandler(EmbedderRootsHandler::RootHandling::
                                 kQueryEmbedderForNonDroppableReferences) {}

  bool IsRoot(const v8::TracedReference<v8::Value>& handle) final {
    // Convention for test: Objects that are optimized have their first field
    // set as a back pointer. The `handle` passed here may be different from the
    // original handle, so we cannot check equality against `&handle`.
    return v8::Object::GetAlignedPointerFromInternalField(
               handle.As<v8::Object>(), 0) == nullptr;
  }

  void ResetRoot(const v8::TracedReference<v8::Value>& handle) final {
    CHECK(!IsRoot(handle));
    // Convention for test: Objects that are optimized have their first field
    // set as a back pointer.
    BasicTracedReference<v8::Value>* original_handle =
        reinterpret_cast<BasicTracedReference<v8::Value>*>(
            v8::Object::GetAlignedPointerFromInternalField(
                handle.As<v8::Object>(), 0));
    original_handle->Reset();
  }
};

template <typename T>
void SetupOptimizedAndNonOptimizedHandle(v8::Isolate* isolate,
                                         T* optimized_handle,
                                         T* non_optimized_handle) {
  v8::HandleScope scope(isolate);

  v8::Local<v8::Object> optimized_object = WrapperHelper::CreateWrapper(
      isolate->GetCurrentContext(), optimized_handle, nullptr);
  EXPECT_TRUE(optimized_handle->IsEmpty());
  *optimized_handle = T(isolate, optimized_object);
  EXPECT_FALSE(optimized_handle->IsEmpty());

  v8::Local<v8::Object> non_optimized_object = WrapperHelper::CreateWrapper(
      isolate->GetCurrentContext(), nullptr, nullptr);
  EXPECT_TRUE(non_optimized_handle->IsEmpty());
  *non_optimized_handle = T(isolate, non_optimized_object);
  EXPECT_FALSE(non_optimized_handle->IsEmpty());
}

}  // namespace

TEST_F(EmbedderRootsHandlerTest,
       TracedReferenceNoDestructorReclaimedOnScavenge) {
  if (v8_flags.single_generation) return;

  ManualGCScope manual_gc(i_isolate());
  v8::HandleScope scope(v8_isolate());

  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());

  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);

  auto* traced_handles = i_isolate()->traced_handles();
  const size_t initial_count = traced_handles->used_node_count();
  auto* optimized_handle = new v8::TracedReference<v8::Value>();
  auto* non_optimized_handle = new v8::TracedReference<v8::Value>();
  SetupOptimizedAndNonOptimizedHandle(v8_isolate(), optimized_handle,
                                      non_optimized_handle);
  EXPECT_EQ(initial_count + 2, traced_handles->used_node_count());
  InvokeMinorGC();
  EXPECT_EQ(initial_count + 1, traced_handles->used_node_count());
  EXPECT_TRUE(optimized_handle->IsEmpty());
  delete optimized_handle;
  EXPECT_FALSE(non_optimized_handle->IsEmpty());
  non_optimized_handle->Reset();
  delete non_optimized_handle;
  EXPECT_EQ(initial_count, traced_handles->used_node_count());
}

namespace {

void ConstructJSObject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::TracedReference<v8::Object>* handle) {
  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> object(v8::Object::New(isolate));
  EXPECT_FALSE(object.IsEmpty());
  *handle = v8::TracedReference<v8::Object>(isolate, object);
  EXPECT_FALSE(handle->IsEmpty());
}

template <typename T>
void ConstructJSApiObject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          T* global) {
  v8::HandleScope scope(isolate);
  v8::Local<v8::Object> object =
      WrapperHelper::CreateWrapper(context, nullptr, nullptr);
  EXPECT_FALSE(object.IsEmpty());
  *global = T(isolate, object);
  EXPECT_FALSE(global->IsEmpty());
}

enum class SurvivalMode { kSurvives, kDies };

template <typename ModifierFunction, typename ConstructTracedReferenceFunction,
          typename GCFunction>
void TracedReferenceTest(v8::Isolate* isolate,
                         ConstructTracedReferenceFunction construct_function,
                         ModifierFunction modifier_function,
                         GCFunction gc_function, SurvivalMode survives) {
  auto i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate->heap());
  v8::HandleScope scope(isolate);
  auto* traced_handles = i_isolate->traced_handles();
  const size_t initial_count = traced_handles->used_node_count();
  auto gc_invisible_handle =
      std::make_unique<v8::TracedReference<v8::Object>>();
  construct_function(isolate, isolate->GetCurrentContext(),
                     gc_invisible_handle.get());
  ASSERT_TRUE(IsNewObjectInCorrectGeneration(isolate, *gc_invisible_handle));
  modifier_function(*gc_invisible_handle);
  const size_t after_modification_count = traced_handles->used_node_count();
  gc_function();
  // Cannot check the handle as it is not explicitly cleared by the GC. Instead
  // check the handles count.
  CHECK_IMPLIES(survives == SurvivalMode::kSurvives,
                after_modification_count == traced_handles->used_node_count());
  CHECK_IMPLIES(survives == SurvivalMode::kDies,
                initial_count == traced_handles->used_node_count());
}

}  // namespace

// EmbedderRootsHandler does not affect full GCs.
TEST_F(EmbedderRootsHandlerTest,
       TracedReferenceToUnmodifiedJSObjectDiesOnFullGC) {
  // When stressing incremental marking, a write barrier may keep the object
  // alive.
  if (v8_flags.stress_incremental_marking) return;

  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  TracedReferenceTest(
      v8_isolate(), ConstructJSObject,
      [](const TracedReference<v8::Object>&) {}, [this]() { InvokeMajorGC(); },
      SurvivalMode::kDies);
}

// EmbedderRootsHandler does not affect full GCs.
TEST_F(
    EmbedderRootsHandlerTest,
    TracedReferenceToUnmodifiedJSObjectDiesOnFullGCEvenWhenPointeeIsHeldAlive) {
  ManualGCScope manual_gcs(i_isolate());
  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  // The TracedReference itself will die as it's not found by the full GC. The
  // pointee will be kept alive through other means.
  v8::Global<v8::Object> strong_global;
  TracedReferenceTest(
      v8_isolate(), ConstructJSObject,
      [this, &strong_global](const TracedReference<v8::Object>& handle) {
        v8::HandleScope scope(v8_isolate());
        strong_global =
            v8::Global<v8::Object>(v8_isolate(), handle.Get(v8_isolate()));
      },
      [this, &strong_global]() {
        InvokeMajorGC();
        strong_global.Reset();
      },
      SurvivalMode::kDies);
}

// EmbedderRootsHandler does not affect non-API objects.
TEST_F(EmbedderRootsHandlerTest,
       TracedReferenceToUnmodifiedJSObjectSurvivesYoungGC) {
  if (v8_flags.single_generation) return;

  ManualGCScope manual_gc(i_isolate());
  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  TracedReferenceTest(
      v8_isolate(), ConstructJSObject,
      [](const TracedReference<v8::Object>&) {}, [this]() { InvokeMinorGC(); },
      SurvivalMode::kSurvives);
}

// EmbedderRootsHandler does not affect non-API objects.
TEST_F(
    EmbedderRootsHandlerTest,
    TracedReferenceToUnmodifiedJSObjectSurvivesYoungGCWhenExcludedFromRoots) {
  if (v8_flags.single_generation) return;

  ManualGCScope manual_gc(i_isolate());
  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  TracedReferenceTest(
      v8_isolate(), ConstructJSObject,
      [](TracedReference<v8::Object>& handle) {}, [this]() { InvokeMinorGC(); },
      SurvivalMode::kSurvives);
}

// EmbedderRootsHandler does not affect API objects for handles that have
// their class ids not set up.
TEST_F(EmbedderRootsHandlerTest,
       TracedReferenceToUnmodifiedJSApiObjectSurvivesScavengePerDefault) {
  if (v8_flags.single_generation) return;

  ManualGCScope manual_gc(i_isolate());
  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  TracedReferenceTest(
      v8_isolate(), ConstructJSApiObject<TracedReference<v8::Object>>,
      [](const TracedReference<v8::Object>&) {}, [this]() { InvokeMinorGC(); },
      SurvivalMode::kSurvives);
}

// EmbedderRootsHandler resets API objects for handles that have their class ids
// set to being optimized.
TEST_F(
    EmbedderRootsHandlerTest,
    TracedReferenceToUnmodifiedJSApiObjectDiesOnScavengeWhenExcludedFromRoots) {
  if (v8_flags.single_generation) return;

  ManualGCScope manual_gc(i_isolate());
  ClearingEmbedderRootsHandler handler;
  TemporaryEmbedderRootsHandleScope roots_handler_scope(v8_isolate(), &handler);
  TracedReferenceTest(
      v8_isolate(), ConstructJSApiObject<TracedReference<v8::Object>>,
      [this](TracedReference<v8::Object>& handle) {
        {
          HandleScope handles(i_isolate());
          auto local = handle.Get(v8_isolate());
          local->SetAlignedPointerInInternalField(0, &handle);
        }
      },
      [this]() { InvokeMinorGC(); }, SurvivalMode::kDies);
}

}  // namespace v8::internal
