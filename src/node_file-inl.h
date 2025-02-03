#ifndef SRC_NODE_FILE_INL_H_
#define SRC_NODE_FILE_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_file.h"
#include "req_wrap-inl.h"

namespace node {
namespace fs {

FSContinuationData::FSContinuationData(uv_fs_t* req, int mode, uv_fs_cb done_cb)
  : done_cb_(done_cb), req_(req), mode_(mode) {
}

void FSContinuationData::PushPath(std::string&& path) {
  paths_.emplace_back(std::move(path));
}

void FSContinuationData::PushPath(const std::string& path) {
  paths_.push_back(path);
}

void FSContinuationData::MaybeSetFirstPath(const std::string& path) {
  if (first_path_.empty()) {
    first_path_ = path;
  }
}

std::string FSContinuationData::PopPath() {
  CHECK(!paths_.empty());
  std::string path = std::move(paths_.back());
  paths_.pop_back();
  return path;
}

void FSContinuationData::Done(int result) {
  req_->result = result;
  done_cb_(req_);
}

FSReqBase::FSReqBase(BindingData* binding_data,
                     v8::Local<v8::Object> req,
                     AsyncWrap::ProviderType type,
                     bool use_bigint)
  : ReqWrap(binding_data->env(), req, type),
    use_bigint_(use_bigint),
    binding_data_(binding_data) {
}

void FSReqBase::Init(const char* syscall,
                     const char* data,
                     size_t len,
                     enum encoding encoding) {
  syscall_ = syscall;
  encoding_ = encoding;

  if (data != nullptr) {
    CHECK(!has_data_);
    buffer_.AllocateSufficientStorage(len + 1);
    buffer_.SetLengthAndZeroTerminate(len);
    memcpy(*buffer_, data, len);
    has_data_ = true;
  }
}

FSReqBase::FSReqBuffer&
FSReqBase::Init(const char* syscall, size_t len, enum encoding encoding) {
  syscall_ = syscall;
  encoding_ = encoding;

  buffer_.AllocateSufficientStorage(len + 1);
  has_data_ = false;  // so that the data does not show up in error messages
  return buffer_;
}

FSReqCallback::FSReqCallback(BindingData* binding_data,
                             v8::Local<v8::Object> req,
                             bool use_bigint)
  : FSReqBase(binding_data,
              req,
              AsyncWrap::PROVIDER_FSREQCALLBACK,
              use_bigint) {}

template <typename NativeT, typename V8T>
void FillStatsArray(AliasedBufferBase<NativeT, V8T>* fields,
                    const uv_stat_t* s,
                    const size_t offset) {
#define SET_FIELD_WITH_STAT(stat_offset, stat)                                 \
  fields->SetValue(offset + static_cast<size_t>(FsStatsOffset::stat_offset),   \
                   static_cast<NativeT>(stat))

// On win32, time is stored in uint64_t and starts from 1601-01-01.
// libuv calculates tv_sec and tv_nsec from it and converts to signed long,
// which causes Y2038 overflow. On the other platforms it is safe to treat
// negative values as pre-epoch time.
#ifdef _WIN32
#define SET_FIELD_WITH_TIME_STAT(stat_offset, stat)                            \
  /* NOLINTNEXTLINE(runtime/int) */                                            \
  SET_FIELD_WITH_STAT(stat_offset, static_cast<unsigned long>(stat))
#else
#define SET_FIELD_WITH_TIME_STAT(stat_offset, stat)                            \
  SET_FIELD_WITH_STAT(stat_offset, static_cast<double>(stat))
#endif  // _WIN32

  SET_FIELD_WITH_STAT(kDev, s->st_dev);
  SET_FIELD_WITH_STAT(kMode, s->st_mode);
  SET_FIELD_WITH_STAT(kNlink, s->st_nlink);
  SET_FIELD_WITH_STAT(kUid, s->st_uid);
  SET_FIELD_WITH_STAT(kGid, s->st_gid);
  SET_FIELD_WITH_STAT(kRdev, s->st_rdev);
  SET_FIELD_WITH_STAT(kBlkSize, s->st_blksize);
  SET_FIELD_WITH_STAT(kIno, s->st_ino);
  SET_FIELD_WITH_STAT(kSize, s->st_size);
  SET_FIELD_WITH_STAT(kBlocks, s->st_blocks);

  SET_FIELD_WITH_TIME_STAT(kATimeSec, s->st_atim.tv_sec);
  SET_FIELD_WITH_TIME_STAT(kATimeNsec, s->st_atim.tv_nsec);
  SET_FIELD_WITH_TIME_STAT(kMTimeSec, s->st_mtim.tv_sec);
  SET_FIELD_WITH_TIME_STAT(kMTimeNsec, s->st_mtim.tv_nsec);
  SET_FIELD_WITH_TIME_STAT(kCTimeSec, s->st_ctim.tv_sec);
  SET_FIELD_WITH_TIME_STAT(kCTimeNsec, s->st_ctim.tv_nsec);
  SET_FIELD_WITH_TIME_STAT(kBirthTimeSec, s->st_birthtim.tv_sec);
  SET_FIELD_WITH_TIME_STAT(kBirthTimeNsec, s->st_birthtim.tv_nsec);

#undef SET_FIELD_WITH_TIME_STAT
#undef SET_FIELD_WITH_STAT
}

v8::Local<v8::Value> FillGlobalStatsArray(BindingData* binding_data,
                                          const bool use_bigint,
                                          const uv_stat_t* s,
                                          const bool second) {
  const ptrdiff_t offset =
      second ? static_cast<ptrdiff_t>(FsStatsOffset::kFsStatsFieldsNumber) : 0;
  if (use_bigint) {
    auto* const arr = &binding_data->stats_field_bigint_array;
    FillStatsArray(arr, s, offset);
    return arr->GetJSArray();
  } else {
    auto* const arr = &binding_data->stats_field_array;
    FillStatsArray(arr, s, offset);
    return arr->GetJSArray();
  }
}

template <typename NativeT, typename V8T>
void FillStatFsArray(AliasedBufferBase<NativeT, V8T>* fields,
                     const uv_statfs_t* s) {
#define SET_FIELD(field, stat)                                                 \
  fields->SetValue(static_cast<size_t>(FsStatFsOffset::field),                 \
                   static_cast<NativeT>(stat))

  SET_FIELD(kType, s->f_type);
  SET_FIELD(kBSize, s->f_bsize);
  SET_FIELD(kBlocks, s->f_blocks);
  SET_FIELD(kBFree, s->f_bfree);
  SET_FIELD(kBAvail, s->f_bavail);
  SET_FIELD(kFiles, s->f_files);
  SET_FIELD(kFFree, s->f_ffree);

#undef SET_FIELD
}

v8::Local<v8::Value> FillGlobalStatFsArray(BindingData* binding_data,
                                           const bool use_bigint,
                                           const uv_statfs_t* s) {
  if (use_bigint) {
    auto* const arr = &binding_data->statfs_field_bigint_array;
    FillStatFsArray(arr, s);
    return arr->GetJSArray();
  } else {
    auto* const arr = &binding_data->statfs_field_array;
    FillStatFsArray(arr, s);
    return arr->GetJSArray();
  }
}

template <typename AliasedBufferT>
FSReqPromise<AliasedBufferT>*
FSReqPromise<AliasedBufferT>::New(BindingData* binding_data,
                                  bool use_bigint) {
  Environment* env = binding_data->env();
  v8::Local<v8::Object> obj;
  if (!env->fsreqpromise_constructor_template()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return nullptr;
  }
  v8::Local<v8::Promise::Resolver> resolver;
  if (!v8::Promise::Resolver::New(env->context()).ToLocal(&resolver) ||
      obj->Set(env->context(), env->promise_string(), resolver).IsNothing()) {
    return nullptr;
  }
  return new FSReqPromise(binding_data, obj, use_bigint);
}

template <typename AliasedBufferT>
FSReqPromise<AliasedBufferT>::~FSReqPromise() {
  // Validate that the promise was explicitly resolved or rejected but only if
  // the Isolate is not terminating because in this case the promise might have
  // not finished.
  CHECK_IMPLIES(!finished_, !env()->can_call_into_js());
}

template <typename AliasedBufferT>
FSReqPromise<AliasedBufferT>::FSReqPromise(BindingData* binding_data,
                                           v8::Local<v8::Object> obj,
                                           bool use_bigint)
    : FSReqBase(
          binding_data, obj, AsyncWrap::PROVIDER_FSREQPROMISE, use_bigint),
      stats_field_array_(
          env()->isolate(),
          static_cast<size_t>(FsStatsOffset::kFsStatsFieldsNumber)),
      statfs_field_array_(
          env()->isolate(),
          static_cast<size_t>(FsStatFsOffset::kFsStatFsFieldsNumber)) {}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::Reject(v8::Local<v8::Value> reject) {
  finished_ = true;
  v8::HandleScope scope(env()->isolate());
  InternalCallbackScope callback_scope(this);
  v8::Local<v8::Value> value;
  if (!object()
           ->Get(env()->context(), env()->promise_string())
           .ToLocal(&value)) {
    // If we hit this, getting the value from the object failed and
    // an error was likely scheduled. We could try to reject the promise
    // but let's just allow the error to propagate.
    return;
  }
  v8::Local<v8::Promise::Resolver> resolver = value.As<v8::Promise::Resolver>();
  USE(resolver->Reject(env()->context(), reject).FromJust());
}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::Resolve(v8::Local<v8::Value> value) {
  finished_ = true;
  v8::HandleScope scope(env()->isolate());
  InternalCallbackScope callback_scope(this);
  v8::Local<v8::Value> val;
  if (!object()->Get(env()->context(), env()->promise_string()).ToLocal(&val)) {
    // If we hit this, getting the value from the object failed and
    // an error was likely scheduled. We could try to reject the promise
    // but let's just allow the error to propagate.
    return;
  }
  v8::Local<v8::Promise::Resolver> resolver = val.As<v8::Promise::Resolver>();
  USE(resolver->Resolve(env()->context(), value).FromJust());
}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::ResolveStat(const uv_stat_t* stat) {
  FillStatsArray(&stats_field_array_, stat);
  Resolve(stats_field_array_.GetJSArray());
}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::ResolveStatFs(const uv_statfs_t* stat) {
  FillStatFsArray(&statfs_field_array_, stat);
  Resolve(statfs_field_array_.GetJSArray());
}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::SetReturnValue(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Local<v8::Value> val;
  if (!object()->Get(env()->context(), env()->promise_string()).ToLocal(&val)) {
    // If we hit this, getting the value from the object failed and
    // an error was likely scheduled. We could try to reject the promise
    // but let's just allow the error to propagate.
    return;
  }
  v8::Local<v8::Promise::Resolver> resolver = val.As<v8::Promise::Resolver>();
  args.GetReturnValue().Set(resolver->GetPromise());
}

template <typename AliasedBufferT>
void FSReqPromise<AliasedBufferT>::MemoryInfo(MemoryTracker* tracker) const {
  FSReqBase::MemoryInfo(tracker);
  tracker->TrackField("stats_field_array", stats_field_array_);
  tracker->TrackField("statfs_field_array", statfs_field_array_);
}

FSReqBase* GetReqWrap(const v8::FunctionCallbackInfo<v8::Value>& args,
                      int index,
                      bool use_bigint) {
  v8::Local<v8::Value> value = args[index];
  if (value->IsObject()) {
    return BaseObject::Unwrap<FSReqBase>(value.As<v8::Object>());
  }

  Realm* realm = Realm::GetCurrent(args);
  BindingData* binding_data = realm->GetBindingData<BindingData>();

  if (value->StrictEquals(realm->isolate_data()->fs_use_promises_symbol())) {
    if (use_bigint) {
      return FSReqPromise<AliasedBigInt64Array>::New(binding_data, use_bigint);
    } else {
      return FSReqPromise<AliasedFloat64Array>::New(binding_data, use_bigint);
    }
  }
  return nullptr;
}

// Returns nullptr if the operation fails from the start.
template <typename Func, typename... Args>
FSReqBase* AsyncDestCall(Environment* env, FSReqBase* req_wrap,
                         const v8::FunctionCallbackInfo<v8::Value>& args,
                         const char* syscall, const char* dest,
                         size_t len, enum encoding enc, uv_fs_cb after,
                         Func fn, Args... fn_args) {
  CHECK_NOT_NULL(req_wrap);
  req_wrap->Init(syscall, dest, len, enc);
  int err = req_wrap->Dispatch(fn, fn_args..., after);
  if (err < 0) {
    uv_fs_t* uv_req = req_wrap->req();
    uv_req->result = err;
    uv_req->path = nullptr;
    after(uv_req);  // after may delete req_wrap if there is an error
    req_wrap = nullptr;
  } else {
    req_wrap->SetReturnValue(args);
  }

  return req_wrap;
}

// Returns nullptr if the operation fails from the start.
template <typename Func, typename... Args>
FSReqBase* AsyncCall(Environment* env,
                     FSReqBase* req_wrap,
                     const v8::FunctionCallbackInfo<v8::Value>& args,
                     const char* syscall, enum encoding enc,
                     uv_fs_cb after, Func fn, Args... fn_args) {
  return AsyncDestCall(env, req_wrap, args,
                       syscall, nullptr, 0, enc,
                       after, fn, fn_args...);
}

// Template counterpart of SYNC_CALL, except that it only puts
// the error number and the syscall in the context instead of
// creating an error in the C++ land.
// ctx must be checked using value->IsObject() before being passed.
template <typename Func, typename... Args>
int SyncCall(Environment* env, v8::Local<v8::Value> ctx,
             FSReqWrapSync* req_wrap, const char* syscall,
             Func fn, Args... args) {
  env->PrintSyncTrace();
  int err = fn(env->event_loop(), &(req_wrap->req), args..., nullptr);
  if (err < 0) {
    v8::Local<v8::Context> context = env->context();
    v8::Local<v8::Object> ctx_obj = ctx.As<v8::Object>();
    v8::Isolate* isolate = env->isolate();
    ctx_obj->Set(context,
                 env->errno_string(),
                 v8::Integer::New(isolate, err)).Check();
    ctx_obj->Set(context,
                 env->syscall_string(),
                 OneByteString(isolate, syscall)).Check();
  }
  return err;
}

// Similar to SyncCall but throws immediately if there is an error.
template <typename Predicate, typename Func, typename... Args>
int SyncCallAndThrowIf(Predicate should_throw,
                       Environment* env,
                       FSReqWrapSync* req_wrap,
                       Func fn,
                       Args... args) {
  env->PrintSyncTrace();
  int result = fn(nullptr, &(req_wrap->req), args..., nullptr);
  if (should_throw(result)) {
    env->ThrowUVException(result,
                          req_wrap->syscall_p,
                          nullptr,
                          req_wrap->path_p,
                          req_wrap->dest_p);
  }
  return result;
}

constexpr bool is_uv_error(int result) {
  return result < 0;
}

// Similar to SyncCall but throws immediately if there is an error.
template <typename Func, typename... Args>
int SyncCallAndThrowOnError(Environment* env,
                            FSReqWrapSync* req_wrap,
                            Func fn,
                            Args... args) {
  return SyncCallAndThrowIf(is_uv_error, env, req_wrap, fn, args...);
}

}  // namespace fs
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_FILE_INL_H_
