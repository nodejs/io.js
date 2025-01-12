#include "node_url_pattern.h"
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
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
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::MaybeLocal;
using v8::Name;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::String;
using v8::Value;

URLPattern::URLPattern(Environment* env,
                       Local<Object> object,
                       ada::url_pattern&& url_pattern)
    : BaseObject(env, object), url_pattern_(std::move(url_pattern)) {
  MakeWeak();
}

void URLPattern::MemoryInfo(MemoryTracker* tracker) const {
  // TODO(anonrig): Implement this.
}

void URLPattern::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args.IsConstructCall());

  // If no arguments are passed, then we parse the empty URLPattern.
  if (args.Length() == 0) {
    auto url_pattern = parse_url_pattern(ada::url_pattern_init{});
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
    env->ThrowTypeError("input must be an object or a string.");
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
      options = URLPatternOptions::FromJsObject(env, args[0].As<Object>());
    } else {
      env->ThrowTypeError("baseURL or options must be provided");
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
    }
  }

  // Either url_pattern_init or input as a string must be provided.
  CHECK_IMPLIES(init.has_value(), !input.has_value());

  tl::expected<ada::url_pattern, ada::errors> url_pattern;

  std::string_view base_url_view{};
  if (base_url) base_url_view = {base_url->data(), base_url->size()};
  if (init.has_value()) {
    url_pattern =
        parse_url_pattern(*init,
                          base_url ? &base_url_view : nullptr,
                          options.has_value() ? &options.value() : nullptr);
  } else {
    url_pattern =
        parse_url_pattern(*input,
                          base_url ? &base_url_view : nullptr,
                          options.has_value() ? &options.value() : nullptr);
  }

  if (!url_pattern) {
    env->ThrowTypeError("Failed to construct URLPattern");
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
    USE(result->Set(context,
                    env->protocol_string(),
                    ToV8Value(context, *init.protocol).ToLocalChecked()));
  }
  if (init.username) {
    USE(result->Set(context,
                    env->username_string(),
                    ToV8Value(context, *init.username).ToLocalChecked()));
  }
  if (init.password) {
    USE(result->Set(context,
                    env->password_string(),
                    ToV8Value(context, *init.password).ToLocalChecked()));
  }
  if (init.hostname) {
    USE(result->Set(context,
                    env->hostname_string(),
                    ToV8Value(context, *init.hostname).ToLocalChecked()));
  }
  if (init.port) {
    USE(result->Set(context,
                    env->port_string(),
                    ToV8Value(context, *init.port).ToLocalChecked()));
  }
  if (init.pathname) {
    USE(result->Set(context,
                    env->pathname_string(),
                    ToV8Value(context, *init.pathname).ToLocalChecked()));
  }
  if (init.search) {
    USE(result->Set(context,
                    env->search_string(),
                    ToV8Value(context, *init.search).ToLocalChecked()));
  }
  if (init.hash) {
    USE(result->Set(context,
                    env->hash_string(),
                    ToV8Value(context, *init.hash).ToLocalChecked()));
  }
  if (init.base_url) {
    USE(result->Set(context,
                    env->base_url_string(),
                    ToV8Value(context, *init.base_url).ToLocalChecked()));
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
  Local<Value> value;
  auto isolate = env->isolate();
  auto set_parameter = [&](Local<String> component,
                           std::string_view utf8_value) {
    // TODO(anonrig): Optimization opportunity.
    // Get rid of calling env->xxx_string() for every component match.
    if (component == env->protocol_string()) {
      init.protocol = std::string(utf8_value);
    } else if (component == env->username_string()) {
      init.username = std::string(utf8_value);
    } else if (component == env->password_string()) {
      init.password = std::string(utf8_value);
    } else if (component == env->hostname_string()) {
      init.hostname = std::string(utf8_value);
    } else if (component == env->port_string()) {
      init.port = std::string(utf8_value);
    } else if (component == env->pathname_string()) {
      init.pathname = std::string(utf8_value);
    } else if (component == env->search_string()) {
      init.search = std::string(utf8_value);
    } else if (component == env->hash_string()) {
      init.hash = std::string(utf8_value);
    } else if (component == env->base_url_string()) {
      init.base_url = std::string(utf8_value);
    }
  };
  for (const auto& component : components) {
    if (obj->Get(env->context(), component).ToLocal(&value)) {
      if (value->IsString()) {
        Utf8Value utf8_value(isolate, value);
        set_parameter(component, utf8_value.ToStringView());
      }
    }
  }
  return init;
}

Local<Object> URLPattern::URLPatternComponentResult::ToJSObject(
    Environment* env, const ada::url_pattern_component_result& result) {
  auto isolate = env->isolate();
  auto context = env->context();
  const auto parse_groups =
      [&](std::unordered_map<std::string, std::string> groups) {
        // TODO(@anonrig): Use predefined object constructor to avoid calling
        // Set on each key.
        auto res = Object::New(isolate);
        for (const auto& group : groups) {
          USE(res->Set(context,
                       ToV8Value(context, group.first).ToLocalChecked(),
                       ToV8Value(context, group.second).ToLocalChecked()));
        }
        return res;
      };
  Local<Name> names[] = {env->input_string(), env->groups_string()};
  Local<Value> values[] = {
      ToV8Value(env->context(), result.input).ToLocalChecked(),
      parse_groups(result.groups),
  };
  DCHECK_EQ(arraysize(names), arraysize(values));
  return Object::New(
      isolate, Object::New(isolate), names, values, arraysize(names));
}

Local<Object> URLPattern::URLPatternResult::ToJSObject(
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
      inputs[index] = ToV8Value(env->context(), input_str).ToLocalChecked();
    } else {
      DCHECK(std::holds_alternative<ada::url_pattern_init>(input));
      auto init = std::get<ada::url_pattern_init>(input);
      inputs[index] = URLPatternInit::ToJsObject(env, init).ToLocalChecked();
    }
    index++;
  }
  Local<Value> values[] = {
      Array::New(isolate, inputs.data(), inputs.size()),
      URLPatternComponentResult::ToJSObject(env, result.protocol),
      URLPatternComponentResult::ToJSObject(env, result.username),
      URLPatternComponentResult::ToJSObject(env, result.password),
      URLPatternComponentResult::ToJSObject(env, result.hostname),
      URLPatternComponentResult::ToJSObject(env, result.port),
      URLPatternComponentResult::ToJSObject(env, result.pathname),
      URLPatternComponentResult::ToJSObject(env, result.search),
      URLPatternComponentResult::ToJSObject(env, result.hash),
  };
  DCHECK_EQ(arraysize(names), arraysize(values));
  return Object::New(
      isolate, Object::New(isolate), names, values, arraysize(names));
}

ada::url_pattern_options URLPattern::URLPatternOptions::FromJsObject(
    Environment* env, Local<Object> obj) {
  ada::url_pattern_options options{};
  Local<Value> ignore_case;
  if (obj->Get(env->context(),
               FIXED_ONE_BYTE_STRING(env->isolate(), "ignoreCase"))
          .ToLocal(&ignore_case)) {
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
  if (auto result =
          url_pattern_.exec(input, baseURL ? &baseURL.value() : nullptr)) {
    if (result->has_value()) {
      return URLPatternResult::ToJSObject(env, result->value());
    }
    return Null(env->isolate());
  }
  env->ThrowTypeError("Failed to exec URLPattern");
  return {};
}

bool URLPattern::Test(Environment* env,
                      const ada::url_pattern_input& input,
                      std::optional<std::string_view>& baseURL) {
  if (auto result =
          url_pattern_.test(input, baseURL ? &baseURL.value() : nullptr)) {
    return *result;
  }
  env->ThrowTypeError("Failed to test URLPattern");
  return false;
}

// V8 Methods

void URLPattern::Exec(const FunctionCallbackInfo<Value>& args) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, args.This());
  if (args.Length() == 0) {  // input, [baseURL]
    return args.GetReturnValue().SetNull();
  }
  auto env = Environment::GetCurrent(args);

  ada::url_pattern_input input;
  std::optional<std::string> baseURL{};
  std::string input_base;
  if (args[0]->IsString()) {
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

  if (args.Length() > 1 && args[1]->IsString()) {
    Utf8Value base_url_value(env->isolate(), args[1].As<String>());
    baseURL = base_url_value.ToStringView();
  }

  Local<Value> result;
  std::optional<std::string_view> baseURL_opt =
      baseURL ? std::optional<std::string_view>(*baseURL) : std::nullopt;
  if (!url_pattern->Exec(env, input, baseURL_opt).ToLocal(&result)) {
    return;
  }
  args.GetReturnValue().Set(result);
}

void URLPattern::Test(const FunctionCallbackInfo<Value>& args) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, args.This());
  if (args.Length() == 0) {  // input, [baseURL]
    return args.GetReturnValue().Set(false);
  }
  auto env = Environment::GetCurrent(args);

  ada::url_pattern_input input;
  std::optional<std::string> baseURL{};
  std::string input_base;
  if (args[0]->IsString()) {
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

  if (args.Length() > 1 && args[1]->IsString()) {
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
  if (!url_pattern->Protocol().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Username(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Username().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Password(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Password().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Hostname(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Hostname().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Port(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Port().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Pathname(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Pathname().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Search(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Search().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void URLPattern::Hash(const FunctionCallbackInfo<Value>& info) {
  URLPattern* url_pattern;
  ASSIGN_OR_RETURN_UNWRAP(&url_pattern, info.This());
  Local<Value> result;
  if (!url_pattern->Hash().ToLocal(&result)) {
    return;
  }
  info.GetReturnValue().Set(result);
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
