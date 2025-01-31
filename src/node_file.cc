// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.
#include "node_file.h"  // NOLINT(build/include_inline)
#include "ada.h"
#include "aliased_buffer-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_file-inl.h"
#include "node_metadata.h"
#include "node_process-inl.h"
#include "node_stat_watcher.h"
#include "node_url.h"
#include "path.h"
#include "permission/permission.h"
#include "util-inl.h"

#include "tracing/trace_event.h"

#include "req_wrap-inl.h"
#include "stream_base-inl.h"
#include "string_bytes.h"
#include "uv.h"
#include "v8-fast-api-calls.h"

#include <filesystem>

#if defined(__MINGW32__) || defined(_MSC_VER)
# include <io.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace node {

namespace fs {

using v8::Array;
using v8::BigInt;
using v8::Context;
using v8::EscapableHandleScope;
using v8::FastApiCallbackOptions;
using v8::FastOneByteString;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Promise;
using v8::String;
using v8::Undefined;
using v8::Value;

#ifndef S_ISDIR
# define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifdef __POSIX__
constexpr char kPathSeparator = '/';
#else
const char* const kPathSeparator = "\\/";
#endif

inline int64_t GetOffset(Local<Value> value) {
  return IsSafeJsInt(value) ? value.As<Integer>()->Value() : -1;
}

static const char* get_fs_func_name_by_type(uv_fs_type req_type) {
  switch (req_type) {
#define FS_TYPE_TO_NAME(type, name)                                            \
  case UV_FS_##type:                                                           \
    return name;
    FS_TYPE_TO_NAME(OPEN, "open")
    FS_TYPE_TO_NAME(CLOSE, "close")
    FS_TYPE_TO_NAME(READ, "read")
    FS_TYPE_TO_NAME(WRITE, "write")
    FS_TYPE_TO_NAME(SENDFILE, "sendfile")
    FS_TYPE_TO_NAME(STAT, "stat")
    FS_TYPE_TO_NAME(LSTAT, "lstat")
    FS_TYPE_TO_NAME(FSTAT, "fstat")
    FS_TYPE_TO_NAME(FTRUNCATE, "ftruncate")
    FS_TYPE_TO_NAME(UTIME, "utime")
    FS_TYPE_TO_NAME(FUTIME, "futime")
    FS_TYPE_TO_NAME(ACCESS, "access")
    FS_TYPE_TO_NAME(CHMOD, "chmod")
    FS_TYPE_TO_NAME(FCHMOD, "fchmod")
    FS_TYPE_TO_NAME(FSYNC, "fsync")
    FS_TYPE_TO_NAME(FDATASYNC, "fdatasync")
    FS_TYPE_TO_NAME(UNLINK, "unlink")
    FS_TYPE_TO_NAME(RMDIR, "rmdir")
    FS_TYPE_TO_NAME(MKDIR, "mkdir")
    FS_TYPE_TO_NAME(MKDTEMP, "mkdtemp")
    FS_TYPE_TO_NAME(RENAME, "rename")
    FS_TYPE_TO_NAME(SCANDIR, "scandir")
    FS_TYPE_TO_NAME(LINK, "link")
    FS_TYPE_TO_NAME(SYMLINK, "symlink")
    FS_TYPE_TO_NAME(READLINK, "readlink")
    FS_TYPE_TO_NAME(CHOWN, "chown")
    FS_TYPE_TO_NAME(FCHOWN, "fchown")
    FS_TYPE_TO_NAME(REALPATH, "realpath")
    FS_TYPE_TO_NAME(COPYFILE, "copyfile")
    FS_TYPE_TO_NAME(LCHOWN, "lchown")
    FS_TYPE_TO_NAME(STATFS, "statfs")
    FS_TYPE_TO_NAME(MKSTEMP, "mkstemp")
    FS_TYPE_TO_NAME(LUTIME, "lutime")
#undef FS_TYPE_TO_NAME
    default:
      return "unknown";
  }
}

#define TRACE_NAME(name) "fs.sync." #name
#define GET_TRACE_ENABLED                                                      \
  (*TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(                                \
       TRACING_CATEGORY_NODE2(fs, sync)) != 0)
#define FS_SYNC_TRACE_BEGIN(syscall, ...)                                      \
  if (GET_TRACE_ENABLED)                                                       \
    TRACE_EVENT_BEGIN(                                                         \
        TRACING_CATEGORY_NODE2(fs, sync), TRACE_NAME(syscall), ##__VA_ARGS__);
#define FS_SYNC_TRACE_END(syscall, ...)                                        \
  if (GET_TRACE_ENABLED)                                                       \
    TRACE_EVENT_END(                                                           \
        TRACING_CATEGORY_NODE2(fs, sync), TRACE_NAME(syscall), ##__VA_ARGS__);

#define FS_ASYNC_TRACE_BEGIN0(fs_type, id)                                     \
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(TRACING_CATEGORY_NODE2(fs, async),         \
                                    get_fs_func_name_by_type(fs_type),         \
                                    id);

#define FS_ASYNC_TRACE_END0(fs_type, id)                                       \
  TRACE_EVENT_NESTABLE_ASYNC_END0(TRACING_CATEGORY_NODE2(fs, async),           \
                                  get_fs_func_name_by_type(fs_type),           \
                                  id);

#define FS_ASYNC_TRACE_BEGIN1(fs_type, id, name, value)                        \
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(TRACING_CATEGORY_NODE2(fs, async),         \
                                    get_fs_func_name_by_type(fs_type),         \
                                    id,                                        \
                                    name,                                      \
                                    value);

#define FS_ASYNC_TRACE_END1(fs_type, id, name, value)                          \
  TRACE_EVENT_NESTABLE_ASYNC_END1(TRACING_CATEGORY_NODE2(fs, async),           \
                                  get_fs_func_name_by_type(fs_type),           \
                                  id,                                          \
                                  name,                                        \
                                  value);

#define FS_ASYNC_TRACE_BEGIN2(fs_type, id, name1, value1, name2, value2)       \
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(TRACING_CATEGORY_NODE2(fs, async),         \
                                    get_fs_func_name_by_type(fs_type),         \
                                    id,                                        \
                                    name1,                                     \
                                    value1,                                    \
                                    name2,                                     \
                                    value2);

#define FS_ASYNC_TRACE_END2(fs_type, id, name1, value1, name2, value2)         \
  TRACE_EVENT_NESTABLE_ASYNC_END2(TRACING_CATEGORY_NODE2(fs, async),           \
                                  get_fs_func_name_by_type(fs_type),           \
                                  id,                                          \
                                  name1,                                       \
                                  value1,                                      \
                                  name2,                                       \
                                  value2);

// We sometimes need to convert a C++ lambda function to a raw C-style function.
// This is helpful, because ReqWrap::Dispatch() does not recognize lambda
// functions, and thus does not wrap them properly.
typedef void(*uv_fs_callback_t)(uv_fs_t*);


void FSContinuationData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("paths", paths_);
}

FileHandleReadWrap::~FileHandleReadWrap() = default;

FSReqBase::~FSReqBase() = default;

void FSReqBase::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("continuation_data", continuation_data_);
}

// The FileHandle object wraps a file descriptor and will close it on garbage
// collection if necessary. If that happens, a process warning will be
// emitted (or a fatal exception will occur if the fd cannot be closed.)
FileHandle::FileHandle(BindingData* binding_data,
                       Local<Object> obj, int fd)
    : AsyncWrap(binding_data->env(), obj, AsyncWrap::PROVIDER_FILEHANDLE),
      StreamBase(env()),
      fd_(fd),
      binding_data_(binding_data) {
  MakeWeak();
  StreamBase::AttachToObject(GetObject());
}

FileHandle* FileHandle::New(BindingData* binding_data,
                            int fd,
                            Local<Object> obj,
                            std::optional<int64_t> maybeOffset,
                            std::optional<int64_t> maybeLength) {
  Environment* env = binding_data->env();
  if (obj.IsEmpty() && !env->fd_constructor_template()
                            ->NewInstance(env->context())
                            .ToLocal(&obj)) {
    return nullptr;
  }
  auto handle = new FileHandle(binding_data, obj, fd);
  if (maybeOffset.has_value()) handle->read_offset_ = maybeOffset.value();
  if (maybeLength.has_value()) handle->read_length_ = maybeLength.value();
  return handle;
}

void FileHandle::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  CHECK(args[0]->IsInt32());
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();

  std::optional<int64_t> maybeOffset = std::nullopt;
  std::optional<int64_t> maybeLength = std::nullopt;
  if (args[1]->IsNumber())
    maybeOffset = args[1]->IntegerValue(realm->context()).FromJust();
  if (args[2]->IsNumber())
    maybeLength = args[2]->IntegerValue(realm->context()).FromJust();

  FileHandle::New(binding_data,
                  args[0].As<Int32>()->Value(),
                  args.This(),
                  maybeOffset,
                  maybeLength);
}

FileHandle::~FileHandle() {
  CHECK(!closing_);  // We should not be deleting while explicitly closing!
  Close();           // Close synchronously and emit warning
  CHECK(closed_);    // We have to be closed at the point
}

int FileHandle::DoWrite(WriteWrap* w,
                        uv_buf_t* bufs,
                        size_t count,
                        uv_stream_t* send_handle) {
  return UV_ENOSYS;  // Not implemented (yet).
}

void FileHandle::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("current_read", current_read_);
}

BaseObject::TransferMode FileHandle::GetTransferMode() const {
  return reading_ || closing_ || closed_
             ? TransferMode::kDisallowCloneAndTransfer
             : TransferMode::kTransferable;
}

std::unique_ptr<worker::TransferData> FileHandle::TransferForMessaging() {
  CHECK_NE(GetTransferMode(), TransferMode::kDisallowCloneAndTransfer);
  auto ret = std::make_unique<TransferData>(fd_);
  closed_ = true;
  return ret;
}

FileHandle::TransferData::TransferData(int fd) : fd_(fd) {}

FileHandle::TransferData::~TransferData() {
  if (fd_ > 0) {
    uv_fs_t close_req;
    CHECK_NE(fd_, -1);
    FS_SYNC_TRACE_BEGIN(close);
    CHECK_EQ(0, uv_fs_close(nullptr, &close_req, fd_, nullptr));
    FS_SYNC_TRACE_END(close);
    uv_fs_req_cleanup(&close_req);
  }
}

BaseObjectPtr<BaseObject> FileHandle::TransferData::Deserialize(
    Environment* env,
    v8::Local<v8::Context> context,
    std::unique_ptr<worker::TransferData> self) {
  BindingData* bd = Realm::GetBindingData<BindingData>(context);
  if (bd == nullptr) return {};

  int fd = fd_;
  fd_ = -1;
  return BaseObjectPtr<BaseObject> { FileHandle::New(bd, fd) };
}

// Close the file descriptor if it hasn't already been closed. A process
// warning will be emitted using a SetImmediate to avoid calling back to
// JS during GC. If closing the fd fails at this point, a fatal exception
// will crash the process immediately.
inline void FileHandle::Close() {
  if (closed_ || closing_) return;
  uv_fs_t req;
  CHECK_NE(fd_, -1);
  FS_SYNC_TRACE_BEGIN(close);
  int ret = uv_fs_close(env()->event_loop(), &req, fd_, nullptr);
  FS_SYNC_TRACE_END(close);
  uv_fs_req_cleanup(&req);

  struct err_detail { int ret; int fd; };

  err_detail detail { ret, fd_ };

  AfterClose();

  if (ret < 0) {
    // Do not unref this
    env()->SetImmediate([detail](Environment* env) {
      char msg[70];
      snprintf(msg, arraysize(msg),
              "Closing file descriptor %d on garbage collection failed",
              detail.fd);
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
  // this because not explicitly closing the FileHandle is a bug.

  env()->SetImmediate([detail](Environment* env) {
    ProcessEmitWarning(env,
                       "Closing file descriptor %d on garbage collection",
                       detail.fd);
    if (env->filehandle_close_warning()) {
      env->set_filehandle_close_warning(false);
      USE(ProcessEmitDeprecationWarning(
          env,
          "Closing a FileHandle object on garbage collection is deprecated. "
          "Please close FileHandle objects explicitly using "
          "FileHandle.prototype.close(). In the future, an error will be "
          "thrown if a file descriptor is closed during garbage collection.",
          "DEP0137"));
    }
  }, CallbackFlags::kUnrefed);
}

void FileHandle::CloseReq::Resolve() {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Context::Scope context_scope(env()->context());
  InternalCallbackScope callback_scope(this);
  Local<Promise> promise = promise_.Get(isolate);
  Local<Promise::Resolver> resolver = promise.As<Promise::Resolver>();
  resolver->Resolve(env()->context(), Undefined(isolate)).Check();
}

void FileHandle::CloseReq::Reject(Local<Value> reason) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Context::Scope context_scope(env()->context());
  InternalCallbackScope callback_scope(this);
  Local<Promise> promise = promise_.Get(isolate);
  Local<Promise::Resolver> resolver = promise.As<Promise::Resolver>();
  resolver->Reject(env()->context(), reason).Check();
}

FileHandle* FileHandle::CloseReq::file_handle() {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Value> val = ref_.Get(isolate);
  Local<Object> obj = val.As<Object>();
  return Unwrap<FileHandle>(obj);
}

FileHandle::CloseReq::CloseReq(Environment* env,
                               Local<Object> obj,
                               Local<Promise> promise,
                               Local<Value> ref)
  : ReqWrap(env, obj, AsyncWrap::PROVIDER_FILEHANDLECLOSEREQ) {
  promise_.Reset(env->isolate(), promise);
  ref_.Reset(env->isolate(), ref);
}

FileHandle::CloseReq::~CloseReq() {
  uv_fs_req_cleanup(req());
  promise_.Reset();
  ref_.Reset();
}

void FileHandle::CloseReq::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("promise", promise_);
  tracker->TrackField("ref", ref_);
}



// Closes this FileHandle asynchronously and returns a Promise that will be
// resolved when the callback is invoked, or rejects with a UVException if
// there was a problem closing the fd. This is the preferred mechanism for
// closing the FD object even tho the object will attempt to close
// automatically on gc.
MaybeLocal<Promise> FileHandle::ClosePromise() {
  Isolate* isolate = env()->isolate();
  EscapableHandleScope scope(isolate);
  Local<Context> context = env()->context();

  Local<Value> close_resolver =
      object()->GetInternalField(FileHandle::kClosingPromiseSlot).As<Value>();
  if (close_resolver->IsPromise()) {
    return close_resolver.As<Promise>();
  }

  CHECK(!closed_);
  CHECK(!closing_);
  CHECK(!reading_);

  auto maybe_resolver = Promise::Resolver::New(context);
  CHECK(!maybe_resolver.IsEmpty());
  Local<Promise::Resolver> resolver;
  if (!maybe_resolver.ToLocal(&resolver)) return {};
  Local<Promise> promise = resolver.As<Promise>();

  Local<Object> close_req_obj;
  if (!env()->fdclose_constructor_template()
          ->NewInstance(env()->context()).ToLocal(&close_req_obj)) {
    return MaybeLocal<Promise>();
  }
  closing_ = true;
  object()->SetInternalField(FileHandle::kClosingPromiseSlot, promise);

  CloseReq* req = new CloseReq(env(), close_req_obj, promise, object());
  auto AfterClose = uv_fs_callback_t{[](uv_fs_t* req) {
    CloseReq* req_wrap = CloseReq::from_req(req);
    FS_ASYNC_TRACE_END1(
        req->fs_type, req_wrap, "result", static_cast<int>(req->result))
    BaseObjectPtr<CloseReq> close(req_wrap);
    CHECK(close);
    close->file_handle()->AfterClose();
    if (!close->env()->can_call_into_js()) return;
    Isolate* isolate = close->env()->isolate();
    if (req->result < 0) {
      HandleScope handle_scope(isolate);
      close->Reject(
          UVException(isolate, static_cast<int>(req->result), "close"));
    } else {
      close->Resolve();
    }
  }};
  CHECK_NE(fd_, -1);
  FS_ASYNC_TRACE_BEGIN0(UV_FS_CLOSE, req)
  int ret = req->Dispatch(uv_fs_close, fd_, AfterClose);
  if (ret < 0) {
    req->Reject(UVException(isolate, ret, "close"));
    delete req;
  }

  return scope.Escape(promise);
}

void FileHandle::Close(const FunctionCallbackInfo<Value>& args) {
  FileHandle* fd;
  ASSIGN_OR_RETURN_UNWRAP(&fd, args.This());
  Local<Promise> ret;
  if (!fd->ClosePromise().ToLocal(&ret)) return;
  args.GetReturnValue().Set(ret);
}


void FileHandle::ReleaseFD(const FunctionCallbackInfo<Value>& args) {
  FileHandle* fd;
  ASSIGN_OR_RETURN_UNWRAP(&fd, args.This());
  fd->Release();
}

int FileHandle::Release() {
  int fd = GetFD();
  // Just pretend that Close was called and we're all done.
  AfterClose();
  return fd;
}

void FileHandle::AfterClose() {
  closing_ = false;
  closed_ = true;
  fd_ = -1;
  if (reading_ && !persistent().IsEmpty())
    EmitRead(UV_EOF);
}

void FileHandleReadWrap::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("buffer", buffer_);
  tracker->TrackField("file_handle", this->file_handle_);
}

FileHandleReadWrap::FileHandleReadWrap(FileHandle* handle, Local<Object> obj)
  : ReqWrap(handle->env(), obj, AsyncWrap::PROVIDER_FSREQCALLBACK),
    file_handle_(handle) {}

int FileHandle::ReadStart() {
  if (!IsAlive() || IsClosing())
    return UV_EOF;

  reading_ = true;

  if (current_read_)
    return 0;

  BaseObjectPtr<FileHandleReadWrap> read_wrap;

  if (read_length_ == 0) {
    EmitRead(UV_EOF);
    return 0;
  }

  {
    // Create a new FileHandleReadWrap or re-use one.
    // Either way, we need these two scopes for AsyncReset() or otherwise
    // for creating the new instance.
    HandleScope handle_scope(env()->isolate());
    AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(this);

    auto& freelist = binding_data_->file_handle_read_wrap_freelist;
    if (freelist.size() > 0) {
      read_wrap = std::move(freelist.back());
      freelist.pop_back();
      // Use a fresh async resource.
      // Lifetime is ensured via AsyncWrap::resource_.
      Local<Object> resource = Object::New(env()->isolate());
      USE(resource->Set(
          env()->context(), env()->handle_string(), read_wrap->object()));
      read_wrap->AsyncReset(resource);
      read_wrap->file_handle_ = this;
    } else {
      Local<Object> wrap_obj;
      if (!env()
               ->filehandlereadwrap_template()
               ->NewInstance(env()->context())
               .ToLocal(&wrap_obj)) {
        return UV_EBUSY;
      }
      read_wrap = MakeDetachedBaseObject<FileHandleReadWrap>(this, wrap_obj);
    }
  }
  int64_t recommended_read = 65536;
  if (read_length_ >= 0 && read_length_ <= recommended_read)
    recommended_read = read_length_;

  read_wrap->buffer_ = EmitAlloc(recommended_read);

  current_read_ = std::move(read_wrap);
  FS_ASYNC_TRACE_BEGIN0(UV_FS_READ, current_read_.get())
  current_read_->Dispatch(uv_fs_read,
                          fd_,
                          &current_read_->buffer_,
                          1,
                          read_offset_,
                          uv_fs_callback_t{[](uv_fs_t* req) {
    FileHandle* handle;
    {
      FileHandleReadWrap* req_wrap = FileHandleReadWrap::from_req(req);
      FS_ASYNC_TRACE_END1(
          req->fs_type, req_wrap, "result", static_cast<int>(req->result))
      handle = req_wrap->file_handle_;
      CHECK_EQ(handle->current_read_.get(), req_wrap);
    }

    // ReadStart() checks whether current_read_ is set to determine whether
    // a read is in progress. Moving it into a local variable makes sure that
    // the ReadStart() call below doesn't think we're still actively reading.
    BaseObjectPtr<FileHandleReadWrap> read_wrap =
        std::move(handle->current_read_);

    ssize_t result = req->result;
    uv_buf_t buffer = read_wrap->buffer_;

    uv_fs_req_cleanup(req);

    // Push the read wrap back to the freelist, or let it be destroyed
    // once we’re exiting the current scope.
    constexpr size_t kWantedFreelistFill = 100;
    auto& freelist = handle->binding_data_->file_handle_read_wrap_freelist;
    if (freelist.size() < kWantedFreelistFill) {
      read_wrap->Reset();
      freelist.emplace_back(std::move(read_wrap));
    }

    if (result >= 0) {
      // Read at most as many bytes as we originally planned to.
      if (handle->read_length_ >= 0 && handle->read_length_ < result)
        result = handle->read_length_;

      // If we read data and we have an expected length, decrease it by
      // how much we have read.
      if (handle->read_length_ >= 0)
        handle->read_length_ -= result;

      // If we have an offset, increase it by how much we have read.
      if (handle->read_offset_ >= 0)
        handle->read_offset_ += result;
    }

    // Reading 0 bytes from a file always means EOF, or that we reached
    // the end of the requested range.
    if (result == 0)
      result = UV_EOF;

    handle->EmitRead(result, buffer);

    // Start over, if EmitRead() didn’t tell us to stop.
    if (handle->reading_)
      handle->ReadStart();
  }});

  return 0;
}

int FileHandle::ReadStop() {
  reading_ = false;
  return 0;
}

typedef SimpleShutdownWrap<ReqWrap<uv_fs_t>> FileHandleCloseWrap;

ShutdownWrap* FileHandle::CreateShutdownWrap(Local<Object> object) {
  return new FileHandleCloseWrap(this, object);
}

int FileHandle::DoShutdown(ShutdownWrap* req_wrap) {
  if (closing_ || closed_) {
    req_wrap->Done(0);
    return 1;
  }
  FileHandleCloseWrap* wrap = static_cast<FileHandleCloseWrap*>(req_wrap);
  closing_ = true;
  CHECK_NE(fd_, -1);
  FS_ASYNC_TRACE_BEGIN0(UV_FS_CLOSE, wrap)
  wrap->Dispatch(uv_fs_close, fd_, uv_fs_callback_t{[](uv_fs_t* req) {
    FileHandleCloseWrap* wrap = static_cast<FileHandleCloseWrap*>(
        FileHandleCloseWrap::from_req(req));
    FS_ASYNC_TRACE_END1(
        req->fs_type, wrap, "result", static_cast<int>(req->result))
    FileHandle* handle = static_cast<FileHandle*>(wrap->stream());
    handle->AfterClose();

    int result = static_cast<int>(req->result);
    uv_fs_req_cleanup(req);
    wrap->Done(result);
  }});

  return 0;
}


void FSReqCallback::Reject(Local<Value> reject) {
  MakeCallback(env()->oncomplete_string(), 1, &reject);
}

void FSReqCallback::ResolveStat(const uv_stat_t* stat) {
  Resolve(FillGlobalStatsArray(binding_data(), use_bigint(), stat));
}

void FSReqCallback::ResolveStatFs(const uv_statfs_t* stat) {
  Resolve(FillGlobalStatFsArray(binding_data(), use_bigint(), stat));
}

void FSReqCallback::Resolve(Local<Value> value) {
  Local<Value> argv[2] {
    Null(env()->isolate()),
    value
  };
  MakeCallback(env()->oncomplete_string(),
               value->IsUndefined() ? 1 : arraysize(argv),
               argv);
}

void FSReqCallback::SetReturnValue(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().SetUndefined();
}

void NewFSReqCallback(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  BindingData* binding_data = Realm::GetBindingData<BindingData>(args);
  new FSReqCallback(binding_data, args.This(), args[0]->IsTrue());
}

FSReqAfterScope::FSReqAfterScope(FSReqBase* wrap, uv_fs_t* req)
    : wrap_(wrap),
      req_(req),
      handle_scope_(wrap->env()->isolate()),
      context_scope_(wrap->env()->context()) {
  CHECK_EQ(wrap_->req(), req);
}

FSReqAfterScope::~FSReqAfterScope() {
  Clear();
}

void FSReqAfterScope::Clear() {
  if (!wrap_) return;

  uv_fs_req_cleanup(wrap_->req());
  wrap_->Detach();
  wrap_.reset();
}

// TODO(joyeecheung): create a normal context object, and
// construct the actual errors in the JS land using the context.
// The context should include fds for some fs APIs, currently they are
// missing in the error messages. The path, dest, syscall, fd, .etc
// can be put into the context before the binding is even invoked,
// the only information that has to come from the C++ layer is the
// error number (and possibly the syscall for abstraction),
// which is also why the errors should have been constructed
// in JS for more flexibility.
void FSReqAfterScope::Reject(uv_fs_t* req) {
  BaseObjectPtr<FSReqBase> wrap { wrap_ };
  Local<Value> exception = UVException(wrap_->env()->isolate(),
                                       static_cast<int>(req->result),
                                       wrap_->syscall(),
                                       nullptr,
                                       req->path,
                                       wrap_->data());
  Clear();
  wrap->Reject(exception);
}

bool FSReqAfterScope::Proceed() {
  if (!wrap_->env()->can_call_into_js()) {
    return false;
  }

  if (req_->result < 0) {
    Reject(req_);
    return false;
  }
  return true;
}

void AfterNoArgs(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed())
    req_wrap->Resolve(Undefined(req_wrap->env()->isolate()));
}

void AfterStat(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed()) {
    req_wrap->ResolveStat(&req->statbuf);
  }
}

void AfterStatFs(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed()) {
    req_wrap->ResolveStatFs(static_cast<uv_statfs_t*>(req->ptr));
  }
}

void AfterInteger(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  int result = static_cast<int>(req->result);
  if (result >= 0 && req_wrap->is_plain_open())
    req_wrap->env()->AddUnmanagedFd(result);

  if (after.Proceed())
    req_wrap->Resolve(Integer::New(req_wrap->env()->isolate(), result));
}

void AfterOpenFileHandle(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed()) {
    FileHandle* fd = FileHandle::New(req_wrap->binding_data(),
                                     static_cast<int>(req->result));
    if (fd == nullptr) return;
    req_wrap->Resolve(fd->object());
  }
}

void AfterMkdirp(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (after.Proceed()) {
    std::string first_path(req_wrap->continuation_data()->first_path());
    if (first_path.empty())
      return req_wrap->Resolve(Undefined(req_wrap->env()->isolate()));
    Local<Value> path;
    Local<Value> error;
    if (!StringBytes::Encode(req_wrap->env()->isolate(), first_path.c_str(),
                             req_wrap->encoding(),
                             &error).ToLocal(&path)) {
      return req_wrap->Reject(error);
    }
    return req_wrap->Resolve(path);
  }
}

void AfterStringPath(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  MaybeLocal<Value> link;
  Local<Value> error;

  if (after.Proceed()) {
    link = StringBytes::Encode(req_wrap->env()->isolate(),
                               req->path,
                               req_wrap->encoding(),
                               &error);
    if (link.IsEmpty()) {
      req_wrap->Reject(error);
    } else {
      Local<Value> val;
      if (link.ToLocal(&val)) req_wrap->Resolve(val);
    }
  }
}

void AfterStringPtr(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  MaybeLocal<Value> link;
  Local<Value> error;

  if (after.Proceed()) {
    link = StringBytes::Encode(req_wrap->env()->isolate(),
                               static_cast<const char*>(req->ptr),
                               req_wrap->encoding(),
                               &error);
    if (link.IsEmpty()) {
      req_wrap->Reject(error);
    } else {
      Local<Value> val;
      if (link.ToLocal(&val)) req_wrap->Resolve(val);
    }
  }
}

void AfterScanDir(uv_fs_t* req) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  FSReqAfterScope after(req_wrap, req);
  FS_ASYNC_TRACE_END1(
      req->fs_type, req_wrap, "result", static_cast<int>(req->result))
  if (!after.Proceed()) {
    return;
  }

  Environment* env = req_wrap->env();
  Isolate* isolate = env->isolate();
  Local<Value> error;
  int r;

  LocalVector<Value> name_v(isolate);
  LocalVector<Value> type_v(isolate);

  const bool with_file_types = req_wrap->with_file_types();

  for (;;) {
    uv_dirent_t ent;

    r = uv_fs_scandir_next(req, &ent);
    if (r == UV_EOF)
      break;
    if (r != 0) {
      return req_wrap->Reject(
          UVException(isolate, r, nullptr, req_wrap->syscall(), req->path));
    }

    Local<Value> filename;
    if (!StringBytes::Encode(isolate, ent.name, req_wrap->encoding(), &error)
             .ToLocal(&filename)) {
      return req_wrap->Reject(error);
    }
    name_v.push_back(filename);

    if (with_file_types) type_v.emplace_back(Integer::New(isolate, ent.type));
  }

  if (with_file_types) {
    Local<Value> result[] = {Array::New(isolate, name_v.data(), name_v.size()),
                             Array::New(isolate, type_v.data(), type_v.size())};
    req_wrap->Resolve(Array::New(isolate, result, arraysize(result)));
  } else {
    req_wrap->Resolve(Array::New(isolate, name_v.data(), name_v.size()));
  }
}

void Access(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);

  const int argc = args.Length();
  CHECK_GE(argc, 2);  // path, mode

  int mode;
  if (!GetValidFileMode(env, args[1], UV_FS_ACCESS).To(&mode)) {
    return;
  }

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  if (argc > 2) {  // access(path, mode, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    CHECK_NOT_NULL(req_wrap_async);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        path.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_ACCESS, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "access", UTF8, AfterNoArgs,
              uv_fs_access, *path, mode);
  } else {  // access(path, mode)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
    FSReqWrapSync req_wrap_sync("access", *path);
    FS_SYNC_TRACE_BEGIN(access);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_access, *path, mode);
    FS_SYNC_TRACE_END(access);
  }
}

void Close(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 1);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }
  env->RemoveUnmanagedFd(fd);

  if (argc > 1) {  // close(fd, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 1);
    CHECK_NOT_NULL(req_wrap_async);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_CLOSE, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "close", UTF8, AfterNoArgs,
              uv_fs_close, fd);
  } else {  // close(fd)
    FSReqWrapSync req_wrap_sync("close");
    FS_SYNC_TRACE_BEGIN(close);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_close, fd);
    FS_SYNC_TRACE_END(close);
  }
}

static void ExistsSync(const FunctionCallbackInfo<Value>& args) {
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
  FS_SYNC_TRACE_BEGIN(access);
  int err = uv_fs_access(nullptr, &req, path.out(), 0, nullptr);
  FS_SYNC_TRACE_END(access);

#ifdef _WIN32
  // In case of an invalid symlink, `uv_fs_access` on win32
  // will **not** return an error and is therefore not enough.
  // Double check with `uv_fs_stat()`.
  if (err == 0) {
    FS_SYNC_TRACE_BEGIN(stat);
    err = uv_fs_stat(nullptr, &req, path.out(), nullptr);
    FS_SYNC_TRACE_END(stat);
  }
#endif  // _WIN32

  args.GetReturnValue().Set(err == 0);
}

// Used to speed up module loading.  Returns 0 if the path refers to
// a file, 1 when it's a directory or < 0 on error (usually -ENOENT.)
// The speedup comes from not creating thousands of Stat and Error objects.
// Do not expose this function through public API as it doesn't hold
// Permission Model checks.
static void InternalModuleStat(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_GE(args.Length(), 2);
  CHECK(args[1]->IsString());
  BufferValue path(env->isolate(), args[1]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  uv_fs_t req;
  int rc = uv_fs_stat(env->event_loop(), &req, *path, nullptr);
  if (rc == 0) {
    const uv_stat_t* const s = static_cast<const uv_stat_t*>(req.ptr);
    rc = S_ISDIR(s->st_mode);
  }
  uv_fs_req_cleanup(&req);

  args.GetReturnValue().Set(rc);
}

static int32_t FastInternalModuleStat(
    Local<Object> unused,
    Local<Object> recv,
    const FastOneByteString& input,
    // NOLINTNEXTLINE(runtime/references) This is V8 api.
    FastApiCallbackOptions& options) {
  Environment* env = Environment::GetCurrent(options.isolate);
  HandleScope scope(env->isolate());

  auto path = std::filesystem::path(input.data, input.data + input.length);

  switch (std::filesystem::status(path).type()) {
    case std::filesystem::file_type::directory:
      return 1;
    case std::filesystem::file_type::regular:
      return 0;
    default:
      return -1;
  }
}

v8::CFunction fast_internal_module_stat_(
    v8::CFunction::Make(FastInternalModuleStat));

constexpr bool is_uv_error_except_no_entry(int result) {
  return result < 0 && result != UV_ENOENT;
}

static void Stat(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Environment* env = realm->env();

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(realm->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  bool use_bigint = args[1]->IsTrue();
  if (!args[2]->IsUndefined()) {  // stat(path, use_bigint, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2, use_bigint);
    CHECK_NOT_NULL(req_wrap_async);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        path.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_STAT, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "stat", UTF8, AfterStat,
              uv_fs_stat, *path);
  } else {  // stat(path, use_bigint, undefined, do_not_throw_if_no_entry)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
    bool do_not_throw_if_no_entry = args[3]->IsFalse();
    FSReqWrapSync req_wrap_sync("stat", *path);
    FS_SYNC_TRACE_BEGIN(stat);
    int result;
    if (do_not_throw_if_no_entry) {
      result = SyncCallAndThrowIf(
          is_uv_error_except_no_entry, env, &req_wrap_sync, uv_fs_stat, *path);
    } else {
      result = SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_stat, *path);
    }
    FS_SYNC_TRACE_END(stat);
    if (is_uv_error(result)) {
      return;
    }
    Local<Value> arr = FillGlobalStatsArray(binding_data, use_bigint,
        static_cast<const uv_stat_t*>(req_wrap_sync.req.ptr));
    args.GetReturnValue().Set(arr);
  }
}

static void LStat(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Environment* env = realm->env();

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(realm->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  bool use_bigint = args[1]->IsTrue();
  if (!args[2]->IsUndefined()) {  // lstat(path, use_bigint, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2, use_bigint);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_LSTAT, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "lstat", UTF8, AfterStat,
              uv_fs_lstat, *path);
  } else {  // lstat(path, use_bigint, undefined, throw_if_no_entry)
    bool do_not_throw_if_no_entry = args[3]->IsFalse();
    FSReqWrapSync req_wrap_sync("lstat", *path);
    FS_SYNC_TRACE_BEGIN(lstat);
    int result;
    if (do_not_throw_if_no_entry) {
      result = SyncCallAndThrowIf(
          is_uv_error_except_no_entry, env, &req_wrap_sync, uv_fs_lstat, *path);
    } else {
      result = SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_lstat, *path);
    }
    FS_SYNC_TRACE_END(lstat);
    if (is_uv_error(result)) {
      return;
    }

    Local<Value> arr = FillGlobalStatsArray(binding_data, use_bigint,
        static_cast<const uv_stat_t*>(req_wrap_sync.req.ptr));
    args.GetReturnValue().Set(arr);
  }
}

static void FStat(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Environment* env = realm->env();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  bool use_bigint = args[1]->IsTrue();
  if (!args[2]->IsUndefined()) {  // fstat(fd, use_bigint, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2, use_bigint);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FSTAT, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "fstat", UTF8, AfterStat,
              uv_fs_fstat, fd);
  } else {  // fstat(fd, use_bigint, undefined, do_not_throw_error)
    bool do_not_throw_error = args[2]->IsTrue();
    const auto should_throw = [do_not_throw_error](int result) {
      return is_uv_error(result) && !do_not_throw_error;
    };
    FSReqWrapSync req_wrap_sync("fstat");
    FS_SYNC_TRACE_BEGIN(fstat);
    int err =
        SyncCallAndThrowIf(should_throw, env, &req_wrap_sync, uv_fs_fstat, fd);
    FS_SYNC_TRACE_END(fstat);
    if (is_uv_error(err)) {
      return;
    }

    Local<Value> arr = FillGlobalStatsArray(binding_data, use_bigint,
        static_cast<const uv_stat_t*>(req_wrap_sync.req.ptr));
    args.GetReturnValue().Set(arr);
  }
}

static void StatFs(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Environment* env = realm->env();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue path(realm->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  bool use_bigint = args[1]->IsTrue();
  if (argc > 2) {  // statfs(path, use_bigint, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2, use_bigint);
    CHECK_NOT_NULL(req_wrap_async);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        path.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_STATFS, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env,
              req_wrap_async,
              args,
              "statfs",
              UTF8,
              AfterStatFs,
              uv_fs_statfs,
              *path);
  } else {  // statfs(path, use_bigint)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
    FSReqWrapSync req_wrap_sync("statfs", *path);
    FS_SYNC_TRACE_BEGIN(statfs);
    int result =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_statfs, *path);
    FS_SYNC_TRACE_END(statfs);
    if (is_uv_error(result)) {
      return;
    }

    Local<Value> arr = FillGlobalStatFsArray(
        binding_data,
        use_bigint,
        static_cast<const uv_statfs_t*>(req_wrap_sync.req.ptr));
    args.GetReturnValue().Set(arr);
  }
}

static void Symlink(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue target(isolate, args[0]);
  CHECK_NOT_NULL(*target);
  auto target_view = target.ToStringView();
  // To avoid bypass the symlink target should be allowed to read and write
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, target_view);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, target_view);

  BufferValue path(isolate, args[1]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  CHECK(args[2]->IsInt32());
  int flags = args[2].As<Int32>()->Value();

  if (argc > 3) {  // symlink(target, path, flags, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN2(UV_FS_SYMLINK,
                          req_wrap_async,
                          "target",
                          TRACE_STR_COPY(*target),
                          "path",
                          TRACE_STR_COPY(*path))
    AsyncDestCall(env, req_wrap_async, args, "symlink", *path, path.length(),
                  UTF8, AfterNoArgs, uv_fs_symlink, *target, *path, flags);
  } else {  // symlink(target, path, flags, undefined, ctx)
    FSReqWrapSync req_wrap_sync("symlink", *target, *path);
    FS_SYNC_TRACE_BEGIN(symlink);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_symlink, *target, *path, flags);
    FS_SYNC_TRACE_END(symlink);
  }
}

static void Link(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue src(isolate, args[0]);
  CHECK_NOT_NULL(*src);
  ToNamespacedPath(env, &src);

  const auto src_view = src.ToStringView();

  BufferValue dest(isolate, args[1]);
  CHECK_NOT_NULL(*dest);
  ToNamespacedPath(env, &dest);

  const auto dest_view = dest.ToStringView();

  if (argc > 2) {  // link(src, dest, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    // To avoid bypass the link target should be allowed to read and write
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        src_view);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        src_view);

    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        dest_view);
    FS_ASYNC_TRACE_BEGIN2(UV_FS_LINK,
                          req_wrap_async,
                          "src",
                          TRACE_STR_COPY(*src),
                          "dest",
                          TRACE_STR_COPY(*dest))
    AsyncDestCall(env, req_wrap_async, args, "link", *dest, dest.length(), UTF8,
                  AfterNoArgs, uv_fs_link, *src, *dest);
  } else {  // link(src, dest)
    // To avoid bypass the link target should be allowed to read and write
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, src_view);
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemWrite, src_view);

    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemWrite, dest_view);
    FSReqWrapSync req_wrap_sync("link", *src, *dest);
    FS_SYNC_TRACE_BEGIN(link);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_link, *src, *dest);
    FS_SYNC_TRACE_END(link);
  }
}

static void ReadLink(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, path.ToStringView());

  const enum encoding encoding = ParseEncoding(isolate, args[1], UTF8);

  if (argc > 2) {  // readlink(path, encoding, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_READLINK, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "readlink", encoding, AfterStringPtr,
              uv_fs_readlink, *path);
  } else {  // readlink(path, encoding)
    FSReqWrapSync req_wrap_sync("readlink", *path);
    FS_SYNC_TRACE_BEGIN(readlink);
    int err =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_readlink, *path);
    FS_SYNC_TRACE_END(readlink);
    if (err < 0) {
      return;
    }
    const char* link_path = static_cast<const char*>(req_wrap_sync.req.ptr);

    Local<Value> error;
    Local<Value> ret;
    if (!StringBytes::Encode(isolate, link_path, encoding, &error)
             .ToLocal(&ret)) {
      DCHECK(!error.IsEmpty());
      env->isolate()->ThrowException(error);
      return;
    }

    args.GetReturnValue().Set(ret);
  }
}

static void Rename(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue old_path(isolate, args[0]);
  CHECK_NOT_NULL(*old_path);
  ToNamespacedPath(env, &old_path);
  auto view_old_path = old_path.ToStringView();

  BufferValue new_path(isolate, args[1]);
  CHECK_NOT_NULL(*new_path);
  ToNamespacedPath(env, &new_path);

  if (argc > 2) {  // rename(old_path, new_path, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        view_old_path);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        view_old_path);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        new_path.ToStringView());
    FS_ASYNC_TRACE_BEGIN2(UV_FS_RENAME,
                          req_wrap_async,
                          "old_path",
                          TRACE_STR_COPY(*old_path),
                          "new_path",
                          TRACE_STR_COPY(*new_path))
    AsyncDestCall(env, req_wrap_async, args, "rename", *new_path,
                  new_path.length(), UTF8, AfterNoArgs, uv_fs_rename,
                  *old_path, *new_path);
  } else {  // rename(old_path, new_path)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, view_old_path);
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemWrite, view_old_path);
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        new_path.ToStringView());
    FSReqWrapSync req_wrap_sync("rename", *old_path, *new_path);
    FS_SYNC_TRACE_BEGIN(rename);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_rename, *old_path, *new_path);
    FS_SYNC_TRACE_END(rename);
  }
}

static void FTruncate(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  CHECK(IsSafeJsInt(args[1]));
  const int64_t len = args[1].As<Integer>()->Value();

  if (argc > 2) {  // ftruncate(fd, len, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FTRUNCATE, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "ftruncate", UTF8, AfterNoArgs,
              uv_fs_ftruncate, fd, len);
  } else {  // ftruncate(fd, len)
    FSReqWrapSync req_wrap_sync("ftruncate");
    FS_SYNC_TRACE_BEGIN(ftruncate);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_ftruncate, fd, len);
    FS_SYNC_TRACE_END(ftruncate);
  }
}

static void Fdatasync(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 1);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  if (argc > 1) {  // fdatasync(fd, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 1);
    CHECK_NOT_NULL(req_wrap_async);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FDATASYNC, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "fdatasync", UTF8, AfterNoArgs,
              uv_fs_fdatasync, fd);
  } else {  // fdatasync(fd)
    FSReqWrapSync req_wrap_sync("fdatasync");
    FS_SYNC_TRACE_BEGIN(fdatasync);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_fdatasync, fd);
    FS_SYNC_TRACE_END(fdatasync);
  }
}

static void Fsync(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 1);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  if (argc > 1) {
    FSReqBase* req_wrap_async = GetReqWrap(args, 1);
    CHECK_NOT_NULL(req_wrap_async);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FSYNC, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "fsync", UTF8, AfterNoArgs,
              uv_fs_fsync, fd);
  } else {
    FSReqWrapSync req_wrap_sync("fsync");
    FS_SYNC_TRACE_BEGIN(fsync);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_fsync, fd);
    FS_SYNC_TRACE_END(fsync);
  }
}

static void Unlink(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 1);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  if (argc > 1) {  // unlink(path, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 1);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    CHECK_NOT_NULL(req_wrap_async);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_UNLINK, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "unlink", UTF8, AfterNoArgs,
              uv_fs_unlink, *path);
  } else {  // unlink(path)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    FSReqWrapSync req_wrap_sync("unlink", *path);
    FS_SYNC_TRACE_BEGIN(unlink);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_unlink, *path);
    FS_SYNC_TRACE_END(unlink);
  }
}

static void RMDir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 1);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  if (argc > 1) {
    FSReqBase* req_wrap_async = GetReqWrap(args, 1);  // rmdir(path, req)
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_RMDIR, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "rmdir", UTF8, AfterNoArgs,
              uv_fs_rmdir, *path);
  } else {  // rmdir(path)
    FSReqWrapSync req_wrap_sync("rmdir", *path);
    FS_SYNC_TRACE_BEGIN(rmdir);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_rmdir, *path);
    FS_SYNC_TRACE_END(rmdir);
  }
}

static void RmSync(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_EQ(args.Length(), 4);  // path, maxRetries, recursive, retryDelay

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());
  auto file_path = std::filesystem::path(path.ToStringView());
  std::error_code error;
  auto file_status = std::filesystem::status(file_path, error);

  if (file_status.type() == std::filesystem::file_type::not_found) {
    return;
  }

  int maxRetries = args[1].As<Int32>()->Value();
  int recursive = args[2]->IsTrue();
  int retryDelay = args[3].As<Int32>()->Value();

  // File is a directory and recursive is false
  if (file_status.type() == std::filesystem::file_type::directory &&
      !recursive) {
    return THROW_ERR_FS_EISDIR(
        isolate, "Path is a directory: %s", file_path.c_str());
  }

  // Allowed errors are:
  // - EBUSY: std::errc::device_or_resource_busy
  // - EMFILE: std::errc::too_many_files_open
  // - ENFILE: std::errc::too_many_files_open_in_system
  // - ENOTEMPTY: std::errc::directory_not_empty
  // - EPERM: std::errc::operation_not_permitted
  auto can_omit_error = [](std::error_code error) -> bool {
    return (error == std::errc::device_or_resource_busy ||
            error == std::errc::too_many_files_open ||
            error == std::errc::too_many_files_open_in_system ||
            error == std::errc::directory_not_empty ||
            error == std::errc::operation_not_permitted);
  };

  int i = 1;

  while (maxRetries >= 0) {
    if (recursive) {
      std::filesystem::remove_all(file_path, error);
    } else {
      std::filesystem::remove(file_path, error);
    }

    if (!error || error == std::errc::no_such_file_or_directory) {
      return;
    } else if (!can_omit_error(error)) {
      break;
    }

    if (retryDelay > 0) {
#ifdef _WIN32
      Sleep(i * retryDelay / 1000);
#else
      sleep(i * retryDelay / 1000);
#endif
    }
    maxRetries--;
    i++;
  }

  // On Windows path::c_str() returns wide char, convert to std::string first.
  std::string file_path_str = file_path.string();
  const char* path_c_str = file_path_str.c_str();
#ifdef _WIN32
  int permission_denied_error = EPERM;
#else
  int permission_denied_error = EACCES;
#endif  // !_WIN32

  if (error == std::errc::operation_not_permitted) {
    std::string message = "Operation not permitted: " + file_path_str;
    return env->ThrowErrnoException(EPERM, "rm", message.c_str(), path_c_str);
  } else if (error == std::errc::directory_not_empty) {
    std::string message = "Directory not empty: " + file_path_str;
    return env->ThrowErrnoException(EACCES, "rm", message.c_str(), path_c_str);
  } else if (error == std::errc::not_a_directory) {
    std::string message = "Not a directory: " + file_path_str;
    return env->ThrowErrnoException(ENOTDIR, "rm", message.c_str(), path_c_str);
  } else if (error == std::errc::permission_denied) {
    std::string message = "Permission denied: " + file_path_str;
    return env->ThrowErrnoException(
        permission_denied_error, "rm", message.c_str(), path_c_str);
  }

  std::string message = "Unknown error: " + error.message();
  return env->ThrowErrnoException(
      UV_UNKNOWN, "rm", message.c_str(), path_c_str);
}

int MKDirpSync(uv_loop_t* loop,
               uv_fs_t* req,
               const std::string& path,
               int mode,
               uv_fs_cb cb) {
  FSReqWrapSync* req_wrap = ContainerOf(&FSReqWrapSync::req, req);

  // on the first iteration of algorithm, stash state information.
  if (req_wrap->continuation_data() == nullptr) {
    req_wrap->set_continuation_data(
        std::make_unique<FSContinuationData>(req, mode, cb));
    req_wrap->continuation_data()->PushPath(std::move(path));
  }

  while (req_wrap->continuation_data()->paths().size() > 0) {
    std::string next_path = req_wrap->continuation_data()->PopPath();
    int err = uv_fs_mkdir(loop, req, next_path.c_str(), mode, nullptr);
    while (true) {
      switch (err) {
        // Note: uv_fs_req_cleanup in terminal paths will be called by
        // ~FSReqWrapSync():
        case 0:
          req_wrap->continuation_data()->MaybeSetFirstPath(next_path);
          if (req_wrap->continuation_data()->paths().empty()) {
            return 0;
          }
          break;
        case UV_EACCES:
        case UV_ENOSPC:
        case UV_ENOTDIR:
        case UV_EPERM: {
          return err;
        }
        case UV_ENOENT: {
          std::string dirname =
              next_path.substr(0, next_path.find_last_of(kPathSeparator));
          if (dirname != next_path) {
            req_wrap->continuation_data()->PushPath(std::move(next_path));
            req_wrap->continuation_data()->PushPath(std::move(dirname));
          } else if (req_wrap->continuation_data()->paths().empty()) {
            err = UV_EEXIST;
            continue;
          }
          break;
        }
        default:
          uv_fs_req_cleanup(req);
          int orig_err = err;
          err = uv_fs_stat(loop, req, next_path.c_str(), nullptr);
          if (err == 0 && !S_ISDIR(req->statbuf.st_mode)) {
            uv_fs_req_cleanup(req);
            if (orig_err == UV_EEXIST &&
              req_wrap->continuation_data()->paths().size() > 0) {
              return UV_ENOTDIR;
            }
            return UV_EEXIST;
          }
          if (err < 0) return err;
          break;
      }
      break;
    }
    uv_fs_req_cleanup(req);
  }

  return 0;
}

int MKDirpAsync(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int mode,
                uv_fs_cb cb) {
  FSReqBase* req_wrap = FSReqBase::from_req(req);
  // on the first iteration of algorithm, stash state information.
  if (req_wrap->continuation_data() == nullptr) {
    req_wrap->set_continuation_data(
        std::make_unique<FSContinuationData>(req, mode, cb));
    req_wrap->continuation_data()->PushPath(std::move(path));
  }

  // on each iteration of algorithm, mkdir directory on top of stack.
  std::string next_path = req_wrap->continuation_data()->PopPath();
  int err = uv_fs_mkdir(loop, req, next_path.c_str(), mode,
                        uv_fs_callback_t{[](uv_fs_t* req) {
    FSReqBase* req_wrap = FSReqBase::from_req(req);
    Environment* env = req_wrap->env();
    uv_loop_t* loop = env->event_loop();
    std::string path = req->path;
    int err = static_cast<int>(req->result);

    while (true) {
      switch (err) {
        // Note: uv_fs_req_cleanup in terminal paths will be called by
        // FSReqAfterScope::~FSReqAfterScope()
        case 0: {
          if (req_wrap->continuation_data()->paths().empty()) {
            req_wrap->continuation_data()->MaybeSetFirstPath(path);
            req_wrap->continuation_data()->Done(0);
          } else {
            req_wrap->continuation_data()->MaybeSetFirstPath(path);
            uv_fs_req_cleanup(req);
            MKDirpAsync(loop, req, path.c_str(),
                        req_wrap->continuation_data()->mode(), nullptr);
          }
          break;
        }
        case UV_EACCES:
        case UV_ENOTDIR:
        case UV_EPERM: {
          req_wrap->continuation_data()->Done(err);
          break;
        }
        case UV_ENOENT: {
          std::string dirname =
              path.substr(0, path.find_last_of(kPathSeparator));
          if (dirname != path) {
            req_wrap->continuation_data()->PushPath(path);
            req_wrap->continuation_data()->PushPath(std::move(dirname));
          } else if (req_wrap->continuation_data()->paths().empty()) {
            err = UV_EEXIST;
            continue;
          }
          uv_fs_req_cleanup(req);
          MKDirpAsync(loop, req, path.c_str(),
                      req_wrap->continuation_data()->mode(), nullptr);
          break;
        }
        default:
          uv_fs_req_cleanup(req);
          // Stash err for use in the callback.
          req->data = reinterpret_cast<void*>(static_cast<intptr_t>(err));
          int err = uv_fs_stat(loop, req, path.c_str(),
                               uv_fs_callback_t{[](uv_fs_t* req) {
            FSReqBase* req_wrap = FSReqBase::from_req(req);
            int err = static_cast<int>(req->result);
            if (reinterpret_cast<intptr_t>(req->data) == UV_EEXIST &&
                  req_wrap->continuation_data()->paths().size() > 0) {
              if (err == 0 && S_ISDIR(req->statbuf.st_mode)) {
                Environment* env = req_wrap->env();
                uv_loop_t* loop = env->event_loop();
                std::string path = req->path;
                uv_fs_req_cleanup(req);
                MKDirpAsync(loop, req, path.c_str(),
                            req_wrap->continuation_data()->mode(), nullptr);
                return;
              }
              err = UV_ENOTDIR;
            }
            // verify that the path pointed to is actually a directory.
            if (err == 0 && !S_ISDIR(req->statbuf.st_mode)) err = UV_EEXIST;
            req_wrap->continuation_data()->Done(err);
          }});
          if (err < 0) req_wrap->continuation_data()->Done(err);
          break;
      }
      break;
    }
  }});

  return err;
}

static void MKDir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  CHECK(args[1]->IsInt32());
  const int mode = args[1].As<Int32>()->Value();

  CHECK(args[2]->IsBoolean());
  bool mkdirp = args[2]->IsTrue();

  if (argc > 3) {  // mkdir(path, mode, recursive, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_UNLINK, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "mkdir", UTF8,
              mkdirp ? AfterMkdirp : AfterNoArgs,
              mkdirp ? MKDirpAsync : uv_fs_mkdir, *path, mode);
  } else {  // mkdir(path, mode, recursive)
    FSReqWrapSync req_wrap_sync("mkdir", *path);
    FS_SYNC_TRACE_BEGIN(mkdir);
    if (mkdirp) {
      env->PrintSyncTrace();
      int err = MKDirpSync(
          env->event_loop(), &req_wrap_sync.req, *path, mode, nullptr);
      if (is_uv_error(err)) {
        env->ThrowUVException(err, "mkdir", nullptr, *path);
        return;
      }
      if (!req_wrap_sync.continuation_data()->first_path().empty()) {
        Local<Value> error;
        Local<Value> ret;
        std::string first_path(req_wrap_sync.continuation_data()->first_path());
        if (!StringBytes::Encode(
                 env->isolate(), first_path.c_str(), UTF8, &error)
                 .ToLocal(&ret)) {
          DCHECK(!error.IsEmpty());
          env->isolate()->ThrowException(error);
          return;
        }
        args.GetReturnValue().Set(ret);
      }
    } else {
      SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_mkdir, *path, mode);
    }
    FS_SYNC_TRACE_END(mkdir);
  }
}

static void RealPath(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  const enum encoding encoding = ParseEncoding(isolate, args[1], UTF8);

  if (argc > 2) {  // realpath(path, encoding, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_REALPATH, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "realpath", encoding, AfterStringPtr,
              uv_fs_realpath, *path);
  } else {  // realpath(path, encoding, undefined, ctx)
    FSReqWrapSync req_wrap_sync("realpath", *path);
    FS_SYNC_TRACE_BEGIN(realpath);
    int err =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_realpath, *path);
    FS_SYNC_TRACE_END(realpath);
    if (err < 0) {
      return;
    }

    const char* link_path = static_cast<const char*>(req_wrap_sync.req.ptr);

    Local<Value> error;
    Local<Value> ret;
    if (!StringBytes::Encode(isolate, link_path, encoding, &error)
             .ToLocal(&ret)) {
      DCHECK(!error.IsEmpty());
      env->isolate()->ThrowException(error);
      return;
    }

    args.GetReturnValue().Set(ret);
  }
}

static void ReadDir(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(isolate, args[0]);
  CHECK_NOT_NULL(*path);
#ifdef _WIN32
  // On Windows, some API functions accept paths with trailing slashes,
  // while others do not. This code checks if the input path ends with
  // a slash (either '/' or '\\') and, if so, ensures that the processed
  // path also ends with a trailing backslash ('\\').
  bool slashCheck = false;
  if (path.ToStringView().ends_with("/") ||
      path.ToStringView().ends_with("\\")) {
    slashCheck = true;
  }
#endif

  ToNamespacedPath(env, &path);

#ifdef _WIN32
  if (slashCheck) {
    size_t new_length = path.length() + 1;
    path.AllocateSufficientStorage(new_length + 1);
    path.SetLengthAndZeroTerminate(new_length);
    path.out()[new_length - 1] = '\\';
  }
#endif

  const enum encoding encoding = ParseEncoding(isolate, args[1], UTF8);

  bool with_types = args[2]->IsTrue();

  if (argc > 3) {  // readdir(path, encoding, withTypes, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        path.ToStringView());
    req_wrap_async->set_with_file_types(with_types);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_SCANDIR, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env,
              req_wrap_async,
              args,
              "scandir",
              encoding,
              AfterScanDir,
              uv_fs_scandir,
              *path,
              0 /*flags*/);
  } else {  // readdir(path, encoding, withTypes)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
    FSReqWrapSync req_wrap_sync("scandir", *path);
    FS_SYNC_TRACE_BEGIN(readdir);
    int err = SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_scandir, *path, 0 /*flags*/);
    FS_SYNC_TRACE_END(readdir);
    if (is_uv_error(err)) {
      return;
    }

    int r;
    LocalVector<Value> name_v(isolate);
    LocalVector<Value> type_v(isolate);

    for (;;) {
      uv_dirent_t ent;

      r = uv_fs_scandir_next(&(req_wrap_sync.req), &ent);
      if (r == UV_EOF)
        break;
      if (is_uv_error(r)) {
        env->ThrowUVException(r, "scandir", nullptr, *path);
        return;
      }

      Local<Value> error;
      Local<Value> fn;
      if (!StringBytes::Encode(isolate, ent.name, encoding, &error)
               .ToLocal(&fn)) {
        DCHECK(!error.IsEmpty());
        isolate->ThrowException(error);
        return;
      }

      name_v.push_back(fn);

      if (with_types) {
        type_v.emplace_back(Integer::New(isolate, ent.type));
      }
    }


    Local<Array> names = Array::New(isolate, name_v.data(), name_v.size());
    if (with_types) {
      Local<Value> result[] = {
        names,
        Array::New(isolate, type_v.data(), type_v.size())
      };
      args.GetReturnValue().Set(Array::New(isolate, result, arraysize(result)));
    } else {
      args.GetReturnValue().Set(names);
    }
  }
}

static inline Maybe<void> AsyncCheckOpenPermissions(Environment* env,
                                                    FSReqBase* req_wrap,
                                                    const BufferValue& path,
                                                    int flags) {
  // These flags capture the intention of the open() call.
  const int rwflags = flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR);

  // These flags have write-like side effects even with O_RDONLY, at least on
  // some operating systems. On Windows, for example, O_RDONLY | O_TEMPORARY
  // can be used to delete a file. Bizarre.
  const int write_as_side_effect = flags & (UV_FS_O_APPEND | UV_FS_O_CREAT |
                                            UV_FS_O_TRUNC | UV_FS_O_TEMPORARY);

  auto pathView = path.ToStringView();
  if (rwflags != UV_FS_O_WRONLY) {
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap,
        permission::PermissionScope::kFileSystemRead,
        pathView,
        Nothing<void>());
  }
  if (rwflags != UV_FS_O_RDONLY || write_as_side_effect) {
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap,
        permission::PermissionScope::kFileSystemWrite,
        pathView,
        Nothing<void>());
  }
  return JustVoid();
}

static inline Maybe<void> CheckOpenPermissions(Environment* env,
                                               const BufferValue& path,
                                               int flags) {
  // These flags capture the intention of the open() call.
  const int rwflags = flags & (UV_FS_O_RDONLY | UV_FS_O_WRONLY | UV_FS_O_RDWR);

  // These flags have write-like side effects even with O_RDONLY, at least on
  // some operating systems. On Windows, for example, O_RDONLY | O_TEMPORARY
  // can be used to delete a file. Bizarre.
  const int write_as_side_effect = flags & (UV_FS_O_APPEND | UV_FS_O_CREAT |
                                            UV_FS_O_TRUNC | UV_FS_O_TEMPORARY);

  auto pathView = path.ToStringView();
  if (rwflags != UV_FS_O_WRONLY) {
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemRead,
        pathView,
        Nothing<void>());
  }
  if (rwflags != UV_FS_O_RDONLY || write_as_side_effect) {
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        pathView,
        Nothing<void>());
  }
  return JustVoid();
}

static void Open(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  CHECK(args[1]->IsInt32());
  const int flags = args[1].As<Int32>()->Value();

  CHECK(args[2]->IsInt32());
  const int mode = args[2].As<Int32>()->Value();

  if (argc > 3) {  // open(path, flags, mode, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    CHECK_NOT_NULL(req_wrap_async);
    if (AsyncCheckOpenPermissions(env, req_wrap_async, path, flags).IsNothing())
      return;
    req_wrap_async->set_is_plain_open(true);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_OPEN, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "open", UTF8, AfterInteger,
              uv_fs_open, *path, flags, mode);
  } else {  // open(path, flags, mode)
    if (CheckOpenPermissions(env, path, flags).IsNothing()) return;
    FSReqWrapSync req_wrap_sync("open", *path);
    FS_SYNC_TRACE_BEGIN(open);
    int result = SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_open, *path, flags, mode);
    FS_SYNC_TRACE_END(open);
    if (is_uv_error(result)) return;
    env->AddUnmanagedFd(result);
    args.GetReturnValue().Set(result);
  }
}

static void OpenFileHandle(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();
  Environment* env = realm->env();

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(realm->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  CHECK(args[1]->IsInt32());
  const int flags = args[1].As<Int32>()->Value();

  CHECK(args[2]->IsInt32());
  const int mode = args[2].As<Int32>()->Value();

  if (CheckOpenPermissions(env, path, flags).IsNothing()) return;

  FSReqBase* req_wrap_async = GetReqWrap(args, 3);
  if (req_wrap_async != nullptr) {  // openFileHandle(path, flags, mode, req)
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_OPEN, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "open", UTF8, AfterOpenFileHandle,
              uv_fs_open, *path, flags, mode);
  } else {  // openFileHandle(path, flags, mode, undefined, ctx)
    CHECK_EQ(argc, 5);
    FSReqWrapSync req_wrap_sync;
    FS_SYNC_TRACE_BEGIN(open);
    int result = SyncCall(env, args[4], &req_wrap_sync, "open",
                          uv_fs_open, *path, flags, mode);
    FS_SYNC_TRACE_END(open);
    if (result < 0) {
      return;  // syscall failed, no need to continue, error info is in ctx
    }
    FileHandle* fd = FileHandle::New(binding_data, result);
    if (fd == nullptr) return;
    args.GetReturnValue().Set(fd->object());
  }
}

static void CopyFile(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 3);  // src, dest, flags

  int flags;
  if (!GetValidFileMode(env, args[2], UV_FS_COPYFILE).To(&flags)) {
    return;
  }

  BufferValue src(isolate, args[0]);
  CHECK_NOT_NULL(*src);
  ToNamespacedPath(env, &src);

  BufferValue dest(isolate, args[1]);
  CHECK_NOT_NULL(*dest);
  ToNamespacedPath(env, &dest);

  if (argc > 3) {  // copyFile(src, dest, flags, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemRead,
        src.ToStringView());
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        dest.ToStringView());
    FS_ASYNC_TRACE_BEGIN2(UV_FS_COPYFILE,
                          req_wrap_async,
                          "src",
                          TRACE_STR_COPY(*src),
                          "dest",
                          TRACE_STR_COPY(*dest))
    AsyncDestCall(env, req_wrap_async, args, "copyfile",
                  *dest, dest.length(), UTF8, AfterNoArgs,
                  uv_fs_copyfile, *src, *dest, flags);
  } else {  // copyFile(src, dest, flags)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kFileSystemRead, src.ToStringView());
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        dest.ToStringView());
    FSReqWrapSync req_wrap_sync("copyfile", *src, *dest);
    FS_SYNC_TRACE_BEGIN(copyfile);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_copyfile, *src, *dest, flags);
    FS_SYNC_TRACE_END(copyfile);
  }
}

// Wrapper for write(2).
//
// bytesWritten = write(fd, buffer, offset, length, position, callback)
// 0 fd        integer. file descriptor
// 1 buffer    the data to write
// 2 offset    where in the buffer to start from
// 3 length    how much to write
// 4 position  if integer, position to write at in the file.
//             if null, write from the current position
static void WriteBuffer(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 4);

  CHECK(args[0]->IsInt32());
  const int fd = args[0].As<Int32>()->Value();

  CHECK(Buffer::HasInstance(args[1]));
  Local<Object> buffer_obj = args[1].As<Object>();
  char* buffer_data = Buffer::Data(buffer_obj);
  size_t buffer_length = Buffer::Length(buffer_obj);

  CHECK(IsSafeJsInt(args[2]));
  const int64_t off_64 = args[2].As<Integer>()->Value();
  CHECK_GE(off_64, 0);
  CHECK_LE(static_cast<uint64_t>(off_64), buffer_length);
  const size_t off = static_cast<size_t>(off_64);

  CHECK(args[3]->IsInt32());
  const size_t len = static_cast<size_t>(args[3].As<Int32>()->Value());
  CHECK(Buffer::IsWithinBounds(off, len, buffer_length));
  CHECK_LE(len, buffer_length);
  CHECK_GE(off + len, off);

  const int64_t pos = GetOffset(args[4]);

  char* buf = buffer_data + off;
  uv_buf_t uvbuf = uv_buf_init(buf, len);

  FSReqBase* req_wrap_async = GetReqWrap(args, 5);
  if (req_wrap_async != nullptr) {  // write(fd, buffer, off, len, pos, req)
    FS_ASYNC_TRACE_BEGIN0(UV_FS_WRITE, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "write", UTF8, AfterInteger,
              uv_fs_write, fd, &uvbuf, 1, pos);
  } else {  // write(fd, buffer, off, len, pos, undefined, ctx)
    CHECK_EQ(argc, 7);
    FSReqWrapSync req_wrap_sync;
    FS_SYNC_TRACE_BEGIN(write);
    int bytesWritten = SyncCall(env, args[6], &req_wrap_sync, "write",
                                uv_fs_write, fd, &uvbuf, 1, pos);
    FS_SYNC_TRACE_END(write, "bytesWritten", bytesWritten);
    args.GetReturnValue().Set(bytesWritten);
  }
}


// Wrapper for writev(2).
//
// bytesWritten = writev(fd, chunks, position, callback)
// 0 fd        integer. file descriptor
// 1 chunks    array of buffers to write
// 2 position  if integer, position to write at in the file.
//             if null, write from the current position
static void WriteBuffers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  CHECK(args[0]->IsInt32());
  const int fd = args[0].As<Int32>()->Value();

  CHECK(args[1]->IsArray());
  Local<Array> chunks = args[1].As<Array>();

  int64_t pos = GetOffset(args[2]);

  MaybeStackBuffer<uv_buf_t> iovs(chunks->Length());

  for (uint32_t i = 0; i < iovs.length(); i++) {
    Local<Value> chunk;
    if (!chunks->Get(env->context(), i).ToLocal(&chunk)) return;
    CHECK(Buffer::HasInstance(chunk));
    iovs[i] = uv_buf_init(Buffer::Data(chunk), Buffer::Length(chunk));
  }

  if (argc > 3) {  // writeBuffers(fd, chunks, pos, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_WRITE, req_wrap_async)
    AsyncCall(env,
              req_wrap_async,
              args,
              "write",
              UTF8,
              AfterInteger,
              uv_fs_write,
              fd,
              *iovs,
              iovs.length(),
              pos);
  } else {  // writeBuffers(fd, chunks, pos)
    FSReqWrapSync req_wrap_sync("write");
    FS_SYNC_TRACE_BEGIN(write);
    int bytesWritten = SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_write, fd, *iovs, iovs.length(), pos);
    FS_SYNC_TRACE_END(write, "bytesWritten", bytesWritten);
    if (is_uv_error(bytesWritten)) {
      return;
    }
    args.GetReturnValue().Set(bytesWritten);
  }
}


// Wrapper for write(2).
//
// bytesWritten = write(fd, string, position, enc, callback)
// 0 fd        integer. file descriptor
// 1 string    non-buffer values are converted to strings
// 2 position  if integer, position to write at in the file.
//             if null, write from the current position
// 3 enc       encoding of string
static void WriteString(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 4);
  CHECK(args[0]->IsInt32());
  const int fd = args[0].As<Int32>()->Value();

  const int64_t pos = GetOffset(args[2]);

  const auto enc = ParseEncoding(isolate, args[3], UTF8);

  Local<Value> value = args[1];
  char* buf = nullptr;
  size_t len;

  FSReqBase* req_wrap_async = GetReqWrap(args, 4);
  const bool is_async = req_wrap_async != nullptr;

  // Avoid copying the string when it is externalized but only when:
  // 1. The target encoding is compatible with the string's encoding, and
  // 2. The write is synchronous, otherwise the string might get neutered
  //    while the request is in flight, and
  // 3. For UCS2, when the host system is little-endian.  Big-endian systems
  //    need to call StringBytes::Write() to ensure proper byte swapping.
  // The const_casts are conceptually sound: memory is read but not written.
  if (!is_async && value->IsString()) {
    auto string = value.As<String>();
    if ((enc == ASCII || enc == LATIN1) && string->IsExternalOneByte()) {
      auto ext = string->GetExternalOneByteStringResource();
      buf = const_cast<char*>(ext->data());
      len = ext->length();
    } else if (enc == UCS2 && string->IsExternalTwoByte()) {
      if constexpr (IsLittleEndian()) {
        auto ext = string->GetExternalStringResource();
        buf = reinterpret_cast<char*>(const_cast<uint16_t*>(ext->data()));
        len = ext->length() * sizeof(*ext->data());
      }
    }
  }

  if (is_async) {  // write(fd, string, pos, enc, req)
    CHECK_NOT_NULL(req_wrap_async);
    if (!StringBytes::StorageSize(isolate, value, enc).To(&len)) return;
    FSReqBase::FSReqBuffer& stack_buffer =
        req_wrap_async->Init("write", len, enc);
    // StorageSize may return too large a char, so correct the actual length
    // by the write size
    len = StringBytes::Write(isolate, *stack_buffer, len, args[1], enc);
    stack_buffer.SetLengthAndZeroTerminate(len);
    uv_buf_t uvbuf = uv_buf_init(*stack_buffer, len);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_WRITE, req_wrap_async)
    int err = req_wrap_async->Dispatch(uv_fs_write,
                                       fd,
                                       &uvbuf,
                                       1,
                                       pos,
                                       AfterInteger);
    if (err < 0) {
      uv_fs_t* uv_req = req_wrap_async->req();
      uv_req->result = err;
      uv_req->path = nullptr;
      AfterInteger(uv_req);  // after may delete req_wrap_async if there is
                             // an error
    } else {
      req_wrap_async->SetReturnValue(args);
    }
  } else {  // write(fd, string, pos, enc, undefined, ctx)
    CHECK_EQ(argc, 6);
    FSReqBase::FSReqBuffer stack_buffer;
    if (buf == nullptr) {
      if (!StringBytes::StorageSize(isolate, value, enc).To(&len))
        return;
      stack_buffer.AllocateSufficientStorage(len + 1);
      // StorageSize may return too large a char, so correct the actual length
      // by the write size
      len = StringBytes::Write(isolate, *stack_buffer,
                               len, args[1], enc);
      stack_buffer.SetLengthAndZeroTerminate(len);
      buf = *stack_buffer;
    }
    uv_buf_t uvbuf = uv_buf_init(buf, len);
    FSReqWrapSync req_wrap_sync("write");
    FS_SYNC_TRACE_BEGIN(write);
    int bytesWritten = SyncCall(env, args[5], &req_wrap_sync, "write",
                                uv_fs_write, fd, &uvbuf, 1, pos);
    FS_SYNC_TRACE_END(write, "bytesWritten", bytesWritten);
    args.GetReturnValue().Set(bytesWritten);
  }
}

static void WriteFileUtf8(const FunctionCallbackInfo<Value>& args) {
  // Fast C++ path for fs.writeFileSync(path, data) with utf8 encoding
  // (file, data, options.flag, options.mode)

  Environment* env = Environment::GetCurrent(args);
  auto isolate = env->isolate();

  CHECK_EQ(args.Length(), 4);

  BufferValue value(isolate, args[1]);
  CHECK_NOT_NULL(*value);

  CHECK(args[2]->IsInt32());
  const int flags = args[2].As<Int32>()->Value();

  CHECK(args[3]->IsInt32());
  const int mode = args[3].As<Int32>()->Value();

  uv_file file;

  bool is_fd = args[0]->IsInt32();

  // Check for file descriptor
  if (is_fd) {
    file = args[0].As<Int32>()->Value();
  } else {
    BufferValue path(isolate, args[0]);
    CHECK_NOT_NULL(*path);
    ToNamespacedPath(env, &path);
    if (CheckOpenPermissions(env, path, flags).IsNothing()) return;

    FSReqWrapSync req_open("open", *path);

    FS_SYNC_TRACE_BEGIN(open);
    file =
        SyncCallAndThrowOnError(env, &req_open, uv_fs_open, *path, flags, mode);
    FS_SYNC_TRACE_END(open);

    if (is_uv_error(file)) {
      return;
    }
  }

  int bytesWritten = 0;
  uint32_t offset = 0;

  const size_t length = value.length();
  uv_buf_t uvbuf = uv_buf_init(value.out(), length);

  FS_SYNC_TRACE_BEGIN(write);
  while (offset < length) {
    FSReqWrapSync req_write("write");
    bytesWritten = SyncCallAndThrowOnError(
        env, &req_write, uv_fs_write, file, &uvbuf, 1, -1);

    // Write errored out
    if (bytesWritten < 0) {
      break;
    }

    offset += bytesWritten;
    DCHECK_LE(offset, length);
    uvbuf.base += bytesWritten;
    uvbuf.len -= bytesWritten;
  }
  FS_SYNC_TRACE_END(write);

  if (!is_fd) {
    FSReqWrapSync req_close("close");

    FS_SYNC_TRACE_BEGIN(close);
    int result = SyncCallAndThrowOnError(env, &req_close, uv_fs_close, file);
    FS_SYNC_TRACE_END(close);

    if (is_uv_error(result)) {
      return;
    }
  }
}

/*
 * Wrapper for read(2).
 *
 * bytesRead = fs.read(fd, buffer, offset, length, position)
 *
 * 0 fd        int32. file descriptor
 * 1 buffer    instance of Buffer
 * 2 offset    int64. offset to start reading into inside buffer
 * 3 length    int32. length to read
 * 4 position  int64. file position - -1 for current position
 */
static void Read(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 5);

  CHECK(args[0]->IsInt32());
  const int fd = args[0].As<Int32>()->Value();

  CHECK(Buffer::HasInstance(args[1]));
  Local<Object> buffer_obj = args[1].As<Object>();
  char* buffer_data = Buffer::Data(buffer_obj);
  size_t buffer_length = Buffer::Length(buffer_obj);

  CHECK(IsSafeJsInt(args[2]));
  const int64_t off_64 = args[2].As<Integer>()->Value();
  CHECK_GE(off_64, 0);
  CHECK_LT(static_cast<uint64_t>(off_64), buffer_length);
  const size_t off = static_cast<size_t>(off_64);

  CHECK(args[3]->IsInt32());
  const size_t len = static_cast<size_t>(args[3].As<Int32>()->Value());
  CHECK(Buffer::IsWithinBounds(off, len, buffer_length));

  CHECK(IsSafeJsInt(args[4]) || args[4]->IsBigInt());
  const int64_t pos = args[4]->IsNumber() ?
                      args[4].As<Integer>()->Value() :
                      args[4].As<BigInt>()->Int64Value();

  char* buf = buffer_data + off;
  uv_buf_t uvbuf = uv_buf_init(buf, len);

  if (argc > 5) {  // read(fd, buffer, offset, len, pos, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 5);
    CHECK_NOT_NULL(req_wrap_async);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_READ, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "read", UTF8, AfterInteger,
              uv_fs_read, fd, &uvbuf, 1, pos);
  } else {  // read(fd, buffer, offset, len, pos)
    FSReqWrapSync req_wrap_sync("read");
    FS_SYNC_TRACE_BEGIN(read);
    const int bytesRead = SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_read, fd, &uvbuf, 1, pos);
    FS_SYNC_TRACE_END(read, "bytesRead", bytesRead);

    if (is_uv_error(bytesRead)) {
      return;
    }

    args.GetReturnValue().Set(bytesRead);
  }
}

static void ReadFileUtf8(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  auto isolate = env->isolate();

  CHECK_GE(args.Length(), 2);

  CHECK(args[1]->IsInt32());
  const int flags = args[1].As<Int32>()->Value();

  uv_file file;
  uv_fs_t req;

  bool is_fd = args[0]->IsInt32();

  // Check for file descriptor
  if (is_fd) {
    file = args[0].As<Int32>()->Value();
  } else {
    BufferValue path(env->isolate(), args[0]);
    CHECK_NOT_NULL(*path);
    ToNamespacedPath(env, &path);
    if (CheckOpenPermissions(env, path, flags).IsNothing()) return;

    FS_SYNC_TRACE_BEGIN(open);
    file = uv_fs_open(nullptr, &req, *path, flags, 0666, nullptr);
    FS_SYNC_TRACE_END(open);
    if (req.result < 0) {
      uv_fs_req_cleanup(&req);
      // req will be cleaned up by scope leave.
      return env->ThrowUVException(
          static_cast<int>(req.result), "open", nullptr, path.out());
    }
  }

  auto defer_close = OnScopeLeave([file, is_fd, &req]() {
    if (!is_fd) {
      FS_SYNC_TRACE_BEGIN(close);
      CHECK_EQ(0, uv_fs_close(nullptr, &req, file, nullptr));
      FS_SYNC_TRACE_END(close);
    }
    uv_fs_req_cleanup(&req);
  });

  std::string result{};
  char buffer[8192];
  uv_buf_t buf = uv_buf_init(buffer, sizeof(buffer));

  FS_SYNC_TRACE_BEGIN(read);
  while (true) {
    auto r = uv_fs_read(nullptr, &req, file, &buf, 1, -1, nullptr);
    if (req.result < 0) {
      FS_SYNC_TRACE_END(read);
      // req will be cleaned up by scope leave.
      return env->ThrowUVException(
          static_cast<int>(req.result), "read", nullptr);
    }
    if (r <= 0) {
      break;
    }
    result.append(buf.base, r);
  }
  FS_SYNC_TRACE_END(read);

  Local<Value> val;
  if (!ToV8Value(env->context(), result, isolate).ToLocal(&val)) {
    return;
  }

  args.GetReturnValue().Set(val);
}

// Wrapper for readv(2).
//
// bytesRead = fs.readv(fd, buffers[, position], callback)
// 0 fd        integer. file descriptor
// 1 buffers   array of buffers to read
// 2 position  if integer, position to read at in the file.
//             if null, read from the current position
static void ReadBuffers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  CHECK(args[0]->IsInt32());
  const int fd = args[0].As<Int32>()->Value();

  CHECK(args[1]->IsArray());
  Local<Array> buffers = args[1].As<Array>();

  int64_t pos = GetOffset(args[2]);  // -1 if not a valid JS int

  MaybeStackBuffer<uv_buf_t> iovs(buffers->Length());

  // Init uv buffers from ArrayBufferViews
  for (uint32_t i = 0; i < iovs.length(); i++) {
    Local<Value> buffer;
    if (!buffers->Get(env->context(), i).ToLocal(&buffer)) return;
    CHECK(Buffer::HasInstance(buffer));
    iovs[i] = uv_buf_init(Buffer::Data(buffer), Buffer::Length(buffer));
  }

  if (argc > 3) {  // readBuffers(fd, buffers, pos, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_READ, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "read", UTF8, AfterInteger,
              uv_fs_read, fd, *iovs, iovs.length(), pos);
  } else {  // readBuffers(fd, buffers, undefined, ctx)
    FSReqWrapSync req_wrap_sync("read");
    FS_SYNC_TRACE_BEGIN(read);
    int bytesRead = SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_read, fd, *iovs, iovs.length(), pos);
    FS_SYNC_TRACE_END(read, "bytesRead", bytesRead);
    if (is_uv_error(bytesRead)) {
      return;
    }
    args.GetReturnValue().Set(bytesRead);
  }
}


/* fs.chmod(path, mode);
 * Wrapper for chmod(1) / EIO_CHMOD
 */
static void Chmod(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  CHECK(args[1]->IsInt32());
  int mode = args[1].As<Int32>()->Value();

  if (argc > 2) {  // chmod(path, mode, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_CHMOD, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "chmod", UTF8, AfterNoArgs,
              uv_fs_chmod, *path, mode);
  } else {  // chmod(path, mode)
    FSReqWrapSync req_wrap_sync("chmod", *path);
    FS_SYNC_TRACE_BEGIN(chmod);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_chmod, *path, mode);
    FS_SYNC_TRACE_END(chmod);
  }
}


/* fs.fchmod(fd, mode);
 * Wrapper for fchmod(1) / EIO_FCHMOD
 */
static void FChmod(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  CHECK(args[1]->IsInt32());
  const int mode = args[1].As<Int32>()->Value();

  if (argc > 2) {  // fchmod(fd, mode, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FCHMOD, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "fchmod", UTF8, AfterNoArgs,
              uv_fs_fchmod, fd, mode);
  } else {  // fchmod(fd, mode)
    FSReqWrapSync req_wrap_sync("fchmod");
    FS_SYNC_TRACE_BEGIN(fchmod);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_fchmod, fd, mode);
    FS_SYNC_TRACE_END(fchmod);
  }
}

/* fs.chown(path, uid, gid);
 * Wrapper for chown(1) / EIO_CHOWN
 */
static void Chown(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  CHECK(IsSafeJsInt(args[1]));
  const uv_uid_t uid = static_cast<uv_uid_t>(args[1].As<Integer>()->Value());

  CHECK(IsSafeJsInt(args[2]));
  const uv_gid_t gid = static_cast<uv_gid_t>(args[2].As<Integer>()->Value());

  if (argc > 3) {  // chown(path, uid, gid, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    CHECK_NOT_NULL(req_wrap_async);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_CHOWN, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "chown", UTF8, AfterNoArgs,
              uv_fs_chown, *path, uid, gid);
  } else {  // chown(path, uid, gid)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    FSReqWrapSync req_wrap_sync("chown", *path);
    FS_SYNC_TRACE_BEGIN(chown);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_chown, *path, uid, gid);
    FS_SYNC_TRACE_END(chown);
  }
}


/* fs.fchown(fd, uid, gid);
 * Wrapper for fchown(1) / EIO_FCHOWN
 */
static void FChown(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  CHECK(IsSafeJsInt(args[1]));
  const uv_uid_t uid = static_cast<uv_uid_t>(args[1].As<Integer>()->Value());

  CHECK(IsSafeJsInt(args[2]));
  const uv_gid_t gid = static_cast<uv_gid_t>(args[2].As<Integer>()->Value());

  if (argc > 3) {  // fchown(fd, uid, gid, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FCHOWN, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "fchown", UTF8, AfterNoArgs,
              uv_fs_fchown, fd, uid, gid);
  } else {  // fchown(fd, uid, gid)
    FSReqWrapSync req_wrap_sync("fchown");
    FS_SYNC_TRACE_BEGIN(fchown);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_fchown, fd, uid, gid);
    FS_SYNC_TRACE_END(fchown);
  }
}


static void LChown(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);

  CHECK(IsSafeJsInt(args[1]));
  const uv_uid_t uid = static_cast<uv_uid_t>(args[1].As<Integer>()->Value());

  CHECK(IsSafeJsInt(args[2]));
  const uv_gid_t gid = static_cast<uv_gid_t>(args[2].As<Integer>()->Value());

  if (argc > 3) {  // lchown(path, uid, gid, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_LCHOWN, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "lchown", UTF8, AfterNoArgs,
              uv_fs_lchown, *path, uid, gid);
  } else {  // lchown(path, uid, gid)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        path.ToStringView());
    FSReqWrapSync req_wrap_sync("lchown", *path);
    FS_SYNC_TRACE_BEGIN(lchown);
    SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_lchown, *path, uid, gid);
    FS_SYNC_TRACE_END(lchown);
  }
}


static void UTimes(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  CHECK(args[1]->IsNumber());
  const double atime = args[1].As<Number>()->Value();

  CHECK(args[2]->IsNumber());
  const double mtime = args[2].As<Number>()->Value();

  if (argc > 3) {  // utimes(path, atime, mtime, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_UTIME, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "utime", UTF8, AfterNoArgs,
              uv_fs_utime, *path, atime, mtime);
  } else {  // utimes(path, atime, mtime)
    FSReqWrapSync req_wrap_sync("utime", *path);
    FS_SYNC_TRACE_BEGIN(utimes);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_utime, *path, atime, mtime);
    FS_SYNC_TRACE_END(utimes);
  }
}

static void FUTimes(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  int fd;
  if (!GetValidatedFd(env, args[0]).To(&fd)) {
    return;
  }

  CHECK(args[1]->IsNumber());
  const double atime = args[1].As<Number>()->Value();

  CHECK(args[2]->IsNumber());
  const double mtime = args[2].As<Number>()->Value();

  if (argc > 3) {  // futimes(fd, atime, mtime, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN0(UV_FS_FUTIME, req_wrap_async)
    AsyncCall(env, req_wrap_async, args, "futime", UTF8, AfterNoArgs,
              uv_fs_futime, fd, atime, mtime);
  } else {  // futimes(fd, atime, mtime)
    FSReqWrapSync req_wrap_sync("futime");
    FS_SYNC_TRACE_BEGIN(futimes);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_futime, fd, atime, mtime);
    FS_SYNC_TRACE_END(futimes);
  }
}

static void LUTimes(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const int argc = args.Length();
  CHECK_GE(argc, 3);

  BufferValue path(env->isolate(), args[0]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());

  CHECK(args[1]->IsNumber());
  const double atime = args[1].As<Number>()->Value();

  CHECK(args[2]->IsNumber());
  const double mtime = args[2].As<Number>()->Value();

  if (argc > 3) {  // lutimes(path, atime, mtime, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 3);
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_LUTIME, req_wrap_async, "path", TRACE_STR_COPY(*path))
    AsyncCall(env, req_wrap_async, args, "lutime", UTF8, AfterNoArgs,
              uv_fs_lutime, *path, atime, mtime);
  } else {  // lutimes(path, atime, mtime)
    FSReqWrapSync req_wrap_sync("lutime", *path);
    FS_SYNC_TRACE_BEGIN(lutimes);
    SyncCallAndThrowOnError(
        env, &req_wrap_sync, uv_fs_lutime, *path, atime, mtime);
    FS_SYNC_TRACE_END(lutimes);
  }
}

static void Mkdtemp(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  const int argc = args.Length();
  CHECK_GE(argc, 2);

  BufferValue tmpl(isolate, args[0]);
  static constexpr const char* const suffix = "XXXXXX";
  const auto length = tmpl.length();
  tmpl.AllocateSufficientStorage(length + strlen(suffix));
  snprintf(tmpl.out() + length, tmpl.length(), "%s", suffix);

  CHECK_NOT_NULL(*tmpl);

  const enum encoding encoding = ParseEncoding(isolate, args[1], UTF8);

  if (argc > 2) {  // mkdtemp(tmpl, encoding, req)
    FSReqBase* req_wrap_async = GetReqWrap(args, 2);
    ASYNC_THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        req_wrap_async,
        permission::PermissionScope::kFileSystemWrite,
        tmpl.ToStringView());
    FS_ASYNC_TRACE_BEGIN1(
        UV_FS_MKDTEMP, req_wrap_async, "path", TRACE_STR_COPY(*tmpl))
    AsyncCall(env, req_wrap_async, args, "mkdtemp", encoding, AfterStringPath,
              uv_fs_mkdtemp, *tmpl);
  } else {  // mkdtemp(tmpl, encoding)
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        tmpl.ToStringView());
    FSReqWrapSync req_wrap_sync("mkdtemp", *tmpl);
    FS_SYNC_TRACE_BEGIN(mkdtemp);
    int result =
        SyncCallAndThrowOnError(env, &req_wrap_sync, uv_fs_mkdtemp, *tmpl);
    FS_SYNC_TRACE_END(mkdtemp);
    if (is_uv_error(result)) {
      return;
    }
    Local<Value> error;
    Local<Value> ret;
    if (!StringBytes::Encode(isolate, req_wrap_sync.req.path, encoding, &error)
             .ToLocal(&ret)) {
      DCHECK(!error.IsEmpty());
      env->isolate()->ThrowException(error);
      return;
    }
    args.GetReturnValue().Set(ret);
  }
}

static void GetFormatOfExtensionlessFile(
    const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());

  Environment* env = Environment::GetCurrent(args);
  BufferValue input(args.GetIsolate(), args[0]);
  ToNamespacedPath(env, &input);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, input.ToStringView());

  uv_fs_t req;
  FS_SYNC_TRACE_BEGIN(open)
  uv_file file = uv_fs_open(nullptr, &req, input.out(), O_RDONLY, 0, nullptr);
  FS_SYNC_TRACE_END(open);

  if (req.result < 0) {
    return args.GetReturnValue().Set(EXTENSIONLESS_FORMAT_JAVASCRIPT);
  }

  auto cleanup = OnScopeLeave([&req, &file]() {
    FS_SYNC_TRACE_BEGIN(close);
    CHECK_EQ(0, uv_fs_close(nullptr, &req, file, nullptr));
    FS_SYNC_TRACE_END(close);
    uv_fs_req_cleanup(&req);
  });

  char buffer[4];
  uv_buf_t buf = uv_buf_init(buffer, sizeof(buffer));
  int err = uv_fs_read(nullptr, &req, file, &buf, 1, 0, nullptr);

  if (err < 0) {
    return args.GetReturnValue().Set(EXTENSIONLESS_FORMAT_JAVASCRIPT);
  }

  // We do this by taking advantage of the fact that all Wasm files start with
  // the header `0x00 0x61 0x73 0x6d`
  if (buffer[0] == 0x00 && buffer[1] == 0x61 && buffer[2] == 0x73 &&
      buffer[3] == 0x6d) {
    return args.GetReturnValue().Set(EXTENSIONLESS_FORMAT_WASM);
  }

  return args.GetReturnValue().Set(EXTENSIONLESS_FORMAT_JAVASCRIPT);
}

#ifdef _WIN32
#define BufferValueToPath(str)                                                 \
  std::filesystem::path(ConvertToWideString(str.ToString(), CP_UTF8))

std::string ConvertWideToUTF8(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();

  int size_needed = WideCharToMultiByte(CP_UTF8,
                                        0,
                                        &wstr[0],
                                        static_cast<int>(wstr.size()),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8,
                      0,
                      &wstr[0],
                      static_cast<int>(wstr.size()),
                      &strTo[0],
                      size_needed,
                      nullptr,
                      nullptr);
  return strTo;
}

#define PathToString(path) ConvertWideToUTF8(path.wstring());

#else  // _WIN32

#define BufferValueToPath(str) std::filesystem::path(str.ToStringView());
#define PathToString(path) path.native();

#endif  // _WIN32

static void CpSyncCheckPaths(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_EQ(args.Length(), 4);  // src, dest, dereference, recursive

  BufferValue src(isolate, args[0]);
  CHECK_NOT_NULL(*src);
  ToNamespacedPath(env, &src);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, src.ToStringView());

  auto src_path = BufferValueToPath(src);

  BufferValue dest(isolate, args[1]);
  CHECK_NOT_NULL(*dest);
  ToNamespacedPath(env, &dest);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, dest.ToStringView());

  auto dest_path = BufferValueToPath(dest);
  bool dereference = args[2]->IsTrue();
  bool recursive = args[3]->IsTrue();

  std::error_code error_code;
  auto src_status = dereference
                        ? std::filesystem::symlink_status(src_path, error_code)
                        : std::filesystem::status(src_path, error_code);

  if (error_code) {
#ifdef _WIN32
    int errorno = uv_translate_sys_error(error_code.value());
#else
    int errorno =
        error_code.value() > 0 ? -error_code.value() : error_code.value();
#endif
    return env->ThrowUVException(
        errorno, dereference ? "stat" : "lstat", nullptr, src.out());
  }
  auto dest_status =
      dereference ? std::filesystem::symlink_status(dest_path, error_code)
                  : std::filesystem::status(dest_path, error_code);

  bool dest_exists = !error_code && dest_status.type() !=
                                        std::filesystem::file_type::not_found;
  bool src_is_dir =
      (src_status.type() == std::filesystem::file_type::directory) ||
      (dereference && src_status.type() == std::filesystem::file_type::symlink);

  auto src_path_str = PathToString(src_path);
  auto dest_path_str = PathToString(dest_path);

  if (!error_code) {
    // Check if src and dest are identical.
    if (std::filesystem::equivalent(src_path, dest_path)) {
      std::string message = "src and dest cannot be the same %s";
      return THROW_ERR_FS_CP_EINVAL(env, message.c_str(), dest_path_str);
    }

    const bool dest_is_dir =
        dest_status.type() == std::filesystem::file_type::directory;
    if (src_is_dir && !dest_is_dir) {
      std::string message =
          "Cannot overwrite non-directory %s with directory %s";
      return THROW_ERR_FS_CP_DIR_TO_NON_DIR(
          env, message.c_str(), src_path_str, dest_path_str);
    }

    if (!src_is_dir && dest_is_dir) {
      std::string message =
          "Cannot overwrite directory %s with non-directory %s";
      return THROW_ERR_FS_CP_NON_DIR_TO_DIR(
          env, message.c_str(), dest_path_str, src_path_str);
    }
  }

  if (!src_path_str.ends_with(std::filesystem::path::preferred_separator)) {
    src_path_str += std::filesystem::path::preferred_separator;
  }
  // Check if dest_path is a subdirectory of src_path.
  if (src_is_dir && dest_path_str.starts_with(src_path_str)) {
    std::string message = "Cannot copy %s to a subdirectory of self %s";
    return THROW_ERR_FS_CP_EINVAL(
        env, message.c_str(), src_path_str, dest_path_str);
  }

  auto dest_parent = dest_path.parent_path();
  // "/" parent is itself. Therefore, we need to check if the parent is the same
  // as itself.
  while (src_path.parent_path() != dest_parent &&
         dest_parent.has_parent_path() &&
         dest_parent.parent_path() != dest_parent) {
    if (std::filesystem::equivalent(
            src_path, dest_path.parent_path(), error_code)) {
      std::string message = "Cannot copy %s to a subdirectory of self %s";
      return THROW_ERR_FS_CP_EINVAL(
          env, message.c_str(), src_path_str, dest_path_str);
    }

    // If equivalent fails, it's highly likely that dest_parent does not exist
    if (error_code) {
      break;
    }

    dest_parent = dest_parent.parent_path();
  }

  if (src_is_dir && !recursive) {
    std::string message =
        "Recursive option not enabled, cannot copy a directory: %s";
    return THROW_ERR_FS_EISDIR(env, message.c_str(), src_path_str);
  }

  switch (src_status.type()) {
    case std::filesystem::file_type::socket: {
      std::string message = "Cannot copy a socket file: %s";
      return THROW_ERR_FS_CP_SOCKET(env, message.c_str(), dest_path_str);
    }
    case std::filesystem::file_type::fifo: {
      std::string message = "Cannot copy a FIFO pipe: %s";
      return THROW_ERR_FS_CP_FIFO_PIPE(env, message.c_str(), dest_path_str);
    }
    case std::filesystem::file_type::unknown: {
      std::string message = "Cannot copy an unknown file type: %s";
      return THROW_ERR_FS_CP_UNKNOWN(env, message.c_str(), dest_path_str);
    }
    default:
      break;
  }

  // Optimization opportunity: Check if this "exists" call is good for
  // performance.
  if (!dest_exists || !std::filesystem::exists(dest_path.parent_path())) {
    std::filesystem::create_directories(dest_path.parent_path(), error_code);
  }
}

BindingData::FilePathIsFileReturnType BindingData::FilePathIsFile(
    Environment* env, const std::string& file_path) {
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env,
      permission::PermissionScope::kFileSystemRead,
      file_path,
      BindingData::FilePathIsFileReturnType::kThrowInsufficientPermissions);

  uv_fs_t req;

  int rc = uv_fs_stat(env->event_loop(), &req, file_path.c_str(), nullptr);

  if (rc == 0) {
    const uv_stat_t* const s = static_cast<const uv_stat_t*>(req.ptr);
    rc = S_ISDIR(s->st_mode);
  }

  uv_fs_req_cleanup(&req);

  // rc is 0 if the path refers to a file
  if (rc == 0) return BindingData::FilePathIsFileReturnType::kIsFile;

  return BindingData::FilePathIsFileReturnType::kIsNotFile;
}

namespace {

// define the final index of the algorithm resolution
// when packageConfig.main is defined.
constexpr uint8_t legacy_main_extensions_with_main_end = 7;
// define the final index of the algorithm resolution
// when packageConfig.main is NOT defined
constexpr uint8_t legacy_main_extensions_package_fallback_end = 10;
// the possible file extensions that should be tested
// 0-6: when packageConfig.main is defined
// 7-9: when packageConfig.main is NOT defined,
//      or when the previous case didn't found the file
constexpr std::array<std::string_view, 10> legacy_main_extensions = {
    "",
    ".js",
    ".json",
    ".node",
    "/index.js",
    "/index.json",
    "/index.node",
    ".js",
    ".json",
    ".node"};

}  // namespace

void BindingData::LegacyMainResolve(const FunctionCallbackInfo<Value>& args) {
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());

  Environment* env = Environment::GetCurrent(args);
  auto isolate = env->isolate();

  auto utf8_package_path = Utf8Value(isolate, args[0]).ToString();

  std::string package_initial_file = "";

  std::optional<std::string> initial_file_path;
  std::string file_path;

  if (args.Length() >= 2 && args[1]->IsString()) {
    auto package_config_main = Utf8Value(isolate, args[1]).ToString();

    initial_file_path =
        PathResolve(env, {utf8_package_path, package_config_main});
    FromNamespacedPath(&initial_file_path.value());

    package_initial_file = *initial_file_path;

    for (int i = 0; i < legacy_main_extensions_with_main_end; i++) {
      file_path = *initial_file_path + std::string(legacy_main_extensions[i]);
      // TODO(anonrig): Remove this when ToNamespacedPath supports std::string
      Local<Value> local_file_path;
      if (!Buffer::Copy(env->isolate(), file_path.c_str(), file_path.size())
               .ToLocal(&local_file_path)) {
        return;
      }
      BufferValue buff_file_path(isolate, local_file_path);
      ToNamespacedPath(env, &buff_file_path);

      switch (FilePathIsFile(env, buff_file_path.ToString())) {
        case BindingData::FilePathIsFileReturnType::kIsFile:
          return args.GetReturnValue().Set(i);
        case BindingData::FilePathIsFileReturnType::kIsNotFile:
          continue;
        case BindingData::FilePathIsFileReturnType::
            kThrowInsufficientPermissions:
          // the default behavior when do not have permission is to return
          // and exit the execution of the method as soon as possible
          // the internal function will throw the exception
          return;
        default:
          UNREACHABLE();
      }
    }
  }

  initial_file_path = PathResolve(env, {utf8_package_path, "./index"});
  if (!initial_file_path.has_value()) {
    return;
  }

  FromNamespacedPath(&initial_file_path.value());

  for (int i = legacy_main_extensions_with_main_end;
       i < legacy_main_extensions_package_fallback_end;
       i++) {
    file_path = *initial_file_path + std::string(legacy_main_extensions[i]);
    // TODO(anonrig): Remove this when ToNamespacedPath supports std::string
    Local<Value> local_file_path;
    if (!Buffer::Copy(env->isolate(), file_path.c_str(), file_path.size())
             .ToLocal(&local_file_path)) {
      return;
    }
    BufferValue buff_file_path(isolate, local_file_path);
    ToNamespacedPath(env, &buff_file_path);

    switch (FilePathIsFile(env, buff_file_path.ToString())) {
      case BindingData::FilePathIsFileReturnType::kIsFile:
        return args.GetReturnValue().Set(i);
      case BindingData::FilePathIsFileReturnType::kIsNotFile:
        continue;
      case BindingData::FilePathIsFileReturnType::kThrowInsufficientPermissions:
        // the default behavior when do not have permission is to return
        // and exit the execution of the method as soon as possible
        // the internal function will throw the exception
        return;
      default:
        UNREACHABLE();
    }
  }

  if (package_initial_file == "")
    package_initial_file = *initial_file_path + ".js";

  std::optional<std::string> module_base;

  if (args.Length() >= 3 && args[2]->IsString()) {
    Utf8Value utf8_base_path(isolate, args[2]);
    auto base_url =
        ada::parse<ada::url_aggregator>(utf8_base_path.ToStringView());

    if (!base_url) {
      THROW_ERR_INVALID_URL(isolate, "Invalid URL");
      return;
    }

    module_base = node::url::FileURLToPath(env, *base_url);
    if (!module_base.has_value()) {
      return;
    }
  } else {
    THROW_ERR_INVALID_ARG_TYPE(
        isolate,
        "The \"base\" argument must be of type string or an instance of URL.");
    return;
  }

  THROW_ERR_MODULE_NOT_FOUND(isolate,
                             "Cannot find package '%s' imported from %s",
                             package_initial_file,
                             *module_base);
}

void BindingData::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("stats_field_array", stats_field_array);
  tracker->TrackField("stats_field_bigint_array", stats_field_bigint_array);
  tracker->TrackField("statfs_field_array", statfs_field_array);
  tracker->TrackField("statfs_field_bigint_array", statfs_field_bigint_array);
  tracker->TrackField("file_handle_read_wrap_freelist",
                      file_handle_read_wrap_freelist);
}

BindingData::BindingData(Realm* realm,
                         v8::Local<v8::Object> wrap,
                         InternalFieldInfo* info)
    : SnapshotableObject(realm, wrap, type_int),
      stats_field_array(realm->isolate(),
                        kFsStatsBufferLength,
                        MAYBE_FIELD_PTR(info, stats_field_array)),
      stats_field_bigint_array(realm->isolate(),
                               kFsStatsBufferLength,
                               MAYBE_FIELD_PTR(info, stats_field_bigint_array)),
      statfs_field_array(realm->isolate(),
                         kFsStatFsBufferLength,
                         MAYBE_FIELD_PTR(info, statfs_field_array)),
      statfs_field_bigint_array(
          realm->isolate(),
          kFsStatFsBufferLength,
          MAYBE_FIELD_PTR(info, statfs_field_bigint_array)) {
  Isolate* isolate = realm->isolate();
  Local<Context> context = realm->context();

  if (info == nullptr) {
    wrap->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "statValues"),
              stats_field_array.GetJSArray())
        .Check();

    wrap->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "bigintStatValues"),
              stats_field_bigint_array.GetJSArray())
        .Check();

    wrap->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "statFsValues"),
              statfs_field_array.GetJSArray())
        .Check();

    wrap->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "bigintStatFsValues"),
              statfs_field_bigint_array.GetJSArray())
        .Check();
  } else {
    stats_field_array.Deserialize(realm->context());
    stats_field_bigint_array.Deserialize(realm->context());
    statfs_field_array.Deserialize(realm->context());
    statfs_field_bigint_array.Deserialize(realm->context());
  }
  stats_field_array.MakeWeak();
  stats_field_bigint_array.MakeWeak();
  statfs_field_array.MakeWeak();
  statfs_field_bigint_array.MakeWeak();
}

void BindingData::Deserialize(Local<Context> context,
                              Local<Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  InternalFieldInfo* casted_info = static_cast<InternalFieldInfo*>(info);
  BindingData* binding =
      realm->AddBindingData<BindingData>(holder, casted_info);
  CHECK_NOT_NULL(binding);
}

bool BindingData::PrepareForSerialization(Local<Context> context,
                                          v8::SnapshotCreator* creator) {
  CHECK(file_handle_read_wrap_freelist.empty());
  DCHECK_NULL(internal_field_info_);
  internal_field_info_ = InternalFieldInfoBase::New<InternalFieldInfo>(type());
  internal_field_info_->stats_field_array =
      stats_field_array.Serialize(context, creator);
  internal_field_info_->stats_field_bigint_array =
      stats_field_bigint_array.Serialize(context, creator);
  internal_field_info_->statfs_field_array =
      statfs_field_array.Serialize(context, creator);
  internal_field_info_->statfs_field_bigint_array =
      statfs_field_bigint_array.Serialize(context, creator);
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info = internal_field_info_;
  internal_field_info_ = nullptr;
  return info;
}

void BindingData::CreatePerIsolateProperties(IsolateData* isolate_data,
                                             Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(
      isolate, target, "legacyMainResolve", BindingData::LegacyMainResolve);
}

void BindingData::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(BindingData::LegacyMainResolve);
}

static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                       Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(isolate,
            target,
            "getFormatOfExtensionlessFile",
            GetFormatOfExtensionlessFile);
  SetMethod(isolate, target, "access", Access);
  SetMethod(isolate, target, "close", Close);
  SetMethod(isolate, target, "existsSync", ExistsSync);
  SetMethod(isolate, target, "open", Open);
  SetMethod(isolate, target, "openFileHandle", OpenFileHandle);
  SetMethod(isolate, target, "read", Read);
  SetMethod(isolate, target, "readFileUtf8", ReadFileUtf8);
  SetMethod(isolate, target, "readBuffers", ReadBuffers);
  SetMethod(isolate, target, "fdatasync", Fdatasync);
  SetMethod(isolate, target, "fsync", Fsync);
  SetMethod(isolate, target, "rename", Rename);
  SetMethod(isolate, target, "ftruncate", FTruncate);
  SetMethod(isolate, target, "rmdir", RMDir);
  SetMethod(isolate, target, "rmSync", RmSync);
  SetMethod(isolate, target, "mkdir", MKDir);
  SetMethod(isolate, target, "readdir", ReadDir);
  SetFastMethod(isolate,
                target,
                "internalModuleStat",
                InternalModuleStat,
                &fast_internal_module_stat_);
  SetMethod(isolate, target, "stat", Stat);
  SetMethod(isolate, target, "lstat", LStat);
  SetMethod(isolate, target, "fstat", FStat);
  SetMethod(isolate, target, "statfs", StatFs);
  SetMethod(isolate, target, "link", Link);
  SetMethod(isolate, target, "symlink", Symlink);
  SetMethod(isolate, target, "readlink", ReadLink);
  SetMethod(isolate, target, "unlink", Unlink);
  SetMethod(isolate, target, "writeBuffer", WriteBuffer);
  SetMethod(isolate, target, "writeBuffers", WriteBuffers);
  SetMethod(isolate, target, "writeString", WriteString);
  SetMethod(isolate, target, "writeFileUtf8", WriteFileUtf8);
  SetMethod(isolate, target, "realpath", RealPath);
  SetMethod(isolate, target, "copyFile", CopyFile);

  SetMethod(isolate, target, "chmod", Chmod);
  SetMethod(isolate, target, "fchmod", FChmod);

  SetMethod(isolate, target, "chown", Chown);
  SetMethod(isolate, target, "fchown", FChown);
  SetMethod(isolate, target, "lchown", LChown);

  SetMethod(isolate, target, "utimes", UTimes);
  SetMethod(isolate, target, "futimes", FUTimes);
  SetMethod(isolate, target, "lutimes", LUTimes);

  SetMethod(isolate, target, "mkdtemp", Mkdtemp);

  SetMethod(isolate, target, "cpSyncCheckPaths", CpSyncCheckPaths);

  StatWatcher::CreatePerIsolateProperties(isolate_data, target);
  BindingData::CreatePerIsolateProperties(isolate_data, target);

  target->Set(
      FIXED_ONE_BYTE_STRING(isolate, "kFsStatsFieldsNumber"),
      Integer::New(isolate,
                   static_cast<int32_t>(FsStatsOffset::kFsStatsFieldsNumber)));

  // Create FunctionTemplate for FSReqCallback
  Local<FunctionTemplate> fst = NewFunctionTemplate(isolate, NewFSReqCallback);
  fst->InstanceTemplate()->SetInternalFieldCount(
      FSReqBase::kInternalFieldCount);
  fst->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  SetConstructorFunction(isolate, target, "FSReqCallback", fst);

  // Create FunctionTemplate for FileHandleReadWrap. There’s no need
  // to do anything in the constructor, so we only store the instance template.
  Local<FunctionTemplate> fh_rw = FunctionTemplate::New(isolate);
  fh_rw->InstanceTemplate()->SetInternalFieldCount(
      FSReqBase::kInternalFieldCount);
  fh_rw->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  Local<String> fhWrapString =
      FIXED_ONE_BYTE_STRING(isolate, "FileHandleReqWrap");
  fh_rw->SetClassName(fhWrapString);
  isolate_data->set_filehandlereadwrap_template(fst->InstanceTemplate());

  // Create Function Template for FSReqPromise
  Local<FunctionTemplate> fpt = FunctionTemplate::New(isolate);
  fpt->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  Local<String> promiseString =
      FIXED_ONE_BYTE_STRING(isolate, "FSReqPromise");
  fpt->SetClassName(promiseString);
  Local<ObjectTemplate> fpo = fpt->InstanceTemplate();
  fpo->SetInternalFieldCount(FSReqBase::kInternalFieldCount);
  isolate_data->set_fsreqpromise_constructor_template(fpo);

  // Create FunctionTemplate for FileHandle
  Local<FunctionTemplate> fd = NewFunctionTemplate(isolate, FileHandle::New);
  fd->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  SetProtoMethod(isolate, fd, "close", FileHandle::Close);
  SetProtoMethod(isolate, fd, "releaseFD", FileHandle::ReleaseFD);
  Local<ObjectTemplate> fdt = fd->InstanceTemplate();
  fdt->SetInternalFieldCount(FileHandle::kInternalFieldCount);
  StreamBase::AddMethods(isolate_data, fd);
  SetConstructorFunction(isolate, target, "FileHandle", fd);
  isolate_data->set_fd_constructor_template(fdt);

  // Create FunctionTemplate for FileHandle::CloseReq
  Local<FunctionTemplate> fdclose = FunctionTemplate::New(isolate);
  fdclose->SetClassName(FIXED_ONE_BYTE_STRING(isolate,
                        "FileHandleCloseReq"));
  fdclose->Inherit(AsyncWrap::GetConstructorTemplate(isolate_data));
  Local<ObjectTemplate> fdcloset = fdclose->InstanceTemplate();
  fdcloset->SetInternalFieldCount(FSReqBase::kInternalFieldCount);
  isolate_data->set_fdclose_constructor_template(fdcloset);

  target->Set(isolate, "kUsePromises", isolate_data->fs_use_promises_symbol());
}

static void CreatePerContextProperties(Local<Object> target,
                                       Local<Value> unused,
                                       Local<Context> context,
                                       void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  realm->AddBindingData<BindingData>(target);
}

BindingData* FSReqBase::binding_data() {
  return binding_data_.get();
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(Access);
  StatWatcher::RegisterExternalReferences(registry);
  BindingData::RegisterExternalReferences(registry);

  registry->Register(GetFormatOfExtensionlessFile);
  registry->Register(Close);
  registry->Register(ExistsSync);
  registry->Register(Open);
  registry->Register(OpenFileHandle);
  registry->Register(Read);
  registry->Register(ReadFileUtf8);
  registry->Register(ReadBuffers);
  registry->Register(Fdatasync);
  registry->Register(Fsync);
  registry->Register(Rename);
  registry->Register(FTruncate);
  registry->Register(RMDir);
  registry->Register(RmSync);
  registry->Register(MKDir);
  registry->Register(ReadDir);
  registry->Register(InternalModuleStat);
  registry->Register(FastInternalModuleStat);
  registry->Register(fast_internal_module_stat_.GetTypeInfo());
  registry->Register(Stat);
  registry->Register(LStat);
  registry->Register(FStat);
  registry->Register(StatFs);
  registry->Register(Link);
  registry->Register(Symlink);
  registry->Register(ReadLink);
  registry->Register(Unlink);
  registry->Register(WriteBuffer);
  registry->Register(WriteBuffers);
  registry->Register(WriteString);
  registry->Register(WriteFileUtf8);
  registry->Register(RealPath);
  registry->Register(CopyFile);

  registry->Register(CpSyncCheckPaths);

  registry->Register(Chmod);
  registry->Register(FChmod);

  registry->Register(Chown);
  registry->Register(FChown);
  registry->Register(LChown);

  registry->Register(UTimes);
  registry->Register(FUTimes);
  registry->Register(LUTimes);

  registry->Register(Mkdtemp);
  registry->Register(NewFSReqCallback);

  registry->Register(FileHandle::New);
  registry->Register(FileHandle::Close);
  registry->Register(FileHandle::ReleaseFD);
  StreamBase::RegisterExternalReferences(registry);
}

}  // namespace fs

}  // end namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(fs, node::fs::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(fs, node::fs::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(fs, node::fs::RegisterExternalReferences)
