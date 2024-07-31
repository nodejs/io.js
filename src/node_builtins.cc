#include "node_builtins.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_threadsafe_cow-inl.h"
#include "simdutf.h"
#include "util-inl.h"

namespace node {
namespace builtins {

using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::IntegrityLevel;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Name;
using v8::None;
using v8::Object;
using v8::ObjectTemplate;
using v8::PropertyCallbackInfo;
using v8::ScriptCompiler;
using v8::ScriptOrigin;
using v8::Set;
using v8::SideEffectType;
using v8::String;
using v8::Undefined;
using v8::Value;

BuiltinLoader::BuiltinLoader()
    : config_(GetConfig()), code_cache_(std::make_shared<BuiltinCodeCache>()) {
  LoadJavaScriptSource();
#ifdef NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_LEXER_PATH
  AddExternalizedBuiltin(
      "internal/deps/cjs-module-lexer/lexer",
      STRINGIFY(NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_LEXER_PATH));
#endif  // NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_LEXER_PATH

#ifdef NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_DIST_LEXER_PATH
  AddExternalizedBuiltin(
      "internal/deps/cjs-module-lexer/dist/lexer",
      STRINGIFY(NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_DIST_LEXER_PATH));
#endif  // NODE_SHARED_BUILTIN_CJS_MODULE_LEXER_DIST_LEXER_PATH

#ifdef NODE_SHARED_BUILTIN_UNDICI_UNDICI_PATH
  AddExternalizedBuiltin("internal/deps/undici/undici",
                         STRINGIFY(NODE_SHARED_BUILTIN_UNDICI_UNDICI_PATH));
#endif  // NODE_SHARED_BUILTIN_UNDICI_UNDICI_PATH
}

bool BuiltinLoader::Exists(const char* id) {
  auto source = source_.read();
  return source->find(id) != source->end();
}

bool BuiltinLoader::Add(const char* id, const UnionBytes& source) {
  auto result = source_.write()->emplace(id, source);
  return result.second;
}

void BuiltinLoader::GetNatives(Local<Name> property,
                               const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();

  Local<Object> out = Object::New(isolate);
  auto source = env->builtin_loader()->source_.read();
  for (auto const& x : *source) {
    Local<String> key = OneByteString(isolate, x.first.c_str(), x.first.size());
    out->Set(context, key, x.second.ToStringChecked(isolate)).FromJust();
  }
  info.GetReturnValue().Set(out);
}

Local<String> BuiltinLoader::GetConfigString(Isolate* isolate) {
  return config_.ToStringChecked(isolate);
}

std::vector<std::string_view> BuiltinLoader::GetBuiltinIds() const {
  std::vector<std::string_view> ids;
  auto source = source_.read();
  ids.reserve(source->size());
  for (auto const& x : *source) {
    ids.emplace_back(x.first);
  }
  return ids;
}

BuiltinLoader::BuiltinCategories BuiltinLoader::GetBuiltinCategories() const {
  BuiltinCategories builtin_categories;

  const std::vector<std::string_view> prefixes = {
#if !HAVE_OPENSSL
    "internal/crypto/",
    "internal/debugger/",
#endif  // !HAVE_OPENSSL

    "internal/bootstrap/",
    "internal/per_context/",
    "internal/deps/",
    "internal/main/"
  };

  builtin_categories.can_be_required.emplace(
      "internal/deps/cjs-module-lexer/lexer");

  builtin_categories.cannot_be_required = std::set<std::string> {
#if !HAVE_INSPECTOR
    "inspector", "inspector/promises", "internal/util/inspector",
#endif  // !HAVE_INSPECTOR

#if !NODE_USE_V8_PLATFORM || !defined(NODE_HAVE_I18N_SUPPORT)
        "trace_events",
#endif  // !NODE_USE_V8_PLATFORM || !defined(NODE_HAVE_I18N_SUPPORT)

#if !HAVE_OPENSSL
        "crypto", "crypto/promises", "https", "http2", "tls", "_tls_common",
        "_tls_wrap", "internal/tls/secure-pair",
        "internal/tls/parse-cert-string", "internal/tls/secure-context",
        "internal/http2/core", "internal/http2/compat",
        "internal/streams/lazy_transform",
#endif           // !HAVE_OPENSSL
        "sqlite",  // Experimental.
        "sys",     // Deprecated.
        "wasi",    // Experimental.
        "internal/test/binding", "internal/v8_prof_polyfill",
        "internal/v8_prof_processor",
  };

  auto source = source_.read();
  for (auto const& x : *source) {
    const std::string& id = x.first;
    for (auto const& prefix : prefixes) {
      if (prefix.length() > id.length()) {
        continue;
      }
      if (id.find(prefix) == 0 &&
          builtin_categories.can_be_required.count(id) == 0) {
        builtin_categories.cannot_be_required.emplace(id);
      }
    }
  }

  for (auto const& x : *source) {
    const std::string& id = x.first;
    if (0 == builtin_categories.cannot_be_required.count(id)) {
      builtin_categories.can_be_required.emplace(id);
    }
  }

  return builtin_categories;
}

#ifdef NODE_BUILTIN_MODULES_PATH
static std::string OnDiskFileName(const char* id) {
  std::string filename = NODE_BUILTIN_MODULES_PATH;
  filename += "/";

  if (strncmp(id, "internal/deps", strlen("internal/deps")) == 0) {
    id += strlen("internal/");
  } else {
    filename += "lib/";
  }
  filename += id;
  filename += ".js";

  return filename;
}
#endif  // NODE_BUILTIN_MODULES_PATH

MaybeLocal<String> BuiltinLoader::LoadBuiltinSource(Isolate* isolate,
                                                    const char* id) const {
  auto source = source_.read();
#ifndef NODE_BUILTIN_MODULES_PATH
  const auto source_it = source->find(id);
  if (UNLIKELY(source_it == source->end())) {
    fprintf(stderr, "Cannot find native builtin: \"%s\".\n", id);
    ABORT();
  }
  return source_it->second.ToStringChecked(isolate);
#else   // !NODE_BUILTIN_MODULES_PATH
  std::string filename = OnDiskFileName(id);

  std::string contents;
  int r = ReadFileSync(&contents, filename.c_str());
  if (r != 0) {
    const std::string buf = SPrintF("Cannot read local builtin. %s: %s \"%s\"",
                                    uv_err_name(r),
                                    uv_strerror(r),
                                    filename);
    Local<String> message = OneByteString(isolate, buf.c_str());
    isolate->ThrowException(v8::Exception::Error(message));
    return MaybeLocal<String>();
  }
  return String::NewFromUtf8(
      isolate, contents.c_str(), v8::NewStringType::kNormal, contents.length());
#endif  // NODE_BUILTIN_MODULES_PATH
}

namespace {
static Mutex externalized_builtins_mutex;
std::unordered_map<std::string, std::unique_ptr<StaticExternalTwoByteResource>>
    externalized_builtin_sources;
}  // namespace

void BuiltinLoader::AddExternalizedBuiltin(const char* id,
                                           const char* filename) {
  StaticExternalTwoByteResource* resource;
  {
    Mutex::ScopedLock lock(externalized_builtins_mutex);
    auto it = externalized_builtin_sources.find(id);
    if (it == externalized_builtin_sources.end()) {
      std::string source;
      int r = ReadFileSync(&source, filename);
      if (r != 0) {
        fprintf(stderr,
                "Cannot load externalized builtin: \"%s:%s\".\n",
                id,
                filename);
        ABORT();
      }
      size_t expected_u16_length =
          simdutf::utf16_length_from_utf8(source.data(), source.length());
      auto out = std::make_shared<std::vector<uint16_t>>(expected_u16_length);
      size_t u16_length = simdutf::convert_utf8_to_utf16(
          source.data(),
          source.length(),
          reinterpret_cast<char16_t*>(out->data()));
      out->resize(u16_length);

      auto result = externalized_builtin_sources.emplace(
          id,
          std::make_unique<StaticExternalTwoByteResource>(
              out->data(), out->size(), out));
      CHECK(result.second);
      it = result.first;
    }
    // OK to get the raw pointer, since externalized_builtin_sources owns
    // the resource, resources are never removed from the map, and
    // externalized_builtin_sources has static lifetime.
    resource = it->second.get();
  }

  Add(id, UnionBytes(resource));
}

MaybeLocal<Function> BuiltinLoader::LookupAndCompileInternal(
    Local<Context> context,
    const char* id,
    std::vector<Local<String>>* parameters,
    Realm* optional_realm) {
  Isolate* isolate = context->GetIsolate();
  EscapableHandleScope scope(isolate);

  Local<String> source;
  if (!LoadBuiltinSource(isolate, id).ToLocal(&source)) {
    return {};
  }

  std::string filename_s = std::string("node:") + id;
  Local<String> filename =
      OneByteString(isolate, filename_s.c_str(), filename_s.size());
  ScriptOrigin origin(filename, 0, 0, true);

  BuiltinCodeCacheData cached_data{};
  {
    // Note: The lock here should not extend into the
    // `CompileFunction()` call below, because this function may recurse if
    // there is a syntax error during bootstrap (because the fatal exception
    // handler is invoked, which may load built-in modules).
    RwLock::ScopedLock lock(code_cache_->mutex);
    auto cache_it = code_cache_->map.find(id);
    if (cache_it != code_cache_->map.end()) {
      // Transfer ownership to ScriptCompiler::Source later.
      cached_data = cache_it->second;
    }
  }

  const bool has_cache = cached_data.data != nullptr;
  ScriptCompiler::CompileOptions options =
      has_cache ? ScriptCompiler::kConsumeCodeCache
                : ScriptCompiler::kNoCompileOptions;
  if (should_eager_compile_) {
    options = ScriptCompiler::kEagerCompile;
  } else if (!to_eager_compile_.empty()) {
    if (to_eager_compile_.find(id) != to_eager_compile_.end()) {
      options = ScriptCompiler::kEagerCompile;
    }
  }
  ScriptCompiler::Source script_source(
      source,
      origin,
      has_cache ? cached_data.AsCachedData().release() : nullptr);

  per_process::Debug(
      DebugCategory::CODE_CACHE,
      "Compiling %s %s code cache %s\n",
      id,
      has_cache ? "with" : "without",
      options == ScriptCompiler::kEagerCompile ? "eagerly" : "lazily");

  MaybeLocal<Function> maybe_fun =
      ScriptCompiler::CompileFunction(context,
                                      &script_source,
                                      parameters->size(),
                                      parameters->data(),
                                      0,
                                      nullptr,
                                      options);

  // This could fail when there are early errors in the built-in modules,
  // e.g. the syntax errors
  Local<Function> fun;
  if (!maybe_fun.ToLocal(&fun)) {
    // In the case of early errors, v8 is already capable of
    // decorating the stack for us - note that we use CompileFunction
    // so there is no need to worry about wrappers.
    return MaybeLocal<Function>();
  }

  // XXX(joyeecheung): this bookkeeping is not exactly accurate because
  // it only starts after the Environment is created, so the per_context.js
  // will never be in any of these two sets, but the two sets are only for
  // testing anyway.

  Result result = (has_cache && !script_source.GetCachedData()->rejected)
                      ? Result::kWithCache
                      : Result::kWithoutCache;
  if (optional_realm != nullptr) {
    DCHECK_EQ(this, optional_realm->env()->builtin_loader());
    RecordResult(id, result, optional_realm);
  }

  if (has_cache) {
    per_process::Debug(DebugCategory::CODE_CACHE,
                       "Code cache of %s (%s) %s\n",
                       id,
                       script_source.GetCachedData()->buffer_policy ==
                               ScriptCompiler::CachedData::BufferNotOwned
                           ? "BufferNotOwned"
                           : "BufferOwned",
                       script_source.GetCachedData()->rejected ? "is rejected"
                                                               : "is accepted");
  }

  if (result == Result::kWithoutCache && optional_realm != nullptr &&
      !optional_realm->env()->isolate_data()->is_building_snapshot()) {
    // We failed to accept this cache, maybe because it was rejected, maybe
    // because it wasn't present. Either way, we'll attempt to replace this
    // code cache info with a new one.
    // This is only done when the isolate is not being serialized because
    // V8 does not support serializing code cache with an unfinalized read-only
    // space (which is what isolates pending to be serialized have).
    SaveCodeCache(id, fun);
  }

  return scope.Escape(fun);
}

void BuiltinLoader::SaveCodeCache(const char* id, Local<Function> fun) {
  std::shared_ptr<ScriptCompiler::CachedData> new_cached_data(
      ScriptCompiler::CreateCodeCacheForFunction(fun));
  CHECK_NOT_NULL(new_cached_data);

  {
    RwLock::ScopedLock lock(code_cache_->mutex);
    code_cache_->map.insert_or_assign(
        id, BuiltinCodeCacheData(std::move(new_cached_data)));
  }
}

MaybeLocal<Function> BuiltinLoader::LookupAndCompile(Local<Context> context,
                                                     const char* id,
                                                     Realm* optional_realm) {
  std::vector<Local<String>> parameters;
  Isolate* isolate = context->GetIsolate();
  // Detects parameters of the scripts based on module ids.
  // internal/bootstrap/realm: process, getLinkedBinding,
  //                           getInternalBinding, primordials
  if (strcmp(id, "internal/bootstrap/realm") == 0) {
    parameters = {
        FIXED_ONE_BYTE_STRING(isolate, "process"),
        FIXED_ONE_BYTE_STRING(isolate, "getLinkedBinding"),
        FIXED_ONE_BYTE_STRING(isolate, "getInternalBinding"),
        FIXED_ONE_BYTE_STRING(isolate, "primordials"),
    };
  } else if (strncmp(id,
                     "internal/per_context/",
                     strlen("internal/per_context/")) == 0) {
    // internal/per_context/*: global, exports, primordials
    parameters = {
        FIXED_ONE_BYTE_STRING(isolate, "exports"),
        FIXED_ONE_BYTE_STRING(isolate, "primordials"),
    };
  } else if (strncmp(id, "internal/main/", strlen("internal/main/")) == 0 ||
             strncmp(id,
                     "internal/bootstrap/",
                     strlen("internal/bootstrap/")) == 0) {
    // internal/main/*, internal/bootstrap/*: process, require,
    //                                        internalBinding, primordials
    parameters = {
        FIXED_ONE_BYTE_STRING(isolate, "process"),
        FIXED_ONE_BYTE_STRING(isolate, "require"),
        FIXED_ONE_BYTE_STRING(isolate, "internalBinding"),
        FIXED_ONE_BYTE_STRING(isolate, "primordials"),
    };
  } else {
    // others: exports, require, module, process, internalBinding, primordials
    parameters = {
        FIXED_ONE_BYTE_STRING(isolate, "exports"),
        FIXED_ONE_BYTE_STRING(isolate, "require"),
        FIXED_ONE_BYTE_STRING(isolate, "module"),
        FIXED_ONE_BYTE_STRING(isolate, "process"),
        FIXED_ONE_BYTE_STRING(isolate, "internalBinding"),
        FIXED_ONE_BYTE_STRING(isolate, "primordials"),
    };
  }

  MaybeLocal<Function> maybe =
      LookupAndCompileInternal(context, id, &parameters, optional_realm);
  return maybe;
}

MaybeLocal<Value> BuiltinLoader::CompileAndCall(Local<Context> context,
                                                const char* id,
                                                Realm* realm) {
  Isolate* isolate = context->GetIsolate();
  // Arguments must match the parameters specified in
  // BuiltinLoader::LookupAndCompile().
  std::vector<Local<Value>> arguments;
  // Detects parameters of the scripts based on module ids.
  // internal/bootstrap/realm: process, getLinkedBinding,
  //                           getInternalBinding, primordials
  if (strcmp(id, "internal/bootstrap/realm") == 0) {
    Local<Value> get_linked_binding;
    Local<Value> get_internal_binding;
    if (!NewFunctionTemplate(isolate, binding::GetLinkedBinding)
             ->GetFunction(context)
             .ToLocal(&get_linked_binding) ||
        !NewFunctionTemplate(isolate, binding::GetInternalBinding)
             ->GetFunction(context)
             .ToLocal(&get_internal_binding)) {
      return MaybeLocal<Value>();
    }
    arguments = {realm->process_object(),
                 get_linked_binding,
                 get_internal_binding,
                 realm->primordials()};
  } else if (strncmp(id, "internal/main/", strlen("internal/main/")) == 0 ||
             strncmp(id,
                     "internal/bootstrap/",
                     strlen("internal/bootstrap/")) == 0) {
    // internal/main/*, internal/bootstrap/*: process, require,
    //                                        internalBinding, primordials
    arguments = {realm->process_object(),
                 realm->builtin_module_require(),
                 realm->internal_binding_loader(),
                 realm->primordials()};
  } else {
    // This should be invoked with the other CompileAndCall() methods, as
    // we are unable to generate the arguments.
    // Currently there are two cases:
    // internal/per_context/*: the arguments are generated in
    //                         InitializePrimordials()
    // all the other cases: the arguments are generated in the JS-land loader.
    UNREACHABLE();
  }
  return CompileAndCall(context, id, arguments.size(), arguments.data(), realm);
}

MaybeLocal<Value> BuiltinLoader::CompileAndCall(Local<Context> context,
                                                const char* id,
                                                int argc,
                                                Local<Value> argv[],
                                                Realm* optional_realm) {
  // Arguments must match the parameters specified in
  // BuiltinLoader::LookupAndCompile().
  MaybeLocal<Function> maybe_fn = LookupAndCompile(context, id, optional_realm);
  Local<Function> fn;
  if (!maybe_fn.ToLocal(&fn)) {
    return MaybeLocal<Value>();
  }
  Local<Value> undefined = Undefined(context->GetIsolate());
  return fn->Call(context, undefined, argc, argv);
}

MaybeLocal<Function> BuiltinLoader::LookupAndCompile(
    Local<Context> context,
    const char* id,
    std::vector<Local<String>>* parameters,
    Realm* optional_realm) {
  return LookupAndCompileInternal(context, id, parameters, optional_realm);
}

bool BuiltinLoader::CompileAllBuiltinsAndCopyCodeCache(
    Local<Context> context,
    const std::vector<std::string>& eager_builtins,
    std::vector<CodeCacheInfo>* out) {
  std::vector<std::string_view> ids = GetBuiltinIds();
  bool all_succeeded = true;
  std::string v8_tools_prefix = "internal/deps/v8/tools/";
  std::string primordial_prefix = "internal/per_context/";
  std::string bootstrap_prefix = "internal/bootstrap/";
  std::string main_prefix = "internal/main/";
  to_eager_compile_ = std::unordered_set<std::string>(eager_builtins.begin(),
                                                      eager_builtins.end());

  for (const auto& id : ids) {
    if (id.compare(0, v8_tools_prefix.size(), v8_tools_prefix) == 0) {
      // No need to generate code cache for v8 scripts.
      continue;
    }

    // Eagerly compile primordials/boostrap/main scripts during code cache
    // generation.
    if (id.compare(0, primordial_prefix.size(), primordial_prefix) == 0 ||
        id.compare(0, bootstrap_prefix.size(), bootstrap_prefix) == 0 ||
        id.compare(0, main_prefix.size(), main_prefix) == 0) {
      to_eager_compile_.emplace(id);
    }

    v8::TryCatch bootstrapCatch(context->GetIsolate());
    auto fn = LookupAndCompile(context, id.data(), nullptr);
    if (bootstrapCatch.HasCaught()) {
      per_process::Debug(DebugCategory::CODE_CACHE,
                         "Failed to compile code cache for %s\n",
                         id.data());
      all_succeeded = false;
      PrintCaughtException(context->GetIsolate(), context, bootstrapCatch);
    } else {
      // This is used by the snapshot builder, so save the code cache
      // unconditionally.
      SaveCodeCache(id.data(), fn.ToLocalChecked());
    }
  }

  RwLock::ScopedReadLock lock(code_cache_->mutex);
  for (auto const& item : code_cache_->map) {
    out->push_back({item.first, item.second});
  }
  return all_succeeded;
}

void BuiltinLoader::RefreshCodeCache(const std::vector<CodeCacheInfo>& in) {
  RwLock::ScopedLock lock(code_cache_->mutex);
  code_cache_->map.reserve(in.size());
  DCHECK(code_cache_->map.empty());
  for (auto const& item : in) {
    auto result = code_cache_->map.emplace(item.id, item.data);
    USE(result.second);
    DCHECK(result.second);
  }
  code_cache_->has_code_cache = true;
}

void BuiltinLoader::GetBuiltinCategories(
    Local<Name> property, const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();
  Local<Object> result = Object::New(isolate);

  BuiltinCategories builtin_categories =
      env->builtin_loader()->GetBuiltinCategories();

  if (!env->owns_process_state()) {
    builtin_categories.can_be_required.erase("trace_events");
    builtin_categories.cannot_be_required.insert("trace_events");
  }

  Local<Value> cannot_be_required_js;
  Local<Value> can_be_required_js;

  if (!ToV8Value(context, builtin_categories.cannot_be_required)
           .ToLocal(&cannot_be_required_js))
    return;
  if (result
          ->Set(context,
                OneByteString(isolate, "cannotBeRequired"),
                cannot_be_required_js)
          .IsNothing())
    return;
  if (!ToV8Value(context, builtin_categories.can_be_required)
           .ToLocal(&can_be_required_js))
    return;
  if (result
          ->Set(context,
                OneByteString(isolate, "canBeRequired"),
                can_be_required_js)
          .IsNothing()) {
    return;
  }
  info.GetReturnValue().Set(result);
}

void BuiltinLoader::GetCacheUsage(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Isolate* isolate = realm->isolate();
  Local<Context> context = realm->context();
  Local<Object> result = Object::New(isolate);

  Local<Value> builtins_with_cache_js;
  Local<Value> builtins_without_cache_js;
  Local<Value> builtins_in_snapshot_js;
  if (!ToV8Value(context, realm->builtins_with_cache)
           .ToLocal(&builtins_with_cache_js)) {
    return;
  }
  if (result
          ->Set(context,
                OneByteString(isolate, "compiledWithCache"),
                builtins_with_cache_js)
          .IsNothing()) {
    return;
  }

  if (!ToV8Value(context, realm->builtins_without_cache)
           .ToLocal(&builtins_without_cache_js)) {
    return;
  }
  if (result
          ->Set(context,
                OneByteString(isolate, "compiledWithoutCache"),
                builtins_without_cache_js)
          .IsNothing()) {
    return;
  }

  if (!ToV8Value(context, realm->builtins_in_snapshot)
           .ToLocal(&builtins_in_snapshot_js)) {
    return;
  }
  if (result
          ->Set(context,
                OneByteString(isolate, "compiledInSnapshot"),
                builtins_in_snapshot_js)
          .IsNothing()) {
    return;
  }

  args.GetReturnValue().Set(result);
}

void BuiltinLoader::BuiltinIdsGetter(Local<Name> property,
                                     const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();

  std::vector<std::string_view> ids = env->builtin_loader()->GetBuiltinIds();
  info.GetReturnValue().Set(
      ToV8Value(isolate->GetCurrentContext(), ids).ToLocalChecked());
}

void BuiltinLoader::ConfigStringGetter(
    Local<Name> property, const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  info.GetReturnValue().Set(
      env->builtin_loader()->GetConfigString(info.GetIsolate()));
}

void BuiltinLoader::RecordResult(const char* id,
                                 BuiltinLoader::Result result,
                                 Realm* realm) {
  if (result == BuiltinLoader::Result::kWithCache) {
    realm->builtins_with_cache.insert(id);
  } else {
    realm->builtins_without_cache.insert(id);
  }
}

void BuiltinLoader::CompileFunction(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  CHECK(args[0]->IsString());
  node::Utf8Value id_v(realm->isolate(), args[0].As<String>());
  const char* id = *id_v;
  MaybeLocal<Function> maybe = realm->env()->builtin_loader()->LookupAndCompile(
      realm->context(), id, realm);
  Local<Function> fn;
  if (maybe.ToLocal(&fn)) {
    args.GetReturnValue().Set(fn);
  }
}

void BuiltinLoader::HasCachedBuiltins(const FunctionCallbackInfo<Value>& args) {
  auto instance = Environment::GetCurrent(args)->builtin_loader();
  RwLock::ScopedReadLock lock(instance->code_cache_->mutex);
  args.GetReturnValue().Set(v8::Boolean::New(
      args.GetIsolate(), instance->code_cache_->has_code_cache));
}

void SetInternalLoaders(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  CHECK(args[0]->IsFunction());
  CHECK(args[1]->IsFunction());
  DCHECK(realm->internal_binding_loader().IsEmpty());
  DCHECK(realm->builtin_module_require().IsEmpty());
  realm->set_internal_binding_loader(args[0].As<Function>());
  realm->set_builtin_module_require(args[1].As<Function>());
}

void BuiltinLoader::CopySourceAndCodeCacheReferenceFrom(
    const BuiltinLoader* other) {
  code_cache_ = other->code_cache_;
  source_ = other->source_;
}

void BuiltinLoader::CreatePerIsolateProperties(IsolateData* isolate_data,
                                               Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  target->SetNativeDataProperty(isolate_data->config_string(),
                                ConfigStringGetter,
                                nullptr,
                                Local<Value>(),
                                None,
                                SideEffectType::kHasNoSideEffect);

  target->SetNativeDataProperty(FIXED_ONE_BYTE_STRING(isolate, "builtinIds"),
                                BuiltinIdsGetter,
                                nullptr,
                                Local<Value>(),
                                None,
                                SideEffectType::kHasNoSideEffect);

  target->SetNativeDataProperty(
      FIXED_ONE_BYTE_STRING(isolate, "builtinCategories"),
      GetBuiltinCategories,
      nullptr,
      Local<Value>(),
      None,
      SideEffectType::kHasNoSideEffect);

  target->SetNativeDataProperty(FIXED_ONE_BYTE_STRING(isolate, "natives"),
                                GetNatives,
                                nullptr,
                                Local<Value>(),
                                None,
                                SideEffectType::kHasNoSideEffect);

  SetMethod(isolate, target, "getCacheUsage", BuiltinLoader::GetCacheUsage);
  SetMethod(isolate, target, "compileFunction", BuiltinLoader::CompileFunction);
  SetMethod(isolate, target, "hasCachedBuiltins", HasCachedBuiltins);
  SetMethod(isolate, target, "setInternalLoaders", SetInternalLoaders);
}

void BuiltinLoader::CreatePerContextProperties(Local<Object> target,
                                               Local<Value> unused,
                                               Local<Context> context,
                                               void* priv) {
  // internalBinding('builtins') should be frozen
  target->SetIntegrityLevel(context, IntegrityLevel::kFrozen).FromJust();
}

void BuiltinLoader::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(ConfigStringGetter);
  registry->Register(BuiltinIdsGetter);
  registry->Register(GetBuiltinCategories);
  registry->Register(GetCacheUsage);
  registry->Register(CompileFunction);
  registry->Register(HasCachedBuiltins);
  registry->Register(SetInternalLoaders);
  registry->Register(GetNatives);

  RegisterExternalReferencesForInternalizedBuiltinCode(registry);
}

}  // namespace builtins
}  // namespace node

NODE_BINDING_PER_ISOLATE_INIT(
    builtins, node::builtins::BuiltinLoader::CreatePerIsolateProperties)
NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    builtins, node::builtins::BuiltinLoader::CreatePerContextProperties)
NODE_BINDING_EXTERNAL_REFERENCE(
    builtins, node::builtins::BuiltinLoader::RegisterExternalReferences)
