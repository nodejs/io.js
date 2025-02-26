#ifndef TEST_EMBEDDING_EMBEDTEST_C_API_COMMON_H_
#define TEST_EMBEDDING_EMBEDTEST_C_API_COMMON_H_

#define NAPI_EXPERIMENTAL

#include <node_embedding_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char* main_script;

int32_t StatusToExitCode(node_embedding_status status);

node_embedding_status PrintErrorMessage(const char* exe_name,
                                        node_embedding_status status);

node_embedding_status LoadUtf8Script(
    node_embedding_runtime_config runtime_config, const char* script);

napi_status GetAndThrowLastErrorMessage(napi_env env, napi_status status);

void ThrowLastErrorMessage(napi_env env, const char* format, ...);

#define DYNAMIC_STRING_BUFFER_SIZE 256

typedef struct {
  char* data;
  size_t length;
  char buffer[DYNAMIC_STRING_BUFFER_SIZE];
} dynamic_string_t;

void dynamic_string_init(dynamic_string_t* str);
void dynamic_string_destroy(dynamic_string_t* str);
void dynamic_string_set(dynamic_string_t* str, const char* value);
void dynamic_string_append(dynamic_string_t* str, const char* value);

//==============================================================================
// Error handing macros
//==============================================================================

#define NODE_API_CALL(expr)                                                    \
  do {                                                                         \
    status = (expr);                                                           \
    if (status != napi_ok) {                                                   \
      goto on_exit;                                                            \
    }                                                                          \
  } while (0)

// TODO: The GetAndThrowLastErrorMessage is not going to work in this mode.
// TODO: Use the napi_is_exception_pending to check if there is an exception
#define NODE_API_ASSERT(expr)                                                  \
  do {                                                                         \
    if (!(expr)) {                                                             \
      status = napi_generic_failure;                                           \
      napi_throw_error(env, NULL, "Failed: (" #expr ")");                      \
      goto on_exit;                                                            \
    }                                                                          \
  } while (0)

#define NODE_API_FAIL(format, ...)                                             \
  do {                                                                         \
    status = napi_generic_failure;                                             \
    ThrowLastErrorMessage(env, format, ##__VA_ARGS__);                         \
    goto on_exit;                                                              \
  } while (0)

#define NODE_EMBEDDING_CALL(expr)                                              \
  do {                                                                         \
    embedding_status = (expr);                                                 \
    if (embedding_status != node_embedding_status_ok) {                        \
      goto on_exit;                                                            \
    }                                                                          \
  } while (0)

#define NODE_EMBEDDING_ASSERT(expr)                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      embedding_status = node_embedding_status_generic_error;                  \
      node_embedding_last_error_message_set_format(                            \
          "Failed: %s\nFile: %s\nLine: %d\n", #expr, __FILE__, __LINE__);      \
      goto on_exit;                                                            \
    }                                                                          \
  } while (0)

#endif  // TEST_EMBEDDING_EMBEDTEST_C_API_COMMON_H_
