#ifndef TEST_EMBEDDING_EMBEDTEST_C_CPP_API_COMMON_H_
#define TEST_EMBEDDING_EMBEDTEST_C_CPP_API_COMMON_H_

#define NAPI_EXPERIMENTAL

#include <node_embedding_api_cpp.h>

namespace node::embedding {

extern const char* main_script;

napi_status AddUtf8String(std::string& str, napi_env env, napi_value value);

void GetAndThrowLastErrorMessage(napi_env env);

void ThrowLastErrorMessage(napi_env env, const char* message);

NodeExpected<void> LoadUtf8Script(const NodeRuntimeConfig& runtime_config,
                                  std::string_view script);

NodeExpected<void> PrintErrorMessage(std::string_view exe_name,
                                     NodeStatus status);

class TestExitCodeHandler : public NodeEmbeddingErrorHandler {
 public:
  TestExitCodeHandler(const char* exe_name) : exe_name_(exe_name) {}

  int32_t ReportResult() {
    return PrintErrorMessage(exe_name_,
                             NodeEmbeddingErrorHandler::ReportResult().status())
        .exit_code();
  }

 private:
  const char* exe_name_ = nullptr;
};

class TestExitOnErrorHandler : public NodeEmbeddingErrorHandler {
 public:
  TestExitOnErrorHandler(const char* exe_name) : exe_name_(exe_name) {}

  void ReportResult() {
    int32_t exit_code =
        PrintErrorMessage(exe_name_,
                          NodeEmbeddingErrorHandler::ReportResult().status())
            .exit_code();
    if (exit_code != 0) {
      exit(exit_code);
    }
  }

 private:
  const char* exe_name_ = nullptr;
};

}  // namespace node::embedding

//==============================================================================
// Error handing macros
//==============================================================================

#define NODE_ASSERT(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      error_handler.SetLastErrorMessage(                                       \
          "Failed: %s\nFile: %s\nLine: %d\n", #expr, __FILE__, __LINE__);      \
      return error_handler.ReportResult();                                     \
    }                                                                          \
  } while (0)

#define NODE_FAIL(format, ...)                                                 \
  do {                                                                         \
    error_handler.SetLastErrorMessage("Failed: " format                        \
                                      "\nFile: %s\nLine: %d\n",                \
                                      ##__VA_ARGS__,                           \
                                      __FILE__,                                \
                                      __LINE__);                               \
    return error_handler.ReportResult();                                       \
  } while (0)

#define NODE_API_CALL(expr)                                                    \
  do {                                                                         \
    error_handler.set_node_api_status(expr);                                   \
    if (error_handler.has_node_api_error()) {                                  \
      return error_handler.ReportResult();                                     \
    }                                                                          \
  } while (0)

#define NODE_EMBEDDING_CALL(expr)                                              \
  do {                                                                         \
    error_handler.set_embedding_status(expr);                                  \
    if (error_handler.has_embedding_error()) {                                 \
      return error_handler.ReportResult();                                     \
    }                                                                          \
  } while (0)

#endif  // TEST_EMBEDDING_EMBEDTEST_C_CPP_API_COMMON_H_
