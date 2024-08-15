#include <node_api.h>
#include <stdlib.h>

#define NAPI_CALL(env, call)                          \
  do {                                                \
    napi_status status = (call);                      \
    if (status != napi_ok) {                          \
      napi_throw_error((env), NULL, #call " failed"); \
      return NULL;                                    \
    }                                                 \
  } while (0)

static napi_value
GetCount(napi_env env, napi_callback_info info) {
  napi_value result;
  size_t* count;

  NAPI_CALL(env, napi_get_instance_data(env, (void**)&count));
  NAPI_CALL(env, napi_create_uint32(env, *count, &result));

  return result;
}

static napi_value
SetCount(napi_env env, napi_callback_info info) {
  size_t* count;

  NAPI_CALL(env, napi_get_instance_data(env, (void**)&count));

  // Set the count to zero irrespective of what is passed into the setter.
  *count = 0;

  return NULL;
}

static void IncrementCounter(node_api_basic_env env, void* data, void* hint) {
  size_t* count = data;
  (*count) = (*count) + 1;
}

static napi_value
NewWeak(napi_env env, napi_callback_info info) {
  napi_value result;
  void* instance_data;

  NAPI_CALL(env, napi_create_object(env, &result));
  NAPI_CALL(env, napi_get_instance_data(env, &instance_data));
  NAPI_CALL(env, napi_add_finalizer(env,
                                    result,
                                    instance_data,
                                    IncrementCounter,
                                    NULL,
                                    NULL));

  return result;
}

static void
FreeCount(napi_env env, void* data, void* hint) {
  free(data);
}

/* napi_value */
NAPI_MODULE_INIT(/* napi_env env, napi_value exports */) {
  napi_property_descriptor props[] = {
    { "count", NULL, NULL, GetCount, SetCount, NULL, napi_enumerable, NULL },
    { "newWeak", NULL, NewWeak, NULL, NULL, NULL, napi_enumerable, NULL }
  };

  size_t* count = malloc(sizeof(*count));
  *count = 0;

  NAPI_CALL(env, napi_define_properties(env,
                                        exports,
                                        sizeof(props) / sizeof(*props),
                                        props));
  NAPI_CALL(env, napi_set_instance_data(env, count, FreeCount, NULL));

  return exports;
}
