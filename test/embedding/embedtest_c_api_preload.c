#include "embedtest_c_api_common.h"

static void OnPreload(void* cb_data,
                      node_embedding_runtime runtime,
                      napi_env env,
                      napi_value process,
                      napi_value require) {
  napi_status status = napi_ok;
  napi_value global, value;
  NODE_API_CALL(napi_get_global(env, &global));
  NODE_API_CALL(napi_create_int32(env, 42, &value));
  NODE_API_CALL(napi_set_named_property(env, global, "preloadValue", value));
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
  NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));
on_exit:
  return embedding_status;
}

// Tests that the same preload callback is called from the main thread and from
// the worker thread.
int32_t test_main_c_api_preload(int32_t argc, const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(node_embedding_main_run(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, ConfigureRuntime, NULL));
on_exit:
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}
