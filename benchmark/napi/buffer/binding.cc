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

static void Finalize(node_api_basic_env env, void* data, void* hint) {
  delete[] static_cast<uint8_t*>(data);
}

static napi_value CreateExternalBuffer(napi_env env, napi_callback_info info) {
  napi_value argv[2], undefined, start, end;
  size_t argc = 2;
  int32_t n = 0;
  napi_valuetype val_type = napi_undefined;

  // Validate params and retrieve start and end function.
  NODE_API_CALL(napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
  ABORT_IF_FALSE(argc == 2);
  NODE_API_CALL(napi_typeof(env, argv[0], &val_type));
  ABORT_IF_FALSE(val_type == napi_object);
  NODE_API_CALL(napi_typeof(env, argv[1], &val_type));
  ABORT_IF_FALSE(val_type == napi_number);
  NODE_API_CALL(napi_get_named_property(env, argv[0], "start", &start));
  NODE_API_CALL(napi_typeof(env, start, &val_type));
  ABORT_IF_FALSE(val_type == napi_function);
  NODE_API_CALL(napi_get_named_property(env, argv[0], "end", &end));
  NODE_API_CALL(napi_typeof(env, end, &val_type));
  ABORT_IF_FALSE(val_type == napi_function);
  NODE_API_CALL(napi_get_value_int32(env, argv[1], &n));

  NODE_API_CALL(napi_get_undefined(env, &undefined));

  constexpr uint32_t kBufferLen = 32;

  // Start the benchmark.
  napi_call_function(env, argv[0], start, 0, nullptr, nullptr);

  for (int32_t idx = 0; idx < n; idx++) {
    napi_handle_scope scope;
    uint8_t* buffer = new uint8_t[kBufferLen];
    napi_value jsbuffer;
    NODE_API_CALL(napi_open_handle_scope(env, &scope));
    NODE_API_CALL(napi_create_external_buffer(
        env, kBufferLen, buffer, Finalize, nullptr, &jsbuffer));
    NODE_API_CALL(napi_close_handle_scope(env, scope));
  }

  // Conclude the benchmark.
  napi_value end_argv[] = {argv[1]};
  NODE_API_CALL(napi_call_function(env, argv[0], end, 1, end_argv, nullptr));

  return undefined;
}

NAPI_MODULE_INIT() {
  napi_property_descriptor props[] = {
      {"createExternalBuffer",
       nullptr,
       CreateExternalBuffer,
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
