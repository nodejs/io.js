#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_metadata.h"
#include "node_options-inl.h"
#include "node_process-inl.h"
#include "node_realm-inl.h"
#include "node_revert.h"
#include "util-inl.h"

#include <climits>  // PATH_MAX

namespace node {
using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Name;
using v8::NewStringType;
using v8::None;
using v8::Object;
using v8::PropertyCallbackInfo;
using v8::SideEffectType;
using v8::String;
using v8::Value;

static void ProcessTitleGetter(Local<Name> property,
                               const PropertyCallbackInfo<Value>& info) {
  std::string title = GetProcessTitle("node");
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), title.data(),
                          NewStringType::kNormal, title.size())
      .ToLocalChecked());
}

static void ProcessTitleSetter(Local<Name> property,
                               Local<Value> value,
                               const PropertyCallbackInfo<void>& info) {
  node::Utf8Value title(info.GetIsolate(), value);
  TRACE_EVENT_METADATA1(
      "__metadata", "process_name", "name", TRACE_STR_COPY(*title));
  uv_set_process_title(*title);
}

static void DebugPortGetter(Local<Name> property,
                            const PropertyCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  ExclusiveAccess<HostPort>::Scoped host_port(env->inspector_host_port());
  int port = host_port->port();
  info.GetReturnValue().Set(port);
}

static void DebugPortSetter(Local<Name> property,
                            Local<Value> value,
                            const PropertyCallbackInfo<void>& info) {
  Environment* env = Environment::GetCurrent(info);
  int32_t port = value->Int32Value(env->context()).FromMaybe(0);

  if ((port != 0 && port < 1024) || port > 65535) {
    return THROW_ERR_OUT_OF_RANGE(
      env,
      "process.debugPort must be 0 or in range 1024 to 65535");
  }

  ExclusiveAccess<HostPort>::Scoped host_port(env->inspector_host_port());
  host_port->set_port(static_cast<int>(port));
}

static void GetParentProcessId(Local<Name> property,
                               const PropertyCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(uv_os_getppid());
}

static void SetVersions(Isolate* isolate, Local<Object> versions) {
  Local<Context> context = isolate->GetCurrentContext();

  // Node.js version is always on the top
  READONLY_STRING_PROPERTY(
      versions, "node", per_process::metadata.versions.node);

#define V(key) +1
  std::pair<std::string_view, std::string_view>
      versions_array[NODE_VERSIONS_KEYS(V)];
#undef V
  auto* slot = &versions_array[0];

#define V(key)                                                                 \
  do {                                                                         \
    *slot++ = std::pair<std::string_view, std::string_view>(                   \
        #key, per_process::metadata.versions.key);                             \
  } while (0);
  NODE_VERSIONS_KEYS(V)
#undef V

  std::sort(&versions_array[0],
            &versions_array[arraysize(versions_array)],
            [](auto& a, auto& b) { return a.first < b.first; });

  for (const auto& version : versions_array) {
    versions
        ->DefineOwnProperty(context,
                            OneByteString(isolate, version.first),
                            OneByteString(isolate, version.second),
                            v8::ReadOnly)
        .Check();
  }
}

MaybeLocal<Object> CreateProcessObject(Realm* realm) {
  Isolate* isolate = realm->isolate();
  EscapableHandleScope scope(isolate);
  Local<Context> context = realm->context();

  Local<FunctionTemplate> process_template = FunctionTemplate::New(isolate);
  process_template->SetClassName(realm->env()->process_string());
  Local<Function> process_ctor;
  Local<Object> process;
  if (!process_template->GetFunction(context).ToLocal(&process_ctor) ||
      !process_ctor->NewInstance(context).ToLocal(&process)) {
    return MaybeLocal<Object>();
  }

  // process[exit_info_private_symbol]
  if (process
          ->SetPrivate(context,
                       realm->env()->exit_info_private_symbol(),
                       realm->env()->exit_info().GetJSArray())
          .IsNothing()) {
    return {};
  }

  // process.version
  READONLY_PROPERTY(
      process, "version", FIXED_ONE_BYTE_STRING(isolate, NODE_VERSION));

  // process.versions
  Local<Object> versions = Object::New(isolate);
  SetVersions(isolate, versions);
  READONLY_PROPERTY(process, "versions", versions);

  // process.arch
  READONLY_STRING_PROPERTY(process, "arch", per_process::metadata.arch);

  // process.platform
  READONLY_STRING_PROPERTY(process, "platform", per_process::metadata.platform);

  // process.release
  Local<Object> release = Object::New(isolate);
  READONLY_PROPERTY(process, "release", release);
  READONLY_STRING_PROPERTY(release, "name", per_process::metadata.release.name);
#if NODE_VERSION_IS_LTS
  READONLY_STRING_PROPERTY(release, "lts", per_process::metadata.release.lts);
#endif  // NODE_VERSION_IS_LTS

#ifdef NODE_HAS_RELEASE_URLS
  READONLY_STRING_PROPERTY(
      release, "sourceUrl", per_process::metadata.release.source_url);
  READONLY_STRING_PROPERTY(
      release, "headersUrl", per_process::metadata.release.headers_url);
#ifdef _WIN32
  READONLY_STRING_PROPERTY(
      release, "libUrl", per_process::metadata.release.lib_url);
#endif  // _WIN32
#endif  // NODE_HAS_RELEASE_URLS

  // process._rawDebug: may be overwritten later in JS land, but should be
  // available from the beginning for debugging purposes
  SetMethod(context, process, "_rawDebug", RawDebug);

  return scope.Escape(process);
}

void PatchProcessObject(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  CHECK(args[0]->IsObject());
  Local<Object> process = args[0].As<Object>();

  // process.title
  CHECK(process
            ->SetNativeDataProperty(
                context,
                FIXED_ONE_BYTE_STRING(isolate, "title"),
                ProcessTitleGetter,
                env->owns_process_state() ? ProcessTitleSetter : nullptr,
                Local<Value>(),
                None,
                SideEffectType::kHasNoSideEffect)
            .FromJust());

  // process.argv
  process->Set(context,
               FIXED_ONE_BYTE_STRING(isolate, "argv"),
               ToV8Value(context, env->argv()).ToLocalChecked()).Check();

  // process.execArgv
  process->Set(context,
               FIXED_ONE_BYTE_STRING(isolate, "execArgv"),
               ToV8Value(context, env->exec_argv())
                   .ToLocalChecked()).Check();

  READONLY_PROPERTY(process, "pid",
                    Integer::New(isolate, uv_os_getpid()));

  CHECK(process
            ->SetNativeDataProperty(context,
                                    FIXED_ONE_BYTE_STRING(isolate, "ppid"),
                                    GetParentProcessId,
                                    nullptr,
                                    Local<Value>(),
                                    None,
                                    SideEffectType::kHasNoSideEffect)
            .FromJust());

  // --security-revert flags
#define V(code, _, __)                                                        \
  do {                                                                        \
    if (IsReverted(SECURITY_REVERT_ ## code)) {                               \
      READONLY_PROPERTY(process, "REVERT_" #code, True(isolate));             \
    }                                                                         \
  } while (0);
  SECURITY_REVERSIONS(V)
#undef V

  // process.execPath
  process
      ->Set(context,
            FIXED_ONE_BYTE_STRING(isolate, "execPath"),
            String::NewFromUtf8(isolate,
                                env->exec_path().c_str(),
                                NewStringType::kInternalized,
                                env->exec_path().size())
                .ToLocalChecked())
      .Check();

  // process.debugPort
  CHECK(process
            ->SetNativeDataProperty(
                context,
                FIXED_ONE_BYTE_STRING(isolate, "debugPort"),
                DebugPortGetter,
                env->owns_process_state() ? DebugPortSetter : nullptr,
                Local<Value>(),
                None,
                SideEffectType::kHasNoSideEffect)
            .FromJust());

  // process.versions
  Local<Object> versions = Object::New(isolate);
  SetVersions(isolate, versions);
  READONLY_PROPERTY(process, "versions", versions);
}

void RegisterProcessExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(RawDebug);
  registry->Register(GetParentProcessId);
  registry->Register(DebugPortSetter);
  registry->Register(DebugPortGetter);
  registry->Register(ProcessTitleSetter);
  registry->Register(ProcessTitleGetter);
}

}  // namespace node

NODE_BINDING_EXTERNAL_REFERENCE(process_object,
                                node::RegisterProcessExternalReferences)
