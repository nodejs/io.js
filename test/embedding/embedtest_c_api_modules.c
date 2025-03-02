#include <uv.h>
#include "embedtest_c_api_common.h"

typedef struct {
  uv_mutex_t mutex;
  int32_t greeter_module_init_call_count;
  int32_t replicator_module_init_call_count;
} test_data_t;

static napi_value GreeterFunction(napi_env env, napi_callback_info info) {
  napi_status status = napi_ok;
  char greeting_buf[256] = {0};
  strcpy(greeting_buf, "Hello, ");
  size_t offset = strlen(greeting_buf);
  napi_value result = NULL;

  napi_value arg;
  size_t arg_count = 1;
  NODE_API_CALL(napi_get_cb_info(env, info, &arg_count, &arg, NULL, NULL));
  NODE_API_CALL(napi_get_value_string_utf8(
      env, arg, greeting_buf + offset, 256 - offset, NULL));
  NODE_API_CALL(
      napi_create_string_utf8(env, greeting_buf, NAPI_AUTO_LENGTH, &result));

on_exit:
  GetAndThrowLastErrorMessage(env, status);
  return result;
}

static napi_value InitGreeterModule(void* cb_data,
                                    node_embedding_runtime runtime,
                                    napi_env env,
                                    const char* module_name,
                                    napi_value exports) {
  napi_status status = napi_ok;
  test_data_t* test_data = (test_data_t*)cb_data;
  uv_mutex_lock(&test_data->mutex);
  ++test_data->greeter_module_init_call_count;
  uv_mutex_unlock(&test_data->mutex);

  napi_value greet_func;
  NODE_API_CALL(napi_create_function(
      env, "greet", NAPI_AUTO_LENGTH, GreeterFunction, NULL, &greet_func));
  NODE_API_CALL(napi_set_named_property(env, exports, "greet", greet_func));

on_exit:
  GetAndThrowLastErrorMessage(env, status);
  return exports;
}

static napi_value ReplicatorFunction(napi_env env, napi_callback_info info) {
  napi_status status = napi_ok;
  char replicator_buf[256] = {0};

  napi_value result = NULL;
  napi_value arg;
  size_t arg_count = 1;
  NODE_API_CALL(napi_get_cb_info(env, info, &arg_count, &arg, NULL, NULL));
  size_t str_size = 0;
  NODE_API_CALL(
      napi_get_value_string_utf8(env, arg, replicator_buf, 256, &str_size));
  strcpy(replicator_buf + str_size, " ");
  strncpy(replicator_buf + str_size + 1, replicator_buf, str_size);
  NODE_API_CALL(
      napi_create_string_utf8(env, replicator_buf, NAPI_AUTO_LENGTH, &result));

on_exit:
  GetAndThrowLastErrorMessage(env, status);
  return result;
}

static napi_value InitReplicatorModule(void* cb_data,
                                       node_embedding_runtime runtime,
                                       napi_env env,
                                       const char* module_name,
                                       napi_value exports) {
  napi_status status = napi_ok;
  test_data_t* test_data = (test_data_t*)cb_data;
  uv_mutex_lock(&test_data->mutex);
  ++test_data->replicator_module_init_call_count;
  uv_mutex_unlock(&test_data->mutex);

  napi_value greet_func;
  NODE_API_CALL(napi_create_function(env,
                                     "replicate",
                                     NAPI_AUTO_LENGTH,
                                     ReplicatorFunction,
                                     NULL,
                                     &greet_func));
  NODE_API_CALL(napi_set_named_property(env, exports, "replicate", greet_func));

on_exit:
  GetAndThrowLastErrorMessage(env, status);
  return exports;
}

static void OnPreload(void* cb_data,
                      node_embedding_runtime runtime,
                      napi_env env,
                      napi_value process,
                      napi_value require) {
  napi_status status = napi_ok;
  napi_value global;
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_set_named_property(env, global, "process", process));
on_exit:
  GetAndThrowLastErrorMessage(env, status);
}

static node_embedding_status ConfigureRuntime(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(node_embedding_runtime_config_on_preload(
      runtime_config, OnPreload, NULL, NULL));
  NODE_EMBEDDING_CALL(
      node_embedding_runtime_config_add_module(runtime_config,
                                               "greeter_module",
                                               InitGreeterModule,
                                               cb_data,
                                               NULL,
                                               NAPI_VERSION));
  NODE_EMBEDDING_CALL(
      node_embedding_runtime_config_add_module(runtime_config,
                                               "replicator_module",
                                               InitReplicatorModule,
                                               cb_data,
                                               NULL,
                                               NAPI_VERSION));
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
on_exit:
  return embedding_status;
}

int32_t test_main_c_api_linked_modules(int32_t argc, const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  int32_t expected_greeter_module_init_call_count = atoi(argv[2]);
  int32_t expected_replicator_module_init_call_count = atoi(argv[2]);

  test_data_t test_data = {0};
  uv_mutex_init(&test_data.mutex);

  NODE_EMBEDDING_ASSERT(argc == 4);

  NODE_EMBEDDING_CALL(node_embedding_main_run(NODE_EMBEDDING_VERSION,
                                              argc,
                                              argv,
                                              NULL,
                                              NULL,
                                              ConfigureRuntime,
                                              &test_data));

  NODE_EMBEDDING_ASSERT(test_data.greeter_module_init_call_count ==
                        expected_greeter_module_init_call_count);
  NODE_EMBEDDING_ASSERT(test_data.replicator_module_init_call_count ==
                        expected_replicator_module_init_call_count);

on_exit:
  uv_mutex_destroy(&test_data.mutex);
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}
