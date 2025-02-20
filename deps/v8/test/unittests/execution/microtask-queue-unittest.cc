// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/microtask-queue.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "include/v8-function.h"
#include "src/heap/factory.h"
#include "src/objects/foreign.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/promise-inl.h"
#include "src/objects/visitors.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using Closure = std::function<void()>;

void RunStdFunction(void* data) {
  std::unique_ptr<Closure> f(static_cast<Closure*>(data));
  (*f)();
}

template <typename TMixin>
class WithFinalizationRegistryMixin : public TMixin {
 public:
  WithFinalizationRegistryMixin() = default;
  ~WithFinalizationRegistryMixin() override = default;
  WithFinalizationRegistryMixin(const WithFinalizationRegistryMixin&) = delete;
  WithFinalizationRegistryMixin& operator=(
      const WithFinalizationRegistryMixin&) = delete;

  static void SetUpTestSuite() {
    CHECK_NULL(save_flags_);
    save_flags_ = new SaveFlags();
    v8_flags.expose_gc = true;
    v8_flags.allow_natives_syntax = true;
    TMixin::SetUpTestSuite();
  }

  static void TearDownTestSuite() {
    TMixin::TearDownTestSuite();
    CHECK_NOT_NULL(save_flags_);
    delete save_flags_;
    save_flags_ = nullptr;
  }

 private:
  static SaveFlags* save_flags_;
};

template <typename TMixin>
SaveFlags* WithFinalizationRegistryMixin<TMixin>::save_flags_ = nullptr;

using TestWithNativeContextAndFinalizationRegistry =  //
    WithInternalIsolateMixin<                         //
        WithContextMixin<                             //
            WithFinalizationRegistryMixin<            //
                WithIsolateScopeMixin<                //
                    WithIsolateMixin<                 //
                        WithDefaultPlatformMixin<     //
                            ::testing::Test>>>>>>;

namespace {

void DummyPromiseHook(PromiseHookType type, Local<Promise> promise,
                      Local<Value> parent) {}

}  // namespace

class MicrotaskQueueTest : public TestWithNativeContextAndFinalizationRegistry,
                           public ::testing::WithParamInterface<bool> {
 public:
  template <typename F>
  DirectHandle<Microtask> NewMicrotask(F&& f) {
    DirectHandle<Foreign> runner = factory()->NewForeign<kMicrotaskCallbackTag>(
        reinterpret_cast<Address>(&RunStdFunction));
    DirectHandle<Foreign> data =
        factory()->NewForeign<kMicrotaskCallbackDataTag>(
            reinterpret_cast<Address>(new Closure(std::forward<F>(f))));
    return factory()->NewCallbackTask(runner, data);
  }

  void SetUp() override {
    microtask_queue_ = MicrotaskQueue::New(isolate());
    native_context()->set_microtask_queue(isolate(), microtask_queue());

    if (GetParam()) {
      // Use a PromiseHook to switch the implementation to ResolvePromise
      // runtime, instead of ResolvePromise builtin.
      v8_isolate()->SetPromiseHook(&DummyPromiseHook);
    }
  }

  void TearDown() override {
    if (microtask_queue()) {
      microtask_queue()->RunMicrotasks(isolate());
      context()->DetachGlobal();
    }
  }

  MicrotaskQueue* microtask_queue() const { return microtask_queue_.get(); }

  void ClearTestMicrotaskQueue() {
    context()->DetachGlobal();
    microtask_queue_ = nullptr;
  }

  template <size_t N>
  DirectHandle<Name> NameFromChars(const char (&chars)[N]) {
    return isolate()->factory()->NewStringFromStaticChars(chars);
  }

 private:
  std::unique_ptr<MicrotaskQueue> microtask_queue_;
};

class RecordingVisitor : public RootVisitor {
 public:
  RecordingVisitor() = default;
  ~RecordingVisitor() override = default;

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot current = start; current != end; ++current) {
      visited_.push_back(*current);
    }
  }

  const std::vector<Tagged<Object>>& visited() const { return visited_; }

 private:
  std::vector<Tagged<Object>> visited_;
};

// Sanity check. Ensure a microtask is stored in a queue and run.
TEST_P(MicrotaskQueueTest, EnqueueAndRun) {
  bool ran = false;
  EXPECT_EQ(0, microtask_queue()->capacity());
  EXPECT_EQ(0, microtask_queue()->size());
  microtask_queue()->EnqueueMicrotask(*NewMicrotask([this, &ran] {
    EXPECT_FALSE(ran);
    ran = true;
    EXPECT_TRUE(microtask_queue()->HasMicrotasksSuppressions());
  }));
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(1, microtask_queue()->size());
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_TRUE(ran);
  EXPECT_EQ(0, microtask_queue()->size());
}

// Check for a buffer growth.
TEST_P(MicrotaskQueueTest, BufferGrowth) {
  int count = 0;

  // Enqueue and flush the queue first to have non-zero |start_|.
  microtask_queue()->EnqueueMicrotask(
      *NewMicrotask([&count] { EXPECT_EQ(0, count++); }));
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));

  EXPECT_LT(0, microtask_queue()->capacity());
  EXPECT_EQ(0, microtask_queue()->size());
  EXPECT_EQ(1, microtask_queue()->start());

  // Fill the queue with Microtasks.
  for (int i = 1; i <= MicrotaskQueue::kMinimumCapacity; ++i) {
    microtask_queue()->EnqueueMicrotask(
        *NewMicrotask([&count, i] { EXPECT_EQ(i, count++); }));
  }
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity, microtask_queue()->size());

  // Add another to grow the ring buffer.
  microtask_queue()->EnqueueMicrotask(*NewMicrotask(
      [&] { EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1, count++); }));

  EXPECT_LT(MicrotaskQueue::kMinimumCapacity, microtask_queue()->capacity());
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1, microtask_queue()->size());

  // Run all pending Microtasks to ensure they run in the proper order.
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 1,
            microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity + 2, count);
}

// MicrotaskQueue instances form a doubly linked list.
TEST_P(MicrotaskQueueTest, InstanceChain) {
  ClearTestMicrotaskQueue();

  MicrotaskQueue* default_mtq = isolate()->default_microtask_queue();
  ASSERT_TRUE(default_mtq);
  EXPECT_EQ(default_mtq, default_mtq->next());
  EXPECT_EQ(default_mtq, default_mtq->prev());

  // Create two instances, and check their connection.
  // The list contains all instances in the creation order, and the next of the
  // last instance is the first instance:
  //   default_mtq -> mtq1 -> mtq2 -> default_mtq.
  std::unique_ptr<MicrotaskQueue> mtq1 = MicrotaskQueue::New(isolate());
  std::unique_ptr<MicrotaskQueue> mtq2 = MicrotaskQueue::New(isolate());
  EXPECT_EQ(default_mtq->next(), mtq1.get());
  EXPECT_EQ(mtq1->next(), mtq2.get());
  EXPECT_EQ(mtq2->next(), default_mtq);
  EXPECT_EQ(default_mtq, mtq1->prev());
  EXPECT_EQ(mtq1.get(), mtq2->prev());
  EXPECT_EQ(mtq2.get(), default_mtq->prev());

  // Deleted item should be also removed from the list.
  mtq1 = nullptr;
  EXPECT_EQ(default_mtq->next(), mtq2.get());
  EXPECT_EQ(mtq2->next(), default_mtq);
  EXPECT_EQ(default_mtq, mtq2->prev());
  EXPECT_EQ(mtq2.get(), default_mtq->prev());
}

// Pending Microtasks in MicrotaskQueues are strong roots. Ensure they are
// visited exactly once.
TEST_P(MicrotaskQueueTest, VisitRoot) {
  // Ensure that the ring buffer has separate in-use region.
  for (int i = 0; i < MicrotaskQueue::kMinimumCapacity / 2 + 1; ++i) {
    microtask_queue()->EnqueueMicrotask(*NewMicrotask([] {}));
  }
  EXPECT_EQ(MicrotaskQueue::kMinimumCapacity / 2 + 1,
            microtask_queue()->RunMicrotasks(isolate()));

  std::vector<Tagged<Object>> expected;
  for (int i = 0; i < MicrotaskQueue::kMinimumCapacity / 2 + 1; ++i) {
    DirectHandle<Microtask> microtask = NewMicrotask([] {});
    expected.push_back(*microtask);
    microtask_queue()->EnqueueMicrotask(*microtask);
  }
  EXPECT_GT(microtask_queue()->start() + microtask_queue()->size(),
            microtask_queue()->capacity());

  RecordingVisitor visitor;
  microtask_queue()->IterateMicrotasks(&visitor);

  std::vector<Tagged<Object>> actual = visitor.visited();
  std::sort(expected.begin(), expected.end());
  std::sort(actual.begin(), actual.end());
  EXPECT_EQ(expected, actual);
}

TEST_P(MicrotaskQueueTest, PromiseHandlerContext) {
  microtask_queue()->set_microtasks_policy(MicrotasksPolicy::kExplicit);
  Local<v8::Context> v8_context2 = v8::Context::New(v8_isolate());
  Local<v8::Context> v8_context3 = v8::Context::New(v8_isolate());
  Local<v8::Context> v8_context4 = v8::Context::New(v8_isolate());
  DirectHandle<Context> context2 =
      Utils::OpenDirectHandle(*v8_context2, isolate());
  DirectHandle<Context> context3 =
      Utils::OpenDirectHandle(*v8_context3, isolate());
  DirectHandle<Context> context4 =
      Utils::OpenDirectHandle(*v8_context3, isolate());
  context2->native_context()->set_microtask_queue(isolate(), microtask_queue());
  context3->native_context()->set_microtask_queue(isolate(), microtask_queue());
  context4->native_context()->set_microtask_queue(isolate(), microtask_queue());

  DirectHandle<JSFunction> handler;
  DirectHandle<JSProxy> proxy;
  DirectHandle<JSProxy> revoked_proxy;
  DirectHandle<JSBoundFunction> bound;

  // Create a JSFunction on |context2|
  {
    v8::Context::Scope scope(v8_context2);
    handler = RunJS<JSFunction>("()=>{}");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(handler).ToHandleChecked());
  }

  // Create a JSProxy on |context3|.
  {
    v8::Context::Scope scope(v8_context3);
    ASSERT_TRUE(
        v8_context3->Global()
            ->Set(v8_context3, NewString("handler"), Utils::ToLocal(handler))
            .FromJust());
    proxy = RunJS<JSProxy>("new Proxy(handler, {})");
    revoked_proxy = RunJS<JSProxy>(
        "let {proxy, revoke} = Proxy.revocable(handler, {});"
        "revoke();"
        "proxy");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(proxy).ToHandleChecked());
    EXPECT_TRUE(JSReceiver::GetContextForMicrotask(revoked_proxy).is_null());
  }

  // Create a JSBoundFunction on |context4|.
  // Note that its CreationContext and ContextForTaskCancellation is |context2|.
  {
    v8::Context::Scope scope(v8_context4);
    ASSERT_TRUE(
        v8_context4->Global()
            ->Set(v8_context4, NewString("handler"), Utils::ToLocal(handler))
            .FromJust());
    bound = RunJS<JSBoundFunction>("handler.bind()");
    EXPECT_EQ(*context2,
              *JSReceiver::GetContextForMicrotask(bound).ToHandleChecked());
  }

  // Give the objects to the main context.
  SetGlobalProperty("handler", Utils::ToLocal(handler));
  SetGlobalProperty("proxy", Utils::ToLocal(proxy));
  SetGlobalProperty("revoked_proxy", Utils::ToLocal(revoked_proxy));
  SetGlobalProperty("bound", Utils::ToLocal(Cast<JSReceiver>(bound)));
  RunJS(
      "Promise.resolve().then(handler);"
      "Promise.reject().catch(proxy);"
      "Promise.resolve().then(revoked_proxy);"
      "Promise.resolve().then(bound);");

  ASSERT_EQ(4, microtask_queue()->size());
  DirectHandle<Microtask> microtask1(microtask_queue()->get(0), isolate());
  ASSERT_TRUE(IsPromiseFulfillReactionJobTask(*microtask1));
  EXPECT_EQ(*context2,
            Cast<PromiseFulfillReactionJobTask>(microtask1)->context());

  DirectHandle<Microtask> microtask2(microtask_queue()->get(1), isolate());
  ASSERT_TRUE(IsPromiseRejectReactionJobTask(*microtask2));
  EXPECT_EQ(*context2,
            Cast<PromiseRejectReactionJobTask>(microtask2)->context());

  DirectHandle<Microtask> microtask3(microtask_queue()->get(2), isolate());
  ASSERT_TRUE(IsPromiseFulfillReactionJobTask(*microtask3));
  // |microtask3| corresponds to a PromiseReaction for |revoked_proxy|.
  // As |revoked_proxy| doesn't have a context, the current context should be
  // used as the fallback context.
  EXPECT_EQ(*native_context(),
            Cast<PromiseFulfillReactionJobTask>(microtask3)->context());

  DirectHandle<Microtask> microtask4(microtask_queue()->get(3), isolate());
  ASSERT_TRUE(IsPromiseFulfillReactionJobTask(*microtask4));
  EXPECT_EQ(*context2,
            Cast<PromiseFulfillReactionJobTask>(microtask4)->context());

  v8_context4->DetachGlobal();
  v8_context3->DetachGlobal();
  v8_context2->DetachGlobal();
}

TEST_P(MicrotaskQueueTest, DetachGlobal_Enqueue) {
  EXPECT_EQ(0, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // No microtask should be enqueued after DetachGlobal call.
  EXPECT_EQ(0, microtask_queue()->size());
  RunJS("Promise.resolve().then(()=>{})");
  EXPECT_EQ(0, microtask_queue()->size());
}

TEST_P(MicrotaskQueueTest, DetachGlobal_Run) {
  microtask_queue()->set_microtasks_policy(MicrotasksPolicy::kExplicit);
  EXPECT_EQ(0, microtask_queue()->size());

  // Enqueue microtasks to the current context.
  DirectHandle<JSArray> ran = RunJS<JSArray>(
      "var ran = [false, false, false, false];"
      "Promise.resolve().then(() => { ran[0] = true; });"
      "Promise.reject().catch(() => { ran[1] = true; });"
      "ran");

  DirectHandle<JSFunction> function =
      RunJS<JSFunction>("(function() { ran[2] = true; })");
  DirectHandle<CallableTask> callable =
      factory()->NewCallableTask(function, Utils::OpenDirectHandle(*context()));
  microtask_queue()->EnqueueMicrotask(*callable);

  // The handler should not run at this point.
  const int kNumExpectedTasks = 3;
  for (int i = 0; i < kNumExpectedTasks; ++i) {
    EXPECT_TRUE(
        IsFalse(*Object::GetElement(isolate(), ran, i).ToHandleChecked()));
  }
  EXPECT_EQ(kNumExpectedTasks, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // RunMicrotasks processes pending Microtasks, but Microtasks that are
  // associated to a detached context should be cancelled and should not take
  // effect.
  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_EQ(0, microtask_queue()->size());
  for (int i = 0; i < kNumExpectedTasks; ++i) {
    EXPECT_TRUE(
        IsFalse(*Object::GetElement(isolate(), ran, i).ToHandleChecked()));
  }
}

TEST_P(MicrotaskQueueTest, DetachGlobal_PromiseResolveThenableJobTask) {
  microtask_queue()->set_microtasks_policy(MicrotasksPolicy::kExplicit);
  RunJS(
      "var resolve;"
      "var promise = new Promise(r => { resolve = r; });"
      "promise.then(() => {});"
      "resolve({});");

  // A PromiseResolveThenableJobTask is pending in the MicrotaskQueue.
  EXPECT_EQ(1, microtask_queue()->size());

  // Detach MicrotaskQueue from the current context.
  context()->DetachGlobal();

  // RunMicrotasks processes the pending Microtask, but Microtasks that are
  // associated to a detached context should be cancelled and should not take
  // effect.
  // As PromiseResolveThenableJobTask queues another task for resolution,
  // the return value is 2 if it ran.
  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_EQ(0, microtask_queue()->size());
}

TEST_P(MicrotaskQueueTest, DetachGlobal_ResolveThenableForeignThen) {
  microtask_queue()->set_microtasks_policy(MicrotasksPolicy::kExplicit);
  DirectHandle<JSArray> result = RunJS<JSArray>(
      "let result = [false];"
      "result");
  DirectHandle<JSFunction> then =
      RunJS<JSFunction>("() => { result[0] = true; }");

  DirectHandle<JSPromise> stale_promise;

  {
    // Create a context with its own microtask queue.
    std::unique_ptr<MicrotaskQueue> sub_microtask_queue =
        MicrotaskQueue::New(isolate());
    sub_microtask_queue->set_microtasks_policy(MicrotasksPolicy::kExplicit);
    Local<v8::Context> sub_context = v8::Context::New(
        v8_isolate(),
        /* extensions= */ nullptr,
        /* global_template= */ MaybeLocal<ObjectTemplate>(),
        /* global_object= */ MaybeLocal<Value>(),
        /* internal_fields_deserializer= */ DeserializeInternalFieldsCallback(),
        sub_microtask_queue.get());

    {
      v8::Context::Scope scope(sub_context);
      CHECK(sub_context->Global()
                ->Set(sub_context, NewString("then"),
                      Utils::ToLocal(Cast<JSReceiver>(then)))
                .FromJust());

      ASSERT_EQ(0, microtask_queue()->size());
      ASSERT_EQ(0, sub_microtask_queue->size());
      ASSERT_TRUE(
          IsFalse(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));

      // With a regular thenable, a microtask is queued on the sub-context.
      RunJS<JSPromise>("Promise.resolve({ then: cb => cb(1) })");
      EXPECT_EQ(0, microtask_queue()->size());
      EXPECT_EQ(1, sub_microtask_queue->size());
      EXPECT_TRUE(
          IsFalse(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));

      // But when the `then` method comes from another context, a microtask is
      // instead queued on the main context.
      stale_promise = RunJS<JSPromise>("Promise.resolve({ then })");
      EXPECT_EQ(1, microtask_queue()->size());
      EXPECT_EQ(1, sub_microtask_queue->size());
      EXPECT_TRUE(
          IsFalse(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));
    }

    sub_context->DetachGlobal();
  }

  EXPECT_EQ(1, microtask_queue()->size());
  EXPECT_TRUE(
      IsFalse(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));

  EXPECT_EQ(1, microtask_queue()->RunMicrotasks(isolate()));
  EXPECT_EQ(0, microtask_queue()->size());
  EXPECT_TRUE(
      IsTrue(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));
}

TEST_P(MicrotaskQueueTest, DetachGlobal_HandlerContext) {
  // EnqueueMicrotask should use the context associated to the handler instead
  // of the current context. E.g.
  //   // At Context A.
  //   let resolved = Promise.resolve();
  //   // Call DetachGlobal on A, so that microtasks associated to A is
  //   // cancelled.
  //
  //   // At Context B.
  //   let handler = () => {
  //     console.log("here");
  //   };
  //   // The microtask to run |handler| should be associated to B instead of A,
  //   // so that handler runs even |resolved| is on the detached context A.
  //   resolved.then(handler);

  DirectHandle<JSReceiver> results =
      isolate()->factory()->NewJSObjectWithNullProto();

  // These belong to a stale Context.
  DirectHandle<JSPromise> stale_resolved_promise;
  DirectHandle<JSPromise> stale_rejected_promise;
  DirectHandle<JSReceiver> stale_handler;

  Local<v8::Context> sub_context = v8::Context::New(v8_isolate());
  {
    v8::Context::Scope scope(sub_context);
    stale_resolved_promise = RunJS<JSPromise>("Promise.resolve()");
    stale_rejected_promise = RunJS<JSPromise>("Promise.reject()");
    stale_handler = RunJS<JSReceiver>(
        "(results, label) => {"
        "  results[label] = true;"
        "}");
  }
  // DetachGlobal() cancels all microtasks associated to the context.
  sub_context->DetachGlobal();
  sub_context.Clear();

  SetGlobalProperty("results", Utils::ToLocal(results));
  SetGlobalProperty("stale_resolved_promise",
                    Utils::ToLocal(Cast<JSReceiver>(stale_resolved_promise)));
  SetGlobalProperty("stale_rejected_promise",
                    Utils::ToLocal(Cast<JSReceiver>(stale_rejected_promise)));
  SetGlobalProperty("stale_handler", Utils::ToLocal(stale_handler));

  // Set valid handlers to stale promises.
  RunJS(
      "stale_resolved_promise.then(() => {"
      "  results['stale_resolved_promise'] = true;"
      "})");
  RunJS(
      "stale_rejected_promise.catch(() => {"
      "  results['stale_rejected_promise'] = true;"
      "})");
  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_TRUE(JSReceiver::HasProperty(isolate(), results,
                                      NameFromChars("stale_resolved_promise"))
                  .FromJust());
  EXPECT_TRUE(JSReceiver::HasProperty(isolate(), results,
                                      NameFromChars("stale_rejected_promise"))
                  .FromJust());

  // Set stale handlers to valid promises.
  RunJS(
      "Promise.resolve("
      "    stale_handler.bind(null, results, 'stale_handler_resolve'))");
  RunJS(
      "Promise.reject("
      "    stale_handler.bind(null, results, 'stale_handler_reject'))");
  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_FALSE(JSReceiver::HasProperty(isolate(), results,
                                       NameFromChars("stale_handler_resolve"))
                   .FromJust());
  EXPECT_FALSE(JSReceiver::HasProperty(isolate(), results,
                                       NameFromChars("stale_handler_reject"))
                   .FromJust());
}

TEST_P(MicrotaskQueueTest, DetachGlobal_Chain) {
  DirectHandle<JSPromise> stale_rejected_promise;

  Local<v8::Context> sub_context = v8::Context::New(v8_isolate());
  {
    v8::Context::Scope scope(sub_context);
    stale_rejected_promise = RunJS<JSPromise>("Promise.reject()");
  }
  sub_context->DetachGlobal();
  sub_context.Clear();

  SetGlobalProperty("stale_rejected_promise",
                    Utils::ToLocal(Cast<JSReceiver>(stale_rejected_promise)));
  DirectHandle<JSArray> result = RunJS<JSArray>(
      "let result = [false];"
      "stale_rejected_promise"
      "  .then(() => {})"
      "  .catch(() => {"
      "    result[0] = true;"
      "  });"
      "result");
  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_TRUE(
      IsTrue(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));
}

TEST_P(MicrotaskQueueTest, DetachGlobal_InactiveHandler) {
  Local<v8::Context> sub_context = v8::Context::New(v8_isolate());
  Utils::OpenDirectHandle(*sub_context)
      ->native_context()
      ->set_microtask_queue(isolate(), microtask_queue());

  DirectHandle<JSArray> result;
  DirectHandle<JSFunction> stale_handler;
  DirectHandle<JSPromise> stale_promise;
  {
    v8::Context::Scope scope(sub_context);
    result = RunJS<JSArray>("var result = [false, false]; result");
    stale_handler = RunJS<JSFunction>("() => { result[0] = true; }");
    stale_promise = RunJS<JSPromise>(
        "var stale_promise = new Promise(()=>{});"
        "stale_promise");
    RunJS("stale_promise.then(() => { result [1] = true; });");
  }
  sub_context->DetachGlobal();
  sub_context.Clear();

  // The context of |stale_handler| and |stale_promise| is detached at this
  // point.
  // Ensure that resolution handling for |stale_handler| is cancelled without
  // crash. Also, the resolution of |stale_promise| is also cancelled.

  SetGlobalProperty("stale_handler", Utils::ToLocal(stale_handler));
  RunJS("%EnqueueMicrotask(stale_handler)");

  v8_isolate()->EnqueueMicrotask(Utils::ToLocal(stale_handler));

  JSPromise::Fulfill(
      stale_promise,
      direct_handle(ReadOnlyRoots(isolate()).undefined_value(), isolate()));

  microtask_queue()->RunMicrotasks(isolate());
  EXPECT_TRUE(
      IsFalse(*Object::GetElement(isolate(), result, 0).ToHandleChecked()));
  EXPECT_TRUE(
      IsFalse(*Object::GetElement(isolate(), result, 1).ToHandleChecked()));
}

TEST_P(MicrotaskQueueTest, MicrotasksScope) {
  ASSERT_NE(isolate()->default_microtask_queue(), microtask_queue());
  microtask_queue()->set_microtasks_policy(MicrotasksPolicy::kScoped);

  bool ran = false;
  {
    MicrotasksScope scope(v8_isolate(), microtask_queue(),
                          MicrotasksScope::kRunMicrotasks);
    microtask_queue()->EnqueueMicrotask(*NewMicrotask([&ran]() {
      EXPECT_FALSE(ran);
      ran = true;
    }));
  }
  EXPECT_TRUE(ran);
}

INSTANTIATE_TEST_SUITE_P(
    , MicrotaskQueueTest, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<MicrotaskQueueTest::ParamType>& info) {
      return info.param ? "runtime" : "builtin";
    });

}  // namespace internal
}  // namespace v8
