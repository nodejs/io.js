#include "permission.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_file.h"

#include "v8.h"

#include <memory>
#include <string>
#include <vector>

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Object;
using v8::String;
using v8::Value;

namespace permission {

namespace {

// permission.has('fs.in', '/tmp/')
// permission.has('fs.in')
static void Has(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  v8::Isolate* isolate = env->isolate();
  CHECK(args[0]->IsString());

  String::Utf8Value utf8_deny_scope(isolate, args[0]);
  if (*utf8_deny_scope == nullptr) {
    return;
  }

  const std::string deny_scope = *utf8_deny_scope;
  PermissionScope scope = Permission::StringToPermission(deny_scope);
  if (scope == PermissionScope::kPermissionsRoot) {
    return args.GetReturnValue().Set(false);
  }

  if (args.Length() > 1 && !args[1]->IsUndefined()) {
    String::Utf8Value utf8_arg(isolate, args[1]);
    if (*utf8_arg == nullptr) {
      return;
    }
    return args.GetReturnValue().Set(
        env->permission()->is_granted(env, scope, *utf8_arg));
  }

  return args.GetReturnValue().Set(env->permission()->is_granted(env, scope));
}

}  // namespace

#define V(Name, label, _)                                                      \
  if (perm == PermissionScope::k##Name) return #Name;
const char* Permission::PermissionToString(const PermissionScope perm) {
  PERMISSIONS(V)
  return nullptr;
}
#undef V

#define V(Name, label, _)                                                      \
  if (perm == label) return PermissionScope::k##Name;
PermissionScope Permission::StringToPermission(const std::string& perm) {
  PERMISSIONS(V)
  return PermissionScope::kPermissionsRoot;
}
#undef V

Permission::Permission() : enabled_(false) {
  std::shared_ptr<PermissionBase> fs = std::make_shared<FSPermission>();
  std::shared_ptr<PermissionBase> child_p =
      std::make_shared<ChildProcessPermission>();
  std::shared_ptr<PermissionBase> worker_t =
      std::make_shared<WorkerPermission>();
  std::shared_ptr<PermissionBase> inspector =
      std::make_shared<InspectorPermission>();
  std::shared_ptr<PermissionBase> wasi = std::make_shared<WASIPermission>();
#define V(Name, _, __)                                                         \
  nodes_.insert(std::make_pair(PermissionScope::k##Name, fs));
  FILESYSTEM_PERMISSIONS(V)
#undef V
#define V(Name, _, __)                                                         \
  nodes_.insert(std::make_pair(PermissionScope::k##Name, child_p));
  CHILD_PROCESS_PERMISSIONS(V)
#undef V
#define V(Name, _, __)                                                         \
  nodes_.insert(std::make_pair(PermissionScope::k##Name, worker_t));
  WORKER_THREADS_PERMISSIONS(V)
#undef V
#define V(Name, _, __)                                                         \
  nodes_.insert(std::make_pair(PermissionScope::k##Name, inspector));
  INSPECTOR_PERMISSIONS(V)
#undef V
#define V(Name, _, __)                                                         \
  nodes_.insert(std::make_pair(PermissionScope::k##Name, wasi));
  WASI_PERMISSIONS(V)
#undef V
}

MaybeLocal<Value> CreateAccessDeniedError(Environment* env,
                                          PermissionScope perm,
                                          const std::string_view& res) {
  Local<Object> err = ERR_ACCESS_DENIED(env->isolate());
  Local<String> perm_string;
  Local<String> resource_string;
  if (!String::NewFromUtf8(env->isolate(),
                           Permission::PermissionToString(perm),
                           NewStringType::kNormal)
           .ToLocal(&perm_string) ||
      !String::NewFromUtf8(
           env->isolate(), std::string(res).c_str(), NewStringType::kNormal)
           .ToLocal(&resource_string) ||
      err->Set(env->context(), env->permission_string(), perm_string)
          .IsNothing() ||
      err->Set(env->context(), env->resource_string(), resource_string)
          .IsNothing()) {
    return MaybeLocal<Value>();
  }
  return err;
}

void Permission::ThrowAccessDenied(Environment* env,
                                   PermissionScope perm,
                                   const std::string_view& res) {
  MaybeLocal<Value> err = CreateAccessDeniedError(env, perm, res);
  if (err.IsEmpty()) return;
  env->isolate()->ThrowException(err.ToLocalChecked());
}

void Permission::AsyncThrowAccessDenied(Environment* env,
                                        fs::FSReqBase* req_wrap,
                                        PermissionScope perm,
                                        const std::string_view& res) {
  MaybeLocal<Value> err = CreateAccessDeniedError(env, perm, res);
  if (err.IsEmpty()) return;
  return req_wrap->Reject(err.ToLocalChecked());
}

void Permission::EnablePermissions() {
  if (!enabled_) {
    enabled_ = true;
  }
}

void Permission::Apply(Environment* env,
                       const std::vector<std::string>& allow,
                       PermissionScope scope) {
  auto permission = nodes_.find(scope);
  if (permission != nodes_.end()) {
    permission->second->Apply(env, allow, scope);
  }
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  SetMethodNoSideEffect(context, target, "has", Has);

  target->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromJust();
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(Has);
}

}  // namespace permission
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(permission, node::permission::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(permission,
                                node::permission::RegisterExternalReferences)
