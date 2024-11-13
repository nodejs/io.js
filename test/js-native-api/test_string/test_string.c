#include <js_native_api.h>
#include <limits.h>  // INT_MAX
#include <stdlib.h>
#include <string.h>
#include "../common.h"
#include "../entry_point.h"
#include "test_null.h"

enum length_type { actual_length, auto_length };

static napi_status validate_and_retrieve_single_string_arg(
    napi_env env, napi_callback_info info, napi_value* arg) {
  size_t argc = 1;
  NODE_API_CHECK_STATUS(napi_get_cb_info(env, info, &argc, arg, NULL, NULL));

  NODE_API_ASSERT_STATUS(env, argc >= 1, "Wrong number of arguments");

  napi_valuetype valuetype;
  NODE_API_CHECK_STATUS(napi_typeof(env, *arg, &valuetype));

  NODE_API_ASSERT_STATUS(env,
                         valuetype == napi_string,
                         "Wrong type of argment. Expects a string.");

  return napi_ok;
}

// These help us factor out code that is common between the bindings.
typedef napi_status (*OneByteCreateAPI)(napi_env,
                                        const char*,
                                        size_t,
                                        napi_value*);
typedef napi_status (*OneByteGetAPI)(
    napi_env, napi_value, char*, size_t, size_t*);
typedef napi_status (*TwoByteCreateAPI)(napi_env,
                                        const char16_t*,
                                        size_t,
                                        napi_value*);
typedef napi_status (*TwoByteGetAPI)(
    napi_env, napi_value, char16_t*, size_t, size_t*);

// Test passing back the one-byte string we got from JS.
static napi_value TestOneByteImpl(napi_env env,
                                  napi_callback_info info,
                                  OneByteGetAPI get_api,
                                  OneByteCreateAPI create_api,
                                  enum length_type length_mode) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  char buffer[128];
  size_t buffer_size = 128;
  size_t copied;

  NODE_API_CALL(env, get_api(env, args[0], buffer, buffer_size, &copied));

  napi_value output;
  if (length_mode == auto_length) {
    copied = NAPI_AUTO_LENGTH;
  }
  NODE_API_CALL(env, create_api(env, buffer, copied, &output));

  return output;
}

// Test passing back the two-byte string we got from JS.
static napi_value TestTwoByteImpl(napi_env env,
                                  napi_callback_info info,
                                  TwoByteGetAPI get_api,
                                  TwoByteCreateAPI create_api,
                                  enum length_type length_mode) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  char16_t buffer[128];
  size_t buffer_size = 128;
  size_t copied;

  NODE_API_CALL(env, get_api(env, args[0], buffer, buffer_size, &copied));

  napi_value output;
  if (length_mode == auto_length) {
    copied = NAPI_AUTO_LENGTH;
  }
  NODE_API_CALL(env, create_api(env, buffer, copied, &output));

  return output;
}

static void free_string(node_api_basic_env env, void* data, void* hint) {
  free(data);
}

static napi_status create_external_latin1(napi_env env,
                                          const char* string,
                                          size_t length,
                                          napi_value* result) {
  napi_status status;
  // Initialize to true, because that is the value we don't want.
  bool copied = true;
  char* string_copy;
  const size_t actual_length =
      (length == NAPI_AUTO_LENGTH ? strlen(string) : length);
  const size_t length_bytes = (actual_length + 1) * sizeof(*string_copy);
  string_copy = malloc(length_bytes);
  memcpy(string_copy, string, length_bytes);
  string_copy[actual_length] = 0;

  status = node_api_create_external_string_latin1(
      env, string_copy, length, free_string, NULL, result, &copied);
  // We do not want the string to be copied.
  if (copied) {
    return napi_generic_failure;
  }
  if (status != napi_ok) {
    free(string_copy);
    return status;
  }
  return napi_ok;
}

// strlen for char16_t. Needed in case we're copying a string of length
// NAPI_AUTO_LENGTH.
static size_t strlen16(const char16_t* string) {
  for (const char16_t* iter = string;; iter++) {
    if (*iter == 0) {
      return iter - string;
    }
  }
  // We should never get here.
  abort();
}

static napi_status create_external_utf16(napi_env env,
                                         const char16_t* string,
                                         size_t length,
                                         napi_value* result) {
  napi_status status;
  // Initialize to true, because that is the value we don't want.
  bool copied = true;
  char16_t* string_copy;
  const size_t actual_length =
      (length == NAPI_AUTO_LENGTH ? strlen16(string) : length);
  const size_t length_bytes = (actual_length + 1) * sizeof(*string_copy);
  string_copy = malloc(length_bytes);
  memcpy(string_copy, string, length_bytes);
  string_copy[actual_length] = 0;

  status = node_api_create_external_string_utf16(
      env, string_copy, length, free_string, NULL, result, &copied);
  if (status != napi_ok) {
    free(string_copy);
    return status;
  }

  return napi_ok;
}

static napi_value TestLatin1(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         napi_create_string_latin1,
                         actual_length);
}

static napi_value TestUtf8(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_utf8,
                         napi_create_string_utf8,
                         actual_length);
}

static napi_value TestUtf16(napi_env env, napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         napi_create_string_utf16,
                         actual_length);
}

static napi_value TestLatin1AutoLength(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         napi_create_string_latin1,
                         auto_length);
}

static napi_value TestUtf8AutoLength(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_utf8,
                         napi_create_string_utf8,
                         auto_length);
}

static napi_value TestUtf16AutoLength(napi_env env, napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         napi_create_string_utf16,
                         auto_length);
}

static napi_value TestLatin1External(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         create_external_latin1,
                         actual_length);
}

static napi_value TestUtf16External(napi_env env, napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         create_external_utf16,
                         actual_length);
}

static napi_value TestLatin1ExternalAutoLength(napi_env env,
                                               napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         create_external_latin1,
                         auto_length);
}

static napi_value TestUtf16ExternalAutoLength(napi_env env,
                                              napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         create_external_utf16,
                         auto_length);
}

static napi_value TestLatin1Insufficient(napi_env env,
                                         napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  char buffer[4];
  size_t buffer_size = 4;
  size_t copied;

  NODE_API_CALL(
      env,
      napi_get_value_string_latin1(env, args[0], buffer, buffer_size, &copied));

  napi_value output;
  NODE_API_CALL(env, napi_create_string_latin1(env, buffer, copied, &output));

  return output;
}

static napi_value TestUtf8Insufficient(napi_env env, napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  char buffer[4];
  size_t buffer_size = 4;
  size_t copied;

  NODE_API_CALL(
      env,
      napi_get_value_string_utf8(env, args[0], buffer, buffer_size, &copied));

  napi_value output;
  NODE_API_CALL(env, napi_create_string_utf8(env, buffer, copied, &output));

  return output;
}

static napi_value TestUtf16Insufficient(napi_env env, napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  char16_t buffer[4];
  size_t buffer_size = 4;
  size_t copied;

  NODE_API_CALL(
      env,
      napi_get_value_string_utf16(env, args[0], buffer, buffer_size, &copied));

  napi_value output;
  NODE_API_CALL(env, napi_create_string_utf16(env, buffer, copied, &output));

  return output;
}

static napi_value TestPropertyKeyLatin1(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         node_api_create_property_key_latin1,
                         actual_length);
}

static napi_value TestPropertyKeyLatin1AutoLength(napi_env env,
                                                  napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_latin1,
                         node_api_create_property_key_latin1,
                         auto_length);
}

static napi_value TestPropertyKeyUtf8(napi_env env, napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_utf8,
                         node_api_create_property_key_utf8,
                         actual_length);
}

static napi_value TestPropertyKeyUtf8AutoLength(napi_env env,
                                                napi_callback_info info) {
  return TestOneByteImpl(env,
                         info,
                         napi_get_value_string_utf8,
                         node_api_create_property_key_utf8,
                         auto_length);
}

static napi_value TestPropertyKeyUtf16(napi_env env, napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         node_api_create_property_key_utf16,
                         actual_length);
}

static napi_value TestPropertyKeyUtf16AutoLength(napi_env env,
                                                 napi_callback_info info) {
  return TestTwoByteImpl(env,
                         info,
                         napi_get_value_string_utf16,
                         node_api_create_property_key_utf16,
                         auto_length);
}

static napi_value Latin1Length(napi_env env, napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  size_t length;
  NODE_API_CALL(env,
                napi_get_value_string_latin1(env, args[0], NULL, 0, &length));

  napi_value output;
  NODE_API_CALL(env, napi_create_uint32(env, (uint32_t)length, &output));

  return output;
}

static napi_value Utf16Length(napi_env env, napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  size_t length;
  NODE_API_CALL(env,
                napi_get_value_string_utf16(env, args[0], NULL, 0, &length));

  napi_value output;
  NODE_API_CALL(env, napi_create_uint32(env, (uint32_t)length, &output));

  return output;
}

static napi_value Utf8Length(napi_env env, napi_callback_info info) {
  napi_value args[1];
  NODE_API_CALL(env, validate_and_retrieve_single_string_arg(env, info, args));

  size_t length;
  NODE_API_CALL(env,
                napi_get_value_string_utf8(env, args[0], NULL, 0, &length));

  napi_value output;
  NODE_API_CALL(env, napi_create_uint32(env, (uint32_t)length, &output));

  return output;
}

static napi_value TestLargeUtf8(napi_env env, napi_callback_info info) {
  napi_value output;
  if (SIZE_MAX > INT_MAX) {
    NODE_API_CALL(
        env, napi_create_string_utf8(env, "", ((size_t)INT_MAX) + 1, &output));
  } else {
    // just throw the expected error as there is nothing to test
    // in this case since we can't overflow
    NODE_API_CALL(env, napi_throw_error(env, NULL, "Invalid argument"));
  }

  return output;
}

static napi_value TestLargeLatin1(napi_env env, napi_callback_info info) {
  napi_value output;
  if (SIZE_MAX > INT_MAX) {
    NODE_API_CALL(
        env,
        napi_create_string_latin1(env, "", ((size_t)INT_MAX) + 1, &output));
  } else {
    // just throw the expected error as there is nothing to test
    // in this case since we can't overflow
    NODE_API_CALL(env, napi_throw_error(env, NULL, "Invalid argument"));
  }

  return output;
}

static napi_value TestLargeUtf16(napi_env env, napi_callback_info info) {
  napi_value output;
  if (SIZE_MAX > INT_MAX) {
    NODE_API_CALL(
        env,
        napi_create_string_utf16(
            env, ((const char16_t*)""), ((size_t)INT_MAX) + 1, &output));
  } else {
    // just throw the expected error as there is nothing to test
    // in this case since we can't overflow
    NODE_API_CALL(env, napi_throw_error(env, NULL, "Invalid argument"));
  }

  return output;
}

static napi_value TestMemoryCorruption(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, NULL, NULL));

  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");

  char buf[10] = {0};
  NODE_API_CALL(env, napi_get_value_string_utf8(env, args[0], buf, 0, NULL));

  char zero[10] = {0};
  if (memcmp(buf, zero, sizeof(buf)) != 0) {
    NODE_API_CALL(env, napi_throw_error(env, NULL, "Buffer overwritten"));
  }

  return NULL;
}

EXTERN_C_START
napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
      DECLARE_NODE_API_PROPERTY("TestLatin1", TestLatin1),
      DECLARE_NODE_API_PROPERTY("TestLatin1AutoLength", TestLatin1AutoLength),
      DECLARE_NODE_API_PROPERTY("TestLatin1External", TestLatin1External),
      DECLARE_NODE_API_PROPERTY("TestLatin1ExternalAutoLength",
                                TestLatin1ExternalAutoLength),
      DECLARE_NODE_API_PROPERTY("TestLatin1Insufficient",
                                TestLatin1Insufficient),
      DECLARE_NODE_API_PROPERTY("TestUtf8", TestUtf8),
      DECLARE_NODE_API_PROPERTY("TestUtf8AutoLength", TestUtf8AutoLength),
      DECLARE_NODE_API_PROPERTY("TestUtf8Insufficient", TestUtf8Insufficient),
      DECLARE_NODE_API_PROPERTY("TestUtf16", TestUtf16),
      DECLARE_NODE_API_PROPERTY("TestUtf16AutoLength", TestUtf16AutoLength),
      DECLARE_NODE_API_PROPERTY("TestUtf16External", TestUtf16External),
      DECLARE_NODE_API_PROPERTY("TestUtf16ExternalAutoLength",
                                TestUtf16ExternalAutoLength),
      DECLARE_NODE_API_PROPERTY("TestUtf16Insufficient", TestUtf16Insufficient),
      DECLARE_NODE_API_PROPERTY("Latin1Length", Latin1Length),
      DECLARE_NODE_API_PROPERTY("Utf16Length", Utf16Length),
      DECLARE_NODE_API_PROPERTY("Utf8Length", Utf8Length),
      DECLARE_NODE_API_PROPERTY("TestLargeUtf8", TestLargeUtf8),
      DECLARE_NODE_API_PROPERTY("TestLargeLatin1", TestLargeLatin1),
      DECLARE_NODE_API_PROPERTY("TestLargeUtf16", TestLargeUtf16),
      DECLARE_NODE_API_PROPERTY("TestMemoryCorruption", TestMemoryCorruption),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyLatin1", TestPropertyKeyLatin1),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyLatin1AutoLength",
                                TestPropertyKeyLatin1AutoLength),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyUtf8", TestPropertyKeyUtf8),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyUtf8AutoLength",
                                TestPropertyKeyUtf8AutoLength),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyUtf16", TestPropertyKeyUtf16),
      DECLARE_NODE_API_PROPERTY("TestPropertyKeyUtf16AutoLength",
                                TestPropertyKeyUtf16AutoLength),
  };

  init_test_null(env, exports);

  NODE_API_CALL(
      env,
      napi_define_properties(
          env, exports, sizeof(properties) / sizeof(*properties), properties));

  return exports;
}
EXTERN_C_END
