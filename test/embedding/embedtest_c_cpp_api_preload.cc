#include "embedtest_c_cpp_api_common.h"

namespace node::embedding {

// Tests that the same preload callback is called from the main thread and from
// the worker thread.
int32_t test_main_c_cpp_api_preload(int32_t argc, const char* argv[]) {
  TestExitCodeHandler error_handler(argv[0]);
  NODE_EMBEDDING_CALL(NodePlatform::RunMain(
      NodeArgs(argc, argv),
      nullptr,
      [](const NodePlatform& platform,
         const NodeRuntimeConfig& runtime_config) {
        NodeEmbeddingErrorHandler error_handler;
        NODE_EMBEDDING_CALL(
            runtime_config.OnPreload([](const NodeRuntime& runtime,
                                        napi_env env,
                                        napi_value /*process*/,
                                        napi_value /*require*/
                                     ) {
              NodeApiErrorHandler<void> error_handler(env);
              napi_value global, value;
              NODE_API_CALL(napi_get_global(env, &global));
              NODE_API_CALL(napi_create_int32(env, 42, &value));
              NODE_API_CALL(
                  napi_set_named_property(env, global, "preloadValue", value));
            }));

        NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config, main_script));

        return error_handler.ReportResult();
      }));
  return error_handler.ReportResult();
}

}  // namespace node::embedding
