#include <js_native_api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common.h"
#include "../entry_point.h"

typedef struct {
  int32_t finalize_count;
  napi_ref js_func;
} FinalizerData;

static void finalizerOnlyCallback(node_api_basic_env env,
                                  void* finalize_data,
                                  void* finalize_hint) {
  FinalizerData* data = (FinalizerData*)finalize_data;
  int32_t count = ++data->finalize_count;

  // It is safe to access instance data
  NODE_API_BASIC_CALL_RETURN_VOID(env,
                                  napi_get_instance_data(env, (void**)&data));
  NODE_API_BASIC_ASSERT_RETURN_VOID(count = data->finalize_count,
                                    "Expected to be the same FinalizerData");
}

static void finalizerCallingJSCallback(napi_env env,
                                       void* finalize_data,
                                       void* finalize_hint) {
  napi_value js_func, undefined;
  FinalizerData* data = (FinalizerData*)finalize_data;
  NODE_API_CALL_RETURN_VOID(
      env, napi_get_reference_value(env, data->js_func, &js_func));
  NODE_API_CALL_RETURN_VOID(env, napi_get_undefined(env, &undefined));
  NODE_API_CALL_RETURN_VOID(
      env, napi_call_function(env, undefined, js_func, 0, NULL, NULL));
  NODE_API_CALL_RETURN_VOID(env, napi_delete_reference(env, data->js_func));
  data->js_func = NULL;
  ++data->finalize_count;
}

// Schedule async finalizer to run JavaScript-touching code.
static void finalizerWithJSCallback(node_api_basic_env env,
                                    void* finalize_data,
                                    void* finalize_hint) {
  NODE_API_BASIC_CALL_RETURN_VOID(
      env,
      node_api_post_finalizer(
          env, finalizerCallingJSCallback, finalize_data, finalize_hint));
}

static void finalizerWithFailedJSCallback(node_api_basic_env basic_env,
                                          void* finalize_data,
                                          void* finalize_hint) {
  // Intentionally cast to a napi_env to test the fatal failure.
  napi_env env = (napi_env)basic_env;
  napi_value obj;
  FinalizerData* data = (FinalizerData*)finalize_data;
  ++data->finalize_count;
  NODE_API_CALL_RETURN_VOID(env, napi_create_object(env, &obj));
}

static napi_value addFinalizer(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {0};
  FinalizerData* data;

  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  NODE_API_CALL(env, napi_get_instance_data(env, (void**)&data));
  NODE_API_CALL(env,
                napi_add_finalizer(
                    env, argv[0], data, finalizerOnlyCallback, NULL, NULL));
  return NULL;
}

// This finalizer is going to call JavaScript from finalizer and succeed.
static napi_value addFinalizerWithJS(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2] = {0};
  napi_valuetype arg_type;
  FinalizerData* data;

  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  NODE_API_CALL(env, napi_get_instance_data(env, (void**)&data));
  NODE_API_CALL(env, napi_typeof(env, argv[1], &arg_type));
  NODE_API_ASSERT(
      env, arg_type == napi_function, "Expected function as the second arg");
  NODE_API_CALL(env, napi_create_reference(env, argv[1], 1, &data->js_func));
  NODE_API_CALL(env,
                napi_add_finalizer(
                    env, argv[0], data, finalizerWithJSCallback, NULL, NULL));
  return NULL;
}

// This finalizer is going to call JavaScript from finalizer and fail.
static napi_value addFinalizerFailOnJS(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1] = {0};
  FinalizerData* data;

  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  NODE_API_CALL(env, napi_get_instance_data(env, (void**)&data));
  NODE_API_CALL(
      env,
      napi_add_finalizer(
          env, argv[0], data, finalizerWithFailedJSCallback, NULL, NULL));
  return NULL;
}

static napi_value getFinalizerCallCount(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  FinalizerData* data;
  napi_value result;

  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  NODE_API_CALL(env, napi_get_instance_data(env, (void**)&data));
  NODE_API_CALL(env, napi_create_int32(env, data->finalize_count, &result));
  return result;
}

static void finalizeData(napi_env env, void* data, void* hint) {
  free(data);
}

EXTERN_C_START
napi_value Init(napi_env env, napi_value exports) {
  FinalizerData* data = (FinalizerData*)malloc(sizeof(FinalizerData));
  NODE_API_ASSERT(env, data != NULL, "Failed to allocate memory");
  memset(data, 0, sizeof(FinalizerData));
  NODE_API_CALL(env, napi_set_instance_data(env, data, finalizeData, NULL));
  napi_property_descriptor descriptors[] = {
      DECLARE_NODE_API_PROPERTY("addFinalizer", addFinalizer),
      DECLARE_NODE_API_PROPERTY("addFinalizerWithJS", addFinalizerWithJS),
      DECLARE_NODE_API_PROPERTY("addFinalizerFailOnJS", addFinalizerFailOnJS),
      DECLARE_NODE_API_PROPERTY("getFinalizerCallCount",
                                getFinalizerCallCount)};

  NODE_API_CALL(
      env,
      napi_define_properties(env,
                             exports,
                             sizeof(descriptors) / sizeof(*descriptors),
                             descriptors));

  return exports;
}
EXTERN_C_END
