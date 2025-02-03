#include "node_dir.h"
#include "memory_tracker-inl.h"
#include "node_external_reference.h"
#include "node_file-inl.h"
#include "node_process-inl.h"
#include "path.h"
#include "permission/permission.h"
#include "util.h"

#include "tracing/trace_event.h"

#include "string_bytes.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstring>
#include <cerrno>
#include <climits>

#include <memory>

namespace node {

namespace fs_dir {

using fs::FSReqAfterScope;
using fs::FSReqBase;
using fs::FSReqWrapSync;
using fs::GetReqWrap;

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Value;

static const char* get_dir_func_name_by_type(uv_fs_type req_type) {
  switch (req_type) {
#define FS_TYPE_TO_NAME(type, name)                                            \
  case UV_FS_##type:                                                           \
    return name;
    FS_TYPE_TO_NAME(OPENDIR, "opendir")
    FS_TYPE_TO_NAME(READDIR, "readdir")
    FS_TYPE_TO_NAME(CLOSEDIR, "closedir")
#undef FS_TYPE_TO_NAME
    default:
      return "unknown";
  }
}

#define TRACE_NAME(name) "fs_dir.sync." #name
#define GET_TRACE_ENABLED                                                      \
  (*TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(                                \
       TRACING_CATEGORY_NODE2(fs_dir, sync)) != 0)
#define FS_DIR_SYNC_TRACE_BEGIN(syscall, ...)                                  \
  if (GET_TRACE_ENABLED)                                                       \
    TRACE_EVENT_BEGIN(TRACING_CATEGORY_NODE2(fs_dir, sync),                    \
                      TRACE_NAME(syscall),                                     \
                      ##__VA_ARGS__);
#define FS_DIR_SYNC_TRACE_END(syscall, ...)                                    \
  if (GET_TRACE_ENABLED)                                                       \
    TRACE_EVENT_END(TRACING_CATEGORY_NODE2(fs_dir, sync),                      \
                    TRACE_NAME(syscall),                                       \
                    ##__VA_ARGS__);

#define FS_DIR_ASYNC_TRACE_BEGIN0(fs_type, id)                                 \
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(TRACING_CATEGORY_NODE2(fs_dir, async),     \
                                    get_dir_func_name_by_type(fs_type),        \
                                    id);
#define FS_DIR_ASYNC_TRACE_END0(fs_type, id)                                   \
  TRACE_EVENT_NESTABLE_ASYNC_END0(TRACING_CATEGORY_NODE2(fs_dir, async),       \
                                  get_dir_func_name_by_type(fs_type),          \
                                  id);

#define FS_DIR_ASYNC_TRACE_BEGIN1(fs_type, id, name, value)                    \
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(TRACING_CATEGORY_NODE2(fs_dir, async),     \
                                    get_dir_func_name_by_type(fs_type),        \
                                    id,                                        \
                                    name,                                      \
                                    value);

#define FS_DIR_ASYNC_TRACE_END1(fs_type, id, name, value)                      \
  TRACE_EVENT_NESTABLE_ASYNC_END1(TRACING_CATEGORY_NODE2(fs_dir, async),       \
                                  get_dir_func_name_by_type(fs_type),          \
                                  id,                                          \
                                  name,                                        \
                                  value);

DirHandle::DirHandle(Environment* env, Local<Object> obj, uv_dir_t* dir)
    : AsyncWrap(env, obj, AsyncWrap::PROVIDER_DIRHANDLE),
      dir_(dir) {
  MakeWeak();

  dir_->nentries = 0;
  dir_->dirents = nullptr;
}

DirHandle* DirHandle::New(Environment* env, uv_dir_t* dir) {
  Local<Object> obj;
  if (!env->dir_instance_template()
          ->NewInstance(env->context())
          .ToLocal(&obj)) {
    return nullptr;
  }

  return new DirHandle(env, obj, dir);
}

void DirHandle::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
}

DirHandle::~DirHandle() {
  CHECK(!closing_);  // We should not be deleting while explicitly closing!
  GCClose();         // Close synchronously and emit warning
  CHECK(closed_);    // We have to be closed at the point
}

void DirHandle::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("dir", sizeof(*dir_));
}

// Close the directory handle if it hasn't already been closed. A process
// warning will be emitted using a SetImmediate to avoid calling back to
// JS during GC. If closing the fd fails at this point, a fatal exception
// will crash the process immediately.
inline void DirHandle::GCClose() {
  if (closed_) return;
  uv_fs_t req;
  FS_DIR_SYNC_TRACE_BEGIN(closedir);
  int ret = uv_fs_closedir(nullptr, &req, dir_, nullptr);
  FS_DIR_SYNC_TRACE_END(closedir);
  uv_fs_req_cleanup(&req);
  closing_ = false;
  closed_ = true;

  struct err_detail { int ret; };

  err_detail detail { ret };

  if (ret < 0) {
    // Do not unref this
    env()->SetImmediate([detail](Environment* env) {
      const char* msg = "Closing directory handle on garbage collection failed";
      // This exception will end up being fatal for the process because
      // it is being thrown from within the SetImmediate handler and
      // there is no JS stack to bubble it to. In other words, tearing
      // down the process is the only reasonable thing we can do here.
      HandleScope handle_scope(env->isolate());
      env->ThrowUVException(detail.ret, "close", msg);
    });
    return;
  }

  // If the close was successful, we still want to emit a process warning
  // to notify that the file descriptor was gc'd. We want to be noisy about
  // this because not explicitly closing the DirHandle is a bug.

  env()->SetImmediate([](Environment* env) {
    ProcessEmitWarning(env,
                       "Closing directory handle on garbage collection");
  }, CallbackFlags::kUnrefed);
}

void AfterClose(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_DIR_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed())
    req_wrap->Resolve(Undefined(req_wrap->env()->isolate()));
}

void DirHandle::Close(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_GE(args.Length(), 0);  // [req]

  DirHandle* dir;
  ASSIGN_OR_RETURN_UNWRAP(&dir, args.This());

  dir->closing_ = false;
  dir->closed_ = true;

  if (!args[0]->IsUndefined()) {  // close(req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 0);
    CHECK_NOT_NULL(req_wrap_async);
    FS_DIR_ASYNC_TRACE_BEGIN0(UV_FS_CLOSEDIR, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "closedir", UTF8, AfterClose,
              uv_fs_closedir, dir->dir());
  } else {  // close()
    FSReqWrapSync req_wrap_sync("closedir");
    FS_DIR_SYNC_TRACE_BEGIN(closedir);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_closedir, dir->dir());
    FS_DIR_SYNC_TRACE_END(closedir);
  }
}

static MaybeLocal<Array> DirentListToArray(
    Environment* env,
    uv_dirent_t* ents,
    int num,
    enum encoding encoding,
    Local<Value>* err_out) {
  MaybeStackBuffer<Local<Value>, 64> entries(num * 2);

  // Return an array of all read filenames.
  int j = 0;
  for (int i = 0; i < num; i++) {
    Local<Value> filename;
    Local<Value> error;
    const size_t namelen = strlen(ents[i].name);
    if (!StringBytes::Encode(env->isolate(),
                             ents[i].name,
                             namelen,
                             encoding,
                             &error).ToLocal(&filename)) {
      *err_out = error;
      return MaybeLocal<Array>();
    }

    entries[j++] = filename;
    entries[j++] = Integer::New(env->isolate(), ents[i].type);
  }

  return Array::New(env->isolate(), entries.out(), j);
}

static void AfterDirRead(uv_fs_t* req) {
  BaseObjectPtr<FSReqBase> req_wrap { FSReqBase::from_req(req) };
  FSReqAfterScope after(req_wrap.get(), req);
  FS_DIR_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (!after.Proceed()) {
    return;
  }

  Environment* env = req_wrap->env();
  Isolate* isolate = env->isolate();

  if (req->result == 0) {
    // Done
    Local<Value> done = Null(isolate);
    after.Clear();
    req_wrap->Resolve(done);
    return;
  }

  uv_dir_t* dir = static_cast<uv_dir_t*>(req->ptr);

  Local<Value> error;
  Local<Array> js_array;
  if (!DirentListToArray(env,
                         dir->dirents,
                         static_cast<int>(req->result),
                         req_wrap->encoding(),
                         &error)
           .ToLocal(&js_array)) {
    // Clear libuv resources *before* delivering results to JS land because
    // that can schedule another operation on the same uv_dir_t. Ditto below.
    after.Clear();
    return req_wrap->Reject(error);
  }

  after.Clear();
  req_wrap->Resolve(js_array);
}


void DirHandle::Read(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_GE(args.Length(), 2);  // encoding, bufferSize, [callback]

  const enum encoding encoding = ParseEncoding(isolate, args[0], UTF8);

  DirHandle* dir;
  ASSIGN_OR_RETURN_UNWRAP(&dir, args.This());

  CHECK(args[1]->IsNumber());
  uint64_t buffer_size = static_cast<uint64_t>(args[1].As<Number>()->Value());

  if (buffer_size != dir->dirents_.size()) {
    dir->dirents_.resize(buffer_size);
    dir->dir_->nentries = buffer_size;
    dir->dir_->dirents = dir->dirents_.data();
  }

  if (!args[2]->IsUndefined()) {  // dir.read(encoding, bufferSize, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    CHECK_NOT_NULL(req_wrap_async);
    FS_DIR_ASYNC_TRACE_BEGIN0(UV_FS_READDIR, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "readdir", encoding,
              AfterDirRead, uv_fs_readdir, dir->dir());
  } else {  // dir.read(encoding, bufferSize)
    FSReqWrapSync req_wrap_sync("readdir");
    FS_DIR_SYNC_TRACE_BEGIN(readdir);
    int err =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_readdir, dir->dir());
    FS_DIR_SYNC_TRACE_END(readdir);
    if (err < 0) {
      return;  // syscall failed, no need to continue, error is already thrown
    }

    if (req_wrap_sync.req.result == 0) {
      // Done
      return args.GetReturnValue().SetNull();
    }

    CHECK_GE(req_wrap_sync.req.result, 0);

    Local<Value> error;
    Local<Array> js_array;
    if (!DirentListToArray(env,
                           dir->dir()->dirents,
                           static_cast<int>(req_wrap_sync.req.result),
                           encoding,
                           &error)
             .ToLocal(&js_array)) {
      // TODO(anonrig): Initializing BufferValue here is wasteful.
      BufferValue error_payload(isolate, error);
      env->ThrowError(error_payload.out());
      return;
    }

    args.GetReturnValue().Set(js_array);
  }
}

void AfterOpenDir(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_DIR_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (!after.Proceed()) {
    return;
  }

  Environment* env = req_wrap->env();

  uv_dir_t* dir = static_cast<uv_dir_t*>(req->ptr);
  DirHandle* handle = DirHandle::New(env, dir);

  req_wrap->Resolve(handle->object().As<Value>());
}

static void OpenDir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_GE(args.Length(), 2);  // path, encoding, [callback]

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  const enum encoding encoding = ParseEncoding(isolate, args[1], UTF8);

  if (!args[2]->IsUndefined()) {  // openDir(path, encoding, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    CHECK_NOT_NULL(req_wrap_async);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        path.ToStringView());
    FS_DIR_ASYNC_TRACE_BEGIN1(
        UV_FS_OPENDIR, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "opendir", encoding, AfterOpenDir,
              uv_fs_opendir, *path);
  } else {  // openDir(path, encoding)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
    FSReqWrapSync req_wrap_sync("opendir", *path);
    FS_DIR_SYNC_TRACE_BEGIN(opendir);
    int result =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_opendir, *path);
    FS_DIR_SYNC_TRACE_END(opendir);
    if (result < 0) {
      return;  // syscall failed, no need to continue, error is already thrown
    }

    uv_fs_t* req = &req_wrap_sync.req;
    uv_dir_t* dir = static_cast<uv_dir_t*>(req->ptr);
    DirHandle* handle = DirHandle::New(env, dir);

    args.GetReturnValue().Set(handle->object().As<Value>());
  }
}

static void OpenDirSync(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_GE(args.Length(), 1);

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, path.ToStringView());

  uv_fs_t req;
  auto make = OnScopeLeave([&req]() { uv_fs_req_cleanup(&req); });
  FS_DIR_SYNC_TRACE_BEGIN(opendir);
  int err = uv_fs_opendir(nullptr, &req, *path, nullptr);
  FS_DIR_SYNC_TRACE_END(opendir);
  if (err < 0) {
    return env->ThrowUVException(err, "opendir");
  }

  uv_dir_t* dir = static_cast<uv_dir_t*>(req.ptr);
  DirHandle* handle = DirHandle::New(env, dir);

  args.GetReturnValue().Set(handle->object().As<Value>());
}

void CreatePerIsolateProperties(IsolateData* isolate_data,
                                Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(isolate, target, "opendir", OpenDir);
  SetMethod(isolate, target, "opendirSync", OpenDirSync);

  // Create FunctionTemplate for DirHandle
  Local<FunctionTemplate> dir = NewFunctionTemplate(isolate, DirHandle::New);
  dir->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  SetProtoMethod(isolate, dir, "read", DirHandle::Read);
  SetProtoMethod(isolate, dir, "close", DirHandle::Close);
  Local<ObjectTemplate> dirt = dir->InstanceTemplate();
  dirt->SetInternalFieldCount(DirHandle::kInternalFieldCount);
  SetConstructorFunction(isolate, target, "DirHandle", dir);
  isolate_data->set_dir_instance_template(dirt);
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(OpenDir);
  registry->Register(OpenDirSync);
  registry->Register(DirHandle::New);
  registry->Register(DirHandle::Read);
  registry->Register(DirHandle::Close);
}

}  // namespace fs_dir

}  // end namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(fs_dir,
                                    node::fs_dir::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(fs_dir, node::fs_dir::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(fs_dir,
                                node::fs_dir::RegisterExternalReferences)
