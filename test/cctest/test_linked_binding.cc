// Include node.h first to define NAPI_VERSION built with the
// binary.
// The node.h should also be included first in embedder's use case.
#include "node.h"
#include "node_api.h"
#include "node_test_fixture.h"

void InitializeBinding(v8::Local<v8::Object> exports,
                       v8::Local<v8::Value> module,
                       v8::Local<v8::Context> context,
                       void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  exports
      ->Set(context,
            v8::String::NewFromOneByte(isolate,
                                       reinterpret_cast<const uint8_t*>("key"))
                .ToLocalChecked(),
            v8::String::NewFromOneByte(
                isolate, reinterpret_cast<const uint8_t*>("value"))
                .ToLocalChecked())
      .FromJust();
}

NODE_MODULE_LINKED(cctest_linkedbinding, InitializeBinding)

class LinkedBindingTest : public EnvironmentTestFixture {};

TEST_F(LinkedBindingTest, SimpleTest) {
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script = "process._linkedBinding('cctest_linkedbinding').key";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8val(isolate_, completion_value);
  CHECK_NOT_NULL(*utf8val);
  CHECK_EQ(strcmp(*utf8val, "value"), 0);
}

void InitializeLocalBinding(v8::Local<v8::Object> exports,
                            v8::Local<v8::Value> module,
                            v8::Local<v8::Context> context,
                            void* priv) {
  ++*static_cast<int*>(priv);
  v8::Isolate* isolate = context->GetIsolate();
  exports
      ->Set(context,
            v8::String::NewFromOneByte(isolate,
                                       reinterpret_cast<const uint8_t*>("key"))
                .ToLocalChecked(),
            v8::String::NewFromOneByte(
                isolate, reinterpret_cast<const uint8_t*>("value"))
                .ToLocalChecked())
      .FromJust();
}

TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingTest) {
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  int calls = 0;
  AddLinkedBinding(*test_env, "local_linked", InitializeLocalBinding, &calls);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script = "process._linkedBinding('local_linked').key";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8val(isolate_, completion_value);
  CHECK_NOT_NULL(*utf8val);
  CHECK_EQ(strcmp(*utf8val, "value"), 0);
  CHECK_EQ(calls, 1);
}

napi_value InitializeLocalNapiBinding(napi_env env, napi_value exports) {
  napi_value key, value;
  CHECK_EQ(napi_create_string_utf8(env, "hello", NAPI_AUTO_LENGTH, &key),
           napi_ok);
  CHECK_EQ(napi_create_string_utf8(env, "world", NAPI_AUTO_LENGTH, &value),
           napi_ok);
  CHECK_EQ(napi_set_property(env, exports, key, value), napi_ok);
  return nullptr;
}

static napi_module local_linked_napi = {
    NAPI_MODULE_VERSION,
    node::ModuleFlags::kLinked,
    nullptr,
    InitializeLocalNapiBinding,
    "local_linked_napi",
    nullptr,
    {0},
};

TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingNapiTest) {
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  AddLinkedBinding(*test_env, local_linked_napi);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script = "process._linkedBinding('local_linked_napi').hello";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8val(isolate_, completion_value);
  CHECK_NOT_NULL(*utf8val);
  CHECK_EQ(strcmp(*utf8val, "world"), 0);
}

TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingNapiCallbackTest) {
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  AddLinkedBinding(*test_env,
                   "local_linked_napi_cb",
                   InitializeLocalNapiBinding,
                   NAPI_VERSION);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script =
      "process._linkedBinding('local_linked_napi_cb').hello";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8val(isolate_, completion_value);
  CHECK_NOT_NULL(*utf8val);
  CHECK_EQ(strcmp(*utf8val, "world"), 0);
}

static int32_t node_api_version = NODE_API_DEFAULT_MODULE_API_VERSION;

napi_value InitializeLocalNapiRefBinding(napi_env env, napi_value exports) {
  napi_value key, value;
  CHECK_EQ(
      napi_create_string_utf8(env, "napi_ref_created", NAPI_AUTO_LENGTH, &key),
      napi_ok);

  // In experimental Node-API version we can create napi_ref to any value type.
  // Here we are trying to create a reference to the key string.
  napi_ref ref{};
  if (node_api_version == NAPI_VERSION_EXPERIMENTAL) {
    CHECK_EQ(napi_create_reference(env, key, 1, &ref), napi_ok);
    CHECK_EQ(napi_delete_reference(env, ref), napi_ok);
  } else {
    CHECK_EQ(napi_create_reference(env, key, 1, &ref), napi_invalid_arg);
  }
  CHECK_EQ(napi_get_boolean(env, ref != nullptr, &value), napi_ok);
  CHECK_EQ(napi_set_property(env, exports, key, value), napi_ok);
  return nullptr;
}

// napi_ref in Node-API version 8 cannot accept strings.
TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingNapiRefVersion8Test) {
  node_api_version = NODE_API_DEFAULT_MODULE_API_VERSION;
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  AddLinkedBinding(*test_env,
                   "local_linked_napi_ref_v8",
                   InitializeLocalNapiRefBinding,
                   node_api_version);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script =
      "process._linkedBinding('local_linked_napi_ref_v8').napi_ref_created";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  CHECK(completion_value->IsBoolean());
  CHECK(!completion_value.As<v8::Boolean>()->Value());
}

// Experimental version of napi_ref in Node-API can accept strings.
TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingNapiRefExperimentalTest) {
  node_api_version = NAPI_VERSION_EXPERIMENTAL;
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  AddLinkedBinding(*test_env,
                   "local_linked_napi_ref_experimental",
                   InitializeLocalNapiRefBinding,
                   node_api_version);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script = "process._linkedBinding('local_linked_napi_ref_"
                           "experimental').napi_ref_created";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  CHECK(completion_value->IsBoolean());
  CHECK(completion_value.As<v8::Boolean>()->Value());
}

napi_value NapiLinkedWithInstanceData(napi_env env, napi_value exports) {
  int* instance_data = new int(0);
  CHECK_EQ(napi_set_instance_data(
               env,
               instance_data,
               [](napi_env env, void* data, void* hint) {
                 ++*static_cast<int*>(data);
               },
               nullptr),
           napi_ok);

  napi_value key, value;
  CHECK_EQ(napi_create_string_utf8(env, "hello", NAPI_AUTO_LENGTH, &key),
           napi_ok);
  CHECK_EQ(napi_create_external_arraybuffer(
               env, instance_data, 1, nullptr, nullptr, &value),
           napi_ok);
  CHECK_EQ(napi_set_property(env, exports, key, value), napi_ok);
  return nullptr;
}

static napi_module local_linked_napi_id = {
    NAPI_MODULE_VERSION,
    node::ModuleFlags::kLinked,
    nullptr,
    NapiLinkedWithInstanceData,
    "local_linked_napi_id",
    nullptr,
    {0},
};

TEST_F(LinkedBindingTest, LocallyDefinedLinkedBindingNapiInstanceDataTest) {
  const v8::HandleScope handle_scope(isolate_);
  int* instance_data = nullptr;

  {
    const Argv argv;
    Env test_env{handle_scope, argv};

    AddLinkedBinding(*test_env, local_linked_napi_id);

    v8::Local<v8::Context> context = isolate_->GetCurrentContext();

    const char* run_script =
        "process._linkedBinding('local_linked_napi_id').hello";
    v8::Local<v8::Script> script =
        v8::Script::Compile(
            context,
            v8::String::NewFromOneByte(
                isolate_, reinterpret_cast<const uint8_t*>(run_script))
                .ToLocalChecked())
            .ToLocalChecked();
    v8::Local<v8::Value> completion_value =
        script->Run(context).ToLocalChecked();
    CHECK(completion_value->IsArrayBuffer());
    instance_data =
        static_cast<int*>(completion_value.As<v8::ArrayBuffer>()->Data());
    CHECK_NE(instance_data, nullptr);
    CHECK_EQ(*instance_data, 0);
  }

  CHECK_EQ(*instance_data, 1);
  delete instance_data;
}

TEST_F(LinkedBindingTest,
       LocallyDefinedLinkedBindingNapiCallbackInstanceDataTest) {
  const v8::HandleScope handle_scope(isolate_);
  int* instance_data = nullptr;

  {
    const Argv argv;
    Env test_env{handle_scope, argv};

    AddLinkedBinding(*test_env,
                     "local_linked_napi_id_cb",
                     NapiLinkedWithInstanceData,
                     NAPI_VERSION);

    v8::Local<v8::Context> context = isolate_->GetCurrentContext();

    const char* run_script =
        "process._linkedBinding('local_linked_napi_id_cb').hello";
    v8::Local<v8::Script> script =
        v8::Script::Compile(
            context,
            v8::String::NewFromOneByte(
                isolate_, reinterpret_cast<const uint8_t*>(run_script))
                .ToLocalChecked())
            .ToLocalChecked();
    v8::Local<v8::Value> completion_value =
        script->Run(context).ToLocalChecked();
    CHECK(completion_value->IsArrayBuffer());
    instance_data =
        static_cast<int*>(completion_value.As<v8::ArrayBuffer>()->Data());
    CHECK_NE(instance_data, nullptr);
    CHECK_EQ(*instance_data, 0);
  }

  CHECK_EQ(*instance_data, 1);
  delete instance_data;
}

TEST_F(LinkedBindingTest, ManyBindingsTest) {
  const v8::HandleScope handle_scope(isolate_);
  const Argv argv;
  Env test_env{handle_scope, argv};

  int calls = 0;
  AddLinkedBinding(*test_env, "local_linked1", InitializeLocalBinding, &calls);
  AddLinkedBinding(*test_env, "local_linked2", InitializeLocalBinding, &calls);
  AddLinkedBinding(*test_env, "local_linked3", InitializeLocalBinding, &calls);
  AddLinkedBinding(*test_env, local_linked_napi);  // Add a N-API addon as well.
  AddLinkedBinding(*test_env, "local_linked4", InitializeLocalBinding, &calls);
  AddLinkedBinding(*test_env, "local_linked5", InitializeLocalBinding, &calls);

  v8::Local<v8::Context> context = isolate_->GetCurrentContext();

  const char* run_script =
      "for (let i = 1; i <= 5; i++)process._linkedBinding(`local_linked${i}`);"
      "process._linkedBinding('local_linked_napi').hello";
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context,
          v8::String::NewFromOneByte(
              isolate_, reinterpret_cast<const uint8_t*>(run_script))
              .ToLocalChecked())
          .ToLocalChecked();
  v8::Local<v8::Value> completion_value = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8val(isolate_, completion_value);
  CHECK_NOT_NULL(*utf8val);
  CHECK_EQ(strcmp(*utf8val, "world"), 0);
  CHECK_EQ(calls, 5);
}
