// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope-inl.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using DirectHandlesTest = TestWithIsolate;

TEST_F(DirectHandlesTest, CreateDirectHandleFromLocal) {
  HandleScope scope(isolate());
  Local<String> foo = String::NewFromUtf8Literal(isolate(), "foo");

  i::DirectHandle<i::String> direct = Utils::OpenDirectHandle(*foo);
  i::IndirectHandle<i::String> handle = Utils::OpenIndirectHandle(*foo);

  EXPECT_EQ(*direct, *handle);
}

TEST_F(DirectHandlesTest, CreateLocalFromDirectHandle) {
  HandleScope scope(isolate());
  i::Handle<i::String> handle =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::DirectHandle<i::String> direct = handle;

  Local<String> l1 = Utils::ToLocal(direct);
  Local<String> l2 = Utils::ToLocal(handle);

  EXPECT_EQ(l1, l2);
}

TEST_F(DirectHandlesTest, CreateMaybeDirectHandle) {
  HandleScope scope(isolate());
  i::Handle<i::String> handle =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::DirectHandle<i::String> direct = handle;

  i::MaybeDirectHandle<i::String> maybe_direct(direct);
  i::MaybeHandle<i::String> maybe_handle(handle);

  EXPECT_EQ(*maybe_direct.ToHandleChecked(), *maybe_handle.ToHandleChecked());
}

TEST_F(DirectHandlesTest, CreateMaybeDirectObjectHandle) {
  HandleScope scope(isolate());
  i::Handle<i::String> handle =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::DirectHandle<i::String> direct = handle;

  i::MaybeObjectDirectHandle maybe_direct(direct);
  i::MaybeObjectHandle maybe_handle(handle);

  EXPECT_EQ(*maybe_direct, *maybe_handle);
}

TEST_F(DirectHandlesTest, IsIdenticalTo) {
  i::DirectHandle<i::String> d1 =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::DirectHandle<i::String> d2(d1);

  i::DirectHandle<i::String> d3 =
      i_isolate()->factory()->NewStringFromAsciiChecked("bar");
  i::DirectHandle<i::String> d4;
  i::DirectHandle<i::String> d5;

  EXPECT_TRUE(d1.is_identical_to(d2));
  EXPECT_TRUE(d2.is_identical_to(d1));
  EXPECT_FALSE(d1.is_identical_to(d3));
  EXPECT_FALSE(d1.is_identical_to(d4));
  EXPECT_FALSE(d4.is_identical_to(d1));
  EXPECT_TRUE(d4.is_identical_to(d5));
}

TEST_F(DirectHandlesTest, MaybeObjectDirectHandleIsIdenticalTo) {
  i::DirectHandle<i::String> foo =
      i_isolate()->factory()->NewStringFromAsciiChecked("foo");
  i::DirectHandle<i::String> bar =
      i_isolate()->factory()->NewStringFromAsciiChecked("bar");

  i::MaybeObjectDirectHandle d1(foo);
  i::MaybeObjectDirectHandle d2(foo);
  i::MaybeObjectDirectHandle d3(bar);
  i::MaybeObjectDirectHandle d4;
  i::MaybeObjectDirectHandle d5;

  EXPECT_TRUE(d1.is_identical_to(d2));
  EXPECT_TRUE(d2.is_identical_to(d1));
  EXPECT_FALSE(d1.is_identical_to(d3));
  EXPECT_FALSE(d1.is_identical_to(d4));
  EXPECT_FALSE(d4.is_identical_to(d1));
  EXPECT_TRUE(d4.is_identical_to(d5));
}

// Tests to check DirectHandle usage.
// Such usage violations are only detected in debug builds, with the
// compile-time flag for enabling direct handles.

#if defined(DEBUG) && defined(V8_ENABLE_DIRECT_HANDLE)

namespace {
template <typename Callback>
void ExpectFailure(Callback callback) {
  EXPECT_DEATH_IF_SUPPORTED(callback(), "");
}
}  // anonymous namespace

TEST_F(DirectHandlesTest, DirectHandleOutOfStackFails) {
  // Out-of-stack allocation of direct handles should fail.
  ExpectFailure([]() {
    auto ptr = std::make_unique<i::DirectHandle<i::String>>();
    USE(ptr);
  });
}

namespace {
class BackgroundThread final : public v8::base::Thread {
 public:
  explicit BackgroundThread(i::Isolate* isolate, bool park_and_wait)
      : v8::base::Thread(base::Thread::Options("BackgroundThread")),
        isolate_(isolate),
        park_and_wait_(park_and_wait) {}

  void Run() override {
    i::LocalIsolate isolate(isolate_, i::ThreadKind::kBackground);
    i::UnparkedScope unparked_scope(&isolate);
    i::LocalHandleScope handle_scope(&isolate);
    // Using a direct handle when unparked is allowed.
    i::DirectHandle<i::String> direct = isolate.factory()->empty_string();
    // Park and wait, if we must.
    if (park_and_wait_) {
      // Parking a background thread through the trampoline while holding a
      // direct handle is also allowed.
      isolate.heap()->ExecuteWhileParked([]() {
        // nothing
      });
    }
    // Keep the direct handle alive.
    CHECK_EQ(0, direct->length());
  }

 private:
  i::Isolate* isolate_;
  bool park_and_wait_;
};
}  // anonymous namespace

TEST_F(DirectHandlesTest, DirectHandleInBackgroundThread) {
  i::LocalHeap lh(i_isolate()->heap(), i::ThreadKind::kMain);
  lh.SetUpMainThreadForTesting();
  auto thread = std::make_unique<BackgroundThread>(i_isolate(), false);
  CHECK(thread->Start());
  thread->Join();
}

TEST_F(DirectHandlesTest, DirectHandleInParkedBackgroundThread) {
  i::LocalHeap lh(i_isolate()->heap(), i::ThreadKind::kMain);
  lh.SetUpMainThreadForTesting();
  auto thread = std::make_unique<BackgroundThread>(i_isolate(), true);
  CHECK(thread->Start());
  thread->Join();
}

#if V8_CAN_CREATE_SHARED_HEAP_BOOL

using DirectHandlesSharedTest = i::TestJSSharedMemoryWithIsolate;

namespace {
class ClientThread final : public i::ParkingThread {
 public:
  ClientThread() : ParkingThread(base::Thread::Options("ClientThread")) {}

  void Run() override {
    IsolateWrapper isolate_wrapper(kNoCounters);
    // Direct handles can be used in the main thread of client isolates.
    i::DirectHandle<i::String> direct;
    USE(direct);
  }
};
}  // anonymous namespace

TEST_F(DirectHandlesSharedTest, DirectHandleInClient) {
  auto thread = std::make_unique<ClientThread>();
  CHECK(thread->Start());
  thread->ParkedJoin(i_isolate()->main_thread_local_isolate());
}

namespace {
class ClientMainThread final : public i::ParkingThread {
 public:
  explicit ClientMainThread(bool background_park_and_wait)
      : ParkingThread(base::Thread::Options("ClientMainThread")),
        background_park_and_wait_(background_park_and_wait) {}

  void Run() override {
    IsolateWrapper isolate_wrapper(kNoCounters);
    i::Isolate* i_client_isolate =
        reinterpret_cast<i::Isolate*>(isolate_wrapper.isolate());

    i::LocalHeap lh(i_client_isolate->heap(), i::ThreadKind::kMain);
    lh.SetUpMainThreadForTesting();
    auto thread = std::make_unique<BackgroundThread>(i_client_isolate,
                                                     background_park_and_wait_);
    CHECK(thread->Start());
    thread->Join();
  }

 private:
  bool background_park_and_wait_;
};
}  // anonymous namespace

TEST_F(DirectHandlesSharedTest, DirectHandleInClientBackgroundThread) {
  auto thread = std::make_unique<ClientMainThread>(false);
  CHECK(thread->Start());
  thread->ParkedJoin(i_isolate()->main_thread_local_isolate());
}

TEST_F(DirectHandlesSharedTest, DirectHandleInParkedClientBackgroundThread) {
  auto thread = std::make_unique<ClientMainThread>(true);
  CHECK(thread->Start());
  thread->ParkedJoin(i_isolate()->main_thread_local_isolate());
}

#endif  // V8_CAN_CREATE_SHARED_HEAP_BOOL
#endif  // DEBUG && V8_ENABLE_DIRECT_HANDLE

}  // namespace v8
