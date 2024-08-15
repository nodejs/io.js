#ifndef JS_NATIVE_API_COMMON_H_
#define JS_NATIVE_API_COMMON_H_

#include <js_native_api.h>
#include <stdlib.h>  // abort()

// Empty value so that macros here are able to return NULL or void
#define NODE_API_RETVAL_NOTHING  // Intentionally blank #define

#define GET_AND_THROW_LAST_ERROR(env)                                    \
  do {                                                                   \
    const napi_extended_error_info *error_info;                          \
    napi_get_last_error_info((env), &error_info);                        \
    bool is_pending;                                                     \
    const char* err_message = error_info->error_message;                  \
    napi_is_exception_pending((env), &is_pending);                       \
    /* If an exception is already pending, don't rethrow it */           \
    if (!is_pending) {                                                   \
      const char* error_message = err_message != NULL ?                  \
                       err_message :                                     \
                      "empty error message";                             \
      napi_throw_error((env), NULL, error_message);                      \
    }                                                                    \
  } while (0)

// The basic version of GET_AND_THROW_LAST_ERROR. We cannot access any
// exceptions and we cannot fail by way of JS exception, so we abort.
#define FATALLY_FAIL_WITH_LAST_ERROR(env)                                      \
  do {                                                                         \
    const napi_extended_error_info* error_info;                                \
    napi_get_last_error_info((env), &error_info);                              \
    const char* err_message = error_info->error_message;                       \
    const char* error_message =                                                \
        err_message != NULL ? err_message : "empty error message";             \
    fprintf(stderr, "%s\n", error_message);                                    \
    abort();                                                                   \
  } while (0)

#define NODE_API_ASSERT_BASE(env, assertion, message, ret_val)           \
  do {                                                                   \
    if (!(assertion)) {                                                  \
      napi_throw_error(                                                  \
          (env),                                                         \
        NULL,                                                            \
          "assertion (" #assertion ") failed: " message);                \
      return ret_val;                                                    \
    }                                                                    \
  } while (0)

#define NODE_API_BASIC_ASSERT_BASE(assertion, message, ret_val)                \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      fprintf(stderr, "assertion (" #assertion ") failed: " message);          \
      abort();                                                                 \
      return ret_val;                                                          \
    }                                                                          \
  } while (0)

// Returns NULL on failed assertion.
// This is meant to be used inside napi_callback methods.
#define NODE_API_ASSERT(env, assertion, message)                         \
  NODE_API_ASSERT_BASE(env, assertion, message, NULL)

// Returns empty on failed assertion.
// This is meant to be used inside functions with void return type.
#define NODE_API_ASSERT_RETURN_VOID(env, assertion, message)             \
  NODE_API_ASSERT_BASE(env, assertion, message, NODE_API_RETVAL_NOTHING)

#define NODE_API_BASIC_ASSERT_RETURN_VOID(assertion, message)                  \
  NODE_API_BASIC_ASSERT_BASE(assertion, message, NODE_API_RETVAL_NOTHING)

#define NODE_API_CALL_BASE(env, the_call, ret_val)                       \
  do {                                                                   \
    if ((the_call) != napi_ok) {                                         \
      GET_AND_THROW_LAST_ERROR((env));                                   \
      return ret_val;                                                    \
    }                                                                    \
  } while (0)

#define NODE_API_BASIC_CALL_BASE(env, the_call, ret_val)                       \
  do {                                                                         \
    if ((the_call) != napi_ok) {                                               \
      FATALLY_FAIL_WITH_LAST_ERROR((env));                                     \
      return ret_val;                                                          \
    }                                                                          \
  } while (0)

// Returns NULL if the_call doesn't return napi_ok.
#define NODE_API_CALL(env, the_call)                                     \
  NODE_API_CALL_BASE(env, the_call, NULL)

// Returns empty if the_call doesn't return napi_ok.
#define NODE_API_CALL_RETURN_VOID(env, the_call)                         \
  NODE_API_CALL_BASE(env, the_call, NODE_API_RETVAL_NOTHING)

#define NODE_API_BASIC_CALL_RETURN_VOID(env, the_call)                         \
  NODE_API_BASIC_CALL_BASE(env, the_call, NODE_API_RETVAL_NOTHING)

#define NODE_API_CHECK_STATUS(the_call)                                   \
  do {                                                                         \
    napi_status status = (the_call);                                           \
    if (status != napi_ok) {                                                   \
      return status;                                                           \
    }                                                                          \
  } while (0)

#define NODE_API_ASSERT_STATUS(env, assertion, message)                        \
  NODE_API_ASSERT_BASE(env, assertion, message, napi_generic_failure)

#define DECLARE_NODE_API_PROPERTY(name, func)                            \
  { (name), NULL, (func), NULL, NULL, NULL, napi_default, NULL }

#define DECLARE_NODE_API_GETTER(name, func)                              \
  { (name), NULL, NULL, (func), NULL, NULL, napi_default, NULL }

#define DECLARE_NODE_API_PROPERTY_VALUE(name, value)                     \
  { (name), NULL, NULL, NULL, NULL, (value), napi_default, NULL }

static inline void add_returned_status(napi_env env,
                                       const char* key,
                                       napi_value object,
                                       char* expected_message,
                                       napi_status expected_status,
                                       napi_status actual_status);

static inline void add_last_status(napi_env env,
                                   const char* key,
                                   napi_value return_value);

#include "common-inl.h"

#endif  // JS_NATIVE_API_COMMON_H_
