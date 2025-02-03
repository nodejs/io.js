#ifndef TEST_JS_NATIVE_API_6_OBJECT_WRAP_MYOBJECT_H_
#define TEST_JS_NATIVE_API_6_OBJECT_WRAP_MYOBJECT_H_

#include <js_native_api.h>

class MyObject {
 public:
  static void Init(napi_env env, napi_value exports);
  static void Destructor(node_api_basic_env env,
                         void* nativeObject,
                         void* finalize_hint);

 private:
  explicit MyObject(double value_ = 0);
  ~MyObject();

  static napi_value New(napi_env env, napi_callback_info info);
  static napi_value GetValue(napi_env env, napi_callback_info info);
  static napi_value SetValue(napi_env env, napi_callback_info info);
  static napi_value PlusOne(napi_env env, napi_callback_info info);
  static napi_value Multiply(napi_env env, napi_callback_info info);
  static napi_ref constructor;
  double value_;
  napi_env env_;
  napi_ref wrapper_;
};

#endif  // TEST_JS_NATIVE_API_6_OBJECT_WRAP_MYOBJECT_H_
