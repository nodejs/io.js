#include "node_url.h"
#include "ada.h"
#include "base_object-inl.h"
#include "node_debug.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_i18n.h"
#include "node_metadata.h"
#include "node_process-inl.h"
#include "path.h"
#include "util-inl.h"
#include "v8-fast-api-calls.h"
#include "v8.h"

#include <cstdint>
#include <cstdio>
#include <numeric>

namespace node {
namespace url {

using v8::CFunction;
using v8::Context;
using v8::FastOneByteString;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::NewStringType;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Value;

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("url_components_buffer", url_components_buffer_);
}

BindingData::BindingData(Realm* realm, v8::Local<v8::Object> object)
    : SnapshotableObject(realm, object, type_int),
      url_components_buffer_(realm->isolate(), kURLComponentsLength) {
  object
      ->Set(realm->context(),
            FIXED_ONE_BYTE_STRING(realm->isolate(), "urlComponents"),
            url_components_buffer_.GetJSArray())
      .Check();
  url_components_buffer_.MakeWeak();
}

bool BindingData::PrepareForSerialization(v8::Local<v8::Context> context,
                                          v8::SnapshotCreator* creator) {
  // We'll just re-initialize the buffers in the constructor since their
  // contents can be thrown away once consumed in the previous call.
  url_components_buffer_.Release();
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info =
      InternalFieldInfoBase::New<InternalFieldInfo>(type());
  return info;
}

void BindingData::Deserialize(v8::Local<v8::Context> context,
                              v8::Local<v8::Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  v8::HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  BindingData* binding = realm->AddBindingData<BindingData>(holder);
  CHECK_NOT_NULL(binding);
}

#ifndef LARGEST_ASCII_CHAR_CODE_TO_ENCODE
#define LARGEST_ASCII_CHAR_CODE_TO_ENCODE '~'
#endif

// RFC1738 defines the following chars as "unsafe" for URLs
// @see https://www.ietf.org/rfc/rfc1738.txt 2.2. URL Character Encoding Issues
constexpr auto lookup_table = []() consteval {
  // Each entry is an array that can hold up to 3 chars + null terminator
  std::array<std::array<char, 4>, LARGEST_ASCII_CHAR_CODE_TO_ENCODE + 1>
      result{};

  for (uint8_t i = 0; i <= LARGEST_ASCII_CHAR_CODE_TO_ENCODE; i++) {
    switch (i) {
#define ENCODE_CHAR(CHAR, HEX_DIGIT_2, HEX_DIGIT_1)                            \
  case CHAR:                                                                   \
    result[i] = {{'%', HEX_DIGIT_2, HEX_DIGIT_1, 0}};                          \
    break;

      ENCODE_CHAR('\0', '0', '0')  // '\0' == 0x00
      ENCODE_CHAR('\t', '0', '9')  // '\t' == 0x09
      ENCODE_CHAR('\n', '0', 'A')  // '\n' == 0x0A
      ENCODE_CHAR('\r', '0', 'D')  // '\r' == 0x0D
      ENCODE_CHAR(' ', '2', '0')   // ' ' == 0x20
      ENCODE_CHAR('"', '2', '2')   // '"' == 0x22
      ENCODE_CHAR('#', '2', '3')   // '#' == 0x23
      ENCODE_CHAR('%', '2', '5')   // '%' == 0x25
      ENCODE_CHAR('?', '3', 'F')   // '?' == 0x3F
      ENCODE_CHAR('[', '5', 'B')   // '[' == 0x5B
      ENCODE_CHAR('\\', '5', 'C')  // '\\' == 0x5C
      ENCODE_CHAR(']', '5', 'D')   // ']' == 0x5D
      ENCODE_CHAR('^', '5', 'E')   // '^' == 0x5E
      ENCODE_CHAR('|', '7', 'C')   // '|' == 0x7C
      ENCODE_CHAR('~', '7', 'E')   // '~' == 0x7E
#undef ENCODE_CHAR

      default:
        result[i] = {{static_cast<char>(i), '\0', '\0', '\0'}};
        break;
    }
  }

  return result;
}
();

enum class OS { WINDOWS, POSIX };

std::string EncodePathChars(std::string_view input_str, OS operating_system) {
  std::string encoded = "file://";
  encoded.reserve(input_str.size() +
                  7);  // Reserve space for "file://" and input_str
  for (size_t i : input_str) {
    if (i > LARGEST_ASCII_CHAR_CODE_TO_ENCODE) [[unlikely]] {
      encoded.push_back(i);
      continue;
    }
    if (operating_system == OS::WINDOWS) {
      if (i == '\\') {
        encoded.push_back('/');
        continue;
      }
    }
    encoded.append(lookup_table[i].data());
  }

  return encoded;
}

void BindingData::PathToFileURL(const FunctionCallbackInfo<Value>& args) {
  CHECK_GE(args.Length(), 2);  // input
  CHECK(args[0]->IsString());
  CHECK(args[1]->IsBoolean());

  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Isolate* isolate = realm->isolate();
  OS os = args[1]->IsTrue() ? OS::WINDOWS : OS::POSIX;

  Utf8Value input(isolate, args[0]);
  auto input_str = input.ToStringView();
  CHECK(!input_str.empty());

  auto out =
      ada::parse<ada::url_aggregator>(EncodePathChars(input_str, os), nullptr);

  if (!out) {
    return ThrowInvalidURL(realm->env(), input.ToStringView(), nullptr);
  }

  if (os == OS::WINDOWS && args.Length() > 2 && !args[2]->IsUndefined())
      [[unlikely]] {
    CHECK(args[2]->IsString());
    Utf8Value hostname(isolate, args[2]);
    CHECK(out->set_hostname(hostname.ToStringView()));
  }

  binding_data->UpdateComponents(out->get_components(), out->type);

  args.GetReturnValue().Set(
      ToV8Value(realm->context(), out->get_href(), isolate).ToLocalChecked());
}

void BindingData::DomainToASCII(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);  // input
  CHECK(args[0]->IsString());

  Utf8Value input(env->isolate(), args[0]);
  if (input.ToStringView().empty()) {
    return args.GetReturnValue().SetEmptyString();
  }

  // It is important to have an initial value that contains a special scheme.
  // Since it will change the implementation of `set_hostname` according to URL
  // spec.
  auto out = ada::parse<ada::url>("ws://x");
  DCHECK(out);
  if (!out->set_hostname(input.ToStringView())) {
    return args.GetReturnValue().Set(String::Empty(env->isolate()));
  }
  std::string host = out->get_hostname();
  args.GetReturnValue().Set(
      String::NewFromUtf8(env->isolate(), host.c_str()).ToLocalChecked());
}

void BindingData::DomainToUnicode(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);  // input
  CHECK(args[0]->IsString());

  Utf8Value input(env->isolate(), args[0]);
  if (input.ToStringView().empty()) {
    return args.GetReturnValue().SetEmptyString();
  }

  // It is important to have an initial value that contains a special scheme.
  // Since it will change the implementation of `set_hostname` according to URL
  // spec.
  auto out = ada::parse<ada::url>("ws://x");
  DCHECK(out);
  if (!out->set_hostname(input.ToStringView())) {
    return args.GetReturnValue().Set(String::Empty(env->isolate()));
  }
  std::string result = ada::idna::to_unicode(out->get_hostname());

  args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(),
                                                result.c_str(),
                                                NewStringType::kNormal,
                                                result.length())
                                .ToLocalChecked());
}

void BindingData::GetOrigin(const v8::FunctionCallbackInfo<Value>& args) {
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());  // input

  Environment* env = Environment::GetCurrent(args);
  HandleScope handle_scope(env->isolate());

  Utf8Value input(env->isolate(), args[0]);
  std::string_view input_view = input.ToStringView();
  auto out = ada::parse<ada::url_aggregator>(input_view);

  if (!out) {
    THROW_ERR_INVALID_URL(env, "Invalid URL");
    return;
  }

  std::string origin = out->get_origin();
  args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(),
                                                origin.data(),
                                                NewStringType::kNormal,
                                                origin.length())
                                .ToLocalChecked());
}

void BindingData::CanParse(const FunctionCallbackInfo<Value>& args) {
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());  // input
  // args[1] // base url

  Environment* env = Environment::GetCurrent(args);
  HandleScope handle_scope(env->isolate());

  Utf8Value input(env->isolate(), args[0]);
  std::string_view input_view = input.ToStringView();

  bool can_parse{};
  if (args[1]->IsString()) {
    Utf8Value base(env->isolate(), args[1]);
    std::string_view base_view = base.ToStringView();
    can_parse = ada::can_parse(input_view, &base_view);
  } else {
    can_parse = ada::can_parse(input_view);
  }

  args.GetReturnValue().Set(can_parse);
}

bool BindingData::FastCanParse(Local<Value> receiver,
                               const FastOneByteString& input) {
  TRACK_V8_FAST_API_CALL("url.canParse");
  return ada::can_parse(std::string_view(input.data, input.length));
}

bool BindingData::FastCanParseWithBase(Local<Value> receiver,
                                       const FastOneByteString& input,
                                       const FastOneByteString& base) {
  TRACK_V8_FAST_API_CALL("url.canParse.withBase");
  auto base_view = std::string_view(base.data, base.length);
  return ada::can_parse(std::string_view(input.data, input.length), &base_view);
}

CFunction BindingData::fast_can_parse_methods_[] = {
    CFunction::Make(FastCanParse), CFunction::Make(FastCanParseWithBase)};

void BindingData::Format(const FunctionCallbackInfo<Value>& args) {
  CHECK_GT(args.Length(), 4);
  CHECK(args[0]->IsString());  // url href

  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  Utf8Value href(isolate, args[0].As<String>());
  const bool hash = args[1]->IsTrue();
  const bool unicode = args[2]->IsTrue();
  const bool search = args[3]->IsTrue();
  const bool auth = args[4]->IsTrue();

  // ada::url provides a faster alternative to ada::url_aggregator if we
  // directly want to manipulate the url components without using the respective
  // setters. therefore we are using ada::url here.
  auto out = ada::parse<ada::url>(href.ToStringView());
  CHECK(out);

  if (!hash) {
    out->hash = std::nullopt;
  }

  if (unicode && out->has_hostname()) {
    out->host = ada::idna::to_unicode(out->get_hostname());
  }

  if (!search) {
    out->query = std::nullopt;
  }

  if (!auth) {
    out->username = "";
    out->password = "";
  }

  std::string result = out->get_href();
  args.GetReturnValue().Set(String::NewFromUtf8(env->isolate(),
                                                result.data(),
                                                NewStringType::kNormal,
                                                result.length())
                                .ToLocalChecked());
}

void BindingData::Parse(const FunctionCallbackInfo<Value>& args) {
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());  // input
  // args[1] // base url
  // args[2] // raise Exception

  const bool raise_exception = args.Length() > 2 && args[2]->IsTrue();

  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Isolate* isolate = realm->isolate();
  std::optional<std::string> base_{};

  Utf8Value input(isolate, args[0]);
  ada::result<ada::url_aggregator> base;
  ada::url_aggregator* base_pointer = nullptr;
  if (args[1]->IsString()) {
    base_ = Utf8Value(isolate, args[1]).ToString();
    base = ada::parse<ada::url_aggregator>(*base_);
    if (!base && raise_exception) {
      return ThrowInvalidURL(realm->env(), input.ToStringView(), base_);
    } else if (!base) {
      return;
    }
    base_pointer = &base.value();
  }
  auto out =
      ada::parse<ada::url_aggregator>(input.ToStringView(), base_pointer);

  if (!out && raise_exception) {
    return ThrowInvalidURL(realm->env(), input.ToStringView(), base_);
  } else if (!out) {
    return;
  }

  binding_data->UpdateComponents(out->get_components(), out->type);

  args.GetReturnValue().Set(
      ToV8Value(realm->context(), out->get_href(), isolate).ToLocalChecked());
}

void BindingData::Update(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());    // href
  CHECK(args[1]->IsNumber());    // action type
  CHECK(args[2]->IsString());    // new value

  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Isolate* isolate = realm->isolate();

  enum url_update_action action = static_cast<enum url_update_action>(
      args[1]->Uint32Value(realm->context()).FromJust());
  Utf8Value input(isolate, args[0].As<String>());
  Utf8Value new_value(isolate, args[2].As<String>());

  std::string_view new_value_view = new_value.ToStringView();
  auto out = ada::parse<ada::url_aggregator>(input.ToStringView());
  CHECK(out);

  bool result{true};

  switch (action) {
    case kPathname: {
      result = out->set_pathname(new_value_view);
      break;
    }
    case kHash: {
      out->set_hash(new_value_view);
      break;
    }
    case kHost: {
      result = out->set_host(new_value_view);
      break;
    }
    case kHostname: {
      result = out->set_hostname(new_value_view);
      break;
    }
    case kHref: {
      result = out->set_href(new_value_view);
      break;
    }
    case kPassword: {
      result = out->set_password(new_value_view);
      break;
    }
    case kPort: {
      result = out->set_port(new_value_view);
      break;
    }
    case kProtocol: {
      result = out->set_protocol(new_value_view);
      break;
    }
    case kSearch: {
      out->set_search(new_value_view);
      break;
    }
    case kUsername: {
      result = out->set_username(new_value_view);
      break;
    }
    default:
      UNREACHABLE("Unsupported URL update action");
  }

  if (!result) {
    return args.GetReturnValue().Set(false);
  }

  binding_data->UpdateComponents(out->get_components(), out->type);
  args.GetReturnValue().Set(
      ToV8Value(realm->context(), out->get_href(), isolate).ToLocalChecked());
}

void BindingData::UpdateComponents(const ada::url_components& components,
                                   const ada::scheme::type type) {
  url_components_buffer_[0] = components.protocol_end;
  url_components_buffer_[1] = components.username_end;
  url_components_buffer_[2] = components.host_start;
  url_components_buffer_[3] = components.host_end;
  url_components_buffer_[4] = components.port;
  url_components_buffer_[5] = components.pathname_start;
  url_components_buffer_[6] = components.search_start;
  url_components_buffer_[7] = components.hash_start;
  url_components_buffer_[8] = type;
  static_assert(kURLComponentsLength == 9,
                "kURLComponentsLength should be up-to-date");
}

void BindingData::CreatePerIsolateProperties(IsolateData* isolate_data,
                                             Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();
  SetMethodNoSideEffect(isolate, target, "domainToASCII", DomainToASCII);
  SetMethodNoSideEffect(isolate, target, "domainToUnicode", DomainToUnicode);
  SetMethodNoSideEffect(isolate, target, "format", Format);
  SetMethodNoSideEffect(isolate, target, "getOrigin", GetOrigin);
  SetMethod(isolate, target, "parse", Parse);
  SetMethod(isolate, target, "pathToFileURL", PathToFileURL);
  SetMethod(isolate, target, "update", Update);
  SetFastMethodNoSideEffect(
      isolate, target, "canParse", CanParse, {fast_can_parse_methods_, 2});
}

void BindingData::CreatePerContextProperties(Local<Object> target,
                                             Local<Value> unused,
                                             Local<Context> context,
                                             void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  realm->AddBindingData<BindingData>(target);
}

void BindingData::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(DomainToASCII);
  registry->Register(DomainToUnicode);
  registry->Register(Format);
  registry->Register(GetOrigin);
  registry->Register(Parse);
  registry->Register(PathToFileURL);
  registry->Register(Update);
  registry->Register(CanParse);
  registry->Register(FastCanParse);
  registry->Register(FastCanParseWithBase);

  for (const CFunction& method : fast_can_parse_methods_) {
    registry->Register(method.GetTypeInfo());
  }
}

void ThrowInvalidURL(node::Environment* env,
                     std::string_view input,
                     std::optional<std::string> base) {
  Local<Value> err = ERR_INVALID_URL(env->isolate(), "Invalid URL");
  DCHECK(err->IsObject());

  auto err_object = err.As<Object>();

  USE(err_object->Set(env->context(),
                      env->input_string(),
                      v8::String::NewFromUtf8(env->isolate(),
                                              input.data(),
                                              v8::NewStringType::kNormal,
                                              input.size())
                          .ToLocalChecked()));

  if (base.has_value()) {
    USE(err_object->Set(env->context(),
                        env->base_string(),
                        v8::String::NewFromUtf8(env->isolate(),
                                                base.value().c_str(),
                                                v8::NewStringType::kNormal,
                                                base.value().size())
                            .ToLocalChecked()));
  }

  env->isolate()->ThrowException(err);
}

std::string FromFilePath(std::string_view file_path) {
  // Avoid unnecessary allocations.
  size_t pos = file_path.empty() ? std::string_view::npos : file_path.find('%');
  if (pos == std::string_view::npos) {
    return ada::href_from_file(file_path);
  }
  // Escape '%' characters to a temporary string.
  std::string escaped_file_path;
  do {
    escaped_file_path += file_path.substr(0, pos + 1);
    escaped_file_path += "25";
    file_path = file_path.substr(pos + 1);
    pos = file_path.empty() ? std::string_view::npos : file_path.find('%');
  } while (pos != std::string_view::npos);
  escaped_file_path += file_path;
  return ada::href_from_file(escaped_file_path);
}

std::optional<std::string> FileURLToPath(Environment* env,
                                         const ada::url_aggregator& file_url) {
  if (file_url.type != ada::scheme::FILE) {
    THROW_ERR_INVALID_URL_SCHEME(env->isolate());
    return std::nullopt;
  }

  std::string_view pathname = file_url.get_pathname();
#ifdef _WIN32
  size_t first_percent = std::string::npos;
  size_t pathname_size = pathname.size();
  std::string pathname_escaped_slash;

  for (size_t i = 0; i < pathname_size; i++) {
    if (pathname[i] == '/') {
      pathname_escaped_slash += '\\';
    } else {
      pathname_escaped_slash += pathname[i];
    }

    if (pathname[i] != '%') continue;

    if (first_percent == std::string::npos) {
      first_percent = i;
    }

    // just safe-guard against access the pathname
    // outside the bounds
    if ((i + 2) >= pathname_size) continue;

    char third = pathname[i + 2] | 0x20;

    bool is_slash = pathname[i + 1] == '2' && third == 102;
    bool is_forward_slash = pathname[i + 1] == '5' && third == 99;

    if (!is_slash && !is_forward_slash) continue;

    THROW_ERR_INVALID_FILE_URL_PATH(
        env->isolate(),
        "File URL path must not include encoded \\ or / characters");
    return std::nullopt;
  }

  std::string_view hostname = file_url.get_hostname();
  std::string decoded_pathname = ada::unicode::percent_decode(
      std::string_view(pathname_escaped_slash), first_percent);

  if (hostname.size() > 0) {
    // If hostname is set, then we have a UNC path
    // Pass the hostname through domainToUnicode just in case
    // it is an IDN using punycode encoding. We do not need to worry
    // about percent encoding because the URL parser will have
    // already taken care of that for us. Note that this only
    // causes IDNs with an appropriate `xn--` prefix to be decoded.
    return "\\\\" + ada::idna::to_unicode(hostname) + decoded_pathname;
  }

  char letter = decoded_pathname[1] | 0x20;
  char sep = decoded_pathname[2];

  // a..z A..Z
  if (letter < 'a' || letter > 'z' || sep != ':') {
    THROW_ERR_INVALID_FILE_URL_PATH(env->isolate(),
                                    "File URL path must be absolute");
    return std::nullopt;
  }

  return decoded_pathname.substr(1);
#else   // _WIN32
  std::string_view hostname = file_url.get_hostname();

  if (hostname.size() > 0) {
    THROW_ERR_INVALID_FILE_URL_HOST(
        env->isolate(),
        "File URL host must be \"localhost\" or empty on ",
        std::string(per_process::metadata.platform));
    return std::nullopt;
  }

  size_t first_percent = std::string::npos;
  for (size_t i = 0; (i + 2) < pathname.size(); i++) {
    if (pathname[i] != '%') continue;

    if (first_percent == std::string::npos) {
      first_percent = i;
    }

    if (pathname[i + 1] == '2' && (pathname[i + 2] | 0x20) == 102) {
      THROW_ERR_INVALID_FILE_URL_PATH(
          env->isolate(),
          "File URL path must not include encoded / characters");
      return std::nullopt;
    }
  }

  return ada::unicode::percent_decode(pathname, first_percent);
#endif  // _WIN32
}

}  // namespace url

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    url, node::url::BindingData::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(
    url, node::url::BindingData::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(
    url, node::url::BindingData::RegisterExternalReferences)
