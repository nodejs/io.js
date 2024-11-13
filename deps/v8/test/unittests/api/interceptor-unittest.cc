// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-exception.h"
#include "include/v8-function.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-template.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

using InterceptorTest = TestWithContext;

v8::Intercepted NamedGetter(Local<Name> property,
                            const PropertyCallbackInfo<Value>& info) {
  return v8::Intercepted::kNo;
}

TEST_F(InterceptorTest, FreezeApiObjectWithInterceptor) {
  TryCatch try_catch(isolate());

  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate());
  tmpl->InstanceTemplate()->SetHandler(
      NamedPropertyHandlerConfiguration(NamedGetter));

  Local<Function> ctor = tmpl->GetFunction(context()).ToLocalChecked();
  Local<Object> obj = ctor->NewInstance(context()).ToLocalChecked();
  ASSERT_TRUE(
      obj->SetIntegrityLevel(context(), IntegrityLevel::kFrozen).IsNothing());
  ASSERT_TRUE(try_catch.HasCaught());
}

}  // namespace

namespace internal {
namespace {

class InterceptorLoggingTest : public TestWithNativeContext {
 public:
  InterceptorLoggingTest() = default;

  static const int kTestIndex = 0;

  static v8::Intercepted NamedPropertyGetter(
      Local<v8::Name> name, const v8::PropertyCallbackInfo<Value>& info) {
    LogCallback(info, "named getter");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedPropertySetter(
      Local<v8::Name> name, Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& info) {
    LogCallback(info, "named setter");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedPropertyQuery(
      Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
    LogCallback(info, "named query");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedPropertyDeleter(
      Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    LogCallback(info, "named deleter");
    return v8::Intercepted::kNo;
  }

  static void NamedPropertyEnumerator(
      const v8::PropertyCallbackInfo<Array>& info) {
    LogCallback(info, "named enumerator");
  }

  static v8::Intercepted NamedPropertyDefiner(
      Local<v8::Name> name, const v8::PropertyDescriptor& desc,
      const v8::PropertyCallbackInfo<void>& info) {
    LogCallback(info, "named definer");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted NamedPropertyDescriptor(
      Local<v8::Name> name, const v8::PropertyCallbackInfo<Value>& info) {
    LogCallback(info, "named descriptor");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedPropertyGetter(
      uint32_t index, const v8::PropertyCallbackInfo<Value>& info) {
    LogCallback(info, "indexed getter");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedPropertySetter(
      uint32_t index, Local<v8::Value> value,
      const v8::PropertyCallbackInfo<void>& info) {
    LogCallback(info, "indexed setter");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedPropertyQuery(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
    LogCallback(info, "indexed query");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedPropertyDeleter(
      uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    LogCallback(info, "indexed deleter");
    return v8::Intercepted::kNo;
  }

  static void IndexedPropertyEnumerator(
      const v8::PropertyCallbackInfo<Array>& info) {
    LogCallback(info, "indexed enumerator");
  }

  static v8::Intercepted IndexedPropertyDefiner(
      uint32_t index, const v8::PropertyDescriptor& desc,
      const v8::PropertyCallbackInfo<void>& info) {
    LogCallback(info, "indexed definer");
    return v8::Intercepted::kNo;
  }

  static v8::Intercepted IndexedPropertyDescriptor(
      uint32_t index, const v8::PropertyCallbackInfo<Value>& info) {
    LogCallback(info, "indexed descriptor");
    return v8::Intercepted::kNo;
  }

  template <class T>
  static void LogCallback(const v8::PropertyCallbackInfo<T>& info,
                          const char* callback_name) {
    InterceptorLoggingTest* test = reinterpret_cast<InterceptorLoggingTest*>(
        info.This()->GetAlignedPointerFromInternalField(kTestIndex));
    test->Log(callback_name);
  }

  void Log(const char* callback_name) {
    if (log_is_empty_) {
      log_is_empty_ = false;
    } else {
      log_ << ", ";
    }
    log_ << callback_name;
  }

 protected:
  void SetUp() override {
    // Set up the object that supports full interceptors.
    v8::Local<v8::ObjectTemplate> templ = v8::ObjectTemplate::New(v8_isolate());
    templ->SetInternalFieldCount(1);
    templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
        NamedPropertyGetter, NamedPropertySetter, NamedPropertyQuery,
        NamedPropertyDeleter, NamedPropertyEnumerator, NamedPropertyDefiner,
        NamedPropertyDescriptor));
    templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
        IndexedPropertyGetter, IndexedPropertySetter, IndexedPropertyQuery,
        IndexedPropertyDeleter, IndexedPropertyEnumerator,
        IndexedPropertyDefiner, IndexedPropertyDescriptor));
    v8::Local<v8::Object> instance =
        templ->NewInstance(context()).ToLocalChecked();
    instance->SetAlignedPointerInInternalField(kTestIndex, this);
    SetGlobalProperty("obj", instance);
  }

  std::string Run(const char* script) {
    log_is_empty_ = true;
    log_.str(std::string());
    log_.clear();

    RunJS(script);
    return log_.str();
  }

 private:
  bool log_is_empty_ = false;
  std::stringstream log_;
};

TEST_F(InterceptorLoggingTest, DispatchTest) {
  EXPECT_EQ(Run("for (var p in obj) {}"),
            "indexed enumerator, named enumerator");
  EXPECT_EQ(Run("Object.keys(obj)"), "indexed enumerator, named enumerator");

  EXPECT_EQ(Run("obj.foo"), "named getter");
  EXPECT_EQ(Run("obj[42]"), "indexed getter");

  EXPECT_EQ(Run("obj.foo = null"),
            "named setter, named descriptor, named query");
  EXPECT_EQ(Run("obj[42] = null"),
            "indexed setter, indexed descriptor, indexed query");

  EXPECT_EQ(Run("Object.getOwnPropertyDescriptor(obj, 'foo')"),
            "named descriptor");

  EXPECT_EQ(Run("Object.getOwnPropertyDescriptor(obj, 42)"),
            "indexed descriptor");

  EXPECT_EQ(Run("Object.defineProperty(obj, 'foo', {value: 42})"),
            "named descriptor, named definer, named setter");
  EXPECT_EQ(Run("Object.defineProperty(obj, 'foo', {get(){} })"),
            "named descriptor, named definer");
  EXPECT_EQ(Run("Object.defineProperty(obj, 'foo', {set(value){}})"),
            "named descriptor, named definer");
  EXPECT_EQ(Run("Object.defineProperty(obj, 'foo', {get(){}, set(value){}})"),
            "named descriptor, named definer");

  EXPECT_EQ(Run("Object.defineProperty(obj, 42, {value: 'foo'})"),
            "indexed descriptor, "
            // then attempt definer first and fallback to setter.
            "indexed definer, indexed setter");

  EXPECT_EQ(Run("Object.prototype.propertyIsEnumerable.call(obj, 'a')"),
            "named query");
  EXPECT_EQ(Run("Object.prototype.propertyIsEnumerable.call(obj, 42)"),
            "indexed query");

  EXPECT_EQ(Run("Object.prototype.hasOwnProperty.call(obj, 'a')"),
            "named query");
  // TODO(cbruni): Fix once hasOnwProperty is fixed (https://crbug.com/872628)
  EXPECT_EQ(Run("Object.prototype.hasOwnProperty.call(obj, '42')"), "");
}
}  // namespace
}  // namespace internal
}  // namespace v8
