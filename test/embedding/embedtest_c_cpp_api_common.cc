#include "embedtest_c_cpp_api_common.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace node::embedding {

const char* main_script =
    "globalThis.require = require('module').createRequire(process.execPath);\n"
    "globalThis.embedVars = { nön_ascıı: '🏳️‍🌈' };\n"
    "require('vm').runInThisContext(process.argv[1]);";

napi_status AddUtf8String(std::string& str, napi_env env, napi_value value) {
  size_t str_size = 0;
  napi_status status =
      napi_get_value_string_utf8(env, value, nullptr, 0, &str_size);
  if (status != napi_ok) {
    return status;
  }
  size_t offset = str.size();
  str.resize(offset + str_size);
  status = napi_get_value_string_utf8(
      env, value, &str[0] + offset, str_size + 1, &str_size);
  return status;
}

NodeExpected<void> LoadUtf8Script(const NodeRuntimeConfig& runtime_config,
                                  std::string_view script) {
  NodeEmbeddingErrorHandler error_handler;
  NODE_EMBEDDING_CALL(runtime_config.OnStartExecution(
      [script = std::string(script)](const NodeRuntime& /*runtime*/,
                                     napi_env env,
                                     napi_value /*process*/,
                                     napi_value /*require*/,
                                     napi_value run_cjs) -> napi_value {
        NodeApiErrorHandler<napi_value> error_handler(env);
        napi_value script_value{}, null_value{}, result{};
        NODE_API_CALL(napi_create_string_utf8(
            env, script.c_str(), script.size(), &script_value));
        NODE_API_CALL(napi_get_null(env, &null_value));
        NODE_API_CALL(napi_call_function(
            env, null_value, run_cjs, 1, &script_value, &result));
        return result;
      }));
  return error_handler.ReportResult();
}

NodeExpected<void> PrintErrorMessage(std::string_view exe_name,
                                     NodeStatus status) {
  std::string error_message = NodeErrorInfo::GetAndClearLastErrorMessage();
  if (status != NodeStatus::kOk) {
    fprintf(stderr, "%s: %s\n", exe_name.data(), error_message.c_str());
  } else if (!error_message.empty()) {
    fprintf(stdout, "%s\n", error_message.c_str());
  }
  return NodeExpected<void>(status);
}

}  // namespace node::embedding
