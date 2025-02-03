#include "../common.h"
#include "../entry_point.h"
#include "assert.h"
#include "myobject.h"

typedef int32_t FinalizerData;

napi_ref MyObject::constructor;

MyObject::MyObject(double value)
    : value_(value), env_(nullptr), wrapper_(nullptr) {}

MyObject::~MyObject() { napi_delete_reference(env_, wrapper_); }

void MyObject::Destructor(node_api_basic_env env,
                          void* nativeObject,
                          void* /*finalize_hint*/) {
  MyObject* obj = static_cast<MyObject*>(nativeObject);
  delete obj;

  FinalizerData* data;
  NODE_API_BASIC_CALL_RETURN_VOID(
      env, napi_get_instance_data(env, reinterpret_cast<void**>(&data)));
  *data += 1;
}

void MyObject::Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
    { "value", nullptr, nullptr, GetValue, SetValue, 0, napi_default, 0 },
    { "valueReadonly", nullptr, nullptr, GetValue, nullptr, 0, napi_default,
      0 },
    DECLARE_NODE_API_PROPERTY("plusOne", PlusOne),
    DECLARE_NODE_API_PROPERTY("multiply", Multiply),
  };

  napi_value cons;
  NODE_API_CALL_RETURN_VOID(env, napi_define_class(
      env, "MyObject", -1, New, nullptr,
      sizeof(properties) / sizeof(napi_property_descriptor),
      properties, &cons));

  NODE_API_CALL_RETURN_VOID(env,
      napi_create_reference(env, cons, 1, &constructor));

  NODE_API_CALL_RETURN_VOID(env,
      napi_set_named_property(env, exports, "MyObject", cons));
}

napi_value MyObject::New(napi_env env, napi_callback_info info) {
  napi_value new_target;
  NODE_API_CALL(env, napi_get_new_target(env, info, &new_target));
  bool is_constructor = (new_target != nullptr);

  size_t argc = 1;
  napi_value args[1];
  napi_value _this;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, &_this, nullptr));

  if (is_constructor) {
    // Invoked as constructor: `new MyObject(...)`
    double value = 0;

    napi_valuetype valuetype;
    NODE_API_CALL(env, napi_typeof(env, args[0], &valuetype));

    if (valuetype != napi_undefined) {
      NODE_API_CALL(env, napi_get_value_double(env, args[0], &value));
    }

    MyObject* obj = new MyObject(value);

    obj->env_ = env;
    NODE_API_CALL(env,
        napi_wrap(env, _this, obj, MyObject::Destructor,
            nullptr /* finalize_hint */, &obj->wrapper_));

    return _this;
  }

  // Invoked as plain function `MyObject(...)`, turn into construct call.
  argc = 1;
  napi_value argv[1] = {args[0]};

  napi_value cons;
  NODE_API_CALL(env, napi_get_reference_value(env, constructor, &cons));

  napi_value instance;
  NODE_API_CALL(env, napi_new_instance(env, cons, argc, argv, &instance));

  return instance;
}

napi_value MyObject::GetValue(napi_env env, napi_callback_info info) {
  napi_value _this;
  NODE_API_CALL(env,
      napi_get_cb_info(env, info, nullptr, nullptr, &_this, nullptr));

  MyObject* obj;
  NODE_API_CALL(env, napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)));

  napi_value num;
  NODE_API_CALL(env, napi_create_double(env, obj->value_, &num));

  return num;
}

napi_value MyObject::SetValue(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_value _this;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, &_this, nullptr));

  MyObject* obj;
  NODE_API_CALL(env, napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)));

  NODE_API_CALL(env, napi_get_value_double(env, args[0], &obj->value_));

  return nullptr;
}

napi_value MyObject::PlusOne(napi_env env, napi_callback_info info) {
  napi_value _this;
  NODE_API_CALL(env,
      napi_get_cb_info(env, info, nullptr, nullptr, &_this, nullptr));

  MyObject* obj;
  NODE_API_CALL(env, napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)));

  obj->value_ += 1;

  napi_value num;
  NODE_API_CALL(env, napi_create_double(env, obj->value_, &num));

  return num;
}

napi_value MyObject::Multiply(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_value _this;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, &_this, nullptr));

  double multiple = 1;
  if (argc >= 1) {
    NODE_API_CALL(env, napi_get_value_double(env, args[0], &multiple));
  }

  MyObject* obj;
  NODE_API_CALL(env, napi_unwrap(env, _this, reinterpret_cast<void**>(&obj)));

  napi_value cons;
  NODE_API_CALL(env, napi_get_reference_value(env, constructor, &cons));

  const int kArgCount = 1;
  napi_value argv[kArgCount];
  NODE_API_CALL(env, napi_create_double(env, obj->value_ * multiple, argv));

  napi_value instance;
  NODE_API_CALL(env, napi_new_instance(env, cons, kArgCount, argv, &instance));

  return instance;
}

// This finalizer should never be invoked.
void ObjectWrapDanglingReferenceFinalizer(node_api_basic_env env,
                                          void* finalize_data,
                                          void* finalize_hint) {
  assert(0 && "unreachable");
}

napi_ref dangling_ref;
napi_value ObjectWrapDanglingReference(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  NODE_API_CALL(env,
                napi_get_cb_info(env, info, &argc, args, nullptr, nullptr));

  // Create a napi_wrap and remove it immediately, whilst leaving the out-param
  // ref dangling (not deleted).
  NODE_API_CALL(env,
                napi_wrap(env,
                          args[0],
                          nullptr,
                          ObjectWrapDanglingReferenceFinalizer,
                          nullptr,
                          &dangling_ref));
  NODE_API_CALL(env, napi_remove_wrap(env, args[0], nullptr));

  return args[0];
}

napi_value ObjectWrapDanglingReferenceTest(napi_env env,
                                           napi_callback_info info) {
  napi_value out;
  napi_value ret;
  NODE_API_CALL(env, napi_get_reference_value(env, dangling_ref, &out));

  if (out == nullptr) {
    // If the napi_ref has been invalidated, delete it.
    NODE_API_CALL(env, napi_delete_reference(env, dangling_ref));
    NODE_API_CALL(env, napi_get_boolean(env, true, &ret));
  } else {
    // The dangling napi_ref is still valid.
    NODE_API_CALL(env, napi_get_boolean(env, false, &ret));
  }
  return ret;
}

static napi_value GetFinalizerCallCount(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  FinalizerData* data;
  napi_value result;

  NODE_API_CALL(env,
                napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
  NODE_API_CALL(env,
                napi_get_instance_data(env, reinterpret_cast<void**>(&data)));
  NODE_API_CALL(env, napi_create_int32(env, *data, &result));
  return result;
}

static void finalizeData(napi_env env, void* data, void* hint) {
  delete reinterpret_cast<FinalizerData*>(data);
}

EXTERN_C_START
napi_value Init(napi_env env, napi_value exports) {
  FinalizerData* data = new FinalizerData;
  *data = 0;
  NODE_API_CALL(env, napi_set_instance_data(env, data, finalizeData, nullptr));

  MyObject::Init(env, exports);

  napi_property_descriptor descriptors[] = {
      DECLARE_NODE_API_PROPERTY("objectWrapDanglingReference",
                                ObjectWrapDanglingReference),
      DECLARE_NODE_API_PROPERTY("objectWrapDanglingReferenceTest",
                                ObjectWrapDanglingReferenceTest),
      DECLARE_NODE_API_PROPERTY("getFinalizerCallCount", GetFinalizerCallCount),
  };

  NODE_API_CALL(
      env,
      napi_define_properties(env,
                             exports,
                             sizeof(descriptors) / sizeof(*descriptors),
                             descriptors));

  return exports;
}
EXTERN_C_END
