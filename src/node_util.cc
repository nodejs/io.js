#include "base_object-inl.h"
#include "node_dotenv.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "util-inl.h"
#include "v8-fast-api-calls.h"

namespace node {
namespace util {

using v8::ALL_PROPERTIES;
using v8::Array;
using v8::ArrayBufferView;
using v8::BigInt;
using v8::Boolean;
using v8::CFunction;
using v8::Context;
using v8::External;
using v8::FunctionCallbackInfo;
using v8::IndexFilter;
using v8::Integer;
using v8::Isolate;
using v8::KeyCollectionMode;
using v8::Local;
using v8::LocalVector;
using v8::Name;
using v8::Object;
using v8::ObjectTemplate;
using v8::ONLY_CONFIGURABLE;
using v8::ONLY_ENUMERABLE;
using v8::ONLY_WRITABLE;
using v8::Promise;
using v8::PropertyFilter;
using v8::Proxy;
using v8::SKIP_STRINGS;
using v8::SKIP_SYMBOLS;
using v8::StackFrame;
using v8::StackTrace;
using v8::String;
using v8::Uint32;
using v8::Value;

// If a UTF-16 character is a low/trailing surrogate.
CHAR_TEST(16, IsUnicodeTrail, (ch & 0xFC00) == 0xDC00)

// If a UTF-16 character is a surrogate.
CHAR_TEST(16, IsUnicodeSurrogate, (ch & 0xF800) == 0xD800)

// If a UTF-16 surrogate is a low/trailing one.
CHAR_TEST(16, IsUnicodeSurrogateTrail, (ch & 0x400) != 0)

static void GetOwnNonIndexProperties(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsUint32());

  Local<Object> object = args[0].As<Object>();

  Local<Array> properties;

  PropertyFilter filter =
    static_cast<PropertyFilter>(args[1].As<Uint32>()->Value());

  if (!object->GetPropertyNames(
        context, KeyCollectionMode::kOwnOnly,
        filter,
        IndexFilter::kSkipIndices)
          .ToLocal(&properties)) {
    return;
  }
  args.GetReturnValue().Set(properties);
}

static void GetConstructorName(
    const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsObject());

  Local<Object> object = args[0].As<Object>();
  Local<String> name = object->GetConstructorName();

  args.GetReturnValue().Set(name);
}

static void GetExternalValue(
    const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsExternal());
  Isolate* isolate = args.GetIsolate();
  Local<External> external = args[0].As<External>();

  void* ptr = external->Value();
  uint64_t value = reinterpret_cast<uint64_t>(ptr);
  Local<BigInt> ret = BigInt::NewFromUnsigned(isolate, value);
  args.GetReturnValue().Set(ret);
}

static void GetPromiseDetails(const FunctionCallbackInfo<Value>& args) {
  // Return undefined if it's not a Promise.
  if (!args[0]->IsPromise())
    return;

  auto isolate = args.GetIsolate();

  Local<Promise> promise = args[0].As<Promise>();

  int state = promise->State();
  Local<Value> values[2] = { Integer::New(isolate, state) };
  size_t number_of_values = 1;
  if (state != Promise::PromiseState::kPending)
    values[number_of_values++] = promise->Result();
  Local<Array> ret = Array::New(isolate, values, number_of_values);
  args.GetReturnValue().Set(ret);
}

static void GetProxyDetails(const FunctionCallbackInfo<Value>& args) {
  // Return undefined if it's not a proxy.
  if (!args[0]->IsProxy())
    return;

  Local<Proxy> proxy = args[0].As<Proxy>();

  // TODO(BridgeAR): Remove the length check as soon as we prohibit access to
  // the util binding layer. It's accessed in the wild and `esm` would break in
  // case the check is removed.
  if (args.Length() == 1 || args[1]->IsTrue()) {
    Local<Value> ret[] = {
      proxy->GetTarget(),
      proxy->GetHandler()
    };

    args.GetReturnValue().Set(
        Array::New(args.GetIsolate(), ret, arraysize(ret)));
  } else {
    Local<Value> ret = proxy->GetTarget();

    args.GetReturnValue().Set(ret);
  }
}

static void GetCallerLocation(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<StackTrace> trace = StackTrace::CurrentStackTrace(isolate, 2);

  // This function is frame zero. The caller is frame one. If there aren't two
  // stack frames, return undefined.
  if (trace->GetFrameCount() != 2) {
    return;
  }

  Local<StackFrame> frame = trace->GetFrame(isolate, 1);
  Local<Value> file = frame->GetScriptNameOrSourceURL();

  if (file.IsEmpty()) {
    return;
  }

  Local<Value> ret[] = {Integer::New(isolate, frame->GetLineNumber()),
                        Integer::New(isolate, frame->GetColumn()),
                        file};

  args.GetReturnValue().Set(Array::New(args.GetIsolate(), ret, arraysize(ret)));
}

static void PreviewEntries(const FunctionCallbackInfo<Value>& args) {
  if (!args[0]->IsObject())
    return;

  Environment* env = Environment::GetCurrent(args);
  bool is_key_value;
  Local<Array> entries;
  if (!args[0].As<Object>()->PreviewEntries(&is_key_value).ToLocal(&entries))
    return;
  // Fast path for WeakMap and WeakSet.
  if (args.Length() == 1)
    return args.GetReturnValue().Set(entries);

  Local<Value> ret[] = {
    entries,
    Boolean::New(env->isolate(), is_key_value)
  };
  return args.GetReturnValue().Set(
      Array::New(env->isolate(), ret, arraysize(ret)));
}

static void Sleep(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsUint32());
  uint32_t msec = args[0].As<Uint32>()->Value();
  uv_sleep(msec);
}

void ArrayBufferViewHasBuffer(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsArrayBufferView());
  args.GetReturnValue().Set(args[0].As<ArrayBufferView>()->HasBuffer());
}

static uint32_t GetUVHandleTypeCode(const uv_handle_type type) {
  // TODO(anonrig): We can use an enum here and then create the array in the
  // binding, which will remove the hard-coding in C++ and JS land.

  // Currently, the return type of this function corresponds to the index of the
  // array defined in the JS land. This is done as an optimization to reduce the
  // string serialization overhead.
  switch (type) {
    case UV_TCP:
      return 0;
    case UV_TTY:
      return 1;
    case UV_UDP:
      return 2;
    case UV_FILE:
      return 3;
    case UV_NAMED_PIPE:
      return 4;
    case UV_UNKNOWN_HANDLE:
      return 5;
    default:
      ABORT();
  }
}

static void GuessHandleType(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  int fd;
  if (!args[0]->Int32Value(env->context()).To(&fd)) return;
  CHECK_GE(fd, 0);

  uv_handle_type t = uv_guess_handle(fd);
  args.GetReturnValue().Set(GetUVHandleTypeCode(t));
}

static uint32_t FastGuessHandleType(Local<Value> receiver, const uint32_t fd) {
  uv_handle_type t = uv_guess_handle(fd);
  return GetUVHandleTypeCode(t);
}

CFunction fast_guess_handle_type_(CFunction::Make(FastGuessHandleType));

static void ParseEnv(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);  // content
  CHECK(args[0]->IsString());
  Utf8Value content(env->isolate(), args[0]);
  Dotenv dotenv{};
  dotenv.ParseContent(content.ToStringView());
  args.GetReturnValue().Set(dotenv.ToObject(env));
}

static void GetCallSites(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsNumber());
  const uint32_t frames = args[0].As<Uint32>()->Value();
  DCHECK(frames >= 1 && frames <= 200);

  // +1 for disregarding node:util
  Local<StackTrace> stack = StackTrace::CurrentStackTrace(isolate, frames + 1);
  const int frame_count = stack->GetFrameCount();
  LocalVector<Value> callsite_objects(isolate);

  // Frame 0 is node:util. It should be skipped.
  for (int i = 1; i < frame_count; ++i) {
    Local<StackFrame> stack_frame = stack->GetFrame(isolate, i);

    Local<Value> function_name = stack_frame->GetFunctionName();
    if (function_name.IsEmpty()) {
      function_name = v8::String::Empty(isolate);
    }

    Local<Value> script_name = stack_frame->GetScriptName();
    if (script_name.IsEmpty()) {
      script_name = v8::String::Empty(isolate);
    }

    std::string script_id = std::to_string(stack_frame->GetScriptId());

    Local<Name> names[] = {
        env->function_name_string(),
        env->script_id_string(),
        env->script_name_string(),
        env->line_number_string(),
        env->column_number_string(),
        // TODO(legendecas): deprecate CallSite.column.
        env->column_string(),
    };
    Local<Value> values[] = {
        function_name,
        OneByteString(isolate, script_id),
        script_name,
        Integer::NewFromUnsigned(isolate, stack_frame->GetLineNumber()),
        Integer::NewFromUnsigned(isolate, stack_frame->GetColumn()),
        // TODO(legendecas): deprecate CallSite.column.
        Integer::NewFromUnsigned(isolate, stack_frame->GetColumn()),
    };
    Local<Object> obj = Object::New(
        isolate, v8::Null(isolate), names, values, arraysize(names));

    callsite_objects.push_back(obj);
  }

  Local<Array> callsites =
      Array::New(isolate, callsite_objects.data(), callsite_objects.size());
  args.GetReturnValue().Set(callsites);
}

static void IsInsideNodeModules(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsInt32());  // frame_limit
  // The second argument is the default value.

  int frames_limit = args[0].As<v8::Int32>()->Value();
  Local<StackTrace> stack =
      StackTrace::CurrentStackTrace(isolate, frames_limit);
  int frame_count = stack->GetFrameCount();

  // If the search requires looking into more than |frames_limit| frames, give
  // up and return the specified default value.
  if (frame_count == frames_limit) {
    return args.GetReturnValue().Set(args[1]);
  }

  bool result = false;
  for (int i = 0; i < frame_count; ++i) {
    Local<StackFrame> stack_frame = stack->GetFrame(isolate, i);
    Local<String> script_name = stack_frame->GetScriptName();

    if (script_name.IsEmpty() || script_name->Length() == 0) {
      continue;
    }
    Utf8Value script_name_utf8(isolate, script_name);
    std::string_view script_name_str = script_name_utf8.ToStringView();
    if (script_name_str.starts_with("node:")) {
      continue;
    }
    if (script_name_str.find("/node_modules/") != std::string::npos ||
        script_name_str.find("\\node_modules\\") != std::string::npos ||
        script_name_str.find("/node_modules\\") != std::string::npos ||
        script_name_str.find("\\node_modules/") != std::string::npos) {
      result = true;
      break;
    }
  }

  args.GetReturnValue().Set(result);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(GetPromiseDetails);
  registry->Register(GetProxyDetails);
  registry->Register(GetCallerLocation);
  registry->Register(PreviewEntries);
  registry->Register(GetCallSites);
  registry->Register(GetOwnNonIndexProperties);
  registry->Register(GetConstructorName);
  registry->Register(GetExternalValue);
  registry->Register(Sleep);
  registry->Register(ArrayBufferViewHasBuffer);
  registry->Register(GuessHandleType);
  registry->Register(FastGuessHandleType);
  registry->Register(fast_guess_handle_type_.GetTypeInfo());
  registry->Register(ParseEnv);
  registry->Register(IsInsideNodeModules);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  {
    Local<ObjectTemplate> tmpl = ObjectTemplate::New(isolate);
#define V(PropertyName, _)                                                     \
  tmpl->Set(FIXED_ONE_BYTE_STRING(env->isolate(), #PropertyName),              \
            env->PropertyName());

    PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)
#undef V

    target
        ->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "privateSymbols"),
              tmpl->NewInstance(context).ToLocalChecked())
        .Check();
  }

  {
    Local<Object> constants = Object::New(isolate);
#define V(name)                                                                \
  constants                                                                    \
      ->Set(context,                                                           \
            FIXED_ONE_BYTE_STRING(isolate, #name),                             \
            Integer::New(isolate, Promise::PromiseState::name))                \
      .Check();

    V(kPending);
    V(kFulfilled);
    V(kRejected);
#undef V

#define V(name)                                                                \
  constants                                                                    \
      ->Set(context,                                                           \
            FIXED_ONE_BYTE_STRING(isolate, #name),                             \
            Integer::New(isolate, Environment::ExitInfoField::name))           \
      .Check();

    V(kExiting);
    V(kExitCode);
    V(kHasExitCode);
#undef V

#define V(name)                                                                \
  constants                                                                    \
      ->Set(context,                                                           \
            FIXED_ONE_BYTE_STRING(isolate, #name),                             \
            Integer::New(isolate, PropertyFilter::name))                       \
      .Check();

    V(ALL_PROPERTIES);
    V(ONLY_WRITABLE);
    V(ONLY_ENUMERABLE);
    V(ONLY_CONFIGURABLE);
    V(SKIP_STRINGS);
    V(SKIP_SYMBOLS);
#undef V

#define V(name)                                                                \
  constants                                                                    \
      ->Set(                                                                   \
          context,                                                             \
          FIXED_ONE_BYTE_STRING(isolate, #name),                               \
          Integer::New(isolate,                                                \
                       static_cast<int32_t>(BaseObject::TransferMode::name)))  \
      .Check();

    V(kDisallowCloneAndTransfer);
    V(kTransferable);
    V(kCloneable);
#undef V

    target->Set(context, env->constants_string(), constants).Check();
  }

  SetMethod(context, target, "isInsideNodeModules", IsInsideNodeModules);
  SetMethodNoSideEffect(
      context, target, "getPromiseDetails", GetPromiseDetails);
  SetMethodNoSideEffect(context, target, "getProxyDetails", GetProxyDetails);
  SetMethodNoSideEffect(
      context, target, "getCallerLocation", GetCallerLocation);
  SetMethodNoSideEffect(context, target, "previewEntries", PreviewEntries);
  SetMethodNoSideEffect(
      context, target, "getOwnNonIndexProperties", GetOwnNonIndexProperties);
  SetMethodNoSideEffect(
      context, target, "getConstructorName", GetConstructorName);
  SetMethodNoSideEffect(context, target, "getExternalValue", GetExternalValue);
  SetMethodNoSideEffect(context, target, "getCallSites", GetCallSites);
  SetMethod(context, target, "sleep", Sleep);
  SetMethod(context, target, "parseEnv", ParseEnv);

  SetMethod(
      context, target, "arrayBufferViewHasBuffer", ArrayBufferViewHasBuffer);

  Local<String> should_abort_on_uncaught_toggle =
      FIXED_ONE_BYTE_STRING(env->isolate(), "shouldAbortOnUncaughtToggle");
  CHECK(target
            ->Set(context,
                  should_abort_on_uncaught_toggle,
                  env->should_abort_on_uncaught_toggle().GetJSArray())
            .FromJust());

  SetFastMethodNoSideEffect(context,
                            target,
                            "guessHandleType",
                            GuessHandleType,
                            &fast_guess_handle_type_);
}

}  // namespace util
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(util, node::util::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(util, node::util::RegisterExternalReferences)
