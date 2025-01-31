#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_i18n.h"
#include "node_process-inl.h"

#include <time.h>  // tzset(), _tzset()
#include <optional>

namespace node {
using v8::Array;
using v8::Boolean;
using v8::Context;
using v8::DontDelete;
using v8::DontEnum;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Intercepted;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Name;
using v8::NamedPropertyHandlerConfiguration;
using v8::NewStringType;
using v8::Nothing;
using v8::Object;
using v8::ObjectTemplate;
using v8::PropertyCallbackInfo;
using v8::PropertyDescriptor;
using v8::PropertyHandlerFlags;
using v8::ReadOnly;
using v8::String;
using v8::Value;

class RealEnvStore final : public KVStore {
 public:
  MaybeLocal<String> Get(Isolate* isolate, Local<String> key) const override;
  std::optional<std::string> Get(const char* key) const override;
  void Set(Isolate* isolate, Local<String> key, Local<String> value) override;
  int32_t Query(Isolate* isolate, Local<String> key) const override;
  int32_t Query(const char* key) const override;
  void Delete(Isolate* isolate, Local<String> key) override;
  Local<Array> Enumerate(Isolate* isolate) const override;
};

class MapKVStore final : public KVStore {
 public:
  MaybeLocal<String> Get(Isolate* isolate, Local<String> key) const override;
  std::optional<std::string> Get(const char* key) const override;
  void Set(Isolate* isolate, Local<String> key, Local<String> value) override;
  int32_t Query(Isolate* isolate, Local<String> key) const override;
  int32_t Query(const char* key) const override;
  void Delete(Isolate* isolate, Local<String> key) override;
  Local<Array> Enumerate(Isolate* isolate) const override;

  std::shared_ptr<KVStore> Clone(Isolate* isolate) const override;

  MapKVStore() = default;
  MapKVStore(const MapKVStore& other) : KVStore(), map_(other.map_) {}

 private:
  mutable Mutex mutex_;
  std::unordered_map<std::string, std::string> map_;
};

namespace per_process {
Mutex env_var_mutex;
std::shared_ptr<KVStore> system_environment = std::make_shared<RealEnvStore>();
}  // namespace per_process

template <typename T>
void DateTimeConfigurationChangeNotification(
    Isolate* isolate,
    const T& key,
    const char* val = nullptr) {
  if (key.length() == 2 && key[0] == 'T' && key[1] == 'Z') {
#ifdef __POSIX__
    tzset();
    isolate->DateTimeConfigurationChangeNotification(
        Isolate::TimeZoneDetection::kRedetect);
#else
    _tzset();

# if defined(NODE_HAVE_I18N_SUPPORT)
    isolate->DateTimeConfigurationChangeNotification(
        Isolate::TimeZoneDetection::kSkip);

    // On windows, the TZ environment is not supported out of the box.
    // By default, v8 will only be able to detect the system configured
    // timezone. This supports using the TZ environment variable to set
    // the default timezone instead.
    if (val != nullptr) i18n::SetDefaultTimeZone(val);
# else
    isolate->DateTimeConfigurationChangeNotification(
        Isolate::TimeZoneDetection::kRedetect);
# endif
#endif
  }
}

std::optional<std::string> RealEnvStore::Get(const char* key) const {
  Mutex::ScopedLock lock(per_process::env_var_mutex);

  size_t init_sz = 256;
  MaybeStackBuffer<char, 256> val;
  int ret = uv_os_getenv(key, *val, &init_sz);

  if (ret == UV_ENOBUFS) {
    // Buffer is not large enough, reallocate to the updated init_sz
    // and fetch env value again.
    val.AllocateSufficientStorage(init_sz);
    ret = uv_os_getenv(key, *val, &init_sz);
  }

  if (ret >= 0) {  // Env key value fetch success.
    return std::string(*val, init_sz);
  }

  return std::nullopt;
}

MaybeLocal<String> RealEnvStore::Get(Isolate* isolate,
                                     Local<String> property) const {
  node::Utf8Value key(isolate, property);
  std::optional<std::string> value = Get(*key);

  if (value.has_value()) {
    std::string val = value.value();
    return String::NewFromUtf8(
        isolate, val.data(), NewStringType::kNormal, val.size());
  }

  return MaybeLocal<String>();
}

void RealEnvStore::Set(Isolate* isolate,
                       Local<String> property,
                       Local<String> value) {
  Mutex::ScopedLock lock(per_process::env_var_mutex);

  node::Utf8Value key(isolate, property);
  node::Utf8Value val(isolate, value);

#ifdef _WIN32
  if (key.length() > 0 && key[0] == '=') return;
#endif
  uv_os_setenv(*key, *val);
  DateTimeConfigurationChangeNotification(isolate, key, *val);
}

int32_t RealEnvStore::Query(const char* key) const {
  Mutex::ScopedLock lock(per_process::env_var_mutex);

  char val[2];
  size_t init_sz = sizeof(val);
  int ret = uv_os_getenv(key, val, &init_sz);

  if (ret == UV_ENOENT) {
    return -1;
  }

#ifdef _WIN32
  if (key[0] == '=') {
    return static_cast<int32_t>(ReadOnly) |
           static_cast<int32_t>(DontDelete) |
           static_cast<int32_t>(DontEnum);
  }
#endif

  return 0;
}

int32_t RealEnvStore::Query(Isolate* isolate, Local<String> property) const {
  node::Utf8Value key(isolate, property);
  return Query(*key);
}

void RealEnvStore::Delete(Isolate* isolate, Local<String> property) {
  Mutex::ScopedLock lock(per_process::env_var_mutex);

  node::Utf8Value key(isolate, property);
  uv_os_unsetenv(*key);
  DateTimeConfigurationChangeNotification(isolate, key);
}

Local<Array> RealEnvStore::Enumerate(Isolate* isolate) const {
  Mutex::ScopedLock lock(per_process::env_var_mutex);
  uv_env_item_t* items;
  int count;

  auto cleanup = OnScopeLeave([&]() { uv_os_free_environ(items, count); });
  CHECK_EQ(uv_os_environ(&items, &count), 0);

  MaybeStackBuffer<Local<Value>, 256> env_v(count);
  int env_v_index = 0;
  for (int i = 0; i < count; i++) {
#ifdef _WIN32
    // If the key starts with '=' it is a hidden environment variable.
    if (items[i].name[0] == '=') continue;
#endif
    MaybeLocal<String> str = String::NewFromUtf8(isolate, items[i].name);
    if (str.IsEmpty()) {
      isolate->ThrowException(ERR_STRING_TOO_LONG(isolate));
      return Local<Array>();
    }
    env_v[env_v_index++] = str.ToLocalChecked();
  }

  return Array::New(isolate, env_v.out(), env_v_index);
}

std::shared_ptr<KVStore> KVStore::Clone(Isolate* isolate) const {
  HandleScope handle_scope(isolate);
  Local<Context> context = isolate->GetCurrentContext();

  std::shared_ptr<KVStore> copy = KVStore::CreateMapKVStore();
  Local<Array> keys = Enumerate(isolate);
  uint32_t keys_length = keys->Length();
  for (uint32_t i = 0; i < keys_length; i++) {
    Local<Value> key = keys->Get(context, i).ToLocalChecked();
    CHECK(key->IsString());
    copy->Set(isolate,
              key.As<String>(),
              Get(isolate, key.As<String>()).ToLocalChecked());
  }
  return copy;
}

std::optional<std::string> MapKVStore::Get(const char* key) const {
  Mutex::ScopedLock lock(mutex_);
  auto it = map_.find(key);
  return it == map_.end() ? std::nullopt : std::make_optional(it->second);
}

MaybeLocal<String> MapKVStore::Get(Isolate* isolate, Local<String> key) const {
  Utf8Value str(isolate, key);
  std::optional<std::string> value = Get(*str);
  if (!value.has_value()) return MaybeLocal<String>();
  std::string val = value.value();
  return String::NewFromUtf8(
      isolate, val.data(), NewStringType::kNormal, val.size());
}

void MapKVStore::Set(Isolate* isolate, Local<String> key, Local<String> value) {
  Mutex::ScopedLock lock(mutex_);
  Utf8Value key_str(isolate, key);
  Utf8Value value_str(isolate, value);
  if (*key_str != nullptr && key_str.length() > 0 && *value_str != nullptr) {
    map_[std::string(*key_str, key_str.length())] =
        std::string(*value_str, value_str.length());
  }
}

int32_t MapKVStore::Query(const char* key) const {
  Mutex::ScopedLock lock(mutex_);
  return map_.find(key) == map_.end() ? -1 : 0;
}

int32_t MapKVStore::Query(Isolate* isolate, Local<String> key) const {
  Utf8Value str(isolate, key);
  return Query(*str);
}

void MapKVStore::Delete(Isolate* isolate, Local<String> key) {
  Mutex::ScopedLock lock(mutex_);
  Utf8Value str(isolate, key);
  map_.erase(std::string(*str, str.length()));
}

Local<Array> MapKVStore::Enumerate(Isolate* isolate) const {
  Mutex::ScopedLock lock(mutex_);
  LocalVector<Value> values(isolate);
  values.reserve(map_.size());
  for (const auto& pair : map_) {
    values.emplace_back(
        String::NewFromUtf8(isolate, pair.first.data(),
                            NewStringType::kNormal, pair.first.size())
            .ToLocalChecked());
  }
  return Array::New(isolate, values.data(), values.size());
}

std::shared_ptr<KVStore> MapKVStore::Clone(Isolate* isolate) const {
  return std::make_shared<MapKVStore>(*this);
}

std::shared_ptr<KVStore> KVStore::CreateMapKVStore() {
  return std::make_shared<MapKVStore>();
}

Maybe<void> KVStore::AssignFromObject(Local<Context> context,
                                      Local<Object> entries) {
  Isolate* isolate = context->GetIsolate();
  HandleScope handle_scope(isolate);
  Local<Array> keys;
  if (!entries->GetOwnPropertyNames(context).ToLocal(&keys))
    return Nothing<void>();
  uint32_t keys_length = keys->Length();
  for (uint32_t i = 0; i < keys_length; i++) {
    Local<Value> key;
    if (!keys->Get(context, i).ToLocal(&key)) return Nothing<void>();
    if (!key->IsString()) continue;

    Local<Value> value;
    Local<String> value_string;
    if (!entries->Get(context, key).ToLocal(&value) ||
        !value->ToString(context).ToLocal(&value_string)) {
      return Nothing<void>();
    }

    Set(isolate, key.As<String>(), value_string);
  }
  return JustVoid();
}

// TODO(bnoordhuis) Not super efficient but called infrequently. Not worth
// the trouble yet of specializing for RealEnvStore and MapKVStore.
Maybe<void> KVStore::AssignToObject(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> object) {
  HandleScope scope(isolate);
  Local<Array> keys = Enumerate(isolate);
  uint32_t keys_length = keys->Length();
  for (uint32_t i = 0; i < keys_length; i++) {
    Local<Value> key;
    Local<String> value;
    bool ok = keys->Get(context, i).ToLocal(&key);
    ok = ok && key->IsString();
    ok = ok && Get(isolate, key.As<String>()).ToLocal(&value);
    ok = ok && object->Set(context, key, value).To(&ok);
    if (!ok) return Nothing<void>();
  }
  return JustVoid();
}

struct TraceEnvVarOptions {
  bool print_message : 1 = 0;
  bool print_js_stack : 1 = 0;
  bool print_native_stack : 1 = 0;
};

template <typename... Args>
inline void TraceEnvVarImpl(Environment* env,
                            TraceEnvVarOptions options,
                            const char* format,
                            Args&&... args) {
  if (options.print_message) {
    fprintf(stderr, format, std::forward<Args>(args)...);
  }
  if (options.print_native_stack) {
    DumpNativeBacktrace(stderr);
  }
  if (options.print_js_stack) {
    DumpJavaScriptBacktrace(stderr);
  }
}

TraceEnvVarOptions GetTraceEnvVarOptions(Environment* env) {
  TraceEnvVarOptions options;
  auto cli_options = env != nullptr
                         ? env->options()
                         : per_process::cli_options->per_isolate->per_env;
  if (cli_options->trace_env) {
    options.print_message = 1;
  };
  if (cli_options->trace_env_js_stack) {
    options.print_js_stack = 1;
  };
  if (cli_options->trace_env_native_stack) {
    options.print_native_stack = 1;
  };
  return options;
}

void TraceEnvVar(Environment* env, const char* message) {
  TraceEnvVarImpl(
      env, GetTraceEnvVarOptions(env), "[--trace-env] %s\n", message);
}

void TraceEnvVar(Environment* env, const char* message, const char* key) {
  TraceEnvVarImpl(env,
                  GetTraceEnvVarOptions(env),
                  "[--trace-env] %s \"%s\"\n",
                  message,
                  key);
}

void TraceEnvVar(Environment* env,
                 const char* message,
                 v8::Local<v8::String> key) {
  TraceEnvVarOptions options = GetTraceEnvVarOptions(env);
  if (options.print_message) {
    Utf8Value key_utf8(env->isolate(), key);
    TraceEnvVarImpl(env,
                    options,
                    "[--trace-env] %s \"%.*s\"\n",
                    message,
                    static_cast<int>(key_utf8.length()),
                    key_utf8.out());
  }
}

static Intercepted EnvGetter(Local<Name> property,
                             const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(env->has_run_bootstrapping_code());
  if (property->IsSymbol()) {
    info.GetReturnValue().SetUndefined();
    return Intercepted::kYes;
  }
  CHECK(property->IsString());
  MaybeLocal<String> value_string =
      env->env_vars()->Get(env->isolate(), property.As<String>());

  bool has_env = !value_string.IsEmpty();
  TraceEnvVar(env, "get", property.As<String>());

  if (has_env) {
    info.GetReturnValue().Set(value_string.ToLocalChecked());
    return Intercepted::kYes;
  }
  return Intercepted::kNo;
}

static Intercepted EnvSetter(Local<Name> property,
                             Local<Value> value,
                             const PropertyCallbackInfo<void>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(env->has_run_bootstrapping_code());
  // calling env->EmitProcessEnvWarning() sets a variable indicating that
  // warnings have been emitted. It should be called last after other
  // conditions leading to a warning have been met.
  if (env->options()->pending_deprecation && !value->IsString() &&
      !value->IsNumber() && !value->IsBoolean() &&
      env->EmitProcessEnvWarning()) {
    if (ProcessEmitDeprecationWarning(
            env,
            "Assigning any value other than a string, number, or boolean to a "
            "process.env property is deprecated. Please make sure to convert "
            "the "
            "value to a string before setting process.env with it.",
            "DEP0104")
            .IsNothing()) {
      return Intercepted::kNo;
    }
  }

  Local<String> key;
  Local<String> value_string;
  if (!property->ToString(env->context()).ToLocal(&key) ||
      !value->ToString(env->context()).ToLocal(&value_string)) {
    return Intercepted::kNo;
  }

  env->env_vars()->Set(env->isolate(), key, value_string);
  TraceEnvVar(env, "set", key);

  return Intercepted::kYes;
}

static Intercepted EnvQuery(Local<Name> property,
                            const PropertyCallbackInfo<Integer>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(env->has_run_bootstrapping_code());
  if (property->IsString()) {
    int32_t rc = env->env_vars()->Query(env->isolate(), property.As<String>());
    bool has_env = (rc != -1);
    TraceEnvVar(env, "query", property.As<String>());
    if (has_env) {
      // Return attributes for the property.
      info.GetReturnValue().Set(v8::None);
      return Intercepted::kYes;
    }
  }
  return Intercepted::kNo;
}

static Intercepted EnvDeleter(Local<Name> property,
                              const PropertyCallbackInfo<Boolean>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(env->has_run_bootstrapping_code());
  if (property->IsString()) {
    env->env_vars()->Delete(env->isolate(), property.As<String>());

    TraceEnvVar(env, "delete", property.As<String>());
  }

  // process.env never has non-configurable properties, so always
  // return true like the tc39 delete operator.
  info.GetReturnValue().Set(true);
  return Intercepted::kYes;
}

static void EnvEnumerator(const PropertyCallbackInfo<Array>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(env->has_run_bootstrapping_code());

  TraceEnvVar(env, "enumerate environment variables");

  info.GetReturnValue().Set(
      env->env_vars()->Enumerate(env->isolate()));
}

static Intercepted EnvDefiner(Local<Name> property,
                              const PropertyDescriptor& desc,
                              const PropertyCallbackInfo<void>& info) {
  Environment* env = Environment::GetCurrent(info);
  if (desc.has_value()) {
    if (!desc.has_writable() ||
        !desc.has_enumerable() ||
        !desc.has_configurable()) {
      THROW_ERR_INVALID_OBJECT_DEFINE_PROPERTY(env,
                                               "'process.env' only accepts a "
                                               "configurable, writable,"
                                               " and enumerable "
                                               "data descriptor");
      return Intercepted::kYes;
    } else if (!desc.configurable() ||
               !desc.enumerable() ||
               !desc.writable()) {
      THROW_ERR_INVALID_OBJECT_DEFINE_PROPERTY(env,
                                               "'process.env' only accepts a "
                                               "configurable, writable,"
                                               " and enumerable "
                                               "data descriptor");
      return Intercepted::kYes;
    } else {
      return EnvSetter(property, desc.value(), info);
    }
  } else if (desc.has_get() || desc.has_set()) {
    // we don't accept a getter/setter in 'process.env'
    THROW_ERR_INVALID_OBJECT_DEFINE_PROPERTY(env,
                                             "'process.env' does not accept an"
                                             " accessor(getter/setter)"
                                             " descriptor");
    return Intercepted::kYes;
  } else {
    THROW_ERR_INVALID_OBJECT_DEFINE_PROPERTY(env,
                                             "'process.env' only accepts a "
                                             "configurable, writable,"
                                             " and enumerable "
                                             "data descriptor");
    return Intercepted::kYes;
  }
}

void CreateEnvProxyTemplate(IsolateData* isolate_data) {
  Isolate* isolate = isolate_data->isolate();
  HandleScope scope(isolate);
  if (!isolate_data->env_proxy_template().IsEmpty()) return;
  Local<FunctionTemplate> env_proxy_ctor_template =
      FunctionTemplate::New(isolate);
  Local<ObjectTemplate> env_proxy_template =
      ObjectTemplate::New(isolate, env_proxy_ctor_template);
  env_proxy_template->SetHandler(NamedPropertyHandlerConfiguration(
      EnvGetter,
      EnvSetter,
      EnvQuery,
      EnvDeleter,
      EnvEnumerator,
      EnvDefiner,
      nullptr,
      Local<Value>(),
      PropertyHandlerFlags::kHasNoSideEffect));
  isolate_data->set_env_proxy_template(env_proxy_template);
  isolate_data->set_env_proxy_ctor_template(env_proxy_ctor_template);
}

void RegisterEnvVarExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(EnvGetter);
  registry->Register(EnvSetter);
  registry->Register(EnvQuery);
  registry->Register(EnvDeleter);
  registry->Register(EnvEnumerator);
  registry->Register(EnvDefiner);
}
}  // namespace node

NODE_BINDING_EXTERNAL_REFERENCE(env_var, node::RegisterEnvVarExternalReferences)
