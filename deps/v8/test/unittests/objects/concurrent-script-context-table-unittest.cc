// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/local-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/objects/contexts.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using ConcurrentScriptContextTableTest = TestWithContext;

namespace internal {

namespace {

std::atomic<int> g_initialized_entries;

class ScriptContextTableAccessUsedThread final : public v8::base::Thread {
 public:
  ScriptContextTableAccessUsedThread(
      Isolate* isolate, Heap* heap, base::Semaphore* sema_started,
      std::unique_ptr<PersistentHandles> ph,
      Handle<ScriptContextTable> script_context_table)
      : v8::base::Thread(
            base::Thread::Options("ScriptContextTableAccessUsedThread")),
        heap_(heap),
        sema_started_(sema_started),
        ph_(std::move(ph)),
        script_context_table_(script_context_table) {}

  void Run() override {
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope unparked_scope(&local_heap);
    LocalHandleScope scope(&local_heap);

    sema_started_->Signal();

    for (int i = 0; i < script_context_table_->length(kAcquireLoad); ++i) {
      Tagged<Context> context = script_context_table_->get(i);
      EXPECT_TRUE(context->IsScriptContext());
    }
  }

 private:
  Heap* heap_;
  base::Semaphore* sema_started_;
  std::unique_ptr<PersistentHandles> ph_;
  Handle<ScriptContextTable> script_context_table_;
};

class AccessScriptContextTableThread final : public v8::base::Thread {
 public:
  AccessScriptContextTableThread(Isolate* isolate, Heap* heap,
                                 base::Semaphore* sema_started,
                                 std::unique_ptr<PersistentHandles> ph,
                                 Handle<NativeContext> native_context)
      : v8::base::Thread(
            base::Thread::Options("AccessScriptContextTableThread")),
        heap_(heap),
        sema_started_(sema_started),
        ph_(std::move(ph)),
        native_context_(native_context) {}

  void Run() override {
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope unparked_scope(&local_heap);
    LocalHandleScope scope(&local_heap);

    sema_started_->Signal();

    for (int i = 0; i < 1000; ++i) {
      // Read upper bound with relaxed semantics to not add any ordering
      // constraints.
      while (i >= g_initialized_entries.load(std::memory_order_relaxed)) {
      }
      auto script_context_table = Handle<ScriptContextTable>(
          native_context_->synchronized_script_context_table(), &local_heap);
      Handle<Context> context(script_context_table->get(i), &local_heap);
      EXPECT_TRUE(!context.is_null());
    }
  }

 private:
  Heap* heap_;
  base::Semaphore* sema_started_;
  std::unique_ptr<PersistentHandles> ph_;
  Handle<NativeContext> native_context_;
};

TEST_F(ConcurrentScriptContextTableTest, ScriptContextTable_Extend) {
  v8::HandleScope scope(isolate());
  const bool kIgnoreDuplicateNames = true;

  Factory* factory = i_isolate()->factory();
  Handle<NativeContext> native_context = factory->NewNativeContext();
  Handle<Map> script_context_map = factory->NewContextfulMap(
      native_context, SCRIPT_CONTEXT_TYPE, kVariableSizeSentinel);
  script_context_map->set_native_context(*native_context);
  native_context->set_script_context_map(*script_context_map);

  Handle<ScriptContextTable> script_context_table =
      factory->NewScriptContextTable();

  Handle<ScopeInfo> scope_info =
      ReadOnlyRoots(i_isolate()).global_this_binding_scope_info_handle();

  for (int i = 0; i < 10; ++i) {
    Handle<Context> script_context =
        factory->NewScriptContext(native_context, scope_info);

    script_context_table =
        ScriptContextTable::Add(i_isolate(), script_context_table,
                                script_context, kIgnoreDuplicateNames);
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();
  Handle<ScriptContextTable> persistent_script_context_table =
      ph->NewHandle(script_context_table);

  base::Semaphore sema_started(0);

  auto thread = std::make_unique<ScriptContextTableAccessUsedThread>(
      i_isolate(), i_isolate()->heap(), &sema_started, std::move(ph),
      persistent_script_context_table);

  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  for (int i = 0; i < 100; ++i) {
    Handle<Context> context =
        factory->NewScriptContext(native_context, scope_info);
    script_context_table = ScriptContextTable::Add(
        i_isolate(), script_context_table, context, kIgnoreDuplicateNames);
  }

  thread->Join();
}

TEST_F(ConcurrentScriptContextTableTest,
       ScriptContextTable_AccessScriptContextTable) {
  v8::HandleScope scope(isolate());

  Factory* factory = i_isolate()->factory();
  Handle<NativeContext> native_context = factory->NewNativeContext();
  Handle<Map> script_context_map = factory->NewContextfulMap(
      native_context, SCRIPT_CONTEXT_TYPE, kVariableSizeSentinel);
  script_context_map->set_native_context(*native_context);
  native_context->set_script_context_map(*script_context_map);

  Handle<ScopeInfo> scope_info =
      ReadOnlyRoots(i_isolate()).global_this_binding_scope_info_handle();

  Handle<ScriptContextTable> script_context_table =
      factory->NewScriptContextTable();
  Handle<Context> context =
      factory->NewScriptContext(native_context, scope_info);
  script_context_table = ScriptContextTable::Add(
      i_isolate(), script_context_table, context, false);
  int initialized_entries = 1;
  g_initialized_entries.store(initialized_entries, std::memory_order_release);

  native_context->set_script_context_table(*script_context_table);
  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();
  Handle<NativeContext> persistent_native_context =
      ph->NewHandle(native_context);

  base::Semaphore sema_started(0);

  auto thread = std::make_unique<AccessScriptContextTableThread>(
      i_isolate(), i_isolate()->heap(), &sema_started, std::move(ph),
      persistent_native_context);

  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  const bool kIgnoreDuplicateNames = true;
  for (; initialized_entries < 1000; ++initialized_entries) {
    Handle<Context> new_context =
        factory->NewScriptContext(native_context, scope_info);
    script_context_table = ScriptContextTable::Add(
        i_isolate(), script_context_table, new_context, kIgnoreDuplicateNames);
    native_context->synchronized_set_script_context_table(
        *script_context_table);
    // Update with relaxed semantics to not introduce ordering constraints.
    g_initialized_entries.store(initialized_entries, std::memory_order_relaxed);
  }
  g_initialized_entries.store(initialized_entries, std::memory_order_relaxed);

  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
