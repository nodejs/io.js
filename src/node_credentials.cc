#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "util-inl.h"

#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS
#include <grp.h>  // getgrnam()
#include <pwd.h>  // getpwnam()
#endif            // NODE_IMPLEMENTS_POSIX_CREDENTIALS

#if !defined(_MSC_VER)
#include <unistd.h>  // setuid, getuid
#endif
#ifdef __linux__
#include <dlfcn.h>  // dlsym()
#include <linux/capability.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#endif  // __linux__

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Uint32;
using v8::Value;

bool linux_at_secure() {
  // This could reasonably be a static variable, but this way
  // we can guarantee that this function is always usable
  // and returns the correct value,  e.g. even in static
  // initialization code in other files.
#ifdef __linux__
  static const bool value = getauxval(AT_SECURE);
  return value;
#else
  return false;
#endif
}

namespace credentials {

#if defined(__linux__)
// Returns true if the current process only has the passed-in capability.
static bool HasOnly(int capability) {
  DCHECK(cap_valid(capability));

  struct __user_cap_data_struct cap_data[_LINUX_CAPABILITY_U32S_3];
  struct __user_cap_header_struct cap_header_data = {
    _LINUX_CAPABILITY_VERSION_3,
    getpid()};


  if (syscall(SYS_capget, &cap_header_data, &cap_data) != 0) {
    return false;
  }

  static_assert(arraysize(cap_data) == 2);
  return cap_data[CAP_TO_INDEX(capability)].permitted ==
             static_cast<unsigned int>(CAP_TO_MASK(capability)) &&
         cap_data[1 - CAP_TO_INDEX(capability)].permitted == 0;
}
#endif

// Look up the environment variable and allow the lookup if the current
// process only has the capability CAP_NET_BIND_SERVICE set. If the current
// process does not have any capabilities set and the process is running as
// setuid root then lookup will not be allowed.
bool SafeGetenv(const char* key, std::string* text, Environment* env) {
#if !defined(__CloudABI__) && !defined(_WIN32)
#if defined(__linux__)
  if ((!HasOnly(CAP_NET_BIND_SERVICE) && linux_at_secure()) ||
      getuid() != geteuid() || getgid() != getegid())
#else
  if (linux_at_secure() || getuid() != geteuid() || getgid() != getegid())
#endif
    return false;
#endif

  // Fallback to system environment which reads the real environment variable
  // through uv_os_getenv.
  std::shared_ptr<KVStore> env_vars;
  if (env == nullptr) {
    env_vars = per_process::system_environment;
  } else {
    env_vars = env->env_vars();
  }

  std::optional<std::string> value = env_vars->Get(key);

  bool has_env = value.has_value();
  if (has_env) {
    *text = value.value();
  }

  TraceEnvVar(env, "get", key);

  return has_env;
}

static void SafeGetenv(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  Utf8Value strenvtag(isolate, args[0]);
  std::string text;
  if (!SafeGetenv(*strenvtag, &text, env)) return;
  Local<Value> result;
  if (ToV8Value(isolate->GetCurrentContext(), text).ToLocal(&result)) {
    args.GetReturnValue().Set(result);
  }
}

static void GetTempDir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  std::string dir;

  // Let's wrap SafeGetEnv since it returns true for empty string.
  auto get_env = [&dir, &env](std::string_view key) {
    USE(SafeGetenv(key.data(), &dir, env));
    return !dir.empty();
  };

  // Try TMPDIR, TMP, and TEMP in that order.
  if (!get_env("TMPDIR") && !get_env("TMP") && !get_env("TEMP")) {
    return;
  }

  if (dir.size() > 1 && dir.ends_with("/")) {
    dir.pop_back();
  }

  Local<Value> result;
  if (ToV8Value(isolate->GetCurrentContext(), dir).ToLocal(&result)) {
    args.GetReturnValue().Set(result);
  }
}

#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS

static const uid_t uid_not_found = static_cast<uid_t>(-1);
static const gid_t gid_not_found = static_cast<gid_t>(-1);

static uid_t uid_by_name(const char* name) {
  struct passwd pwd;
  struct passwd* pp;
  char buf[8192];

  errno = 0;
  pp = nullptr;

  if (getpwnam_r(name, &pwd, buf, sizeof(buf), &pp) == 0 && pp != nullptr)
    return pp->pw_uid;

  return uid_not_found;
}

static char* name_by_uid(uid_t uid) {
  struct passwd pwd;
  struct passwd* pp;
  char buf[8192];
  int rc;

  errno = 0;
  pp = nullptr;

  if ((rc = getpwuid_r(uid, &pwd, buf, sizeof(buf), &pp)) == 0 &&
      pp != nullptr) {
    return strdup(pp->pw_name);
  }

  if (rc == 0) errno = ENOENT;

  return nullptr;
}

static gid_t gid_by_name(const char* name) {
  struct group pwd;
  struct group* pp;
  char buf[8192];

  errno = 0;
  pp = nullptr;

  if (getgrnam_r(name, &pwd, buf, sizeof(buf), &pp) == 0 && pp != nullptr)
    return pp->gr_gid;

  return gid_not_found;
}

#if 0  // For future use.
static const char* name_by_gid(gid_t gid) {
  struct group pwd;
  struct group* pp;
  char buf[8192];
  int rc;

  errno = 0;
  pp = nullptr;

  if ((rc = getgrgid_r(gid, &pwd, buf, sizeof(buf), &pp)) == 0 &&
      pp != nullptr) {
    return strdup(pp->gr_name);
  }

  if (rc == 0)
    errno = ENOENT;

  return nullptr;
}
#endif

static uid_t uid_by_name(Isolate* isolate, Local<Value> value) {
  if (value->IsUint32()) {
    static_assert(std::is_same<uid_t, uint32_t>::value);
    return value.As<Uint32>()->Value();
  } else {
    Utf8Value name(isolate, value);
    return uid_by_name(*name);
  }
}

static gid_t gid_by_name(Isolate* isolate, Local<Value> value) {
  if (value->IsUint32()) {
    static_assert(std::is_same<gid_t, uint32_t>::value);
    return value.As<Uint32>()->Value();
  } else {
    Utf8Value name(isolate, value);
    return gid_by_name(*name);
  }
}

static bool UvMightBeUsingIoUring() {
#ifdef __linux__
  // Support for io_uring is only included in libuv 1.45.0 and later. Starting
  // with 1.49.0 is disabled by default. Check the version in case Node.js is
  // dynamically to an io_uring-enabled version of libuv.
  unsigned int version = uv_version();
  return version >= 0x012d00u && version < 0x013100u;
#else
  return false;
#endif
}

static bool ThrowIfUvMightBeUsingIoUring(Environment* env, const char* fn) {
  if (UvMightBeUsingIoUring()) {
    node::THROW_ERR_INVALID_STATE(
        env, "%s() disabled: io_uring may be enabled. See CVE-2024-22017.", fn);
    return true;
  }
  return false;
}

static void GetUid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  // uid_t is an uint32_t on all supported platforms.
  args.GetReturnValue().Set(static_cast<uint32_t>(getuid()));
}

static void GetGid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  // gid_t is an uint32_t on all supported platforms.
  args.GetReturnValue().Set(static_cast<uint32_t>(getgid()));
}

static void GetEUid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  // uid_t is an uint32_t on all supported platforms.
  args.GetReturnValue().Set(static_cast<uint32_t>(geteuid()));
}

static void GetEGid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());
  // gid_t is an uint32_t on all supported platforms.
  args.GetReturnValue().Set(static_cast<uint32_t>(getegid()));
}

static void SetGid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->owns_process_state());

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32() || args[0]->IsString());

  if (ThrowIfUvMightBeUsingIoUring(env, "setgid")) return;

  gid_t gid = gid_by_name(env->isolate(), args[0]);

  if (gid == gid_not_found) {
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    args.GetReturnValue().Set(1);
  } else if (setgid(gid)) {
    env->ThrowErrnoException(errno, "setgid");
  } else {
    args.GetReturnValue().Set(0);
  }
}

static void SetEGid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->owns_process_state());

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32() || args[0]->IsString());

  if (ThrowIfUvMightBeUsingIoUring(env, "setegid")) return;

  gid_t gid = gid_by_name(env->isolate(), args[0]);

  if (gid == gid_not_found) {
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    args.GetReturnValue().Set(1);
  } else if (setegid(gid)) {
    env->ThrowErrnoException(errno, "setegid");
  } else {
    args.GetReturnValue().Set(0);
  }
}

static void SetUid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->owns_process_state());

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32() || args[0]->IsString());

  if (ThrowIfUvMightBeUsingIoUring(env, "setuid")) return;

  uid_t uid = uid_by_name(env->isolate(), args[0]);

  if (uid == uid_not_found) {
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    args.GetReturnValue().Set(1);
  } else if (setuid(uid)) {
    env->ThrowErrnoException(errno, "setuid");
  } else {
    args.GetReturnValue().Set(0);
  }
}

static void SetEUid(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->owns_process_state());

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32() || args[0]->IsString());

  if (ThrowIfUvMightBeUsingIoUring(env, "seteuid")) return;

  uid_t uid = uid_by_name(env->isolate(), args[0]);

  if (uid == uid_not_found) {
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    args.GetReturnValue().Set(1);
  } else if (seteuid(uid)) {
    env->ThrowErrnoException(errno, "seteuid");
  } else {
    args.GetReturnValue().Set(0);
  }
}

static void GetGroups(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(env->has_run_bootstrapping_code());

  int ngroups = getgroups(0, nullptr);
  if (ngroups == -1) return env->ThrowErrnoException(errno, "getgroups");

  std::vector<gid_t> groups(ngroups);

  ngroups = getgroups(ngroups, groups.data());
  if (ngroups == -1)
    return env->ThrowErrnoException(errno, "getgroups");

  groups.resize(ngroups);
  gid_t egid = getegid();
  if (std::find(groups.begin(), groups.end(), egid) == groups.end())
    groups.push_back(egid);
  Local<Value> result;
  if (ToV8Value(env->context(), groups).ToLocal(&result)) {
    args.GetReturnValue().Set(result);
  }
}

static void SetGroups(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsArray());

  if (ThrowIfUvMightBeUsingIoUring(env, "setgroups")) return;

  Local<Array> groups_list = args[0].As<Array>();
  size_t size = groups_list->Length();
  MaybeStackBuffer<gid_t, 64> groups(size);

  for (size_t i = 0; i < size; i++) {
    Local<Value> val;
    if (!groups_list->Get(env->context(), i).ToLocal(&val)) {
      // V8 will have scheduled an error to be thrown.
      return;
    }
    gid_t gid = gid_by_name(env->isolate(), val);

    if (gid == gid_not_found) {
      // Tells JS to throw ERR_INVALID_CREDENTIAL
      args.GetReturnValue().Set(static_cast<uint32_t>(i + 1));
      return;
    }

    groups[i] = gid;
  }

  int rc = setgroups(size, *groups);

  if (rc == -1) return env->ThrowErrnoException(errno, "setgroups");

  args.GetReturnValue().Set(0);
}

static void InitGroups(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsUint32() || args[0]->IsString());
  CHECK(args[1]->IsUint32() || args[1]->IsString());

  if (ThrowIfUvMightBeUsingIoUring(env, "initgroups")) return;

  Utf8Value arg0(env->isolate(), args[0]);
  gid_t extra_group;
  bool must_free;
  char* user;

  if (args[0]->IsUint32()) {
    user = name_by_uid(args[0].As<Uint32>()->Value());
    must_free = true;
  } else {
    user = *arg0;
    must_free = false;
  }

  if (user == nullptr) {
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    return args.GetReturnValue().Set(1);
  }

  extra_group = gid_by_name(env->isolate(), args[1]);

  if (extra_group == gid_not_found) {
    if (must_free) free(user);
    // Tells JS to throw ERR_INVALID_CREDENTIAL
    return args.GetReturnValue().Set(2);
  }

  int rc = initgroups(user, extra_group);

  if (must_free) free(user);

  if (rc) return env->ThrowErrnoException(errno, "initgroups");

  args.GetReturnValue().Set(0);
}

#endif  // NODE_IMPLEMENTS_POSIX_CREDENTIALS

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(SafeGetenv);
  registry->Register(GetTempDir);

#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS
  registry->Register(GetUid);
  registry->Register(GetEUid);
  registry->Register(GetGid);
  registry->Register(GetEGid);
  registry->Register(GetGroups);

  registry->Register(InitGroups);
  registry->Register(SetEGid);
  registry->Register(SetEUid);
  registry->Register(SetGid);
  registry->Register(SetUid);
  registry->Register(SetGroups);
#endif  // NODE_IMPLEMENTS_POSIX_CREDENTIALS
}

static void Initialize(Local<Object> target,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  SetMethod(context, target, "safeGetenv", SafeGetenv);
  SetMethod(context, target, "getTempDir", GetTempDir);

#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  READONLY_TRUE_PROPERTY(target, "implementsPosixCredentials");
  SetMethodNoSideEffect(context, target, "getuid", GetUid);
  SetMethodNoSideEffect(context, target, "geteuid", GetEUid);
  SetMethodNoSideEffect(context, target, "getgid", GetGid);
  SetMethodNoSideEffect(context, target, "getegid", GetEGid);
  SetMethodNoSideEffect(context, target, "getgroups", GetGroups);

  if (env->owns_process_state()) {
    SetMethod(context, target, "initgroups", InitGroups);
    SetMethod(context, target, "setegid", SetEGid);
    SetMethod(context, target, "seteuid", SetEUid);
    SetMethod(context, target, "setgid", SetGid);
    SetMethod(context, target, "setuid", SetUid);
    SetMethod(context, target, "setgroups", SetGroups);
  }
#endif  // NODE_IMPLEMENTS_POSIX_CREDENTIALS
}

}  // namespace credentials
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(credentials, node::credentials::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(credentials,
                                node::credentials::RegisterExternalReferences)
