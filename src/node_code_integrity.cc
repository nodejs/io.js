#include "node_code_integrity.h"
#include "env-inl.h"
#include "node.h"
#include "node_external_reference.h"
#include "v8.h"

namespace node {

using v8::Boolean;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Local;
using v8::Object;
using v8::Value;

namespace per_process {
bool isWldpInitialized = false;
pfnWldpCanExecuteFile WldpCanExecuteFile;
pfnWldpGetApplicationSettingBoolean WldpGetApplicationSettingBoolean;
pfnWldpQuerySecurityPolicy WldpQuerySecurityPolicy;
}  // namespace per_process

namespace code_integrity {

static PCWSTR NODEJS = L"Node.js";
static PCWSTR ENFORCE_CODE_INTEGRITY_SETTING_NAME = L"EnforceCodeIntegrity";
static PCWSTR DISABLE_INTERPRETIVE_MODE_SETTING_NAME =
    L"DisableInteractiveMode";

void InitWldp(Environment* env) {
  if (per_process::isWldpInitialized) {
    return;
  }

  HMODULE wldp_module =
      LoadLibraryExA("wldp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

  if (wldp_module == nullptr) {
    return env->ThrowError("Unable to load wldp.dll");
  }

  per_process::WldpCanExecuteFile =
      (pfnWldpCanExecuteFile)GetProcAddress(wldp_module, "WldpCanExecuteFile");

  per_process::WldpGetApplicationSettingBoolean =
      (pfnWldpGetApplicationSettingBoolean)GetProcAddress(
          wldp_module, "WldpGetApplicationSettingBoolean");

  per_process::WldpQuerySecurityPolicy =
      (pfnWldpQuerySecurityPolicy)GetProcAddress(wldp_module,
                                                 "WldpQuerySecurityPolicy");

  per_process::isWldpInitialized = true;
}

static void IsFileTrustedBySystemCodeIntegrityPolicy(
    const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());

  Environment* env = Environment::GetCurrent(args);
  if (!per_process::isWldpInitialized) {
    InitWldp(env);
  }

  BufferValue path(env->isolate(), args[0]);
  if (*path == nullptr) {
    return env->ThrowError("path cannot be empty");
  }

  HANDLE hFile = CreateFileA(*path,
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);

  if (hFile == INVALID_HANDLE_VALUE || hFile == nullptr) {
    return env->ThrowError("Unable to open file");
  }

  const GUID wldp_host_other = WLDP_HOST_OTHER;
  WLDP_EXECUTION_POLICY result;
  HRESULT hr =
      per_process::WldpCanExecuteFile(wldp_host_other,
                                      WLDP_EXECUTION_EVALUATION_OPTION_NONE,
                                      hFile,
                                      NODEJS,
                                      &result);
  CloseHandle(hFile);

  if (FAILED(hr)) {
    return env->ThrowError("WldpCanExecuteFile failed");
  }

  bool isFileTrusted = (result == WLDP_EXECUTION_POLICY_ALLOWED);
  args.GetReturnValue().Set(isFileTrusted);
}

static void IsInteractiveModeDisabledInternal(
    const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 0);

  Environment* env = Environment::GetCurrent(args);

  if (!per_process::isWldpInitialized) {
    InitWldp(env);
  }

  if (per_process::WldpGetApplicationSettingBoolean != nullptr) {
    BOOL ret;
    HRESULT hr = per_process::WldpGetApplicationSettingBoolean(
        NODEJS, DISABLE_INTERPRETIVE_MODE_SETTING_NAME, &ret);

    if (SUCCEEDED(hr)) {
      args.GetReturnValue().Set(Boolean::New(env->isolate(), ret));
      return;
    } else if (hr != E_NOTFOUND) {
      // If the setting is not found, continue through to attempt
      // WldpQuerySecurityPolicy, as the setting may be defined
      // in the old settings format
      args.GetReturnValue().Set(Boolean::New(env->isolate(), false));
      return;
    }
  }

  // WldpGetApplicationSettingBoolean is the preferred way for applications to
  // query security policy values. However, this method only exists on Windows
  // versions going back to circa Win10 2023H2. In order to support systems
  // older than that (down to Win10RS2), we can use the deprecated
  // WldpQuerySecurityPolicy
  if (per_process::WldpQuerySecurityPolicy != nullptr) {
    DECLARE_CONST_UNICODE_STRING(providerName, L"Node.js");
    DECLARE_CONST_UNICODE_STRING(keyName, L"Settings");
    DECLARE_CONST_UNICODE_STRING(valueName, L"DisableInteractiveMode");
    WLDP_SECURE_SETTING_VALUE_TYPE valueType =
        WLDP_SECURE_SETTING_VALUE_TYPE_BOOLEAN;
    ULONG valueSize = sizeof(int);
    int ret = 0;
    HRESULT hr = per_process::WldpQuerySecurityPolicy(
        &providerName, &keyName, &valueName, &valueType, &ret, &valueSize);
    if (FAILED(hr)) {
      args.GetReturnValue().Set(Boolean::New(env->isolate(), false));
      return;
    }

    args.GetReturnValue().Set(
        Boolean::New(env->isolate(), static_cast<bool>(ret)));
  }
}

static void IsSystemEnforcingCodeIntegrity(
    const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 0);

  Environment* env = Environment::GetCurrent(args);

  if (!per_process::isWldpInitialized) {
    InitWldp(env);
  }

  if (per_process::WldpGetApplicationSettingBoolean != nullptr) {
    BOOL ret;
    HRESULT hr = per_process::WldpGetApplicationSettingBoolean(
        NODEJS, ENFORCE_CODE_INTEGRITY_SETTING_NAME, &ret);

    if (SUCCEEDED(hr)) {
      args.GetReturnValue().Set(Boolean::New(env->isolate(), ret));
      return;
    } else if (hr != E_NOTFOUND) {
      // If the setting is not found, continue through to attempt
      // WldpQuerySecurityPolicy, as the setting may be defined
      // in the old settings format
      args.GetReturnValue().Set(Boolean::New(env->isolate(), false));
      return;
    }
  }

  // WldpGetApplicationSettingBoolean is the preferred way for applications to
  // query security policy values. However, this method only exists on Windows
  // versions going back to circa Win10 2023H2. In order to support systems
  // older than that (down to Win10RS2), we can use the deprecated
  // WldpQuerySecurityPolicy
  if (per_process::WldpQuerySecurityPolicy != nullptr) {
    DECLARE_CONST_UNICODE_STRING(providerName, L"Node.js");
    DECLARE_CONST_UNICODE_STRING(keyName, L"Settings");
    DECLARE_CONST_UNICODE_STRING(valueName, L"EnforceCodeIntegrity");
    WLDP_SECURE_SETTING_VALUE_TYPE valueType =
        WLDP_SECURE_SETTING_VALUE_TYPE_BOOLEAN;
    ULONG valueSize = sizeof(int);
    int ret = 0;
    HRESULT hr = per_process::WldpQuerySecurityPolicy(
        &providerName, &keyName, &valueName, &valueType, &ret, &valueSize);
    if (FAILED(hr)) {
      args.GetReturnValue().Set(Boolean::New(env->isolate(), false));
      return;
    }

    args.GetReturnValue().Set(
        Boolean::New(env->isolate(), static_cast<bool>(ret)));
  }
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  SetMethod(context,
            target,
            "isFileTrustedBySystemCodeIntegrityPolicy",
            IsFileTrustedBySystemCodeIntegrityPolicy);

  SetMethod(context,
            target,
            "isInteractiveModeDisabledInternal",
            IsInteractiveModeDisabledInternal);

  SetMethod(context,
            target,
            "isSystemEnforcingCodeIntegrity",
            IsSystemEnforcingCodeIntegrity);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(IsFileTrustedBySystemCodeIntegrityPolicy);
  registry->Register(IsInteractiveModeDisabledInternal);
  registry->Register(IsSystemEnforcingCodeIntegrity);
}

}  // namespace code_integrity
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(code_integrity,
                                    node::code_integrity::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(
    code_integrity, node::code_integrity::RegisterExternalReferences)
