#include <cppgc/allocation.h>
#include <cppgc/garbage-collected.h>
#include <cppgc/heap.h>
#include <node.h>
#include <v8-cppgc.h>
#include <v8-sandbox.h>
#include <v8.h>
#include "node_test_fixture.h"

// This tests that Node.js can work with an existing CppHeap.

// Mimic a class that does not know about Node.js.
class CppGCed : public cppgc::GarbageCollected<CppGCed> {
 public:
  static int kConstructCount;
  static int kDestructCount;
  static int kTraceCount;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Object> js_object = args.This();
    auto* heap = isolate->GetCppHeap();
    CHECK_NOT_NULL(heap);
    CppGCed* gc_object =
        cppgc::MakeGarbageCollected<CppGCed>(heap->GetAllocationHandle());
    node::SetCppgcReference(isolate, js_object, gc_object);
    kConstructCount++;
    args.GetReturnValue().Set(js_object);
  }

  static v8::Local<v8::Function> GetConstructor(
      v8::Local<v8::Context> context) {
    auto ft = v8::FunctionTemplate::New(context->GetIsolate(), New);
    return ft->GetFunction(context).ToLocalChecked();
  }

  CppGCed() = default;

  ~CppGCed() { kDestructCount++; }

  void Trace(cppgc::Visitor* visitor) const { kTraceCount++; }
};

int CppGCed::kConstructCount = 0;
int CppGCed::kDestructCount = 0;
int CppGCed::kTraceCount = 0;

TEST_F(NodeZeroIsolateTestFixture, ExistingCppHeapTest) {
  v8::Isolate* isolate =
      node::NewIsolate(allocator.get(), &current_loop, platform.get());

  // Create and attach the CppHeap before we set up the IsolateData so that
  // it recognizes the existing heap.
  std::unique_ptr<v8::CppHeap> cpp_heap =
      v8::CppHeap::Create(platform.get(), v8::CppHeapCreateParams{{}});

  // TODO(joyeecheung): pass it into v8::Isolate::CreateParams and let V8
  // own it when we can keep the isolate registered/task runner discoverable
  // during isolate disposal.
  isolate->AttachCppHeap(cpp_heap.get());

  // Try creating Context + IsolateData + Environment.
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    std::unique_ptr<node::IsolateData, decltype(&node::FreeIsolateData)>
        isolate_data{
            node::CreateIsolateData(isolate, &current_loop, platform.get()),
            node::FreeIsolateData};
    CHECK(isolate_data);

    {
      auto context = node::NewContext(isolate);
      CHECK(!context.IsEmpty());
      v8::Context::Scope context_scope(context);

      // An Environment should already contain a few BaseObjects that are
      // supposed to have non-cppgc IDs.
      std::unique_ptr<node::Environment, decltype(&node::FreeEnvironment)>
          environment{
              node::CreateEnvironment(isolate_data.get(), context, {}, {}),
              node::FreeEnvironment};
      CHECK(environment);

      context->Global()
          ->Set(context,
                v8::String::NewFromUtf8(isolate, "CppGCed").ToLocalChecked(),
                CppGCed::GetConstructor(context))
          .FromJust();

      const char* code =
          "globalThis.array = [];"
          "for (let i = 0; i < 100; ++i) { array.push(new CppGCed()); }";
      node::LoadEnvironment(environment.get(), code).ToLocalChecked();
      // Request a GC and check if CppGCed::Trace() is invoked.
      isolate->LowMemoryNotification();
      int exit_code = SpinEventLoop(environment.get()).FromJust();
      EXPECT_EQ(exit_code, 0);
    }

    platform->DrainTasks(isolate);

    // Cleanup.
    isolate->DetachCppHeap();
    cpp_heap->Terminate();
    platform->DrainTasks(isolate);
  }

  platform->UnregisterIsolate(isolate);
  isolate->Dispose();

  // Check that all the objects are created and destroyed properly.
  EXPECT_EQ(CppGCed::kConstructCount, 100);
  EXPECT_EQ(CppGCed::kDestructCount, 100);
  // GC does not have to feel pressured enough to visit all of them to
  // reclaim memory before we are done with the isolate and the entire
  // heap can be reclaimed. So just check at least some of them are traced.
  EXPECT_GT(CppGCed::kTraceCount, 0);
}
