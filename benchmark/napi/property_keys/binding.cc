#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>

#define NODE_API_CALL(call)                                                    \
  do {                                                                         \
    napi_status status = call;                                                 \
    if (status != napi_ok) {                                                   \
      fprintf(stderr, #call " failed: %d\n", status);                          \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define ABORT_IF_FALSE(condition)                                              \
  if (!(condition)) {                                                          \
    fprintf(stderr, #condition " failed\n");                                   \
    abort();                                                                   \
  }

static napi_value Runner(napi_env env,
                         napi_callback_info info,
                         napi_value property_key) {
  napi_value argv[2], undefined, js_array_length, start, end;
  size_t argc = 2;
  napi_valuetype val_type = napi_undefined;
  bool is_array = false;
  uint32_t array_length = 0;
  napi_value* native_array;

  // Validate params and retrieve start and end function.
  NODE_API_CALL(napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
  ABORT_IF_FALSE(argc == 2);
  NODE_API_CALL(napi_typeof(env, argv[0], &val_type));
  ABORT_IF_FALSE(val_type == napi_object);
  NODE_API_CALL(napi_is_array(env, argv[1], &is_array));
  ABORT_IF_FALSE(is_array);
  NODE_API_CALL(napi_get_array_length(env, argv[1], &array_length));
  NODE_API_CALL(napi_get_named_property(env, argv[0], "start", &start));
  NODE_API_CALL(napi_typeof(env, start, &val_type));
  ABORT_IF_FALSE(val_type == napi_function);
  NODE_API_CALL(napi_get_named_property(env, argv[0], "end", &end));
  NODE_API_CALL(napi_typeof(env, end, &val_type));
  ABORT_IF_FALSE(val_type == napi_function);

  NODE_API_CALL(napi_get_undefined(env, &undefined));
  NODE_API_CALL(napi_create_uint32(env, array_length, &js_array_length));

  // Copy objects into a native array.
  native_array =
      static_cast<napi_value*>(malloc(array_length * sizeof(napi_value)));
  for (uint32_t idx = 0; idx < array_length; idx++) {
    NODE_API_CALL(napi_get_element(env, argv[1], idx, &native_array[idx]));
  }

  // Start the benchmark.
  napi_call_function(env, argv[0], start, 0, nullptr, nullptr);

  for (uint32_t idx = 0; idx < array_length; idx++) {
    NODE_API_CALL(
        napi_set_property(env, native_array[idx], property_key, undefined));
  }

  // Conclude the benchmark.
  NODE_API_CALL(
      napi_call_function(env, argv[0], end, 1, &js_array_length, nullptr));

  free(native_array);

  return undefined;
}

static napi_value RunPropertyKey(napi_env env, napi_callback_info info) {
  napi_value property_key;
  NODE_API_CALL(node_api_create_property_key_utf16(
      env, u"prop", NAPI_AUTO_LENGTH, &property_key));
  return Runner(env, info, property_key);
}

static napi_value RunNormalString(napi_env env, napi_callback_info info) {
  napi_value property_key;
  NODE_API_CALL(
      napi_create_string_utf16(env, u"prop", NAPI_AUTO_LENGTH, &property_key));
  return Runner(env, info, property_key);
}

NAPI_MODULE_INIT() {
  napi_property_descriptor props[] = {
      {"RunPropertyKey",
       nullptr,
       RunPropertyKey,
       nullptr,
       nullptr,
       nullptr,
       static_cast<napi_property_attributes>(napi_writable | napi_configurable |
                                             napi_enumerable),
       nullptr},
      {"RunNormalString",
       nullptr,
       RunNormalString,
       nullptr,
       nullptr,
       nullptr,
       static_cast<napi_property_attributes>(napi_writable | napi_configurable |
                                             napi_enumerable),
       nullptr},
  };

  NODE_API_CALL(napi_define_properties(
      env, exports, sizeof(props) / sizeof(*props), props));
  return exports;
}
