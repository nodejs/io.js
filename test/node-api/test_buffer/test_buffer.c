#include <stdlib.h>
#include <string.h>
#include <node_api.h>
#include "../../js-native-api/common.h"

static const char theText[] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";

static int deleterCallCount = 0;

static void deleteTheText(node_api_basic_env env,
                          void* data,
                          void* finalize_hint) {
  NODE_API_BASIC_ASSERT_RETURN_VOID(data != NULL && strcmp(data, theText) == 0,
                                    "invalid data");

  (void)finalize_hint;
  free(data);
  deleterCallCount++;
}

static void noopDeleter(node_api_basic_env env,
                        void* data,
                        void* finalize_hint) {
  NODE_API_BASIC_ASSERT_RETURN_VOID(data != NULL && strcmp(data, theText) == 0,
                                    "invalid data");
  (void)finalize_hint;
  deleterCallCount++;
}

static napi_value newBuffer(napi_env env, napi_callback_info info) {
  napi_value theBuffer;
  char* theCopy;
  const unsigned int kBufferSize = sizeof(theText);

  NODE_API_CALL(env,
      napi_create_buffer(
          env, sizeof(theText), (void**)(&theCopy), &theBuffer));
  NODE_API_ASSERT(env, theCopy, "Failed to copy static text for newBuffer");
  memcpy(theCopy, theText, kBufferSize);

  return theBuffer;
}

static napi_value newExternalBuffer(napi_env env, napi_callback_info info) {
  napi_value theBuffer;
  char* theCopy = strdup(theText);
  NODE_API_ASSERT(
      env, theCopy, "Failed to copy static text for newExternalBuffer");
  NODE_API_CALL(env,
                napi_create_external_buffer(env,
                                            sizeof(theText),
                                            theCopy,
                                            deleteTheText,
                                            NULL /* finalize_hint */,
                                            &theBuffer));

  return theBuffer;
}

static napi_value getDeleterCallCount(napi_env env, napi_callback_info info) {
  napi_value callCount;
  NODE_API_CALL(env, napi_create_int32(env, deleterCallCount, &callCount));
  return callCount;
}

static napi_value copyBuffer(napi_env env, napi_callback_info info) {
  napi_value theBuffer;
  NODE_API_CALL(env, napi_create_buffer_copy(
      env, sizeof(theText), theText, NULL, &theBuffer));
  return theBuffer;
}

static napi_value bufferHasInstance(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, NULL, NULL));
  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");
  napi_value theBuffer = args[0];
  bool hasInstance;
  napi_valuetype theType;
  NODE_API_CALL(env, napi_typeof(env, theBuffer, &theType));
  NODE_API_ASSERT(env,
      theType == napi_object, "bufferHasInstance: instance is not an object");
  NODE_API_CALL(env, napi_is_buffer(env, theBuffer, &hasInstance));
  NODE_API_ASSERT(env, hasInstance, "bufferHasInstance: instance is not a buffer");
  napi_value returnValue;
  NODE_API_CALL(env, napi_get_boolean(env, hasInstance, &returnValue));
  return returnValue;
}

static napi_value bufferInfo(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, NULL, NULL));
  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");
  napi_value theBuffer = args[0];
  char *bufferData;
  napi_value returnValue;
  size_t bufferLength;
  NODE_API_CALL(env,
      napi_get_buffer_info(
          env, theBuffer, (void**)(&bufferData), &bufferLength));
  NODE_API_CALL(env, napi_get_boolean(env,
      !strcmp(bufferData, theText) && bufferLength == sizeof(theText),
      &returnValue));
  return returnValue;
}

static napi_value staticBuffer(napi_env env, napi_callback_info info) {
  napi_value theBuffer;
  NODE_API_CALL(env,
                napi_create_external_buffer(env,
                                            sizeof(theText),
                                            (void*)theText,
                                            noopDeleter,
                                            NULL /* finalize_hint */,
                                            &theBuffer));
  return theBuffer;
}

static napi_value invalidObjectAsBuffer(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, args, NULL, NULL));
  NODE_API_ASSERT(env, argc == 1, "Wrong number of arguments");

  napi_value notTheBuffer = args[0];
  napi_status status = napi_get_buffer_info(env, notTheBuffer, NULL, NULL);
  NODE_API_ASSERT(env,
                  status == napi_invalid_arg,
                  "napi_get_buffer_info: should fail with napi_invalid_arg "
                  "when passed non buffer");

  return notTheBuffer;
}

static napi_value bufferFromArrayBuffer(napi_env env, napi_callback_info info) {
  napi_status status;
  napi_value arraybuffer;
  napi_value buffer;
  size_t byte_length = 1024;
  void* data = NULL;
  size_t buffer_length = 0;
  void* buffer_data = NULL;

  status = napi_create_arraybuffer(env, byte_length, &data, &arraybuffer);
  NODE_API_ASSERT(env, status == napi_ok, "Failed to create arraybuffer");

  status = node_api_create_buffer_from_arraybuffer(
      env, arraybuffer, 0, byte_length, &buffer);
  NODE_API_ASSERT(
      env, status == napi_ok, "Failed to create buffer from arraybuffer");

  status = napi_get_buffer_info(env, buffer, &buffer_data, &buffer_length);
  NODE_API_ASSERT(env, status == napi_ok, "Failed to get buffer info");

  NODE_API_ASSERT(env, buffer_length == byte_length, "Buffer length mismatch");

  return buffer;
}

static napi_value Init(napi_env env, napi_value exports) {
  napi_value theValue;

  NODE_API_CALL(env,
      napi_create_string_utf8(env, theText, sizeof(theText), &theValue));
  NODE_API_CALL(env,
      napi_set_named_property(env, exports, "theText", theValue));

  napi_property_descriptor methods[] = {
      DECLARE_NODE_API_PROPERTY("newBuffer", newBuffer),
      DECLARE_NODE_API_PROPERTY("newExternalBuffer", newExternalBuffer),
      DECLARE_NODE_API_PROPERTY("getDeleterCallCount", getDeleterCallCount),
      DECLARE_NODE_API_PROPERTY("copyBuffer", copyBuffer),
      DECLARE_NODE_API_PROPERTY("bufferHasInstance", bufferHasInstance),
      DECLARE_NODE_API_PROPERTY("bufferInfo", bufferInfo),
      DECLARE_NODE_API_PROPERTY("staticBuffer", staticBuffer),
      DECLARE_NODE_API_PROPERTY("invalidObjectAsBuffer", invalidObjectAsBuffer),
      DECLARE_NODE_API_PROPERTY("bufferFromArrayBuffer", bufferFromArrayBuffer),
  };

  NODE_API_CALL(env, napi_define_properties(
      env, exports, sizeof(methods) / sizeof(methods[0]), methods));

  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
