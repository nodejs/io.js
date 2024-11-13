// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-initialization.h"
#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/base/strings.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/parked-scope-inl.h"
#include "src/heap/remembered-set.h"
#include "src/heap/safepoint.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-weak-refs.h"
#include "src/objects/objects-inl.h"
#include "src/objects/string-forwarding-table-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace test_shared_strings {

struct V8_NODISCARD IsolateWrapper {
  explicit IsolateWrapper(v8::Isolate* isolate) : isolate(isolate) {}
  ~IsolateWrapper() { isolate->Dispose(); }
  v8::Isolate* const isolate;
};

// Some tests in this file allocate two Isolates in the same thread to directly
// test shared string behavior. Because both are considered running, when
// disposing these Isolates, one must be parked to not cause a deadlock in the
// shared heap verification that happens on client Isolate disposal.
struct V8_NODISCARD IsolateParkOnDisposeWrapper {
  IsolateParkOnDisposeWrapper(v8::Isolate* isolate,
                              v8::Isolate* isolate_to_park)
      : isolate(isolate), isolate_to_park(isolate_to_park) {}

  ~IsolateParkOnDisposeWrapper() {
    auto main_isolate = reinterpret_cast<Isolate*>(isolate_to_park)
                            ->main_thread_local_isolate();
    main_isolate->ExecuteMainThreadWhileParked(
        [this]() { isolate->Dispose(); });
  }

  v8::Isolate* const isolate;
  v8::Isolate* const isolate_to_park;
};

class MultiClientIsolateTest {
 public:
  MultiClientIsolateTest() {
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator.get();
    main_isolate_ = v8::Isolate::New(create_params);
    i_main_isolate()->Enter();
  }

  ~MultiClientIsolateTest() {
    i_main_isolate()->Exit();
    main_isolate_->Dispose();
  }

  v8::Isolate* main_isolate() const { return main_isolate_; }

  Isolate* i_main_isolate() const {
    return reinterpret_cast<Isolate*>(main_isolate_);
  }

  int& main_isolate_wakeup_counter() { return main_isolate_wakeup_counter_; }

  v8::Isolate* NewClientIsolate() {
    CHECK_NOT_NULL(main_isolate_);
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator.get();
    return v8::Isolate::New(create_params);
  }

 private:
  v8::Isolate* main_isolate_;
  int main_isolate_wakeup_counter_ = 0;
};

UNINITIALIZED_TEST(InPlaceInternalizableStringsAreShared) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Old generation 1- and 2-byte seq strings are in-place internalizable.
  DirectHandle<String> old_one_byte_seq =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  CHECK(InAnySharedSpace(*old_one_byte_seq));
  DirectHandle<String> old_two_byte_seq =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  CHECK(InAnySharedSpace(*old_two_byte_seq));

  // Young generation are not internalizable and not shared when sharing the
  // string table.
  DirectHandle<String> young_one_byte_seq =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kYoung);
  CHECK(!InAnySharedSpace(*young_one_byte_seq));
  DirectHandle<String> young_two_byte_seq =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
          .ToHandleChecked();
  CHECK(!InAnySharedSpace(*young_two_byte_seq));

  // Internalized strings are shared.
  uint64_t seed = HashSeed(i_isolate1);
  DirectHandle<String> one_byte_intern = factory1->NewOneByteInternalizedString(
      base::OneByteVector(raw_one_byte),
      StringHasher::HashSequentialString<char>(raw_one_byte, 3, seed));
  CHECK(InAnySharedSpace(*one_byte_intern));
  DirectHandle<String> two_byte_intern = factory1->NewTwoByteInternalizedString(
      two_byte,
      StringHasher::HashSequentialString<uint16_t>(raw_two_byte, 3, seed));
  CHECK(InAnySharedSpace(*two_byte_intern));
}

UNINITIALIZED_TEST(InPlaceInternalization) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  ManualGCScope manual_gc_scope(test.i_main_isolate());

  IsolateParkOnDisposeWrapper isolate_wrapper(test.NewClientIsolate(),
                                              test.main_isolate());
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate_wrapper.isolate);
  Factory* factory2 = i_isolate2->factory();

  HandleScope scope1(i_isolate1);
  HandleScope scope2(i_isolate2);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Allocate two in-place internalizable strings in isolate1 then intern
  // them.
  DirectHandle<String> old_one_byte_seq1 =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  DirectHandle<String> old_two_byte_seq1 =
      factory1->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  DirectHandle<String> one_byte_intern1 =
      factory1->InternalizeString(old_one_byte_seq1);
  DirectHandle<String> two_byte_intern1 =
      factory1->InternalizeString(old_two_byte_seq1);
  CHECK(InAnySharedSpace(*old_one_byte_seq1));
  CHECK(InAnySharedSpace(*old_two_byte_seq1));
  CHECK(InAnySharedSpace(*one_byte_intern1));
  CHECK(InAnySharedSpace(*two_byte_intern1));
  CHECK(old_one_byte_seq1.equals(one_byte_intern1));
  CHECK(old_two_byte_seq1.equals(two_byte_intern1));
  CHECK_EQ(*old_one_byte_seq1, *one_byte_intern1);
  CHECK_EQ(*old_two_byte_seq1, *two_byte_intern1);

  // Allocate two in-place internalizable strings with the same contents in
  // isolate2 then intern them. They should be the same as the interned strings
  // from isolate1.
  DirectHandle<String> old_one_byte_seq2 =
      factory2->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  DirectHandle<String> old_two_byte_seq2 =
      factory2->NewStringFromTwoByte(two_byte, AllocationType::kOld)
          .ToHandleChecked();
  DirectHandle<String> one_byte_intern2 =
      factory2->InternalizeString(old_one_byte_seq2);
  DirectHandle<String> two_byte_intern2 =
      factory2->InternalizeString(old_two_byte_seq2);
  CHECK(InAnySharedSpace(*old_one_byte_seq2));
  CHECK(InAnySharedSpace(*old_two_byte_seq2));
  CHECK(InAnySharedSpace(*one_byte_intern2));
  CHECK(InAnySharedSpace(*two_byte_intern2));
  CHECK(!old_one_byte_seq2.equals(one_byte_intern2));
  CHECK(!old_two_byte_seq2.equals(two_byte_intern2));
  CHECK_NE(*old_one_byte_seq2, *one_byte_intern2);
  CHECK_NE(*old_two_byte_seq2, *two_byte_intern2);
  CHECK_EQ(*one_byte_intern1, *one_byte_intern2);
  CHECK_EQ(*two_byte_intern1, *two_byte_intern2);
}

UNINITIALIZED_TEST(YoungInternalization) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  IsolateParkOnDisposeWrapper isolate_wrapper(test.NewClientIsolate(),
                                              test.main_isolate());
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate_wrapper.isolate);
  Factory* factory2 = i_isolate2->factory();

  HandleScope scope1(i_isolate1);
  HandleScope scope2(i_isolate2);

  const char raw_one_byte[] = "foo";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<const base::uc16> two_byte(raw_two_byte, 3);

  // Allocate two young strings in isolate1 then intern them. Young strings
  // aren't in-place internalizable and are copied when internalized.
  Handle<String> young_one_byte_seq1;
  Handle<String> young_two_byte_seq1;
  Handle<String> one_byte_intern1;
  Handle<String> two_byte_intern1;
  i_isolate2->main_thread_local_isolate()->ExecuteMainThreadWhileParked([&]() {
    young_one_byte_seq1 = factory1->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    young_two_byte_seq1 =
        factory1->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
            .ToHandleChecked();
    one_byte_intern1 = factory1->InternalizeString(young_one_byte_seq1);
    two_byte_intern1 = factory1->InternalizeString(young_two_byte_seq1);
    CHECK(!InAnySharedSpace(*young_one_byte_seq1));
    CHECK(!InAnySharedSpace(*young_two_byte_seq1));
    CHECK(InAnySharedSpace(*one_byte_intern1));
    CHECK(InAnySharedSpace(*two_byte_intern1));
    CHECK(!young_one_byte_seq1.equals(one_byte_intern1));
    CHECK(!young_two_byte_seq1.equals(two_byte_intern1));
    CHECK_NE(*young_one_byte_seq1, *one_byte_intern1);
    CHECK_NE(*young_two_byte_seq1, *two_byte_intern1);
  });

  // Allocate two young strings with the same contents in isolate2 then intern
  // them. They should be the same as the interned strings from isolate1.
  Handle<String> young_one_byte_seq2;
  Handle<String> young_two_byte_seq2;
  Handle<String> one_byte_intern2;
  Handle<String> two_byte_intern2;
  {
    v8::Isolate::Scope isolate_scope(isolate_wrapper.isolate);
    young_one_byte_seq2 = factory2->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    young_two_byte_seq2 =
        factory2->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
            .ToHandleChecked();
    one_byte_intern2 = factory2->InternalizeString(young_one_byte_seq2);
    two_byte_intern2 = factory2->InternalizeString(young_two_byte_seq2);
    CHECK(!young_one_byte_seq2.equals(one_byte_intern2));
    CHECK(!young_two_byte_seq2.equals(two_byte_intern2));
    CHECK_NE(*young_one_byte_seq2, *one_byte_intern2);
    CHECK_NE(*young_two_byte_seq2, *two_byte_intern2);
    CHECK_EQ(*one_byte_intern1, *one_byte_intern2);
    CHECK_EQ(*two_byte_intern1, *two_byte_intern2);
  }
}

class ConcurrentStringThreadBase : public ParkingThread {
 public:
  ConcurrentStringThreadBase(const char* name, MultiClientIsolateTest* test,
                             IndirectHandle<FixedArray> shared_strings,
                             ParkingSemaphore* sema_ready,
                             ParkingSemaphore* sema_execute_start,
                             ParkingSemaphore* sema_execute_complete)
      : ParkingThread(base::Thread::Options(name)),
        test_(test),
        shared_strings_(shared_strings),
        sema_ready_(sema_ready),
        sema_execute_start_(sema_execute_start),
        sema_execute_complete_(sema_execute_complete) {}

  virtual void Setup() {}
  virtual void RunForString(Handle<String> string, int counter) = 0;
  virtual void Teardown() {}
  void Run() override {
    IsolateWrapper isolate_wrapper(test_->NewClientIsolate());
    i_isolate = reinterpret_cast<Isolate*>(isolate_wrapper.isolate);

    Setup();

    sema_ready_->Signal();
    sema_execute_start_->ParkedWait(i_isolate->main_thread_local_isolate());

    {
      HandleScope scope(i_isolate);
      for (int i = 0; i < shared_strings_->length(); i++) {
        Handle<String> input_string(Cast<String>(shared_strings_->get(i)),
                                    i_isolate);
        RunForString(input_string, i);
      }
    }

    sema_execute_complete_->Signal();

    Teardown();

    i_isolate = nullptr;
  }

 protected:
  Isolate* i_isolate;
  MultiClientIsolateTest* test_;
  IndirectHandle<FixedArray> shared_strings_;
  ParkingSemaphore* sema_ready_;
  ParkingSemaphore* sema_execute_start_;
  ParkingSemaphore* sema_execute_complete_;
};

enum TestHitOrMiss { kTestMiss, kTestHit };

class ConcurrentInternalizationThread final
    : public ConcurrentStringThreadBase {
 public:
  ConcurrentInternalizationThread(MultiClientIsolateTest* test,
                                  IndirectHandle<FixedArray> shared_strings,
                                  TestHitOrMiss hit_or_miss,
                                  ParkingSemaphore* sema_ready,
                                  ParkingSemaphore* sema_execute_start,
                                  ParkingSemaphore* sema_execute_complete)
      : ConcurrentStringThreadBase("ConcurrentInternalizationThread", test,
                                   shared_strings, sema_ready,
                                   sema_execute_start, sema_execute_complete),
        hit_or_miss_(hit_or_miss) {}

  void Setup() override { factory = i_isolate->factory(); }

  void RunForString(Handle<String> input_string, int counter) override {
    CHECK(input_string->IsShared());
    Handle<String> interned = factory->InternalizeString(input_string);
    CHECK(interned->IsShared());
    CHECK(IsInternalizedString(*interned));
    if (hit_or_miss_ == kTestMiss) {
      CHECK_EQ(*input_string, *interned);
    } else {
      CHECK(input_string->HasForwardingIndex(kAcquireLoad));
      CHECK(String::Equals(i_isolate, input_string, interned));
    }
  }

 private:
  TestHitOrMiss hit_or_miss_;
  Factory* factory;
};

namespace {

std::pair<Handle<String>, MaybeHandle<String>> CreateSharedOneByteString(
    Isolate* isolate, Factory* factory, int length, bool internalize) {
  char* ascii = new char[length + 1];
  // Don't make single character strings, which will end up deduplicating to
  // an RO string and mess up the string table hit test.
  CHECK_GT(length, 1);
  for (int j = 0; j < length; j++) ascii[j] = 'a';
  ascii[length] = '\0';
  MaybeHandle<String> internalized;
  if (internalize) {
    // When testing concurrent string table hits, pre-internalize a string
    // of the same contents so all subsequent internalizations are hits.
    internalized =
        factory->InternalizeString(factory->NewStringFromAsciiChecked(ascii));
    CHECK(IsInternalizedString(*internalized.ToHandleChecked()));
  }
  Handle<String> string = String::Share(
      isolate, factory->NewStringFromAsciiChecked(ascii, AllocationType::kOld));
  delete[] ascii;
  CHECK(string->IsShared());
  string->EnsureHash();
  return std::make_pair(string, internalized);
}

IndirectHandle<FixedArray> CreateSharedOneByteStrings(
    Isolate* isolate, Factory* factory, int count, int lo_count,
    int min_length = 2, bool internalize = false) {
  IndirectHandle<FixedArray> shared_strings =
      factory->NewFixedArray(count + lo_count, AllocationType::kSharedOld);
  // Buffer to keep internalized strings alive in the current scope.
  DirectHandle<FixedArray> internalized_handles;
  if (internalize) {
    internalized_handles =
        factory->NewFixedArray(count + lo_count, AllocationType::kOld);
  }
  {
    // Create strings in their own scope to be able to delete and GC them.
    HandleScope scope(isolate);
    for (int i = 0; i < count; i++) {
      int length = i + min_length + 1;
      auto strings =
          CreateSharedOneByteString(isolate, factory, length, internalize);
      shared_strings->set(i, *strings.first);
      if (internalize) {
        internalized_handles->set(i, *strings.second.ToHandleChecked());
      }
    }
    int min_lo_length =
        isolate->heap()->MaxRegularHeapObjectSize(AllocationType::kOld) + 1;
    for (int i = 0; i < lo_count; i++) {
      int length = i + min_lo_length + 1;
      auto strings =
          CreateSharedOneByteString(isolate, factory, length, internalize);
      shared_strings->set(count + i, *strings.first);
      if (internalize) {
        internalized_handles->set(count + i, *strings.second.ToHandleChecked());
      }
    }
  }
  return shared_strings;
}

void TestConcurrentInternalization(TestHitOrMiss hit_or_miss) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  constexpr int kThreads = 4;
  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  IndirectHandle<FixedArray> shared_strings =
      CreateSharedOneByteStrings(i_isolate, factory, kStrings - kLOStrings,
                                 kLOStrings, 2, hit_or_miss == kTestHit);

  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<ConcurrentInternalizationThread>> threads;
  for (int i = 0; i < kThreads; i++) {
    auto thread = std::make_unique<ConcurrentInternalizationThread>(
        &test, shared_strings, hit_or_miss, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_isolate->main_thread_local_isolate();
  for (int i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_start.Signal();
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
}
}  // namespace

UNINITIALIZED_TEST(ConcurrentInternalizationMiss) {
  TestConcurrentInternalization(kTestMiss);
}

UNINITIALIZED_TEST(ConcurrentInternalizationHit) {
  TestConcurrentInternalization(kTestHit);
}

class ConcurrentStringTableLookupThread final
    : public ConcurrentStringThreadBase {
 public:
  ConcurrentStringTableLookupThread(MultiClientIsolateTest* test,
                                    IndirectHandle<FixedArray> shared_strings,
                                    ParkingSemaphore* sema_ready,
                                    ParkingSemaphore* sema_execute_start,
                                    ParkingSemaphore* sema_execute_complete)
      : ConcurrentStringThreadBase("ConcurrentStringTableLookup", test,
                                   shared_strings, sema_ready,
                                   sema_execute_start, sema_execute_complete) {}

  void RunForString(Handle<String> input_string, int counter) override {
    CHECK(input_string->IsShared());
    Tagged<Object> result =
        Tagged<Object>(StringTable::TryStringToIndexOrLookupExisting(
            i_isolate, input_string->ptr()));
    if (IsString(result)) {
      Tagged<String> internalized = Cast<String>(result);
      CHECK(IsInternalizedString(internalized));
      CHECK_IMPLIES(IsInternalizedString(*input_string),
                    *input_string == internalized);
    } else {
      CHECK_EQ(Cast<Smi>(result).value(), ResultSentinel::kNotFound);
    }
  }
};

UNINITIALIZED_TEST(ConcurrentStringTableLookup) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  constexpr int kTotalThreads = 4;
  constexpr int kInternalizationThreads = 1;
  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  IndirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
      i_isolate, factory, kStrings - kLOStrings, kLOStrings, 2, false);

  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<ConcurrentStringThreadBase>> threads;
  for (int i = 0; i < kInternalizationThreads; i++) {
    auto thread = std::make_unique<ConcurrentInternalizationThread>(
        &test, shared_strings, kTestMiss, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }
  for (int i = 0; i < kTotalThreads - kInternalizationThreads; i++) {
    auto thread = std::make_unique<ConcurrentStringTableLookupThread>(
        &test, shared_strings, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_isolate->main_thread_local_isolate();
  for (int i = 0; i < kTotalThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kTotalThreads; i++) {
    sema_execute_start.Signal();
  }
  for (int i = 0; i < kTotalThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
}

namespace {

void CheckSharedStringIsEqualCopy(DirectHandle<String> shared,
                                  DirectHandle<String> original) {
  CHECK(shared->IsShared());
  CHECK(shared->Equals(*original));
  CHECK_NE(*shared, *original);
}

Handle<String> ShareAndVerify(Isolate* isolate, Handle<String> string) {
  Handle<String> shared = String::Share(isolate, string);
  CHECK(shared->IsShared());
#ifdef VERIFY_HEAP
  Object::ObjectVerify(*shared, isolate);
  Object::ObjectVerify(*string, isolate);
#endif  // VERIFY_HEAP
  return shared;
}

class OneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  OneByteResource(const char* data, size_t length)
      : data_(data), length_(length) {}
  const char* data() const override { return data_; }
  size_t length() const override { return length_; }
  void Dispose() override {
    CHECK(!IsDisposed());
    i::DeleteArray(data_);
    data_ = nullptr;
  }
  bool IsDisposed() const { return data_ == nullptr; }

 private:
  const char* data_;
  size_t length_;
};

class TwoByteResource : public v8::String::ExternalStringResource {
 public:
  TwoByteResource(const uint16_t* data, size_t length)
      : data_(data), length_(length) {}
  const uint16_t* data() const override { return data_; }
  size_t length() const override { return length_; }
  void Dispose() override {
    i::DeleteArray(data_);
    data_ = nullptr;
  }
  bool IsDisposed() const { return data_ == nullptr; }

 private:
  const uint16_t* data_;
  size_t length_;
};

class ExternalResourceFactory {
 public:
  ~ExternalResourceFactory() {
    for (auto* res : one_byte_resources_) {
      CHECK(res->IsDisposed());
      delete res;
    }
    for (auto* res : two_byte_resources_) {
      CHECK(res->IsDisposed());
      delete res;
    }
  }
  OneByteResource* CreateOneByte(const char* data, size_t length,
                                 bool copy = true) {
    OneByteResource* res =
        new OneByteResource(copy ? i::StrDup(data) : data, length);
    Register(res);
    return res;
  }
  OneByteResource* CreateOneByte(const char* data, bool copy = true) {
    return CreateOneByte(data, strlen(data), copy);
  }
  TwoByteResource* CreateTwoByte(const uint16_t* data, size_t length,
                                 bool copy = true) {
    TwoByteResource* res = new TwoByteResource(data, length);
    Register(res);
    return res;
  }
  TwoByteResource* CreateTwoByte(base::Vector<base::uc16> vector,
                                 bool copy = true) {
    auto vec = copy ? vector.Clone() : vector;
    return CreateTwoByte(vec.begin(), vec.size(), copy);
  }
  void Register(OneByteResource* res) { one_byte_resources_.push_back(res); }
  void Register(TwoByteResource* res) { two_byte_resources_.push_back(res); }

 private:
  std::vector<OneByteResource*> one_byte_resources_;
  std::vector<TwoByteResource*> two_byte_resources_;
};

}  // namespace

UNINITIALIZED_TEST(StringShare) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ManualGCScope manual_gc_scope;
  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  // A longer string so that concatenated to itself, the result is >
  // ConsString::kMinLength.
  const char raw_one_byte[] =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte(raw_two_byte, 3);

  {
    // Old-generation sequential strings are shared in-place.
    Handle<String> one_byte_seq =
        factory->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
    Handle<String> two_byte_seq =
        factory->NewStringFromTwoByte(two_byte, AllocationType::kOld)
            .ToHandleChecked();
    CHECK(!one_byte_seq->IsShared());
    CHECK(!two_byte_seq->IsShared());
    DirectHandle<String> shared_one_byte =
        ShareAndVerify(i_isolate, one_byte_seq);
    DirectHandle<String> shared_two_byte =
        ShareAndVerify(i_isolate, two_byte_seq);
    CHECK_EQ(*one_byte_seq, *shared_one_byte);
    CHECK_EQ(*two_byte_seq, *shared_two_byte);
  }

  {
    // Internalized strings are always shared.
    Handle<String> one_byte_seq =
        factory->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
    Handle<String> two_byte_seq =
        factory->NewStringFromTwoByte(two_byte, AllocationType::kOld)
            .ToHandleChecked();
    CHECK(!one_byte_seq->IsShared());
    CHECK(!two_byte_seq->IsShared());
    Handle<String> one_byte_intern = factory->InternalizeString(one_byte_seq);
    Handle<String> two_byte_intern = factory->InternalizeString(two_byte_seq);
    CHECK(one_byte_intern->IsShared());
    CHECK(two_byte_intern->IsShared());
    DirectHandle<String> shared_one_byte_intern =
        ShareAndVerify(i_isolate, one_byte_intern);
    DirectHandle<String> shared_two_byte_intern =
        ShareAndVerify(i_isolate, two_byte_intern);
    CHECK_EQ(*one_byte_intern, *shared_one_byte_intern);
    CHECK_EQ(*two_byte_intern, *shared_two_byte_intern);
  }

  {
    // Old-generation external strings are shared in-place.
    Handle<String> one_byte_ext =
        factory->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
    Handle<String> two_byte_ext =
        factory->NewStringFromTwoByte(two_byte, AllocationType::kOld)
            .ToHandleChecked();
    OneByteResource* one_byte_res =
        resource_factory.CreateOneByte(raw_one_byte);
    TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte);
    CHECK(one_byte_ext->MakeExternal(one_byte_res));
    CHECK(two_byte_ext->MakeExternal(two_byte_res));
    if (v8_flags.always_use_string_forwarding_table) {
      i_isolate->heap()->CollectGarbageShared(
          i_isolate->main_thread_local_heap(),
          GarbageCollectionReason::kTesting);
    }
    CHECK(IsExternalString(*one_byte_ext));
    CHECK(IsExternalString(*two_byte_ext));
    CHECK(!one_byte_ext->IsShared());
    CHECK(!two_byte_ext->IsShared());
    DirectHandle<String> shared_one_byte =
        ShareAndVerify(i_isolate, one_byte_ext);
    DirectHandle<String> shared_two_byte =
        ShareAndVerify(i_isolate, two_byte_ext);
    CHECK_EQ(*one_byte_ext, *shared_one_byte);
    CHECK_EQ(*two_byte_ext, *shared_two_byte);
  }

  // All other strings are flattened then copied if the flatten didn't already
  // create a new copy.

  if (!v8_flags.single_generation) {
    // Young strings
    Handle<String> young_one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    Handle<String> young_two_byte_seq =
        factory->NewStringFromTwoByte(two_byte, AllocationType::kYoung)
            .ToHandleChecked();
    CHECK(Heap::InYoungGeneration(*young_one_byte_seq));
    CHECK(Heap::InYoungGeneration(*young_two_byte_seq));
    CHECK(!young_one_byte_seq->IsShared());
    CHECK(!young_two_byte_seq->IsShared());
    DirectHandle<String> shared_one_byte =
        ShareAndVerify(i_isolate, young_one_byte_seq);
    DirectHandle<String> shared_two_byte =
        ShareAndVerify(i_isolate, young_two_byte_seq);
    CheckSharedStringIsEqualCopy(shared_one_byte, young_one_byte_seq);
    CheckSharedStringIsEqualCopy(shared_two_byte, young_two_byte_seq);
  }

  if (!v8_flags.always_use_string_forwarding_table) {
    // Thin strings
    Handle<String> one_byte_seq1 =
        factory->NewStringFromAsciiChecked(raw_one_byte);
    Handle<String> one_byte_seq2 =
        factory->NewStringFromAsciiChecked(raw_one_byte);
    CHECK(!one_byte_seq1->IsShared());
    CHECK(!one_byte_seq2->IsShared());
    factory->InternalizeString(one_byte_seq1);
    factory->InternalizeString(one_byte_seq2);
    CHECK(StringShape(*one_byte_seq2).IsThin());
    DirectHandle<String> shared = ShareAndVerify(i_isolate, one_byte_seq2);
    CheckSharedStringIsEqualCopy(shared, one_byte_seq2);
  }

  {
    // Cons strings
    Handle<String> one_byte_seq1 =
        factory->NewStringFromAsciiChecked(raw_one_byte);
    Handle<String> one_byte_seq2 =
        factory->NewStringFromAsciiChecked(raw_one_byte);
    CHECK(!one_byte_seq1->IsShared());
    CHECK(!one_byte_seq2->IsShared());
    Handle<String> cons =
        factory->NewConsString(one_byte_seq1, one_byte_seq2).ToHandleChecked();
    CHECK(!cons->IsShared());
    CHECK(IsConsString(*cons));
    DirectHandle<String> shared = ShareAndVerify(i_isolate, cons);
    CheckSharedStringIsEqualCopy(shared, cons);
  }

  {
    // Sliced strings
    Handle<String> one_byte_seq =
        factory->NewStringFromAsciiChecked(raw_one_byte);
    CHECK(!one_byte_seq->IsShared());
    Handle<String> sliced =
        factory->NewSubString(one_byte_seq, 1, one_byte_seq->length());
    CHECK(!sliced->IsShared());
    CHECK(IsSlicedString(*sliced));
    DirectHandle<String> shared = ShareAndVerify(i_isolate, sliced);
    CheckSharedStringIsEqualCopy(shared, sliced);
  }
}

UNINITIALIZED_TEST(PromotionMarkCompact) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();
  // Heap* shared_heap = test.i_shared_isolate()->heap();

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    // heap::SealCurrentObjects(heap);
    // heap::SealCurrentObjects(shared_heap);

    IndirectHandle<String> one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);

    CHECK(String::IsInPlaceInternalizable(*one_byte_seq));
    CHECK(heap->InSpace(*one_byte_seq, NEW_SPACE));

    // 1st GC moves `one_byte_seq` to old space and 2nd GC evacuates it within
    // old space.
    heap::InvokeMajorGC(heap);
    heap::ForceEvacuationCandidate(
        i::PageMetadata::FromHeapObject(*one_byte_seq));

    {
      // We need to invoke GC without stack, otherwise no compaction is
      // performed.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // In-place-internalizable strings are promoted into the shared heap when
    // sharing.
    CHECK(heap->SharedHeapContains(*one_byte_seq));
  }
}

UNINITIALIZED_TEST(PromotionScavenge) {
  if (v8_flags.minor_ms) return;
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();
  // Heap* shared_heap = test.i_shared_isolate()->heap();

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    // heap::SealCurrentObjects(heap);
    // heap::SealCurrentObjects(shared_heap);

    DirectHandle<String> one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);

    CHECK(String::IsInPlaceInternalizable(*one_byte_seq));
    CHECK(heap->InSpace(*one_byte_seq, NEW_SPACE));

    for (int i = 0; i < 2; i++) {
      heap::InvokeMinorGC(heap);
    }

    // In-place-internalizable strings are promoted into the shared heap when
    // sharing.
    CHECK(heap->SharedHeapContains(*one_byte_seq));
  }
}

UNINITIALIZED_TEST(PromotionScavengeOldToShared) {
  if (v8_flags.minor_ms) {
    // Promoting from new space directly to shared heap is not implemented in
    // MinorMS.
    return;
  }
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  if (v8_flags.stress_concurrent_allocation) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();
  ManualGCScope manual_gc(i_isolate);

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    DirectHandle<FixedArray> old_object =
        factory->NewFixedArray(1, AllocationType::kOld);
    MemoryChunk* old_object_chunk = MemoryChunk::FromHeapObject(*old_object);
    CHECK(!old_object_chunk->InYoungGeneration());

    DirectHandle<String> one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    CHECK(String::IsInPlaceInternalizable(*one_byte_seq));
    CHECK(MemoryChunk::FromHeapObject(*one_byte_seq)->InYoungGeneration());

    old_object->set(0, *one_byte_seq);
    ObjectSlot slot = old_object->RawFieldOfFirstElement();
    CHECK(RememberedSet<OLD_TO_NEW>::Contains(
        MutablePageMetadata::cast(
            MutablePageMetadata::cast(old_object_chunk->Metadata())),
        slot.address()));

    for (int i = 0; i < 2; i++) {
      heap::InvokeMinorGC(heap);
    }

    // In-place-internalizable strings are promoted into the shared heap when
    // sharing.
    CHECK(heap->SharedHeapContains(*one_byte_seq));

    // Since the GC promoted that string into shared heap, it also needs to
    // create an OLD_TO_SHARED slot.
    CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
        MutablePageMetadata::cast(old_object_chunk->Metadata()),
        slot.address()));
  }
}

UNINITIALIZED_TEST(PromotionMarkCompactNewToShared) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  if (v8_flags.stress_concurrent_allocation) return;

  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  v8_flags.page_promotion = false;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    IndirectHandle<FixedArray> old_object =
        factory->NewFixedArray(1, AllocationType::kOld);
    MemoryChunk* old_object_chunk = MemoryChunk::FromHeapObject(*old_object);
    CHECK(!old_object_chunk->InYoungGeneration());

    IndirectHandle<String> one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    CHECK(String::IsInPlaceInternalizable(*one_byte_seq));
    CHECK(MemoryChunk::FromHeapObject(*one_byte_seq)->InYoungGeneration());

    old_object->set(0, *one_byte_seq);
    ObjectSlot slot = old_object->RawFieldOfFirstElement();
    CHECK(RememberedSet<OLD_TO_NEW>::Contains(
        MutablePageMetadata::cast(old_object_chunk->Metadata()),
        slot.address()));

    {
      // We need to invoke GC without stack, otherwise no compaction is
      // performed.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // In-place-internalizable strings are promoted into the shared heap when
    // sharing.
    CHECK(heap->SharedHeapContains(*one_byte_seq));

    // Since the GC promoted that string into shared heap, it also needs to
    // create an OLD_TO_SHARED slot.
    CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
        MutablePageMetadata::cast(old_object_chunk->Metadata()),
        slot.address()));
  }
}

UNINITIALIZED_TEST(PromotionMarkCompactOldToShared) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  if (v8_flags.stress_concurrent_allocation) return;
  if (!v8_flags.page_promotion) return;
  if (v8_flags.single_generation) {
    // String allocated in old space may be "pretenured" to the shared heap.
    return;
  }

  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    IndirectHandle<FixedArray> old_object =
        factory->NewFixedArray(1, AllocationType::kOld);
    MemoryChunk* old_object_chunk = MemoryChunk::FromHeapObject(*old_object);
    CHECK(!old_object_chunk->InYoungGeneration());

    IndirectHandle<String> one_byte_seq = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kYoung);
    CHECK(String::IsInPlaceInternalizable(*one_byte_seq));
    CHECK(MemoryChunk::FromHeapObject(*one_byte_seq)->InYoungGeneration());

    std::vector<Handle<FixedArray>> handles;
    // Fill the page and do a full GC. Page promotion should kick in and promote
    // the page as is to old space.
    heap::FillCurrentPage(heap->new_space(), &handles);
    heap::InvokeMajorGC(heap);
    // Make sure 'one_byte_seq' is in old space.
    CHECK(!MemoryChunk::FromHeapObject(*one_byte_seq)->InYoungGeneration());
    CHECK(heap->Contains(*one_byte_seq));

    old_object->set(0, *one_byte_seq);
    ObjectSlot slot = old_object->RawFieldOfFirstElement();
    CHECK(!RememberedSet<OLD_TO_NEW>::Contains(
        MutablePageMetadata::cast(old_object_chunk->Metadata()),
        slot.address()));

    heap::ForceEvacuationCandidate(PageMetadata::FromHeapObject(*one_byte_seq));
    {
      // We need to invoke GC without stack, otherwise no compaction is
      // performed.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);
      heap::InvokeMajorGC(heap);
    }

    // In-place-internalizable strings are promoted into the shared heap when
    // sharing.
    CHECK(heap->SharedHeapContains(*one_byte_seq));

    // Since the GC promoted that string into shared heap, it also needs to
    // create an OLD_TO_SHARED slot.
    CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
        MutablePageMetadata::cast(old_object_chunk->Metadata()),
        slot.address()));
  }
}

UNINITIALIZED_TEST(PagePromotionRecordingOldToShared) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  if (v8_flags.stress_concurrent_allocation) return;

  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();
  Heap* heap = i_isolate->heap();

  const char raw_one_byte[] = "foo";

  {
    HandleScope scope(i_isolate);

    DirectHandle<FixedArray> young_object =
        factory->NewFixedArray(1, AllocationType::kYoung);
    CHECK(Heap::InYoungGeneration(*young_object));
    Address young_object_address = young_object->address();

    std::vector<Handle<FixedArray>> handles;
    // Make the whole page transition from new->old, getting the buffers
    // processed in the sweeper (relying on marking information) instead of
    // processing during newspace evacuation.
    heap::FillCurrentPage(heap->new_space(), &handles);

    DirectHandle<String> shared_string = factory->NewStringFromAsciiChecked(
        raw_one_byte, AllocationType::kSharedOld);
    CHECK(InWritableSharedSpace(*shared_string));

    young_object->set(0, *shared_string);

    heap::EmptyNewSpaceUsingGC(heap);

    // Object should get promoted using page promotion, so address should remain
    // the same.
    CHECK(!Heap::InYoungGeneration(*shared_string));
    CHECK_EQ(young_object_address, young_object->address());

    // Since the GC promoted that string into shared heap, it also needs to
    // create an OLD_TO_SHARED slot.
    ObjectSlot slot = young_object->RawFieldOfFirstElement();
    CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
        MutablePageMetadata::FromHeapObject(*young_object), slot.address()));
  }
}

namespace {

void TriggerGCWithTransitions(Heap* heap) {
  v8_flags.transition_strings_during_gc_with_stack = true;
  heap::CollectSharedGarbage(heap);
  v8_flags.transition_strings_during_gc_with_stack = false;
}

}  // namespace

UNINITIALIZED_TEST(InternalizedSharedStringsTransitionDuringGC) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  // Run two times to test that everything is reset correctly during GC.
  for (int run = 0; run < 2; run++) {
    DirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
        i_isolate, factory, kStrings - kLOStrings, kLOStrings, 2, run == 0);

    // Check strings are in the forwarding table after internalization.
    for (int i = 0; i < shared_strings->length(); i++) {
      Handle<String> input_string(Cast<String>(shared_strings->get(i)),
                                  i_isolate);
      Handle<String> interned = factory->InternalizeString(input_string);
      CHECK(input_string->IsShared());
      CHECK(!IsThinString(*input_string));
      CHECK(input_string->HasForwardingIndex(kAcquireLoad));
      CHECK(String::Equals(i_isolate, input_string, interned));
    }

    // Trigger garbage collection on the shared isolate.
    TriggerGCWithTransitions(i_isolate->heap());

    // Check that GC cleared the forwarding table.
    CHECK_EQ(i_isolate->string_forwarding_table()->size(), 0);

    // Check all strings are transitioned to ThinStrings
    for (int i = 0; i < shared_strings->length(); i++) {
      DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                        i_isolate);
      CHECK(IsThinString(*input_string));
    }
  }
}

UNINITIALIZED_TEST(ShareExternalString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";

  // External strings in old space can be shared in-place.
  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  CHECK(!one_byte->IsShared());

  OneByteResource* resource = resource_factory.CreateOneByte(raw_one_byte);
  one_byte->MakeExternal(resource);
  if (v8_flags.always_use_string_forwarding_table) {
    i_isolate1->heap()->CollectGarbageShared(
        i_isolate1->main_thread_local_heap(),
        GarbageCollectionReason::kTesting);
  }
  CHECK(IsExternalString(*one_byte));
  Handle<ExternalOneByteString> one_byte_external =
      Cast<ExternalOneByteString>(one_byte);
  DirectHandle<String> shared_one_byte =
      ShareAndVerify(i_isolate1, one_byte_external);
  CHECK_EQ(*shared_one_byte, *one_byte);
}

namespace {

void CheckExternalStringResource(
    Handle<String> string, v8::String::ExternalStringResourceBase* resource) {
  const bool is_one_byte = string->IsOneByteRepresentation();
  Local<v8::String> api_string = Utils::ToLocal(string);
  v8::String::Encoding encoding;
  CHECK_EQ(resource, api_string->GetExternalStringResourceBase(&encoding));
  if (is_one_byte) {
    CHECK_EQ(encoding, v8::String::Encoding::ONE_BYTE_ENCODING);
    CHECK_EQ(resource, api_string->GetExternalOneByteStringResource());
  } else {
    CHECK(string->IsTwoByteRepresentation());
    CHECK_EQ(encoding, v8::String::Encoding::TWO_BYTE_ENCODING);
    CHECK_EQ(resource, api_string->GetExternalStringResource());
  }
}

}  // namespace

UNINITIALIZED_TEST(ExternalizeSharedString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte_vec(raw_two_byte, 3);

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> two_byte =
      factory1->NewStringFromTwoByte(two_byte_vec, AllocationType::kOld)
          .ToHandleChecked();
  CHECK(one_byte->IsOneByteRepresentation());
  CHECK(two_byte->IsTwoByteRepresentation());
  CHECK(!one_byte->IsShared());
  CHECK(!two_byte->IsShared());

  Handle<String> shared_one_byte = ShareAndVerify(i_isolate1, one_byte);
  Handle<String> shared_two_byte = ShareAndVerify(i_isolate1, two_byte);

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);
  TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte_vec);
  shared_one_byte->MakeExternal(one_byte_res);
  shared_two_byte->MakeExternal(two_byte_res);
  CHECK(!IsExternalString(*shared_one_byte));
  CHECK(!IsExternalString(*shared_two_byte));
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasExternalForwardingIndex(kAcquireLoad));

  // Check that API calls return the resource from the forwarding table.
  CheckExternalStringResource(shared_one_byte, one_byte_res);
  CheckExternalStringResource(shared_two_byte, two_byte_res);
}

UNINITIALIZED_TEST(ExternalizedSharedStringsTransitionDuringGC) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;

  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  // Run two times to test that everything is reset correctly during GC.
  for (int run = 0; run < 2; run++) {
    DirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
        i_isolate, factory, kStrings - kLOStrings, kLOStrings,
        sizeof(UncachedExternalString), run == 0);

    // Check strings are in the forwarding table after internalization.
    for (int i = 0; i < shared_strings->length(); i++) {
      DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                        i_isolate);
      const int length = input_string->length();
      char* buffer = new char[length + 1];
      String::WriteToFlat(*input_string, reinterpret_cast<uint8_t*>(buffer), 0,
                          length);
      OneByteResource* resource =
          resource_factory.CreateOneByte(buffer, length, false);
      CHECK(input_string->MakeExternal(resource));
      CHECK(input_string->IsShared());
      CHECK(!IsExternalString(*input_string));
      CHECK(input_string->HasExternalForwardingIndex(kAcquireLoad));
    }

    // Trigger garbage collection on the shared isolate.
    TriggerGCWithTransitions(i_isolate->heap());

    // Check that GC cleared the forwarding table.
    CHECK_EQ(i_isolate->string_forwarding_table()->size(), 0);

    // Check all strings are transitioned to ExternalStrings
    for (int i = 0; i < shared_strings->length(); i++) {
      DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                        i_isolate);
      CHECK(IsExternalString(*input_string));
    }
  }
}

UNINITIALIZED_TEST(ExternalizeInternalizedString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte_vec(raw_two_byte, 3);

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> two_byte =
      factory1->NewStringFromTwoByte(two_byte_vec, AllocationType::kOld)
          .ToHandleChecked();
  // Internalize copies, s.t. internalizing the original strings creates a
  // forwarding entry.
  factory1->InternalizeString(
      factory1->NewStringFromAsciiChecked(raw_one_byte));
  factory1->InternalizeString(
      factory1->NewStringFromTwoByte(two_byte_vec).ToHandleChecked());
  Handle<String> one_byte_intern = factory1->InternalizeString(one_byte);
  Handle<String> two_byte_intern = factory1->InternalizeString(two_byte);
  if (v8_flags.always_use_string_forwarding_table) {
    i_isolate1->heap()->CollectGarbageShared(
        i_isolate1->main_thread_local_heap(),
        GarbageCollectionReason::kTesting);
  }
  CHECK(IsThinString(*one_byte));
  CHECK(IsThinString(*two_byte));
  CHECK(one_byte_intern->IsOneByteRepresentation());
  CHECK(two_byte_intern->IsTwoByteRepresentation());
  CHECK(one_byte_intern->IsShared());
  CHECK(two_byte_intern->IsShared());

  uint32_t one_byte_hash = one_byte_intern->hash();
  uint32_t two_byte_hash = two_byte_intern->hash();

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);
  TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte_vec);
  CHECK(one_byte_intern->MakeExternal(one_byte_res));
  CHECK(two_byte_intern->MakeExternal(two_byte_res));
  CHECK(!IsExternalString(*one_byte_intern));
  CHECK(!IsExternalString(*two_byte_intern));
  CHECK(one_byte_intern->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(two_byte_intern->HasExternalForwardingIndex(kAcquireLoad));
  // The hash of internalized strings is stored in the forwarding table.
  CHECK_EQ(one_byte_intern->hash(), one_byte_hash);
  CHECK_EQ(two_byte_intern->hash(), two_byte_hash);

  // Check that API calls return the resource from the forwarding table.
  CheckExternalStringResource(one_byte_intern, one_byte_res);
  CheckExternalStringResource(two_byte_intern, two_byte_res);
}

UNINITIALIZED_TEST(InternalizeSharedExternalString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte_vec(raw_two_byte, 3);

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> two_byte =
      factory1->NewStringFromTwoByte(two_byte_vec, AllocationType::kOld)
          .ToHandleChecked();

  Handle<String> shared_one_byte = ShareAndVerify(i_isolate1, one_byte);
  DirectHandle<String> shared_two_byte = ShareAndVerify(i_isolate1, two_byte);

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);
  TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte_vec);
  CHECK(shared_one_byte->MakeExternal(one_byte_res));
  CHECK(shared_two_byte->MakeExternal(two_byte_res));
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasExternalForwardingIndex(kAcquireLoad));

  // Trigger GC to externalize the shared string.
  TriggerGCWithTransitions(i_isolate1->heap());

  CHECK(shared_one_byte->IsShared());
  CHECK(IsExternalString(*shared_one_byte));
  CHECK(shared_two_byte->IsShared());
  CHECK(IsExternalString(*shared_two_byte));

  // Shared cached external strings are in-place internalizable.
  DirectHandle<String> one_byte_intern =
      factory1->InternalizeString(shared_one_byte);
  CHECK_EQ(*one_byte_intern, *shared_one_byte);
  CHECK(IsExternalString(*shared_one_byte));
  CHECK(IsInternalizedString(*shared_one_byte));

  // Depending on the architecture/build options the two byte string might be
  // cached or uncached.
  const bool is_uncached =
      two_byte->Size() < static_cast<int>(sizeof(ExternalString));

  if (is_uncached) {
    // Shared uncached external strings are not internalizable. A new internal
    // copy will be created.
    DirectHandle<String> two_byte_intern =
        factory1->InternalizeString(two_byte);
    CHECK_NE(*two_byte_intern, *shared_two_byte);
    CHECK(shared_two_byte->HasInternalizedForwardingIndex(kAcquireLoad));
    CHECK(IsInternalizedString(*two_byte_intern));
    CHECK(!IsExternalString(*two_byte_intern));
  } else {
    DirectHandle<String> two_byte_intern =
        factory1->InternalizeString(two_byte);
    CHECK_EQ(*two_byte_intern, *shared_two_byte);
    CHECK(IsExternalString(*shared_two_byte));
    CHECK(IsInternalizedString(*shared_two_byte));
  }

  // Another GC should create an externalized internalized string of the cached
  // (one byte) string and turn the uncached (two byte) string into a
  // ThinString, disposing the external resource.
  TriggerGCWithTransitions(i_isolate1->heap());

  CHECK_EQ(shared_one_byte->map()->instance_type(),
           InstanceType::EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE);
  if (is_uncached) {
    CHECK(IsThinString(*shared_two_byte));
    CHECK(two_byte_res->IsDisposed());
  } else {
    CHECK_EQ(shared_two_byte->map()->instance_type(),
             InstanceType::EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE);
  }
}

UNINITIALIZED_TEST(ExternalizeAndInternalizeMissSharedString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  uint32_t one_byte_hash = one_byte->EnsureHash();

  Handle<String> shared_one_byte = ShareAndVerify(i_isolate1, one_byte);

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);

  CHECK(shared_one_byte->MakeExternal(one_byte_res));
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));

  DirectHandle<String> one_byte_intern =
      factory1->InternalizeString(shared_one_byte);
  CHECK_EQ(*one_byte_intern, *shared_one_byte);
  CHECK(IsInternalizedString(*shared_one_byte));
  // Check that we have both, a forwarding index and an accessible hash.
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_one_byte->HasHashCode());
  CHECK_EQ(shared_one_byte->hash(), one_byte_hash);
}

UNINITIALIZED_TEST(InternalizeHitAndExternalizeSharedString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte_vec(raw_two_byte, 3);

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> two_byte =
      factory1->NewStringFromTwoByte(two_byte_vec, AllocationType::kOld)
          .ToHandleChecked();
  Handle<String> shared_one_byte = ShareAndVerify(i_isolate1, one_byte);
  Handle<String> shared_two_byte = ShareAndVerify(i_isolate1, two_byte);
  // Internalize copies, s.t. internalizing the original strings creates a
  // forwarding entry.
  factory1->InternalizeString(
      factory1->NewStringFromAsciiChecked(raw_one_byte));
  factory1->InternalizeString(
      factory1->NewStringFromTwoByte(two_byte_vec).ToHandleChecked());
  DirectHandle<String> one_byte_intern =
      factory1->InternalizeString(shared_one_byte);
  DirectHandle<String> two_byte_intern =
      factory1->InternalizeString(shared_two_byte);
  CHECK_NE(*one_byte_intern, *shared_one_byte);
  CHECK_NE(*two_byte_intern, *shared_two_byte);
  CHECK(String::IsHashFieldComputed(one_byte_intern->raw_hash_field()));
  CHECK(String::IsHashFieldComputed(two_byte_intern->raw_hash_field()));
  CHECK(shared_one_byte->HasInternalizedForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasInternalizedForwardingIndex(kAcquireLoad));

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);
  TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte_vec);
  CHECK(shared_one_byte->MakeExternal(one_byte_res));
  CHECK(shared_two_byte->MakeExternal(two_byte_res));
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_one_byte->HasInternalizedForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasInternalizedForwardingIndex(kAcquireLoad));

  // Check that API calls return the resource from the forwarding table.
  CheckExternalStringResource(shared_one_byte, one_byte_res);
  CheckExternalStringResource(shared_two_byte, two_byte_res);
}

UNINITIALIZED_TEST(InternalizeMissAndExternalizeSharedString) {
  if (v8_flags.single_generation) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;
  Isolate* i_isolate1 = test.i_main_isolate();
  Factory* factory1 = i_isolate1->factory();

  HandleScope handle_scope(i_isolate1);

  const char raw_one_byte[] = "external string";
  base::uc16 raw_two_byte[] = {2001, 2002, 2003};
  base::Vector<base::uc16> two_byte_vec(raw_two_byte, 3);

  Handle<String> one_byte =
      factory1->NewStringFromAsciiChecked(raw_one_byte, AllocationType::kOld);
  Handle<String> two_byte =
      factory1->NewStringFromTwoByte(two_byte_vec, AllocationType::kOld)
          .ToHandleChecked();
  Handle<String> shared_one_byte = ShareAndVerify(i_isolate1, one_byte);
  Handle<String> shared_two_byte = ShareAndVerify(i_isolate1, two_byte);
  DirectHandle<String> one_byte_intern =
      factory1->InternalizeString(shared_one_byte);
  DirectHandle<String> two_byte_intern =
      factory1->InternalizeString(shared_two_byte);
  CHECK_EQ(*one_byte_intern, *shared_one_byte);
  CHECK_EQ(*two_byte_intern, *shared_two_byte);
  CHECK(!shared_one_byte->HasInternalizedForwardingIndex(kAcquireLoad));
  CHECK(!shared_two_byte->HasInternalizedForwardingIndex(kAcquireLoad));

  OneByteResource* one_byte_res = resource_factory.CreateOneByte(raw_one_byte);
  TwoByteResource* two_byte_res = resource_factory.CreateTwoByte(two_byte_vec);
  CHECK(shared_one_byte->MakeExternal(one_byte_res));
  CHECK(shared_two_byte->MakeExternal(two_byte_res));
  CHECK(shared_one_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(shared_two_byte->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(one_byte_intern->HasExternalForwardingIndex(kAcquireLoad));
  CHECK(two_byte_intern->HasExternalForwardingIndex(kAcquireLoad));

  // Check that API calls return the resource from the forwarding table.
  CheckExternalStringResource(shared_one_byte, one_byte_res);
  CheckExternalStringResource(shared_two_byte, two_byte_res);
}

class ConcurrentExternalizationThread final
    : public ConcurrentStringThreadBase {
 public:
  ConcurrentExternalizationThread(MultiClientIsolateTest* test,
                                  IndirectHandle<FixedArray> shared_strings,
                                  std::vector<OneByteResource*> resources,
                                  bool share_resources,
                                  ParkingSemaphore* sema_ready,
                                  ParkingSemaphore* sema_execute_start,
                                  ParkingSemaphore* sema_execute_complete)
      : ConcurrentStringThreadBase("ConcurrentExternalizationThread", test,
                                   shared_strings, sema_ready,
                                   sema_execute_start, sema_execute_complete),
        resources_(resources),
        share_resources_(share_resources) {}

  void RunForString(Handle<String> input_string, int counter) override {
    CHECK(input_string->IsShared());
    OneByteResource* resource = Resource(counter);
    if (!input_string->MakeExternal(resource)) {
      if (!share_resources_) resource->Dispose();
    }
    CHECK(input_string->HasForwardingIndex(kAcquireLoad));
  }

  OneByteResource* Resource(int index) const { return resources_[index]; }

 private:
  std::vector<OneByteResource*> resources_;
  const bool share_resources_;
};

namespace {

void CreateExternalResources(Isolate* i_isolate,
                             DirectHandle<FixedArray> strings,
                             std::vector<OneByteResource*>& resources,
                             ExternalResourceFactory& resource_factory) {
  HandleScope scope(i_isolate);
  resources.reserve(strings->length());
  for (int i = 0; i < strings->length(); i++) {
    DirectHandle<String> input_string(Cast<String>(strings->get(i)), i_isolate);
    CHECK(Utils::ToLocal(input_string)
              ->CanMakeExternal(v8::String::Encoding::ONE_BYTE_ENCODING));
    const int length = input_string->length();
    char* buffer = new char[length + 1];
    String::WriteToFlat(*input_string, reinterpret_cast<uint8_t*>(buffer), 0,
                        length);
    resources.push_back(resource_factory.CreateOneByte(buffer, length, false));
  }
}

void CheckStringAndResource(
    Tagged<String> string, int index, bool should_be_alive,
    Tagged<String> deleted_string, bool check_transition, bool shared_resources,
    const std::vector<std::unique_ptr<ConcurrentExternalizationThread>>&
        threads) {
  if (check_transition) {
    if (should_be_alive) {
      CHECK(IsExternalString(string));
    } else {
      CHECK_EQ(string, deleted_string);
    }
  }
  int alive_resources = 0;
  for (size_t t = 0; t < threads.size(); t++) {
    ConcurrentExternalizationThread* thread = threads[t].get();
    if (!thread->Resource(index)->IsDisposed()) {
      alive_resources++;
    }
  }

  // Check exact alive resources only if the string has transitioned, otherwise
  // there can still be multiple resource instances in the forwarding table.
  // Only check no resource is alive if the string is dead.
  const bool check_alive = check_transition || !should_be_alive;
  if (check_alive) {
    size_t expected_alive;
    if (should_be_alive) {
      if (shared_resources) {
        // Since we share the same resource for all threads, we accounted for it
        // in every thread.
        expected_alive = threads.size();
      } else {
        // Check that exactly one resource is alive.
        expected_alive = 1;
      }
    } else {
      expected_alive = 0;
    }
    CHECK_EQ(alive_resources, expected_alive);
  }
}

}  // namespace

void TestConcurrentExternalization(bool share_resources) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;

  constexpr int kThreads = 4;
  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  IndirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
      i_isolate, factory, kStrings - kLOStrings, kLOStrings,
      sizeof(UncachedExternalString), false);

  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<ConcurrentExternalizationThread>> threads;
  std::vector<OneByteResource*> shared_resources;

  if (share_resources) {
    CreateExternalResources(i_isolate, shared_strings, shared_resources,
                            resource_factory);
  }

  for (int i = 0; i < kThreads; i++) {
    std::vector<OneByteResource*> local_resources;
    if (share_resources) {
      local_resources = shared_resources;
    } else {
      CreateExternalResources(i_isolate, shared_strings, local_resources,
                              resource_factory);
    }
    auto thread = std::make_unique<ConcurrentExternalizationThread>(
        &test, shared_strings, local_resources, share_resources, &sema_ready,
        &sema_execute_start, &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_isolate->main_thread_local_isolate();
  for (int i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_start.Signal();
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  TriggerGCWithTransitions(i_isolate->heap());

  for (int i = 0; i < shared_strings->length(); i++) {
    DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                      i_isolate);
    Tagged<String> string = *input_string;
    CheckStringAndResource(string, i, true, {}, true, share_resources, threads);
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
}

UNINITIALIZED_TEST(ConcurrentExternalizationWithUniqueResources) {
  TestConcurrentExternalization(false);
}

UNINITIALIZED_TEST(ConcurrentExternalizationWithSharedResources) {
  TestConcurrentExternalization(true);
}

void TestConcurrentExternalizationWithDeadStrings(bool share_resources,
                                                  bool transition_with_stack) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;

  constexpr int kThreads = 4;
  constexpr int kStrings = 12;
  constexpr int kLOStrings = 2;

  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  ManualGCScope manual_gc_scope(i_isolate);
  HandleScope scope(i_isolate);

  IndirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
      i_isolate, factory, kStrings - kLOStrings, kLOStrings,
      sizeof(UncachedExternalString), false);

  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<ConcurrentExternalizationThread>> threads;
  std::vector<OneByteResource*> shared_resources;

  if (share_resources) {
    CreateExternalResources(i_isolate, shared_strings, shared_resources,
                            resource_factory);
  }

  for (int i = 0; i < kThreads; i++) {
    std::vector<OneByteResource*> local_resources;
    if (share_resources) {
      local_resources = shared_resources;
    } else {
      CreateExternalResources(i_isolate, shared_strings, local_resources,
                              resource_factory);
    }
    auto thread = std::make_unique<ConcurrentExternalizationThread>(
        &test, shared_strings, local_resources, share_resources, &sema_ready,
        &sema_execute_start, &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_isolate->main_thread_local_isolate();
  for (int i = 0; i < kThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_start.Signal();
  }
  for (int i = 0; i < kThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  DirectHandle<String> empty_string(
      ReadOnlyRoots(i_isolate->heap()).empty_string(), i_isolate);
  for (int i = 0; i < shared_strings->length(); i++) {
    DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                      i_isolate);
    // Patch every third string to empty. The next GC will dispose the external
    // resources.
    if (i % 3 == 0) {
      input_string.PatchValue(*empty_string);
      shared_strings->set(i, *input_string);
    }
  }

  v8_flags.transition_strings_during_gc_with_stack = transition_with_stack;
  i_isolate->heap()->CollectGarbageShared(i_isolate->main_thread_local_heap(),
                                          GarbageCollectionReason::kTesting);

  for (int i = 0; i < shared_strings->length(); i++) {
    DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                      i_isolate);
    const bool should_be_alive = i % 3 != 0;
    Tagged<String> string = *input_string;
    CheckStringAndResource(string, i, should_be_alive, *empty_string,
                           transition_with_stack, share_resources, threads);
  }

  // If we didn't test transitions during GC with stack, trigger another GC
  // (allowing transitions with stack) to ensure everything is handled
  // correctly.
  if (!transition_with_stack) {
    v8_flags.transition_strings_during_gc_with_stack = true;

    i_isolate->heap()->CollectGarbageShared(i_isolate->main_thread_local_heap(),
                                            GarbageCollectionReason::kTesting);

    for (int i = 0; i < shared_strings->length(); i++) {
      DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                        i_isolate);
      const bool should_be_alive = i % 3 != 0;
      Tagged<String> string = *input_string;
      CheckStringAndResource(string, i, should_be_alive, *empty_string, true,
                             share_resources, threads);
    }
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
}

UNINITIALIZED_TEST(
    ExternalizationWithDeadStringsAndUniqueResourcesTransitionWithStack) {
  TestConcurrentExternalizationWithDeadStrings(false, true);
}

UNINITIALIZED_TEST(
    ExternalizationWithDeadStringsAndSharedResourcesTransitionWithStack) {
  TestConcurrentExternalizationWithDeadStrings(true, true);
}

UNINITIALIZED_TEST(ExternalizationWithDeadStringsAndUniqueResources) {
  TestConcurrentExternalizationWithDeadStrings(false, false);
}

UNINITIALIZED_TEST(ExternalizationWithDeadStringsAndSharedResources) {
  TestConcurrentExternalizationWithDeadStrings(true, false);
}

void TestConcurrentExternalizationAndInternalization(
    TestHitOrMiss hit_or_miss) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  ExternalResourceFactory resource_factory;
  MultiClientIsolateTest test;

  constexpr int kInternalizationThreads = 4;
  constexpr int kExternalizationThreads = 4;
  constexpr int kTotalThreads =
      kInternalizationThreads + kExternalizationThreads;
  constexpr int kStrings = 4096;
  constexpr int kLOStrings = 16;

  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope scope(i_isolate);

  IndirectHandle<FixedArray> shared_strings = CreateSharedOneByteStrings(
      i_isolate, factory, kStrings - kLOStrings, kLOStrings,
      sizeof(UncachedExternalString), hit_or_miss == kTestHit);

  ParkingSemaphore sema_ready(0);
  ParkingSemaphore sema_execute_start(0);
  ParkingSemaphore sema_execute_complete(0);
  std::vector<std::unique_ptr<ConcurrentStringThreadBase>> threads;
  for (int i = 0; i < kInternalizationThreads; i++) {
    auto thread = std::make_unique<ConcurrentInternalizationThread>(
        &test, shared_strings, hit_or_miss, &sema_ready, &sema_execute_start,
        &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }
  for (int i = 0; i < kExternalizationThreads; i++) {
    std::vector<OneByteResource*> resources;
    CreateExternalResources(i_isolate, shared_strings, resources,
                            resource_factory);
    auto thread = std::make_unique<ConcurrentExternalizationThread>(
        &test, shared_strings, resources, false, &sema_ready,
        &sema_execute_start, &sema_execute_complete);
    CHECK(thread->Start());
    threads.push_back(std::move(thread));
  }

  LocalIsolate* local_isolate = i_isolate->main_thread_local_isolate();
  for (int i = 0; i < kTotalThreads; i++) {
    sema_ready.ParkedWait(local_isolate);
  }
  for (int i = 0; i < kTotalThreads; i++) {
    sema_execute_start.Signal();
  }
  for (int i = 0; i < kTotalThreads; i++) {
    sema_execute_complete.ParkedWait(local_isolate);
  }

  TriggerGCWithTransitions(i_isolate->heap());

  for (int i = 0; i < shared_strings->length(); i++) {
    DirectHandle<String> input_string(Cast<String>(shared_strings->get(i)),
                                      i_isolate);
    Tagged<String> string = *input_string;
    if (hit_or_miss == kTestHit) {
      CHECK(IsThinString(string));
      string = Cast<ThinString>(string)->actual();
    }
    int alive_resources = 0;
    for (int t = kInternalizationThreads; t < kTotalThreads; t++) {
      ConcurrentExternalizationThread* thread =
          reinterpret_cast<ConcurrentExternalizationThread*>(threads[t].get());
      if (!thread->Resource(i)->IsDisposed()) {
        alive_resources++;
      }
    }

    StringShape shape(string);
    CHECK(shape.IsInternalized());
    // Check at most one external resource is alive.
    // If internalization happens on an external string and we already have an
    // internalized string with the same content, we turn it into a ThinString
    // and dispose the resource.
    CHECK_LE(alive_resources, 1);
    CHECK_EQ(shape.IsExternal(), alive_resources);
    CHECK(string->HasHashCode());
  }

  ParkingThread::ParkedJoinAll(local_isolate, threads);
}

UNINITIALIZED_TEST(ConcurrentExternalizationAndInternalizationMiss) {
  TestConcurrentExternalizationAndInternalization(kTestMiss);
}

UNINITIALIZED_TEST(ConcurrentExternalizationAndInternalizationHit) {
  TestConcurrentExternalizationAndInternalization(kTestHit);
}

UNINITIALIZED_TEST(SharedStringInGlobalHandle) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  Isolate* i_isolate = test.i_main_isolate();
  Factory* factory = i_isolate->factory();

  HandleScope handle_scope(i_isolate);
  Handle<String> shared_string =
      factory->NewStringFromAsciiChecked("foobar", AllocationType::kSharedOld);
  CHECK(InWritableSharedSpace(*shared_string));
  v8::Local<v8::String> lh_shared_string = Utils::ToLocal(shared_string);
  v8::Global<v8::String> gh_shared_string(test.main_isolate(),
                                          lh_shared_string);
  gh_shared_string.SetWeak();

  heap::InvokeMajorGC(i_isolate->heap());

  CHECK(!gh_shared_string.IsEmpty());
}

class WakeupTask : public CancelableTask {
 public:
  explicit WakeupTask(Isolate* isolate, int& wakeup_counter)
      : CancelableTask(isolate), wakeup_counter_(wakeup_counter) {}

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override { (wakeup_counter_)++; }

  int& wakeup_counter_;
};

class WorkerIsolateThread : public v8::base::Thread {
 public:
  WorkerIsolateThread(const char* name, MultiClientIsolateTest* test)
      : v8::base::Thread(base::Thread::Options(name)), test_(test) {}

  void Run() override {
    v8::Isolate* client = test_->NewClientIsolate();
    Isolate* i_client = reinterpret_cast<Isolate*>(client);
    Factory* factory = i_client->factory();

    v8::Global<v8::String> gh_shared_string;

    {
      HandleScope handle_scope(i_client);
      Handle<String> shared_string = factory->NewStringFromAsciiChecked(
          "foobar", AllocationType::kSharedOld);
      CHECK(InWritableSharedSpace(*shared_string));
      v8::Local<v8::String> lh_shared_string = Utils::ToLocal(shared_string);
      gh_shared_string.Reset(test_->main_isolate(), lh_shared_string);
      gh_shared_string.SetWeak();
    }

    {
      // We need to invoke GC without stack, otherwise some objects may survive.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(
          i_client->heap());
      i_client->heap()->CollectGarbageShared(i_client->main_thread_local_heap(),
                                             GarbageCollectionReason::kTesting);
    }

    CHECK(gh_shared_string.IsEmpty());
    client->Dispose();

    V8::GetCurrentPlatform()
        ->GetForegroundTaskRunner(test_->main_isolate())
        ->PostTask(std::make_unique<WakeupTask>(
            test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));
  }

 private:
  MultiClientIsolateTest* test_;
};

UNINITIALIZED_TEST(SharedStringInClientGlobalHandle) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.shared_string_table = true;

  MultiClientIsolateTest test;
  ManualGCScope manual_gc_scope(test.i_main_isolate());
  WorkerIsolateThread thread("worker", &test);
  CHECK(thread.Start());

  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

class ClientIsolateThreadForPagePromotions : public v8::base::Thread {
 public:
  // Expects a ManualGCScope to be in scope while `Run()` is executed.
  ClientIsolateThreadForPagePromotions(const char* name,
                                       MultiClientIsolateTest* test,
                                       Handle<String>* shared_string,
                                       const ManualGCScope& witness)
      : v8::base::Thread(base::Thread::Options(name)),
        test_(test),
        shared_string_(shared_string) {}

  void Run() override {
    CHECK(v8_flags.minor_ms);
    v8::Isolate* client = test_->NewClientIsolate();
    Isolate* i_client = reinterpret_cast<Isolate*>(client);
    Factory* factory = i_client->factory();
    Heap* heap = i_client->heap();

    {
      v8::Isolate::Scope isolate_scope(client);
      HandleScope handle_scope(i_client);

      DirectHandle<FixedArray> young_object =
          factory->NewFixedArray(1, AllocationType::kYoung);
      CHECK(Heap::InYoungGeneration(*young_object));
      Address young_object_address = young_object->address();

      std::vector<Handle<FixedArray>> handles;
      // Make the whole page transition from new->old, getting the buffers
      // processed in the sweeper (relying on marking information) instead of
      // processing during newspace evacuation.
      heap::FillCurrentPage(heap->new_space(), &handles);

      CHECK(!heap->Contains(**shared_string_));
      CHECK(heap->SharedHeapContains(**shared_string_));
      young_object->set(0, **shared_string_);

      heap::EmptyNewSpaceUsingGC(heap);
      heap->CompleteSweepingFull();

      // Object should get promoted using page promotion, so address should
      // remain the same.
      CHECK(!Heap::InYoungGeneration(*young_object));
      CHECK(heap->Contains(*young_object));
      CHECK_EQ(young_object_address, young_object->address());

      // Since the GC promoted that string into shared heap, it also needs to
      // create an OLD_TO_SHARED slot.
      ObjectSlot slot = young_object->RawFieldOfFirstElement();
      CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
          MutablePageMetadata::FromHeapObject(*young_object), slot.address()));
    }

    client->Dispose();

    V8::GetCurrentPlatform()
        ->GetForegroundTaskRunner(test_->main_isolate())
        ->PostTask(std::make_unique<WakeupTask>(
            test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));
  }

 private:
  MultiClientIsolateTest* test_;
  Handle<String>* shared_string_;
};

UNINITIALIZED_TEST(RegisterOldToSharedForPromotedPageFromClient) {
  if (v8_flags.single_generation) return;
  if (!v8_flags.minor_ms) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  MultiClientIsolateTest test;

  Isolate* i_isolate = test.i_main_isolate();
  Isolate* shared_isolate = i_isolate->shared_space_isolate();
  Heap* shared_heap = shared_isolate->heap();

  HandleScope scope(i_isolate);

  const char raw_one_byte[] = "foo";
  Handle<String> shared_string =
      i_isolate->factory()->NewStringFromAsciiChecked(
          raw_one_byte, AllocationType::kSharedOld);
  CHECK(shared_heap->Contains(*shared_string));

  ClientIsolateThreadForPagePromotions thread("worker", &test, &shared_string,
                                              manual_gc_scope);
  CHECK(thread.Start());

  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

UNINITIALIZED_TEST(
    RegisterOldToSharedForPromotedPageFromClientDuringIncrementalMarking) {
  if (v8_flags.single_generation) return;
  if (!v8_flags.minor_ms) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  v8_flags.incremental_marking_task =
      false;  // Prevent the incremental GC from finishing and finalizing in a
              // task.

  MultiClientIsolateTest test;

  Isolate* i_isolate = test.i_main_isolate();
  Isolate* shared_isolate = i_isolate->shared_space_isolate();
  Heap* shared_heap = shared_isolate->heap();

  HandleScope scope(i_isolate);

  const char raw_one_byte[] = "foo";
  Handle<String> shared_string =
      i_isolate->factory()->NewStringFromAsciiChecked(
          raw_one_byte, AllocationType::kSharedOld);
  CHECK(shared_heap->Contains(*shared_string));

  // Start an incremental shared GC such that shared_string resides on an
  // evacuation candidate.
  heap::ForceEvacuationCandidate(PageMetadata::FromHeapObject(*shared_string));
  i::IncrementalMarking* marking = shared_heap->incremental_marking();
  CHECK(marking->IsStopped());
  {
    IsolateSafepointScope safepoint_scope(shared_heap);
    shared_heap->tracer()->StartCycle(
        GarbageCollector::MARK_COMPACTOR, GarbageCollectionReason::kTesting,
        "collector cctest", GCTracer::MarkingType::kIncremental);
    marking->Start(GarbageCollector::MARK_COMPACTOR,
                   i::GarbageCollectionReason::kTesting);
  }

  ClientIsolateThreadForPagePromotions thread("worker", &test, &shared_string,
                                              manual_gc_scope);
  CHECK(thread.Start());

  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

class ClientIsolateThreadForRetainingByRememberedSet : public v8::base::Thread {
 public:
  // Expects a ManualGCScope to be in scope while `Run()` is executed.
  ClientIsolateThreadForRetainingByRememberedSet(
      const char* name, MultiClientIsolateTest* test,
      Persistent<v8::String>* weak_ref, const ManualGCScope& witness)
      : v8::base::Thread(base::Thread::Options(name)),
        test_(test),
        weak_ref_(weak_ref) {}

  void Run() override {
    CHECK(v8_flags.minor_ms);
    client_isolate_ = test_->NewClientIsolate();
    Isolate* i_client = reinterpret_cast<Isolate*>(client_isolate_);
    Factory* factory = i_client->factory();
    Heap* heap = i_client->heap();

    {
      v8::Isolate::Scope isolate_scope(client_isolate_);
      HandleScope scope(i_client);

      IndirectHandle<FixedArray> young_object =
          factory->NewFixedArray(1, AllocationType::kYoung);
      CHECK(Heap::InYoungGeneration(*young_object));
      Address young_object_address = young_object->address();

      std::vector<IndirectHandle<FixedArray>> handles;
      // Make the whole page transition from new->old, getting the buffers
      // processed in the sweeper (relying on marking information) instead of
      // processing during newspace evacuation.
      heap::FillCurrentPage(heap->new_space(), &handles);

      // Create a new to shared reference.
      CHECK(!weak_ref_->IsEmpty());
      IndirectHandle<String> shared_string =
          Utils::OpenHandle<v8::String, String>(
              weak_ref_->Get(client_isolate_));
      CHECK(!heap->Contains(*shared_string));
      CHECK(heap->SharedHeapContains(*shared_string));
      young_object->set(0, *shared_string);

      heap::EmptyNewSpaceUsingGC(heap);

      // Object should get promoted using page promotion, so address should
      // remain the same.
      CHECK(!Heap::InYoungGeneration(*young_object));
      CHECK(heap->Contains(*young_object));
      CHECK_EQ(young_object_address, young_object->address());

      // GC should still be in progress (unless heap verification is enabled).
      CHECK_IMPLIES(!v8_flags.verify_heap, heap->sweeping_in_progress());

      // Inform main thread that the client is set up and is doing a GC.
      V8::GetCurrentPlatform()
          ->GetForegroundTaskRunner(test_->main_isolate())
          ->PostTask(std::make_unique<WakeupTask>(
              test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));

      // We need to ensure that the shared GC does not scan the stack for this
      // client, otherwise some objects may survive.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

      // Wait for main thread to do a shared GC.
      while (wakeup_counter_ < 1) {
        v8::platform::PumpMessageLoop(
            i::V8::GetCurrentPlatform(), isolate(),
            v8::platform::MessageLoopBehavior::kWaitForWork);
      }

      // Since the GC promoted that string into shared heap, it also needs to
      // create an OLD_TO_SHARED slot.
      ObjectSlot slot = young_object->RawFieldOfFirstElement();
      CHECK(RememberedSet<OLD_TO_SHARED>::Contains(
          MutablePageMetadata::FromHeapObject(*young_object), slot.address()));
    }

    client_isolate_->Dispose();

    // Inform main thread that client is finished.
    V8::GetCurrentPlatform()
        ->GetForegroundTaskRunner(test_->main_isolate())
        ->PostTask(std::make_unique<WakeupTask>(
            test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));
  }

  v8::Isolate* isolate() const {
    DCHECK_NOT_NULL(client_isolate_);
    return client_isolate_;
  }

  int& wakeup_counter() { return wakeup_counter_; }

 private:
  MultiClientIsolateTest* test_;
  Persistent<v8::String>* weak_ref_;
  v8::Isolate* client_isolate_;
  int wakeup_counter_ = 0;
};

UNINITIALIZED_TEST(SharedObjectRetainedByClientRememberedSet) {
  if (v8_flags.single_generation) return;
  if (!v8_flags.minor_ms) return;
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;

  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  v8_flags.shared_string_table = true;
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  MultiClientIsolateTest test;

  v8::Isolate* isolate = test.main_isolate();
  Isolate* i_isolate = test.i_main_isolate();
  Isolate* shared_isolate = i_isolate->shared_space_isolate();
  Heap* shared_heap = shared_isolate->heap();

  // We need to invoke GC without stack, otherwise some objects may survive.
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      shared_heap);

  // Create two weak references to Strings. One should die, the other should be
  // kept alive by the client isolate.
  Persistent<v8::String> live_weak_ref;
  Persistent<v8::String> dead_weak_ref;
  {
    HandleScope scope(i_isolate);
    const char raw_one_byte[] = "foo";

    Handle<String> live_shared_string =
        i_isolate->factory()->NewStringFromAsciiChecked(
            raw_one_byte, AllocationType::kSharedOld);
    CHECK(shared_heap->Contains(*live_shared_string));
    live_weak_ref.Reset(isolate, Utils::ToLocal(live_shared_string));
    live_weak_ref.SetWeak();

    Handle<String> dead_shared_string =
        i_isolate->factory()->NewStringFromAsciiChecked(
            raw_one_byte, AllocationType::kSharedOld);
    CHECK(shared_heap->Contains(*dead_shared_string));
    dead_weak_ref.Reset(isolate, Utils::ToLocal(dead_shared_string));
    dead_weak_ref.SetWeak();
  }

  ClientIsolateThreadForRetainingByRememberedSet thread(
      "worker", &test, &live_weak_ref, manual_gc_scope);
  CHECK(thread.Start());

  // Wait for client isolate to allocate objects and start a GC.
  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  // Do shared GC. The live weak ref should be kept alive via a OLD_TO_SHARED
  // slot in the client isolate.
  CHECK(!live_weak_ref.IsEmpty());
  CHECK(!dead_weak_ref.IsEmpty());
  heap::CollectSharedGarbage(i_isolate->heap());
  CHECK(!live_weak_ref.IsEmpty());
  CHECK(dead_weak_ref.IsEmpty());

  // Inform client that shared GC is finished.
  auto thread_wakeup_task = std::make_unique<WakeupTask>(
      reinterpret_cast<Isolate*>(thread.isolate()), thread.wakeup_counter());
  V8::GetCurrentPlatform()
      ->GetForegroundTaskRunner(thread.isolate())
      ->PostTask(std::move(thread_wakeup_task));

  while (test.main_isolate_wakeup_counter() < 2) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

class Regress1424955ClientIsolateThread : public v8::base::Thread {
 public:
  Regress1424955ClientIsolateThread(const char* name,
                                    MultiClientIsolateTest* test)
      : v8::base::Thread(base::Thread::Options(name)), test_(test) {}

  void Run() override {
    client_isolate_ = test_->NewClientIsolate();
    Isolate* i_client = reinterpret_cast<Isolate*>(client_isolate_);
    Heap* i_client_heap = i_client->heap();
    Factory* factory = i_client->factory();

    {
      // Allocate an object so that there is work for the sweeper. Otherwise,
      // starting a minor GC after a full GC may finalize sweeping since it is
      // out of work.
      HandleScope handle_scope(i_client);
      Handle<FixedArray> array =
          factory->NewFixedArray(64, AllocationType::kOld);
      USE(array);

      // Start sweeping.
      heap::InvokeMajorGC(i_client_heap);
      CHECK(i_client_heap->sweeping_in_progress());

      // Inform the initiator thread it's time to request a global safepoint.
      V8::GetCurrentPlatform()
          ->GetForegroundTaskRunner(test_->main_isolate())
          ->PostTask(std::make_unique<WakeupTask>(
              test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));

      // Wait for the initiator thread to request a global safepoint.
      while (!i_client->shared_space_isolate()
                  ->global_safepoint()
                  ->IsRequestedForTesting()) {
        v8::base::OS::Sleep(v8::base::TimeDelta::FromMilliseconds(1));
      }

      // Start a minor GC. This will cause this client isolate to join the
      // global safepoint. At which point, the initiator isolate will try to
      // finalize sweeping on behalf of this client isolate.
      heap::InvokeMinorGC(i_client_heap);
    }

    // Wait for the initiator isolate to finish the shared GC.
    while (wakeup_counter_ < 1) {
      v8::platform::PumpMessageLoop(
          i::V8::GetCurrentPlatform(), client_isolate_,
          v8::platform::MessageLoopBehavior::kWaitForWork);
    }

    client_isolate_->Dispose();

    V8::GetCurrentPlatform()
        ->GetForegroundTaskRunner(test_->main_isolate())
        ->PostTask(std::make_unique<WakeupTask>(
            test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));
  }

  v8::Isolate* isolate() const {
    DCHECK_NOT_NULL(client_isolate_);
    return client_isolate_;
  }

  int& wakeup_counter() { return wakeup_counter_; }

 private:
  MultiClientIsolateTest* test_;
  v8::Isolate* client_isolate_;
  int wakeup_counter_ = 0;
};

UNINITIALIZED_TEST(Regress1424955) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  if (v8_flags.single_generation) return;
  // When heap verification is enabled, sweeping is finalized in the atomic
  // pause. This issue requires that sweeping is still in progress after the
  // atomic pause is finished.
  if (v8_flags.verify_heap) return;
  v8_flags.shared_string_table = true;

  ManualGCScope manual_gc_scope;

  MultiClientIsolateTest test;
  Regress1424955ClientIsolateThread thread("worker", &test);
  CHECK(thread.Start());

  // Wait for client thread to start sweeping.
  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  // Client isolate waits for this isolate to request a global safepoint and
  // then triggers a minor GC.
  heap::CollectSharedGarbage(test.i_main_isolate()->heap());
  V8::GetCurrentPlatform()
      ->GetForegroundTaskRunner(thread.isolate())
      ->PostTask(std::make_unique<WakeupTask>(
          reinterpret_cast<Isolate*>(thread.isolate()),
          thread.wakeup_counter()));

  // Wait for client isolate to finish the minor GC and dispose of its isolate.
  while (test.main_isolate_wakeup_counter() < 2) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

class ProtectExternalStringTableAddStringClientIsolateThread
    : public v8::base::Thread {
 public:
  ProtectExternalStringTableAddStringClientIsolateThread(
      const char* name, MultiClientIsolateTest* test, v8::Isolate* isolate)
      : v8::base::Thread(base::Thread::Options(name)),
        test_(test),
        isolate_(isolate),
        i_isolate_(reinterpret_cast<Isolate*>(isolate)) {}

  void Run() override {
    const char* text = "worker_external_string";

    {
      v8::Isolate::Scope isolate_scope(isolate_);

      for (int i = 0; i < 1'000; i++) {
        HandleScope scope(i_isolate_);
        DirectHandle<String> string =
            i_isolate_->factory()->NewStringFromAsciiChecked(
                text, AllocationType::kOld);
        CHECK(InWritableSharedSpace(*string));
        CHECK(!string->IsShared());
        CHECK(string->MakeExternal(new StaticOneByteResource(text)));
        CHECK(IsExternalOneByteString(*string));
      }
    }

    isolate_->Dispose();

    V8::GetCurrentPlatform()
        ->GetForegroundTaskRunner(test_->main_isolate())
        ->PostTask(std::make_unique<WakeupTask>(
            test_->i_main_isolate(), test_->main_isolate_wakeup_counter()));
  }

 private:
  MultiClientIsolateTest* test_;
  v8::Isolate* isolate_;
  Isolate* i_isolate_;
};

UNINITIALIZED_TEST(ProtectExternalStringTableAddString) {
  if (!V8_CAN_CREATE_SHARED_HEAP_BOOL) return;
  v8_flags.shared_string_table = true;

  ManualGCScope manual_gc_scope;

  MultiClientIsolateTest test;
  v8::Isolate* client = test.NewClientIsolate();
  ProtectExternalStringTableAddStringClientIsolateThread thread("worker", &test,
                                                                client);
  CHECK(thread.Start());
  Isolate* isolate = test.i_main_isolate();
  HandleScope scope(isolate);

  for (int i = 0; i < 1'000; i++) {
    isolate->factory()
        ->NewExternalStringFromOneByte(
            new StaticOneByteResource("main_external_string"))
        .Check();
  }

  // Wait for client isolate to finish the minor GC and dispose of its isolate.
  while (test.main_isolate_wakeup_counter() < 1) {
    v8::platform::PumpMessageLoop(
        i::V8::GetCurrentPlatform(), test.main_isolate(),
        v8::platform::MessageLoopBehavior::kWaitForWork);
  }

  thread.Join();
}

}  // namespace test_shared_strings
}  // namespace internal
}  // namespace v8
