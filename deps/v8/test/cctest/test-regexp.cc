// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "include/v8-regexp.h"
#include "src/api/api-inl.h"
#include "src/execution/frames-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

using namespace v8;

namespace {

const char kOneByteSubjectString[] = {
    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', '\0'};
const uint16_t kTwoByteSubjectString[] = {
    0xCF80, 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
    'a',    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
    'a',    'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', '\0'};

const int kSubjectStringLength = arraysize(kOneByteSubjectString) - 1;
static_assert(arraysize(kOneByteSubjectString) ==
              arraysize(kTwoByteSubjectString));

class OneByteVectorResource : public String::ExternalOneByteStringResource {
 public:
  explicit OneByteVectorResource(base::Vector<const char> vector)
      : data_(vector) {}
  ~OneByteVectorResource() override = default;
  size_t length() const override { return data_.length(); }
  const char* data() const override { return data_.begin(); }
  void Dispose() override {}

 private:
  base::Vector<const char> data_;
};

class UC16VectorResource : public String::ExternalStringResource {
 public:
  explicit UC16VectorResource(base::Vector<const base::uc16> vector)
      : data_(vector) {}
  ~UC16VectorResource() override = default;
  size_t length() const override { return data_.length(); }
  const base::uc16* data() const override { return data_.begin(); }
  void Dispose() override {}

 private:
  base::Vector<const base::uc16> data_;
};

OneByteVectorResource one_byte_string_resource(
    base::Vector<const char>(&kOneByteSubjectString[0], kSubjectStringLength));
UC16VectorResource two_byte_string_resource(base::Vector<const base::uc16>(
    &kTwoByteSubjectString[0], kSubjectStringLength));

class InterruptTest {
 public:
  InterruptTest()
      : i_thread(this),
        env_(),
        isolate_(env_->GetIsolate()),
        sem_(0),
        ran_test_body_(false),
        ran_to_completion_(false) {}

  void RunTest(InterruptCallback test_body_fn) {
    HandleScope handle_scope(isolate_);
    Local<RegExp> re =
        RegExp::New(env_.local(), v8_str("((a*)*)*b"), v8::RegExp::kNone)
            .ToLocalChecked();
    regexp_handle_.Reset(isolate_, re);
    i_thread.SetTestBody(test_body_fn);
    CHECK(i_thread.Start());
    TestBody();
    i_thread.Join();
  }

  static void InvokeMajorGC(Isolate* isolate, void* data) {
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    i::heap::InvokeAtomicMajorGC(i_isolate->heap());
  }

  static void MakeSubjectOneByteExternal(Isolate* isolate, void* data) {
    auto instance = reinterpret_cast<InterruptTest*>(data);
    HandleScope scope(isolate);
    Local<String> string =
        Local<String>::New(isolate, instance->subject_string_handle_);
    CHECK(string->CanMakeExternal(String::Encoding::ONE_BYTE_ENCODING));
    string->MakeExternal(isolate, &one_byte_string_resource);
  }

  static void MakeSubjectTwoByteExternal(Isolate* isolate, void* data) {
    auto instance = reinterpret_cast<InterruptTest*>(data);
    HandleScope scope(isolate);
    Local<String> string =
        Local<String>::New(isolate, instance->subject_string_handle_);
    CHECK(string->CanMakeExternal(String::Encoding::TWO_BYTE_ENCODING));
    string->MakeExternal(isolate, &two_byte_string_resource);
  }

  static void TwoByteSubjectToOneByte(Isolate* isolate, void* data) {
    auto instance = reinterpret_cast<InterruptTest*>(data);
    HandleScope scope(isolate);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    Local<RegExp> re = instance->regexp_handle_.Get(isolate);
    i::DirectHandle<i::JSRegExp> regexp = Utils::OpenDirectHandle(*re);
    // We executed on a two-byte subject so far, so we expect only bytecode for
    // two-byte to be present.
    i::Tagged<i::IrRegExpData> re_data =
        Cast<i::IrRegExpData>(regexp->data(i_isolate));
    CHECK(!re_data->has_latin1_bytecode());
    CHECK(re_data->has_uc16_bytecode());

    // Transition the subject string to one-byte by internalizing it.
    // It already contains only one-byte characters.
    Local<String> string = instance->GetSubjectString();
    CHECK(!string->IsOneByte());
    CHECK(string->ContainsOnlyOneByte());
    // Internalize the subject by using it as a computed property name in an
    // object.
    CompileRun("o = { [subject_string]: 'foo' }");
    CHECK(string->IsOneByte());
  }

  static void IterateStack(Isolate* isolate, void* data) {
    HandleScope scope(isolate);

    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    v8::RegisterState state;
#if defined(USE_SIMULATOR)
    SimulatorHelper simulator_helper;
    if (!simulator_helper.Init(isolate)) return;
    simulator_helper.FillRegisters(&state);
#else
    state.pc = nullptr;
    state.fp = &state;
    state.sp = &state;
#endif

    i::StackFrameIteratorForProfilerForTesting it(
        i_isolate, reinterpret_cast<i::Address>(state.pc),
        reinterpret_cast<i::Address>(state.fp),
        reinterpret_cast<i::Address>(state.sp),
        reinterpret_cast<i::Address>(state.lr), i_isolate->js_entry_sp());

    for (; !it.done(); it.Advance()) {
      // Ideally we'd access the frame a bit (doesn't matter how); but this
      // iterator is very limited in what it may access, and prints run into
      // DCHECKs. So we can't do this:
      // it.frame()->Print(&accumulator, i::StackFrame::OVERVIEW,
      //                   frame_index++);
    }
  }

  void SetOneByteSubjectString() {
    HandleScope handle_scope(isolate_);
    i::Isolate* i_isolate = this->i_isolate();
    // The string must be in old space to support externalization.
    i::DirectHandle<i::String> i_one_byte_string =
        i_isolate->factory()->NewStringFromAsciiChecked(
            &kOneByteSubjectString[0], i::AllocationType::kOld);
    SetSubjectString(Utils::ToLocal(i_one_byte_string));
  }

  void SetTwoByteSubjectString() {
    HandleScope handle_scope(isolate_);
    i::Isolate* i_isolate = this->i_isolate();
    // The string must be in old space to support externalization.
    i::DirectHandle<i::String> i_two_byte_string =
        i_isolate->factory()
            ->NewStringFromTwoByte(
                base::Vector<const base::uc16>(&kTwoByteSubjectString[0],
                                               kSubjectStringLength),
                i::AllocationType::kOld)
            .ToHandleChecked();
    SetSubjectString(Utils::ToLocal(i_two_byte_string));
  }

  void SetSubjectString(Local<String> subject) {
    env_->Global()
        ->Set(env_.local(), v8_str("subject_string"), subject)
        .FromJust();
    subject_string_handle_.Reset(env_->GetIsolate(), subject);
  }

  Local<String> GetSubjectString() const {
    return subject_string_handle_.Get(isolate_);
  }

  Local<RegExp> GetRegExp() const { return regexp_handle_.Get(isolate_); }

  i::Isolate* i_isolate() const {
    return reinterpret_cast<i::Isolate*>(isolate_);
  }

 private:
  static void SignalSemaphore(Isolate* isolate, void* data) {
    reinterpret_cast<InterruptTest*>(data)->sem_.Signal();
  }

  void TestBody() {
    CHECK(!ran_test_body_.load());
    CHECK(!ran_to_completion_.load());

    DCHECK(!subject_string_handle_.IsEmpty());

    TryCatch try_catch(env_->GetIsolate());

    isolate_->RequestInterrupt(&SignalSemaphore, this);
    MaybeLocal<Object> result = regexp_handle_.Get(isolate_)->Exec(
        env_.local(), subject_string_handle_.Get(isolate_));
    CHECK(result.IsEmpty());

    CHECK(try_catch.HasTerminated());
    CHECK(ran_test_body_.load());
    CHECK(ran_to_completion_.load());
  }

  class InterruptThread : public base::Thread {
   public:
    explicit InterruptThread(InterruptTest* test)
        : Thread(Options("InterruptTest")), test_(test) {}

    void Run() override {
      CHECK_NOT_NULL(test_body_fn_);

      // Wait for JS execution to start.
      test_->sem_.Wait();

      // Sleep for a bit to allow irregexp execution to start up, then run the
      // test body.
      base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));
      test_->isolate_->RequestInterrupt(&RunTestBody, test_);
      test_->isolate_->RequestInterrupt(&SignalSemaphore, test_);

      // Wait for the scheduled interrupt to signal.
      test_->sem_.Wait();

      // Sleep again to resume irregexp execution, then terminate.
      base::OS::Sleep(base::TimeDelta::FromMilliseconds(50));
      test_->ran_to_completion_.store(true);
      test_->isolate_->TerminateExecution();
    }

    static void RunTestBody(Isolate* isolate, void* data) {
      auto instance = reinterpret_cast<InterruptTest*>(data);
      instance->i_thread.test_body_fn_(isolate, data);
      instance->ran_test_body_.store(true);
    }

    void SetTestBody(InterruptCallback callback) { test_body_fn_ = callback; }

   private:
    InterruptCallback test_body_fn_;
    InterruptTest* test_;
  };

  InterruptThread i_thread;

  LocalContext env_;
  Isolate* isolate_;
  base::Semaphore sem_;  // Coordinates between main and interrupt threads.

  Persistent<String> subject_string_handle_;
  Persistent<RegExp> regexp_handle_;

  std::atomic<bool> ran_test_body_;
  std::atomic<bool> ran_to_completion_;
};

void SetCommonV8FlagsForInterruptTests() {
  // Interrupt tests rely on quirks of the backtracking engine to trigger
  // pattern execution long enough s.t. we can reliably trigger an interrupt
  // while the regexp code is still executing.
  i::v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks = false;
}

}  // namespace

TEST(InterruptAndInvokeMajorGC) {
  // Move all movable objects on GC.
  i::v8_flags.compact_on_every_full_gc = true;
  SetCommonV8FlagsForInterruptTests();
  InterruptTest test{};
  test.SetOneByteSubjectString();
  test.RunTest(InterruptTest::InvokeMajorGC);
}

TEST(InterruptAndMakeSubjectOneByteExternal) {
  SetCommonV8FlagsForInterruptTests();
  InterruptTest test{};
  test.SetOneByteSubjectString();
  test.RunTest(InterruptTest::MakeSubjectOneByteExternal);
}

TEST(InterruptAndMakeSubjectTwoByteExternal) {
  SetCommonV8FlagsForInterruptTests();
  InterruptTest test{};
  test.SetTwoByteSubjectString();
  test.RunTest(InterruptTest::MakeSubjectTwoByteExternal);
}

TEST(InterruptAndIterateStack) {
  i::v8_flags.regexp_tier_up = false;
  SetCommonV8FlagsForInterruptTests();
  InterruptTest test{};
  test.SetOneByteSubjectString();
  test.RunTest(InterruptTest::IterateStack);
}

TEST(InterruptAndTransitionSubjectFromTwoByteToOneByte) {
  SetCommonV8FlagsForInterruptTests();
  InterruptTest test{};
  i::Isolate* i_isolate = test.i_isolate();
  i::HandleScope handle_scope(i_isolate);
  // Internalize a one-byte copy of the two-byte string we are going to
  // internalize during the interrupt. This ensures that the two-byte string
  // transitions to a ThinString pointing to a one-byte string.
  Local<String> internalized_string =
      String::NewFromUtf8(
          reinterpret_cast<Isolate*>(i_isolate), &kOneByteSubjectString[1],
          v8::NewStringType::kInternalized, kSubjectStringLength - 1)
          .ToLocalChecked();
  CHECK(internalized_string->IsOneByte());

  test.SetTwoByteSubjectString();
  Local<String> string = test.GetSubjectString();
  CHECK(!string->IsOneByte());
  // Set the subject string as a substring of the original subject (containing
  // only one-byte characters).
  v8::Local<Value> value =
      CompileRun("subject_string = subject_string.substring(1)");
  test.SetSubjectString(value.As<String>());
  CHECK(test.GetSubjectString()->ContainsOnlyOneByte());

  test.RunTest(InterruptTest::TwoByteSubjectToOneByte);
  // After the test, we expect that bytecode for a one-byte subject has been
  // installed during the interrupt.
  i::DirectHandle<i::JSRegExp> regexp =
      Utils::OpenDirectHandle(*test.GetRegExp());
  i::Tagged<i::IrRegExpData> data =
      Cast<i::IrRegExpData>(regexp->data(i_isolate));
  CHECK(data->has_latin1_bytecode());
}
