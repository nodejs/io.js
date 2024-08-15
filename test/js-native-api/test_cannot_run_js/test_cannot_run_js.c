#include <js_native_api.h>
#include "../common.h"
#include "../entry_point.h"
#include "stdlib.h"

static void Finalize(napi_env env, void* data, void* hint) {
  napi_value global, set_timeout;
  napi_ref* ref = data;

  NODE_API_BASIC_ASSERT_RETURN_VOID(
      napi_delete_reference(env, *ref) == napi_ok,
      "deleting reference in finalizer should succeed");
  NODE_API_BASIC_ASSERT_RETURN_VOID(
      napi_get_global(env, &global) == napi_ok,
      "getting global reference in finalizer should succeed");
  napi_status result =
      napi_get_named_property(env, global, "setTimeout", &set_timeout);

  // The finalizer could be invoked either from check callbacks (as native
  // immediates) if the event loop is still running (where napi_ok is returned)
  // or during environment shutdown (where napi_cannot_run_js or
  // napi_pending_exception is returned). This is not deterministic from
  // the point of view of the addon.

#ifdef NAPI_EXPERIMENTAL
  NODE_API_BASIC_ASSERT_RETURN_VOID(
      result == napi_cannot_run_js || result == napi_ok,
      "getting named property from global in finalizer should succeed "
      "or return napi_cannot_run_js");
#else
  NODE_API_BASIC_ASSERT_RETURN_VOID(
      result == napi_pending_exception || result == napi_ok,
      "getting named property from global in finalizer should succeed "
      "or return napi_pending_exception");
#endif  // NAPI_EXPERIMENTAL
  free(ref);
}

static void BasicFinalize(node_api_basic_env env, void* data, void* hint) {
#ifdef NAPI_EXPERIMENTAL
  NODE_API_BASIC_CALL_RETURN_VOID(
      env, node_api_post_finalizer(env, Finalize, data, hint));
#else
  Finalize(env, data, hint);
#endif
}

static napi_value CreateRef(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value cb;
  napi_valuetype value_type;
  napi_ref* ref = malloc(sizeof(*ref));
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, &cb, NULL, NULL));
  NODE_API_ASSERT(env, argc == 1, "Function takes only one argument");
  NODE_API_CALL(env, napi_typeof(env, cb, &value_type));
  NODE_API_ASSERT(
      env, value_type == napi_function, "argument must be function");
  NODE_API_CALL(env,
                napi_add_finalizer(env, cb, ref, BasicFinalize, NULL, ref));
  return cb;
}

EXTERN_C_START
napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
      DECLARE_NODE_API_PROPERTY("createRef", CreateRef),
  };

  NODE_API_CALL(
      env,
      napi_define_properties(
          env, exports, sizeof(properties) / sizeof(*properties), properties));

  return exports;
}
EXTERN_C_END
