// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/local-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using ConcurrentStringTest = TestWithContext;

namespace internal {

namespace {

#define DOUBLE_VALUE 28.123456789
#define STRING_VALUE "28.123456789"
#define ARRAY_VALUE \
  { '2', '8', '.', '1', '2', '3', '4', '5', '6', '7', '8', '9' }

// Adapted from cctest/test-api.cc, and
// test/cctest/heap/test-external-string-tracker.cc.
class TestOneByteResource : public v8::String::ExternalOneByteStringResource {
 public:
  explicit TestOneByteResource(const char* data)
      : data_(data), length_(strlen(data)) {}

  ~TestOneByteResource() override { i::DeleteArray(data_); }

  const char* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  const char* data_;
  size_t length_;
};

// Adapted from cctest/test-api.cc.
class TestTwoByteResource : public v8::String::ExternalStringResource {
 public:
  explicit TestTwoByteResource(uint16_t* data) : data_(data), length_(0) {
    while (data[length_]) ++length_;
  }

  ~TestTwoByteResource() override { i::DeleteArray(data_); }

  const uint16_t* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  uint16_t* data_;
  size_t length_;
};

class ConcurrentStringThread final : public v8::base::Thread {
 public:
  ConcurrentStringThread(Isolate* isolate, Handle<String> str,
                         std::unique_ptr<PersistentHandles> ph,
                         base::Semaphore* sema_started,
                         std::vector<uint16_t> chars)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        isolate_(isolate),
        str_(str),
        ph_(std::move(ph)),
        sema_started_(sema_started),
        length_(chars.size()),
        chars_(chars) {}

  void Run() override {
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    local_isolate.heap()->AttachPersistentHandles(std::move(ph_));
    UnparkedScope unparked_scope(local_isolate.heap());

    sema_started_->Signal();
    // Check the three operations we do from the StringRef concurrently: get the
    // string, the nth character, and convert into a double.
    EXPECT_EQ(str_->length(kAcquireLoad), static_cast<int>(length_));
    for (unsigned int i = 0; i < length_; ++i) {
      EXPECT_EQ(str_->Get(i, &local_isolate), chars_[i]);
    }
    EXPECT_EQ(TryStringToDouble(&local_isolate, str_).value(), DOUBLE_VALUE);
  }

 private:
  Isolate* isolate_;
  Handle<String> str_;
  std::unique_ptr<PersistentHandles> ph_;
  base::Semaphore* sema_started_;
  uint64_t length_;
  std::vector<uint16_t> chars_;
};

// Inspect a one byte string, while the main thread externalizes it.
TEST_F(ConcurrentStringTest, InspectOneByteExternalizing) {
  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  auto factory = i_isolate()->factory();
  HandleScope handle_scope(i_isolate());

  // Crate an internalized one-byte string.
  const char* raw_string = STRING_VALUE;
  Handle<String> one_byte_string = factory->InternalizeString(
      factory->NewStringFromAsciiChecked(raw_string));
  EXPECT_TRUE(one_byte_string->IsOneByteRepresentation());
  EXPECT_TRUE(!IsExternalString(*one_byte_string));
  EXPECT_TRUE(IsInternalizedString(*one_byte_string));

  Handle<String> persistent_string = ph->NewHandle(one_byte_string);

  std::vector<uint16_t> chars;
  for (int i = 0; i < one_byte_string->length(); ++i) {
    chars.push_back(one_byte_string->Get(i));
  }

  base::Semaphore sema_started(0);

  std::unique_ptr<ConcurrentStringThread> thread(new ConcurrentStringThread(
      i_isolate(), persistent_string, std::move(ph), &sema_started, chars));
  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  // Externalize it to a one-byte external string.
  // We need to use StrDup in this case since the TestOneByteResource will get
  // ownership of raw_string otherwise.
  EXPECT_TRUE(one_byte_string->MakeExternal(
      new TestOneByteResource(i::StrDup(raw_string))));
  EXPECT_TRUE(IsExternalOneByteString(*one_byte_string));
  EXPECT_TRUE(IsInternalizedString(*one_byte_string));

  thread->Join();
}

// Inspect a two byte string, while the main thread externalizes it.
TEST_F(ConcurrentStringTest, InspectTwoByteExternalizing) {
  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  auto factory = i_isolate()->factory();
  HandleScope handle_scope(i_isolate());

  // Crate an internalized two-byte string.
  // TODO(solanes): Can we have only one raw string?
  const char* raw_string = STRING_VALUE;
  // TODO(solanes): Is this the best way to create a two byte string from chars?
  const int kLength = 12;
  const uint16_t two_byte_array[kLength] = ARRAY_VALUE;
  Handle<String> two_bytes_string;
  {
    Handle<SeqTwoByteString> raw =
        factory->NewRawTwoByteString(kLength).ToHandleChecked();
    DisallowGarbageCollection no_gc;
    CopyChars(raw->GetChars(no_gc), two_byte_array, kLength);
    two_bytes_string = raw;
  }
  two_bytes_string = factory->InternalizeString(two_bytes_string);
  EXPECT_TRUE(two_bytes_string->IsTwoByteRepresentation());
  EXPECT_TRUE(!IsExternalString(*two_bytes_string));
  EXPECT_TRUE(IsInternalizedString(*two_bytes_string));

  Handle<String> persistent_string = ph->NewHandle(two_bytes_string);
  std::vector<uint16_t> chars;
  for (int i = 0; i < two_bytes_string->length(); ++i) {
    chars.push_back(two_bytes_string->Get(i));
  }
  base::Semaphore sema_started(0);

  std::unique_ptr<ConcurrentStringThread> thread(new ConcurrentStringThread(
      i_isolate(), persistent_string, std::move(ph), &sema_started, chars));
  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  // Externalize it to a two-bytes external string.
  EXPECT_TRUE(two_bytes_string->MakeExternal(
      new TestTwoByteResource(AsciiToTwoByteString(raw_string))));
  EXPECT_TRUE(IsExternalTwoByteString(*two_bytes_string));
  EXPECT_TRUE(IsInternalizedString(*two_bytes_string));

  thread->Join();
}

// Inspect a one byte string, while the main thread externalizes it. Same as
// InspectOneByteExternalizing, but using thin strings.
TEST_F(ConcurrentStringTest, InspectOneByteExternalizing_ThinString) {
  // We will not create a thin string if single_generation is turned on.
  if (v8_flags.single_generation) return;
  // We don't create ThinStrings immediately when using the forwarding table.
  if (v8_flags.always_use_string_forwarding_table) return;
  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  auto factory = i_isolate()->factory();
  HandleScope handle_scope(i_isolate());

  // Create a string.
  const char* raw_string = STRING_VALUE;
  Handle<String> thin_string = factory->NewStringFromAsciiChecked(raw_string);
  EXPECT_TRUE(!IsExternalString(*thin_string));
  EXPECT_TRUE(!IsInternalizedString(*thin_string));

  // Crate an internalized one-byte version of that string string.
  DirectHandle<String> internalized_string =
      factory->InternalizeString(thin_string);
  EXPECT_TRUE(internalized_string->IsOneByteRepresentation());
  EXPECT_TRUE(!IsExternalString(*internalized_string));
  EXPECT_TRUE(IsInternalizedString(*internalized_string));

  // We now should have an internalized string, and a thin string pointing to
  // it.
  EXPECT_TRUE(IsThinString(*thin_string));
  EXPECT_NE(*thin_string, *internalized_string);

  Handle<String> persistent_string = ph->NewHandle(thin_string);

  std::vector<uint16_t> chars;
  for (int i = 0; i < thin_string->length(); ++i) {
    chars.push_back(thin_string->Get(i));
  }

  base::Semaphore sema_started(0);

  std::unique_ptr<ConcurrentStringThread> thread(new ConcurrentStringThread(
      i_isolate(), persistent_string, std::move(ph), &sema_started, chars));
  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  // Externalize it to a one-byte external string.
  // We need to use StrDup in this case since the TestOneByteResource will get
  // ownership of raw_string otherwise.
  EXPECT_TRUE(internalized_string->MakeExternal(
      new TestOneByteResource(i::StrDup(raw_string))));
  EXPECT_TRUE(IsExternalOneByteString(*internalized_string));
  EXPECT_TRUE(IsInternalizedString(*internalized_string));

  // Check that the thin string is unmodified.
  EXPECT_TRUE(!IsExternalString(*thin_string));
  EXPECT_TRUE(!IsInternalizedString(*thin_string));
  EXPECT_TRUE(IsThinString(*thin_string));

  thread->Join();
}

// Inspect a two byte string, while the main thread externalizes it. Same as
// InspectTwoByteExternalizing, but using thin strings.
TEST_F(ConcurrentStringTest, InspectTwoByteExternalizing_ThinString) {
  // We will not create a thin string if single_generation is turned on.
  if (v8_flags.single_generation) return;
  // We don't create ThinStrings immediately when using the forwarding table.
  if (v8_flags.always_use_string_forwarding_table) return;
  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  auto factory = i_isolate()->factory();
  HandleScope handle_scope(i_isolate());

  // Crate an internalized two-byte string.
  // TODO(solanes): Can we have only one raw string?
  const char* raw_string = STRING_VALUE;
  // TODO(solanes): Is this the best way to create a two byte string from chars?
  const int kLength = 12;
  const uint16_t two_byte_array[kLength] = ARRAY_VALUE;
  Handle<String> thin_string;
  {
    Handle<SeqTwoByteString> raw =
        factory->NewRawTwoByteString(kLength).ToHandleChecked();
    DisallowGarbageCollection no_gc;
    CopyChars(raw->GetChars(no_gc), two_byte_array, kLength);
    thin_string = raw;
  }

  DirectHandle<String> internalized_string =
      factory->InternalizeString(thin_string);
  EXPECT_TRUE(internalized_string->IsTwoByteRepresentation());
  EXPECT_TRUE(!IsExternalString(*internalized_string));
  EXPECT_TRUE(IsInternalizedString(*internalized_string));

  Handle<String> persistent_string = ph->NewHandle(thin_string);
  std::vector<uint16_t> chars;
  for (int i = 0; i < thin_string->length(); ++i) {
    chars.push_back(thin_string->Get(i));
  }
  base::Semaphore sema_started(0);

  std::unique_ptr<ConcurrentStringThread> thread(new ConcurrentStringThread(
      i_isolate(), persistent_string, std::move(ph), &sema_started, chars));
  EXPECT_TRUE(thread->Start());

  sema_started.Wait();

  // Externalize it to a two-bytes external string.
  EXPECT_TRUE(internalized_string->MakeExternal(
      new TestTwoByteResource(AsciiToTwoByteString(raw_string))));
  EXPECT_TRUE(IsExternalTwoByteString(*internalized_string));
  EXPECT_TRUE(IsInternalizedString(*internalized_string));

  // Check that the thin string is unmodified.
  EXPECT_TRUE(!IsExternalString(*thin_string));
  EXPECT_TRUE(!IsInternalizedString(*thin_string));
  EXPECT_TRUE(IsThinString(*thin_string));

  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
