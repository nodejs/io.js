// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/d8/d8.h"

#include "include/v8-fast-api-calls.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"

// This file exposes a d8.test.fast_c_api object, which adds testing facility
// for writing mjsunit tests that exercise fast API calls.
// The fast_c_api object also supports querying the number of fast/slow calls
// and resetting these counters.

namespace v8 {
namespace {

#define CHECK_SELF_OR_THROW_FAST_OPTIONS(return_value)                      \
  if (!self) {                                                              \
    HandleScope handle_scope(options.isolate);                              \
    options.isolate->ThrowError(                                            \
        "This method is not defined on objects inheriting from FastCAPI."); \
    return return_value;                                                    \
  }

#define CHECK_SELF_OR_THROW_FAST(return_value)                              \
  if (!self) {                                                              \
    receiver->GetIsolate()->ThrowError(                                     \
        "This method is not defined on objects inheriting from FastCAPI."); \
    return return_value;                                                    \
  }

#define CHECK_SELF_OR_THROW_SLOW()                                          \
  if (!self) {                                                              \
    info.GetIsolate()->ThrowError(                                          \
        "This method is not defined on objects inheriting from FastCAPI."); \
    return;                                                                 \
  }

class FastCApiObject {
 public:
  static FastCApiObject& instance();

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType ThrowNoFallbackFastCallbackPatch(AnyCType receiver) {
    AnyCType ret;
    ThrowNoFallbackFastCallback(receiver.object_value);
    return ret;
  }

#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static int ThrowNoFallbackFastCallback(Local<Object> receiver) {
    FastCApiObject* self = UnwrapObject(receiver);
    if (!self) {
      self = &FastCApiObject::instance();
    }
    self->fast_call_count_++;
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Context::Scope context_scope(context);
    isolate->ThrowError("Exception from fast callback");
    return 0;
  }

  static void ThrowFallbackSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    info.GetIsolate()->ThrowError("Exception from slow callback");
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType CopyStringFastCallbackPatch(AnyCType receiver,
                                              AnyCType source, AnyCType out,
                                              AnyCType options) {
    AnyCType ret;
    CopyStringFastCallback(receiver.object_value, *source.string_value,
                           out.object_value, *options.options_value);
    return ret;
  }

#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static void CopyStringFastCallback(Local<Object> receiver,
                                     const FastOneByteString& source,
                                     Local<Object> out,
                                     FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    self->fast_call_count_++;

    HandleScope handle_scope(options.isolate);
    if (!out->IsUint8Array()) {
      options.isolate->ThrowError(
          "Invalid parameter, the second parameter has to be a a Uint8Array.");
      return;
    }
    Local<Uint8Array> array = out.As<Uint8Array>();
    if (array->Length() < source.length) {
      options.isolate->ThrowError(
          "Invalid parameter, destination array is too small.");
      return;
    }
    uint8_t* memory =
        reinterpret_cast<uint8_t*>(out.As<Uint8Array>()->Buffer()->Data());
    memcpy(memory, source.data, source.length);
  }

  static void CopyStringSlowCallback(const FunctionCallbackInfo<Value>& info) {
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;
  }
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType AddAllFastCallbackPatch(AnyCType receiver,
                                          AnyCType arg_i32, AnyCType arg_u32,
                                          AnyCType arg_i64, AnyCType arg_u64,
                                          AnyCType arg_f32, AnyCType arg_f64,
                                          AnyCType options) {
    AnyCType ret;
    ret.double_value = AddAllFastCallback(
        receiver.object_value, arg_i32.int32_value, arg_u32.uint32_value,
        arg_i64.int64_value, arg_u64.uint64_value, arg_f32.float_value,
        arg_f64.double_value, *options.options_value);
    return ret;
  }

#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static double AddAllFastCallback(Local<Object> receiver, int32_t arg_i32,
                                   uint32_t arg_u32, int64_t arg_i64,
                                   uint64_t arg_u64, float arg_f32,
                                   double arg_f64,
                                   FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    return static_cast<double>(arg_i32) + static_cast<double>(arg_u32) +
           static_cast<double>(arg_i64) + static_cast<double>(arg_u64) +
           static_cast<double>(arg_f32) + arg_f64;
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType AddAllFastCallbackNoOptionsPatch(
      AnyCType receiver, AnyCType arg_i32, AnyCType arg_u32, AnyCType arg_i64,
      AnyCType arg_u64, AnyCType arg_f32, AnyCType arg_f64) {
    AnyCType ret;
    ret.double_value = AddAllFastCallbackNoOptions(
        receiver.object_value, arg_i32.int32_value, arg_u32.uint32_value,
        arg_i64.int64_value, arg_u64.uint64_value, arg_f32.float_value,
        arg_f64.double_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static double AddAllFastCallbackNoOptions(Local<Object> receiver,
                                            int32_t arg_i32, uint32_t arg_u32,
                                            int64_t arg_i64, uint64_t arg_u64,
                                            float arg_f32, double arg_f64) {
    FastCApiObject* self = UnwrapObject(receiver);
    if (!self) {
      self = &FastCApiObject::instance();
    }
    self->fast_call_count_++;

    return static_cast<double>(arg_i32) + static_cast<double>(arg_u32) +
           static_cast<double>(arg_i64) + static_cast<double>(arg_u64) +
           static_cast<double>(arg_f32) + arg_f64;
  }

  static void AddAllSlowCallback(const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    double sum = 0;
    if (info.Length() > 0 && info[0]->IsNumber()) {
      sum += info[0]->Int32Value(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 1 && info[1]->IsNumber()) {
      sum += info[1]->Uint32Value(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 2 && info[2]->IsNumber()) {
      sum += info[2]->IntegerValue(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 3 && info[3]->IsNumber()) {
      sum += info[3]->IntegerValue(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 4 && info[4]->IsNumber()) {
      sum += info[4]->NumberValue(isolate->GetCurrentContext()).FromJust();
    } else {
      sum += std::numeric_limits<double>::quiet_NaN();
    }
    if (info.Length() > 5 && info[5]->IsNumber()) {
      sum += info[5]->NumberValue(isolate->GetCurrentContext()).FromJust();
    } else {
      sum += std::numeric_limits<double>::quiet_NaN();
    }

    info.GetReturnValue().Set(Number::New(isolate, sum));
  }

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
  typedef double Type;
#else
  typedef int32_t Type;
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType AddAllSequenceFastCallbackPatch(AnyCType receiver,
                                                  AnyCType seq_arg,
                                                  AnyCType options) {
    AnyCType ret;
#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
    ret.double_value = AddAllSequenceFastCallback(
        receiver.object_value, seq_arg.sequence_value, *options.options_value);
#else
    ret.int32_value = AddAllSequenceFastCallback(
        receiver.object_value, seq_arg.sequence_value, *options.options_value);
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static Type AddAllSequenceJSArrayHelper(v8::Isolate* isolate,
                                          Local<Array> seq_arg) {
    Type sum = 0;
    uint32_t length = seq_arg->Length();
    if (length > 1024) {
      isolate->ThrowError(
          "Invalid length of array, must be between 0 and 1024.");
      return sum;
    }

    for (uint32_t i = 0; i < length; ++i) {
      v8::MaybeLocal<v8::Value> maybe_element =
          seq_arg->Get(isolate->GetCurrentContext(),
                       v8::Integer::NewFromUnsigned(isolate, i));
      if (maybe_element.IsEmpty()) return sum;

      v8::Local<v8::Value> element = maybe_element.ToLocalChecked();
      if (element->IsNumber()) {
        double value = element->ToNumber(isolate->GetCurrentContext())
                           .ToLocalChecked()
                           ->Value();
        sum += value;
      } else if (element->IsUndefined()) {
        // Hole: ignore the element.
      } else {
        isolate->ThrowError("unexpected element type in JSArray");
        return sum;
      }
    }
    return sum;
  }

  static Type AddAllSequenceFastCallback(Local<Object> receiver,
                                         Local<Object> seq_arg,
                                         FastApiCallbackOptions& options) {
    if (seq_arg->IsUint32Array()) {
      return AddAllTypedArrayFastCallback<uint32_t>(receiver, seq_arg, options);
    }

    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    HandleScope handle_scope(options.isolate);
    if (!seq_arg->IsArray()) {
      options.isolate->ThrowError(
          "This method expects an array as a first argument.");
      return 0;
    }
    Local<Array> array = seq_arg.As<Array>();
    uint32_t length = array->Length();
    if (length > 1024) {
      receiver->GetIsolate()->ThrowError(
          "Invalid length of array, must be between 0 and 1024.");
      return 0;
    }

    Type buffer[1024];
    bool result = TryToCopyAndConvertArrayToCppBuffer<
        CTypeInfoBuilder<Type>::Build().GetId(), Type>(array, buffer, 1024);
    if (!result) {
      return AddAllSequenceJSArrayHelper(receiver->GetIsolate(), array);
    }
    DCHECK_EQ(array->Length(), length);

    Type sum = 0;
    for (uint32_t i = 0; i < length; ++i) {
      sum += buffer[i];
    }

    return sum;
  }

  static void AddAllSequenceSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();

    HandleScope handle_scope(isolate);

    if (info.Length() < 1) {
      self->slow_call_count_++;
      isolate->ThrowError("This method expects at least 1 arguments.");
      return;
    }
    if (info[0]->IsTypedArray()) {
      AddAllTypedArraySlowCallback(info);
      return;
    }
    if (info[0]->IsNumber()) {
      AddAllSlowCallback(info);
      return;
    }
    self->slow_call_count_++;
    if (info[0]->IsUndefined()) {
      Type dummy_result = 0;
      info.GetReturnValue().Set(Number::New(isolate, dummy_result));
      return;
    }
    if (!info[0]->IsArray()) {
      isolate->ThrowError("This method expects an array as a first argument.");
      return;
    }
    Local<Array> seq_arg = info[0].As<Array>();
    Type sum = AddAllSequenceJSArrayHelper(isolate, seq_arg);

    info.GetReturnValue().Set(Number::New(isolate, sum));
  }
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  template <typename T>
  static AnyCType AddAllTypedArrayFastCallbackPatch(AnyCType receiver,
                                                    AnyCType typed_array_arg,
                                                    AnyCType options) {
    AnyCType ret;
#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
    ret.double_value = AddAllTypedArrayFastCallback<T>(
        receiver.object_value, typed_array_arg.object_value,
        *options.options_value);
#else
    ret.int32_value = AddAllTypedArrayFastCallback<T>(
        receiver.object_value, typed_array_arg.object_value,
        *options.options_value);
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  template <typename T>
  static Type AddAllTypedArrayFastCallback(Local<Object> receiver,
                                           Local<Value> typed_array_arg,
                                           FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    HandleScope handle_scope(options.isolate);
    if (!typed_array_arg->IsTypedArray()) {
      options.isolate->ThrowError(
          "This method expects a TypedArray as a first argument.");
      return 0;
    }
    T* memory = reinterpret_cast<T*>(
        typed_array_arg.As<TypedArray>()->Buffer()->Data());
    size_t length = typed_array_arg.As<TypedArray>()->ByteLength() / sizeof(T);
    double sum = 0;
    for (size_t i = 0; i < length; ++i) {
      sum += static_cast<double>(memory[i]);
    }
    return static_cast<Type>(sum);
  }

  static void AddAllTypedArraySlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    if (info.Length() < 1) {
      isolate->ThrowError("This method expects at least 1 arguments.");
      return;
    }
    if (!info[0]->IsTypedArray()) {
      isolate->ThrowError(
          "This method expects a TypedArray as a second argument.");
      return;
    }

    Local<TypedArray> typed_array_arg = info[0].As<TypedArray>();
    size_t length = typed_array_arg->Length();

    void* data = typed_array_arg->Buffer()->GetBackingStore()->Data();
    if (typed_array_arg->IsUint8Array() || typed_array_arg->IsInt32Array() ||
        typed_array_arg->IsUint32Array() ||
        typed_array_arg->IsBigInt64Array() ||
        typed_array_arg->IsBigUint64Array()) {
      int64_t sum = 0;
      for (unsigned i = 0; i < length; ++i) {
        if (typed_array_arg->IsUint8Array()) {
          sum += static_cast<uint8_t*>(data)[i];
        } else if (typed_array_arg->IsInt32Array()) {
          sum += static_cast<int32_t*>(data)[i];
        } else if (typed_array_arg->IsUint32Array()) {
          sum += static_cast<uint32_t*>(data)[i];
        } else if (typed_array_arg->IsBigInt64Array()) {
          sum += static_cast<int64_t*>(data)[i];
        } else if (typed_array_arg->IsBigUint64Array()) {
          sum += static_cast<uint64_t*>(data)[i];
        }
      }
      info.GetReturnValue().Set(Number::New(isolate, sum));
    } else if (typed_array_arg->IsFloat32Array() ||
               typed_array_arg->IsFloat64Array()) {
      double sum = 0;
      for (unsigned i = 0; i < length; ++i) {
        if (typed_array_arg->IsFloat32Array()) {
          sum += static_cast<float*>(data)[i];
        } else if (typed_array_arg->IsFloat64Array()) {
          sum += static_cast<double*>(data)[i];
        }
      }
      info.GetReturnValue().Set(Number::New(isolate, sum));
    } else {
      isolate->ThrowError("TypedArray type is not supported.");
      return;
    }
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType Add32BitIntFastCallbackPatch(AnyCType receiver,
                                               AnyCType arg_i32,
                                               AnyCType arg_u32,
                                               AnyCType options) {
    AnyCType ret;
    ret.int32_value =
        Add32BitIntFastCallback(receiver.object_value, arg_i32.int32_value,
                                arg_u32.uint32_value, *options.options_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static int Add32BitIntFastCallback(v8::Local<v8::Object> receiver,
                                     int32_t arg_i32, uint32_t arg_u32,
                                     FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    return arg_i32 + arg_u32;
  }
  static void Add32BitIntSlowCallback(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    double sum = 0;
    if (info.Length() > 0 && info[0]->IsNumber()) {
      sum += info[0]->Int32Value(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 1 && info[1]->IsNumber()) {
      sum += info[1]->Uint32Value(isolate->GetCurrentContext()).FromJust();
    }

    info.GetReturnValue().Set(Number::New(isolate, sum));
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType AddAll32BitIntFastCallback_8ArgsPatch(
      AnyCType receiver, AnyCType arg1_i32, AnyCType arg2_i32,
      AnyCType arg3_i32, AnyCType arg4_u32, AnyCType arg5_u32,
      AnyCType arg6_u32, AnyCType arg7_u32, AnyCType arg8_u32,
      AnyCType options) {
    AnyCType ret;
    ret.int32_value = AddAll32BitIntFastCallback_8Args(
        receiver.object_value, arg1_i32.int32_value, arg2_i32.int32_value,
        arg3_i32.int32_value, arg4_u32.uint32_value, arg5_u32.uint32_value,
        arg6_u32.uint32_value, arg7_u32.uint32_value, arg8_u32.uint32_value,
        *options.options_value);
    return ret;
  }
  static AnyCType AddAll32BitIntFastCallback_6ArgsPatch(
      AnyCType receiver, AnyCType arg1_i32, AnyCType arg2_i32,
      AnyCType arg3_i32, AnyCType arg4_u32, AnyCType arg5_u32,
      AnyCType arg6_u32, AnyCType options) {
    AnyCType ret;
    ret.int32_value = AddAll32BitIntFastCallback_6Args(
        receiver.object_value, arg1_i32.int32_value, arg2_i32.int32_value,
        arg3_i32.int32_value, arg4_u32.uint32_value, arg5_u32.uint32_value,
        arg6_u32.uint32_value, *options.options_value);
    return ret;
  }
  static AnyCType AddAll32BitIntFastCallback_5ArgsPatch(
      AnyCType receiver, AnyCType arg1_i32, AnyCType arg2_i32,
      AnyCType arg3_i32, AnyCType arg4_u32, AnyCType arg5_u32,
      AnyCType options) {
    AnyCType arg6;
    arg6.uint32_value = 0;
    return AddAll32BitIntFastCallback_6ArgsPatch(receiver, arg1_i32, arg2_i32,
                                                 arg3_i32, arg4_u32, arg5_u32,
                                                 arg6, options);
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static int AddAll32BitIntFastCallback_8Args(
      Local<Object> receiver, int32_t arg1_i32, int32_t arg2_i32,
      int32_t arg3_i32, uint32_t arg4_u32, uint32_t arg5_u32, uint32_t arg6_u32,
      uint32_t arg7_u32, uint32_t arg8_u32, FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    int64_t result = static_cast<int64_t>(arg1_i32) + arg2_i32 + arg3_i32 +
                     arg4_u32 + arg5_u32 + arg6_u32 + arg7_u32 + arg8_u32;
    if (result > INT_MAX) return INT_MAX;
    if (result < INT_MIN) return INT_MIN;
    return static_cast<int>(result);
  }
  static int AddAll32BitIntFastCallback_6Args(
      Local<Object> receiver, int32_t arg1_i32, int32_t arg2_i32,
      int32_t arg3_i32, uint32_t arg4_u32, uint32_t arg5_u32, uint32_t arg6_u32,
      FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    int64_t result = static_cast<int64_t>(arg1_i32) + arg2_i32 + arg3_i32 +
                     arg4_u32 + arg5_u32 + arg6_u32;
    if (result > INT_MAX) return INT_MAX;
    if (result < INT_MIN) return INT_MIN;
    return static_cast<int>(result);
  }
  static int AddAll32BitIntFastCallback_5Args(
      Local<Object> receiver, int32_t arg1_i32, int32_t arg2_i32,
      int32_t arg3_i32, uint32_t arg4_u32, uint32_t arg5_u32,
      FastApiCallbackOptions& options) {
    return AddAll32BitIntFastCallback_6Args(
        receiver, arg1_i32, arg2_i32, arg3_i32, arg4_u32, arg5_u32, 0, options);
  }
  static void AddAll32BitIntSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    Local<Context> context = isolate->GetCurrentContext();
    double sum = 0;
    if (info.Length() > 0 && info[0]->IsNumber()) {
      sum += info[0]->Int32Value(context).FromJust();
    }
    if (info.Length() > 1 && info[1]->IsNumber()) {
      sum += info[1]->Int32Value(context).FromJust();
    }
    if (info.Length() > 2 && info[2]->IsNumber()) {
      sum += info[2]->Int32Value(context).FromJust();
    }
    if (info.Length() > 3 && info[3]->IsNumber()) {
      sum += info[3]->Uint32Value(context).FromJust();
    }
    if (info.Length() > 4 && info[4]->IsNumber()) {
      sum += info[4]->Uint32Value(context).FromJust();
    }
    if (info.Length() > 5 && info[5]->IsNumber()) {
      sum += info[5]->Uint32Value(context).FromJust();
    }
    if (info.Length() > 7 && info[6]->IsNumber() && info[7]->IsNumber()) {
      // info[6] and info[7] only get handled together, because we want to
      // have functions in the list of overloads with 6 parameters and with 8
      // parameters, but not with 7 parameters.
      sum += info[6]->Uint32Value(context).FromJust();
      sum += info[7]->Uint32Value(context).FromJust();
    }

    info.GetReturnValue().Set(Number::New(isolate, sum));
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  template <v8::CTypeInfo::Flags flags>
  static AnyCType AddAllAnnotateFastCallbackPatch(
      AnyCType receiver, AnyCType arg_i32, AnyCType arg_u32, AnyCType arg_i64,
      AnyCType arg_u64, AnyCType options) {
    AnyCType ret;
    ret.double_value = AddAllAnnotateFastCallback<flags>(
        receiver.object_value, arg_i32.int32_value, arg_u32.uint32_value,
        arg_i64.int64_value, arg_u64.uint64_value, *options.options_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  template <v8::CTypeInfo::Flags flags>
  static double AddAllAnnotateFastCallback(Local<Object> receiver,
                                           int32_t arg_i32, uint32_t arg_u32,
                                           int64_t arg_i64, uint64_t arg_u64,
                                           FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_NOT_NULL(self);
    self->fast_call_count_++;

    return static_cast<double>(arg_i32) + static_cast<double>(arg_u32) +
           static_cast<double>(arg_i64) + static_cast<double>(arg_u64);
  }

  static void AddAllAnnotateSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    double sum = 0;
    if (info.Length() > 1 && info[1]->IsNumber()) {
      sum += info[1]->Int32Value(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 2 && info[2]->IsNumber()) {
      sum += info[2]->Uint32Value(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 3 && info[3]->IsNumber()) {
      sum += info[3]->IntegerValue(isolate->GetCurrentContext()).FromJust();
    }
    if (info.Length() > 4 && info[4]->IsNumber()) {
      sum += info[4]->IntegerValue(isolate->GetCurrentContext()).FromJust();
    }

    info.GetReturnValue().Set(Number::New(isolate, sum));
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType EnforceRangeCompareI32Patch(AnyCType receiver,
                                              AnyCType real_arg,
                                              AnyCType checked_arg,
                                              AnyCType options) {
    AnyCType ret;
    ret.bool_value = EnforceRangeCompare<int32_t>(
        receiver.object_value, real_arg.double_value, checked_arg.int32_value,
        *options.options_value);
    return ret;
  }
  static AnyCType EnforceRangeCompareU32Patch(AnyCType receiver,
                                              AnyCType real_arg,
                                              AnyCType checked_arg,
                                              AnyCType options) {
    AnyCType ret;
    ret.bool_value = EnforceRangeCompare<uint32_t>(
        receiver.object_value, real_arg.double_value, checked_arg.uint32_value,
        *options.options_value);
    return ret;
  }
  static AnyCType EnforceRangeCompareI64Patch(AnyCType receiver,
                                              AnyCType real_arg,
                                              AnyCType checked_arg,
                                              AnyCType options) {
    AnyCType ret;
    ret.bool_value = EnforceRangeCompare<int64_t>(
        receiver.object_value, real_arg.double_value, checked_arg.int64_value,
        *options.options_value);
    return ret;
  }
  static AnyCType EnforceRangeCompareU64Patch(AnyCType receiver,
                                              AnyCType real_arg,
                                              AnyCType checked_arg,
                                              AnyCType options) {
    AnyCType ret;
    ret.bool_value = EnforceRangeCompare<uint64_t>(
        receiver.object_value, real_arg.double_value, checked_arg.uint64_value,
        *options.options_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  template <typename IntegerT>
  static bool EnforceRangeCompare(Local<Object> receiver, double real_arg,
                                  IntegerT checked_arg,
                                  FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_NOT_NULL(self);
    self->fast_call_count_++;

    if (!base::IsValueInRangeForNumericType<IntegerT>(real_arg)) {
      return false;
    }
    return static_cast<IntegerT>(real_arg) == checked_arg;
  }

  template <typename IntegerT>
  static void EnforceRangeCompareSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    double real_arg = 0;
    if (info.Length() > 0 && info[0]->IsNumber()) {
      real_arg = info[0]->NumberValue(isolate->GetCurrentContext()).FromJust();
    }
    // Special range checks for int64 and uint64. uint64_max rounds to 2^64 when
    // converted to double, so 2^64 would be considered within uint64 range even
    // though it's not. For int64 the same happens with 2^63.
    bool in_range = base::IsValueInRangeForNumericType<IntegerT>(real_arg);
    if (in_range) {
      IntegerT checked_arg = 0;
      if (info.Length() > 1 && info[1]->IsNumber()) {
        double checked_arg_as_double =
            info[1]->NumberValue(isolate->GetCurrentContext()).FromJust();
        if (base::IsValueInRangeForNumericType<IntegerT>(
                checked_arg_as_double)) {
          checked_arg = static_cast<IntegerT>(checked_arg_as_double);
        }
      }
      info.GetReturnValue().Set(static_cast<IntegerT>(real_arg) == checked_arg);
    } else {
      info.GetIsolate()->ThrowError("Argument out of range.");
    }
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType ClampCompareI32Patch(AnyCType receiver, AnyCType in_range,
                                       AnyCType real_arg, AnyCType checked_arg,
                                       AnyCType options) {
    AnyCType ret;
    ret.double_value = ClampCompare<int32_t>(
        receiver.object_value, in_range.bool_value, real_arg.double_value,
        checked_arg.int32_value, *options.options_value);
    return ret;
  }
  static AnyCType ClampCompareU32Patch(AnyCType receiver, AnyCType in_range,
                                       AnyCType real_arg, AnyCType checked_arg,
                                       AnyCType options) {
    AnyCType ret;
    ret.double_value = ClampCompare<uint32_t>(
        receiver.object_value, in_range.bool_value, real_arg.double_value,
        checked_arg.uint32_value, *options.options_value);
    return ret;
  }
  static AnyCType ClampCompareI64Patch(AnyCType receiver, AnyCType in_range,
                                       AnyCType real_arg, AnyCType checked_arg,
                                       AnyCType options) {
    AnyCType ret;
    ret.double_value = ClampCompare<int64_t>(
        receiver.object_value, in_range.bool_value, real_arg.double_value,
        checked_arg.int64_value, *options.options_value);
    return ret;
  }
  static AnyCType ClampCompareU64Patch(AnyCType receiver, AnyCType in_range,
                                       AnyCType real_arg, AnyCType checked_arg,
                                       AnyCType options) {
    AnyCType ret;
    ret.double_value = ClampCompare<uint64_t>(
        receiver.object_value, in_range.bool_value, real_arg.double_value,
        checked_arg.uint64_value, *options.options_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  template <typename IntegerT>
  static double ClampCompareCompute(bool in_range, double real_arg,
                                    IntegerT checked_arg) {
    if (i::v8_flags.fuzzing) {
      return static_cast<double>(checked_arg);
    }
    if (!in_range) {
      IntegerT lower_bound = std::numeric_limits<IntegerT>::min();
      IntegerT upper_bound = std::numeric_limits<IntegerT>::max();
      if (lower_bound < internal::kMinSafeInteger) {
        lower_bound = static_cast<IntegerT>(internal::kMinSafeInteger);
      }
      if (upper_bound > internal::kMaxSafeInteger) {
        upper_bound = static_cast<IntegerT>(internal::kMaxSafeInteger);
      }
      CHECK(!std::isnan(real_arg));
      if (real_arg < static_cast<double>(lower_bound)) {
        CHECK_EQ(lower_bound, checked_arg);
      } else if (real_arg > static_cast<double>(upper_bound)) {
        CHECK_EQ(upper_bound, checked_arg);
      } else {
        FATAL("Expected value to be out of range.");
      }
    } else if (!std::isnan(real_arg)) {
      if (real_arg != checked_arg) {
        // Check if rounding towards nearest even number happened.
        double diff = std::fabs(real_arg - checked_arg);
        CHECK_LE(diff, 0.5);
        if (diff == 0) {
          // Check if rounding towards nearest even number happened.
          CHECK_EQ(0, checked_arg % 2);
        } else if (checked_arg % 2 == 1) {
          // Behave as if rounding towards nearest even number *has*
          // happened (as it does on the fast path).
          checked_arg += 1;
        }
      } else {
        CHECK_EQ(static_cast<IntegerT>(real_arg), checked_arg);
      }
    }
    return checked_arg;
  }

  template <typename IntegerT>
  static double ClampCompare(Local<Object> receiver, bool in_range,
                             double real_arg, IntegerT checked_arg,
                             FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_NOT_NULL(self);
    self->fast_call_count_++;

    double result = ClampCompareCompute(in_range, real_arg, checked_arg);
    return static_cast<double>(result);
  }

  template <typename IntegerT>
  static void ClampCompareSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    double real_arg = 0;
    if (info.Length() > 1 && info[1]->IsNumber()) {
      real_arg = info[1]->NumberValue(isolate->GetCurrentContext()).FromJust();
    }
    double checked_arg_dbl = std::numeric_limits<double>::max();
    if (info.Length() > 2 && info[2]->IsNumber()) {
      checked_arg_dbl = info[2].As<Number>()->Value();
    }
    bool in_range =
        info[0]->IsBoolean() && info[0]->BooleanValue(isolate) &&
        base::IsValueInRangeForNumericType<IntegerT>(real_arg) &&
        base::IsValueInRangeForNumericType<IntegerT>(checked_arg_dbl);

    IntegerT checked_arg = std::numeric_limits<IntegerT>::max();
    if (in_range) {
      if (checked_arg_dbl != std::numeric_limits<double>::max()) {
        checked_arg = static_cast<IntegerT>(checked_arg_dbl);
      }
      double result = ClampCompareCompute(in_range, real_arg, checked_arg);
      info.GetReturnValue().Set(Number::New(isolate, result));
    } else {
      IntegerT clamped = std::numeric_limits<IntegerT>::max();
      if (std::isnan(checked_arg_dbl) || std::isnan(real_arg)) {
        clamped = 0;
      } else {
        IntegerT lower_bound = std::numeric_limits<IntegerT>::min();
        IntegerT upper_bound = std::numeric_limits<IntegerT>::max();
        if (lower_bound < internal::kMinSafeInteger) {
          lower_bound = static_cast<IntegerT>(internal::kMinSafeInteger);
        }
        if (upper_bound > internal::kMaxSafeInteger) {
          upper_bound = static_cast<IntegerT>(internal::kMaxSafeInteger);
        }

        clamped = std::clamp(real_arg, static_cast<double>(lower_bound),
                             static_cast<double>(upper_bound));
      }
      info.GetReturnValue().Set(Number::New(isolate, clamped));
    }
  }

  static bool IsFastCApiObjectFastCallback(v8::Local<v8::Object> receiver,
                                           v8::Local<v8::Value> arg,
                                           FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(false);

    self->fast_call_count_++;

    if (!arg->IsObject()) {
      return false;
    }
    Local<Object> object = arg.As<Object>();
    if (!IsValidApiObject(object)) return false;

    Isolate* isolate = options.isolate;
    HandleScope handle_scope(isolate);
    return PerIsolateData::Get(isolate)
        ->GetTestApiObjectCtor()
        ->IsLeafTemplateForApiObject(object);
  }

  static void IsFastCApiObjectSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();

    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    HandleScope handle_scope(isolate);

    bool result = false;
    if (info.Length() < 1) {
      info.GetIsolate()->ThrowError(
          "is_valid_api_object should be called with an argument");
      return;
    }
    if (info[0]->IsObject()) {
      Local<Object> object = info[0].As<Object>();
      if (!IsValidApiObject(object)) {
        result = false;
      } else {
        result = PerIsolateData::Get(info.GetIsolate())
                     ->GetTestApiObjectCtor()
                     ->IsLeafTemplateForApiObject(object);
      }
    }

    info.GetReturnValue().Set(result);
  }

  static bool TestWasmMemoryFastCallback(Local<Object> receiver,
                                         uint32_t address,
                                         FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(false);
    self->fast_call_count_++;

    if (i::v8_flags.fuzzing) {
      return true;
    }
    v8::Isolate* isolate = receiver->GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> mem_string =
        v8::String::NewFromUtf8(isolate, "wasm_memory").ToLocalChecked();
    v8::Local<v8::Value> mem;
    if (!receiver->Get(context, mem_string).ToLocal(&mem)) {
      isolate->ThrowError(
          "wasm_memory was used when the WebAssembly.Memory was not set on the "
          "receiver.");
    }

    v8::Local<v8::WasmMemoryObject> wasm_memory =
        mem.As<v8::WasmMemoryObject>();
    reinterpret_cast<uint8_t*>(wasm_memory->Buffer()->Data())[address] = 42;

    return true;
  }

  static void TestWasmMemorySlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    info.GetIsolate()->ThrowError("should be unreachable from wasm");
  }

  static void AssertIsExternal(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();

    Local<Value> value = info[0];

    if (!value->IsExternal()) {
      info.GetIsolate()->ThrowError("Did not get an external.");
    }
  }

  static void* GetPointerFastCallback(Local<Object> receiver,
                                      FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(nullptr);
    self->fast_call_count_++;

    if (i::v8_flags.fuzzing) {
      return nullptr;
    }
    return static_cast<void*>(self);
  }

  static void GetPointerSlowCallback(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (i::v8_flags.fuzzing) {
      info.GetReturnValue().Set(v8::Null(isolate));
      return;
    }
    info.GetReturnValue().Set(External::New(isolate, static_cast<void*>(self)));
  }

  static void* GetNullPointerFastCallback(Local<Object> receiver,
                                          FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(nullptr);
    self->fast_call_count_++;

    return nullptr;
  }

  static void GetNullPointerSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    info.GetReturnValue().Set(v8::Null(isolate));
  }

  static void* PassPointerFastCallback(Local<Object> receiver, void* pointer,
                                       FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(nullptr);
    self->fast_call_count_++;

    return pointer;
  }

  static void PassPointerSlowCallback(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 1) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected one.");
      return;
    }

    Local<Value> maybe_external = info[0].As<Value>();

    if (maybe_external->IsNull()) {
      info.GetReturnValue().Set(maybe_external);
      return;
    }
    if (!maybe_external->IsExternal()) {
      info.GetIsolate()->ThrowError("Did not get an external.");
      return;
    }

    Local<External> external = info[0].As<External>();

    info.GetReturnValue().Set(external);
  }

  static bool ComparePointersFastCallback(Local<Object> receiver,
                                          void* pointer_a, void* pointer_b,
                                          FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(false);
    self->fast_call_count_++;

    return pointer_a == pointer_b;
  }

  static void ComparePointersSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 2) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected two.");
      return;
    }

    Local<Value> value_a = info[0];
    Local<Value> value_b = info[1];

    void* pointer_a;
    if (value_a->IsNull()) {
      pointer_a = nullptr;
    } else if (value_a->IsExternal()) {
      pointer_a = value_a.As<External>()->Value();
    } else {
      info.GetIsolate()->ThrowError(
          "Did not get an external as first parameter.");
      return;
    }

    void* pointer_b;
    if (value_b->IsNull()) {
      pointer_b = nullptr;
    } else if (value_b->IsExternal()) {
      pointer_b = value_b.As<External>()->Value();
    } else {
      info.GetIsolate()->ThrowError(
          "Did not get an external as second parameter.");
      return;
    }

    info.GetReturnValue().Set(pointer_a == pointer_b);
  }

  static int64_t sumInt64FastCallback(Local<Object> receiver, int64_t a,
                                      int64_t b,
                                      FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;

    return a + b;
  }

  template <typename T>
  static bool Convert(double value, T* out_result) {
    if (!base::IsValueInRangeForNumericType<T>(value)) return false;
    *out_result = static_cast<T>(value);
    return true;
  }

  static void sumInt64AsNumberSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 2) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected two.");
      return;
    }

    Local<Value> value_a = info[0];
    Local<Value> value_b = info[1];

    if (!value_a->IsNumber()) {
      info.GetIsolate()->ThrowError("Did not get a number as first parameter.");
      return;
    }
    int64_t a;
    if (!Convert(value_a.As<Number>()->Value(), &a)) {
      info.GetIsolate()->ThrowError("First number is out of int64_t range.");
      return;
    }

    if (!value_b->IsNumber()) {
      info.GetIsolate()->ThrowError(
          "Did not get a number as second parameter.");
      return;
    }
    int64_t b;
    if (!Convert(value_b.As<Number>()->Value(), &b)) {
      info.GetIsolate()->ThrowError("Second number is out of int64_t range.");
      return;
    }

    info.GetReturnValue().Set(Number::New(isolate, static_cast<double>(a + b)));
  }

  static void sumInt64AsBigIntSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 2) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected two.");
      return;
    }

    Local<Value> value_a = info[0];
    Local<Value> value_b = info[1];

    int64_t a;
    if (value_a->IsBigInt()) {
      a = static_cast<int64_t>(value_a.As<BigInt>()->Int64Value());
    } else {
      info.GetIsolate()->ThrowError("Did not get a BigInt as first parameter.");
      return;
    }

    int64_t b;
    if (value_b->IsBigInt()) {
      b = static_cast<int64_t>(value_b.As<BigInt>()->Int64Value());
    } else {
      info.GetIsolate()->ThrowError(
          "Did not get a BigInt as second parameter.");
      return;
    }

    info.GetReturnValue().Set(BigInt::New(isolate, a + b));
  }

  static uint64_t sumUint64FastCallback(Local<Object> receiver, uint64_t a,
                                        uint64_t b,
                                        FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;
    // This CHECK here is unnecessary, but it keeps this function from getting
    // merged with `sumInt64FastCallback`. There is a test which relies on
    // `sumUint64FastCallback` and `sumInt64FastCallback` being different call
    // targets.
    CHECK_GT(self->fast_call_count_, 0);
    return a + b;
  }

  static void sumUint64AsNumberSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 2) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected two.");
      return;
    }

    Local<Value> value_a = info[0];
    Local<Value> value_b = info[1];

    if (!value_a->IsNumber()) {
      info.GetIsolate()->ThrowError("Did not get a number as first parameter.");
      return;
    }
    uint64_t a;
    if (!Convert(value_a.As<Number>()->Value(), &a)) {
      info.GetIsolate()->ThrowError("First number is out of uint64_t range.");
      return;
    }

    if (!value_b->IsNumber()) {
      info.GetIsolate()->ThrowError(
          "Did not get a number as second parameter.");
      return;
    }
    uint64_t b;
    if (!Convert(value_b.As<Number>()->Value(), &b)) {
      info.GetIsolate()->ThrowError("Second number is out of uint64_t range.");
      return;
    }

    info.GetReturnValue().Set(Number::New(isolate, static_cast<double>(a + b)));
  }

  static void sumUint64AsBigIntSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    Isolate* isolate = info.GetIsolate();
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->slow_call_count_++;

    if (info.Length() != 2) {
      info.GetIsolate()->ThrowError(
          "Invalid number of arguments, expected two.");
      return;
    }

    Local<Value> value_a = info[0];
    Local<Value> value_b = info[1];

    uint64_t a;
    if (value_a->IsBigInt()) {
      a = static_cast<uint64_t>(value_a.As<BigInt>()->Uint64Value());
    } else {
      info.GetIsolate()->ThrowError("Did not get a BigInt as first parameter.");
      return;
    }

    uint64_t b;
    if (value_b->IsBigInt()) {
      b = static_cast<uint64_t>(value_b.As<BigInt>()->Uint64Value());
    } else {
      info.GetIsolate()->ThrowError(
          "Did not get a BigInt as second parameter.");
      return;
    }

    info.GetReturnValue().Set(BigInt::NewFromUnsigned(isolate, a + b));
  }

  static void AttributeGetterSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    FastCApiObject* self = UnwrapObject(info.This());
    self->slow_call_count_++;
    info.GetReturnValue().Set(self->attribute_value_);
  }

  static int AttributeGetterFastCallback(Local<Object> receiver,
                                         FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS(0);
    self->fast_call_count_++;
    return self->attribute_value_;
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static AnyCType AttributeGetterFastCallbackPatch(AnyCType receiver,
                                                   AnyCType options) {
    AnyCType ret;
    ret.int32_value = AttributeGetterFastCallback(receiver.object_value,
                                                  *options.options_value);
    return ret;
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static void AttributeSetterSlowCallback(
      const FunctionCallbackInfo<Value>& info) {
    FastCApiObject* self = UnwrapObject(info.This());
    self->slow_call_count_++;
    if (info.Length() < 1 || !info[0]->IsNumber()) {
      info.GetIsolate()->ThrowError(
          "The attribute requires a number as a new value");
      return;
    }
    double double_val =
        info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromJust();
    if (!base::IsValueInRangeForNumericType<int>(double_val)) {
      info.GetIsolate()->ThrowError(
          "New value of attribute is not within int32 range");
      return;
    }
    self->attribute_value_ = static_cast<int>(double_val);
  }

  static void AttributeSetterFastCallback(Local<Object> receiver, int32_t value,
                                          FastApiCallbackOptions& options) {
    FastCApiObject* self = UnwrapObject(receiver);
    CHECK_SELF_OR_THROW_FAST_OPTIONS();
    self->fast_call_count_++;
    self->attribute_value_ = value;
  }

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  static void AttributeSetterFastCallbackPatch(AnyCType receiver,
                                               AnyCType value,
                                               AnyCType options) {
    AttributeSetterFastCallback(receiver.object_value, value.int32_value,
                                *options.options_value);
  }
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

  static void FastCallCount(const FunctionCallbackInfo<Value>& info) {
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    info.GetReturnValue().Set(
        Number::New(info.GetIsolate(), self->fast_call_count()));
  }
  static void SlowCallCount(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    info.GetReturnValue().Set(
        Number::New(info.GetIsolate(), self->slow_call_count()));
  }
  static void ResetCounts(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    self->reset_counts();
    info.GetReturnValue().Set(Undefined(info.GetIsolate()));
  }
  static void SupportsFPParams(const FunctionCallbackInfo<Value>& info) {
    DCHECK(i::ValidateCallbackInfo(info));
    FastCApiObject* self = UnwrapObject(info.This());
    CHECK_SELF_OR_THROW_SLOW();
    info.GetReturnValue().Set(self->supports_fp_params_);
  }

  int fast_call_count() const { return fast_call_count_; }
  int slow_call_count() const { return slow_call_count_; }
  void reset_counts() {
    fast_call_count_ = 0;
    slow_call_count_ = 0;
  }

  static const int kV8WrapperObjectIndex = 1;

 private:
  static bool IsValidApiObject(Local<Object> object) {
    if (object->IsInt32()) return false;
    auto instance_type = i::Internals::GetInstanceType(
        internal::ValueHelper::ValueAsAddress(*object));
    return (base::IsInRange(instance_type, i::Internals::kFirstJSApiObjectType,
                            i::Internals::kLastJSApiObjectType) ||
            instance_type == i::Internals::kJSSpecialApiObjectType);
  }
  static FastCApiObject* UnwrapObject(Local<Object> object) {
    if (!IsValidApiObject(object)) {
      return nullptr;
    }
    if (object->InternalFieldCount() <= kV8WrapperObjectIndex) {
      return nullptr;
    }
    FastCApiObject* wrapped = reinterpret_cast<FastCApiObject*>(
        object->GetAlignedPointerFromInternalField(kV8WrapperObjectIndex));
    CHECK_NOT_NULL(wrapped);
    return wrapped;
  }

  int fast_call_count_ = 0, slow_call_count_ = 0;
  int attribute_value_ = 0;
#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
  bool supports_fp_params_ = true;
#else   // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
  bool supports_fp_params_ = false;
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE
};

#undef CHECK_SELF_OR_THROW_SLOW
#undef CHECK_SELF_OR_THROW_FAST
#undef CHECK_SELF_OR_THROW_FAST_OPTIONS

// The object is statically initialized for simplicity, typically the embedder
// will take care of managing their C++ objects lifetime.
thread_local FastCApiObject kFastCApiObject;
}  // namespace

// static
FastCApiObject& FastCApiObject::instance() { return kFastCApiObject; }

void CreateFastCAPIObject(const FunctionCallbackInfo<Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  if (!info.IsConstructCall()) {
    info.GetIsolate()->ThrowError(
        "FastCAPI helper must be constructed with new.");
    return;
  }
  Local<Object> api_object = info.This();
  api_object->SetAlignedPointerInInternalField(
      FastCApiObject::kV8WrapperObjectIndex,
      reinterpret_cast<void*>(&kFastCApiObject));
  api_object->SetAccessorProperty(
      String::NewFromUtf8Literal(info.GetIsolate(), "supports_fp_params"),
      FunctionTemplate::New(info.GetIsolate(), FastCApiObject::SupportsFPParams)
          ->GetFunction(api_object->GetCreationContext(info.GetIsolate())
                            .ToLocalChecked())
          .ToLocalChecked());
}

Local<FunctionTemplate> Shell::CreateTestFastCApiTemplate(Isolate* isolate) {
  Local<FunctionTemplate> api_obj_ctor =
      FunctionTemplate::New(isolate, CreateFastCAPIObject);
  PerIsolateData::Get(isolate)->SetTestApiObjectCtor(api_obj_ctor);
  Local<Signature> signature = Signature::New(isolate, api_obj_ctor);
  {
    CFunction throw_no_fallback_func = CFunction::Make(
        FastCApiObject::ThrowNoFallbackFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::ThrowNoFallbackFastCallbackPatch));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "throw_no_fallback",
        FunctionTemplate::New(
            isolate, FastCApiObject::ThrowFallbackSlowCallback, Local<Value>(),
            Local<Signature>(), 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &throw_no_fallback_func));

    CFunction copy_str_func = CFunction::Make(
        FastCApiObject::CopyStringFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::CopyStringFastCallbackPatch));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "copy_string",
        FunctionTemplate::New(isolate, FastCApiObject::CopyStringSlowCallback,
                              Local<Value>(), signature, 1,
                              ConstructorBehavior::kThrow,
                              SideEffectType::kHasSideEffect, &copy_str_func));

    CFunction add_all_c_func =
        CFunction::Make(FastCApiObject::AddAllFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAllFastCallbackPatch));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all",
        FunctionTemplate::New(isolate, FastCApiObject::AddAllSlowCallback,
                              Local<Value>(), signature, 1,
                              ConstructorBehavior::kThrow,
                              SideEffectType::kHasSideEffect, &add_all_c_func));

    CFunction fast_setter = CFunction::Make(
        FastCApiObject::AttributeSetterFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::AttributeSetterFastCallback));
    CFunction fast_getter = CFunction::Make(
        FastCApiObject::AttributeGetterFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::AttributeGetterFastCallback));

    api_obj_ctor->PrototypeTemplate()->SetAccessorProperty(
        String::NewFromUtf8(isolate, "fast_attribute").ToLocalChecked(),
        FunctionTemplate::New(
            isolate, FastCApiObject::AttributeGetterSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &fast_getter),
        FunctionTemplate::New(
            isolate, FastCApiObject::AttributeSetterSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &fast_setter),
        v8::PropertyAttribute::None);

    CFunction add_all_seq_c_func = CFunction::Make(
        FastCApiObject::AddAllSequenceFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAllSequenceFastCallbackPatch));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_sequence",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllSequenceSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_seq_c_func));

    CFunction add_all_uint8_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<uint8_t>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<uint8_t>));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_uint8_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_uint8_typed_array_c_func));

    CFunction add_all_int32_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<int32_t>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<int32_t>));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_int32_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_int32_typed_array_c_func));

    CFunction add_all_int64_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<int64_t>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<int64_t>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_int64_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_int64_typed_array_c_func));

    CFunction add_all_uint64_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<uint64_t>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<uint64_t>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_uint64_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect,
            &add_all_uint64_typed_array_c_func));

    CFunction add_all_uint32_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<uint32_t>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<uint32_t>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_uint32_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect,
            &add_all_uint32_typed_array_c_func));

    CFunction add_all_float32_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<float> V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAllTypedArrayFastCallbackPatch<float>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_float32_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect,
            &add_all_float32_typed_array_c_func));

    CFunction add_all_no_options_c_func = CFunction::Make(
        FastCApiObject::AddAllFastCallbackNoOptions V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAllFastCallbackNoOptionsPatch),
        CFunctionInfo::Int64Representation::kBigInt);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_no_options",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllSlowCallback, Local<Value>(),
            Local<Signature>(), 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_no_options_c_func));

    CFunction add_all_float64_typed_array_c_func = CFunction::Make(
        FastCApiObject::AddAllTypedArrayFastCallback<double>
            V8_IF_USE_SIMULATOR(
                FastCApiObject::AddAllTypedArrayFastCallbackPatch<double>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_float64_typed_array",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllTypedArraySlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect,
            &add_all_float64_typed_array_c_func));

    const CFunction add_all_overloads[] = {
        add_all_seq_c_func,
        add_all_no_options_c_func,
    };
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_overload",
        FunctionTemplate::NewWithCFunctionOverloads(
            isolate, FastCApiObject::AddAllSequenceSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, {add_all_overloads, 2}));

    CFunction add_all_32bit_int_8args_c_func = CFunction::Make(
        FastCApiObject::AddAll32BitIntFastCallback_8Args V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAll32BitIntFastCallback_8ArgsPatch));
    CFunction add_all_32bit_int_6args_c_func = CFunction::Make(
        FastCApiObject::AddAll32BitIntFastCallback_6Args V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAll32BitIntFastCallback_6ArgsPatch));
    CFunction add_all_32bit_int_5args_c_func = CFunction::Make(
        FastCApiObject::AddAll32BitIntFastCallback_5Args V8_IF_USE_SIMULATOR(
            FastCApiObject::AddAll32BitIntFastCallback_5ArgsPatch));
    const CFunction c_function_overloads[] = {add_all_32bit_int_6args_c_func,
                                              add_all_32bit_int_5args_c_func};

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "overloaded_add_all_32bit_int",
        FunctionTemplate::NewWithCFunctionOverloads(
            isolate, FastCApiObject::AddAll32BitIntSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, {c_function_overloads, 2}));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "overloaded_add_all_8args",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAll32BitIntSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_32bit_int_8args_c_func));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "overloaded_add_all_32bit_int_no_sig",
        FunctionTemplate::NewWithCFunctionOverloads(
            isolate, FastCApiObject::AddAll32BitIntSlowCallback, Local<Value>(),
            Local<Signature>(), 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, {c_function_overloads, 2}));

    CFunction add_32bit_int_c_func = CFunction::Make(
        FastCApiObject::Add32BitIntFastCallback V8_IF_USE_SIMULATOR(
            FastCApiObject::Add32BitIntFastCallbackPatch));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_32bit_int",
        FunctionTemplate::New(
            isolate, FastCApiObject::Add32BitIntSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_32bit_int_c_func));

    CFunction add_all_annotate_c_func = CFunction::Make(
        FastCApiObject::AddAllAnnotateFastCallback<
            v8::CTypeInfo::Flags::kEnforceRangeBit>
            V8_IF_USE_SIMULATOR(FastCApiObject::AddAllAnnotateFastCallbackPatch<
                                v8::CTypeInfo::Flags::kEnforceRangeBit>));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_annotate_enforce_range",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAllAnnotateSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &add_all_annotate_c_func));

    // Testing enforce range annotation.

    CFunction enforce_range_compare_i32_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::EnforceRangeCompare<int32_t>)
            .Arg<2, v8::CTypeInfo::Flags::kEnforceRangeBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::EnforceRangeCompareI32Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "enforce_range_compare_i32",
        FunctionTemplate::New(
            isolate, FastCApiObject::EnforceRangeCompareSlowCallback<int32_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &enforce_range_compare_i32_c_func));

    CFunction enforce_range_compare_u32_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::EnforceRangeCompare<uint32_t>)
            .Arg<2, v8::CTypeInfo::Flags::kEnforceRangeBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::EnforceRangeCompareU32Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "enforce_range_compare_u32",
        FunctionTemplate::New(
            isolate, FastCApiObject::EnforceRangeCompareSlowCallback<uint32_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &enforce_range_compare_u32_c_func));

    CFunction enforce_range_compare_i64_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::EnforceRangeCompare<int64_t>)
            .Arg<2, v8::CTypeInfo::Flags::kEnforceRangeBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::EnforceRangeCompareI64Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "enforce_range_compare_i64",
        FunctionTemplate::New(
            isolate, FastCApiObject::EnforceRangeCompareSlowCallback<int64_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &enforce_range_compare_i64_c_func));

    CFunction enforce_range_compare_u64_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::EnforceRangeCompare<uint64_t>)
            .Arg<2, v8::CTypeInfo::Flags::kEnforceRangeBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::EnforceRangeCompareU64Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "enforce_range_compare_u64",
        FunctionTemplate::New(
            isolate, FastCApiObject::EnforceRangeCompareSlowCallback<uint64_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &enforce_range_compare_u64_c_func));

    // Testing clamp annotation.

    CFunction clamp_compare_i32_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::ClampCompare<int32_t>)
            .Arg<3, v8::CTypeInfo::Flags::kClampBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::ClampCompareI32Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "clamp_compare_i32",
        FunctionTemplate::New(
            isolate, FastCApiObject::ClampCompareSlowCallback<int32_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &clamp_compare_i32_c_func));

    CFunction clamp_compare_u32_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::ClampCompare<uint32_t>)
            .Arg<3, v8::CTypeInfo::Flags::kClampBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::ClampCompareU32Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "clamp_compare_u32",
        FunctionTemplate::New(
            isolate, FastCApiObject::ClampCompareSlowCallback<uint32_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &clamp_compare_u32_c_func));

    CFunction clamp_compare_i64_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::ClampCompare<int64_t>)
            .Arg<3, v8::CTypeInfo::Flags::kClampBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::ClampCompareI64Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "clamp_compare_i64",
        FunctionTemplate::New(
            isolate, FastCApiObject::ClampCompareSlowCallback<int64_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &clamp_compare_i64_c_func));

    CFunction clamp_compare_u64_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::ClampCompare<uint64_t>)
            .Arg<3, v8::CTypeInfo::Flags::kClampBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::ClampCompareU64Patch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "clamp_compare_u64",
        FunctionTemplate::New(
            isolate, FastCApiObject::ClampCompareSlowCallback<uint64_t>,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &clamp_compare_u64_c_func));

    CFunction is_valid_api_object_c_func =
        CFunction::Make(FastCApiObject::IsFastCApiObjectFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "is_fast_c_api_object",
        FunctionTemplate::New(
            isolate, FastCApiObject::IsFastCApiObjectSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &is_valid_api_object_c_func));

    CFunction test_wasm_memory_c_func =
        CFunction::Make(FastCApiObject::TestWasmMemoryFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "test_wasm_memory",
        FunctionTemplate::New(
            isolate, FastCApiObject::TestWasmMemorySlowCallback, Local<Value>(),
            Local<Signature>(), 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &test_wasm_memory_c_func));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "assert_is_external",
        FunctionTemplate::New(isolate, FastCApiObject::AssertIsExternal,
                              Local<Value>(), signature, 1,
                              ConstructorBehavior::kThrow,
                              SideEffectType::kHasSideEffect, nullptr));

    CFunction get_pointer_c_func =
        CFunction::Make(FastCApiObject::GetPointerFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "get_pointer",
        FunctionTemplate::New(
            isolate, FastCApiObject::GetPointerSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &get_pointer_c_func));
    CFunction get_null_pointer_c_func =
        CFunction::Make(FastCApiObject::GetNullPointerFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "get_null_pointer",
        FunctionTemplate::New(
            isolate, FastCApiObject::GetNullPointerSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &get_null_pointer_c_func));
    CFunction pass_pointer_c_func =
        CFunction::Make(FastCApiObject::PassPointerFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "pass_pointer",
        FunctionTemplate::New(
            isolate, FastCApiObject::PassPointerSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &pass_pointer_c_func));
    CFunction compare_pointers_c_func =
        CFunction::Make(FastCApiObject::ComparePointersFastCallback);
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "compare_pointers",
        FunctionTemplate::New(
            isolate, FastCApiObject::ComparePointersSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &compare_pointers_c_func));
    CFunction sum_int64_as_number_c_func =
        CFunctionBuilder().Fn(FastCApiObject::sumInt64FastCallback).Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "sum_int64_as_number",
        FunctionTemplate::New(
            isolate, FastCApiObject::sumInt64AsNumberSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &sum_int64_as_number_c_func));
    CFunction sum_int64_as_bigint_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::sumInt64FastCallback)
            .Build<CFunctionInfo::Int64Representation::kBigInt>();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "sum_int64_as_bigint",
        FunctionTemplate::New(
            isolate, FastCApiObject::sumInt64AsBigIntSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &sum_int64_as_bigint_c_func));
    CFunction sum_uint64_as_number_c_func =
        CFunctionBuilder().Fn(FastCApiObject::sumUint64FastCallback).Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "sum_uint64_as_number",
        FunctionTemplate::New(
            isolate, FastCApiObject::sumUint64AsNumberSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &sum_uint64_as_number_c_func));
    CFunction sum_uint64_as_bigint_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::sumUint64FastCallback)
            .Build<CFunctionInfo::Int64Representation::kBigInt>();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "sum_uint64_as_bigint",
        FunctionTemplate::New(
            isolate, FastCApiObject::sumUint64AsBigIntSlowCallback,
            Local<Value>(), signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasSideEffect, &sum_uint64_as_bigint_c_func));

    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "fast_call_count",
        FunctionTemplate::New(
            isolate, FastCApiObject::FastCallCount, Local<Value>(), signature,
            1, ConstructorBehavior::kThrow, SideEffectType::kHasNoSideEffect));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "slow_call_count",
        FunctionTemplate::New(
            isolate, FastCApiObject::SlowCallCount, Local<Value>(), signature,
            1, ConstructorBehavior::kThrow, SideEffectType::kHasNoSideEffect));
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "reset_counts",
        FunctionTemplate::New(isolate, FastCApiObject::ResetCounts,
                              Local<Value>(), signature, 1,
                              ConstructorBehavior::kThrow));

    CFunction add_all_32bit_int_5args_enforce_range_c_func =
        CFunctionBuilder()
            .Fn(FastCApiObject::AddAll32BitIntFastCallback_5Args)
            .Arg<3, v8::CTypeInfo::Flags::kEnforceRangeBit>()
            .Arg<5, v8::CTypeInfo::Flags::kEnforceRangeBit>()
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Patch(FastCApiObject::AddAll32BitIntFastCallback_5ArgsPatch)
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
            .Build();
    api_obj_ctor->PrototypeTemplate()->Set(
        isolate, "add_all_5args_enforce_range",
        FunctionTemplate::New(
            isolate, FastCApiObject::AddAll32BitIntSlowCallback, Local<Value>(),
            signature, 1, ConstructorBehavior::kThrow,
            SideEffectType::kHasNoSideEffect,
            &add_all_32bit_int_5args_enforce_range_c_func));
  }
  api_obj_ctor->InstanceTemplate()->SetInternalFieldCount(
      FastCApiObject::kV8WrapperObjectIndex + 1);

  return api_obj_ctor;
}

void CreateLeafInterfaceObject(const FunctionCallbackInfo<Value>& info) {
  if (!info.IsConstructCall()) {
    info.GetIsolate()->ThrowError(
        "LeafInterfaceType helper must be constructed with new.");
  }
}

Local<FunctionTemplate> Shell::CreateLeafInterfaceTypeTemplate(
    Isolate* isolate) {
  Local<FunctionTemplate> leaf_object_ctor =
      FunctionTemplate::New(isolate, CreateLeafInterfaceObject);
  leaf_object_ctor->SetClassName(
      String::NewFromUtf8Literal(isolate, "LeafInterfaceType"));
  return leaf_object_ctor;
}

}  // namespace v8
