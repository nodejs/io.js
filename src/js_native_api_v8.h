#ifndef SRC_JS_NATIVE_API_V8_H_
#define SRC_JS_NATIVE_API_V8_H_

#include "js_native_api_types.h"
#include "js_native_api_v8_internals.h"

inline napi_status napi_clear_last_error(node_api_basic_env env);

namespace v8impl {

class RefTracker {
 public:
  RefTracker() = default;
  virtual ~RefTracker() = default;
  virtual void Finalize() {}

  typedef RefTracker RefList;

  inline void Link(RefList* list) {
    prev_ = list;
    next_ = list->next_;
    if (next_ != nullptr) {
      next_->prev_ = this;
    }
    list->next_ = this;
  }

  inline void Unlink() {
    if (prev_ != nullptr) {
      prev_->next_ = next_;
    }
    if (next_ != nullptr) {
      next_->prev_ = prev_;
    }
    prev_ = nullptr;
    next_ = nullptr;
  }

  static void FinalizeAll(RefList* list) {
    while (list->next_ != nullptr) {
      list->next_->Finalize();
    }
  }

 private:
  RefList* next_ = nullptr;
  RefList* prev_ = nullptr;
};

class Finalizer;
}  // end of namespace v8impl

struct napi_env__ {
  explicit napi_env__(v8::Local<v8::Context> context,
                      int32_t module_api_version)
      : isolate(context->GetIsolate()),
        context_persistent(isolate, context),
        module_api_version(module_api_version) {
    napi_clear_last_error(this);
  }

  inline v8::Local<v8::Context> context() const {
    return v8impl::PersistentToLocal::Strong(context_persistent);
  }

  inline void Ref() { refs++; }
  inline void Unref() {
    if (--refs == 0) DeleteMe();
  }

  virtual bool can_call_into_js() const { return true; }

  static inline void HandleThrow(napi_env env, v8::Local<v8::Value> value) {
    if (env->terminatedOrTerminating()) {
      return;
    }
    env->isolate->ThrowException(value);
  }

  // i.e. whether v8 exited or is about to exit
  inline bool terminatedOrTerminating() {
    return this->isolate->IsExecutionTerminating() || !can_call_into_js();
  }

  // v8 uses a special exception to indicate termination, the
  // `handle_exception` callback should identify such case using
  // terminatedOrTerminating() before actually handle the exception
  template <typename T, typename U = decltype(HandleThrow)>
  inline void CallIntoModule(T&& call, U&& handle_exception = HandleThrow) {
    int open_handle_scopes_before = open_handle_scopes;
    int open_callback_scopes_before = open_callback_scopes;
    napi_clear_last_error(this);
    call(this);
    CHECK_EQ(open_handle_scopes, open_handle_scopes_before);
    CHECK_EQ(open_callback_scopes, open_callback_scopes_before);
    if (!last_exception.IsEmpty()) {
      handle_exception(this, last_exception.Get(this->isolate));
      last_exception.Reset();
    }
  }

  // Call finalizer immediately.
  virtual void CallFinalizer(napi_finalize cb, void* data, void* hint) {
    v8::HandleScope handle_scope(isolate);
    CallIntoModule([&](napi_env env) { cb(env, data, hint); });
  }

  // Invoke finalizer from V8 garbage collector.
  void InvokeFinalizerFromGC(v8impl::RefTracker* finalizer);

  // Enqueue the finalizer to the napi_env's own queue of the second pass
  // weak callback.
  // Implementation should drain the queue at the time it is safe to call
  // into JavaScript.
  virtual void EnqueueFinalizer(v8impl::RefTracker* finalizer) {
    pending_finalizers.emplace(finalizer);
  }

  // Remove the finalizer from the scheduled second pass weak callback queue.
  // The finalizer can be deleted after this call.
  virtual void DequeueFinalizer(v8impl::RefTracker* finalizer) {
    pending_finalizers.erase(finalizer);
  }

  virtual void DeleteMe() {
    // First we must finalize those references that have `napi_finalizer`
    // callbacks. The reason is that addons might store other references which
    // they delete during their `napi_finalizer` callbacks. If we deleted such
    // references here first, they would be doubly deleted when the
    // `napi_finalizer` deleted them subsequently.
    v8impl::RefTracker::FinalizeAll(&finalizing_reflist);
    v8impl::RefTracker::FinalizeAll(&reflist);
    delete this;
  }

  void CheckGCAccess() {
    if (module_api_version == NAPI_VERSION_EXPERIMENTAL && in_gc_finalizer) {
      v8impl::OnFatalError(
          nullptr,
          "Finalizer is calling a function that may affect GC state.\n"
          "The finalizers are run directly from GC and must not affect GC "
          "state.\n"
          "Use `node_api_post_finalizer` from inside of the finalizer to work "
          "around this issue.\n"
          "It schedules the call as a new task in the event loop.");
    }
  }

  v8::Isolate* const isolate;  // Shortcut for context()->GetIsolate()
  v8impl::Persistent<v8::Context> context_persistent;

  v8impl::Persistent<v8::Value> last_exception;

  // We store references in two different lists, depending on whether they have
  // `napi_finalizer` callbacks, because we must first finalize the ones that
  // have such a callback. See `~napi_env__()` above for details.
  v8impl::RefTracker::RefList reflist;
  v8impl::RefTracker::RefList finalizing_reflist;
  // The invocation order of the finalizers is not determined.
  std::unordered_set<v8impl::RefTracker*> pending_finalizers;
  napi_extended_error_info last_error;
  int open_handle_scopes = 0;
  int open_callback_scopes = 0;
  int refs = 1;
  void* instance_data = nullptr;
  int32_t module_api_version = NODE_API_DEFAULT_MODULE_API_VERSION;
  bool in_gc_finalizer = false;

 protected:
  // Should not be deleted directly. Delete with `napi_env__::DeleteMe()`
  // instead.
  virtual ~napi_env__() = default;
};

inline napi_status napi_clear_last_error(node_api_basic_env basic_env) {
  napi_env env = const_cast<napi_env>(basic_env);
  env->last_error.error_code = napi_ok;
  env->last_error.engine_error_code = 0;
  env->last_error.engine_reserved = nullptr;
  env->last_error.error_message = nullptr;
  return napi_ok;
}

inline napi_status napi_set_last_error(node_api_basic_env basic_env,
                                       napi_status error_code,
                                       uint32_t engine_error_code = 0,
                                       void* engine_reserved = nullptr) {
  napi_env env = const_cast<napi_env>(basic_env);
  env->last_error.error_code = error_code;
  env->last_error.engine_error_code = engine_error_code;
  env->last_error.engine_reserved = engine_reserved;
  return error_code;
}

#define RETURN_STATUS_IF_FALSE(env, condition, status)                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return napi_set_last_error((env), (status));                             \
    }                                                                          \
  } while (0)

#define RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(env, condition, status)           \
  do {                                                                         \
    if (!(condition)) {                                                        \
      return napi_set_last_error(                                              \
          (env), try_catch.HasCaught() ? napi_pending_exception : (status));   \
    }                                                                          \
  } while (0)

#define CHECK_ENV(env)                                                         \
  do {                                                                         \
    if ((env) == nullptr) {                                                    \
      return napi_invalid_arg;                                                 \
    }                                                                          \
  } while (0)

#define CHECK_ENV_NOT_IN_GC(env)                                               \
  do {                                                                         \
    CHECK_ENV((env));                                                          \
    (env)->CheckGCAccess();                                                    \
  } while (0)

#define CHECK_ARG(env, arg)                                                    \
  RETURN_STATUS_IF_FALSE((env), ((arg) != nullptr), napi_invalid_arg)

#define CHECK_ARG_WITH_PREAMBLE(env, arg)                                      \
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(                                        \
      (env), ((arg) != nullptr), napi_invalid_arg)

#define CHECK_MAYBE_EMPTY(env, maybe, status)                                  \
  RETURN_STATUS_IF_FALSE((env), !((maybe).IsEmpty()), (status))

#define CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, maybe, status)                    \
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE((env), !((maybe).IsEmpty()), (status))

// NAPI_PREAMBLE is not wrapped in do..while: try_catch must have function scope
#define NAPI_PREAMBLE(env)                                                     \
  CHECK_ENV_NOT_IN_GC((env));                                                  \
  RETURN_STATUS_IF_FALSE(                                                      \
      (env), (env)->last_exception.IsEmpty(), napi_pending_exception);         \
  RETURN_STATUS_IF_FALSE((env),                                                \
                         (env)->can_call_into_js(),                            \
                         (env->module_api_version == NAPI_VERSION_EXPERIMENTAL \
                              ? napi_cannot_run_js                             \
                              : napi_pending_exception));                      \
  napi_clear_last_error((env));                                                \
  v8impl::TryCatch try_catch((env))

#define CHECK_TO_TYPE(env, type, context, result, src, status)                 \
  do {                                                                         \
    CHECK_ARG((env), (src));                                                   \
    auto maybe = v8impl::V8LocalValueFromJsValue((src))->To##type((context));  \
    CHECK_MAYBE_EMPTY((env), maybe, (status));                                 \
    (result) = maybe.ToLocalChecked();                                         \
  } while (0)

#define CHECK_TO_TYPE_WITH_PREAMBLE(env, type, context, result, src, status)   \
  do {                                                                         \
    CHECK_ARG_WITH_PREAMBLE((env), (src));                                     \
    auto maybe = v8impl::V8LocalValueFromJsValue((src))->To##type((context));  \
    CHECK_MAYBE_EMPTY_WITH_PREAMBLE((env), maybe, (status));                   \
    (result) = maybe.ToLocalChecked();                                         \
  } while (0)

#define CHECK_TO_FUNCTION(env, result, src)                                    \
  do {                                                                         \
    CHECK_ARG((env), (src));                                                   \
    v8::Local<v8::Value> v8value = v8impl::V8LocalValueFromJsValue((src));     \
    RETURN_STATUS_IF_FALSE((env), v8value->IsFunction(), napi_invalid_arg);    \
    (result) = v8value.As<v8::Function>();                                     \
  } while (0)

#define CHECK_TO_OBJECT(env, context, result, src)                             \
  CHECK_TO_TYPE((env), Object, (context), (result), (src), napi_object_expected)

#define CHECK_TO_OBJECT_WITH_PREAMBLE(env, context, result, src)               \
  CHECK_TO_TYPE_WITH_PREAMBLE(                                                 \
      (env), Object, (context), (result), (src), napi_object_expected)

#define CHECK_TO_STRING(env, context, result, src)                             \
  CHECK_TO_TYPE((env), String, (context), (result), (src), napi_string_expected)

#define GET_RETURN_STATUS(env)                                                 \
  (!try_catch.HasCaught()                                                      \
       ? napi_ok                                                               \
       : napi_set_last_error((env), napi_pending_exception))

#define THROW_RANGE_ERROR_IF_FALSE(env, condition, error, message)             \
  do {                                                                         \
    if (!(condition)) {                                                        \
      napi_throw_range_error((env), (error), (message));                       \
      return napi_set_last_error((env), napi_generic_failure);                 \
    }                                                                          \
  } while (0)

#define CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, maybe, status)                    \
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE((env), !((maybe).IsEmpty()), (status))

#define STATUS_CALL(call)                                                      \
  do {                                                                         \
    napi_status status = (call);                                               \
    if (status != napi_ok) return status;                                      \
  } while (0)

namespace v8impl {

//=== Conversion between V8 Handles and napi_value ========================

// This asserts v8::Local<> will always be implemented with a single
// pointer field so that we can pass it around as a void*.
static_assert(sizeof(v8::Local<v8::Value>) == sizeof(napi_value),
              "Cannot convert between v8::Local<v8::Value> and napi_value");

inline napi_value JsValueFromV8LocalValue(v8::Local<v8::Value> local) {
  return reinterpret_cast<napi_value>(*local);
}

inline v8::Local<v8::Value> V8LocalValueFromJsValue(napi_value v) {
  v8::Local<v8::Value> local;
  memcpy(static_cast<void*>(&local), &v, sizeof(v));
  return local;
}

// Adapter for napi_finalize callbacks.
class Finalizer {
 protected:
  Finalizer(napi_env env,
            napi_finalize finalize_callback,
            void* finalize_data,
            void* finalize_hint)
      : env_(env),
        finalize_callback_(finalize_callback),
        finalize_data_(finalize_data),
        finalize_hint_(finalize_hint) {}

  virtual ~Finalizer() = default;

 public:
  static Finalizer* New(napi_env env,
                        napi_finalize finalize_callback = nullptr,
                        void* finalize_data = nullptr,
                        void* finalize_hint = nullptr) {
    return new Finalizer(env, finalize_callback, finalize_data, finalize_hint);
  }

  napi_finalize callback() { return finalize_callback_; }
  void* data() { return finalize_data_; }
  void* hint() { return finalize_hint_; }

  void ResetFinalizer();

 protected:
  napi_env env_;
  napi_finalize finalize_callback_;
  void* finalize_data_;
  void* finalize_hint_;
};

class TryCatch : public v8::TryCatch {
 public:
  explicit TryCatch(napi_env env) : v8::TryCatch(env->isolate), _env(env) {}

  ~TryCatch() {
    if (HasCaught()) {
      _env->last_exception.Reset(_env->isolate, Exception());
    }
  }

 private:
  napi_env _env;
};

// Ownership of a reference.
enum class Ownership {
  // The reference is owned by the runtime. No userland call is needed to
  // destruct the reference.
  kRuntime,
  // The reference is owned by the userland. User code is responsible to delete
  // the reference with appropriate node-api calls.
  kUserland,
};

// Wrapper around Finalizer that can be tracked.
class TrackedFinalizer : public Finalizer, public RefTracker {
 protected:
  TrackedFinalizer(napi_env env,
                   napi_finalize finalize_callback,
                   void* finalize_data,
                   void* finalize_hint);

 public:
  static TrackedFinalizer* New(napi_env env,
                               napi_finalize finalize_callback,
                               void* finalize_data,
                               void* finalize_hint);
  ~TrackedFinalizer() override;

 protected:
  void Finalize() override;
  void FinalizeCore(bool deleteMe);
};

// Wrapper around TrackedFinalizer that implements reference counting.
class RefBase : public TrackedFinalizer {
 protected:
  RefBase(napi_env env,
          uint32_t initial_refcount,
          Ownership ownership,
          napi_finalize finalize_callback,
          void* finalize_data,
          void* finalize_hint);

 public:
  static RefBase* New(napi_env env,
                      uint32_t initial_refcount,
                      Ownership ownership,
                      napi_finalize finalize_callback,
                      void* finalize_data,
                      void* finalize_hint);

  void* Data();
  uint32_t Ref();
  uint32_t Unref();
  uint32_t RefCount();

  Ownership ownership() { return ownership_; }

 protected:
  void Finalize() override;

 private:
  uint32_t refcount_;
  Ownership ownership_;
};

// Wrapper around v8impl::Persistent.
class Reference : public RefBase {
 protected:
  template <typename... Args>
  Reference(napi_env env, v8::Local<v8::Value> value, Args&&... args);

 public:
  static Reference* New(napi_env env,
                        v8::Local<v8::Value> value,
                        uint32_t initial_refcount,
                        Ownership ownership,
                        napi_finalize finalize_callback = nullptr,
                        void* finalize_data = nullptr,
                        void* finalize_hint = nullptr);

  virtual ~Reference();
  uint32_t Ref();
  uint32_t Unref();
  v8::Local<v8::Value> Get();

 protected:
  void Finalize() override;

 private:
  static void WeakCallback(const v8::WeakCallbackInfo<Reference>& data);

  void SetWeak();

  v8impl::Persistent<v8::Value> persistent_;
  bool can_be_weak_;
};

}  // end of namespace v8impl

#endif  // SRC_JS_NATIVE_API_V8_H_
