// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_CCTEST_TEST_API_H_
#define V8_TEST_CCTEST_TEST_API_H_

#include "src/api/api-inl.h"
#include "src/execution/isolate.h"
#include "src/execution/vm-state.h"
#include "test/cctest/cctest.h"

template <typename T>
static i::Tagged<i::Object> Get(v8::ReturnValue<T> return_value) {
  return *v8::Utils::OpenDirectHandle(*return_value.Get());
}

template <typename T>
static void CheckReturnValueImpl(v8::Isolate* v8_isolate,
                                 v8::ReturnValue<T> return_value,
                                 i::Address callback) {
  using namespace v8::internal;
  constexpr bool is_int = std::is_same_v<T, v8::Integer>;
  constexpr bool is_bool =
      std::is_same_v<T, v8::Boolean> || std::is_same_v<T, void>;
  constexpr bool is_array = std::is_same_v<T, v8::Array>;
  constexpr bool is_value = std::is_same_v<T, v8::Value>;
  static_assert(is_int || is_bool || is_array || is_value);

  CHECK_EQ(CcTest::isolate(), v8_isolate);
  CHECK_EQ(v8_isolate, return_value.GetIsolate());
  Isolate* isolate = reinterpret_cast<Isolate*>(v8_isolate);

  Tagged<Object> default_value;
  if constexpr (is_int) {
    default_value = Smi::zero();
  } else if constexpr (is_bool) {
    default_value = ReadOnlyRoots(isolate).true_value();
  } else if constexpr (is_array) {
    // TODO(ishell): enumerator callback's return value is initialized with
    // undefined even though it's supposed to return v8::Array.
    default_value = ReadOnlyRoots(isolate).undefined_value();
  } else if constexpr (is_value) {
    default_value = ReadOnlyRoots(isolate).undefined_value();
  }

  auto CheckValueMap = [=](v8::ReturnValue<T>& return_value) {
    Tagged<Object> obj = Get(return_value);
    if constexpr (is_int) {
      return IsNumber(obj);
    } else if constexpr (is_bool) {
      return IsBoolean(obj);
    } else if constexpr (is_array) {
      // TODO(ishell): enumerator callback's return value is initialized with
      // undefined even though it's supposed to return v8::Array.
      return IsUndefined(obj) || IsJSObject(obj);
    } else if constexpr (is_value) {
      // TODO(ishell): just `return IsJSAny(obj);` when available.
      // Similar to v8::Data::IsValue().
      if (IsSmi(obj)) return true;
      Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
      if (i::IsSymbol(heap_object)) {
        return !Cast<Symbol>(heap_object)->is_private();
      }
      return IsPrimitiveHeapObject(heap_object) || IsJSReceiver(heap_object);
    }
    UNREACHABLE();
  };

  // Default state.
  CHECK_EQ(default_value, Get(return_value));
  CHECK(CheckValueMap(return_value));

  if constexpr (is_bool || is_value) {
    return_value.Set(true);
    CHECK_EQ(Get(return_value), i::ReadOnlyRoots(isolate).true_value());
    CHECK(CheckValueMap(return_value));
  }

  if constexpr (is_int || is_value) {
    return_value.Set(42);
    CHECK_EQ(Get(return_value), i::Smi::FromInt(42));
    CHECK(CheckValueMap(return_value));

    return_value.Set(-153);
    CHECK_EQ(Get(return_value), i::Smi::FromInt(-153));
    CHECK(CheckValueMap(return_value));
  }

  if constexpr (is_value) {
    return_value.SetNull();
    CHECK_EQ(Get(return_value), i::ReadOnlyRoots(isolate).null_value());
    CHECK(CheckValueMap(return_value));
  }

  if constexpr (is_value) {
    // Reset to default state.
    return_value.Set(v8::Local<v8::Object>());
    CHECK_EQ(default_value, Get(return_value));
    CHECK(CheckValueMap(return_value));
  }

  // If CPU profiler is active check that when API callback is invoked
  // VMState is set to EXTERNAL.
  if (isolate->is_profiling()) {
    CHECK_EQ(v8::EXTERNAL, isolate->current_vm_state());
    CHECK(isolate->external_callback_scope());
    CHECK_EQ(callback, isolate->external_callback_scope()->callback());
  }
}

template <typename TCallbackInfo>
static void CheckReturnValue(const TCallbackInfo& info, i::Address callback) {
  CheckReturnValueImpl(info.GetIsolate(), info.GetReturnValue(), callback);
}

template <typename T>
static void CheckInternalFieldsAreZero(v8::Local<T> value) {
  CHECK_EQ(T::kInternalFieldCount, value->InternalFieldCount());
  for (int i = 0; i < value->InternalFieldCount(); i++) {
    v8::Local<v8::Value> field =
        value->GetInternalField(i).template As<v8::Value>();
    CHECK_EQ(
        0,
        field->Int32Value(CcTest::isolate()->GetCurrentContext()).FromJust());
  }
}

template <typename T>
struct ConvertJSValue {
  static v8::Maybe<T> Get(v8::Local<v8::Value> value,
                          v8::Local<v8::Context> context);
};

template <>
struct ConvertJSValue<int32_t> {
  static v8::Maybe<int32_t> Get(v8::Local<v8::Value> value,
                                v8::Local<v8::Context> context) {
    return value->Int32Value(context);
  }
};

template <>
struct ConvertJSValue<uint32_t> {
  static v8::Maybe<uint32_t> Get(v8::Local<v8::Value> value,
                                 v8::Local<v8::Context> context) {
    return value->Uint32Value(context);
  }
};

template <>
struct ConvertJSValue<std::nullptr_t> {
  static v8::Maybe<std::nullptr_t> Get(v8::Local<v8::Value> value,
                                       v8::Local<v8::Context> context) {
    return value->IsNull() ? v8::Just(nullptr) : v8::Nothing<std::nullptr_t>();
  }
};

// NaNs and +/-Infinity should be 0, otherwise (modulo 2^64) - 2^63.
// Step 8 - 12 of https://heycam.github.io/webidl/#abstract-opdef-converttoint
// The int64_t and uint64_t implementations below are copied from Blink:
// https://source.chromium.org/chromium/chromium/src/+/master:third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h;l=249?q=doubletointeger&sq=&ss=chromium%2Fchromium%2Fsrc
template <>
struct ConvertJSValue<int64_t> {
  static v8::Maybe<int64_t> Get(v8::Local<v8::Value> value,
                                v8::Local<v8::Context> context) {
    v8::Maybe<double> double_value = value->NumberValue(context);
    if (!double_value.IsJust()) {
      return v8::Nothing<int64_t>();
    }
    double result = double_value.ToChecked();
    if (std::isinf(result) || std::isnan(result)) {
      return v8::Just(int64_t(0));
    }
    result = trunc(result);

    constexpr uint64_t kMaxULL = std::numeric_limits<uint64_t>::max();

    // -2^{64} < fmod_value < 2^{64}.
    double fmod_value = fmod(result, static_cast<double>(kMaxULL));
    if (fmod_value >= 0) {
      if (fmod_value < pow(2, 63)) {
        // 0 <= fmod_value < 2^{63}.
        // 0 <= value < 2^{63}. This cast causes no loss.
        return v8::Just(static_cast<int64_t>(fmod_value));
      } else {
        // 2^{63} <= fmod_value < 2^{64}.
        // 2^{63} <= value < 2^{64}. This cast causes no loss.
        return v8::Just(static_cast<int64_t>(fmod_value - pow(2, 64)));
      }
    }
    // -2^{64} < fmod_value < 0.
    // 0 < fmod_value_uint64 < 2^{64}. This cast causes no loss.
    uint64_t fmod_value_uint64 = static_cast<uint64_t>(-fmod_value);
    // -1 < (kMaxULL - fmod_value_uint64) < 2^{64} - 1.
    // 0 < value < 2^{64}.
    return v8::Just(static_cast<int64_t>(kMaxULL - fmod_value_uint64 + 1));
  }
};

template <>
struct ConvertJSValue<uint64_t> {
  static v8::Maybe<uint64_t> Get(v8::Local<v8::Value> value,
                                 v8::Local<v8::Context> context) {
    v8::Maybe<double> double_value = value->NumberValue(context);
    if (!double_value.IsJust()) {
      return v8::Nothing<uint64_t>();
    }
    double result = double_value.ToChecked();
    if (std::isinf(result) || std::isnan(result)) {
      return v8::Just(uint64_t(0));
    }
    result = trunc(result);

    constexpr uint64_t kMaxULL = std::numeric_limits<uint64_t>::max();

    // -2^{64} < fmod_value < 2^{64}.
    double fmod_value = fmod(result, static_cast<double>(kMaxULL));
    if (fmod_value >= 0) {
      return v8::Just(static_cast<uint64_t>(fmod_value));
    }
    // -2^{64} < fmod_value < 0.
    // 0 < fmod_value_uint64 < 2^{64}. This cast causes no loss.
    uint64_t fmod_value_uint64 = static_cast<uint64_t>(-fmod_value);
    // -1 < (kMaxULL - fmod_value_uint64) < 2^{64} - 1.
    // 0 < value < 2^{64}.
    return v8::Just(static_cast<uint64_t>(kMaxULL - fmod_value_uint64 + 1));
  }
};

template <>
struct ConvertJSValue<v8::BigInt> {
  static v8::Maybe<v8::Local<v8::BigInt>> Get(v8::Local<v8::Value> value,
                                              v8::Local<v8::Context> context) {
    if (value->IsBigInt()) {
      return v8::Just(value.As<v8::BigInt>());
    }
    return v8::Nothing<v8::Local<v8::BigInt>>();
  }
};

template <>
struct ConvertJSValue<float> {
  static v8::Maybe<float> Get(v8::Local<v8::Value> value,
                              v8::Local<v8::Context> context) {
    v8::Maybe<double> val = value->NumberValue(context);
    if (val.IsNothing()) return v8::Nothing<float>();
    return v8::Just(static_cast<float>(val.ToChecked()));
  }
};

template <>
struct ConvertJSValue<double> {
  static v8::Maybe<double> Get(v8::Local<v8::Value> value,
                               v8::Local<v8::Context> context) {
    return value->NumberValue(context);
  }
};

template <>
struct ConvertJSValue<bool> {
  static v8::Maybe<bool> Get(v8::Local<v8::Value> value,
                             v8::Local<v8::Context> context) {
    return v8::Just<bool>(value->BooleanValue(CcTest::isolate()));
  }
};

#endif  // V8_TEST_CCTEST_TEST_API_H_
