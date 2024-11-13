// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests check that Torque-generated verifier functions crash the process
// when encountering data that doesn't fit the Torque type definitions.

#include "src/api/api-inl.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/map-inl.h"
#include "test/cctest/cctest.h"
#include "torque-generated/class-verifiers.h"

namespace v8 {
namespace internal {

// Defines a pair of tests with similar code. The goal is to test that a
// specific action causes a failure, but that everything else in the test case
// succeeds. The general pattern should be:
//
// TEST_PAIR(Something) {
//   do_setup_steps_that_always_succeed();
//   if (should_fail) {
//     do_the_step_that_fails();
//   }
//   do_teardown_steps_that_always_succeed();
// }
//
// A corresponding entry in cctest.status specifies that all Fail* tests in this
// file must fail.
#define TEST_PAIR(Name)               \
  static void Name(bool should_fail); \
  TEST(Pass##Name) { Name(false); }   \
  TEST(Fail##Name) { Name(true); }    \
  static void Name(bool should_fail)

#ifdef VERIFY_HEAP

TEST_PAIR(TestWrongTypeInNormalField) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> v = CompileRun("({a: 3, b: 4})");
  DirectHandle<JSObject> o = Cast<JSObject>(v8::Utils::OpenDirectHandle(*v));
  DirectHandle<Object> original_elements(
      TaggedField<Object>::load(*o, JSObject::kElementsOffset), i_isolate);
  CHECK(IsFixedArrayBase(*original_elements));

  // There must be no GC (and therefore no verifiers running) until we can
  // restore the modified data.
  DisallowGarbageCollection no_gc;

  // Elements must be FixedArrayBase according to the Torque definition, so a
  // JSObject should cause a failure.
  TaggedField<Object>::store(*o, JSObject::kElementsOffset, *o);
  if (should_fail) {
    TorqueGeneratedClassVerifiers::JSObjectVerify(*o, i_isolate);
  }

  // Put back the original value in case verifiers run on test shutdown.
  TaggedField<Object>::store(*o, JSObject::kElementsOffset, *original_elements);
}

TEST_PAIR(TestWrongStrongTypeInIndexedStructField) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> v = CompileRun("({a: 3, b: 4})");
  DirectHandle<Object> o = v8::Utils::OpenDirectHandle(*v);
  DirectHandle<Map> map(Cast<HeapObject>(o)->map(), i_isolate);
  DirectHandle<DescriptorArray> descriptors(
      map->instance_descriptors(i_isolate), i_isolate);
  int offset = DescriptorArray::OffsetOfDescriptorAt(1) +
               DescriptorArray::kEntryKeyOffset;
  DirectHandle<Object> original_key(
      TaggedField<Object>::load(*descriptors, offset), i_isolate);
  CHECK(IsString(*original_key));

  // There must be no GC (and therefore no verifiers running) until we can
  // restore the modified data.
  DisallowGarbageCollection no_gc;

  // Key must be Name|Undefined according to the Torque definition, so a
  // JSObject should cause a failure.
  TaggedField<Object>::store(*descriptors, offset, *o);
  if (should_fail) {
    TorqueGeneratedClassVerifiers::DescriptorArrayVerify(*descriptors,
                                                         i_isolate);
  }

  // Put back the original value in case verifiers run on test shutdown.
  TaggedField<Object>::store(*descriptors, offset, *original_key);
}

TEST_PAIR(TestWrongWeakTypeInIndexedStructField) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> v = CompileRun("({a: 3, b: 4})");
  DirectHandle<Object> o = v8::Utils::OpenDirectHandle(*v);
  DirectHandle<Map> map(Cast<HeapObject>(o)->map(), i_isolate);
  DirectHandle<DescriptorArray> descriptors(
      map->instance_descriptors(i_isolate), i_isolate);
  int offset = DescriptorArray::OffsetOfDescriptorAt(0) +
               DescriptorArray::kEntryValueOffset;
  DirectHandle<Object> original_value(
      TaggedField<Object>::load(*descriptors, offset), i_isolate);

  // There must be no GC (and therefore no verifiers running) until we can
  // restore the modified data.
  DisallowGarbageCollection no_gc;

  // Value can be JSAny, which includes JSObject, and it can be Weak<Map>, but
  // it can't be Weak<JSObject>.
  TaggedField<Object>::store(*descriptors, offset, *o);
  TorqueGeneratedClassVerifiers::DescriptorArrayVerify(*descriptors, i_isolate);
  Tagged<MaybeObject> weak = MakeWeak(*o);
  TaggedField<MaybeObject>::store(*descriptors, offset, weak);
  if (should_fail) {
    TorqueGeneratedClassVerifiers::DescriptorArrayVerify(*descriptors,
                                                         i_isolate);
  }

  // Put back the original value in case verifiers run on test shutdown.
  TaggedField<Object>::store(*descriptors, offset, *original_value);
}

TEST_PAIR(TestWrongOddball) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> v = CompileRun("new Date()");
  DirectHandle<JSDate> date = Cast<JSDate>(v8::Utils::OpenDirectHandle(*v));
  DirectHandle<Object> original_hour(
      TaggedField<Object>::load(*date, JSDate::kHourOffset), i_isolate);

  // There must be no GC (and therefore no verifiers running) until we can
  // restore the modified data.
  DisallowGarbageCollection no_gc;

  // Hour is Undefined|Smi|NaN. Other oddballs like null should cause a failure.
  TaggedField<Object>::store(*date, JSDate::kHourOffset,
                             *i_isolate->factory()->null_value());
  if (should_fail) {
    TorqueGeneratedClassVerifiers::JSDateVerify(*date, i_isolate);
  }

  // Put back the original value in case verifiers run on test shutdown.
  TaggedField<Object>::store(*date, JSDate::kHourOffset, *original_hour);
}

TEST_PAIR(TestWrongNumber) {
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);
  v8::Local<v8::Value> v = CompileRun("new Date()");
  DirectHandle<JSDate> date = Cast<JSDate>(v8::Utils::OpenDirectHandle(*v));
  DirectHandle<Object> original_hour(
      TaggedField<Object>::load(*date, JSDate::kHourOffset), i_isolate);
  v8::Local<v8::Value> v2 = CompileRun("1.1");
  DirectHandle<Object> float_val = v8::Utils::OpenDirectHandle(*v2);

  // There must be no GC (and therefore no verifiers running) until we can
  // restore the modified data.
  DisallowGarbageCollection no_gc;

  // Hour is Undefined|Smi|NaN. Other doubles like 1.1 should cause a failure.
  TaggedField<Object>::store(*date, JSDate::kHourOffset, *float_val);
  if (should_fail) {
    TorqueGeneratedClassVerifiers::JSDateVerify(*date, i_isolate);
  }

  // Put back the original value in case verifiers run on test shutdown.
  TaggedField<Object>::store(*date, JSDate::kHourOffset, *original_hour);
}

#endif  // VERIFY_HEAP

#undef TEST_PAIR

}  // namespace internal
}  // namespace v8
