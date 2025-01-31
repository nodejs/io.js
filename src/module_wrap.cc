#include "module_wrap.h"

#include "env.h"
#include "memory_tracker-inl.h"
#include "node_contextify.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_process-inl.h"
#include "node_watchdog.h"
#include "util-inl.h"

#include <sys/stat.h>  // S_IFDIR

#include <algorithm>

namespace node {
namespace loader {

using errors::TryCatchScope;

using node::contextify::ContextifyContext;
using v8::Array;
using v8::ArrayBufferView;
using v8::Context;
using v8::Data;
using v8::EscapableHandleScope;
using v8::Exception;
using v8::FixedArray;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::MemorySpan;
using v8::Message;
using v8::MicrotaskQueue;
using v8::Module;
using v8::ModuleRequest;
using v8::Name;
using v8::Null;
using v8::Object;
using v8::ObjectTemplate;
using v8::PrimitiveArray;
using v8::Promise;
using v8::PromiseRejectEvent;
using v8::ScriptCompiler;
using v8::ScriptOrigin;
using v8::String;
using v8::Symbol;
using v8::UnboundModuleScript;
using v8::Undefined;
using v8::Value;

ModuleWrap::ModuleWrap(Realm* realm,
                       Local<Object> object,
                       Local<Module> module,
                       Local<String> url,
                       Local<Object> context_object,
                       Local<Value> synthetic_evaluation_step)
    : BaseObject(realm, object),
      module_(realm->isolate(), module),
      module_hash_(module->GetIdentityHash()) {
  realm->env()->hash_to_module_map.emplace(module_hash_, this);

  object->SetInternalField(kModuleSlot, module);
  object->SetInternalField(kURLSlot, url);
  object->SetInternalField(kSyntheticEvaluationStepsSlot,
                           synthetic_evaluation_step);
  object->SetInternalField(kContextObjectSlot, context_object);

  if (!synthetic_evaluation_step->IsUndefined()) {
    synthetic_ = true;
  }
  MakeWeak();
  module_.SetWeak();
}

ModuleWrap::~ModuleWrap() {
  auto range = env()->hash_to_module_map.equal_range(module_hash_);
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second == this) {
      env()->hash_to_module_map.erase(it);
      break;
    }
  }
}

Local<Context> ModuleWrap::context() const {
  Local<Value> obj = object()->GetInternalField(kContextObjectSlot).As<Value>();
  // If this fails, there is likely a bug e.g. ModuleWrap::context() is accessed
  // before the ModuleWrap constructor completes.
  CHECK(obj->IsObject());
  return obj.As<Object>()->GetCreationContextChecked();
}

ModuleWrap* ModuleWrap::GetFromModule(Environment* env,
                                      Local<Module> module) {
  auto range = env->hash_to_module_map.equal_range(module->GetIdentityHash());
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second->module_ == module) {
      return it->second;
    }
  }
  return nullptr;
}

Maybe<bool> ModuleWrap::CheckUnsettledTopLevelAwait() {
  Isolate* isolate = env()->isolate();
  Local<Context> context = env()->context();

  // This must be invoked when the environment is shutting down, and the module
  // is kept alive by the module wrap via an internal field.
  CHECK(env()->exiting());
  CHECK(!module_.IsEmpty());

  Local<Module> module = module_.Get(isolate);
  // It's a synthetic module, likely a facade wrapping CJS.
  if (!module->IsSourceTextModule()) {
    return Just(true);
  }

  if (!module->IsGraphAsync()) {  // There is no TLA, no need to check.
    return Just(true);
  }

  auto stalled_messages =
      std::get<1>(module->GetStalledTopLevelAwaitMessages(isolate));
  if (stalled_messages.empty()) {
    return Just(true);
  }

  if (env()->options()->warnings) {
    for (auto& message : stalled_messages) {
      std::string reason = "Warning: Detected unsettled top-level await at ";
      std::string info =
          FormatErrorMessage(isolate, context, "", message, true);
      reason += info;
      FPrintF(stderr, "%s\n", reason);
    }
  }

  return Just(false);
}

Local<PrimitiveArray> ModuleWrap::GetHostDefinedOptions(
    Isolate* isolate, Local<Symbol> id_symbol) {
  Local<PrimitiveArray> host_defined_options =
      PrimitiveArray::New(isolate, HostDefinedOptions::kLength);
  host_defined_options->Set(isolate, HostDefinedOptions::kID, id_symbol);
  return host_defined_options;
}

// new ModuleWrap(url, context, source, lineOffset, columnOffset[, cachedData]);
// new ModuleWrap(url, context, source, lineOffset, columnOffset,
//                idSymbol);
// new ModuleWrap(url, context, exportNames, evaluationCallback[, cjsModule])
void ModuleWrap::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  CHECK_GE(args.Length(), 3);

  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = realm->isolate();

  Local<Object> that = args.This();

  CHECK(args[0]->IsString());
  Local<String> url = args[0].As<String>();

  Local<Context> context;
  ContextifyContext* contextify_context = nullptr;
  if (args[1]->IsUndefined()) {
    context = that->GetCreationContextChecked();
  } else {
    CHECK(args[1]->IsObject());
    contextify_context = ContextifyContext::ContextFromContextifiedSandbox(
        realm->env(), args[1].As<Object>());
    CHECK_NOT_NULL(contextify_context);
    context = contextify_context->context();
  }

  int line_offset = 0;
  int column_offset = 0;

  bool synthetic = args[2]->IsArray();
  bool can_use_builtin_cache = false;
  Local<PrimitiveArray> host_defined_options =
      PrimitiveArray::New(isolate, HostDefinedOptions::kLength);
  Local<Symbol> id_symbol;
  if (synthetic) {
    // new ModuleWrap(url, context, exportNames, evaluationCallback[,
    // cjsModule])
    CHECK(args[3]->IsFunction());
  } else {
    // new ModuleWrap(url, context, source, lineOffset, columnOffset[,
    //                cachedData]);
    // new ModuleWrap(url, context, source, lineOffset, columnOffset,
    //                idSymbol);
    CHECK(args[2]->IsString());
    CHECK(args[3]->IsNumber());
    line_offset = args[3].As<Int32>()->Value();
    CHECK(args[4]->IsNumber());
    column_offset = args[4].As<Int32>()->Value();
    if (args[5]->IsSymbol()) {
      id_symbol = args[5].As<Symbol>();
      can_use_builtin_cache =
          (id_symbol ==
           realm->isolate_data()->source_text_module_default_hdo());
    } else {
      id_symbol = Symbol::New(isolate, url);
    }
    host_defined_options = GetHostDefinedOptions(isolate, id_symbol);

    if (that->SetPrivate(context,
                         realm->isolate_data()->host_defined_option_symbol(),
                         id_symbol)
            .IsNothing()) {
      return;
    }
  }

  ShouldNotAbortOnUncaughtScope no_abort_scope(realm->env());
  TryCatchScope try_catch(realm->env());

  Local<Module> module;

  {
    Context::Scope context_scope(context);
    if (synthetic) {
      CHECK(args[2]->IsArray());
      Local<Array> export_names_arr = args[2].As<Array>();

      uint32_t len = export_names_arr->Length();
      LocalVector<String> export_names(realm->isolate(), len);
      for (uint32_t i = 0; i < len; i++) {
        Local<Value> export_name_val =
            export_names_arr->Get(context, i).ToLocalChecked();
        CHECK(export_name_val->IsString());
        export_names[i] = export_name_val.As<String>();
      }

      const MemorySpan<const Local<String>> span(export_names.begin(),
                                                 export_names.size());
      module = Module::CreateSyntheticModule(
          isolate, url, span, SyntheticModuleEvaluationStepsCallback);
    } else {
      // When we are compiling for the default loader, this will be
      // std::nullopt, and CompileSourceTextModule() should use
      // on-disk cache.
      std::optional<ScriptCompiler::CachedData*> user_cached_data;
      if (id_symbol !=
          realm->isolate_data()->source_text_module_default_hdo()) {
        user_cached_data = nullptr;
      }
      if (args[5]->IsArrayBufferView()) {
        CHECK(!can_use_builtin_cache);  // We don't use this option internally.
        Local<ArrayBufferView> cached_data_buf = args[5].As<ArrayBufferView>();
        uint8_t* data =
            static_cast<uint8_t*>(cached_data_buf->Buffer()->Data());
        user_cached_data =
            new ScriptCompiler::CachedData(data + cached_data_buf->ByteOffset(),
                                           cached_data_buf->ByteLength());
      }
      Local<String> source_text = args[2].As<String>();

      bool cache_rejected = false;
      if (!CompileSourceTextModule(realm,
                                   source_text,
                                   url,
                                   line_offset,
                                   column_offset,
                                   host_defined_options,
                                   user_cached_data,
                                   &cache_rejected)
               .ToLocal(&module)) {
        if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
          CHECK(!try_catch.Message().IsEmpty());
          CHECK(!try_catch.Exception().IsEmpty());
          AppendExceptionLine(realm->env(),
                              try_catch.Exception(),
                              try_catch.Message(),
                              ErrorHandlingMode::MODULE_ERROR);
          try_catch.ReThrow();
        }
        return;
      }

      if (user_cached_data.has_value() && user_cached_data.value() != nullptr &&
          cache_rejected) {
        THROW_ERR_VM_MODULE_CACHED_DATA_REJECTED(
            realm, "cachedData buffer was rejected");
        try_catch.ReThrow();
        return;
      }

      if (that->Set(context,
                    realm->env()->source_map_url_string(),
                    module->GetUnboundModuleScript()->GetSourceMappingURL())
              .IsNothing()) {
        return;
      }
    }
  }

  if (!that->Set(context, realm->isolate_data()->url_string(), url)
           .FromMaybe(false)) {
    return;
  }

  if (synthetic && args[4]->IsObject() &&
      that->Set(context, realm->isolate_data()->imported_cjs_symbol(), args[4])
          .IsNothing()) {
    return;
  }

  // Initialize an empty slot for source map cache before the object is frozen.
  if (that->SetPrivate(context,
                       realm->isolate_data()->source_map_data_private_symbol(),
                       Undefined(isolate))
          .IsNothing()) {
    return;
  }

  // Use the extras object as an object whose GetCreationContext() will be the
  // original `context`, since the `Context` itself strictly speaking cannot
  // be stored in an internal field.
  Local<Object> context_object = context->GetExtrasBindingObject();
  Local<Value> synthetic_evaluation_step =
      synthetic ? args[3] : Undefined(realm->isolate()).As<Value>();

  ModuleWrap* obj = new ModuleWrap(
      realm, that, module, url, context_object, synthetic_evaluation_step);

  obj->contextify_context_ = contextify_context;

  args.GetReturnValue().Set(that);
}

MaybeLocal<Module> ModuleWrap::CompileSourceTextModule(
    Realm* realm,
    Local<String> source_text,
    Local<String> url,
    int line_offset,
    int column_offset,
    Local<PrimitiveArray> host_defined_options,
    std::optional<ScriptCompiler::CachedData*> user_cached_data,
    bool* cache_rejected) {
  Isolate* isolate = realm->isolate();
  EscapableHandleScope scope(isolate);
  ScriptOrigin origin(url,
                      line_offset,
                      column_offset,
                      true,            // is cross origin
                      -1,              // script id
                      Local<Value>(),  // source map URL
                      false,           // is opaque (?)
                      false,           // is WASM
                      true,            // is ES Module
                      host_defined_options);
  ScriptCompiler::CachedData* cached_data = nullptr;
  CompileCacheEntry* cache_entry = nullptr;
  // When compiling for the default loader, user_cached_data is std::nullptr.
  // When compiling for vm.Module, it's either nullptr or a pointer to the
  // cached data.
  if (user_cached_data.has_value()) {
    cached_data = user_cached_data.value();
  } else if (realm->env()->use_compile_cache()) {
    cache_entry = realm->env()->compile_cache_handler()->GetOrInsert(
        source_text, url, CachedCodeType::kESM);
  }

  if (cache_entry != nullptr && cache_entry->cache != nullptr) {
    // source will take ownership of cached_data.
    cached_data = cache_entry->CopyCache();
  }

  ScriptCompiler::Source source(source_text, origin, cached_data);
  ScriptCompiler::CompileOptions options;
  if (cached_data == nullptr) {
    options = ScriptCompiler::kNoCompileOptions;
  } else {
    options = ScriptCompiler::kConsumeCodeCache;
  }

  Local<Module> module;
  if (!ScriptCompiler::CompileModule(isolate, &source, options)
           .ToLocal(&module)) {
    return scope.EscapeMaybe(MaybeLocal<Module>());
  }

  if (options == ScriptCompiler::kConsumeCodeCache) {
    *cache_rejected = source.GetCachedData()->rejected;
  }

  if (cache_entry != nullptr) {
    realm->env()->compile_cache_handler()->MaybeSave(
        cache_entry, module, *cache_rejected);
  }

  return scope.Escape(module);
}

static Local<Object> createImportAttributesContainer(
    Realm* realm,
    Isolate* isolate,
    Local<FixedArray> raw_attributes,
    const int elements_per_attribute) {
  CHECK_EQ(raw_attributes->Length() % elements_per_attribute, 0);
  size_t num_attributes = raw_attributes->Length() / elements_per_attribute;
  LocalVector<Name> names(isolate, num_attributes);
  LocalVector<Value> values(isolate, num_attributes);

  for (int i = 0; i < raw_attributes->Length(); i += elements_per_attribute) {
    int idx = i / elements_per_attribute;
    names[idx] = raw_attributes->Get(realm->context(), i).As<Name>();
    values[idx] = raw_attributes->Get(realm->context(), i + 1).As<Value>();
  }

  return Object::New(
      isolate, Null(isolate), names.data(), values.data(), num_attributes);
}

static Local<Array> createModuleRequestsContainer(
    Realm* realm, Isolate* isolate, Local<FixedArray> raw_requests) {
  LocalVector<Value> requests(isolate, raw_requests->Length());

  for (int i = 0; i < raw_requests->Length(); i++) {
    Local<ModuleRequest> module_request =
        raw_requests->Get(realm->context(), i).As<ModuleRequest>();

    Local<String> specifier = module_request->GetSpecifier();

    // Contains the import attributes for this request in the form:
    // [key1, value1, source_offset1, key2, value2, source_offset2, ...].
    Local<FixedArray> raw_attributes = module_request->GetImportAttributes();
    Local<Object> attributes =
        createImportAttributesContainer(realm, isolate, raw_attributes, 3);

    Local<Name> names[] = {
        realm->isolate_data()->specifier_string(),
        realm->isolate_data()->attributes_string(),
    };
    Local<Value> values[] = {
        specifier,
        attributes,
    };
    DCHECK_EQ(arraysize(names), arraysize(values));

    Local<Object> request =
        Object::New(isolate, Null(isolate), names, values, arraysize(names));

    requests[i] = request;
  }

  return Array::New(isolate, requests.data(), requests.size());
}

void ModuleWrap::GetModuleRequests(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  Local<Object> that = args.This();

  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, that);

  Local<Module> module = obj->module_.Get(isolate);
  args.GetReturnValue().Set(createModuleRequestsContainer(
      realm, isolate, module->GetModuleRequests()));
}

// moduleWrap.link(specifiers, moduleWraps)
void ModuleWrap::Link(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = realm->context();

  ModuleWrap* dependent;
  ASSIGN_OR_RETURN_UNWRAP(&dependent, args.This());

  CHECK_EQ(args.Length(), 2);

  Local<Array> specifiers = args[0].As<Array>();
  Local<Array> modules = args[1].As<Array>();
  CHECK_EQ(specifiers->Length(), modules->Length());

  std::vector<Global<Value>> specifiers_buffer;
  if (FromV8Array(context, specifiers, &specifiers_buffer).IsNothing()) {
    return;
  }
  std::vector<Global<Value>> modules_buffer;
  if (FromV8Array(context, modules, &modules_buffer).IsNothing()) {
    return;
  }

  for (uint32_t i = 0; i < specifiers->Length(); i++) {
    Local<String> specifier_str =
        specifiers_buffer[i].Get(isolate).As<String>();
    Local<Object> module_object = modules_buffer[i].Get(isolate).As<Object>();

    CHECK(
        realm->isolate_data()->module_wrap_constructor_template()->HasInstance(
            module_object));

    Utf8Value specifier(isolate, specifier_str);
    dependent->resolve_cache_[specifier.ToString()].Reset(isolate,
                                                          module_object);
  }
}

void ModuleWrap::Instantiate(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  Local<Context> context = obj->context();
  Local<Module> module = obj->module_.Get(isolate);
  TryCatchScope try_catch(realm->env());
  USE(module->InstantiateModule(context, ResolveModuleCallback));

  // clear resolve cache on instantiate
  obj->resolve_cache_.clear();

  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    CHECK(!try_catch.Message().IsEmpty());
    CHECK(!try_catch.Exception().IsEmpty());
    AppendExceptionLine(realm->env(),
                        try_catch.Exception(),
                        try_catch.Message(),
                        ErrorHandlingMode::MODULE_ERROR);
    try_catch.ReThrow();
    return;
  }
}

void ModuleWrap::Evaluate(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = realm->isolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  Local<Context> context = obj->context();
  Local<Module> module = obj->module_.Get(isolate);

  ContextifyContext* contextify_context = obj->contextify_context_;
  MicrotaskQueue* microtask_queue = nullptr;
  if (contextify_context != nullptr)
      microtask_queue = contextify_context->microtask_queue();

  // module.evaluate(timeout, breakOnSigint)
  CHECK_EQ(args.Length(), 2);

  CHECK(args[0]->IsNumber());
  int64_t timeout = args[0]->IntegerValue(realm->context()).FromJust();

  CHECK(args[1]->IsBoolean());
  bool break_on_sigint = args[1]->IsTrue();

  ShouldNotAbortOnUncaughtScope no_abort_scope(realm->env());
  TryCatchScope try_catch(realm->env());

  bool timed_out = false;
  bool received_signal = false;
  MaybeLocal<Value> result;
  auto run = [&]() {
    MaybeLocal<Value> result = module->Evaluate(context);
    if (!result.IsEmpty() && microtask_queue)
      microtask_queue->PerformCheckpoint(isolate);
    return result;
  };
  if (break_on_sigint && timeout != -1) {
    Watchdog wd(isolate, timeout, &timed_out);
    SigintWatchdog swd(isolate, &received_signal);
    result = run();
  } else if (break_on_sigint) {
    SigintWatchdog swd(isolate, &received_signal);
    result = run();
  } else if (timeout != -1) {
    Watchdog wd(isolate, timeout, &timed_out);
    result = run();
  } else {
    result = run();
  }

  if (result.IsEmpty()) {
    CHECK(try_catch.HasCaught());
  }

  // Convert the termination exception into a regular exception.
  if (timed_out || received_signal) {
    if (!realm->env()->is_main_thread() && realm->env()->is_stopping()) return;
    isolate->CancelTerminateExecution();
    // It is possible that execution was terminated by another timeout in
    // which this timeout is nested, so check whether one of the watchdogs
    // from this invocation is responsible for termination.
    if (timed_out) {
      THROW_ERR_SCRIPT_EXECUTION_TIMEOUT(realm->env(), timeout);
    } else if (received_signal) {
      THROW_ERR_SCRIPT_EXECUTION_INTERRUPTED(realm->env());
    }
  }

  if (try_catch.HasCaught()) {
    if (!try_catch.HasTerminated())
      try_catch.ReThrow();
    return;
  }

  args.GetReturnValue().Set(result.ToLocalChecked());
}

void ModuleWrap::InstantiateSync(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  Local<Context> context = obj->context();
  Local<Module> module = obj->module_.Get(isolate);
  Environment* env = realm->env();

  {
    TryCatchScope try_catch(env);
    USE(module->InstantiateModule(context, ResolveModuleCallback));

    // clear resolve cache on instantiate
    obj->resolve_cache_.clear();

    if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
      CHECK(!try_catch.Message().IsEmpty());
      CHECK(!try_catch.Exception().IsEmpty());
      AppendExceptionLine(env,
                          try_catch.Exception(),
                          try_catch.Message(),
                          ErrorHandlingMode::MODULE_ERROR);
      try_catch.ReThrow();
      return;
    }
  }

  // TODO(joyeecheung): record Module::HasTopLevelAwait() in every ModuleWrap
  // and infer the asynchronicity from a module's children during linking.
  args.GetReturnValue().Set(module->IsGraphAsync());
}

void ModuleWrap::EvaluateSync(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  Local<Context> context = obj->context();
  Local<Module> module = obj->module_.Get(isolate);
  Environment* env = realm->env();

  Local<Value> result;
  {
    TryCatchScope try_catch(env);
    if (!module->Evaluate(context).ToLocal(&result)) {
      if (try_catch.HasCaught()) {
        if (!try_catch.HasTerminated()) {
          try_catch.ReThrow();
        }
        return;
      }
    }
  }

  CHECK(result->IsPromise());
  Local<Promise> promise = result.As<Promise>();
  if (promise->State() == Promise::PromiseState::kRejected) {
    // The rejected promise is created by V8, so we don't get a chance to mark
    // it as resolved before the rejection happens from evaluation. But we can
    // tell the promise rejection callback to treat it as a promise rejected
    // before handler was added which would remove it from the unhandled
    // rejection handling, since we are converting it into an error and throw
    // from here directly.
    Local<Value> type =
        Integer::New(isolate,
                     static_cast<int32_t>(
                         PromiseRejectEvent::kPromiseHandlerAddedAfterReject));
    Local<Value> args[] = {type, promise, Undefined(isolate)};
    if (env->promise_reject_callback()
            ->Call(context, Undefined(isolate), arraysize(args), args)
            .IsEmpty()) {
      return;
    }
    Local<Value> exception = promise->Result();
    Local<Message> message = Exception::CreateMessage(isolate, exception);
    AppendExceptionLine(
        env, exception, message, ErrorHandlingMode::MODULE_ERROR);
    isolate->ThrowException(exception);
    return;
  }

  if (module->IsGraphAsync()) {
    CHECK(env->options()->print_required_tla);
    auto stalled_messages =
        std::get<1>(module->GetStalledTopLevelAwaitMessages(isolate));
    if (stalled_messages.size() != 0) {
      for (auto& message : stalled_messages) {
        std::string reason = "Error: unexpected top-level await at ";
        std::string info =
            FormatErrorMessage(isolate, context, "", message, true);
        reason += info;
        FPrintF(stderr, "%s\n", reason);
      }
    }
    THROW_ERR_REQUIRE_ASYNC_MODULE(env);
    return;
  }

  CHECK_EQ(promise->State(), Promise::PromiseState::kFulfilled);

  args.GetReturnValue().Set(module->GetModuleNamespace());
}

void ModuleWrap::GetNamespaceSync(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  Local<Module> module = obj->module_.Get(isolate);

  switch (module->GetStatus()) {
    case Module::Status::kUninstantiated:
    case Module::Status::kInstantiating:
      return realm->env()->ThrowError(
          "Cannot get namespace, module has not been instantiated");
    case Module::Status::kInstantiated:
    case Module::Status::kEvaluated:
    case Module::Status::kErrored:
      break;
    case Module::Status::kEvaluating:
      UNREACHABLE();
  }

  if (module->IsGraphAsync()) {
    return THROW_ERR_REQUIRE_ASYNC_MODULE(realm->env());
  }
  Local<Value> result = module->GetModuleNamespace();
  args.GetReturnValue().Set(result);
}

void ModuleWrap::GetNamespace(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());

  Local<Module> module = obj->module_.Get(isolate);

  switch (module->GetStatus()) {
    case Module::Status::kUninstantiated:
    case Module::Status::kInstantiating:
      return realm->env()->ThrowError(
          "cannot get namespace, module has not been instantiated");
    case Module::Status::kInstantiated:
    case Module::Status::kEvaluating:
    case Module::Status::kEvaluated:
    case Module::Status::kErrored:
      break;
    default:
      UNREACHABLE();
  }

  Local<Value> result = module->GetModuleNamespace();
  args.GetReturnValue().Set(result);
}

void ModuleWrap::GetStatus(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());

  Local<Module> module = obj->module_.Get(isolate);

  args.GetReturnValue().Set(module->GetStatus());
}

void ModuleWrap::GetError(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());

  Local<Module> module = obj->module_.Get(isolate);
  args.GetReturnValue().Set(module->GetException());
}

MaybeLocal<Module> ModuleWrap::ResolveModuleCallback(
    Local<Context> context,
    Local<String> specifier,
    Local<FixedArray> import_attributes,
    Local<Module> referrer) {
  Isolate* isolate = context->GetIsolate();
  Environment* env = Environment::GetCurrent(context);
  if (env == nullptr) {
    THROW_ERR_EXECUTION_ENVIRONMENT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Module>();
  }

  Utf8Value specifier_utf8(isolate, specifier);
  std::string specifier_std(*specifier_utf8, specifier_utf8.length());

  ModuleWrap* dependent = GetFromModule(env, referrer);
  if (dependent == nullptr) {
    THROW_ERR_VM_MODULE_LINK_FAILURE(
        env, "request for '%s' is from invalid module", specifier_std);
    return MaybeLocal<Module>();
  }

  if (dependent->resolve_cache_.count(specifier_std) != 1) {
    THROW_ERR_VM_MODULE_LINK_FAILURE(
        env, "request for '%s' is not in cache", specifier_std);
    return MaybeLocal<Module>();
  }

  Local<Object> module_object =
      dependent->resolve_cache_[specifier_std].Get(isolate);
  if (module_object.IsEmpty() || !module_object->IsObject()) {
    THROW_ERR_VM_MODULE_LINK_FAILURE(
        env, "request for '%s' did not return an object", specifier_std);
    return MaybeLocal<Module>();
  }

  ModuleWrap* module;
  ASSIGN_OR_RETURN_UNWRAP(&module, module_object, MaybeLocal<Module>());
  return module->module_.Get(isolate);
}

static MaybeLocal<Promise> ImportModuleDynamically(
    Local<Context> context,
    Local<Data> host_defined_options,
    Local<Value> resource_name,
    Local<String> specifier,
    Local<FixedArray> import_attributes) {
  Isolate* isolate = context->GetIsolate();
  Environment* env = Environment::GetCurrent(context);
  if (env == nullptr) {
    THROW_ERR_EXECUTION_ENVIRONMENT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Promise>();
  }
  Realm* realm = Realm::GetCurrent(context);
  if (realm == nullptr) {
    // Fallback to the principal realm if it's in a vm context.
    realm = env->principal_realm();
  }

  EscapableHandleScope handle_scope(isolate);

  Local<Function> import_callback =
      realm->host_import_module_dynamically_callback();
  Local<Value> id;

  Local<FixedArray> options = host_defined_options.As<FixedArray>();
  // Get referrer id symbol from the host-defined options.
  // If the host-defined options are empty, get the referrer id symbol
  // from the realm global object.
  if (options->Length() == HostDefinedOptions::kLength) {
    id = options->Get(context, HostDefinedOptions::kID).As<Symbol>();
  } else {
    id = context->Global()
             ->GetPrivate(context, env->host_defined_option_symbol())
             .ToLocalChecked();
  }

  Local<Object> attributes =
      createImportAttributesContainer(realm, isolate, import_attributes, 2);

  Local<Value> import_args[] = {
      id,
      Local<Value>(specifier),
      attributes,
      resource_name,
  };

  Local<Value> result;
  if (import_callback->Call(
        context,
        Undefined(isolate),
        arraysize(import_args),
        import_args).ToLocal(&result)) {
    CHECK(result->IsPromise());
    return handle_scope.Escape(result.As<Promise>());
  }

  return MaybeLocal<Promise>();
}

void ModuleWrap::SetImportModuleDynamicallyCallback(
    const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Realm* realm = Realm::GetCurrent(args);
  HandleScope handle_scope(isolate);

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsFunction());
  Local<Function> import_callback = args[0].As<Function>();
  realm->set_host_import_module_dynamically_callback(import_callback);

  isolate->SetHostImportModuleDynamicallyCallback(ImportModuleDynamically);
}

void ModuleWrap::HostInitializeImportMetaObjectCallback(
    Local<Context> context, Local<Module> module, Local<Object> meta) {
  Environment* env = Environment::GetCurrent(context);
  if (env == nullptr)
    return;
  ModuleWrap* module_wrap = GetFromModule(env, module);

  if (module_wrap == nullptr) {
    return;
  }
  Realm* realm = Realm::GetCurrent(context);
  if (realm == nullptr) {
    // Fallback to the principal realm if it's in a vm context.
    realm = env->principal_realm();
  }

  Local<Object> wrap = module_wrap->object();
  Local<Function> callback =
      realm->host_initialize_import_meta_object_callback();
  Local<Value> id;
  if (!wrap->GetPrivate(context, env->host_defined_option_symbol())
           .ToLocal(&id)) {
    return;
  }
  DCHECK(id->IsSymbol());
  Local<Value> args[] = {id, meta, wrap};
  TryCatchScope try_catch(env);
  USE(callback->Call(
      context, Undefined(realm->isolate()), arraysize(args), args));
  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    try_catch.ReThrow();
  }
}

void ModuleWrap::SetInitializeImportMetaObjectCallback(
    const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = realm->isolate();

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsFunction());
  Local<Function> import_meta_callback = args[0].As<Function>();
  realm->set_host_initialize_import_meta_object_callback(import_meta_callback);

  isolate->SetHostInitializeImportMetaObjectCallback(
      HostInitializeImportMetaObjectCallback);
}

MaybeLocal<Value> ModuleWrap::SyntheticModuleEvaluationStepsCallback(
    Local<Context> context, Local<Module> module) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  ModuleWrap* obj = GetFromModule(env, module);

  TryCatchScope try_catch(env);
  Local<Function> synthetic_evaluation_steps =
      obj->object()
          ->GetInternalField(kSyntheticEvaluationStepsSlot)
          .As<Value>()
          .As<Function>();
  obj->object()->SetInternalField(
      kSyntheticEvaluationStepsSlot, Undefined(isolate));
  MaybeLocal<Value> ret = synthetic_evaluation_steps->Call(context,
      obj->object(), 0, nullptr);
  if (ret.IsEmpty()) {
    CHECK(try_catch.HasCaught());
  }
  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    CHECK(!try_catch.Message().IsEmpty());
    CHECK(!try_catch.Exception().IsEmpty());
    try_catch.ReThrow();
    return MaybeLocal<Value>();
  }

  Local<Promise::Resolver> resolver;
  if (!Promise::Resolver::New(context).ToLocal(&resolver)) {
    return MaybeLocal<Value>();
  }

  resolver->Resolve(context, Undefined(isolate)).ToChecked();
  return resolver->GetPromise();
}

void ModuleWrap::SetSyntheticExport(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> that = args.This();

  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, that);

  CHECK(obj->synthetic_);

  CHECK_EQ(args.Length(), 2);

  CHECK(args[0]->IsString());
  Local<String> export_name = args[0].As<String>();

  Local<Value> export_value = args[1];

  Local<Module> module = obj->module_.Get(isolate);
  USE(module->SetSyntheticModuleExport(isolate, export_name, export_value));
}

void ModuleWrap::CreateCachedData(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> that = args.This();

  ModuleWrap* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, that);

  CHECK(!obj->synthetic_);

  Local<Module> module = obj->module_.Get(isolate);

  CHECK_LT(module->GetStatus(), Module::Status::kEvaluating);

  Local<UnboundModuleScript> unbound_module_script =
      module->GetUnboundModuleScript();
  std::unique_ptr<ScriptCompiler::CachedData> cached_data(
      ScriptCompiler::CreateCodeCache(unbound_module_script));
  Environment* env = Environment::GetCurrent(args);
  if (!cached_data) {
    args.GetReturnValue().Set(Buffer::New(env, 0).ToLocalChecked());
  } else {
    MaybeLocal<Object> buf =
        Buffer::Copy(env,
                     reinterpret_cast<const char*>(cached_data->data),
                     cached_data->length);
    args.GetReturnValue().Set(buf.ToLocalChecked());
  }
}

// This v8::Module::ResolveModuleCallback simply links `import 'original'`
// to the env->temporary_required_module_facade_original() which is stashed
// right before this callback is called and will be restored as soon as
// v8::Module::Instantiate() returns.
MaybeLocal<Module> LinkRequireFacadeWithOriginal(
    Local<Context> context,
    Local<String> specifier,
    Local<FixedArray> import_attributes,
    Local<Module> referrer) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = context->GetIsolate();
  CHECK(specifier->Equals(context, env->original_string()).ToChecked());
  CHECK(!env->temporary_required_module_facade_original.IsEmpty());
  return env->temporary_required_module_facade_original.Get(isolate);
}

// Wraps an existing source text module with a facade that adds
// .__esModule = true to the exports.
// See env->required_module_facade_source_string() for the source.
void ModuleWrap::CreateRequiredModuleFacade(
    const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  CHECK(args[0]->IsObject());  // original module
  Local<Object> wrap = args[0].As<Object>();
  ModuleWrap* original;
  ASSIGN_OR_RETURN_UNWRAP(&original, wrap);

  // Use the same facade source and URL to hit the compilation cache.
  ScriptOrigin origin(env->required_module_facade_url_string(),
                      0,               // line offset
                      0,               // column offset
                      true,            // is cross origin
                      -1,              // script id
                      Local<Value>(),  // source map URL
                      false,           // is opaque (?)
                      false,           // is WASM
                      true);           // is ES Module
  ScriptCompiler::Source source(env->required_module_facade_source_string(),
                                origin);

  // The module facade instantiation simply links `import 'original'` in the
  // facade with the original module and should never fail.
  Local<Module> facade =
      ScriptCompiler::CompileModule(isolate, &source).ToLocalChecked();
  // Stash the original module in temporary_required_module_facade_original
  // for the LinkRequireFacadeWithOriginal() callback to pick it up.
  CHECK(env->temporary_required_module_facade_original.IsEmpty());
  env->temporary_required_module_facade_original.Reset(
      isolate, original->module_.Get(isolate));
  CHECK(facade->InstantiateModule(context, LinkRequireFacadeWithOriginal)
            .IsJust());
  env->temporary_required_module_facade_original.Reset();

  // The evaluation of the facade is synchronous.
  Local<Value> evaluated = facade->Evaluate(context).ToLocalChecked();
  CHECK(evaluated->IsPromise());
  CHECK_EQ(evaluated.As<Promise>()->State(), Promise::PromiseState::kFulfilled);

  args.GetReturnValue().Set(facade->GetModuleNamespace());
}

void ModuleWrap::CreatePerIsolateProperties(IsolateData* isolate_data,
                                            Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  Local<FunctionTemplate> tpl = NewFunctionTemplate(isolate, New);
  tpl->InstanceTemplate()->SetInternalFieldCount(
      ModuleWrap::kInternalFieldCount);

  SetProtoMethod(isolate, tpl, "link", Link);
  SetProtoMethod(isolate, tpl, "getModuleRequests", GetModuleRequests);
  SetProtoMethod(isolate, tpl, "instantiateSync", InstantiateSync);
  SetProtoMethod(isolate, tpl, "evaluateSync", EvaluateSync);
  SetProtoMethod(isolate, tpl, "getNamespaceSync", GetNamespaceSync);
  SetProtoMethod(isolate, tpl, "instantiate", Instantiate);
  SetProtoMethod(isolate, tpl, "evaluate", Evaluate);
  SetProtoMethod(isolate, tpl, "setExport", SetSyntheticExport);
  SetProtoMethodNoSideEffect(
      isolate, tpl, "createCachedData", CreateCachedData);
  SetProtoMethodNoSideEffect(isolate, tpl, "getNamespace", GetNamespace);
  SetProtoMethodNoSideEffect(isolate, tpl, "getStatus", GetStatus);
  SetProtoMethodNoSideEffect(isolate, tpl, "getError", GetError);
  SetConstructorFunction(isolate, target, "ModuleWrap", tpl);
  isolate_data->set_module_wrap_constructor_template(tpl);

  SetMethod(isolate,
            target,
            "setImportModuleDynamicallyCallback",
            SetImportModuleDynamicallyCallback);
  SetMethod(isolate,
            target,
            "setInitializeImportMetaObjectCallback",
            SetInitializeImportMetaObjectCallback);
  SetMethod(isolate,
            target,
            "createRequiredModuleFacade",
            CreateRequiredModuleFacade);
}

void ModuleWrap::CreatePerContextProperties(Local<Object> target,
                                            Local<Value> unused,
                                            Local<Context> context,
                                            void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  Isolate* isolate = realm->isolate();
#define V(name)                                                                \
  target                                                                       \
      ->Set(context,                                                           \
            FIXED_ONE_BYTE_STRING(isolate, #name),                             \
            Integer::New(isolate, Module::Status::name))                       \
      .FromJust()
  V(kUninstantiated);
  V(kInstantiating);
  V(kInstantiated);
  V(kEvaluating);
  V(kEvaluated);
  V(kErrored);
#undef V
}

void ModuleWrap::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);

  registry->Register(Link);
  registry->Register(GetModuleRequests);
  registry->Register(InstantiateSync);
  registry->Register(EvaluateSync);
  registry->Register(GetNamespaceSync);
  registry->Register(Instantiate);
  registry->Register(Evaluate);
  registry->Register(SetSyntheticExport);
  registry->Register(CreateCachedData);
  registry->Register(GetNamespace);
  registry->Register(GetStatus);
  registry->Register(GetError);

  registry->Register(CreateRequiredModuleFacade);

  registry->Register(SetImportModuleDynamicallyCallback);
  registry->Register(SetInitializeImportMetaObjectCallback);
}
}  // namespace loader
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    module_wrap, node::loader::ModuleWrap::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(
    module_wrap, node::loader::ModuleWrap::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(
    module_wrap, node::loader::ModuleWrap::RegisterExternalReferences)
