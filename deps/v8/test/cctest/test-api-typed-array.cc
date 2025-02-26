// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-buffer.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/test-api.h"

using ::v8::Array;
using ::v8::Context;
using ::v8::Local;
using ::v8::Value;

namespace {

void CheckElementValue(i::Isolate* isolate, int expected,
                       i::DirectHandle<i::JSAny> obj, int offset) {
  i::Tagged<i::Object> element =
      *i::Object::GetElement(isolate, obj, offset).ToHandleChecked();
  CHECK_EQ(expected, i::Smi::ToInt(element));
}

template <class ElementType>
void ObjectWithExternalArrayTestHelper(Local<Context> context,
                                       v8::Local<v8::TypedArray> obj,
                                       int element_count,
                                       i::ExternalArrayType array_type,
                                       int64_t low, int64_t high) {
  i::DirectHandle<i::JSTypedArray> jsobj = v8::Utils::OpenDirectHandle(*obj);
  v8::Isolate* v8_isolate = context->GetIsolate();
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  obj->Set(context, v8_str("field"), v8::Int32::New(v8_isolate, 1503))
      .FromJust();
  CHECK(context->Global()->Set(context, v8_str("ext_array"), obj).FromJust());
  v8::Local<v8::Value> result = CompileRun("ext_array.field");
  CHECK_EQ(1503, result->Int32Value(context).FromJust());
  result = CompileRun("ext_array[1]");
  CHECK_EQ(1, result->Int32Value(context).FromJust());

  // Check assigned smis
  result = CompileRun(
      "for (var i = 0; i < 8; i++) {"
      "  ext_array[i] = i;"
      "}"
      "var sum = 0;"
      "for (var i = 0; i < 8; i++) {"
      "  sum += ext_array[i];"
      "}"
      "sum;");

  CHECK_EQ(28, result->Int32Value(context).FromJust());
  // Check pass through of assigned smis
  result = CompileRun(
      "var sum = 0;"
      "for (var i = 0; i < 8; i++) {"
      "  sum += ext_array[i] = ext_array[i] = -i;"
      "}"
      "sum;");
  CHECK_EQ(-28, result->Int32Value(context).FromJust());

  // Check assigned smis in reverse order
  result = CompileRun(
      "for (var i = 8; --i >= 0; ) {"
      "  ext_array[i] = i;"
      "}"
      "var sum = 0;"
      "for (var i = 0; i < 8; i++) {"
      "  sum += ext_array[i];"
      "}"
      "sum;");
  CHECK_EQ(28, result->Int32Value(context).FromJust());

  // Check pass through of assigned HeapNumbers
  result = CompileRun(
      "var sum = 0;"
      "for (var i = 0; i < 16; i+=2) {"
      "  sum += ext_array[i] = ext_array[i] = (-i * 0.5);"
      "}"
      "sum;");
  CHECK_EQ(-28, result->Int32Value(context).FromJust());

  // Check assigned HeapNumbers
  result = CompileRun(
      "for (var i = 0; i < 16; i+=2) {"
      "  ext_array[i] = (i * 0.5);"
      "}"
      "var sum = 0;"
      "for (var i = 0; i < 16; i+=2) {"
      "  sum += ext_array[i];"
      "}"
      "sum;");
  CHECK_EQ(28, result->Int32Value(context).FromJust());

  // Check assigned HeapNumbers in reverse order
  result = CompileRun(
      "for (var i = 14; i >= 0; i-=2) {"
      "  ext_array[i] = (i * 0.5);"
      "}"
      "var sum = 0;"
      "for (var i = 0; i < 16; i+=2) {"
      "  sum += ext_array[i];"
      "}"
      "sum;");
  CHECK_EQ(28, result->Int32Value(context).FromJust());

  v8::base::ScopedVector<char> test_buf(1024);

  // Check legal boundary conditions.
  // The repeated loads and stores ensure the ICs are exercised.
  const char* boundary_program =
      "var res = 0;"
      "for (var i = 0; i < 16; i++) {"
      "  ext_array[i] = %lld;"
      "  if (i > 8) {"
      "    res = ext_array[i];"
      "  }"
      "}"
      "res;";
  v8::base::SNPrintF(test_buf, boundary_program, low);
  result = CompileRun(test_buf.begin());
  CHECK_EQ(low, result->IntegerValue(context).FromJust());

  v8::base::SNPrintF(test_buf, boundary_program, high);
  result = CompileRun(test_buf.begin());
  CHECK_EQ(high, result->IntegerValue(context).FromJust());

  // Check misprediction of type in IC.
  result = CompileRun(
      "var tmp_array = ext_array;"
      "var sum = 0;"
      "for (var i = 0; i < 8; i++) {"
      "  tmp_array[i] = i;"
      "  sum += tmp_array[i];"
      "  if (i == 4) {"
      "    tmp_array = {};"
      "  }"
      "}"
      "sum;");
  // Force GC to trigger verification.
  i::heap::InvokeMajorGC(CcTest::heap());
  CHECK_EQ(28, result->Int32Value(context).FromJust());

  // Make sure out-of-range loads do not throw.
  v8::base::SNPrintF(test_buf,
                     "var caught_exception = false;"
                     "try {"
                     "  ext_array[%d];"
                     "} catch (e) {"
                     "  caught_exception = true;"
                     "}"
                     "caught_exception;",
                     element_count);
  result = CompileRun(test_buf.begin());
  CHECK(!result->BooleanValue(v8_isolate));

  // Make sure out-of-range stores do not throw.
  v8::base::SNPrintF(test_buf,
                     "var caught_exception = false;"
                     "try {"
                     "  ext_array[%d] = 1;"
                     "} catch (e) {"
                     "  caught_exception = true;"
                     "}"
                     "caught_exception;",
                     element_count);
  result = CompileRun(test_buf.begin());
  CHECK(!result->BooleanValue(v8_isolate));

  // Check other boundary conditions, values and operations.
  result = CompileRun(
      "for (var i = 0; i < 8; i++) {"
      "  ext_array[7] = undefined;"
      "}"
      "ext_array[7];");
  CHECK_EQ(0, result->Int32Value(context).FromJust());
  if (array_type == i::kExternalFloat64Array ||
      array_type == i::kExternalFloat32Array) {
    CHECK(std::isnan(i::Object::NumberValue(Cast<i::Number>(
        *i::Object::GetElement(isolate, jsobj, 7).ToHandleChecked()))));
  } else {
    CheckElementValue(isolate, 0, jsobj, 7);
  }

  result = CompileRun(
      "for (var i = 0; i < 8; i++) {"
      "  ext_array[6] = '2.3';"
      "}"
      "ext_array[6];");
  CHECK_EQ(2, result->Int32Value(context).FromJust());
  CHECK_EQ(2,
           static_cast<int>(i::Object::NumberValue(Cast<i::Number>(
               *i::Object::GetElement(isolate, jsobj, 6).ToHandleChecked()))));

  if (array_type != i::kExternalFloat32Array &&
      array_type != i::kExternalFloat64Array) {
    // Though the specification doesn't state it, be explicit about
    // converting NaNs and +/-Infinity to zero.
    result = CompileRun(
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = 5;"
        "}"
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = NaN;"
        "}"
        "ext_array[5];");
    CHECK_EQ(0, result->Int32Value(context).FromJust());
    CheckElementValue(isolate, 0, jsobj, 5);

    result = CompileRun(
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = 5;"
        "}"
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = Infinity;"
        "}"
        "ext_array[5];");
    int expected_value =
        (array_type == i::kExternalUint8ClampedArray) ? 255 : 0;
    CHECK_EQ(expected_value, result->Int32Value(context).FromJust());
    CheckElementValue(isolate, expected_value, jsobj, 5);

    result = CompileRun(
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = 5;"
        "}"
        "for (var i = 0; i < 8; i++) {"
        "  ext_array[i] = -Infinity;"
        "}"
        "ext_array[5];");
    CHECK_EQ(0, result->Int32Value(context).FromJust());
    CheckElementValue(isolate, 0, jsobj, 5);

    // Check truncation behavior of integral arrays.
    const char* unsigned_data =
        "var source_data = [0.6, 10.6];"
        "var expected_results = [0, 10];";
    const char* signed_data =
        "var source_data = [0.6, 10.6, -0.6, -10.6];"
        "var expected_results = [0, 10, 0, -10];";
    const char* pixel_data =
        "var source_data = [0.6, 10.6];"
        "var expected_results = [1, 11];";
    bool is_unsigned = (array_type == i::kExternalUint8Array ||
                        array_type == i::kExternalUint16Array ||
                        array_type == i::kExternalUint32Array);
    bool is_pixel_data = array_type == i::kExternalUint8ClampedArray;

    v8::base::SNPrintF(
        test_buf,
        "%s"
        "var all_passed = true;"
        "for (var i = 0; i < source_data.length; i++) {"
        "  for (var j = 0; j < 8; j++) {"
        "    ext_array[j] = source_data[i];"
        "  }"
        "  all_passed = all_passed &&"
        "               (ext_array[5] == expected_results[i]);"
        "}"
        "all_passed;",
        (is_unsigned ? unsigned_data
                     : (is_pixel_data ? pixel_data : signed_data)));
    result = CompileRun(test_buf.begin());
    CHECK(result->BooleanValue(v8_isolate));
  }

  {
    ElementType* data_ptr = static_cast<ElementType*>(jsobj->DataPtr());
    for (int i = 0; i < element_count; i++) {
      data_ptr[i] = static_cast<ElementType>(i);
    }
  }

  bool old_natives_flag_sentry = i::v8_flags.allow_natives_syntax;
  i::v8_flags.allow_natives_syntax = true;

  // Test complex assignments
  result = CompileRun(
      "function ee_op_test_complex_func(sum) {"
      " for (var i = 0; i < 40; ++i) {"
      "   sum += (ext_array[i] += 1);"
      "   sum += (ext_array[i] -= 1);"
      " } "
      " return sum;"
      "};"
      "%PrepareFunctionForOptimization(ee_op_test_complex_func);"
      "sum=0;"
      "sum=ee_op_test_complex_func(sum);"
      "sum=ee_op_test_complex_func(sum);"
      "%OptimizeFunctionOnNextCall(ee_op_test_complex_func);"
      "sum=ee_op_test_complex_func(sum);"
      "sum;");
  CHECK_EQ(4800, result->Int32Value(context).FromJust());

  // Test count operations
  result = CompileRun(
      "function ee_op_test_count_func(sum) {"
      " for (var i = 0; i < 40; ++i) {"
      "   sum += (++ext_array[i]);"
      "   sum += (--ext_array[i]);"
      " } "
      " return sum;"
      "};"
      "%PrepareFunctionForOptimization(ee_op_test_count_func);"
      "sum=0;"
      "sum=ee_op_test_count_func(sum);"
      "sum=ee_op_test_count_func(sum);"
      "%OptimizeFunctionOnNextCall(ee_op_test_count_func);"
      "sum=ee_op_test_count_func(sum);"
      "sum;");
  CHECK_EQ(4800, result->Int32Value(context).FromJust());

  i::v8_flags.allow_natives_syntax = old_natives_flag_sentry;

  result = CompileRun(
      "ext_array[3] = 33;"
      "delete ext_array[3];"
      "ext_array[3];");
  CHECK_EQ(33, result->Int32Value(context).FromJust());

  result = CompileRun(
      "ext_array[0] = 10; ext_array[1] = 11;"
      "ext_array[2] = 12; ext_array[3] = 13;"
      "try { ext_array.__defineGetter__('2', function() { return 120; }); }"
      "catch (e) { }"
      "ext_array[2];");
  CHECK_EQ(12, result->Int32Value(context).FromJust());

  result = CompileRun(
      "var js_array = new Array(40);"
      "js_array[0] = 77;"
      "js_array;");
  CHECK_EQ(77, v8::Object::Cast(*result)
                   ->Get(context, v8_str("0"))
                   .ToLocalChecked()
                   ->Int32Value(context)
                   .FromJust());

  result = CompileRun(
      "ext_array[1] = 23;"
      "ext_array.__proto__ = [];"
      "js_array.__proto__ = ext_array;"
      "js_array.concat(ext_array);");
  CHECK_EQ(77, v8::Object::Cast(*result)
                   ->Get(context, v8_str("0"))
                   .ToLocalChecked()
                   ->Int32Value(context)
                   .FromJust());
  CHECK_EQ(23, v8::Object::Cast(*result)
                   ->Get(context, v8_str("1"))
                   .ToLocalChecked()
                   ->Int32Value(context)
                   .FromJust());

  result = CompileRun("ext_array[1] = 23;");
  CHECK_EQ(23, result->Int32Value(context).FromJust());
}

template <typename ElementType, typename TypedArray, class ArrayBufferType>
void TypedArrayTestHelper(i::ExternalArrayType array_type, int64_t low,
                          int64_t high) {
  const int kElementCount = 50;

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  Local<ArrayBufferType> ab =
      ArrayBufferType::New(isolate, (kElementCount + 2) * sizeof(ElementType));
  Local<TypedArray> ta =
      TypedArray::New(ab, 2 * sizeof(ElementType), kElementCount);
  CheckInternalFieldsAreZero<v8::ArrayBufferView>(ta);
  CHECK_EQ(kElementCount, static_cast<int>(ta->Length()));
  CHECK_EQ(2 * sizeof(ElementType), ta->ByteOffset());
  CHECK_EQ(kElementCount * sizeof(ElementType), ta->ByteLength());
  CHECK(ab->Equals(env.local(), ta->Buffer()).FromJust());

  ElementType* data =
      reinterpret_cast<ElementType*>(ab->GetBackingStore()->Data()) + 2;
  for (int i = 0; i < kElementCount; i++) {
    data[i] = static_cast<ElementType>(i);
  }

  ObjectWithExternalArrayTestHelper<ElementType>(env.local(), ta, kElementCount,
                                                 array_type, low, high);

  // TODO(v8:11111): Use API functions for testing these, once they're exposed
  // via the API.
  i::DirectHandle<i::JSTypedArray> i_ta = v8::Utils::OpenDirectHandle(*ta);
  CHECK(!i_ta->is_length_tracking());
  CHECK(!i_ta->is_backed_by_rab());
}

}  // namespace

THREADED_TEST(Uint8Array) {
  TypedArrayTestHelper<uint8_t, v8::Uint8Array, v8::ArrayBuffer>(
      i::kExternalUint8Array, 0, 0xFF);
}

THREADED_TEST(Int8Array) {
  TypedArrayTestHelper<int8_t, v8::Int8Array, v8::ArrayBuffer>(
      i::kExternalInt8Array, -0x80, 0x7F);
}

THREADED_TEST(Uint16Array) {
  TypedArrayTestHelper<uint16_t, v8::Uint16Array, v8::ArrayBuffer>(
      i::kExternalUint16Array, 0, 0xFFFF);
}

THREADED_TEST(Int16Array) {
  TypedArrayTestHelper<int16_t, v8::Int16Array, v8::ArrayBuffer>(
      i::kExternalInt16Array, -0x8000, 0x7FFF);
}

THREADED_TEST(Uint32Array) {
  TypedArrayTestHelper<uint32_t, v8::Uint32Array, v8::ArrayBuffer>(
      i::kExternalUint32Array, 0, UINT_MAX);
}

THREADED_TEST(Int32Array) {
  TypedArrayTestHelper<int32_t, v8::Int32Array, v8::ArrayBuffer>(
      i::kExternalInt32Array, INT_MIN, INT_MAX);
}

THREADED_TEST(Float32Array) {
  TypedArrayTestHelper<float, v8::Float32Array, v8::ArrayBuffer>(
      i::kExternalFloat32Array, -500, 500);
}

THREADED_TEST(Float64Array) {
  TypedArrayTestHelper<double, v8::Float64Array, v8::ArrayBuffer>(
      i::kExternalFloat64Array, -500, 500);
}

THREADED_TEST(Uint8ClampedArray) {
  TypedArrayTestHelper<uint8_t, v8::Uint8ClampedArray, v8::ArrayBuffer>(
      i::kExternalUint8ClampedArray, 0, 0xFF);
}

THREADED_TEST(DataView) {
  const int kSize = 50;

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, 2 + kSize);
  Local<v8::DataView> dv = v8::DataView::New(ab, 2, kSize);
  CheckInternalFieldsAreZero<v8::ArrayBufferView>(dv);
  CHECK_EQ(2u, dv->ByteOffset());
  CHECK_EQ(kSize, static_cast<int>(dv->ByteLength()));
  CHECK(ab->Equals(env.local(), dv->Buffer()).FromJust());

  // TODO(v8:11111): Use API functions for testing these, once they're exposed
  // via the API.
  i::DirectHandle<i::JSDataViewOrRabGsabDataView> i_dv =
      v8::Utils::OpenDirectHandle(*dv);
  CHECK(!i_dv->is_length_tracking());
  CHECK(!i_dv->is_backed_by_rab());
}

THREADED_TEST(SharedUint8Array) {
  TypedArrayTestHelper<uint8_t, v8::Uint8Array, v8::SharedArrayBuffer>(
      i::kExternalUint8Array, 0, 0xFF);
}

THREADED_TEST(SharedInt8Array) {
  TypedArrayTestHelper<int8_t, v8::Int8Array, v8::SharedArrayBuffer>(
      i::kExternalInt8Array, -0x80, 0x7F);
}

THREADED_TEST(SharedUint16Array) {
  TypedArrayTestHelper<uint16_t, v8::Uint16Array, v8::SharedArrayBuffer>(
      i::kExternalUint16Array, 0, 0xFFFF);
}

THREADED_TEST(SharedInt16Array) {
  TypedArrayTestHelper<int16_t, v8::Int16Array, v8::SharedArrayBuffer>(
      i::kExternalInt16Array, -0x8000, 0x7FFF);
}

THREADED_TEST(SharedUint32Array) {
  TypedArrayTestHelper<uint32_t, v8::Uint32Array, v8::SharedArrayBuffer>(
      i::kExternalUint32Array, 0, UINT_MAX);
}

THREADED_TEST(SharedInt32Array) {
  TypedArrayTestHelper<int32_t, v8::Int32Array, v8::SharedArrayBuffer>(
      i::kExternalInt32Array, INT_MIN, INT_MAX);
}

THREADED_TEST(SharedFloat32Array) {
  TypedArrayTestHelper<float, v8::Float32Array, v8::SharedArrayBuffer>(
      i::kExternalFloat32Array, -500, 500);
}

THREADED_TEST(SharedFloat64Array) {
  TypedArrayTestHelper<double, v8::Float64Array, v8::SharedArrayBuffer>(
      i::kExternalFloat64Array, -500, 500);
}

THREADED_TEST(SharedUint8ClampedArray) {
  TypedArrayTestHelper<uint8_t, v8::Uint8ClampedArray, v8::SharedArrayBuffer>(
      i::kExternalUint8ClampedArray, 0, 0xFF);
}

THREADED_TEST(SharedDataView) {
  const int kSize = 50;

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  Local<v8::SharedArrayBuffer> ab =
      v8::SharedArrayBuffer::New(isolate, 2 + kSize);
  Local<v8::DataView> dv = v8::DataView::New(ab, 2, kSize);
  CheckInternalFieldsAreZero<v8::ArrayBufferView>(dv);
  CHECK_EQ(2u, dv->ByteOffset());
  CHECK_EQ(kSize, static_cast<int>(dv->ByteLength()));
  CHECK(ab->Equals(env.local(), dv->Buffer()).FromJust());

  // TODO(v8:11111): Use API functions for testing these, once they're exposed
  // via the API.
  i::DirectHandle<i::JSDataViewOrRabGsabDataView> i_dv =
      v8::Utils::OpenDirectHandle(*dv);
  CHECK(!i_dv->is_length_tracking());
  CHECK(!i_dv->is_backed_by_rab());
}

#define IS_ARRAY_BUFFER_VIEW_TEST(View)                                     \
  THREADED_TEST(Is##View) {                                                 \
    LocalContext env;                                                       \
    v8::Isolate* isolate = env->GetIsolate();                               \
    v8::HandleScope handle_scope(isolate);                                  \
                                                                            \
    Local<Value> result = CompileRun(                                       \
        "var ab = new ArrayBuffer(128);"                                    \
        "new " #View "(ab)");                                               \
    CHECK(result->IsArrayBufferView());                                     \
    CHECK(result->Is##View());                                              \
    CheckInternalFieldsAreZero<v8::ArrayBufferView>(result.As<v8::View>()); \
  }

IS_ARRAY_BUFFER_VIEW_TEST(Uint8Array)
IS_ARRAY_BUFFER_VIEW_TEST(Int8Array)
IS_ARRAY_BUFFER_VIEW_TEST(Uint16Array)
IS_ARRAY_BUFFER_VIEW_TEST(Int16Array)
IS_ARRAY_BUFFER_VIEW_TEST(Uint32Array)
IS_ARRAY_BUFFER_VIEW_TEST(Int32Array)
IS_ARRAY_BUFFER_VIEW_TEST(Float32Array)
IS_ARRAY_BUFFER_VIEW_TEST(Float64Array)
IS_ARRAY_BUFFER_VIEW_TEST(Uint8ClampedArray)
IS_ARRAY_BUFFER_VIEW_TEST(DataView)

#undef IS_ARRAY_BUFFER_VIEW_TEST

TEST(InternalFieldsOnTypedArray) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = env.local();
  Context::Scope context_scope(context);
  v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, 1);
  v8::Local<v8::Uint8Array> array = v8::Uint8Array::New(buffer, 0, 1);
  for (int i = 0; i < v8::ArrayBufferView::kInternalFieldCount; i++) {
    CHECK_EQ(static_cast<void*>(nullptr),
             array->GetAlignedPointerFromInternalField(i));
  }
}

TEST(InternalFieldsOnDataView) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = env.local();
  Context::Scope context_scope(context);
  v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, 1);
  v8::Local<v8::DataView> array = v8::DataView::New(buffer, 0, 1);
  for (int i = 0; i < v8::ArrayBufferView::kInternalFieldCount; i++) {
    CHECK_EQ(static_cast<void*>(nullptr),
             array->GetAlignedPointerFromInternalField(i));
  }
}

namespace {
void TestOnHeapHasBuffer(const char* array_name, size_t elem_size) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  v8::base::ScopedVector<char> source(128);
  // Test on-heap sizes.
  for (size_t size = 0; size <= i::JSTypedArray::kMaxSizeInHeap;
       size += elem_size) {
    size_t length = size / elem_size;
    v8::base::SNPrintF(source, "new %sArray(%zu)", array_name, length);
    auto typed_array =
        v8::Local<v8::TypedArray>::Cast(CompileRun(source.begin()));

    CHECK_EQ(length, typed_array->Length());

    // Should not (yet) have a buffer.
    CHECK(!typed_array->HasBuffer());

    // Get the buffer and check its length.
    i::DirectHandle<i::JSTypedArray> i_typed_array =
        v8::Utils::OpenDirectHandle(*typed_array);
    auto i_array_buffer1 = i_typed_array->GetBuffer();
    CHECK_EQ(size, i_array_buffer1->byte_length());
    CHECK(typed_array->HasBuffer());

    // Should have the same buffer each time.
    auto i_array_buffer2 = i_typed_array->GetBuffer();
    CHECK(i_array_buffer1.is_identical_to(i_array_buffer2));
  }
}

void TestOffHeapHasBuffer(const char* array_name, size_t elem_size) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);

  v8::base::ScopedVector<char> source(128);
  // Test off-heap sizes.
  size_t size = i::JSTypedArray::kMaxSizeInHeap;
  for (int i = 0; i < 3; i++) {
    size_t length = 1 + (size / elem_size);
    v8::base::SNPrintF(source, "new %sArray(%zu)", array_name, length);
    auto typed_array =
        v8::Local<v8::TypedArray>::Cast(CompileRun(source.begin()));
    CHECK_EQ(length, typed_array->Length());

    // Should already have a buffer.
    CHECK(typed_array->HasBuffer());

    // Get the buffer and check its length.
    i::DirectHandle<i::JSTypedArray> i_typed_array =
        v8::Utils::OpenDirectHandle(*typed_array);
    auto i_array_buffer1 = i_typed_array->GetBuffer();
    CHECK_EQ(length * elem_size, i_array_buffer1->byte_length());

    size *= 2;
  }
}

}  // namespace

#define TEST_HAS_BUFFER(array_name, elem_size)    \
  TEST(OnHeap_##array_name##Array_HasBuffer) {    \
    TestOnHeapHasBuffer(#array_name, elem_size);  \
  }                                               \
  TEST(OffHeap_##array_name##_HasBuffer) {        \
    TestOffHeapHasBuffer(#array_name, elem_size); \
  }

TEST_HAS_BUFFER(Uint8, 1)
TEST_HAS_BUFFER(Int8, 1)
TEST_HAS_BUFFER(Uint16, 2)
TEST_HAS_BUFFER(Int16, 2)
TEST_HAS_BUFFER(Uint32, 4)
TEST_HAS_BUFFER(Int32, 4)
TEST_HAS_BUFFER(Float32, 4)
TEST_HAS_BUFFER(Float64, 8)

#undef TEST_HAS_BUFFER
