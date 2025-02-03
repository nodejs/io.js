#include <js_native_api.h>
#include "../common.h"
#include "../entry_point.h"

static napi_value TestCreateFunctionParameters(napi_env env,
                                               napi_callback_info info) {
  napi_status status;
  napi_value result, return_value;

  NODE_API_CALL(env, napi_create_object(env, &return_value));

  status = napi_create_function(NULL,
                                "TrackedFunction",
                                NAPI_AUTO_LENGTH,
                                TestCreateFunctionParameters,
                                NULL,
                                &result);

  add_returned_status(env,
                      "envIsNull",
                      return_value,
                      "Invalid argument",
                      napi_invalid_arg,
                      status);

  napi_create_function(env,
                       NULL,
                       NAPI_AUTO_LENGTH,
                       TestCreateFunctionParameters,
                       NULL,
                       &result);

  add_last_status(env, "nameIsNull", return_value);

  napi_create_function(env,
                       "TrackedFunction",
                       NAPI_AUTO_LENGTH,
                       NULL,
                       NULL,
                       &result);

  add_last_status(env, "cbIsNull", return_value);

  napi_create_function(env,
                       "TrackedFunction",
                       NAPI_AUTO_LENGTH,
                       TestCreateFunctionParameters,
                       NULL,
                       NULL);

  add_last_status(env, "resultIsNull", return_value);

  return return_value;
}

static napi_value TestCallFunction(napi_env env, napi_callback_info info) {
  size_t argc = 10;
  napi_value args[10];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, NULL, NULL));

  NODE_API_ASSERT(env, argc > 0, "Wrong number of arguments");

  napi_valuetype valuetype0;
  NODE_API_CALL(env, napi_typeof(env, args[0], &valuetype0));

  NODE_API_ASSERT(env, valuetype0 == napi_function,
      "Wrong type of arguments. Expects a function as first argument.");

  napi_value* argv = args + 1;
  argc = argc - 1;

  napi_value global;
  NODE_API_CALL(env, napi_get_global(env, &global));

  napi_value result;
  NODE_API_CALL(env, napi_call_function(env, global, args[0], argc, argv, &result));

  return result;
}

static napi_value TestFunctionName(napi_env env, napi_callback_info info) {
  return NULL;
}

static void finalize_function(napi_env env, void* data, void* hint) {
  napi_ref ref = data;

  // Retrieve the JavaScript undefined value.
  napi_value undefined;
  NODE_API_CALL_RETURN_VOID(env, napi_get_undefined(env, &undefined));

  // Retrieve the JavaScript function we must call.
  napi_value js_function;
  NODE_API_CALL_RETURN_VOID(env, napi_get_reference_value(env, ref, &js_function));

  // Call the JavaScript function to indicate that the generated JavaScript
  // function is about to be gc-ed.
  NODE_API_CALL_RETURN_VOID(env,
      napi_call_function(env, undefined, js_function, 0, NULL, NULL));

  // Destroy the persistent reference to the function we just called so as to
  // properly clean up.
  NODE_API_CALL_RETURN_VOID(env, napi_delete_reference(env, ref));
}

static napi_value MakeTrackedFunction(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value js_finalize_cb;
  napi_valuetype arg_type;

  // Retrieve and validate from the arguments the function we will use to
  // indicate to JavaScript that the function we are about to create is about to
  // be gc-ed.
  NODE_API_CALL(env,
      napi_get_cb_info(env, info, &argc, &js_finalize_cb, NULL, NULL));
  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");
  NODE_API_CALL(env, napi_typeof(env, js_finalize_cb, &arg_type));
  NODE_API_ASSERT(env, arg_type == napi_function, "Argument must be a function");

  // Dynamically create a function.
  napi_value result;
  NODE_API_CALL(env,
      napi_create_function(
          env, "TrackedFunction", NAPI_AUTO_LENGTH, TestFunctionName, NULL,
          &result));

  // Create a strong reference to the function we will call when the tracked
  // function is about to be gc-ed.
  napi_ref js_finalize_cb_ref;
  NODE_API_CALL(env,
      napi_create_reference(env, js_finalize_cb, 1, &js_finalize_cb_ref));

  // Attach a finalizer to the dynamically created function and pass it the
  // strong reference we created in the previous step.
  NODE_API_CALL(env,
      napi_wrap(
          env, result, js_finalize_cb_ref, finalize_function, NULL, NULL));

  return result;
}

static napi_value TestBadReturnExceptionPending(napi_env env, napi_callback_info info) {
  napi_throw_error(env, "throwing exception", "throwing exception");

  // addons should only ever return a valid napi_value even if an
  // exception occurs, but we have seen that the C++ wrapper
  // with exceptions enabled sometimes returns an invalid value
  // when an exception is thrown. Test that we ignore the return
  // value then an exception is pending. We use 0xFFFFFFFF as a value
  // that should never be a valid napi_value and node seems to
  // crash if it is not ignored indicating that it is indeed invalid.
  return (napi_value)(0xFFFFFFFFF);
}

EXTERN_C_START
napi_value Init(napi_env env, napi_value exports) {
  napi_value fn1;
  NODE_API_CALL(env, napi_create_function(
      env, NULL, NAPI_AUTO_LENGTH, TestCallFunction, NULL, &fn1));

  napi_value fn2;
  NODE_API_CALL(env, napi_create_function(
      env, "Name", NAPI_AUTO_LENGTH, TestFunctionName, NULL, &fn2));

  napi_value fn3;
  NODE_API_CALL(env, napi_create_function(
      env, "Name_extra", 5, TestFunctionName, NULL, &fn3));

  napi_value fn4;
  NODE_API_CALL(env,
      napi_create_function(
          env, "MakeTrackedFunction", NAPI_AUTO_LENGTH, MakeTrackedFunction,
          NULL, &fn4));

  napi_value fn5;
  NODE_API_CALL(env,
      napi_create_function(
          env, "TestCreateFunctionParameters", NAPI_AUTO_LENGTH,
          TestCreateFunctionParameters, NULL, &fn5));

  napi_value fn6;
  NODE_API_CALL(env,
      napi_create_function(
          env, "TestBadReturnExceptionPending", NAPI_AUTO_LENGTH,
          TestBadReturnExceptionPending, NULL, &fn6));

  NODE_API_CALL(env, napi_set_named_property(env, exports, "TestCall", fn1));
  NODE_API_CALL(env, napi_set_named_property(env, exports, "TestName", fn2));
  NODE_API_CALL(env,
      napi_set_named_property(env, exports, "TestNameShort", fn3));
  NODE_API_CALL(env,
      napi_set_named_property(env, exports, "MakeTrackedFunction", fn4));

  NODE_API_CALL(env,
      napi_set_named_property(
          env, exports, "TestCreateFunctionParameters", fn5));

  NODE_API_CALL(env,
      napi_set_named_property(
          env, exports, "TestBadReturnExceptionPending", fn6));

  return exports;
}
EXTERN_C_END
