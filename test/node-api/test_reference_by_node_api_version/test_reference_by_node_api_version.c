#include <node_api.h>
#include "../../js-native-api/common.h"
#include "stdlib.h"

static uint32_t finalizeCount = 0;

static void FreeData(node_api_basic_env env, void* data, void* hint) {
  NODE_API_BASIC_ASSERT_RETURN_VOID(data != NULL, "Expects non-NULL data.");
  free(data);
}

static void Finalize(node_api_basic_env env, void* data, void* hint) {
  ++finalizeCount;
}

static napi_status GetArgValue(napi_env env,
                               napi_callback_info info,
                               napi_value* argValue) {
  size_t argc = 1;
  NODE_API_CHECK_STATUS(
      napi_get_cb_info(env, info, &argc, argValue, NULL, NULL));

  NODE_API_ASSERT_STATUS(env, argc == 1, "Expects one arg.");
  return napi_ok;
}

static napi_status GetArgValueAsIndex(napi_env env,
                                      napi_callback_info info,
                                      uint32_t* index) {
  napi_value argValue;
  NODE_API_CHECK_STATUS(GetArgValue(env, info, &argValue));

  napi_valuetype valueType;
  NODE_API_CHECK_STATUS(napi_typeof(env, argValue, &valueType));
  NODE_API_ASSERT_STATUS(
      env, valueType == napi_number, "Argument must be a number.");

  return napi_get_value_uint32(env, argValue, index);
}

static napi_status GetRef(napi_env env,
                          napi_callback_info info,
                          napi_ref* ref) {
  uint32_t index;
  NODE_API_CHECK_STATUS(GetArgValueAsIndex(env, info, &index));

  napi_ref* refValues;
  NODE_API_CHECK_STATUS(napi_get_instance_data(env, (void**)&refValues));
  NODE_API_ASSERT_STATUS(env, refValues != NULL, "Cannot get instance data.");

  *ref = refValues[index];
  return napi_ok;
}

static napi_value ToUInt32Value(napi_env env, uint32_t value) {
  napi_value result;
  NODE_API_CALL(env, napi_create_uint32(env, value, &result));
  return result;
}

static napi_status InitRefArray(napi_env env) {
  // valueRefs array has one entry per napi_valuetype
  napi_ref* valueRefs = malloc(sizeof(napi_ref) * ((int)napi_bigint + 1));
  return napi_set_instance_data(env, valueRefs, (napi_finalize)&FreeData, NULL);
}

static napi_value CreateExternal(napi_env env, napi_callback_info info) {
  napi_value result;
  int* data = (int*)malloc(sizeof(int));
  *data = 42;
  NODE_API_CALL(env, napi_create_external(env, data, &FreeData, NULL, &result));
  return result;
}

static napi_value CreateRef(napi_env env, napi_callback_info info) {
  napi_value argValue;
  NODE_API_CALL(env, GetArgValue(env, info, &argValue));

  napi_valuetype valueType;
  NODE_API_CALL(env, napi_typeof(env, argValue, &valueType));
  uint32_t index = (uint32_t)valueType;

  napi_ref* valueRefs;
  NODE_API_CALL(env, napi_get_instance_data(env, (void**)&valueRefs));
  NODE_API_CALL(env,
                napi_create_reference(env, argValue, 1, valueRefs + index));

  return ToUInt32Value(env, index);
}

static napi_value GetRefValue(napi_env env, napi_callback_info info) {
  napi_ref refValue;
  NODE_API_CALL(env, GetRef(env, info, &refValue));
  napi_value value;
  NODE_API_CALL(env, napi_get_reference_value(env, refValue, &value));
  return value;
}

static napi_value Ref(napi_env env, napi_callback_info info) {
  napi_ref refValue;
  NODE_API_CALL(env, GetRef(env, info, &refValue));
  uint32_t refCount;
  NODE_API_CALL(env, napi_reference_ref(env, refValue, &refCount));
  return ToUInt32Value(env, refCount);
}

static napi_value Unref(napi_env env, napi_callback_info info) {
  napi_ref refValue;
  NODE_API_CALL(env, GetRef(env, info, &refValue));
  uint32_t refCount;
  NODE_API_CALL(env, napi_reference_unref(env, refValue, &refCount));
  return ToUInt32Value(env, refCount);
}

static napi_value DeleteRef(napi_env env, napi_callback_info info) {
  napi_ref refValue;
  NODE_API_CALL(env, GetRef(env, info, &refValue));
  NODE_API_CALL(env, napi_delete_reference(env, refValue));
  return NULL;
}

static napi_value AddFinalizer(napi_env env, napi_callback_info info) {
  napi_value obj;
  NODE_API_CALL(env, GetArgValue(env, info, &obj));

  napi_valuetype valueType;
  NODE_API_CALL(env, napi_typeof(env, obj, &valueType));
  NODE_API_ASSERT(env, valueType == napi_object, "Argument must be an object.");

  NODE_API_CALL(env, napi_add_finalizer(env, obj, NULL, &Finalize, NULL, NULL));
  return NULL;
}

static napi_value GetFinalizeCount(napi_env env, napi_callback_info info) {
  return ToUInt32Value(env, finalizeCount);
}

static napi_value InitFinalizeCount(napi_env env, napi_callback_info info) {
  finalizeCount = 0;
  return NULL;
}

EXTERN_C_START

NAPI_MODULE_INIT() {
  finalizeCount = 0;
  NODE_API_CALL(env, InitRefArray(env));

  napi_property_descriptor properties[] = {
      DECLARE_NODE_API_PROPERTY("createExternal", CreateExternal),
      DECLARE_NODE_API_PROPERTY("createRef", CreateRef),
      DECLARE_NODE_API_PROPERTY("getRefValue", GetRefValue),
      DECLARE_NODE_API_PROPERTY("ref", Ref),
      DECLARE_NODE_API_PROPERTY("unref", Unref),
      DECLARE_NODE_API_PROPERTY("deleteRef", DeleteRef),
      DECLARE_NODE_API_PROPERTY("addFinalizer", AddFinalizer),
      DECLARE_NODE_API_PROPERTY("getFinalizeCount", GetFinalizeCount),
      DECLARE_NODE_API_PROPERTY("initFinalizeCount", InitFinalizeCount),
  };

  NODE_API_CALL(
      env,
      napi_define_properties(
          env, exports, sizeof(properties) / sizeof(*properties), properties));

  return exports;
}

EXTERN_C_END
