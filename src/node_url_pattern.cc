#include "node_url_pattern.h"
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node.h"
#include "node_errors.h"
#include "node_mem-inl.h"
#include "path.h"
#include "util-inl.h"

namespace node::url_pattern {

using v8::Array;
using v8::Context;
using v8::DontDelete;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::MaybeLocal;
using v8::Name;
using v8::NewStringType;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::RegExp;
using v8::String;
using v8::Value;

std::optional<URLPatternRegexProvider::regex_type>
URLPatternRegexProvider::create_instance(std::string_view pattern,
                                         bool ignore_case) {
  auto isolate = Isolate::GetCurrent();
  auto env = Environment::GetCurrent(isolate);
  int flags = RegExp::Flags::kUnicodeSets;
  if (ignore_case) {
    flags |= static_cast<int>(RegExp::Flags::kIgnoreCase);
  }

  Local<String> local_pattern;
  if (!String::NewFromUtf8(
           isolate, pattern.data(), NewStringType::kNormal, pattern.size())
           .ToLocal(&local_pattern)) {
    return std::nullopt;
  }
  Local<RegExp> regexp;
  if (!RegExp::New(
           env->context(), local_pattern, static_cast<RegExp::Flags>(flags))
           .ToLocal(&regexp)) {
    return std::nullopt;
  }
  return Global<RegExp>(isolate, regexp);
}

bool URLPatternRegexProvider::regex_match(std::string_view input,
                                          const regex_type& pattern) {
  auto isolate = Isolate::GetCurrent();
  auto env = Environment::GetCurrent(isolate);
  Local<String> local_input;
  if (!String::NewFromUtf8(
           isolate, input.data(), NewStringType::kNormal, input.size())
           .ToLocal(&local_input)) {
    return false;
  }
  Local<Object> result_object;
  if (!pattern.Get(isolate)
           ->Exec(env->context(), local_input)
           .ToLocal(&result_object)) {
    return false;
  }
  // RegExp::Exec returns null if there is no match.
  return !result_object->IsNull();
}

std::optional<std::vector<std::optional<std::string>>>
URLPatternRegexProvider::regex_search(std::string_view input,
                                      const regex_type& global_pattern) {
  auto isolate = Isolate::GetCurrent();
  auto env = Environment::GetCurrent(isolate);
  Local<String> local_input;
  if (!String::NewFromUtf8(
           isolate, input.data(), NewStringType::kNormal, input.size())
           .ToLocal(&local_input)) {
    return std::nullopt;
  }
  Local<Object> exec_result_object;
  auto pattern = global_pattern.Get(isolate);
  if (!pattern->Exec(env->context(), local_input)
           .ToLocal(&exec_result_object) ||
      exec_result_object->IsNull()) {
    return std::nullopt;
  }
  DCHECK(exec_result_object->IsArray());
  auto exec_result = exec_result_object.As<Array>();
  size_t len = exec_result->Length();
  std::vector<std::optional<std::string>> result;
  result.reserve(len);
  for (size_t i = 1; i < len; i++) {
    Local<Value> entry;
    if (!exec_result->Get(env->context(), i).ToLocal(&entry)) {
      return std::nullopt;
    }

    if (entry->IsUndefined()) {
      result.emplace_back(std::nullopt);
    } else if (entry->IsString()) {
      Utf8Value utf8_entry(isolate, entry.As<String>());
      result.emplace_back(utf8_entry.ToString());
    } else {
      UNREACHABLE("v8::RegExp::Exec return a non-string, non-undefined value.");
    }
  }
  return result;
}

URLPattern::URLPattern(Environment* env,
                       Local<Object> object,
                       ada::url_pattern<URLPatternRegexProvider>&& url_pattern)
    : BaseObject(env, object), url_pattern_(std::move(url_pattern)) {
  MakeWeak();
}

void URLPattern::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("protocol", url_pattern_.get_protocol().size());
  tracker->TrackFieldWithSize("username", url_pattern_.get_username().size());
  tracker->TrackFieldWithSize("password", url_pattern_.get_password().size());
  tracker->TrackFieldWithSize("hostname", url_pattern_.get_hostname().size());
  tracker->TrackFieldWithSize("pathname", url_pattern_.get_pathname().size());
  tracker->TrackFieldWithSize("search", url_pattern_.get_search().size());
  tracker->TrackFieldWithSize("hash", url_pattern_.get_hash().size());
}

void URLPattern::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (!args.IsConstructCall()) {
    THROW_ERR_CONSTRUCT_CALL_REQUIRED(env);
    return;
  }

  // If no arguments are passed, then we parse the empty URLPattern.
  if (args.Length() == 0) {
    auto init = ada::url_pattern_init{};
    auto url_pattern = ada::parse_url_pattern<URLPatternRegexProvider>(init);
    CHECK(url_pattern);
    new URLPattern(env, args.This(), std::move(*url_pattern));
    return;
  }

  std::optional<ada::url_pattern_init> init{};
  std::optional<std::string> input{};
  std::optional<std::string> base_url{};
  std::optional<ada::url_pattern_options> options{};

  // Following patterns are supported:
  // - new URLPattern(input)
  // - new URLPattern(input, baseURL)
  // - new URLPattern(input, options)
  // - new URLPattern(input, baseURL, options)
  if (args[0]->IsString()) {
    BufferValue input_buffer(env->isolate(), args[0]);
    CHECK_NOT_NULL(*input_buffer);
    input = input_buffer.ToString();
  } else if (args[0]->IsObject()) {
    init = URLPatternInit::FromJsObject(env, args[0].As<Object>());
  } else {
    THROW_ERR_INVALID_ARG_TYPE(env, "Input must be an object or a string");
    return;
  }

  // The next argument can be baseURL or options.
  if (args.Length() > 1) {
    if (args[1]->IsString()) {
      BufferValue base_url_buffer(env->isolate(), args[1]);
      CHECK_NOT_NULL(*base_url_buffer);
      base_url = base_url_buffer.ToString();
    } else if (args[1]->IsObject()) {
      CHECK(!options.has_value());
      options = URLPatternOptions::FromJsObject(env, args[1].As<Object>());
      if (!options) {
        THROW_ERR_INVALID_ARG_TYPE(env, "options.ignoreCase must be a boolean");
        return;
      }
    } else {
      THROW_ERR_MISSING_ARGS(env, "baseURL or options must be provided");
      return;
    }

    // Only remaining argument can be options.
    if (args.Length() > 2) {
      if (!args[2]->IsObject()) {
        THROW_ERR_INVALID_ARG_TYPE(env, "options must be an object");
        return;
      }
      CHECK(!options.has_value());
      options = URLPatternOptions::FromJsObject(env, args[2].As<Object>());
      if (!options) {
        THROW_ERR_INVALID_ARG_TYPE(env, "options.ignoreCase must be a boolean");
        return;
      }
    }
  }

  // Either url_pattern_init or input as a string must be provided.
  CHECK_IMPLIES(init.has_value(), !input.has_value());

  std::string_view base_url_view{};
  if (base_url) base_url_view = {base_url->data(), base_url->size()};

  ada::url_pattern_input arg0;
  if (init.has_value()) {
    arg0 = std::move(*init);
  } else {
    arg0 = std::move(*input);
  }

  auto url_pattern = parse_url_pattern<URLPatternRegexProvider>(
      arg0,
      base_url ? &base_url_view : nullptr,
      options.has_value() ? &options.value() : nullptr);

  if (!url_pattern) {
    THROW_ERR_INVALID_URL_PATTERN(env, "Failed to constuct URLPattern");
    return;
  }

  new URLPattern(env, args.This(), std::move(*url_pattern));
}

MaybeLocal<Value> URLPattern::URLPatternInit::ToJsObject(
    Environment* env, const ada::url_pattern_init& init) {
  auto isolate = env->isolate();
  auto context = env->context();
  auto result = Object::New(isolate);
  if (init.protocol) {
    Local<Value> protocol;
    if (!ToV8Value(context, *init.protocol).ToLocal(&protocol) ||
        result->Set(context, env->protocol_string(), protocol).IsNothing()) {
      return {};
    }
  }
  if (init.username) {
    Local<Value> username;
    if (!ToV8Value(context, *init.username).ToLocal(&username) ||
        result->Set(context, env->username_string(), username).IsNothing()) {
      return {};
    }
  }
  if (init.password) {
    Local<Value> password;
    if (!ToV8Value(context, *init.password).ToLocal(&password) ||
        result->Set(context, env->password_string(), password).IsNothing()) {
      return {};
    }
  }
  if (init.hostname) {
    Local<Value> hostname;
    if (!ToV8Value(context, *init.hostname).ToLocal(&hostname) ||
        result->Set(context, env->hostname_string(), hostname).IsNothing()) {
      return {};
    }
  }
  if (init.port) {
    Local<Value> port;
    if (!ToV8Value(context, *init.port).ToLocal(&port) ||
        result->Set(context, env->port_string(), port).IsNothing()) {
      return {};
    }
  }
  if (init.pathname) {
    Local<Value> pathname;
    if (!ToV8Value(context, *init.pathname).ToLocal(&pathname) ||
        result->Set(context, env->pathname_string(), pathname).IsNothing()) {
      return {};
    }
  }
  if (init.search) {
    Local<Value> search;
    if (!ToV8Value(context, *init.search).ToLocal(&search) ||
        result->Set(context, env->search_string(), search).IsNothing()) {
      return {};
    }
  }
  if (init.hash) {
    Local<Value> hash;
    if (!ToV8Value(context, *init.hash).ToLocal(&hash) ||
        result->Set(context, env->hash_string(), hash).IsNothing()) {
      return {};
    }
  }
  if (init.base_url) {
    Local<Value> base_url;
    if (!ToV8Value(context, *init.base_url).ToLocal(&base_url) ||
        result->Set(context, env->base_url_string(), base_url).IsNothing()) {
      return {};
    }
  }
  return result;
}

ada::url_pattern_init URLPattern::URLPatternInit::FromJsObject(
    Environment* env, Local<Object> obj) {
  ada::url_pattern_init init{};
  Local<String> components[] = {
      env->protocol_string(),
      env->username_string(),
      env->password_string(),
      env->hostname_string(),
      env->port_string(),
      env->pathname_string(),
      env->search_string(),
      env->hash_string(),
      env->base_url_string(),
  };
  auto isolate = env->isolate();
  const auto set_parameter = [&](std::string_view key, std::string_view value) {
    if (key == "protocol") {
      init.protocol = std::string(value);
    } else if (key == "username") {
      init.username = std::string(value);
    } else if (key == "password") {
      init.password = std::string(value);
    } else if (key == "hostname") {
      init.hostname = std::string(value);
    } else if (key == "port") {
      init.port = std::string(value);
    } else if (key == "pathname") {
      init.pathname = std::string(value);
    } else if (key == "search") {
      init.search = std::string(value);
    } else if (key == "hash") {
      init.hash = std::string(value);
    } else if (key == "baseURL") {
      init.base_url = std::string(value);
    }
  };
  Local<Value> value;
  for (const auto& component : components) {
    Utf8Value key(isolate, component);
    if (obj->Get(env->context(), component).ToLocal(&value)) {
      if (value->IsString()) {
        Utf8Value utf8_value(isolate, value);
        set_parameter(key.ToStringView(), utf8_value.ToStringView());
      }
    }
  }
  return init;
}

MaybeLocal<Object> URLPattern::URLPatternComponentResult::ToJSObject(
    Environment* env, const ada::url_pattern_component_result& result) {
  auto isolate = env->isolate();
  auto context = env->context();
  auto parsed_group = Object::New(isolate);
  for (const auto& [group_key, group_value] : result.groups) {
    Local<String> key;
    if (!String::NewFromUtf8(isolate,
                             group_key.c_str(),
                             NewStringType::kNormal,
                             group_key.size())
             .ToLocal(&key)) {
      return {};
    }
    Local<Value> value;
    if (group_value) {
      if (!ToV8Value(env->context(), *group_value).ToLocal(&value)) {
        return {};
      }
    } else {
      value = Undefined(isolate);
    }
    USE(parsed_group->Set(context, key, value));
  }
  Local<Value> input;
  if (!ToV8Value(env->context(), result.input).ToLocal(&input)) {
    return {};
  }
  Local<Name> names[] = {env->input_string(), env->groups_string()};
  Local<Value> values[] = {input, parsed_group};
  DCHECK_EQ(arraysize(names), arraysize(values));
  return Object::New(
      isolate, Object::New(isolate), names, values, arraysize(names));
}

MaybeLocal<Value> URLPattern::URLPatternResult::ToJSValue(
    Environment* env, const ada::url_pattern_result& result) {
  auto isolate = env->isolate();
  Local<Name> names[] = {
      env->inputs_string(),
      env->protocol_string(),
      env->username_string(),
      env->password_string(),
      env->hostname_string(),
      env->port_string(),
      env->pathname_string(),
      env->search_string(),
      env->hash_string(),
  };
  LocalVector<Value> inputs(isolate, result.inputs.size());
  size_t index = 0;
  for (auto& input : result.inputs) {
    if (std::holds_alternative<std::string_view>(input)) {
      auto input_str = std::get<std::string_view>(input);
      if (!ToV8Value(env->context(), input_str).ToLocal(&inputs[index])) {
        return {};
      }
    } else {
      DCHECK(std::holds_alternative<ada::url_pattern_init>(input));
      auto init = std::get<ada::url_pattern_init>(input);
      if (!URLPatternInit::ToJsObject(env, init).ToLocal(&inputs[index])) {
        return {};
      }
    }
    index++;
  }
  LocalVector<Value> values(isolate, arraysize(names));
  values[0] = Array::New(isolate, inputs.data(), inputs.size());
  if (!URLPatternComponentResult::ToJSObject(env, result.protocol)
           .ToLocal(&values[1])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.username)
           .ToLocal(&values[2])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.password)
           .ToLocal(&values[3])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.hostname)
           .ToLocal(&values[4])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.port)
           .ToLocal(&values[5])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.pathname)
           .ToLocal(&values[6])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.search)
           .ToLocal(&values[7])) {
    return {};
  }
  if (!URLPatternComponentResult::ToJSObject(env, result.hash)
           .ToLocal(&values[8])) {
    return {};
  }
  return Object::New(
      isolate, Object::New(isolate), names, values.data(), values.size());
}

std::optional<ada::url_pattern_options>
URLPattern::URLPatternOptions::FromJsObject(Environment* env,
                                            Local<Object> obj) {
  ada::url_pattern_options options{};
  Local<Value> ignore_case;
  if (obj->Get(env->context(), env->ignore_case_string())
          .ToLocal(&ignore_case)) {
    if (!ignore_case->IsBoolean()) {
      return std::nullopt;
    }
    options.ignore_case = ignore_case->IsTrue();
  }
  return options;
}

MaybeLocal<Value> URLPattern::Hash() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_hash());
}

MaybeLocal<Value> URLPattern::Hostname() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_hostname());
}

MaybeLocal<Value> URLPattern::Password() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_password());
}

MaybeLocal<Value> URLPattern::Pathname() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_pathname());
}

MaybeLocal<Value> URLPattern::Port() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_port());
}

MaybeLocal<Value> URLPattern::Protocol() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_protocol());
}

MaybeLocal<Value> URLPattern::Search() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_search());
}

MaybeLocal<Value> URLPattern::Username() const {
  auto context = env()->context();
  return ToV8Value(context, url_pattern_.get_username());
}

bool URLPattern::HasRegExpGroups() const {
  return url_pattern_.has_regexp_groups();
}

// Instance methods

MaybeLocal<Value> URLPattern::Exec(Environment* env,
                                   const ada::url_pattern_input& input,
                                   std::optional<std::string_view>& baseURL) {
  if (auto result = url_pattern_.exec(input, baseURL ? &*baseURL : nullptr)) {
    if (result->has_value()) {
      return URLPatternResult::ToJSValue(env, result->value());
    }
    return Null(env->isolate());
  }
  return {};
}

bool URLPattern::Test(Environment* env,
                      const ada::url_pattern_input& input,
                      std::optional<std::string_view>& baseURL) {
  if (auto result = url_pattern_.test(input, baseURL ? &*baseURL : nullptr)) {
    return *result;
  }
  THROW_ERR_OPERATION_FAILED(env, "Failed to test URLPattern");
  return false;
}

// V8 Methods

void URLPattern::Exec(const FunctionCallbackInfo<Value>& args) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, args.This());
  auto env = Environment::GetCurrent(args);

  ada::url_pattern_input input;
  std::optional<std::string> baseURL{};
  std::string input_base;
  if (args.Length() == 0) {
    input = ada::url_pattern_init{};
  } else if (args[0]->IsString()) {
    Utf8Value input_value(env->isolate(), args[0].As<String>());
    input_base = input_value.ToString();
    input = std::string_view(input_base);
  } else if (args[0]->IsObject()) {
    input = URLPatternInit::FromJsObject(env, args[0].As<Object>());
  } else {
    THROW_ERR_INVALID_ARG_TYPE(
        env, "URLPattern input needs to be a string or an object");
    return;
  }

  if (args.Length() > 1) {
    if (!args[1]->IsString()) {
      THROW_ERR_INVALID_ARG_TYPE(env, "baseURL must be a string");
      return;
    }
    Utf8Value base_url_value(env->isolate(), args[1].As<String>());
    baseURL = base_url_value.ToStringView();
  }

  Local<Value> result;
  std::optional<std::string_view> baseURL_opt =
      baseURL ? std::optional<std::string_view>(*baseURL) : std::nullopt;
  if (!url_pattern->Exec(env, input, baseURL_opt).ToLocal(&result)) {
    THROW_ERR_OPERATION_FAILED(env, "Failed to exec URLPattern");
    return;
  }
  args.GetReturnValue().Set(result);
}

void URLPattern::Test(const FunctionCallbackInfo<Value>& args) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, args.This());
  auto env = Environment::GetCurrent(args);

  ada::url_pattern_input input;
  std::optional<std::string> baseURL{};
  std::string input_base;
  if (args.Length() == 0) {
    input = ada::url_pattern_init{};
  } else if (args[0]->IsString()) {
    Utf8Value input_value(env->isolate(), args[0].As<String>());
    input_base = input_value.ToString();
    input = std::string_view(input_base);
  } else if (args[0]->IsObject()) {
    input = URLPatternInit::FromJsObject(env, args[0].As<Object>());
  } else {
    THROW_ERR_INVALID_ARG_TYPE(
        env, "URLPattern input needs to be a string or an object");
    return;
  }

  if (args.Length() > 1) {
    if (!args[1]->IsString()) {
      THROW_ERR_INVALID_ARG_TYPE(env, "baseURL must be a string");
      return;
    }
    Utf8Value base_url_value(env->isolate(), args[1].As<String>());
    baseURL = base_url_value.ToStringView();
  }

  std::optional<std::string_view> baseURL_opt =
      baseURL ? std::optional<std::string_view>(*baseURL) : std::nullopt;
  args.GetReturnValue().Set(url_pattern->Test(env, input, baseURL_opt));
}

void URLPattern::Protocol(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Protocol().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Username(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Username().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Password(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Password().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Hostname(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Hostname().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Port(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Port().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Pathname(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Pathname().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Search(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Search().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::Hash(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (url_pattern->Hash().ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
}

void URLPattern::HasRegexpGroups(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  info.GetReturnValue().Set(url_pattern->HasRegExpGroups());
}

static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(URLPattern::New);
  registry->Register(URLPattern::Protocol);
  registry->Register(URLPattern::Username);
  registry->Register(URLPattern::Password);
  registry->Register(URLPattern::Hostname);
  registry->Register(URLPattern::Port);
  registry->Register(URLPattern::Pathname);
  registry->Register(URLPattern::Search);
  registry->Register(URLPattern::Hash);
  registry->Register(URLPattern::HasRegexpGroups);
  registry->Register(URLPattern::Exec);
  registry->Register(URLPattern::Test);
}

static void Initialize(Local<Object> target,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();
  auto attributes = static_cast<PropertyAttribute>(ReadOnly | DontDelete);
  auto ctor_tmpl = NewFunctionTemplate(isolate, URLPattern::New);
  auto instance_template = ctor_tmpl->InstanceTemplate();
  auto prototype_template = ctor_tmpl->PrototypeTemplate();
  ctor_tmpl->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "URLPattern"));

  instance_template->SetInternalFieldCount(URLPattern::kInternalFieldCount);
  prototype_template->SetAccessorProperty(
      env->protocol_string(),
      FunctionTemplate::New(isolate, URLPattern::Protocol),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->username_string(),
      FunctionTemplate::New(isolate, URLPattern::Username),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->password_string(),
      FunctionTemplate::New(isolate, URLPattern::Password),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->hostname_string(),
      FunctionTemplate::New(isolate, URLPattern::Hostname),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->port_string(),
      FunctionTemplate::New(isolate, URLPattern::Port),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->pathname_string(),
      FunctionTemplate::New(isolate, URLPattern::Pathname),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->search_string(),
      FunctionTemplate::New(isolate, URLPattern::Search),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->hash_string(),
      FunctionTemplate::New(isolate, URLPattern::Hash),
      Local<FunctionTemplate>(),
      attributes);

  prototype_template->SetAccessorProperty(
      env->has_regexp_groups_string(),
      FunctionTemplate::New(isolate, URLPattern::HasRegexpGroups),
      Local<FunctionTemplate>(),
      attributes);

  SetProtoMethodNoSideEffect(isolate, ctor_tmpl, "exec", URLPattern::Exec);
  SetProtoMethodNoSideEffect(isolate, ctor_tmpl, "test", URLPattern::Test);
  SetConstructorFunction(context, target, "URLPattern", ctor_tmpl);
}

}  // namespace node::url_pattern

NODE_BINDING_CONTEXT_AWARE_INTERNAL(url_pattern, node::url_pattern::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(url_pattern,
                                node::url_pattern::RegisterExternalReferences)
