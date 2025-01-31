#include "embedtest_c_api_common.h"

static napi_status CallMe(node_embedding_runtime runtime, napi_env env);
static napi_status WaitMe(node_embedding_runtime runtime, napi_env env);
static napi_status WaitMeWithCheese(node_embedding_runtime runtime,
                                    napi_env env);

static node_embedding_status ConfigurePlatform(
    void* cb_data, node_embedding_platform_config platform_config) {
  return node_embedding_platform_config_set_flags(
      platform_config, node_embedding_platform_flags_disable_node_options_env);
}

static void HandleExecutionResult(void* cb_data,
                                  node_embedding_runtime runtime,
                                  napi_env env,
                                  napi_value execution_result) {
  napi_status status = napi_ok;
  NODE_API_CALL(CallMe(runtime, env));
  NODE_API_CALL(WaitMe(runtime, env));
  NODE_API_CALL(WaitMeWithCheese(runtime, env));
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

static node_embedding_status ConfigureRuntime(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_on_loaded(
      runtime_config, HandleExecutionResult, NULL, NULL));
on_exit:
  return embedding_status;
}

int32_t test_main_c_api(int32_t argc, const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(node_embedding_main_run(NODE_EMBEDDING_VERSION,
                                              argc,
                                              argv,
                                              ConfigurePlatform,
                                              NULL,
                                              ConfigureRuntime,
                                              NULL));
on_exit:
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}

static napi_status CallMe(node_embedding_runtime runtime, napi_env env) {
  napi_status status = napi_ok;
  napi_value global, cb, key;

  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_create_string_utf8(env, "callMe", NAPI_AUTO_LENGTH, &key));
  NODE_API_CALL(napi_get_property(env, global, key, &cb));

  napi_valuetype cb_type;
  NODE_API_CALL(napi_typeof(env, cb, &cb_type));

  // Only evaluate callMe if it was registered as a function.
  if (cb_type == napi_function) {
    napi_value undef;
    NODE_API_CALL(napi_get_undefined(env, &undef));
    napi_value arg;
    NODE_API_CALL(
        napi_create_string_utf8(env, "called", NAPI_AUTO_LENGTH, &arg));
    napi_value result;
    NODE_API_CALL(napi_call_function(env, undef, cb, 1, &arg, &result));

    char buf[32];
    size_t len;
    NODE_API_CALL(napi_get_value_string_utf8(env, result, buf, 32, &len));
    if (strcmp(buf, "called you") != 0) {
      NODE_API_FAIL("Invalid value received: %s\n", buf);
    }
    printf("%s", buf);
  } else if (cb_type != napi_undefined) {
    NODE_API_FAIL("Invalid callMe value\n");
  }
on_exit:
  return status;
}

// TODO: remove static variables
char callback_buf[32];
size_t callback_buf_len;
static napi_value c_cb(napi_env env, napi_callback_info info) {
  napi_status status = napi_ok;
  size_t argc = 1;
  napi_value arg;
  NODE_API_CALL(napi_get_cb_info(env, info, &argc, &arg, NULL, NULL));
  NODE_API_CALL(napi_get_value_string_utf8(
      env, arg, callback_buf, 32, &callback_buf_len));
on_exit:
  GetAndThrowLastErrorMessage(env, status);
  return NULL;
}

static napi_status WaitMe(node_embedding_runtime runtime, napi_env env) {
  napi_status status = napi_ok;
  node_embedding_status embedding_status = node_embedding_status_ok;
  napi_value global, cb, key;

  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_create_string_utf8(env, "waitMe", NAPI_AUTO_LENGTH, &key));
  NODE_API_CALL(napi_get_property(env, global, key, &cb));

  napi_valuetype cb_type;
  NODE_API_CALL(napi_typeof(env, cb, &cb_type));

  // Only evaluate waitMe if it was registered as a function.
  if (cb_type == napi_function) {
    napi_value undef;
    NODE_API_CALL(napi_get_undefined(env, &undef));
    napi_value args[2];
    NODE_API_CALL(
        napi_create_string_utf8(env, "waited", NAPI_AUTO_LENGTH, &args[0]));
    NODE_API_CALL(napi_create_function(
        env, "wait_cb", strlen("wait_cb"), c_cb, NULL, &args[1]));

    napi_value result;
    memset(callback_buf, 0, 32);
    NODE_API_CALL(napi_call_function(env, undef, cb, 2, args, &result));
    if (strcmp(callback_buf, "waited you") == 0) {
      NODE_API_FAIL("Anachronism detected: %s\n", callback_buf);
    }

    bool has_more_events = true;
    while (has_more_events) {
      NODE_EMBEDDING_CALL(node_embedding_runtime_event_loop_run_once(
          runtime, &has_more_events));
    }

    if (strcmp(callback_buf, "waited you") != 0) {
      NODE_API_FAIL("Invalid value received: %s\n", callback_buf);
    }
    printf("%s", callback_buf);
  } else if (cb_type != napi_undefined) {
    NODE_API_FAIL("Invalid waitMe value\n");
  }

on_exit:
  if (embedding_status != node_embedding_status_ok) {
    NODE_API_FAIL("WaitMe failed: %s\n",
                  node_embedding_last_error_message_get());
  }
  return status;
}

typedef enum {
  kPromiseStatePending,
  kPromiseStateFulfilled,
  kPromiseStateRejected,
} PromiseState;

static napi_value OnFulfilled(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value result;
  void* data;
  napi_get_cb_info(env, info, &argc, &result, NULL, &data);
  napi_get_value_string_utf8(env, result, callback_buf, 32, &callback_buf_len);
  *(PromiseState*)data = kPromiseStateFulfilled;
  return NULL;
}

static napi_value OnRejected(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value result;
  void* data;
  napi_get_cb_info(env, info, &argc, &result, NULL, &data);
  napi_get_value_string_utf8(env, result, callback_buf, 32, &callback_buf_len);
  *(PromiseState*)data = kPromiseStateRejected;
  return NULL;
}

static napi_status WaitMeWithCheese(node_embedding_runtime runtime,
                                    napi_env env) {
  napi_status status = napi_ok;
  node_embedding_status embedding_status = node_embedding_status_ok;
  PromiseState promise_state = kPromiseStatePending;
  napi_value global, wait_promise, undefined;
  napi_value on_fulfilled, on_rejected;
  napi_value then_args[2];
  const char* expected;

  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_get_undefined(env, &undefined));

  NODE_API_CALL(
      napi_get_named_property(env, global, "waitPromise", &wait_promise));

  napi_valuetype wait_promise_type;
  NODE_API_CALL(napi_typeof(env, wait_promise, &wait_promise_type));

  // Only evaluate waitPromise if it was registered as a function.
  if (wait_promise_type == napi_undefined) {
    return napi_ok;
  } else if (wait_promise_type != napi_function) {
    NODE_API_FAIL("Invalid waitPromise value\n");
  }

  napi_value arg;
  NODE_API_CALL(napi_create_string_utf8(env, "waited", NAPI_AUTO_LENGTH, &arg));

  memset(callback_buf, 0, 32);
  napi_value promise;
  NODE_API_CALL(
      napi_call_function(env, undefined, wait_promise, 1, &arg, &promise));

  if (strcmp(callback_buf, "waited with cheese") == 0) {
    NODE_API_FAIL("Anachronism detected: %s\n", callback_buf);
  }

  bool is_promise;
  NODE_API_CALL(napi_is_promise(env, promise, &is_promise));
  if (!is_promise) {
    NODE_API_FAIL("Result is not a Promise\n");
  }

  NODE_API_CALL(napi_create_function(env,
                                     "onFulfilled",
                                     NAPI_AUTO_LENGTH,
                                     OnFulfilled,
                                     &promise_state,
                                     &on_fulfilled));
  NODE_API_CALL(napi_create_function(env,
                                     "rejected",
                                     NAPI_AUTO_LENGTH,
                                     OnRejected,
                                     &promise_state,
                                     &on_rejected));
  napi_value then;
  NODE_API_CALL(napi_get_named_property(env, promise, "then", &then));
  then_args[0] = on_fulfilled;
  then_args[1] = on_rejected;
  NODE_API_CALL(napi_call_function(env, promise, then, 2, then_args, NULL));

  bool has_more_events = true;
  while (has_more_events && promise_state == kPromiseStatePending) {
    NODE_EMBEDDING_CALL(
        node_embedding_runtime_event_loop_run_once(runtime, &has_more_events));
  }

  expected = (promise_state == kPromiseStateFulfilled)
                 ? "waited with cheese"
                 : "waited without cheese";

  if (strcmp(callback_buf, expected) != 0) {
    NODE_API_FAIL("Invalid value received: %s\n", callback_buf);
  }
  printf("%s", callback_buf);

on_exit:
  if (embedding_status != node_embedding_status_ok) {
    ThrowLastErrorMessage(env,
                          "WaitMeWithCheese failed: %s",
                          node_embedding_last_error_message_get());
  }
  return status;
}
