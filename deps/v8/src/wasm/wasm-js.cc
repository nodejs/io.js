// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-js.h"

#include <cinttypes>
#include <cstring>
#include <optional>

#include "include/v8-function.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-promise.h"
#include "include/v8-wasm.h"
#include "src/api/api-inl.h"
#include "src/api/api-natives.h"
#include "src/base/logging.h"
#include "src/execution/execution.h"
#include "src/execution/isolate.h"
#include "src/execution/messages.h"
#include "src/flags/flags.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-function.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/templates.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/signature-hashing.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-serialization.h"
#include "src/wasm/wasm-value.h"

namespace v8 {

using v8::internal::wasm::CompileTimeImport;
using v8::internal::wasm::CompileTimeImports;
using v8::internal::wasm::ErrorThrower;
using v8::internal::wasm::WasmEnabledFeatures;

namespace internal {

// Note: The implementation of this function is in runtime-wasm.cc, in order
// to be able to use helpers that aren't visible outside that file.
void ToUtf8Lossy(Isolate* isolate, Handle<String> string, std::string& out);

}  // namespace internal

class WasmStreaming::WasmStreamingImpl {
 public:
  WasmStreamingImpl(
      i::Isolate* isolate, const char* api_method_name,
      CompileTimeImports compile_imports,
      std::shared_ptr<internal::wasm::CompilationResultResolver> resolver)
      : i_isolate_(isolate),
        enabled_features_(WasmEnabledFeatures::FromIsolate(i_isolate_)),
        streaming_decoder_(i::wasm::GetWasmEngine()->StartStreamingCompilation(
            i_isolate_, enabled_features_, std::move(compile_imports),
            handle(i_isolate_->context(), i_isolate_), api_method_name,
            resolver)),
        resolver_(std::move(resolver)) {}

  void OnBytesReceived(const uint8_t* bytes, size_t size) {
    streaming_decoder_->OnBytesReceived(base::VectorOf(bytes, size));
  }
  void Finish(bool can_use_compiled_module) {
    streaming_decoder_->Finish(can_use_compiled_module);
  }

  void Abort(MaybeLocal<Value> exception) {
    i::HandleScope scope(i_isolate_);
    streaming_decoder_->Abort();

    // If no exception value is provided, we do not reject the promise. This can
    // happen when streaming compilation gets aborted when no script execution
    // is allowed anymore, e.g. when a browser tab gets refreshed.
    if (exception.IsEmpty()) return;

    resolver_->OnCompilationFailed(
        Utils::OpenHandle(*exception.ToLocalChecked()));
  }

  bool SetCompiledModuleBytes(base::Vector<const uint8_t> bytes) {
    if (!i::wasm::IsSupportedVersion(bytes, enabled_features_)) return false;
    streaming_decoder_->SetCompiledModuleBytes(bytes);
    return true;
  }

  void SetMoreFunctionsCanBeSerializedCallback(
      std::function<void(CompiledWasmModule)> callback) {
    streaming_decoder_->SetMoreFunctionsCanBeSerializedCallback(
        [callback = std::move(callback),
         url = streaming_decoder_->shared_url()](
            const std::shared_ptr<i::wasm::NativeModule>& native_module) {
          callback(CompiledWasmModule{native_module, url->data(), url->size()});
        });
  }

  void SetUrl(base::Vector<const char> url) { streaming_decoder_->SetUrl(url); }

 private:
  i::Isolate* const i_isolate_;
  const WasmEnabledFeatures enabled_features_;
  const std::shared_ptr<internal::wasm::StreamingDecoder> streaming_decoder_;
  const std::shared_ptr<internal::wasm::CompilationResultResolver> resolver_;
};

WasmStreaming::WasmStreaming(std::unique_ptr<WasmStreamingImpl> impl)
    : impl_(std::move(impl)) {
  TRACE_EVENT0("v8.wasm", "wasm.InitializeStreaming");
}

// The destructor is defined here because we have a unique_ptr with forward
// declaration.
WasmStreaming::~WasmStreaming() = default;

void WasmStreaming::OnBytesReceived(const uint8_t* bytes, size_t size) {
  TRACE_EVENT1("v8.wasm", "wasm.OnBytesReceived", "bytes", size);
  impl_->OnBytesReceived(bytes, size);
}

void WasmStreaming::Finish(bool can_use_compiled_module) {
  TRACE_EVENT0("v8.wasm", "wasm.FinishStreaming");
  impl_->Finish(can_use_compiled_module);
}

void WasmStreaming::Abort(MaybeLocal<Value> exception) {
  TRACE_EVENT0("v8.wasm", "wasm.AbortStreaming");
  impl_->Abort(exception);
}

bool WasmStreaming::SetCompiledModuleBytes(const uint8_t* bytes, size_t size) {
  TRACE_EVENT0("v8.wasm", "wasm.SetCompiledModuleBytes");
  return impl_->SetCompiledModuleBytes(base::VectorOf(bytes, size));
}

void WasmStreaming::SetMoreFunctionsCanBeSerializedCallback(
    std::function<void(CompiledWasmModule)> callback) {
  impl_->SetMoreFunctionsCanBeSerializedCallback(std::move(callback));
}

void WasmStreaming::SetUrl(const char* url, size_t length) {
  DCHECK_EQ('\0', url[length]);  // {url} is null-terminated.
  TRACE_EVENT1("v8.wasm", "wasm.SetUrl", "url", url);
  impl_->SetUrl(base::VectorOf(url, length));
}

// static
std::shared_ptr<WasmStreaming> WasmStreaming::Unpack(Isolate* isolate,
                                                     Local<Value> value) {
  TRACE_EVENT0("v8.wasm", "wasm.WasmStreaming.Unpack");
  i::HandleScope scope(reinterpret_cast<i::Isolate*>(isolate));
  auto managed = i::Cast<i::Managed<WasmStreaming>>(Utils::OpenHandle(*value));
  return managed->get();
}

namespace {

#define ASSIGN(type, var, expr)                          \
  Local<type> var;                                       \
  do {                                                   \
    if (!expr.ToLocal(&var)) {                           \
      DCHECK(i_isolate->has_exception());                \
      return;                                            \
    } else {                                             \
      if (i_isolate->is_execution_terminating()) return; \
      DCHECK(!i_isolate->has_exception());               \
    }                                                    \
  } while (false)

i::Handle<i::String> v8_str(i::Isolate* isolate, const char* str) {
  return isolate->factory()->NewStringFromAsciiChecked(str);
}
Local<String> v8_str(Isolate* isolate, const char* str) {
  return Utils::ToLocal(v8_str(reinterpret_cast<i::Isolate*>(isolate), str));
}

#define GET_FIRST_ARGUMENT_AS(Type)                                  \
  i::MaybeHandle<i::Wasm##Type##Object> GetFirstArgumentAs##Type(    \
      const v8::FunctionCallbackInfo<v8::Value>& info,               \
      ErrorThrower* thrower) {                                       \
    SLOW_DCHECK(i::ValidateCallbackInfo(info));                      \
    i::Handle<i::Object> arg0 = Utils::OpenHandle(*info[0]);         \
    if (!IsWasm##Type##Object(*arg0)) {                              \
      thrower->TypeError("Argument 0 must be a WebAssembly." #Type); \
      return {};                                                     \
    }                                                                \
    return i::Cast<i::Wasm##Type##Object>(arg0);                     \
  }

GET_FIRST_ARGUMENT_AS(Module)
GET_FIRST_ARGUMENT_AS(Tag)

#undef GET_FIRST_ARGUMENT_AS

i::wasm::ModuleWireBytes GetFirstArgumentAsBytes(
    const v8::FunctionCallbackInfo<v8::Value>& info, size_t max_length,
    ErrorThrower* thrower, bool* is_shared) {
  DCHECK(i::ValidateCallbackInfo(info));
  const uint8_t* start = nullptr;
  size_t length = 0;
  v8::Local<v8::Value> source = info[0];
  if (source->IsArrayBuffer()) {
    // A raw array buffer was passed.
    Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(source);
    auto backing_store = buffer->GetBackingStore();

    start = reinterpret_cast<const uint8_t*>(backing_store->Data());
    length = backing_store->ByteLength();
    *is_shared = buffer->IsSharedArrayBuffer();
  } else if (source->IsTypedArray()) {
    // A TypedArray was passed.
    Local<TypedArray> array = Local<TypedArray>::Cast(source);
    Local<ArrayBuffer> buffer = array->Buffer();

    auto backing_store = buffer->GetBackingStore();

    start = reinterpret_cast<const uint8_t*>(backing_store->Data()) +
            array->ByteOffset();
    length = array->ByteLength();
    *is_shared = buffer->IsSharedArrayBuffer();
  } else {
    thrower->TypeError("Argument 0 must be a buffer source");
  }
  DCHECK_IMPLIES(length, start != nullptr);
  if (length == 0) {
    thrower->CompileError("BufferSource argument is empty");
  }
  if (length > max_length) {
    // The spec requires a CompileError for implementation-defined limits, see
    // https://webassembly.github.io/spec/js-api/index.html#limits.
    thrower->CompileError("buffer source exceeds maximum size of %zu (is %zu)",
                          max_length, length);
  }
  if (thrower->error()) return i::wasm::ModuleWireBytes(nullptr, nullptr);
  return i::wasm::ModuleWireBytes(start, start + length);
}

namespace {
i::MaybeHandle<i::JSReceiver> ImportsAsMaybeReceiver(Local<Value> ffi) {
  if (ffi->IsUndefined()) return {};

  Local<Object> obj = Local<Object>::Cast(ffi);
  return i::Cast<i::JSReceiver>(v8::Utils::OpenHandle(*obj));
}

// This class resolves the result of WebAssembly.compile. It just places the
// compilation result in the supplied {promise}.
class AsyncCompilationResolver : public i::wasm::CompilationResultResolver {
 public:
  AsyncCompilationResolver(Isolate* isolate, Local<Context> context,
                           Local<Promise::Resolver> promise_resolver)
      : isolate_(isolate),
        context_(isolate, context),
        promise_resolver_(isolate, promise_resolver) {
    context_.SetWeak();
    promise_resolver_.AnnotateStrongRetainer(kGlobalPromiseHandle);
  }

  void OnCompilationSucceeded(i::Handle<i::WasmModuleObject> result) override {
    if (finished_) return;
    finished_ = true;
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(i::Cast<i::Object>(result)),
             WasmAsyncSuccess::kSuccess);
  }

  void OnCompilationFailed(i::Handle<i::Object> error_reason) override {
    if (finished_) return;
    finished_ = true;
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(error_reason), WasmAsyncSuccess::kFail);
  }

 private:
  static constexpr char kGlobalPromiseHandle[] =
      "AsyncCompilationResolver::promise_";
  bool finished_ = false;
  Isolate* isolate_;
  Global<Context> context_;
  Global<Promise::Resolver> promise_resolver_;
};

constexpr char AsyncCompilationResolver::kGlobalPromiseHandle[];

// This class resolves the result of WebAssembly.instantiate(module, imports).
// It just places the instantiation result in the supplied {promise}.
class InstantiateModuleResultResolver
    : public i::wasm::InstantiationResultResolver {
 public:
  InstantiateModuleResultResolver(Isolate* isolate, Local<Context> context,
                                  Local<Promise::Resolver> promise_resolver)
      : isolate_(isolate),
        context_(isolate, context),
        promise_resolver_(isolate, promise_resolver) {
    context_.SetWeak();
    promise_resolver_.AnnotateStrongRetainer(kGlobalPromiseHandle);
  }

  void OnInstantiationSucceeded(
      i::Handle<i::WasmInstanceObject> instance) override {
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(i::Cast<i::Object>(instance)),
             WasmAsyncSuccess::kSuccess);
  }

  void OnInstantiationFailed(i::Handle<i::Object> error_reason) override {
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(error_reason), WasmAsyncSuccess::kFail);
  }

 private:
  static constexpr char kGlobalPromiseHandle[] =
      "InstantiateModuleResultResolver::promise_";
  Isolate* isolate_;
  Global<Context> context_;
  Global<Promise::Resolver> promise_resolver_;
};

constexpr char InstantiateModuleResultResolver::kGlobalPromiseHandle[];

// This class resolves the result of WebAssembly.instantiate(bytes, imports).
// For that it creates a new {JSObject} which contains both the provided
// {WasmModuleObject} and the resulting {WebAssemblyInstanceObject} itself.
class InstantiateBytesResultResolver
    : public i::wasm::InstantiationResultResolver {
 public:
  InstantiateBytesResultResolver(Isolate* isolate, Local<Context> context,
                                 Local<Promise::Resolver> promise_resolver,
                                 Local<Value> module)
      : isolate_(isolate),
        context_(isolate, context),
        promise_resolver_(isolate, promise_resolver),
        module_(isolate, module) {
    context_.SetWeak();
    promise_resolver_.AnnotateStrongRetainer(kGlobalPromiseHandle);
    module_.AnnotateStrongRetainer(kGlobalModuleHandle);
  }

  void OnInstantiationSucceeded(
      i::Handle<i::WasmInstanceObject> instance) override {
    if (context_.IsEmpty()) return;
    Local<Context> context = context_.Get(isolate_);
    WasmAsyncSuccess success = WasmAsyncSuccess::kSuccess;

    // The result is a JSObject with 2 fields which contain the
    // WasmInstanceObject and the WasmModuleObject.
    Local<Object> result = Object::New(isolate_);
    if (V8_UNLIKELY(result
                        ->CreateDataProperty(context,
                                             v8_str(isolate_, "module"),
                                             module_.Get(isolate_))
                        .IsNothing())) {
      i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate_);
      // We assume that a TerminationException is the only reason why
      // `CreateDataProperty` can fail here. We should revisit
      // https://crbug.com/1515227 again if this CHECK fails.
      CHECK(i::IsTerminationException(i_isolate->exception()));
      result = Utils::ToLocal(
          handle(i::Cast<i::JSObject>(i_isolate->exception()), i_isolate));
      success = WasmAsyncSuccess::kFail;
    }
    if (V8_UNLIKELY(result
                        ->CreateDataProperty(
                            context, v8_str(isolate_, "instance"),
                            Utils::ToLocal(i::Cast<i::Object>(instance)))
                        .IsNothing())) {
      i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate_);
      CHECK(i::IsTerminationException(i_isolate->exception()));
      result = Utils::ToLocal(
          handle(i::Cast<i::JSObject>(i_isolate->exception()), i_isolate));
      success = WasmAsyncSuccess::kFail;
    }

    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context, promise_resolver_.Get(isolate_), result,
             success);
  }

  void OnInstantiationFailed(i::Handle<i::Object> error_reason) override {
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(error_reason), WasmAsyncSuccess::kFail);
  }

 private:
  static constexpr char kGlobalPromiseHandle[] =
      "InstantiateBytesResultResolver::promise_";
  static constexpr char kGlobalModuleHandle[] =
      "InstantiateBytesResultResolver::module_";
  Isolate* isolate_;
  Global<Context> context_;
  Global<Promise::Resolver> promise_resolver_;
  Global<Value> module_;
};

constexpr char InstantiateBytesResultResolver::kGlobalPromiseHandle[];
constexpr char InstantiateBytesResultResolver::kGlobalModuleHandle[];

// This class is the {CompilationResultResolver} for
// WebAssembly.instantiate(bytes, imports). When compilation finishes,
// {AsyncInstantiate} is started on the compilation result.
class AsyncInstantiateCompileResultResolver
    : public i::wasm::CompilationResultResolver {
 public:
  AsyncInstantiateCompileResultResolver(
      Isolate* isolate, Local<Context> context,
      Local<Promise::Resolver> promise_resolver, Local<Value> imports)
      : isolate_(isolate),
        context_(isolate, context),
        promise_resolver_(isolate, promise_resolver),
        imports_(isolate, imports) {
    context_.SetWeak();
    promise_resolver_.AnnotateStrongRetainer(kGlobalPromiseHandle);
    imports_.AnnotateStrongRetainer(kGlobalImportsHandle);
  }

  void OnCompilationSucceeded(i::Handle<i::WasmModuleObject> result) override {
    if (finished_) return;
    finished_ = true;
    i::wasm::GetWasmEngine()->AsyncInstantiate(
        reinterpret_cast<i::Isolate*>(isolate_),
        std::make_unique<InstantiateBytesResultResolver>(
            isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
            Utils::ToLocal(i::Cast<i::Object>(result))),
        result, ImportsAsMaybeReceiver(imports_.Get(isolate_)));
  }

  void OnCompilationFailed(i::Handle<i::Object> error_reason) override {
    if (finished_) return;
    finished_ = true;
    if (context_.IsEmpty()) return;
    auto callback = reinterpret_cast<i::Isolate*>(isolate_)
                        ->wasm_async_resolve_promise_callback();
    CHECK(callback);
    callback(isolate_, context_.Get(isolate_), promise_resolver_.Get(isolate_),
             Utils::ToLocal(error_reason), WasmAsyncSuccess::kFail);
  }

 private:
  static constexpr char kGlobalPromiseHandle[] =
      "AsyncInstantiateCompileResultResolver::promise_";
  static constexpr char kGlobalImportsHandle[] =
      "AsyncInstantiateCompileResultResolver::module_";
  bool finished_ = false;
  Isolate* isolate_;
  Global<Context> context_;
  Global<Promise::Resolver> promise_resolver_;
  Global<Value> imports_;
};

constexpr char AsyncInstantiateCompileResultResolver::kGlobalPromiseHandle[];
constexpr char AsyncInstantiateCompileResultResolver::kGlobalImportsHandle[];

std::string ToString(const char* name) { return std::string(name); }

std::string ToString(const i::DirectHandle<i::String> name) {
  return std::string("Property '") + name->ToCString().get() + "'";
}

// Web IDL: '[EnforceRange] unsigned long'
// Previously called ToNonWrappingUint32 in the draft WebAssembly JS spec.
// https://heycam.github.io/webidl/#EnforceRange
template <typename T>
bool EnforceUint32(T argument_name, Local<v8::Value> v, Local<Context> context,
                   ErrorThrower* thrower, uint32_t* res) {
  double double_number;

  if (!v->NumberValue(context).To(&double_number)) {
    thrower->TypeError("%s must be convertible to a number",
                       ToString(argument_name).c_str());
    return false;
  }
  if (!std::isfinite(double_number)) {
    thrower->TypeError("%s must be convertible to a valid number",
                       ToString(argument_name).c_str());
    return false;
  }
  if (double_number < 0) {
    thrower->TypeError("%s must be non-negative",
                       ToString(argument_name).c_str());
    return false;
  }
  if (double_number > std::numeric_limits<uint32_t>::max()) {
    thrower->TypeError("%s must be in the unsigned long range",
                       ToString(argument_name).c_str());
    return false;
  }

  *res = static_cast<uint32_t>(double_number);
  return true;
}

// The enum values need to match "WasmCompilationMethod" in
// tools/metrics/histograms/enums.xml.
enum CompilationMethod {
  kSyncCompilation = 0,
  kAsyncCompilation = 1,
  kStreamingCompilation = 2,
  kAsyncInstantiation = 3,
  kStreamingInstantiation = 4,
};

void RecordCompilationMethod(i::Isolate* isolate, CompilationMethod method) {
  isolate->counters()->wasm_compilation_method()->AddSample(method);
}

CompileTimeImports ArgumentToCompileOptions(
    Local<Value> arg_value, i::Isolate* isolate,
    WasmEnabledFeatures enabled_features) {
  if (!enabled_features.has_imported_strings()) return {};
  i::Handle<i::Object> arg = Utils::OpenHandle(*arg_value);
  if (!i::IsJSReceiver(*arg)) return {};
  i::Handle<i::JSReceiver> receiver = i::Cast<i::JSReceiver>(arg);
  CompileTimeImports result;

  // ==================== Builtins ====================
  i::Handle<i::Object> builtins;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, builtins,
      i::JSReceiver::GetProperty(isolate, receiver, "builtins"), {});
  if (i::IsJSReceiver(*builtins)) {
    i::Handle<i::Object> length_obj;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, length_obj,
        i::Object::GetLengthFromArrayLike(isolate,
                                          i::Cast<i::JSReceiver>(builtins)),
        {});
    double raw_length = i::Object::NumberValue(*length_obj);
    // Technically we should probably iterate up to 2^53-1 if {length_obj} says
    // so, but lengths above 2^32 probably don't happen in practice (and would
    // be very slow if they do), so just use a saturating to-uint32 conversion
    // for simplicity.
    uint32_t len = raw_length >= i::kMaxUInt32
                       ? i::kMaxUInt32
                       : static_cast<uint32_t>(raw_length);
    for (uint32_t i = 0; i < len; i++) {
      i::LookupIterator it(isolate, builtins, i);
      Maybe<bool> maybe_found = i::JSReceiver::HasProperty(&it);
      MAYBE_RETURN(maybe_found, {});
      if (!maybe_found.FromJust()) continue;
      i::Handle<i::Object> value;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value,
                                       i::Object::GetProperty(&it), {});
      if (i::IsString(*value)) {
        i::Tagged<i::String> builtin = i::Cast<i::String>(*value);
        // TODO(jkummerow): We could make other string comparisons to known
        // constants in this file more efficient by migrating them to this
        // style (rather than `...->StringEquals(v8_str(...))`).
        if (builtin->IsEqualTo(base::CStrVector("js-string"))) {
          result.Add(CompileTimeImport::kJsString);
        } else if (builtin->IsEqualTo(base::CStrVector("text-encoder"))) {
          result.Add(CompileTimeImport::kTextEncoder);
        } else if (builtin->IsEqualTo(base::CStrVector("text-decoder"))) {
          result.Add(CompileTimeImport::kTextDecoder);
        }
      }
    }
  }

  // ==================== String constants ====================
  i::Handle<i::String> importedStringConstants =
      isolate->factory()->InternalizeUtf8String("importedStringConstants");
  if (i::JSReceiver::HasProperty(isolate, receiver, importedStringConstants)
          .FromMaybe(false)) {
    i::Handle<i::Object> constants_value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, constants_value,
        i::JSReceiver::GetProperty(isolate, receiver, importedStringConstants),
        {});
    if (i::IsString(*constants_value)) {
      i::ToUtf8Lossy(isolate, i::Cast<i::String>(constants_value),
                     result.constants_module());
      result.Add(CompileTimeImport::kStringConstants);
    }
  }

  return result;
}
}  // namespace

// WebAssembly.compile(bytes, options) -> Promise
void WebAssemblyCompileImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  constexpr const char* kAPIMethodName = "WebAssembly.compile()";
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  RecordCompilationMethod(i_isolate, kAsyncCompilation);

  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, kAPIMethodName);

  i::Handle<i::NativeContext> native_context = i_isolate->native_context();
  if (!i::wasm::IsWasmCodegenAllowed(i_isolate, native_context)) {
    i::DirectHandle<i::String> error =
        i::wasm::ErrorStringForCodegen(i_isolate, native_context);
    thrower.CompileError("%s", error->ToCString().get());
  }

  Local<Context> context = isolate->GetCurrentContext();
  ASSIGN(Promise::Resolver, promise_resolver, Promise::Resolver::New(context));
  Local<Promise> promise = promise_resolver->GetPromise();
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(promise);

  std::shared_ptr<i::wasm::CompilationResultResolver> resolver(
      new AsyncCompilationResolver(isolate, context, promise_resolver));

  bool is_shared = false;
  auto bytes = GetFirstArgumentAsBytes(info, i::wasm::max_module_size(),
                                       &thrower, &is_shared);
  if (thrower.error()) {
    resolver->OnCompilationFailed(thrower.Reify());
    return;
  }
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[1], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    resolver->OnCompilationFailed(handle(i_isolate->exception(), i_isolate));
    i_isolate->clear_exception();
    return;
  }
  // Asynchronous compilation handles copying wire bytes if necessary.
  i::wasm::GetWasmEngine()->AsyncCompile(
      i_isolate, enabled_features, std::move(compile_imports),
      std::move(resolver), bytes, is_shared, kAPIMethodName);
}

void WasmStreamingCallbackForTesting(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);

  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.compile()");

  std::shared_ptr<v8::WasmStreaming> streaming =
      v8::WasmStreaming::Unpack(info.GetIsolate(), info.Data());

  bool is_shared = false;
  // We don't check the buffer length up front, to allow d8 to test that the
  // streaming decoder implementation handles overly large inputs correctly.
  size_t unlimited = std::numeric_limits<size_t>::max();
  i::wasm::ModuleWireBytes bytes =
      GetFirstArgumentAsBytes(info, unlimited, &thrower, &is_shared);
  if (thrower.error()) {
    streaming->Abort(Utils::ToLocal(thrower.Reify()));
    return;
  }
  streaming->OnBytesReceived(bytes.start(), bytes.length());
  streaming->Finish();
  CHECK(!thrower.error());
}

void WasmStreamingPromiseFailedCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<v8::WasmStreaming> streaming =
      v8::WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->Abort(info[0]);
}

// WebAssembly.compileStreaming(Response | Promise<Response>, options)
//   -> Promise<WebAssembly.Module>
void WebAssemblyCompileStreaming(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  RecordCompilationMethod(i_isolate, kStreamingCompilation);
  HandleScope scope(isolate);
  const char* const kAPIMethodName = "WebAssembly.compileStreaming()";
  ErrorThrower thrower(i_isolate, kAPIMethodName);
  Local<Context> context = isolate->GetCurrentContext();

  // Create and assign the return value of this function.
  ASSIGN(Promise::Resolver, promise_resolver, Promise::Resolver::New(context));
  Local<Promise> promise = promise_resolver->GetPromise();
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(promise);

  // Prepare the CompilationResultResolver for the compilation.
  auto resolver = std::make_shared<AsyncCompilationResolver>(isolate, context,
                                                             promise_resolver);

  i::Handle<i::NativeContext> native_context = i_isolate->native_context();
  if (!i::wasm::IsWasmCodegenAllowed(i_isolate, native_context)) {
    i::DirectHandle<i::String> error =
        i::wasm::ErrorStringForCodegen(i_isolate, native_context);
    thrower.CompileError("%s", error->ToCString().get());
    resolver->OnCompilationFailed(thrower.Reify());
    return;
  }

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[1], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    resolver->OnCompilationFailed(handle(i_isolate->exception(), i_isolate));
    i_isolate->clear_exception();
    return;
  }

  // Allocate the streaming decoder in a Managed so we can pass it to the
  // embedder.
  i::Handle<i::Managed<WasmStreaming>> data = i::Managed<WasmStreaming>::From(
      i_isolate, 0,
      std::make_shared<WasmStreaming>(
          std::make_unique<WasmStreaming::WasmStreamingImpl>(
              i_isolate, kAPIMethodName, std::move(compile_imports),
              resolver)));

  DCHECK_NOT_NULL(i_isolate->wasm_streaming_callback());
  ASSIGN(v8::Function, compile_callback,
         v8::Function::New(context, i_isolate->wasm_streaming_callback(),
                           Utils::ToLocal(i::Cast<i::Object>(data)), 1));
  ASSIGN(v8::Function, reject_callback,
         v8::Function::New(context, WasmStreamingPromiseFailedCallback,
                           Utils::ToLocal(i::Cast<i::Object>(data)), 1));

  // The parameter may be of type {Response} or of type {Promise<Response>}.
  // Treat either case of parameter as Promise.resolve(parameter)
  // as per https://www.w3.org/2001/tag/doc/promises-guide#resolve-arguments

  // Ending with:
  //    return Promise.resolve(parameter).then(compile_callback);
  ASSIGN(Promise::Resolver, input_resolver, Promise::Resolver::New(context));
  if (!input_resolver->Resolve(context, info[0]).IsJust()) return;

  // We do not have any use of the result here. The {compile_callback} will
  // start streaming compilation, which will eventually resolve the promise we
  // set as result value.
  USE(input_resolver->GetPromise()->Then(context, compile_callback,
                                         reject_callback));
}

// WebAssembly.validate(bytes, options) -> bool
void WebAssemblyValidateImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.validate()");

  bool is_shared = false;
  auto bytes = GetFirstArgumentAsBytes(info, i::wasm::max_module_size(),
                                       &thrower, &is_shared);

  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();

  if (thrower.error()) {
    if (thrower.wasm_error()) thrower.Reset();  // Clear error.
    return_value.Set(v8::False(isolate));
    return;
  }

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[1], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    return_value.Set(v8::False(isolate));
    i_isolate->clear_exception();
    return;
  }
  bool validated = false;
  if (is_shared) {
    // Make a copy of the wire bytes to avoid concurrent modification.
    std::unique_ptr<uint8_t[]> copy(new uint8_t[bytes.length()]);
    memcpy(copy.get(), bytes.start(), bytes.length());
    i::wasm::ModuleWireBytes bytes_copy(copy.get(),
                                        copy.get() + bytes.length());
    validated = i::wasm::GetWasmEngine()->SyncValidate(
        i_isolate, enabled_features, std::move(compile_imports), bytes_copy);
  } else {
    // The wire bytes are not shared, OK to use them directly.
    validated = i::wasm::GetWasmEngine()->SyncValidate(
        i_isolate, enabled_features, std::move(compile_imports), bytes);
  }

  return_value.Set(validated);
}

namespace {
bool TransferPrototype(i::Isolate* isolate, i::Handle<i::JSObject> destination,
                       i::Handle<i::JSReceiver> source) {
  i::MaybeHandle<i::HeapObject> maybe_prototype =
      i::JSObject::GetPrototype(isolate, source);
  i::Handle<i::HeapObject> prototype;
  if (maybe_prototype.ToHandle(&prototype)) {
    Maybe<bool> result = i::JSObject::SetPrototype(
        isolate, destination, prototype,
        /*from_javascript=*/false, internal::kThrowOnError);
    if (!result.FromJust()) {
      DCHECK(isolate->has_exception());
      return false;
    }
  }
  return true;
}
}  // namespace

// new WebAssembly.Module(bytes, options) -> WebAssembly.Module
void WebAssemblyModuleImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  if (i_isolate->wasm_module_callback()(info)) return;
  RecordCompilationMethod(i_isolate, kSyncCompilation);

  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Module()");

  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Module must be invoked with 'new'");
    return;
  }
  i::Handle<i::NativeContext> native_context = i_isolate->native_context();
  if (!i::wasm::IsWasmCodegenAllowed(i_isolate, native_context)) {
    i::DirectHandle<i::String> error =
        i::wasm::ErrorStringForCodegen(i_isolate, native_context);
    thrower.CompileError("%s", error->ToCString().get());
    return;
  }

  bool is_shared = false;
  auto bytes = GetFirstArgumentAsBytes(info, i::wasm::max_module_size(),
                                       &thrower, &is_shared);

  if (thrower.error()) {
    return;
  }
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[1], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    // TODO(14179): Does this need different error message handling?
    return;
  }
  i::MaybeHandle<i::WasmModuleObject> maybe_module_obj;
  if (is_shared) {
    // Make a copy of the wire bytes to avoid concurrent modification.
    std::unique_ptr<uint8_t[]> copy(new uint8_t[bytes.length()]);
    memcpy(copy.get(), bytes.start(), bytes.length());
    i::wasm::ModuleWireBytes bytes_copy(copy.get(),
                                        copy.get() + bytes.length());
    maybe_module_obj = i::wasm::GetWasmEngine()->SyncCompile(
        i_isolate, enabled_features, std::move(compile_imports), &thrower,
        bytes_copy);
  } else {
    // The wire bytes are not shared, OK to use them directly.
    maybe_module_obj = i::wasm::GetWasmEngine()->SyncCompile(
        i_isolate, enabled_features, std::move(compile_imports), &thrower,
        bytes);
  }

  i::Handle<i::WasmModuleObject> module_obj;
  if (!maybe_module_obj.ToHandle(&module_obj)) return;

  // The infrastructure for `new Foo` calls allocates an object, which is
  // available here as {info.This()}. We're going to discard this object
  // and use {module_obj} instead, but it does have the correct prototype,
  // which we must harvest from it. This makes a difference when the JS
  // constructor function wasn't {WebAssembly.Module} directly, but some
  // subclass: {module_obj} has {WebAssembly.Module}'s prototype at this
  // point, so we must overwrite that with the correct prototype for {Foo}.
  if (!TransferPrototype(i_isolate, module_obj,
                         Utils::OpenHandle(*info.This()))) {
    return;
  }

  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(Utils::ToLocal(module_obj));
}

// WebAssembly.Module.imports(module) -> Array<Import>
void WebAssemblyModuleImportsImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  HandleScope scope(info.GetIsolate());
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Module.imports()");

  auto maybe_module = GetFirstArgumentAsModule(info, &thrower);
  if (thrower.error()) return;
  auto imports = i::wasm::GetImports(i_isolate, maybe_module.ToHandleChecked());
  info.GetReturnValue().Set(Utils::ToLocal(imports));
}

// WebAssembly.Module.exports(module) -> Array<Export>
void WebAssemblyModuleExportsImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  HandleScope scope(info.GetIsolate());
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Module.exports()");

  auto maybe_module = GetFirstArgumentAsModule(info, &thrower);
  if (thrower.error()) return;
  auto exports = i::wasm::GetExports(i_isolate, maybe_module.ToHandleChecked());
  info.GetReturnValue().Set(Utils::ToLocal(exports));
}

// WebAssembly.Module.customSections(module, name) -> Array<Section>
void WebAssemblyModuleCustomSectionsImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  HandleScope scope(info.GetIsolate());
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Module.customSections()");

  auto maybe_module = GetFirstArgumentAsModule(info, &thrower);
  if (thrower.error()) return;

  if (info[1]->IsUndefined()) {
    thrower.TypeError("Argument 1 is required");
    return;
  }

  i::MaybeHandle<i::Object> maybe_name =
      i::Object::ToString(i_isolate, Utils::OpenHandle(*info[1]));
  i::Handle<i::Object> name;
  if (!maybe_name.ToHandle(&name)) return;
  auto custom_sections =
      i::wasm::GetCustomSections(i_isolate, maybe_module.ToHandleChecked(),
                                 i::Cast<i::String>(name), &thrower);
  if (thrower.error()) return;
  info.GetReturnValue().Set(Utils::ToLocal(custom_sections));
}

// new WebAssembly.Instance(module, imports) -> WebAssembly.Instance
void WebAssemblyInstanceImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  RecordCompilationMethod(i_isolate, kAsyncInstantiation);
  i_isolate->CountUsage(
      v8::Isolate::UseCounterFeature::kWebAssemblyInstantiation);

  HandleScope scope(info.GetIsolate());
  if (i_isolate->wasm_instance_callback()(info)) return;

  i::MaybeHandle<i::JSObject> maybe_instance_obj;
  {
    ErrorThrower thrower(i_isolate, "WebAssembly.Instance()");
    if (!info.IsConstructCall()) {
      thrower.TypeError("WebAssembly.Instance must be invoked with 'new'");
      return;
    }

    i::MaybeHandle<i::WasmModuleObject> maybe_module =
        GetFirstArgumentAsModule(info, &thrower);
    if (thrower.error()) return;

    i::Handle<i::WasmModuleObject> module_obj = maybe_module.ToHandleChecked();

    Local<Value> ffi = info[1];

    if (!ffi->IsUndefined() && !ffi->IsObject()) {
      thrower.TypeError("Argument 1 must be an object");
      return;
    }
    if (thrower.error()) return;

    maybe_instance_obj = i::wasm::GetWasmEngine()->SyncInstantiate(
        i_isolate, &thrower, module_obj, ImportsAsMaybeReceiver(ffi),
        i::MaybeHandle<i::JSArrayBuffer>());
  }

  i::Handle<i::JSObject> instance_obj;
  if (!maybe_instance_obj.ToHandle(&instance_obj)) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  // The infrastructure for `new Foo` calls allocates an object, which is
  // available here as {info.This()}. We're going to discard this object
  // and use {instance_obj} instead, but it does have the correct prototype,
  // which we must harvest from it. This makes a difference when the JS
  // constructor function wasn't {WebAssembly.Instance} directly, but some
  // subclass: {instance_obj} has {WebAssembly.Instance}'s prototype at this
  // point, so we must overwrite that with the correct prototype for {Foo}.
  if (!TransferPrototype(i_isolate, instance_obj,
                         Utils::OpenHandle(*info.This()))) {
    return;
  }

  info.GetReturnValue().Set(Utils::ToLocal(instance_obj));
}

// WebAssembly.instantiateStreaming(
//     Response | Promise<Response> [, imports [, options]])
//   -> Promise<ResultObject>
// (where ResultObject has a "module" and an "instance" field)
void WebAssemblyInstantiateStreaming(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  RecordCompilationMethod(i_isolate, kStreamingInstantiation);
  i_isolate->CountUsage(
      v8::Isolate::UseCounterFeature::kWebAssemblyInstantiation);

  HandleScope scope(isolate);
  Local<Context> context = isolate->GetCurrentContext();
  const char* const kAPIMethodName = "WebAssembly.instantiateStreaming()";
  ErrorThrower thrower(i_isolate, kAPIMethodName);

  // Create and assign the return value of this function.
  ASSIGN(Promise::Resolver, result_resolver, Promise::Resolver::New(context));
  Local<Promise> promise = result_resolver->GetPromise();
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(promise);

  // Create an InstantiateResultResolver in case there is an issue with the
  // passed parameters.
  std::unique_ptr<i::wasm::InstantiationResultResolver> resolver(
      new InstantiateModuleResultResolver(isolate, context, result_resolver));

  i::Handle<i::NativeContext> native_context = i_isolate->native_context();
  if (!i::wasm::IsWasmCodegenAllowed(i_isolate, native_context)) {
    i::DirectHandle<i::String> error =
        i::wasm::ErrorStringForCodegen(i_isolate, native_context);
    thrower.CompileError("%s", error->ToCString().get());
    resolver->OnInstantiationFailed(thrower.Reify());
    return;
  }

  // If info.Length < 2, this will be undefined - see FunctionCallbackInfo.
  Local<Value> ffi = info[1];

  if (!ffi->IsUndefined() && !ffi->IsObject()) {
    thrower.TypeError("Argument 1 must be an object");
    resolver->OnInstantiationFailed(thrower.Reify());
    return;
  }

  // We start compilation now, we have no use for the
  // {InstantiationResultResolver}.
  resolver.reset();

  std::shared_ptr<i::wasm::CompilationResultResolver> compilation_resolver(
      new AsyncInstantiateCompileResultResolver(isolate, context,
                                                result_resolver, ffi));

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[2], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    compilation_resolver->OnCompilationFailed(
        handle(i_isolate->exception(), i_isolate));
    i_isolate->clear_exception();
    return;
  }

  // Allocate the streaming decoder in a Managed so we can pass it to the
  // embedder.
  i::Handle<i::Managed<WasmStreaming>> data = i::Managed<WasmStreaming>::From(
      i_isolate, 0,
      std::make_shared<WasmStreaming>(
          std::make_unique<WasmStreaming::WasmStreamingImpl>(
              i_isolate, kAPIMethodName, std::move(compile_imports),
              compilation_resolver)));

  DCHECK_NOT_NULL(i_isolate->wasm_streaming_callback());
  ASSIGN(v8::Function, compile_callback,
         v8::Function::New(context, i_isolate->wasm_streaming_callback(),
                           Utils::ToLocal(i::Cast<i::Object>(data)), 1));
  ASSIGN(v8::Function, reject_callback,
         v8::Function::New(context, WasmStreamingPromiseFailedCallback,
                           Utils::ToLocal(i::Cast<i::Object>(data)), 1));

  // The parameter may be of type {Response} or of type {Promise<Response>}.
  // Treat either case of parameter as Promise.resolve(parameter)
  // as per https://www.w3.org/2001/tag/doc/promises-guide#resolve-arguments

  // Ending with:
  //    return Promise.resolve(parameter).then(compile_callback);
  ASSIGN(Promise::Resolver, input_resolver, Promise::Resolver::New(context));
  if (!input_resolver->Resolve(context, info[0]).IsJust()) return;

  // We do not have any use of the result here. The {compile_callback} will
  // start streaming compilation, which will eventually resolve the promise we
  // set as result value.
  USE(input_resolver->GetPromise()->Then(context, compile_callback,
                                         reject_callback));
}

// WebAssembly.instantiate(module, imports) -> WebAssembly.Instance
// WebAssembly.instantiate(bytes, imports, options) ->
//     {module: WebAssembly.Module, instance: WebAssembly.Instance}
void WebAssemblyInstantiateImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  constexpr const char* kAPIMethodName = "WebAssembly.instantiate()";
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i_isolate->CountUsage(
      v8::Isolate::UseCounterFeature::kWebAssemblyInstantiation);

  ErrorThrower thrower(i_isolate, kAPIMethodName);

  HandleScope scope(isolate);

  Local<Context> context = isolate->GetCurrentContext();

  ASSIGN(Promise::Resolver, promise_resolver, Promise::Resolver::New(context));
  Local<Promise> promise = promise_resolver->GetPromise();
  info.GetReturnValue().Set(promise);

  std::unique_ptr<i::wasm::InstantiationResultResolver> resolver(
      new InstantiateModuleResultResolver(isolate, context, promise_resolver));

  Local<Value> first_arg_value = info[0];
  i::Handle<i::Object> first_arg = Utils::OpenHandle(*first_arg_value);
  if (!IsJSObject(*first_arg)) {
    thrower.TypeError(
        "Argument 0 must be a buffer source or a WebAssembly.Module object");
    resolver->OnInstantiationFailed(thrower.Reify());
    return;
  }

  // If info.Length < 2, this will be undefined - see FunctionCallbackInfo.
  Local<Value> ffi = info[1];

  if (!ffi->IsUndefined() && !ffi->IsObject()) {
    thrower.TypeError("Argument 1 must be an object");
    resolver->OnInstantiationFailed(thrower.Reify());
    return;
  }

  if (IsWasmModuleObject(*first_arg)) {
    i::Handle<i::WasmModuleObject> module_obj =
        i::Cast<i::WasmModuleObject>(first_arg);

    i::wasm::GetWasmEngine()->AsyncInstantiate(i_isolate, std::move(resolver),
                                               module_obj,
                                               ImportsAsMaybeReceiver(ffi));
    return;
  }

  bool is_shared = false;
  auto bytes = GetFirstArgumentAsBytes(info, i::wasm::max_module_size(),
                                       &thrower, &is_shared);
  if (thrower.error()) {
    resolver->OnInstantiationFailed(thrower.Reify());
    return;
  }

  // We start compilation now, we have no use for the
  // {InstantiationResultResolver}.
  resolver.reset();

  std::shared_ptr<i::wasm::CompilationResultResolver> compilation_resolver(
      new AsyncInstantiateCompileResultResolver(isolate, context,
                                                promise_resolver, ffi));

  // The first parameter is a buffer source, we have to check if we are allowed
  // to compile it.
  i::Handle<i::NativeContext> native_context = i_isolate->native_context();
  if (!i::wasm::IsWasmCodegenAllowed(i_isolate, native_context)) {
    i::DirectHandle<i::String> error =
        i::wasm::ErrorStringForCodegen(i_isolate, native_context);
    thrower.CompileError("%s", error->ToCString().get());
    compilation_resolver->OnCompilationFailed(thrower.Reify());
    return;
  }

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports =
      ArgumentToCompileOptions(info[2], i_isolate, enabled_features);
  if (i_isolate->has_exception()) {
    compilation_resolver->OnCompilationFailed(
        handle(i_isolate->exception(), i_isolate));
    i_isolate->clear_exception();
    return;
  }

  // Asynchronous compilation handles copying wire bytes if necessary.
  i::wasm::GetWasmEngine()->AsyncCompile(
      i_isolate, enabled_features, std::move(compile_imports),
      std::move(compilation_resolver), bytes, is_shared, kAPIMethodName);
}

bool GetIntegerProperty(v8::Isolate* isolate, ErrorThrower* thrower,
                        Local<Context> context, v8::Local<v8::Value> value,
                        i::Handle<i::String> property_name, int64_t* result,
                        int64_t lower_bound, uint64_t upper_bound) {
  uint32_t number;
  if (!EnforceUint32(property_name, value, context, thrower, &number)) {
    return false;
  }
  if (number < lower_bound) {
    thrower->RangeError("Property '%s': value %" PRIu32
                        " is below the lower bound %" PRIx64,
                        property_name->ToCString().get(), number, lower_bound);
    return false;
  }
  if (number > upper_bound) {
    thrower->RangeError("Property '%s': value %" PRIu32
                        " is above the upper bound %" PRIu64,
                        property_name->ToCString().get(), number, upper_bound);
    return false;
  }

  *result = static_cast<int64_t>(number);
  return true;
}

bool GetOptionalIntegerProperty(v8::Isolate* isolate, ErrorThrower* thrower,
                                Local<Context> context,
                                Local<v8::Object> object,
                                Local<String> property, bool* has_property,
                                int64_t* result, int64_t lower_bound,
                                uint64_t upper_bound) {
  v8::Local<v8::Value> value;
  if (!object->Get(context, property).ToLocal(&value)) {
    return false;
  }

  // Web IDL: dictionary presence
  // https://heycam.github.io/webidl/#dfn-present
  if (value->IsUndefined()) {
    if (has_property != nullptr) *has_property = false;
    return true;
  }

  if (has_property != nullptr) *has_property = true;
  i::Handle<i::String> property_name = v8::Utils::OpenHandle(*property);

  return GetIntegerProperty(isolate, thrower, context, value, property_name,
                            result, lower_bound, upper_bound);
}

// Fetch 'initial' or 'minimum' property from object. If both are provided,
// a TypeError is thrown.
// TODO(aseemgarg): change behavior when the following bug is resolved:
// https://github.com/WebAssembly/js-types/issues/6
bool GetInitialOrMinimumProperty(v8::Isolate* isolate, ErrorThrower* thrower,
                                 Local<Context> context,
                                 Local<v8::Object> object, int64_t* result,
                                 int64_t lower_bound, uint64_t upper_bound) {
  bool has_initial = false;
  if (!GetOptionalIntegerProperty(isolate, thrower, context, object,
                                  v8_str(isolate, "initial"), &has_initial,
                                  result, lower_bound, upper_bound)) {
    return false;
  }
  auto enabled_features =
      WasmEnabledFeatures::FromIsolate(reinterpret_cast<i::Isolate*>(isolate));
  if (enabled_features.has_type_reflection()) {
    bool has_minimum = false;
    int64_t minimum = 0;
    if (!GetOptionalIntegerProperty(isolate, thrower, context, object,
                                    v8_str(isolate, "minimum"), &has_minimum,
                                    &minimum, lower_bound, upper_bound)) {
      return false;
    }
    if (has_initial && has_minimum) {
      thrower->TypeError(
          "The properties 'initial' and 'minimum' are not allowed at the same "
          "time");
      return false;
    }
    if (has_minimum) {
      // Only {minimum} exists, so we use {minimum} as {initial}.
      has_initial = true;
      *result = minimum;
    }
  }
  if (!has_initial) {
    // TODO(aseemgarg): update error message when the spec issue is resolved.
    thrower->TypeError("Property 'initial' is required");
    return false;
  }
  return true;
}

namespace {
i::Handle<i::HeapObject> DefaultReferenceValue(i::Isolate* isolate,
                                               i::wasm::ValueType type) {
  DCHECK(type.is_object_reference());
  // Use undefined for JS type (externref) but null for wasm types as wasm does
  // not know undefined.
  if (type.heap_representation() == i::wasm::HeapType::kExtern ||
      type.heap_representation() == i::wasm::HeapType::kNoExtern) {
    return isolate->factory()->undefined_value();
  }
  return isolate->factory()->wasm_null();
}
}  // namespace

// new WebAssembly.Table(info) -> WebAssembly.Table
void WebAssemblyTableImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Table must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a table descriptor");
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
  Local<v8::Object> descriptor = Local<Object>::Cast(info[0]);
  i::wasm::ValueType type;
  // The descriptor's 'element'.
  {
    v8::MaybeLocal<v8::Value> maybe =
        descriptor->Get(context, v8_str(isolate, "element"));
    v8::Local<v8::Value> value;
    if (!maybe.ToLocal(&value)) return;
    v8::Local<v8::String> string;
    if (!value->ToString(context).ToLocal(&string)) return;
    auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
    // The JS api uses 'anyfunc' instead of 'funcref'.
    if (string->StringEquals(v8_str(isolate, "anyfunc"))) {
      type = i::wasm::kWasmFuncRef;
    } else if (enabled_features.has_type_reflection() &&
               string->StringEquals(v8_str(isolate, "funcref"))) {
      // With the type reflection proposal, "funcref" replaces "anyfunc",
      // and anyfunc just becomes an alias for "funcref".
      type = i::wasm::kWasmFuncRef;
    } else if (string->StringEquals(v8_str(isolate, "externref"))) {
      type = i::wasm::kWasmExternRef;
    } else if (enabled_features.has_stringref() &&
               string->StringEquals(v8_str(isolate, "stringref"))) {
      type = i::wasm::kWasmStringRef;
    } else if (string->StringEquals(v8_str(isolate, "anyref"))) {
      type = i::wasm::kWasmAnyRef;
    } else if (string->StringEquals(v8_str(isolate, "eqref"))) {
      type = i::wasm::kWasmEqRef;
    } else if (string->StringEquals(v8_str(isolate, "structref"))) {
      type = i::wasm::kWasmStructRef;
    } else if (string->StringEquals(v8_str(isolate, "arrayref"))) {
      type = i::wasm::kWasmArrayRef;
    } else if (string->StringEquals(v8_str(isolate, "i31ref"))) {
      type = i::wasm::kWasmI31Ref;
    } else {
      thrower.TypeError(
          "Descriptor property 'element' must be a WebAssembly reference type");
      return;
    }
    // TODO(14616): Support shared types.
  }

  int64_t initial = 0;
  if (!GetInitialOrMinimumProperty(isolate, &thrower, context, descriptor,
                                   &initial, 0,
                                   i::wasm::max_table_init_entries())) {
    DCHECK(i_isolate->has_exception() || thrower.error());
    return;
  }
  // The descriptor's 'maximum'.
  int64_t maximum = -1;
  bool has_maximum = true;
  if (!GetOptionalIntegerProperty(isolate, &thrower, context, descriptor,
                                  v8_str(isolate, "maximum"), &has_maximum,
                                  &maximum, initial,
                                  std::numeric_limits<uint32_t>::max())) {
    DCHECK(i_isolate->has_exception() || thrower.error());
    return;
  }

  // Parse the 'index' property of the descriptor.
  v8::Local<v8::Value> index_value;
  if (!descriptor->Get(context, v8_str(isolate, "index"))
           .ToLocal(&index_value)) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  i::WasmTableFlag is_table64_flag = i::WasmTableFlag::kTable32;
  if (!index_value->IsUndefined()) {
    v8::Local<v8::String> index;
    if (!index_value->ToString(context).ToLocal(&index)) {
      DCHECK(i_isolate->has_exception());
      return;
    }
    if (index->StringEquals(v8_str(isolate, "i64"))) {
      is_table64_flag = i::WasmTableFlag::kTable64;
    } else if (!index->StringEquals(v8_str(isolate, "i32"))) {
      thrower.TypeError("Unknown table index");
      return;
    }
  }

  i::Handle<i::WasmTableObject> table_obj = i::WasmTableObject::New(
      i_isolate, i::Handle<i::WasmTrustedInstanceData>(), type,
      static_cast<uint32_t>(initial), has_maximum,
      static_cast<uint32_t>(maximum), DefaultReferenceValue(i_isolate, type),
      is_table64_flag);

  // The infrastructure for `new Foo` calls allocates an object, which is
  // available here as {info.This()}. We're going to discard this object
  // and use {table_obj} instead, but it does have the correct prototype,
  // which we must harvest from it. This makes a difference when the JS
  // constructor function wasn't {WebAssembly.Table} directly, but some
  // subclass: {table_obj} has {WebAssembly.Table}'s prototype at this
  // point, so we must overwrite that with the correct prototype for {Foo}.
  if (!TransferPrototype(i_isolate, table_obj,
                         Utils::OpenHandle(*info.This()))) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  if (initial > 0 && info.Length() >= 2 && !info[1]->IsUndefined()) {
    i::Handle<i::Object> element = Utils::OpenHandle(*info[1]);
    const char* error_message;
    if (!i::WasmTableObject::JSToWasmElement(i_isolate, table_obj, element,
                                             &error_message)
             .ToHandle(&element)) {
      thrower.TypeError(
          "Argument 2 must be undefined or a value of type compatible "
          "with the type of the new table: %s.",
          error_message);
      return;
    }
    for (uint32_t index = 0; index < static_cast<uint32_t>(initial); ++index) {
      i::WasmTableObject::Set(i_isolate, table_obj, index, element);
    }
  } else if (initial > 0) {
    switch (table_obj->type().heap_representation()) {
      case i::wasm::HeapType::kString:
        thrower.TypeError(
            "Missing initial value when creating stringref table");
        return;
      case i::wasm::HeapType::kStringViewWtf8:
        thrower.TypeError("stringview_wtf8 has no JS representation");
        return;
      case i::wasm::HeapType::kStringViewWtf16:
        thrower.TypeError("stringview_wtf16 has no JS representation");
        return;
      case i::wasm::HeapType::kStringViewIter:
        thrower.TypeError("stringview_iter has no JS representation");
        return;
      default:
        break;
    }
  }
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(Utils::ToLocal(i::Cast<i::JSObject>(table_obj)));
}

void WebAssemblyMemoryImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Memory()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Memory must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a memory descriptor");
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
  Local<v8::Object> descriptor = Local<Object>::Cast(info[0]);

  // Parse the 'index' property of the descriptor.
  v8::Local<v8::Value> index_value;
  if (!descriptor->Get(context, v8_str(isolate, "index"))
           .ToLocal(&index_value)) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  i::WasmMemoryFlag memory_flag = i::WasmMemoryFlag::kWasmMemory32;
  if (!index_value->IsUndefined()) {
    v8::Local<v8::String> index;
    if (!index_value->ToString(context).ToLocal(&index)) {
      DCHECK(i_isolate->has_exception());
      return;
    }
    if (index->StringEquals(v8_str(isolate, "i64"))) {
      memory_flag = i::WasmMemoryFlag::kWasmMemory64;
    } else if (!index->StringEquals(v8_str(isolate, "i32"))) {
      thrower.TypeError("Unknown memory index");
      return;
    }
  }
  size_t max_supported_pages = memory_flag == i::WasmMemoryFlag::kWasmMemory64
                                   ? i::wasm::kSpecMaxMemory64Pages
                                   : i::wasm::kSpecMaxMemory32Pages;

  // Parse the 'initial' or 'minimum' property of the descriptor.
  int64_t initial = 0;
  if (!GetInitialOrMinimumProperty(isolate, &thrower, context, descriptor,
                                   &initial, 0, max_supported_pages)) {
    DCHECK(i_isolate->has_exception() || thrower.error());
    return;
  }

  // Parse the 'maximum' property of the descriptor.
  int64_t maximum = i::WasmMemoryObject::kNoMaximum;
  if (!GetOptionalIntegerProperty(isolate, &thrower, context, descriptor,
                                  v8_str(isolate, "maximum"), nullptr, &maximum,
                                  initial, max_supported_pages)) {
    DCHECK(i_isolate->has_exception() || thrower.error());
    return;
  }

  // Parse the 'shared' property of the descriptor.
  v8::Local<v8::Value> value;
  if (!descriptor->Get(context, v8_str(isolate, "shared")).ToLocal(&value)) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  auto shared = value->BooleanValue(isolate) ? i::SharedFlag::kShared
                                             : i::SharedFlag::kNotShared;

  // Throw TypeError if shared is true, and the descriptor has no "maximum".
  if (shared == i::SharedFlag::kShared && maximum == -1) {
    thrower.TypeError("If shared is true, maximum property should be defined.");
    return;
  }

  i::Handle<i::JSObject> memory_obj;
  if (!i::WasmMemoryObject::New(i_isolate, static_cast<int>(initial),
                                static_cast<int>(maximum), shared, memory_flag)
           .ToHandle(&memory_obj)) {
    thrower.RangeError("could not allocate memory");
    return;
  }

  // The infrastructure for `new Foo` calls allocates an object, which is
  // available here as {info.This()}. We're going to discard this object
  // and use {memory_obj} instead, but it does have the correct prototype,
  // which we must harvest from it. This makes a difference when the JS
  // constructor function wasn't {WebAssembly.Memory} directly, but some
  // subclass: {memory_obj} has {WebAssembly.Memory}'s prototype at this
  // point, so we must overwrite that with the correct prototype for {Foo}.
  if (!TransferPrototype(i_isolate, memory_obj,
                         Utils::OpenHandle(*info.This()))) {
    DCHECK(i_isolate->has_exception());
    return;
  }

  if (shared == i::SharedFlag::kShared) {
    i::Handle<i::JSArrayBuffer> buffer(
        i::Cast<i::WasmMemoryObject>(memory_obj)->array_buffer(), i_isolate);
    Maybe<bool> result =
        buffer->SetIntegrityLevel(i_isolate, buffer, i::FROZEN, i::kDontThrow);
    if (!result.FromJust()) {
      thrower.TypeError(
          "Status of setting SetIntegrityLevel of buffer is false.");
      return;
    }
  }
  info.GetReturnValue().Set(Utils::ToLocal(memory_obj));
}

// Determines the type encoded in a value type property (e.g. type reflection).
// Returns false if there was an exception, true upon success. On success the
// outgoing {type} is set accordingly, or set to {wasm::kWasmVoid} in case the
// type could not be properly recognized.
bool GetValueType(Isolate* isolate, MaybeLocal<Value> maybe,
                  Local<Context> context, i::wasm::ValueType* type,
                  WasmEnabledFeatures enabled_features) {
  v8::Local<v8::Value> value;
  if (!maybe.ToLocal(&value)) return false;
  v8::Local<v8::String> string;
  if (!value->ToString(context).ToLocal(&string)) return false;
  if (string->StringEquals(v8_str(isolate, "i32"))) {
    *type = i::wasm::kWasmI32;
  } else if (string->StringEquals(v8_str(isolate, "f32"))) {
    *type = i::wasm::kWasmF32;
  } else if (string->StringEquals(v8_str(isolate, "i64"))) {
    *type = i::wasm::kWasmI64;
  } else if (string->StringEquals(v8_str(isolate, "f64"))) {
    *type = i::wasm::kWasmF64;
  } else if (string->StringEquals(v8_str(isolate, "v128"))) {
    *type = i::wasm::kWasmS128;
  } else if (string->StringEquals(v8_str(isolate, "externref"))) {
    *type = i::wasm::kWasmExternRef;
  } else if (enabled_features.has_type_reflection() &&
             string->StringEquals(v8_str(isolate, "funcref"))) {
    // The type reflection proposal renames "anyfunc" to "funcref", and makes
    // "anyfunc" an alias of "funcref".
    *type = i::wasm::kWasmFuncRef;
  } else if (string->StringEquals(v8_str(isolate, "anyfunc"))) {
    // The JS api spec uses 'anyfunc' instead of 'funcref'.
    *type = i::wasm::kWasmFuncRef;
  } else if (string->StringEquals(v8_str(isolate, "eqref"))) {
    *type = i::wasm::kWasmEqRef;
  } else if (enabled_features.has_stringref() &&
             string->StringEquals(v8_str(isolate, "stringref"))) {
    *type = i::wasm::kWasmStringRef;
  } else if (string->StringEquals(v8_str(isolate, "anyref"))) {
    *type = i::wasm::kWasmAnyRef;
  } else if (string->StringEquals(v8_str(isolate, "structref"))) {
    *type = i::wasm::kWasmStructRef;
  } else if (string->StringEquals(v8_str(isolate, "arrayref"))) {
    *type = i::wasm::kWasmArrayRef;
  } else if (string->StringEquals(v8_str(isolate, "i31ref"))) {
    *type = i::wasm::kWasmI31Ref;
  } else if (enabled_features.has_exnref() &&
             string->StringEquals(v8_str(isolate, "exnref"))) {
    *type = i::wasm::kWasmExnRef;
  } else {
    // Unrecognized type.
    *type = i::wasm::kWasmVoid;
  }
  return true;
}

namespace {

bool ToI32(Local<v8::Value> value, Local<Context> context, int32_t* i32_value) {
  if (!value->IsUndefined()) {
    v8::Local<v8::Int32> int32_value;
    if (!value->ToInt32(context).ToLocal(&int32_value)) return false;
    if (!int32_value->Int32Value(context).To(i32_value)) return false;
  }
  return true;
}

bool ToI64(Local<v8::Value> value, Local<Context> context, int64_t* i64_value) {
  if (!value->IsUndefined()) {
    v8::Local<v8::BigInt> bigint_value;
    if (!value->ToBigInt(context).ToLocal(&bigint_value)) return false;
    *i64_value = bigint_value->Int64Value();
  }
  return true;
}

bool ToF32(Local<v8::Value> value, Local<Context> context, float* f32_value) {
  if (!value->IsUndefined()) {
    double f64_value = 0;
    v8::Local<v8::Number> number_value;
    if (!value->ToNumber(context).ToLocal(&number_value)) return false;
    if (!number_value->NumberValue(context).To(&f64_value)) return false;
    *f32_value = i::DoubleToFloat32(f64_value);
  }
  return true;
}

bool ToF64(Local<v8::Value> value, Local<Context> context, double* f64_value) {
  if (!value->IsUndefined()) {
    v8::Local<v8::Number> number_value;
    if (!value->ToNumber(context).ToLocal(&number_value)) return false;
    if (!number_value->NumberValue(context).To(f64_value)) return false;
  }
  return true;
}
}  // namespace

// WebAssembly.Global
void WebAssemblyGlobalImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Global()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Global must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a global descriptor");
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
  Local<v8::Object> descriptor = Local<Object>::Cast(info[0]);
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  // The descriptor's 'mutable'.
  bool is_mutable = false;
  {
    Local<String> mutable_key = v8_str(isolate, "mutable");
    v8::MaybeLocal<v8::Value> maybe = descriptor->Get(context, mutable_key);
    v8::Local<v8::Value> value;
    if (maybe.ToLocal(&value)) {
      is_mutable = value->BooleanValue(isolate);
    } else {
      DCHECK(i_isolate->has_exception());
      return;
    }
  }

  // The descriptor's type, called 'value'. It is called 'value' because this
  // descriptor is planned to be re-used as the global's type for reflection,
  // so calling it 'type' is redundant.
  i::wasm::ValueType type;
  {
    v8::MaybeLocal<v8::Value> maybe =
        descriptor->Get(context, v8_str(isolate, "value"));
    if (!GetValueType(isolate, maybe, context, &type, enabled_features)) return;
    if (type == i::wasm::kWasmVoid) {
      thrower.TypeError(
          "Descriptor property 'value' must be a WebAssembly type");
      return;
    }
  }

  const uint32_t offset = 0;
  i::MaybeHandle<i::WasmGlobalObject> maybe_global_obj =
      i::WasmGlobalObject::New(
          i_isolate, i::Handle<i::WasmTrustedInstanceData>(),
          i::MaybeHandle<i::JSArrayBuffer>(), i::MaybeHandle<i::FixedArray>(),
          type, offset, is_mutable);

  i::Handle<i::WasmGlobalObject> global_obj;
  if (!maybe_global_obj.ToHandle(&global_obj)) {
    thrower.RangeError("could not allocate memory");
    return;
  }

  // The infrastructure for `new Foo` calls allocates an object, which is
  // available here as {info.This()}. We're going to discard this object
  // and use {global_obj} instead, but it does have the correct prototype,
  // which we must harvest from it. This makes a difference when the JS
  // constructor function wasn't {WebAssembly.Global} directly, but some
  // subclass: {global_obj} has {WebAssembly.Global}'s prototype at this
  // point, so we must overwrite that with the correct prototype for {Foo}.
  if (!TransferPrototype(i_isolate, global_obj,
                         Utils::OpenHandle(*info.This()))) {
    return;
  }

  // Convert value to a WebAssembly value, the default value is 0.
  Local<v8::Value> value = Local<Value>::Cast(info[1]);
  switch (type.kind()) {
    case i::wasm::kI32: {
      int32_t i32_value = 0;
      if (!ToI32(value, context, &i32_value)) return;
      global_obj->SetI32(i32_value);
      break;
    }
    case i::wasm::kI64: {
      int64_t i64_value = 0;
      if (!ToI64(value, context, &i64_value)) return;
      global_obj->SetI64(i64_value);
      break;
    }
    case i::wasm::kF32: {
      float f32_value = 0;
      if (!ToF32(value, context, &f32_value)) return;
      global_obj->SetF32(f32_value);
      break;
    }
    case i::wasm::kF64: {
      double f64_value = 0;
      if (!ToF64(value, context, &f64_value)) return;
      global_obj->SetF64(f64_value);
      break;
    }
    case i::wasm::kRef:
      if (info.Length() < 2) {
        thrower.TypeError("Non-defaultable global needs initial value");
        break;
      }
      [[fallthrough]];
    case i::wasm::kRefNull: {
      // We need the wasm default value {null} over {undefined}.
      i::Handle<i::Object> value_handle;
      if (info.Length() < 2) {
        value_handle = DefaultReferenceValue(i_isolate, type);
      } else {
        value_handle = Utils::OpenHandle(*value);
        const char* error_message;
        // While the JS API generally allows indexed types, it currently has
        // no way to specify such types in `new WebAssembly.Global(...)`.
        // TODO(14034): Fix this if that changes.
        DCHECK(!type.has_index());
        uint32_t unused_canonical_index = 0;
        if (!i::wasm::JSToWasmObject(i_isolate, value_handle, type,
                                     unused_canonical_index, &error_message)
                 .ToHandle(&value_handle)) {
          thrower.TypeError("%s", error_message);
          break;
        }
      }
      global_obj->SetRef(value_handle);
      break;
    }
    case i::wasm::kS128: {
      thrower.TypeError(
          "A global of type 'v128' cannot be created in JavaScript");
      return;
    }
    case i::wasm::kRtt:
    case i::wasm::kI8:
    case i::wasm::kI16:
    case i::wasm::kF16:
    case i::wasm::kVoid:
    case i::wasm::kBottom:
      UNREACHABLE();
  }

  i::Handle<i::JSObject> global_js_object(global_obj);
  info.GetReturnValue().Set(Utils::ToLocal(global_js_object));
}

namespace {

uint32_t GetIterableLength(i::Isolate* isolate, Local<Context> context,
                           Local<Object> iterable) {
  Local<String> length = Utils::ToLocal(isolate->factory()->length_string());
  MaybeLocal<Value> property = iterable->Get(context, length);
  if (property.IsEmpty()) return i::kMaxUInt32;
  MaybeLocal<Uint32> number = property.ToLocalChecked()->ToArrayIndex(context);
  if (number.IsEmpty()) return i::kMaxUInt32;
  DCHECK_NE(i::kMaxUInt32, number.ToLocalChecked()->Value());
  return number.ToLocalChecked()->Value();
}

}  // namespace

// WebAssembly.Tag
void WebAssemblyTagImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);

  ErrorThrower thrower(i_isolate, "WebAssembly.Tag()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Tag must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a tag type");
    return;
  }

  Local<Object> event_type = Local<Object>::Cast(info[0]);
  Local<Context> context = isolate->GetCurrentContext();
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  // Load the 'parameters' property of the event type.
  Local<String> parameters_key = v8_str(isolate, "parameters");
  v8::MaybeLocal<v8::Value> parameters_maybe =
      event_type->Get(context, parameters_key);
  v8::Local<v8::Value> parameters_value;
  if (!parameters_maybe.ToLocal(&parameters_value) ||
      !parameters_value->IsObject()) {
    thrower.TypeError("Argument 0 must be a tag type with 'parameters'");
    return;
  }
  Local<Object> parameters = parameters_value.As<Object>();
  uint32_t parameters_len = GetIterableLength(i_isolate, context, parameters);
  if (parameters_len == i::kMaxUInt32) {
    thrower.TypeError("Argument 0 contains parameters without 'length'");
    return;
  }
  if (parameters_len > i::wasm::kV8MaxWasmFunctionParams) {
    thrower.TypeError("Argument 0 contains too many parameters");
    return;
  }

  // Decode the tag type and construct a signature.
  std::vector<i::wasm::ValueType> param_types(parameters_len,
                                              i::wasm::kWasmVoid);
  for (uint32_t i = 0; i < parameters_len; ++i) {
    i::wasm::ValueType& type = param_types[i];
    MaybeLocal<Value> maybe = parameters->Get(context, i);
    if (!GetValueType(isolate, maybe, context, &type, enabled_features) ||
        type == i::wasm::kWasmVoid) {
      thrower.TypeError(
          "Argument 0 parameter type at index #%u must be a value type", i);
      return;
    }
  }
  const i::wasm::FunctionSig sig{0, parameters_len, param_types.data()};
  // Set the tag index to 0. It is only used for debugging purposes, and has no
  // meaningful value when declared outside of a wasm module.
  auto tag = i::WasmExceptionTag::New(i_isolate, 0);

  uint32_t canonical_type_index =
      i::wasm::GetWasmEngine()->type_canonicalizer()->AddRecursiveGroup(&sig);

  i::Handle<i::JSObject> tag_object =
      i::WasmTagObject::New(i_isolate, &sig, canonical_type_index, tag,
                            i::Handle<i::WasmTrustedInstanceData>());
  info.GetReturnValue().Set(Utils::ToLocal(tag_object));
}

namespace {

uint32_t GetEncodedSize(i::DirectHandle<i::WasmTagObject> tag_object) {
  auto serialized_sig = tag_object->serialized_signature();
  i::wasm::WasmTagSig sig{
      0, static_cast<size_t>(serialized_sig->length()),
      reinterpret_cast<i::wasm::ValueType*>(serialized_sig->begin())};
  return i::WasmExceptionPackage::GetEncodedSize(&sig);
}

void EncodeExceptionValues(
    v8::Isolate* isolate,
    i::DirectHandle<i::PodArray<i::wasm::ValueType>> signature,
    i::DirectHandle<i::WasmTagObject> tag_object, const Local<Value>& arg,
    ErrorThrower* thrower, i::DirectHandle<i::FixedArray> values_out) {
  Local<Context> context = isolate->GetCurrentContext();
  uint32_t index = 0;
  if (!arg->IsObject()) {
    thrower->TypeError("Exception values must be an iterable object");
    return;
  }
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  auto values = arg.As<Object>();
  uint32_t length = GetIterableLength(i_isolate, context, values);
  if (length == i::kMaxUInt32) {
    thrower->TypeError("Exception values argument has no length");
    return;
  }
  if (length != static_cast<uint32_t>(signature->length())) {
    thrower->TypeError(
        "Number of exception values does not match signature length");
    return;
  }
  for (int i = 0; i < signature->length(); ++i) {
    MaybeLocal<Value> maybe_value = values->Get(context, i);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    if (i_isolate->has_exception()) return;
    Local<Value> value = maybe_value.ToLocalChecked();
    i::wasm::ValueType type = signature->get(i);
    switch (type.kind()) {
      case i::wasm::kI32: {
        int32_t i32 = 0;
        if (!ToI32(value, context, &i32)) return;
        i::EncodeI32ExceptionValue(values_out, &index, i32);
        break;
      }
      case i::wasm::kI64: {
        int64_t i64 = 0;
        if (!ToI64(value, context, &i64)) return;
        i::EncodeI64ExceptionValue(values_out, &index, i64);
        break;
      }
      case i::wasm::kF32: {
        float f32 = 0;
        if (!ToF32(value, context, &f32)) return;
        int32_t i32 = base::bit_cast<int32_t>(f32);
        i::EncodeI32ExceptionValue(values_out, &index, i32);
        break;
      }
      case i::wasm::kF64: {
        double f64 = 0;
        if (!ToF64(value, context, &f64)) return;
        int64_t i64 = base::bit_cast<int64_t>(f64);
        i::EncodeI64ExceptionValue(values_out, &index, i64);
        break;
      }
      case i::wasm::kRef:
      case i::wasm::kRefNull: {
        const char* error_message;
        i::Handle<i::Object> value_handle = Utils::OpenHandle(*value);
        uint32_t canonical_index = i::wasm::kInvalidCanonicalIndex;
        if (type.has_index()) {
          // Canonicalize the type using the tag's original module.
          // Indexed types are guaranteed to come from an instance.
          CHECK(tag_object->has_trusted_data());
          i::Tagged<i::WasmTrustedInstanceData> wtid =
              tag_object->trusted_data(i_isolate);
          const i::wasm::WasmModule* module = wtid->module();
          canonical_index =
              module->isorecursive_canonical_type_ids[type.ref_index()];
        }
        if (!i::wasm::JSToWasmObject(i_isolate, value_handle, type,
                                     canonical_index, &error_message)
                 .ToHandle(&value_handle)) {
          thrower->TypeError("%s", error_message);
          return;
        }
        values_out->set(index++, *value_handle);
        break;
      }
      case i::wasm::kRtt:
      case i::wasm::kI8:
      case i::wasm::kI16:
      case i::wasm::kF16:
      case i::wasm::kVoid:
      case i::wasm::kBottom:
      case i::wasm::kS128:
        UNREACHABLE();
    }
  }
}

}  // namespace

void WebAssemblyExceptionImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);

  ErrorThrower thrower(i_isolate, "WebAssembly.Exception()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Exception must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a WebAssembly tag");
    return;
  }
  i::DirectHandle<i::Object> arg0 = Utils::OpenDirectHandle(*info[0]);
  if (!IsWasmTagObject(i::Cast<i::HeapObject>(*arg0))) {
    thrower.TypeError("Argument 0 must be a WebAssembly tag");
    return;
  }
  auto tag_object = i::Cast<i::WasmTagObject>(arg0);
  i::DirectHandle<i::WasmExceptionTag> tag(
      i::Cast<i::WasmExceptionTag>(tag_object->tag()), i_isolate);
  auto js_tag = i::Cast<i::WasmTagObject>(i_isolate->context()->wasm_js_tag());
  if (*tag == js_tag->tag()) {
    thrower.TypeError("Argument 0 cannot be WebAssembly.JSTag");
    return;
  }
  uint32_t size = GetEncodedSize(tag_object);
  i::Handle<i::WasmExceptionPackage> runtime_exception =
      i::WasmExceptionPackage::New(i_isolate, tag, size);
  // The constructor above should guarantee that the cast below succeeds.
  i::DirectHandle<i::FixedArray> values =
      i::Cast<i::FixedArray>(i::WasmExceptionPackage::GetExceptionValues(
          i_isolate, runtime_exception));
  i::DirectHandle<i::PodArray<i::wasm::ValueType>> signature(
      tag_object->serialized_signature(), i_isolate);
  EncodeExceptionValues(isolate, signature, tag_object, info[1], &thrower,
                        values);
  if (thrower.error()) return;

  // Third argument: optional ExceptionOption ({traceStack: <bool>}).
  if (!info[2]->IsNullOrUndefined() && !info[2]->IsObject()) {
    thrower.TypeError("Argument 2 is not an object");
    return;
  }
  if (info[2]->IsObject()) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> trace_stack_obj = Local<Object>::Cast(info[2]);
    Local<String> trace_stack_key = v8_str(isolate, "traceStack");
    v8::MaybeLocal<v8::Value> maybe_trace_stack =
        trace_stack_obj->Get(context, trace_stack_key);
    v8::Local<Value> trace_stack_value;
    if (maybe_trace_stack.ToLocal(&trace_stack_value) &&
        trace_stack_value->BooleanValue(isolate)) {
      auto caller = Utils::OpenHandle(*info.NewTarget());

      i::Handle<i::Object> capture_result;
      if (!i::ErrorUtils::CaptureStackTrace(i_isolate, runtime_exception,
                                            i::SKIP_NONE, caller)
               .ToHandle(&capture_result)) {
        return;
      }
    }
  }

  info.GetReturnValue().Set(
      Utils::ToLocal(i::Cast<i::Object>(runtime_exception)));
}

i::Handle<i::JSFunction> NewPromisingWasmExportedFunction(
    i::Isolate* i_isolate, i::DirectHandle<i::WasmExportedFunctionData> data,
    ErrorThrower& thrower) {
  i::DirectHandle<i::WasmTrustedInstanceData> trusted_instance_data{
      data->instance_data(), i_isolate};
  int func_index = data->function_index();
  const i::wasm::WasmModule* module = trusted_instance_data->module();
  int sig_index = module->functions[func_index].sig_index;
  const i::wasm::FunctionSig* sig = module->signature(sig_index);
  i::DirectHandle<i::Code> wrapper;
  if (!internal::wasm::IsJSCompatibleSignature(sig)) {
    // If the signature is incompatible with JS, the original export will have
    // compiled an incompatible signature wrapper, so just reuse that.
    wrapper =
        i::DirectHandle<i::Code>(data->wrapper_code(i_isolate), i_isolate);
  } else {
    wrapper = BUILTIN_CODE(i_isolate, WasmPromising);
  }

  // TODO(14034): Create funcref RTTs lazily?
  i::DirectHandle<i::Map> rtt{
      i::Cast<i::Map>(
          trusted_instance_data->managed_object_maps()->get(sig_index)),
      i_isolate};

  int num_imported_functions = module->num_imported_functions;
  i::DirectHandle<i::TrustedObject> ref;
  if (func_index >= num_imported_functions) {
    ref = trusted_instance_data;
  } else {
    ref = i_isolate->factory()->NewWasmImportData(direct_handle(
        i::Cast<i::WasmImportData>(
            trusted_instance_data->dispatch_table_for_imports()->implicit_arg(
                func_index)),
        i_isolate));
  }

#if V8_ENABLE_SANDBOX
  uint64_t signature_hash =
      i::wasm::SignatureHasher::Hash(module->functions[func_index].sig);
#else
  uintptr_t signature_hash = 0;
#endif

  i::DirectHandle<i::WasmInternalFunction> internal =
      i_isolate->factory()->NewWasmInternalFunction(ref, func_index,
                                                    signature_hash);
  i::DirectHandle<i::WasmFuncRef> func_ref =
      i_isolate->factory()->NewWasmFuncRef(internal, rtt);
  internal->set_call_target(trusted_instance_data->GetCallTarget(func_index));
  if (func_index < num_imported_functions) {
    i::Cast<i::WasmImportData>(ref)->set_call_origin(*func_ref);
  }

  i::Handle<i::JSFunction> result = i::WasmExportedFunction::New(
      i_isolate, trusted_instance_data, func_ref, internal,
      static_cast<int>(data->sig()->parameter_count()), wrapper);
  return result;
}

// WebAssembly.Function
void WebAssemblyFunction(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Function()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Function must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsObject()) {
    thrower.TypeError("Argument 0 must be a function type");
    return;
  }
  Local<Object> function_type = Local<Object>::Cast(info[0]);
  Local<Context> context = isolate->GetCurrentContext();
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  // Load the 'parameters' property of the function type.
  Local<String> parameters_key = v8_str(isolate, "parameters");
  v8::MaybeLocal<v8::Value> parameters_maybe =
      function_type->Get(context, parameters_key);
  v8::Local<v8::Value> parameters_value;
  if (!parameters_maybe.ToLocal(&parameters_value) ||
      !parameters_value->IsObject()) {
    thrower.TypeError("Argument 0 must be a function type with 'parameters'");
    return;
  }
  Local<Object> parameters = parameters_value.As<Object>();
  uint32_t parameters_len = GetIterableLength(i_isolate, context, parameters);
  if (parameters_len == i::kMaxUInt32) {
    thrower.TypeError("Argument 0 contains parameters without 'length'");
    return;
  }
  if (parameters_len > i::wasm::kV8MaxWasmFunctionParams) {
    thrower.TypeError("Argument 0 contains too many parameters");
    return;
  }

  // Load the 'results' property of the function type.
  Local<String> results_key = v8_str(isolate, "results");
  v8::MaybeLocal<v8::Value> results_maybe =
      function_type->Get(context, results_key);
  v8::Local<v8::Value> results_value;
  if (!results_maybe.ToLocal(&results_value)) return;
  if (!results_value->IsObject()) {
    thrower.TypeError("Argument 0 must be a function type with 'results'");
    return;
  }
  Local<Object> results = results_value.As<Object>();
  uint32_t results_len = GetIterableLength(i_isolate, context, results);
  if (results_len == i::kMaxUInt32) {
    thrower.TypeError("Argument 0 contains results without 'length'");
    return;
  }
  if (results_len > i::wasm::kV8MaxWasmFunctionReturns) {
    thrower.TypeError("Argument 0 contains too many results");
    return;
  }

  // Decode the function type and construct a signature.
  i::Zone zone(i_isolate->allocator(), ZONE_NAME);
  i::wasm::FunctionSig::Builder builder(&zone, results_len, parameters_len);
  for (uint32_t i = 0; i < parameters_len; ++i) {
    i::wasm::ValueType type;
    MaybeLocal<Value> maybe = parameters->Get(context, i);
    if (!GetValueType(isolate, maybe, context, &type, enabled_features) ||
        type == i::wasm::kWasmVoid) {
      thrower.TypeError(
          "Argument 0 parameter type at index #%u must be a value type", i);
      return;
    }
    builder.AddParam(type);
  }
  for (uint32_t i = 0; i < results_len; ++i) {
    i::wasm::ValueType type;
    MaybeLocal<Value> maybe = results->Get(context, i);
    if (!GetValueType(isolate, maybe, context, &type, enabled_features)) return;
    if (type == i::wasm::kWasmVoid) {
      thrower.TypeError(
          "Argument 0 result type at index #%u must be a value type", i);
      return;
    }
    builder.AddReturn(type);
  }

  if (!info[1]->IsObject()) {
    thrower.TypeError("Argument 1 must be a function");
    return;
  }
  const i::wasm::FunctionSig* sig = builder.Build();
  i::wasm::Suspend suspend = i::wasm::kNoSuspend;

  i::Handle<i::JSReceiver> callable = Utils::OpenHandle(*info[1].As<Object>());
  if (i::IsWasmSuspendingObject(*callable)) {
    suspend = i::wasm::kSuspend;
    callable = handle(i::Cast<i::WasmSuspendingObject>(*callable)->callable(),
                      i_isolate);
    DCHECK(i::IsCallable(*callable));
  } else if (!i::IsCallable(*callable)) {
    thrower.TypeError("Argument 1 must be a function");
    return;
  }

  i::Handle<i::JSFunction> result =
      i::WasmJSFunction::New(i_isolate, sig, callable, suspend);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

// WebAssembly.promising(Function) -> Function
void WebAssemblyPromising(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i_isolate->CountUsage(v8::Isolate::kWasmJavaScriptPromiseIntegration);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.promising()");
  if (!info[0]->IsFunction()) {
    thrower.TypeError("Argument 0 must be a function");
    return;
  }
  i::DirectHandle<i::JSReceiver> callable =
      Utils::OpenDirectHandle(*info[0].As<Function>());

  if (!i::WasmExportedFunction::IsWasmExportedFunction(*callable)) {
    thrower.TypeError("Argument 0 must be a WebAssembly exported function");
    return;
  }
  auto wasm_exported_function = i::Cast<i::WasmExportedFunction>(*callable);
  i::DirectHandle<i::WasmExportedFunctionData> data(
      wasm_exported_function->shared()->wasm_exported_function_data(),
      i_isolate);
  if (data->instance_data()->module_object()->is_asm_js()) {
    thrower.TypeError("Argument 0 must be a WebAssembly exported function");
    return;
  }
  i::Handle<i::JSFunction> result =
      NewPromisingWasmExportedFunction(i_isolate, data, thrower);
  info.GetReturnValue().Set(Utils::ToLocal(i::Cast<i::JSObject>(result)));
  return;
}

// WebAssembly.Suspending(Function) -> Suspending
void WebAssemblySuspendingImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i_isolate->CountUsage(v8::Isolate::kWasmJavaScriptPromiseIntegration);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Suspending()");
  if (!info.IsConstructCall()) {
    thrower.TypeError("WebAssembly.Suspending must be invoked with 'new'");
    return;
  }
  if (!info[0]->IsFunction()) {
    thrower.TypeError("Argument 0 must be a function");
    return;
  }

  i::DirectHandle<i::JSReceiver> callable =
      Utils::OpenDirectHandle(*info[0].As<Function>());

  i::Handle<i::WasmSuspendingObject> result =
      i::WasmSuspendingObject::New(i_isolate, callable);
  info.GetReturnValue().Set(Utils::ToLocal(i::Cast<i::JSObject>(result)));
}

// WebAssembly.Function.prototype.type() -> FunctionType
void WebAssemblyFunctionType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Function.type()");

  const i::wasm::FunctionSig* sig;
  i::Zone zone(i_isolate->allocator(), ZONE_NAME);

  i::Handle<i::Object> fun = Utils::OpenHandle(*info.This());
  if (i::WasmExportedFunction::IsWasmExportedFunction(*fun)) {
    auto wasm_exported_function = i::Cast<i::WasmExportedFunction>(fun);
    i::Tagged<i::WasmExportedFunctionData> data =
        wasm_exported_function->shared()->wasm_exported_function_data();
    sig =
        data->instance_data()->module()->functions[data->function_index()].sig;
    i::wasm::Promise promise_flags =
        i::WasmFunctionData::PromiseField::decode(data->js_promise_flags());
    if (promise_flags == i::wasm::kPromise) {
      // The wrapper function returns a promise as an externref instead of the
      // original return type.
      size_t param_count = sig->parameter_count();
      i::wasm::FunctionSig::Builder builder(&zone, 1, param_count);
      for (size_t i = 0; i < param_count; ++i) {
        builder.AddParam(sig->GetParam(i));
      }
      builder.AddReturn(i::wasm::kWasmExternRef);
      sig = builder.Build();
    }
  } else if (i::WasmJSFunction::IsWasmJSFunction(*fun)) {
    sig = i::Cast<i::WasmJSFunction>(fun)
              ->shared()
              ->wasm_js_function_data()
              ->GetSignature();
  } else {
    thrower.TypeError("Receiver must be a WebAssembly.Function");
    return;
  }

  auto type = i::wasm::GetTypeForFunction(i_isolate, sig);
  info.GetReturnValue().Set(Utils::ToLocal(type));
}

constexpr const char* kName_WasmGlobalObject = "WebAssembly.Global";
constexpr const char* kName_WasmMemoryObject = "WebAssembly.Memory";
constexpr const char* kName_WasmInstanceObject = "WebAssembly.Instance";
constexpr const char* kName_WasmTableObject = "WebAssembly.Table";
constexpr const char* kName_WasmTagObject = "WebAssembly.Tag";
constexpr const char* kName_WasmExceptionPackage = "WebAssembly.Exception";

#define EXTRACT_THIS(var, WasmType)                                  \
  i::Handle<i::WasmType> var;                                        \
  {                                                                  \
    i::Handle<i::Object> this_arg = Utils::OpenHandle(*info.This()); \
    if (!Is##WasmType(*this_arg)) {                                  \
      thrower.TypeError("Receiver is not a %s", kName_##WasmType);   \
      return;                                                        \
    }                                                                \
    var = i::Cast<i::WasmType>(this_arg);                            \
  }

void WebAssemblyInstanceGetExportsImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Instance.exports()");
  EXTRACT_THIS(receiver, WasmInstanceObject);
  i::Handle<i::JSObject> exports_object(receiver->exports_object(), i_isolate);
  info.GetReturnValue().Set(Utils::ToLocal(exports_object));
}

void WebAssemblyTableGetLengthImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table.length()");
  EXTRACT_THIS(receiver, WasmTableObject);
  info.GetReturnValue().Set(
      v8::Number::New(isolate, receiver->current_length()));
}

// WebAssembly.Table.grow(num, init_value = null) -> num
void WebAssemblyTableGrowImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table.grow()");
  Local<Context> context = isolate->GetCurrentContext();
  EXTRACT_THIS(receiver, WasmTableObject);

  uint32_t grow_by;
  if (!EnforceUint32("Argument 0", info[0], context, &thrower, &grow_by)) {
    return;
  }

  i::Handle<i::Object> init_value;

  if (info.Length() >= 2) {
    init_value = Utils::OpenHandle(*info[1]);
    const char* error_message;
    if (!i::WasmTableObject::JSToWasmElement(i_isolate, receiver, init_value,
                                             &error_message)
             .ToHandle(&init_value)) {
      thrower.TypeError("Argument 1 is invalid: %s", error_message);
      return;
    }
  } else if (receiver->type().is_non_nullable()) {
    thrower.TypeError(
        "Argument 1 must be specified for non-nullable element type");
    return;
  } else {
    init_value = DefaultReferenceValue(i_isolate, receiver->type());
  }

  int old_size =
      i::WasmTableObject::Grow(i_isolate, receiver, grow_by, init_value);
  if (old_size < 0) {
    thrower.RangeError("failed to grow table by %u", grow_by);
    return;
  }
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(old_size);
}

namespace {
void WasmObjectToJSReturnValue(v8::ReturnValue<v8::Value>& return_value,
                               i::Handle<i::Object> value,
                               i::wasm::ValueType type, i::Isolate* isolate,
                               ErrorThrower* thrower) {
  switch (type.heap_type().representation()) {
    case internal::wasm::HeapType::kStringViewWtf8:
      thrower->TypeError("%s", "stringview_wtf8 has no JS representation");
      break;
    case internal::wasm::HeapType::kStringViewWtf16:
      thrower->TypeError("%s", "stringview_wtf16 has no JS representation");
      break;
    case internal::wasm::HeapType::kStringViewIter:
      thrower->TypeError("%s", "stringview_iter has no JS representation");
      break;
    case internal::wasm::HeapType::kExn:
    case internal::wasm::HeapType::kNoExn:
      thrower->TypeError("invalid type %s", type.name().c_str());
      break;
    default: {
      return_value.Set(Utils::ToLocal(i::wasm::WasmToJSObject(isolate, value)));
      break;
    }
  }
}
}  // namespace

// WebAssembly.Table.get(num) -> any
void WebAssemblyTableGetImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table.get()");
  Local<Context> context = isolate->GetCurrentContext();
  EXTRACT_THIS(receiver, WasmTableObject);

  uint32_t index;
  if (!EnforceUint32("Argument 0", info[0], context, &thrower, &index)) {
    return;
  }
  if (!receiver->is_in_bounds(index)) {
    thrower.RangeError("invalid index %u into %s table of size %d", index,
                       receiver->type().name().c_str(),
                       receiver->current_length());
    return;
  }

  i::Handle<i::Object> result =
      i::WasmTableObject::Get(i_isolate, receiver, index);

  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  WasmObjectToJSReturnValue(return_value, result, receiver->type(), i_isolate,
                            &thrower);
}

// WebAssembly.Table.set(num, any)
void WebAssemblyTableSetImpl(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table.set()");
  Local<Context> context = isolate->GetCurrentContext();
  EXTRACT_THIS(table_object, WasmTableObject);

  // Parameter 0.
  uint32_t index;
  if (!EnforceUint32("Argument 0", info[0], context, &thrower, &index)) {
    return;
  }
  if (!table_object->is_in_bounds(index)) {
    thrower.RangeError("invalid index %u into %s table of size %d", index,
                       table_object->type().name().c_str(),
                       table_object->current_length());
    return;
  }

  i::Handle<i::Object> element;
  if (info.Length() >= 2) {
    element = Utils::OpenHandle(*info[1]);
    const char* error_message;
    if (!i::WasmTableObject::JSToWasmElement(i_isolate, table_object, element,
                                             &error_message)
             .ToHandle(&element)) {
      thrower.TypeError("Argument 1 is invalid for table: %s", error_message);
      return;
    }
  } else if (table_object->type().is_defaultable()) {
    element = DefaultReferenceValue(i_isolate, table_object->type());
  } else {
    thrower.TypeError("Table of non-defaultable type %s needs explicit element",
                      table_object->type().name().c_str());
    return;
  }

  i::WasmTableObject::Set(i_isolate, table_object, index, element);
}

// WebAssembly.Table.type() -> TableType
void WebAssemblyTableType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Table.type()");

  EXTRACT_THIS(table, WasmTableObject);
  std::optional<uint32_t> max_size;
  if (!IsUndefined(table->maximum_length())) {
    uint64_t max_size64 = i::Object::NumberValue(table->maximum_length());
    DCHECK_LE(max_size64, std::numeric_limits<uint32_t>::max());
    max_size.emplace(static_cast<uint32_t>(max_size64));
  }
  auto type = i::wasm::GetTypeForTable(i_isolate, table->type(),
                                       table->current_length(), max_size,
                                       table->is_table64());
  info.GetReturnValue().Set(Utils::ToLocal(type));
}

// WebAssembly.Memory.grow(num) -> num
void WebAssemblyMemoryGrowImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Memory.grow()");
  Local<Context> context = isolate->GetCurrentContext();
  EXTRACT_THIS(receiver, WasmMemoryObject);

  uint32_t delta_pages;
  if (!EnforceUint32("Argument 0", info[0], context, &thrower, &delta_pages)) {
    return;
  }

  i::DirectHandle<i::JSArrayBuffer> old_buffer(receiver->array_buffer(),
                                               i_isolate);

  uint64_t old_pages64 = old_buffer->byte_length() / i::wasm::kWasmPageSize;
  uint64_t new_pages64 = old_pages64 + static_cast<uint64_t>(delta_pages);

  if (new_pages64 > static_cast<uint64_t>(receiver->maximum_pages())) {
    thrower.RangeError("Maximum memory size exceeded");
    return;
  }

  int32_t ret = i::WasmMemoryObject::Grow(i_isolate, receiver, delta_pages);
  if (ret == -1) {
    thrower.RangeError("Unable to grow instance memory");
    return;
  }
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(ret);
}

// WebAssembly.Memory.buffer -> ArrayBuffer
void WebAssemblyMemoryGetBufferImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Memory.buffer");
  EXTRACT_THIS(receiver, WasmMemoryObject);

  i::DirectHandle<i::Object> buffer_obj(receiver->array_buffer(), i_isolate);
  DCHECK(IsJSArrayBuffer(*buffer_obj));
  i::Handle<i::JSArrayBuffer> buffer(i::Cast<i::JSArrayBuffer>(*buffer_obj),
                                     i_isolate);
  if (buffer->is_shared()) {
    // TODO(gdeepti): More needed here for when cached buffer, and current
    // buffer are out of sync, handle that here when bounds checks, and Grow
    // are handled correctly.
    Maybe<bool> result =
        buffer->SetIntegrityLevel(i_isolate, buffer, i::FROZEN, i::kDontThrow);
    if (!result.FromJust()) {
      thrower.TypeError(
          "Status of setting SetIntegrityLevel of buffer is false.");
    }
  }
  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();
  return_value.Set(Utils::ToLocal(buffer));
}

// WebAssembly.Memory.type() -> MemoryType
void WebAssemblyMemoryType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Memory.type()");

  EXTRACT_THIS(memory, WasmMemoryObject);
  i::DirectHandle<i::JSArrayBuffer> buffer(memory->array_buffer(), i_isolate);
  size_t curr_size = buffer->byte_length() / i::wasm::kWasmPageSize;
  DCHECK_LE(curr_size, std::numeric_limits<uint32_t>::max());
  uint32_t min_size = static_cast<uint32_t>(curr_size);
  std::optional<uint32_t> max_size;
  if (memory->has_maximum_pages()) {
    uint64_t max_size64 = memory->maximum_pages();
    DCHECK_LE(max_size64, std::numeric_limits<uint32_t>::max());
    max_size.emplace(static_cast<uint32_t>(max_size64));
  }
  bool shared = buffer->is_shared();
  auto type = i::wasm::GetTypeForMemory(i_isolate, min_size, max_size, shared,
                                        memory->is_memory64());
  info.GetReturnValue().Set(Utils::ToLocal(type));
}

// WebAssembly.Tag.type() -> FunctionType
void WebAssemblyTagType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Tag.type()");

  EXTRACT_THIS(tag, WasmTagObject);
  if (thrower.error()) return;

  int n = tag->serialized_signature()->length();
  std::vector<i::wasm::ValueType> data(n);
  if (n > 0) {
    tag->serialized_signature()->copy_out(0, data.data(), n);
  }
  const i::wasm::FunctionSig sig{0, data.size(), data.data()};
  constexpr bool kForException = true;
  auto type = i::wasm::GetTypeForFunction(i_isolate, &sig, kForException);
  info.GetReturnValue().Set(Utils::ToLocal(type));
}

void WebAssemblyExceptionGetArgImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Exception.getArg()");

  EXTRACT_THIS(exception, WasmExceptionPackage);
  if (thrower.error()) return;

  i::MaybeHandle<i::WasmTagObject> maybe_tag =
      GetFirstArgumentAsTag(info, &thrower);
  if (thrower.error()) return;
  auto tag = maybe_tag.ToHandleChecked();
  Local<Context> context = isolate->GetCurrentContext();
  uint32_t index;
  if (!EnforceUint32("Index", info[1], context, &thrower, &index)) {
    return;
  }
  auto maybe_values =
      i::WasmExceptionPackage::GetExceptionValues(i_isolate, exception);

  auto this_tag =
      i::WasmExceptionPackage::GetExceptionTag(i_isolate, exception);
  DCHECK(IsWasmExceptionTag(*this_tag));
  if (tag->tag() != *this_tag) {
    thrower.TypeError("First argument does not match the exception tag");
    return;
  }

  DCHECK(!IsUndefined(*maybe_values));
  auto values = i::Cast<i::FixedArray>(maybe_values);
  auto signature = tag->serialized_signature();
  if (index >= static_cast<uint32_t>(signature->length())) {
    thrower.RangeError("Index out of range");
    return;
  }
  // First, find the index in the values array.
  uint32_t decode_index = 0;
  // Since the bounds check above passed, the cast to int is safe.
  for (int i = 0; i < static_cast<int>(index); ++i) {
    switch (signature->get(i).kind()) {
      case i::wasm::kI32:
      case i::wasm::kF32:
        decode_index += 2;
        break;
      case i::wasm::kI64:
      case i::wasm::kF64:
        decode_index += 4;
        break;
      case i::wasm::kRef:
      case i::wasm::kRefNull:
        decode_index++;
        break;
      case i::wasm::kRtt:
      case i::wasm::kI8:
      case i::wasm::kI16:
      case i::wasm::kF16:
      case i::wasm::kVoid:
      case i::wasm::kBottom:
      case i::wasm::kS128:
        UNREACHABLE();
    }
  }
  // Decode the value at {decode_index}.
  Local<Value> result;
  switch (signature->get(index).kind()) {
    case i::wasm::kI32: {
      uint32_t u32_bits = 0;
      i::DecodeI32ExceptionValue(values, &decode_index, &u32_bits);
      int32_t i32 = static_cast<int32_t>(u32_bits);
      result = v8::Integer::New(isolate, i32);
      break;
    }
    case i::wasm::kI64: {
      uint64_t u64_bits = 0;
      i::DecodeI64ExceptionValue(values, &decode_index, &u64_bits);
      int64_t i64 = static_cast<int64_t>(u64_bits);
      result = v8::BigInt::New(isolate, i64);
      break;
    }
    case i::wasm::kF32: {
      uint32_t f32_bits = 0;
      DecodeI32ExceptionValue(values, &decode_index, &f32_bits);
      float f32 = base::bit_cast<float>(f32_bits);
      result = v8::Number::New(isolate, f32);
      break;
    }
    case i::wasm::kF64: {
      uint64_t f64_bits = 0;
      DecodeI64ExceptionValue(values, &decode_index, &f64_bits);
      double f64 = base::bit_cast<double>(f64_bits);
      result = v8::Number::New(isolate, f64);
      break;
    }
    case i::wasm::kRef:
    case i::wasm::kRefNull: {
      i::Handle<i::Object> obj = handle(values->get(decode_index), i_isolate);
      ReturnValue<Value> return_value = info.GetReturnValue();
      return WasmObjectToJSReturnValue(return_value, obj, signature->get(index),
                                       i_isolate, &thrower);
    }
    case i::wasm::kRtt:
    case i::wasm::kI8:
    case i::wasm::kI16:
    case i::wasm::kF16:
    case i::wasm::kVoid:
    case i::wasm::kBottom:
    case i::wasm::kS128:
      UNREACHABLE();
  }
  info.GetReturnValue().Set(result);
}

void WebAssemblyExceptionIsImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Exception.is()");

  EXTRACT_THIS(exception, WasmExceptionPackage);
  if (thrower.error()) return;

  auto tag = i::WasmExceptionPackage::GetExceptionTag(i_isolate, exception);
  DCHECK(IsWasmExceptionTag(*tag));

  auto maybe_tag = GetFirstArgumentAsTag(info, &thrower);
  if (thrower.error()) {
    return;
  }
  auto tag_arg = maybe_tag.ToHandleChecked();
  info.GetReturnValue().Set(tag_arg->tag() == *tag);
}

void WebAssemblyGlobalGetValueCommon(
    const v8::FunctionCallbackInfo<v8::Value>& info, const char* name) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  ErrorThrower thrower(i_isolate, name);
  EXTRACT_THIS(receiver, WasmGlobalObject);

  v8::ReturnValue<v8::Value> return_value = info.GetReturnValue();

  switch (receiver->type().kind()) {
    case i::wasm::kI32:
      return_value.Set(receiver->GetI32());
      break;
    case i::wasm::kI64: {
      Local<BigInt> value = BigInt::New(isolate, receiver->GetI64());
      return_value.Set(value);
      break;
    }
    case i::wasm::kF32:
      return_value.Set(receiver->GetF32());
      break;
    case i::wasm::kF64:
      return_value.Set(receiver->GetF64());
      break;
    case i::wasm::kS128:
      thrower.TypeError("Can't get the value of s128 WebAssembly.Global");
      break;
    case i::wasm::kRef:
    case i::wasm::kRefNull: {
      WasmObjectToJSReturnValue(return_value, receiver->GetRef(),
                                receiver->type(), i_isolate, &thrower);
      break;
    }
    case i::wasm::kRtt:
    case i::wasm::kI8:
    case i::wasm::kI16:
    case i::wasm::kF16:
    case i::wasm::kBottom:
    case i::wasm::kVoid:
      UNREACHABLE();
  }
}

// WebAssembly.Global.valueOf() -> num
void WebAssemblyGlobalValueOfImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  return WebAssemblyGlobalGetValueCommon(info, "WebAssembly.Global.valueOf()");
}

// get WebAssembly.Global.value -> num
void WebAssemblyGlobalGetValueImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  return WebAssemblyGlobalGetValueCommon(info, "get WebAssembly.Global.value");
}

// set WebAssembly.Global.value(num)
void WebAssemblyGlobalSetValueImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  HandleScope scope(isolate);
  Local<Context> context = isolate->GetCurrentContext();
  ErrorThrower thrower(i_isolate, "set WebAssembly.Global.value");
  EXTRACT_THIS(receiver, WasmGlobalObject);

  if (!receiver->is_mutable()) {
    thrower.TypeError("Can't set the value of an immutable global.");
    return;
  }
  if (info.Length() == 0) {
    thrower.TypeError("Argument 0 is required");
    return;
  }

  switch (receiver->type().kind()) {
    case i::wasm::kI32: {
      int32_t i32_value = 0;
      if (!info[0]->Int32Value(context).To(&i32_value)) return;
      receiver->SetI32(i32_value);
      break;
    }
    case i::wasm::kI64: {
      v8::Local<v8::BigInt> bigint_value;
      if (!info[0]->ToBigInt(context).ToLocal(&bigint_value)) return;
      receiver->SetI64(bigint_value->Int64Value());
      break;
    }
    case i::wasm::kF32: {
      double f64_value = 0;
      if (!info[0]->NumberValue(context).To(&f64_value)) return;
      receiver->SetF32(i::DoubleToFloat32(f64_value));
      break;
    }
    case i::wasm::kF64: {
      double f64_value = 0;
      if (!info[0]->NumberValue(context).To(&f64_value)) return;
      receiver->SetF64(f64_value);
      break;
    }
    case i::wasm::kS128:
      thrower.TypeError("Can't set the value of s128 WebAssembly.Global");
      break;
    case i::wasm::kRef:
    case i::wasm::kRefNull: {
      const i::wasm::WasmModule* module =
          receiver->has_trusted_data()
              ? receiver->trusted_data(i_isolate)->module()
              : nullptr;
      i::Handle<i::Object> value = Utils::OpenHandle(*info[0]);
      const char* error_message;
      if (!i::wasm::JSToWasmObject(i_isolate, module, value, receiver->type(),
                                   &error_message)
               .ToHandle(&value)) {
        thrower.TypeError("%s", error_message);
        return;
      }
      receiver->SetRef(value);
      return;
    }
    case i::wasm::kRtt:
    case i::wasm::kI8:
    case i::wasm::kI16:
    case i::wasm::kF16:
    case i::wasm::kBottom:
    case i::wasm::kVoid:
      UNREACHABLE();
  }
}

// WebAssembly.Global.type() -> GlobalType
void WebAssemblyGlobalType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  HandleScope scope(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  ErrorThrower thrower(i_isolate, "WebAssembly.Global.type()");

  EXTRACT_THIS(global, WasmGlobalObject);
  auto type = i::wasm::GetTypeForGlobal(i_isolate, global->is_mutable(),
                                        global->type());
  info.GetReturnValue().Set(Utils::ToLocal(type));
}

}  // namespace

namespace internal {
namespace wasm {

#define DEF_WASM_JS_EXTERNAL_REFERENCE(Name)                   \
  void Name(const v8::FunctionCallbackInfo<v8::Value>& info) { \
    Name##Impl(info);                                          \
  }
WASM_JS_EXTERNAL_REFERENCE_LIST(DEF_WASM_JS_EXTERNAL_REFERENCE)
#undef DEF_WASM_JS_EXTERNAL_REFERENCE

}  // namespace wasm
}  // namespace internal

// TODO(titzer): we use the API to create the function template because the
// internal guts are too ugly to replicate here.
static i::Handle<i::FunctionTemplateInfo> NewFunctionTemplate(
    i::Isolate* i_isolate, FunctionCallback func, bool has_prototype,
    SideEffectType side_effect_type = SideEffectType::kHasSideEffect) {
  Isolate* isolate = reinterpret_cast<Isolate*>(i_isolate);
  ConstructorBehavior behavior =
      has_prototype ? ConstructorBehavior::kAllow : ConstructorBehavior::kThrow;
  Local<FunctionTemplate> templ = FunctionTemplate::New(
      isolate, func, {}, {}, 0, behavior, side_effect_type);
  if (has_prototype) templ->ReadOnlyPrototype();
  return v8::Utils::OpenHandle(*templ);
}

static i::Handle<i::ObjectTemplateInfo> NewObjectTemplate(
    i::Isolate* i_isolate) {
  Isolate* isolate = reinterpret_cast<Isolate*>(i_isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  return v8::Utils::OpenHandle(*templ);
}

namespace internal {
namespace {

Handle<JSFunction> CreateFunc(
    Isolate* isolate, Handle<String> name, FunctionCallback func,
    bool has_prototype,
    SideEffectType side_effect_type = SideEffectType::kHasSideEffect,
    Handle<FunctionTemplateInfo> parent = {}) {
  Handle<FunctionTemplateInfo> temp =
      NewFunctionTemplate(isolate, func, has_prototype, side_effect_type);

  if (!parent.is_null()) {
    DCHECK(has_prototype);
    FunctionTemplateInfo::SetParentTemplate(isolate, temp, parent);
  }

  Handle<JSFunction> function =
      ApiNatives::InstantiateFunction(isolate, temp, name).ToHandleChecked();
  DCHECK(function->shared()->HasSharedName());
  return function;
}

Handle<JSFunction> InstallFunc(
    Isolate* isolate, Handle<JSObject> object, Handle<String> name,
    FunctionCallback func, int length, bool has_prototype = false,
    PropertyAttributes attributes = NONE,
    SideEffectType side_effect_type = SideEffectType::kHasSideEffect) {
  Handle<JSFunction> function =
      CreateFunc(isolate, name, func, has_prototype, side_effect_type);
  function->shared()->set_length(length);
  CHECK(!JSObject::HasRealNamedProperty(isolate, object, name).FromMaybe(true));
  JSObject::AddProperty(isolate, object, name, function, attributes);
  return function;
}

Handle<JSFunction> InstallFunc(
    Isolate* isolate, Handle<JSObject> object, const char* str,
    FunctionCallback func, int length, bool has_prototype = false,
    PropertyAttributes attributes = NONE,
    SideEffectType side_effect_type = SideEffectType::kHasSideEffect) {
  Handle<String> name = v8_str(isolate, str);
  return InstallFunc(isolate, object, name, func, length, has_prototype,
                     attributes, side_effect_type);
}

Handle<JSFunction> InstallConstructorFunc(Isolate* isolate,
                                          Handle<JSObject> object,
                                          const char* str,
                                          FunctionCallback func) {
  return InstallFunc(isolate, object, str, func, 1, true, DONT_ENUM,
                     SideEffectType::kHasNoSideEffect);
}

Handle<String> GetterName(Isolate* isolate, Handle<String> name) {
  return Name::ToFunctionName(isolate, name, isolate->factory()->get_string())
      .ToHandleChecked();
}

void InstallGetter(Isolate* isolate, Handle<JSObject> object, const char* str,
                   FunctionCallback func) {
  Handle<String> name = v8_str(isolate, str);
  Handle<JSFunction> function =
      CreateFunc(isolate, GetterName(isolate, name), func, false,
                 SideEffectType::kHasNoSideEffect);

  Utils::ToLocal(object)->SetAccessorProperty(Utils::ToLocal(name),
                                              Utils::ToLocal(function),
                                              Local<Function>(), v8::None);
}

Handle<String> SetterName(Isolate* isolate, Handle<String> name) {
  return Name::ToFunctionName(isolate, name, isolate->factory()->set_string())
      .ToHandleChecked();
}

void InstallGetterSetter(Isolate* isolate, Handle<JSObject> object,
                         const char* str, FunctionCallback getter,
                         FunctionCallback setter) {
  Handle<String> name = v8_str(isolate, str);
  Handle<JSFunction> getter_func =
      CreateFunc(isolate, GetterName(isolate, name), getter, false,
                 SideEffectType::kHasNoSideEffect);
  Handle<JSFunction> setter_func =
      CreateFunc(isolate, SetterName(isolate, name), setter, false);
  setter_func->shared()->set_length(1);

  Utils::ToLocal(object)->SetAccessorProperty(
      Utils::ToLocal(name), Utils::ToLocal(getter_func),
      Utils::ToLocal(setter_func), v8::None);
}

// Assigns a dummy instance template to the given constructor function. Used to
// make sure the implicit receivers for the constructors in this file have an
// instance type different from the internal one, they allocate the resulting
// object explicitly and ignore implicit receiver.
void SetDummyInstanceTemplate(Isolate* isolate, DirectHandle<JSFunction> fun) {
  DirectHandle<ObjectTemplateInfo> instance_template =
      NewObjectTemplate(isolate);
  FunctionTemplateInfo::SetInstanceTemplate(
      isolate, direct_handle(fun->shared()->api_func_data(), isolate),
      instance_template);
}

Handle<JSObject> SetupConstructor(Isolate* isolate,
                                  Handle<JSFunction> constructor,
                                  InstanceType instance_type, int instance_size,
                                  const char* name = nullptr,
                                  int in_object_properties = 0) {
  SetDummyInstanceTemplate(isolate, constructor);
  JSFunction::EnsureHasInitialMap(constructor);
  Handle<JSObject> proto(Cast<JSObject>(constructor->instance_prototype()),
                         isolate);
  Handle<Map> map = isolate->factory()->NewContextfulMap(
      constructor, instance_type, instance_size, TERMINAL_FAST_ELEMENTS_KIND,
      in_object_properties);
  JSFunction::SetInitialMap(isolate, constructor, map, proto);
  constexpr PropertyAttributes ro_attributes =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
  if (name) {
    JSObject::AddProperty(isolate, proto,
                          isolate->factory()->to_string_tag_symbol(),
                          v8_str(isolate, name), ro_attributes);
  }
  return proto;
}

constexpr wasm::ValueType kWasmExceptionTagParams[] = {
    wasm::kWasmExternRef,
};
constexpr wasm::FunctionSig kWasmExceptionTagSignature{
    0, arraysize(kWasmExceptionTagParams), kWasmExceptionTagParams};
}  // namespace

// static
void WasmJs::PrepareForSnapshot(Isolate* isolate) {
  DirectHandle<JSGlobalObject> global = isolate->global_object();
  Handle<NativeContext> native_context(global->native_context(), isolate);

  CHECK(IsUndefined(native_context->get(Context::WASM_MODULE_CONSTRUCTOR_INDEX),
                    isolate));

  Factory* const f = isolate->factory();
  static constexpr PropertyAttributes ro_attributes =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);

  // Create the WebAssembly object.
  Handle<JSObject> webassembly;
  {
    Handle<String> WebAssembly_string = v8_str(isolate, "WebAssembly");
    // Not supposed to be called, hence using the kIllegal builtin as code.
    Handle<SharedFunctionInfo> sfi = f->NewSharedFunctionInfoForBuiltin(
        WebAssembly_string, Builtin::kIllegal);
    sfi->set_language_mode(LanguageMode::kStrict);

    Handle<JSFunction> ctor =
        Factory::JSFunctionBuilder{isolate, sfi, native_context}.Build();
    JSFunction::SetPrototype(ctor, isolate->initial_object_prototype());
    webassembly = f->NewJSObject(ctor, AllocationType::kOld);
    native_context->set_wasm_webassembly_object(*webassembly);

    JSObject::AddProperty(isolate, webassembly, f->to_string_tag_symbol(),
                          WebAssembly_string, ro_attributes);
    InstallFunc(isolate, webassembly, "compile", wasm::WebAssemblyCompile, 1);
    InstallFunc(isolate, webassembly, "validate", wasm::WebAssemblyValidate, 1);
    InstallFunc(isolate, webassembly, "instantiate",
                wasm::WebAssemblyInstantiate, 1);
  }

  // Create the Module object.
  InstallModule(isolate, webassembly);

  // Create the Instance object.
  {
    Handle<JSFunction> instance_constructor = InstallConstructorFunc(
        isolate, webassembly, "Instance", wasm::WebAssemblyInstance);
    Handle<JSObject> instance_proto = SetupConstructor(
        isolate, instance_constructor, WASM_INSTANCE_OBJECT_TYPE,
        WasmInstanceObject::kHeaderSize, "WebAssembly.Instance");
    native_context->set_wasm_instance_constructor(*instance_constructor);
    InstallGetter(isolate, instance_proto, "exports",
                  wasm::WebAssemblyInstanceGetExports);
  }

  // Create the Table object.
  {
    Handle<JSFunction> table_constructor = InstallConstructorFunc(
        isolate, webassembly, "Table", wasm::WebAssemblyTable);
    Handle<JSObject> table_proto =
        SetupConstructor(isolate, table_constructor, WASM_TABLE_OBJECT_TYPE,
                         WasmTableObject::kHeaderSize, "WebAssembly.Table");
    native_context->set_wasm_table_constructor(*table_constructor);
    InstallGetter(isolate, table_proto, "length",
                  wasm::WebAssemblyTableGetLength);
    InstallFunc(isolate, table_proto, "grow", wasm::WebAssemblyTableGrow, 1);
    InstallFunc(isolate, table_proto, "set", wasm::WebAssemblyTableSet, 1);
    InstallFunc(isolate, table_proto, "get", wasm::WebAssemblyTableGet, 1,
                false, NONE, SideEffectType::kHasNoSideEffect);
  }

  // Create the Memory object.
  {
    Handle<JSFunction> memory_constructor = InstallConstructorFunc(
        isolate, webassembly, "Memory", wasm::WebAssemblyMemory);
    Handle<JSObject> memory_proto =
        SetupConstructor(isolate, memory_constructor, WASM_MEMORY_OBJECT_TYPE,
                         WasmMemoryObject::kHeaderSize, "WebAssembly.Memory");
    native_context->set_wasm_memory_constructor(*memory_constructor);
    InstallFunc(isolate, memory_proto, "grow", wasm::WebAssemblyMemoryGrow, 1);
    InstallGetter(isolate, memory_proto, "buffer",
                  wasm::WebAssemblyMemoryGetBuffer);
  }

  // Create the Global object.
  {
    Handle<JSFunction> global_constructor = InstallConstructorFunc(
        isolate, webassembly, "Global", wasm::WebAssemblyGlobal);
    Handle<JSObject> global_proto =
        SetupConstructor(isolate, global_constructor, WASM_GLOBAL_OBJECT_TYPE,
                         WasmGlobalObject::kHeaderSize, "WebAssembly.Global");
    native_context->set_wasm_global_constructor(*global_constructor);
    InstallFunc(isolate, global_proto, "valueOf",
                wasm::WebAssemblyGlobalValueOf, 0, false, NONE,
                SideEffectType::kHasNoSideEffect);
    InstallGetterSetter(isolate, global_proto, "value",
                        wasm::WebAssemblyGlobalGetValue,
                        wasm::WebAssemblyGlobalSetValue);
  }

  // Create the Exception object.
  {
    Handle<JSFunction> tag_constructor = InstallConstructorFunc(
        isolate, webassembly, "Tag", wasm::WebAssemblyTag);
    SetupConstructor(isolate, tag_constructor, WASM_TAG_OBJECT_TYPE,
                     WasmTagObject::kHeaderSize, "WebAssembly.Tag");
    native_context->set_wasm_tag_constructor(*tag_constructor);
    auto js_tag = WasmExceptionTag::New(isolate, 0);
    // Note the canonical_type_index is reset in WasmJs::Install s.t.
    // type_canonicalizer bookkeeping remains valid.
    static constexpr uint32_t kInitialCanonicalTypeIndex = 0;
    DirectHandle<JSObject> js_tag_object = WasmTagObject::New(
        isolate, &kWasmExceptionTagSignature, kInitialCanonicalTypeIndex,
        js_tag, Handle<WasmTrustedInstanceData>());
    native_context->set_wasm_js_tag(*js_tag_object);
    JSObject::AddProperty(isolate, webassembly, "JSTag", js_tag_object,
                          ro_attributes);
  }

  // Set up the runtime exception constructor.
  {
    Handle<JSFunction> exception_constructor = InstallConstructorFunc(
        isolate, webassembly, "Exception", wasm::WebAssemblyException);
    SetDummyInstanceTemplate(isolate, exception_constructor);
    Handle<JSObject> exception_proto = SetupConstructor(
        isolate, exception_constructor, WASM_EXCEPTION_PACKAGE_TYPE,
        WasmExceptionPackage::kSize, "WebAssembly.Exception",
        WasmExceptionPackage::kInObjectFieldCount);
    InstallFunc(isolate, exception_proto, "getArg",
                wasm::WebAssemblyExceptionGetArg, 2);
    InstallFunc(isolate, exception_proto, "is", wasm::WebAssemblyExceptionIs,
                1);
    native_context->set_wasm_exception_constructor(*exception_constructor);

    DirectHandle<Map> initial_map(exception_constructor->initial_map(),
                                  isolate);
    Map::EnsureDescriptorSlack(isolate, initial_map, 2);
    {
      Descriptor d = Descriptor::DataField(
          isolate, f->wasm_exception_tag_symbol(),
          WasmExceptionPackage::kTagIndex, DONT_ENUM, Representation::Tagged());
      initial_map->AppendDescriptor(isolate, &d);
    }
    {
      Descriptor d =
          Descriptor::DataField(isolate, f->wasm_exception_values_symbol(),
                                WasmExceptionPackage::kValuesIndex, DONT_ENUM,
                                Representation::Tagged());
      initial_map->AppendDescriptor(isolate, &d);
    }
  }

  // By default, make all exported functions an instance of {Function}.
  {
    DirectHandle<Map> function_map =
        isolate->sloppy_function_without_prototype_map();
    native_context->set_wasm_exported_function_map(*function_map);
  }

  // Setup errors.
  {
    DirectHandle<JSFunction> compile_error(
        native_context->wasm_compile_error_function(), isolate);
    JSObject::AddProperty(isolate, webassembly, f->CompileError_string(),
                          compile_error, DONT_ENUM);
    DirectHandle<JSFunction> link_error(
        native_context->wasm_link_error_function(), isolate);
    JSObject::AddProperty(isolate, webassembly, f->LinkError_string(),
                          link_error, DONT_ENUM);
    DirectHandle<JSFunction> runtime_error(
        native_context->wasm_runtime_error_function(), isolate);
    JSObject::AddProperty(isolate, webassembly, f->RuntimeError_string(),
                          runtime_error, DONT_ENUM);
  }
}

void WasmJs::InstallModule(Isolate* isolate, Handle<JSObject> webassembly) {
  Handle<JSGlobalObject> global = isolate->global_object();
  Handle<NativeContext> native_context(global->native_context(), isolate);

  Handle<JSFunction> module_constructor;
  if (v8_flags.js_source_phase_imports) {
    Handle<FunctionTemplateInfo>
        intrinsic_abstract_module_source_interface_template =
            NewFunctionTemplate(isolate, nullptr, false);
    Handle<JSObject> abstract_module_source_prototype = Handle<JSObject>(
        native_context->abstract_module_source_prototype(), isolate);
    ApiNatives::AddDataProperty(
        isolate, intrinsic_abstract_module_source_interface_template,
        v8_str(isolate, "prototype"), abstract_module_source_prototype, NONE);

    // Check that this is a reinstallation of the Module object.
    Handle<String> name = v8_str(isolate, "Module");
    DCHECK(
        JSObject::HasRealNamedProperty(isolate, webassembly, name).ToChecked());
    // Reinstall the Module object with AbstractModuleSource as prototype.
    module_constructor =
        CreateFunc(isolate, name, wasm::WebAssemblyModule, true,
                   SideEffectType::kHasNoSideEffect,
                   intrinsic_abstract_module_source_interface_template);
    module_constructor->shared()->set_length(1);
    JSObject::SetOwnPropertyIgnoreAttributes(webassembly, name,
                                             module_constructor, DONT_ENUM)
        .Assert();
  } else {
    module_constructor = InstallConstructorFunc(isolate, webassembly, "Module",
                                                wasm::WebAssemblyModule);
  }
  SetupConstructor(isolate, module_constructor, WASM_MODULE_OBJECT_TYPE,
                   WasmModuleObject::kHeaderSize, "WebAssembly.Module");
  native_context->set_wasm_module_constructor(*module_constructor);

  InstallFunc(isolate, module_constructor, "imports",
              wasm::WebAssemblyModuleImports, 1, false, NONE,
              SideEffectType::kHasNoSideEffect);
  InstallFunc(isolate, module_constructor, "exports",
              wasm::WebAssemblyModuleExports, 1, false, NONE,
              SideEffectType::kHasNoSideEffect);
  InstallFunc(isolate, module_constructor, "customSections",
              wasm::WebAssemblyModuleCustomSections, 2, false, NONE,
              SideEffectType::kHasNoSideEffect);
}

// static
void WasmJs::Install(Isolate* isolate, bool exposed_on_global_object) {
  Handle<JSGlobalObject> global = isolate->global_object();
  DirectHandle<NativeContext> native_context(global->native_context(), isolate);

  if (native_context->is_wasm_js_installed() != Smi::zero()) return;
  native_context->set_is_wasm_js_installed(Smi::FromInt(1));

  // We can get the WebAssembly object here from the native context because no
  // user code has been executed yet. However, once user code has been executed,
  // the WebAssembly object has to be retrieved with a JavaScript property
  // lookup.
  Handle<JSObject> webassembly(native_context->wasm_webassembly_object(),
                               isolate);
  if (v8_flags.js_source_phase_imports) {
    // Reinstall the Module object with the experimental interface.
    InstallModule(isolate, webassembly);
  }

  // Expose the API on the global object if configured to do so.
  if (exposed_on_global_object) {
    Handle<String> WebAssembly_string = v8_str(isolate, "WebAssembly");
    JSObject::AddProperty(isolate, global, WebAssembly_string, webassembly,
                          DONT_ENUM);
  }

  // Reset canonical_type_index based on this Isolate's type_canonicalizer.
  {
    DirectHandle<WasmTagObject> js_tag_object(
        Cast<WasmTagObject>(native_context->wasm_js_tag()), isolate);
    js_tag_object->set_canonical_type_index(
        wasm::GetWasmEngine()->type_canonicalizer()->AddRecursiveGroup(
            &kWasmExceptionTagSignature));
  }

  if (v8_flags.wasm_test_streaming) {
    isolate->set_wasm_streaming_callback(WasmStreamingCallbackForTesting);
  }

  if (isolate->wasm_streaming_callback() != nullptr) {
    InstallFunc(isolate, webassembly, "compileStreaming",
                WebAssemblyCompileStreaming, 1);
    InstallFunc(isolate, webassembly, "instantiateStreaming",
                WebAssemblyInstantiateStreaming, 1);
  }

  // The native_context is not set up completely yet. That's why we cannot use
  // {WasmEnabledFeatures::FromIsolate} and have to use
  // {WasmEnabledFeatures::FromFlags} instead.
  const auto enabled_features = wasm::WasmEnabledFeatures::FromFlags();

  if (enabled_features.has_type_reflection()) {
    InstallTypeReflection(isolate, native_context, webassembly);
  }

  // Initialize and install JSPI feature.
  if (enabled_features.has_jspi()) {
    CHECK(native_context->is_wasm_jspi_installed() == Smi::zero());
    isolate->WasmInitJSPIFeature();
    InstallJSPromiseIntegration(isolate, native_context, webassembly);
    native_context->set_is_wasm_jspi_installed(Smi::FromInt(1));
  }
}

// static
void WasmJs::InstallConditionalFeatures(Isolate* isolate,
                                        Handle<NativeContext> context) {
  Handle<JSGlobalObject> global = handle(context->global_object(), isolate);
  // If some fuzzer decided to make the global object non-extensible, then
  // we can't install any features (and would CHECK-fail if we tried).
  if (!global->map()->is_extensible()) return;

  MaybeHandle<Object> maybe_wasm =
      JSReceiver::GetProperty(isolate, global, "WebAssembly");
  Handle<Object> wasm_obj;
  if (!maybe_wasm.ToHandle(&wasm_obj) || !IsJSObject(*wasm_obj)) return;
  Handle<JSObject> webassembly = Cast<JSObject>(wasm_obj);
  if (!webassembly->map()->is_extensible()) return;
  if (webassembly->map()->is_access_check_needed()) return;

  /*
    If you need to install some optional features, follow the pattern:

    if (isolate->IsMyWasmFeatureEnabled(context)) {
      Handle<String> feature = isolate->factory()->...;
      if (!JSObject::HasRealNamedProperty(isolate, webassembly, feature)
               .FromMaybe(true)) {
        InstallFeature(isolate, webassembly);
      }
    }
  */

  // Install JSPI-related features.
  if (isolate->IsWasmJSPIRequested(context)) {
    if (context->is_wasm_jspi_installed() == Smi::zero()) {
      isolate->WasmInitJSPIFeature();
      if (InstallJSPromiseIntegration(isolate, context, webassembly) &&
          InstallTypeReflection(isolate, context, webassembly)) {
        context->set_is_wasm_jspi_installed(Smi::FromInt(1));
      }
    }
  }
}

// static
// Return true if this call results in JSPI being installed.
bool WasmJs::InstallJSPromiseIntegration(Isolate* isolate,
                                         DirectHandle<NativeContext> context,
                                         Handle<JSObject> webassembly) {
  Handle<String> suspender_string = v8_str(isolate, "Suspender");
  if (JSObject::HasRealNamedProperty(isolate, webassembly, suspender_string)
          .FromMaybe(true)) {
    return false;
  }
  Handle<String> suspending_string = v8_str(isolate, "Suspending");
  if (JSObject::HasRealNamedProperty(isolate, webassembly, suspending_string)
          .FromMaybe(true)) {
    return false;
  }
  Handle<String> promising_string = v8_str(isolate, "promising");
  if (JSObject::HasRealNamedProperty(isolate, webassembly, promising_string)
          .FromMaybe(true)) {
    return false;
  }
  Handle<JSFunction> suspending_constructor = InstallConstructorFunc(
      isolate, webassembly, "Suspending", WebAssemblySuspendingImpl);
  context->set_wasm_suspending_constructor(*suspending_constructor);
  SetupConstructor(isolate, suspending_constructor, WASM_SUSPENDING_OBJECT_TYPE,
                   WasmSuspendingObject::kHeaderSize, "WebAssembly.Suspending");
  InstallFunc(isolate, webassembly, "promising", WebAssemblyPromising, 1);
  return true;
}

// static
// Return true only if this call resulted in installation of type reflection.
bool WasmJs::InstallTypeReflection(Isolate* isolate,
                                   DirectHandle<NativeContext> context,
                                   Handle<JSObject> webassembly) {
  // First check if any of the type reflection fields already exist. If so, bail
  // out and don't install any new fields.
  if (JSObject::HasRealNamedProperty(isolate, webassembly,
                                     isolate->factory()->Function_string())
          .FromMaybe(true)) {
    return false;
  }

  Handle<JSObject> table_proto(
      Cast<JSObject>(context->wasm_table_constructor()->instance_prototype()),
      isolate);
  Handle<JSObject> global_proto(
      Cast<JSObject>(context->wasm_global_constructor()->instance_prototype()),
      isolate);
  Handle<JSObject> memory_proto(
      Cast<JSObject>(context->wasm_memory_constructor()->instance_prototype()),
      isolate);
  Handle<JSObject> tag_proto(
      Cast<JSObject>(context->wasm_tag_constructor()->instance_prototype()),
      isolate);

  Handle<String> type_string = v8_str(isolate, "type");
  if (JSObject::HasRealNamedProperty(isolate, table_proto, type_string)
          .FromMaybe(true)) {
    return false;
  }
  if (JSObject::HasRealNamedProperty(isolate, global_proto, type_string)
          .FromMaybe(true)) {
    return false;
  }
  if (JSObject::HasRealNamedProperty(isolate, memory_proto, type_string)
          .FromMaybe(true)) {
    return false;
  }
  if (JSObject::HasRealNamedProperty(isolate, tag_proto, type_string)
          .FromMaybe(true)) {
    return false;
  }

  // Ensure prototype objects are extensible, otherwise adding properties
  // to them will fail. Extensibility of the `WebAssembly` object should
  // already have been checked by the caller.
  DCHECK(webassembly->map()->is_extensible());
  if (!table_proto->map()->is_extensible() ||
      !global_proto->map()->is_extensible() ||
      !memory_proto->map()->is_extensible() ||
      !tag_proto->map()->is_extensible()) {
    return false;
  }

  // Checks are done, start installing the new fields.
  InstallFunc(isolate, table_proto, type_string, WebAssemblyTableType, 0, false,
              NONE, SideEffectType::kHasNoSideEffect);
  InstallFunc(isolate, memory_proto, type_string, WebAssemblyMemoryType, 0,
              false, NONE, SideEffectType::kHasNoSideEffect);
  InstallFunc(isolate, global_proto, type_string, WebAssemblyGlobalType, 0,
              false, NONE, SideEffectType::kHasNoSideEffect);
  InstallFunc(isolate, tag_proto, type_string, WebAssemblyTagType, 0, false,
              NONE, SideEffectType::kHasNoSideEffect);

  // Create the Function object.
  Handle<JSFunction> function_constructor = InstallConstructorFunc(
      isolate, webassembly, "Function", WebAssemblyFunction);
  SetDummyInstanceTemplate(isolate, function_constructor);
  JSFunction::EnsureHasInitialMap(function_constructor);
  Handle<JSObject> function_proto(
      Cast<JSObject>(function_constructor->instance_prototype()), isolate);
  Handle<Map> function_map =
      Map::Copy(isolate, isolate->sloppy_function_without_prototype_map(),
                "WebAssembly.Function");
  CHECK(JSObject::SetPrototype(
            isolate, function_proto,
            handle(context->function_function()->prototype(), isolate), false,
            kDontThrow)
            .FromJust());
  JSFunction::SetInitialMap(isolate, function_constructor, function_map,
                            function_proto);

  constexpr PropertyAttributes ro_attributes =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);
  JSObject::AddProperty(isolate, function_proto,
                        isolate->factory()->to_string_tag_symbol(),
                        v8_str(isolate, "WebAssembly.Function"), ro_attributes);

  InstallFunc(isolate, function_proto, type_string, WebAssemblyFunctionType, 0);
  SimpleInstallFunction(isolate, function_proto, "bind",
                        Builtin::kWebAssemblyFunctionPrototypeBind, 1, false);
  // Make all exported functions an instance of {WebAssembly.Function}.
  context->set_wasm_exported_function_map(*function_map);
  return true;
}

namespace wasm {
// static
std::unique_ptr<WasmStreaming> StartStreamingForTesting(
    Isolate* isolate,
    std::shared_ptr<wasm::CompilationResultResolver> resolver) {
  return std::make_unique<WasmStreaming>(
      std::make_unique<WasmStreaming::WasmStreamingImpl>(
          isolate, "StartStreamingForTesting", CompileTimeImports{}, resolver));
}
}  // namespace wasm

#undef ASSIGN
#undef EXTRACT_THIS

}  // namespace internal
}  // namespace v8
