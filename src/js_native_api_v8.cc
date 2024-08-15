#include <algorithm>
#include <climits>  // INT_MAX
#include <cmath>
#define NAPI_EXPERIMENTAL
#include "env-inl.h"
#include "js_native_api.h"
#include "js_native_api_v8.h"
#include "util-inl.h"

#define CHECK_MAYBE_NOTHING(env, maybe, status)                                \
  RETURN_STATUS_IF_FALSE((env), !((maybe).IsNothing()), (status))

#define CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, maybe, status)                  \
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE((env), !((maybe).IsNothing()), (status))

#define CHECK_TO_NUMBER(env, context, result, src)                             \
  CHECK_TO_TYPE((env), Number, (context), (result), (src), napi_number_expected)

// n-api defines NAPI_AUTO_LENGTH as the indicator that a string
// is null terminated. For V8 the equivalent is -1. The assert
// validates that our cast of NAPI_AUTO_LENGTH results in -1 as
// needed by V8.
#define CHECK_NEW_FROM_UTF8_LEN(env, result, str, len)                         \
  do {                                                                         \
    static_assert(static_cast<int>(NAPI_AUTO_LENGTH) == -1,                    \
                  "Casting NAPI_AUTO_LENGTH to int must result in -1");        \
    RETURN_STATUS_IF_FALSE(                                                    \
        (env), (len == NAPI_AUTO_LENGTH) || len <= INT_MAX, napi_invalid_arg); \
    RETURN_STATUS_IF_FALSE((env), (str) != nullptr, napi_invalid_arg);         \
    auto str_maybe = v8::String::NewFromUtf8((env)->isolate,                   \
                                             (str),                            \
                                             v8::NewStringType::kInternalized, \
                                             static_cast<int>(len));           \
    CHECK_MAYBE_EMPTY((env), str_maybe, napi_generic_failure);                 \
    (result) = str_maybe.ToLocalChecked();                                     \
  } while (0)

#define CHECK_NEW_FROM_UTF8(env, result, str)                                  \
  CHECK_NEW_FROM_UTF8_LEN((env), (result), (str), NAPI_AUTO_LENGTH)

#define CHECK_NEW_STRING_ARGS(env, str, length, result)                        \
  do {                                                                         \
    CHECK_ENV_NOT_IN_GC((env));                                                \
    if ((length) > 0) CHECK_ARG((env), (str));                                 \
    CHECK_ARG((env), (result));                                                \
    RETURN_STATUS_IF_FALSE(                                                    \
        (env),                                                                 \
        ((length) == NAPI_AUTO_LENGTH) || (length) <= INT_MAX,                 \
        napi_invalid_arg);                                                     \
  } while (0)

#define CREATE_TYPED_ARRAY(                                                    \
    env, type, size_of_element, buffer, byte_offset, length, out)              \
  do {                                                                         \
    if ((size_of_element) > 1) {                                               \
      THROW_RANGE_ERROR_IF_FALSE(                                              \
          (env),                                                               \
          (byte_offset) % (size_of_element) == 0,                              \
          "ERR_NAPI_INVALID_TYPEDARRAY_ALIGNMENT",                             \
          "start offset of " #type                                             \
          " should be a multiple of " #size_of_element);                       \
    }                                                                          \
    THROW_RANGE_ERROR_IF_FALSE(                                                \
        (env),                                                                 \
        (length) * (size_of_element) + (byte_offset) <= buffer->ByteLength(),  \
        "ERR_NAPI_INVALID_TYPEDARRAY_LENGTH",                                  \
        "Invalid typed array length");                                         \
    (out) = v8::type::New((buffer), (byte_offset), (length));                  \
  } while (0)

void napi_env__::InvokeFinalizerFromGC(v8impl::RefTracker* finalizer) {
  if (module_api_version != NAPI_VERSION_EXPERIMENTAL) {
    EnqueueFinalizer(finalizer);
  } else {
    // The experimental code calls finalizers immediately to release native
    // objects as soon as possible. In that state any code that may affect GC
    // state causes a fatal error. To work around this issue the finalizer code
    // can call node_api_post_finalizer.
    auto restore_state = node::OnScopeLeave(
        [this, saved = in_gc_finalizer] { in_gc_finalizer = saved; });
    in_gc_finalizer = true;
    finalizer->Finalize();
  }
}

namespace v8impl {
namespace {

template <typename CCharType, typename StringMaker>
napi_status NewString(napi_env env,
                      const CCharType* str,
                      size_t length,
                      napi_value* result,
                      StringMaker string_maker) {
  CHECK_NEW_STRING_ARGS(env, str, length, result);

  auto isolate = env->isolate;
  auto str_maybe = string_maker(isolate);
  CHECK_MAYBE_EMPTY(env, str_maybe, napi_generic_failure);
  *result = v8impl::JsValueFromV8LocalValue(str_maybe.ToLocalChecked());
  return napi_clear_last_error(env);
}

template <typename CharType, typename CreateAPI, typename StringMaker>
napi_status NewExternalString(napi_env env,
                              CharType* str,
                              size_t length,
                              napi_finalize finalize_callback,
                              void* finalize_hint,
                              napi_value* result,
                              bool* copied,
                              CreateAPI create_api,
                              StringMaker string_maker) {
  CHECK_NEW_STRING_ARGS(env, str, length, result);

  napi_status status;
#if defined(V8_ENABLE_SANDBOX)
  status = create_api(env, str, length, result);
  if (status == napi_ok) {
    if (copied != nullptr) {
      *copied = true;
    }
    if (finalize_callback) {
      env->CallFinalizer(
          finalize_callback, static_cast<CharType*>(str), finalize_hint);
    }
  }
#else
  status = NewString(env, str, length, result, string_maker);
  if (status == napi_ok && copied != nullptr) {
    *copied = false;
  }
#endif  // V8_ENABLE_SANDBOX
  return status;
}

class TrackedStringResource : public Finalizer, RefTracker {
 public:
  TrackedStringResource(napi_env env,
                        napi_finalize finalize_callback,
                        void* data,
                        void* finalize_hint)
      : Finalizer(env, finalize_callback, data, finalize_hint) {
    Link(finalize_callback == nullptr ? &env->reflist
                                      : &env->finalizing_reflist);
  }

 protected:
  // The only time Finalize() gets called before Dispose() is if the
  // environment is dying. Finalize() expects that the item will be unlinked,
  // so we do it here. V8 will still call Dispose() on us later, so we don't do
  // any deleting here. We just null out env_ to avoid passing a stale pointer
  // to the user's finalizer when V8 does finally call Dispose().
  void Finalize() override {
    Unlink();
    env_ = nullptr;
  }

  ~TrackedStringResource() {
    if (finalize_callback_ == nullptr) return;
    if (env_ == nullptr) {
      // The environment is dead. Call the finalizer directly.
      finalize_callback_(nullptr, finalize_data_, finalize_hint_);
    } else {
      // The environment is still alive. Let's remove ourselves from its list
      // of references and call the user's finalizer.
      Unlink();
      env_->CallFinalizer(finalize_callback_, finalize_data_, finalize_hint_);
    }
  }
};

class ExternalOneByteStringResource
    : public v8::String::ExternalOneByteStringResource,
      TrackedStringResource {
 public:
  ExternalOneByteStringResource(napi_env env,
                                char* string,
                                const size_t length,
                                napi_finalize finalize_callback,
                                void* finalize_hint)
      : TrackedStringResource(env, finalize_callback, string, finalize_hint),
        string_(string),
        length_(length) {}

  const char* data() const override { return string_; }
  size_t length() const override { return length_; }

 private:
  const char* string_;
  const size_t length_;
};

class ExternalStringResource : public v8::String::ExternalStringResource,
                               TrackedStringResource {
 public:
  ExternalStringResource(napi_env env,
                         char16_t* string,
                         const size_t length,
                         napi_finalize finalize_callback,
                         void* finalize_hint)
      : TrackedStringResource(env, finalize_callback, string, finalize_hint),
        string_(reinterpret_cast<uint16_t*>(string)),
        length_(length) {}

  const uint16_t* data() const override { return string_; }
  size_t length() const override { return length_; }

 private:
  const uint16_t* string_;
  const size_t length_;
};

inline napi_status V8NameFromPropertyDescriptor(
    napi_env env,
    const napi_property_descriptor* p,
    v8::Local<v8::Name>* result) {
  if (p->utf8name != nullptr) {
    CHECK_NEW_FROM_UTF8(env, *result, p->utf8name);
  } else {
    v8::Local<v8::Value> property_value =
        v8impl::V8LocalValueFromJsValue(p->name);

    RETURN_STATUS_IF_FALSE(env, property_value->IsName(), napi_name_expected);
    *result = property_value.As<v8::Name>();
  }

  return napi_ok;
}

// convert from n-api property attributes to v8::PropertyAttribute
inline v8::PropertyAttribute V8PropertyAttributesFromDescriptor(
    const napi_property_descriptor* descriptor) {
  unsigned int attribute_flags = v8::PropertyAttribute::None;

  // The napi_writable attribute is ignored for accessor descriptors, but
  // V8 would throw `TypeError`s on assignment with nonexistence of a setter.
  if ((descriptor->getter == nullptr && descriptor->setter == nullptr) &&
      (descriptor->attributes & napi_writable) == 0) {
    attribute_flags |= v8::PropertyAttribute::ReadOnly;
  }

  if ((descriptor->attributes & napi_enumerable) == 0) {
    attribute_flags |= v8::PropertyAttribute::DontEnum;
  }
  if ((descriptor->attributes & napi_configurable) == 0) {
    attribute_flags |= v8::PropertyAttribute::DontDelete;
  }

  return static_cast<v8::PropertyAttribute>(attribute_flags);
}

inline napi_deferred JsDeferredFromNodePersistent(
    v8impl::Persistent<v8::Value>* local) {
  return reinterpret_cast<napi_deferred>(local);
}

inline v8impl::Persistent<v8::Value>* NodePersistentFromJsDeferred(
    napi_deferred local) {
  return reinterpret_cast<v8impl::Persistent<v8::Value>*>(local);
}

class HandleScopeWrapper {
 public:
  explicit HandleScopeWrapper(v8::Isolate* isolate) : scope(isolate) {}

 private:
  v8::HandleScope scope;
};

// In node v0.10 version of v8, there is no EscapableHandleScope and the
// node v0.10 port use HandleScope::Close(Local<T> v) to mimic the behavior
// of a EscapableHandleScope::Escape(Local<T> v), but it is not the same
// semantics. This is an example of where the api abstraction fail to work
// across different versions.
class EscapableHandleScopeWrapper {
 public:
  explicit EscapableHandleScopeWrapper(v8::Isolate* isolate)
      : scope(isolate), escape_called_(false) {}
  bool escape_called() const { return escape_called_; }
  template <typename T>
  v8::Local<T> Escape(v8::Local<T> handle) {
    escape_called_ = true;
    return scope.Escape(handle);
  }

 private:
  v8::EscapableHandleScope scope;
  bool escape_called_;
};

inline napi_handle_scope JsHandleScopeFromV8HandleScope(HandleScopeWrapper* s) {
  return reinterpret_cast<napi_handle_scope>(s);
}

inline HandleScopeWrapper* V8HandleScopeFromJsHandleScope(napi_handle_scope s) {
  return reinterpret_cast<HandleScopeWrapper*>(s);
}

inline napi_escapable_handle_scope
JsEscapableHandleScopeFromV8EscapableHandleScope(
    EscapableHandleScopeWrapper* s) {
  return reinterpret_cast<napi_escapable_handle_scope>(s);
}

inline EscapableHandleScopeWrapper*
V8EscapableHandleScopeFromJsEscapableHandleScope(
    napi_escapable_handle_scope s) {
  return reinterpret_cast<EscapableHandleScopeWrapper*>(s);
}

inline napi_status ConcludeDeferred(napi_env env,
                                    napi_deferred deferred,
                                    napi_value result,
                                    bool is_resolved) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8impl::Persistent<v8::Value>* deferred_ref =
      NodePersistentFromJsDeferred(deferred);
  v8::Local<v8::Value> v8_deferred =
      v8::Local<v8::Value>::New(env->isolate, *deferred_ref);

  auto v8_resolver = v8_deferred.As<v8::Promise::Resolver>();

  v8::Maybe<bool> success =
      is_resolved ? v8_resolver->Resolve(
                        context, v8impl::V8LocalValueFromJsValue(result))
                  : v8_resolver->Reject(
                        context, v8impl::V8LocalValueFromJsValue(result));

  delete deferred_ref;

  RETURN_STATUS_IF_FALSE(env, success.FromMaybe(false), napi_generic_failure);

  return GET_RETURN_STATUS(env);
}

enum UnwrapAction { KeepWrap, RemoveWrap };

inline napi_status Unwrap(napi_env env,
                          napi_value js_object,
                          void** result,
                          UnwrapAction action) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, js_object);
  if (action == KeepWrap) {
    CHECK_ARG(env, result);
  }

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(js_object);
  RETURN_STATUS_IF_FALSE(env, value->IsObject(), napi_invalid_arg);
  v8::Local<v8::Object> obj = value.As<v8::Object>();

  auto val = obj->GetPrivate(context, NAPI_PRIVATE_KEY(context, wrapper))
                 .ToLocalChecked();
  RETURN_STATUS_IF_FALSE(env, val->IsExternal(), napi_invalid_arg);
  Reference* reference =
      static_cast<v8impl::Reference*>(val.As<v8::External>()->Value());

  if (result) {
    *result = reference->Data();
  }

  if (action == RemoveWrap) {
    CHECK(obj->DeletePrivate(context, NAPI_PRIVATE_KEY(context, wrapper))
              .FromJust());
    if (reference->ownership() == Ownership::kUserland) {
      // When the wrap is been removed, the finalizer should be reset.
      reference->ResetFinalizer();
    } else {
      delete reference;
    }
  }

  return GET_RETURN_STATUS(env);
}

//=== Function napi_callback wrapper =================================

// Use this data structure to associate callback data with each N-API function
// exposed to JavaScript. The structure is stored in a v8::External which gets
// passed into our callback wrapper. This reduces the performance impact of
// calling through N-API.
// Ref: benchmark/misc/function_call
// Discussion (incl. perf. data): https://github.com/nodejs/node/pull/21072
class CallbackBundle {
 public:
  // Creates an object to be made available to the static function callback
  // wrapper, used to retrieve the native callback function and data pointer.
  static inline v8::Local<v8::Value> New(napi_env env,
                                         napi_callback cb,
                                         void* data) {
    CallbackBundle* bundle = new CallbackBundle();
    bundle->cb = cb;
    bundle->cb_data = data;
    bundle->env = env;

    v8::Local<v8::Value> cbdata = v8::External::New(env->isolate, bundle);
    Reference::New(
        env, cbdata, 0, Ownership::kRuntime, Delete, bundle, nullptr);
    return cbdata;
  }
  napi_env env;   // Necessary to invoke C++ NAPI callback
  void* cb_data;  // The user provided callback data
  napi_callback cb;

 private:
  static void Delete(napi_env env, void* data, void* hint) {
    CallbackBundle* bundle = static_cast<CallbackBundle*>(data);
    delete bundle;
  }
};

// Base class extended by classes that wrap V8 function and property callback
// info.
class CallbackWrapper {
 public:
  inline CallbackWrapper(napi_value this_arg, size_t args_length, void* data)
      : _this(this_arg), _args_length(args_length), _data(data) {}

  virtual napi_value GetNewTarget() = 0;
  virtual void Args(napi_value* buffer, size_t bufferlength) = 0;
  virtual void SetReturnValue(napi_value value) = 0;

  napi_value This() { return _this; }

  size_t ArgsLength() { return _args_length; }

  void* Data() { return _data; }

 protected:
  const napi_value _this;
  const size_t _args_length;
  void* _data;
};

class CallbackWrapperBase : public CallbackWrapper {
 public:
  inline CallbackWrapperBase(const v8::FunctionCallbackInfo<v8::Value>& cbinfo,
                             const size_t args_length)
      : CallbackWrapper(
            JsValueFromV8LocalValue(cbinfo.This()), args_length, nullptr),
        _cbinfo(cbinfo) {
    _bundle = reinterpret_cast<CallbackBundle*>(
        cbinfo.Data().As<v8::External>()->Value());
    _data = _bundle->cb_data;
  }

 protected:
  inline void InvokeCallback() {
    napi_callback_info cbinfo_wrapper = reinterpret_cast<napi_callback_info>(
        static_cast<CallbackWrapper*>(this));

    // All other pointers we need are stored in `_bundle`
    napi_env env = _bundle->env;
    napi_callback cb = _bundle->cb;

    napi_value result = nullptr;
    bool exceptionOccurred = false;
    env->CallIntoModule([&](napi_env env) { result = cb(env, cbinfo_wrapper); },
                        [&](napi_env env, v8::Local<v8::Value> value) {
                          exceptionOccurred = true;
                          if (env->terminatedOrTerminating()) {
                            return;
                          }
                          env->isolate->ThrowException(value);
                        });

    if (!exceptionOccurred && (result != nullptr)) {
      this->SetReturnValue(result);
    }
  }

  const v8::FunctionCallbackInfo<v8::Value>& _cbinfo;
  CallbackBundle* _bundle;
};

class FunctionCallbackWrapper : public CallbackWrapperBase {
 public:
  static void Invoke(const v8::FunctionCallbackInfo<v8::Value>& info) {
    FunctionCallbackWrapper cbwrapper(info);
    cbwrapper.InvokeCallback();
  }

  static inline napi_status NewFunction(napi_env env,
                                        napi_callback cb,
                                        void* cb_data,
                                        v8::Local<v8::Function>* result) {
    v8::Local<v8::Value> cbdata = v8impl::CallbackBundle::New(env, cb, cb_data);
    RETURN_STATUS_IF_FALSE(env, !cbdata.IsEmpty(), napi_generic_failure);

    v8::MaybeLocal<v8::Function> maybe_function =
        v8::Function::New(env->context(), Invoke, cbdata);
    CHECK_MAYBE_EMPTY(env, maybe_function, napi_generic_failure);

    *result = maybe_function.ToLocalChecked();
    return napi_clear_last_error(env);
  }

  static inline napi_status NewTemplate(
      napi_env env,
      napi_callback cb,
      void* cb_data,
      v8::Local<v8::FunctionTemplate>* result,
      v8::Local<v8::Signature> sig = v8::Local<v8::Signature>()) {
    v8::Local<v8::Value> cbdata = v8impl::CallbackBundle::New(env, cb, cb_data);
    RETURN_STATUS_IF_FALSE(env, !cbdata.IsEmpty(), napi_generic_failure);

    *result = v8::FunctionTemplate::New(env->isolate, Invoke, cbdata, sig);
    return napi_clear_last_error(env);
  }

  explicit FunctionCallbackWrapper(
      const v8::FunctionCallbackInfo<v8::Value>& cbinfo)
      : CallbackWrapperBase(cbinfo, cbinfo.Length()) {}

  napi_value GetNewTarget() override {
    if (_cbinfo.IsConstructCall()) {
      return v8impl::JsValueFromV8LocalValue(_cbinfo.NewTarget());
    } else {
      return nullptr;
    }
  }

  /*virtual*/
  void Args(napi_value* buffer, size_t buffer_length) override {
    size_t i = 0;
    size_t min = std::min(buffer_length, _args_length);

    for (; i < min; i += 1) {
      buffer[i] = v8impl::JsValueFromV8LocalValue(_cbinfo[i]);
    }

    if (i < buffer_length) {
      napi_value undefined =
          v8impl::JsValueFromV8LocalValue(v8::Undefined(_cbinfo.GetIsolate()));
      for (; i < buffer_length; i += 1) {
        buffer[i] = undefined;
      }
    }
  }

  /*virtual*/
  void SetReturnValue(napi_value value) override {
    v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
    _cbinfo.GetReturnValue().Set(val);
  }
};

inline napi_status Wrap(napi_env env,
                        napi_value js_object,
                        void* native_object,
                        napi_finalize finalize_cb,
                        void* finalize_hint,
                        napi_ref* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, js_object);

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(js_object);
  RETURN_STATUS_IF_FALSE(env, value->IsObject(), napi_invalid_arg);
  v8::Local<v8::Object> obj = value.As<v8::Object>();

  // If we've already wrapped this object, we error out.
  RETURN_STATUS_IF_FALSE(
      env,
      !obj->HasPrivate(context, NAPI_PRIVATE_KEY(context, wrapper)).FromJust(),
      napi_invalid_arg);

  v8impl::Reference* reference = nullptr;
  if (result != nullptr) {
    // The returned reference should be deleted via napi_delete_reference()
    // ONLY in response to the finalize callback invocation. (If it is deleted
    // before then, then the finalize callback will never be invoked.)
    // Therefore a finalize callback is required when returning a reference.
    CHECK_ARG(env, finalize_cb);
    reference = v8impl::Reference::New(env,
                                       obj,
                                       0,
                                       v8impl::Ownership::kUserland,
                                       finalize_cb,
                                       native_object,
                                       finalize_hint);
    *result = reinterpret_cast<napi_ref>(reference);
  } else {
    // Create a self-deleting reference.
    reference = v8impl::Reference::New(
        env,
        obj,
        0,
        v8impl::Ownership::kRuntime,
        finalize_cb,
        native_object,
        finalize_cb == nullptr ? nullptr : finalize_hint);
  }

  CHECK(obj->SetPrivate(context,
                        NAPI_PRIVATE_KEY(context, wrapper),
                        v8::External::New(env->isolate, reference))
            .FromJust());

  return GET_RETURN_STATUS(env);
}

// In JavaScript, weak references can be created for object types (Object,
// Function, and external Object) and for local symbols that are created with
// the `Symbol` function call. Global symbols created with the `Symbol.for`
// method cannot be weak references because they are never collected.
//
// Currently, V8 has no API to detect if a symbol is local or global.
// Until we have a V8 API for it, we consider that all symbols can be weak.
// This matches the current Node-API behavior.
inline bool CanBeHeldWeakly(v8::Local<v8::Value> value) {
  return value->IsObject() || value->IsSymbol();
}

}  // end of anonymous namespace

void Finalizer::ResetFinalizer() {
  finalize_callback_ = nullptr;
  finalize_data_ = nullptr;
  finalize_hint_ = nullptr;
}

TrackedFinalizer::TrackedFinalizer(napi_env env,
                                   napi_finalize finalize_callback,
                                   void* finalize_data,
                                   void* finalize_hint)
    : Finalizer(env, finalize_callback, finalize_data, finalize_hint),
      RefTracker() {
  Link(finalize_callback == nullptr ? &env->reflist : &env->finalizing_reflist);
}

TrackedFinalizer* TrackedFinalizer::New(napi_env env,
                                        napi_finalize finalize_callback,
                                        void* finalize_data,
                                        void* finalize_hint) {
  return new TrackedFinalizer(
      env, finalize_callback, finalize_data, finalize_hint);
}

// When a TrackedFinalizer is being deleted, it may have been queued to call its
// finalizer.
TrackedFinalizer::~TrackedFinalizer() {
  // Remove from the env's tracked list.
  Unlink();
  // Try to remove the finalizer from the scheduled second pass callback.
  env_->DequeueFinalizer(this);
}

void TrackedFinalizer::Finalize() {
  FinalizeCore(/*deleteMe:*/ true);
}

void TrackedFinalizer::FinalizeCore(bool deleteMe) {
  // Swap out the field finalize_callback so that it can not be accidentally
  // called more than once.
  napi_finalize finalize_callback = finalize_callback_;
  void* finalize_data = finalize_data_;
  void* finalize_hint = finalize_hint_;
  ResetFinalizer();

  // Either the RefBase is going to be deleted in the finalize_callback or not,
  // it should be removed from the tracked list.
  Unlink();
  // If the finalize_callback is present, it should either delete the
  // derived RefBase, or the RefBase ownership was set to Ownership::kRuntime
  // and the deleteMe parameter is true.
  if (finalize_callback != nullptr) {
    env_->CallFinalizer(finalize_callback, finalize_data, finalize_hint);
  }

  if (deleteMe) {
    delete this;
  }
}

// Wrapper around v8impl::Persistent that implements reference counting.
RefBase::RefBase(napi_env env,
                 uint32_t initial_refcount,
                 Ownership ownership,
                 napi_finalize finalize_callback,
                 void* finalize_data,
                 void* finalize_hint)
    : TrackedFinalizer(env, finalize_callback, finalize_data, finalize_hint),
      refcount_(initial_refcount),
      ownership_(ownership) {}

RefBase* RefBase::New(napi_env env,
                      uint32_t initial_refcount,
                      Ownership ownership,
                      napi_finalize finalize_callback,
                      void* finalize_data,
                      void* finalize_hint) {
  return new RefBase(env,
                     initial_refcount,
                     ownership,
                     finalize_callback,
                     finalize_data,
                     finalize_hint);
}

void* RefBase::Data() {
  return finalize_data_;
}

uint32_t RefBase::Ref() {
  return ++refcount_;
}

uint32_t RefBase::Unref() {
  if (refcount_ == 0) {
    return 0;
  }
  return --refcount_;
}

uint32_t RefBase::RefCount() {
  return refcount_;
}

void RefBase::Finalize() {
  // If the RefBase is not Ownership::kRuntime, userland code should delete it.
  // Delete it if it is Ownership::kRuntime.
  FinalizeCore(/*deleteMe:*/ ownership_ == Ownership::kRuntime);
}

template <typename... Args>
Reference::Reference(napi_env env, v8::Local<v8::Value> value, Args&&... args)
    : RefBase(env, std::forward<Args>(args)...),
      persistent_(env->isolate, value),
      can_be_weak_(CanBeHeldWeakly(value)) {
  if (RefCount() == 0) {
    SetWeak();
  }
}

Reference::~Reference() {
  // Reset the handle. And no weak callback will be invoked.
  persistent_.Reset();
}

Reference* Reference::New(napi_env env,
                          v8::Local<v8::Value> value,
                          uint32_t initial_refcount,
                          Ownership ownership,
                          napi_finalize finalize_callback,
                          void* finalize_data,
                          void* finalize_hint) {
  return new Reference(env,
                       value,
                       initial_refcount,
                       ownership,
                       finalize_callback,
                       finalize_data,
                       finalize_hint);
}

uint32_t Reference::Ref() {
  // When the persistent_ is cleared in the WeakCallback, and a second pass
  // callback is pending, return 0 unconditionally.
  if (persistent_.IsEmpty()) {
    return 0;
  }
  uint32_t refcount = RefBase::Ref();
  if (refcount == 1 && can_be_weak_) {
    persistent_.ClearWeak();
  }
  return refcount;
}

uint32_t Reference::Unref() {
  // When the persistent_ is cleared in the WeakCallback, and a second pass
  // callback is pending, return 0 unconditionally.
  if (persistent_.IsEmpty()) {
    return 0;
  }
  uint32_t old_refcount = RefCount();
  uint32_t refcount = RefBase::Unref();
  if (old_refcount == 1 && refcount == 0) {
    SetWeak();
  }
  return refcount;
}

v8::Local<v8::Value> Reference::Get() {
  if (persistent_.IsEmpty()) {
    return v8::Local<v8::Value>();
  } else {
    return v8::Local<v8::Value>::New(env_->isolate, persistent_);
  }
}

void Reference::Finalize() {
  // Unconditionally reset the persistent handle so that no weak callback will
  // be invoked again.
  persistent_.Reset();

  // Chain up to perform the rest of the finalization.
  RefBase::Finalize();
}

// Mark the reference as weak and eligible for collection
// by the gc.
void Reference::SetWeak() {
  if (can_be_weak_) {
    persistent_.SetWeak(this, WeakCallback, v8::WeakCallbackType::kParameter);
  } else {
    persistent_.Reset();
  }
}

// The N-API finalizer callback may make calls into the engine. V8's heap is
// not in a consistent state during the weak callback, and therefore it does
// not support calls back into it. Enqueue the invocation of the finalizer.
void Reference::WeakCallback(const v8::WeakCallbackInfo<Reference>& data) {
  Reference* reference = data.GetParameter();
  // The reference must be reset during the weak callback as the API protocol.
  reference->persistent_.Reset();
  reference->env_->InvokeFinalizerFromGC(reference);
}

/**
 * A wrapper for `v8::External` to support type-tagging. `v8::External` doesn't
 * support defining any properties and private properties on it, even though it
 * is an object. This wrapper is used to store the type tag and the data of the
 * external value.
 */
class ExternalWrapper {
 private:
  explicit ExternalWrapper(void* data) : data_(data), type_tag_{0, 0} {}

  static void WeakCallback(const v8::WeakCallbackInfo<ExternalWrapper>& data) {
    ExternalWrapper* wrapper = data.GetParameter();
    delete wrapper;
  }

 public:
  static v8::Local<v8::External> New(napi_env env, void* data) {
    ExternalWrapper* wrapper = new ExternalWrapper(data);
    v8::Local<v8::External> external = v8::External::New(env->isolate, wrapper);
    wrapper->persistent_.Reset(env->isolate, external);
    wrapper->persistent_.SetWeak(
        wrapper, WeakCallback, v8::WeakCallbackType::kParameter);

    return external;
  }

  static ExternalWrapper* From(v8::Local<v8::External> external) {
    return static_cast<ExternalWrapper*>(external->Value());
  }

  void* Data() { return data_; }

  bool TypeTag(const napi_type_tag* type_tag) {
    if (has_tag_) {
      return false;
    }
    type_tag_ = *type_tag;
    has_tag_ = true;
    return true;
  }

  bool CheckTypeTag(const napi_type_tag* type_tag) {
    return has_tag_ && type_tag->lower == type_tag_.lower &&
           type_tag->upper == type_tag_.upper;
  }

 private:
  v8impl::Persistent<v8::Value> persistent_;
  void* data_;
  napi_type_tag type_tag_;
  bool has_tag_ = false;
};

}  // end of namespace v8impl

// Warning: Keep in-sync with napi_status enum
static const char* error_messages[] = {
    nullptr,
    "Invalid argument",
    "An object was expected",
    "A string was expected",
    "A string or symbol was expected",
    "A function was expected",
    "A number was expected",
    "A boolean was expected",
    "An array was expected",
    "Unknown failure",
    "An exception is pending",
    "The async work item was cancelled",
    "napi_escape_handle already called on scope",
    "Invalid handle scope usage",
    "Invalid callback scope usage",
    "Thread-safe function queue is full",
    "Thread-safe function handle is closing",
    "A bigint was expected",
    "A date was expected",
    "An arraybuffer was expected",
    "A detachable arraybuffer was expected",
    "Main thread would deadlock",
    "External buffers are not allowed",
    "Cannot run JavaScript",
};

napi_status NAPI_CDECL napi_get_last_error_info(
    node_api_basic_env basic_env, const napi_extended_error_info** result) {
  napi_env env = const_cast<napi_env>(basic_env);
  CHECK_ENV(env);
  CHECK_ARG(env, result);

  // The value of the constant below must be updated to reference the last
  // message in the `napi_status` enum each time a new error message is added.
  // We don't have a napi_status_last as this would result in an ABI
  // change each time a message was added.
  const int last_status = napi_cannot_run_js;

  static_assert(NAPI_ARRAYSIZE(error_messages) == last_status + 1,
                "Count of error messages must match count of error values");
  CHECK_LE(env->last_error.error_code, last_status);
  // Wait until someone requests the last error information to fetch the error
  // message string
  env->last_error.error_message = error_messages[env->last_error.error_code];

  if (env->last_error.error_code == napi_ok) {
    napi_clear_last_error(env);
  }
  *result = &(env->last_error);
  return napi_ok;
}

napi_status NAPI_CDECL napi_create_function(napi_env env,
                                            const char* utf8name,
                                            size_t length,
                                            napi_callback cb,
                                            void* callback_data,
                                            napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);
  CHECK_ARG(env, cb);

  v8::Local<v8::Function> return_value;
  v8::EscapableHandleScope scope(env->isolate);
  v8::Local<v8::Function> fn;
  STATUS_CALL(v8impl::FunctionCallbackWrapper::NewFunction(
      env, cb, callback_data, &fn));
  return_value = scope.Escape(fn);

  if (utf8name != nullptr) {
    v8::Local<v8::String> name_string;
    CHECK_NEW_FROM_UTF8_LEN(env, name_string, utf8name, length);
    return_value->SetName(name_string);
  }

  *result = v8impl::JsValueFromV8LocalValue(return_value);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL
napi_define_class(napi_env env,
                  const char* utf8name,
                  size_t length,
                  napi_callback constructor,
                  void* callback_data,
                  size_t property_count,
                  const napi_property_descriptor* properties,
                  napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);
  CHECK_ARG(env, constructor);

  if (property_count > 0) {
    CHECK_ARG(env, properties);
  }

  v8::Isolate* isolate = env->isolate;

  v8::EscapableHandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> tpl;
  STATUS_CALL(v8impl::FunctionCallbackWrapper::NewTemplate(
      env, constructor, callback_data, &tpl));

  v8::Local<v8::String> name_string;
  CHECK_NEW_FROM_UTF8_LEN(env, name_string, utf8name, length);
  tpl->SetClassName(name_string);

  size_t static_property_count = 0;
  for (size_t i = 0; i < property_count; i++) {
    const napi_property_descriptor* p = properties + i;

    if ((p->attributes & napi_static) != 0) {
      // Static properties are handled separately below.
      static_property_count++;
      continue;
    }

    v8::Local<v8::Name> property_name;
    STATUS_CALL(v8impl::V8NameFromPropertyDescriptor(env, p, &property_name));

    v8::PropertyAttribute attributes =
        v8impl::V8PropertyAttributesFromDescriptor(p);

    // This code is similar to that in napi_define_properties(); the
    // difference is it applies to a template instead of an object,
    // and preferred PropertyAttribute for lack of PropertyDescriptor
    // support on ObjectTemplate.
    if (p->getter != nullptr || p->setter != nullptr) {
      v8::Local<v8::FunctionTemplate> getter_tpl;
      v8::Local<v8::FunctionTemplate> setter_tpl;
      if (p->getter != nullptr) {
        STATUS_CALL(v8impl::FunctionCallbackWrapper::NewTemplate(
            env, p->getter, p->data, &getter_tpl));
      }
      if (p->setter != nullptr) {
        STATUS_CALL(v8impl::FunctionCallbackWrapper::NewTemplate(
            env, p->setter, p->data, &setter_tpl));
      }

      tpl->PrototypeTemplate()->SetAccessorProperty(
          property_name, getter_tpl, setter_tpl, attributes);
    } else if (p->method != nullptr) {
      v8::Local<v8::FunctionTemplate> t;
      STATUS_CALL(v8impl::FunctionCallbackWrapper::NewTemplate(
          env, p->method, p->data, &t, v8::Signature::New(isolate, tpl)));

      tpl->PrototypeTemplate()->Set(property_name, t, attributes);
    } else {
      v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(p->value);
      tpl->PrototypeTemplate()->Set(property_name, value, attributes);
    }
  }

  v8::Local<v8::Context> context = env->context();
  *result = v8impl::JsValueFromV8LocalValue(
      scope.Escape(tpl->GetFunction(context).ToLocalChecked()));

  if (static_property_count > 0) {
    std::vector<napi_property_descriptor> static_descriptors;
    static_descriptors.reserve(static_property_count);

    for (size_t i = 0; i < property_count; i++) {
      const napi_property_descriptor* p = properties + i;
      if ((p->attributes & napi_static) != 0) {
        static_descriptors.push_back(*p);
      }
    }

    STATUS_CALL(napi_define_properties(
        env, *result, static_descriptors.size(), static_descriptors.data()));
  }

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_property_names(napi_env env,
                                               napi_value object,
                                               napi_value* result) {
  return napi_get_all_property_names(
      env,
      object,
      napi_key_include_prototypes,
      static_cast<napi_key_filter>(napi_key_enumerable | napi_key_skip_symbols),
      napi_key_numbers_to_strings,
      result);
}

napi_status NAPI_CDECL
napi_get_all_property_names(napi_env env,
                            napi_value object,
                            napi_key_collection_mode key_mode,
                            napi_key_filter key_filter,
                            napi_key_conversion key_conversion,
                            napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;
  CHECK_TO_OBJECT(env, context, obj, object);

  v8::PropertyFilter filter = v8::PropertyFilter::ALL_PROPERTIES;
  if (key_filter & napi_key_writable) {
    filter = static_cast<v8::PropertyFilter>(filter |
                                             v8::PropertyFilter::ONLY_WRITABLE);
  }
  if (key_filter & napi_key_enumerable) {
    filter = static_cast<v8::PropertyFilter>(
        filter | v8::PropertyFilter::ONLY_ENUMERABLE);
  }
  if (key_filter & napi_key_configurable) {
    filter = static_cast<v8::PropertyFilter>(
        filter | v8::PropertyFilter::ONLY_CONFIGURABLE);
  }
  if (key_filter & napi_key_skip_strings) {
    filter = static_cast<v8::PropertyFilter>(filter |
                                             v8::PropertyFilter::SKIP_STRINGS);
  }
  if (key_filter & napi_key_skip_symbols) {
    filter = static_cast<v8::PropertyFilter>(filter |
                                             v8::PropertyFilter::SKIP_SYMBOLS);
  }
  v8::KeyCollectionMode collection_mode;
  v8::KeyConversionMode conversion_mode;

  switch (key_mode) {
    case napi_key_include_prototypes:
      collection_mode = v8::KeyCollectionMode::kIncludePrototypes;
      break;
    case napi_key_own_only:
      collection_mode = v8::KeyCollectionMode::kOwnOnly;
      break;
    default:
      return napi_set_last_error(env, napi_invalid_arg);
  }

  switch (key_conversion) {
    case napi_key_keep_numbers:
      conversion_mode = v8::KeyConversionMode::kKeepNumbers;
      break;
    case napi_key_numbers_to_strings:
      conversion_mode = v8::KeyConversionMode::kConvertToString;
      break;
    default:
      return napi_set_last_error(env, napi_invalid_arg);
  }

  v8::MaybeLocal<v8::Array> maybe_all_propertynames =
      obj->GetPropertyNames(context,
                            collection_mode,
                            filter,
                            v8::IndexFilter::kIncludeIndices,
                            conversion_mode);

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(
      env, maybe_all_propertynames, napi_generic_failure);

  *result =
      v8impl::JsValueFromV8LocalValue(maybe_all_propertynames.ToLocalChecked());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_set_property(napi_env env,
                                         napi_value object,
                                         napi_value key,
                                         napi_value value) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, key);
  CHECK_ARG(env, value);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Local<v8::Value> k = v8impl::V8LocalValueFromJsValue(key);
  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  v8::Maybe<bool> set_maybe = obj->Set(context, k, val);

  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, set_maybe.FromMaybe(false), napi_generic_failure);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_has_property(napi_env env,
                                         napi_value object,
                                         napi_value key,
                                         bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);
  CHECK_ARG(env, key);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Local<v8::Value> k = v8impl::V8LocalValueFromJsValue(key);
  v8::Maybe<bool> has_maybe = obj->Has(context, k);

  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, has_maybe, napi_generic_failure);

  *result = has_maybe.FromMaybe(false);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_property(napi_env env,
                                         napi_value object,
                                         napi_value key,
                                         napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, key);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Value> k = v8impl::V8LocalValueFromJsValue(key);
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  auto get_maybe = obj->Get(context, k);

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, get_maybe, napi_generic_failure);

  v8::Local<v8::Value> val = get_maybe.ToLocalChecked();
  *result = v8impl::JsValueFromV8LocalValue(val);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_delete_property(napi_env env,
                                            napi_value object,
                                            napi_value key,
                                            bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, key);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Value> k = v8impl::V8LocalValueFromJsValue(key);
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);
  v8::Maybe<bool> delete_maybe = obj->Delete(context, k);
  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, delete_maybe, napi_generic_failure);

  if (result != nullptr) *result = delete_maybe.FromMaybe(false);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_has_own_property(napi_env env,
                                             napi_value object,
                                             napi_value key,
                                             bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, key);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);
  v8::Local<v8::Value> k = v8impl::V8LocalValueFromJsValue(key);
  RETURN_STATUS_IF_FALSE(env, k->IsName(), napi_name_expected);
  v8::Maybe<bool> has_maybe = obj->HasOwnProperty(context, k.As<v8::Name>());
  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, has_maybe, napi_generic_failure);
  *result = has_maybe.FromMaybe(false);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_set_named_property(napi_env env,
                                               napi_value object,
                                               const char* utf8name,
                                               napi_value value) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, value);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Local<v8::Name> key;
  CHECK_NEW_FROM_UTF8(env, key, utf8name);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  v8::Maybe<bool> set_maybe = obj->Set(context, key, val);

  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, set_maybe.FromMaybe(false), napi_generic_failure);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_has_named_property(napi_env env,
                                               napi_value object,
                                               const char* utf8name,
                                               bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Local<v8::Name> key;
  CHECK_NEW_FROM_UTF8(env, key, utf8name);

  v8::Maybe<bool> has_maybe = obj->Has(context, key);

  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, has_maybe, napi_generic_failure);

  *result = has_maybe.FromMaybe(false);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_named_property(napi_env env,
                                               napi_value object,
                                               const char* utf8name,
                                               napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Name> key;
  CHECK_NEW_FROM_UTF8(env, key, utf8name);

  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  auto get_maybe = obj->Get(context, key);

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, get_maybe, napi_generic_failure);

  v8::Local<v8::Value> val = get_maybe.ToLocalChecked();
  *result = v8impl::JsValueFromV8LocalValue(val);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_set_element(napi_env env,
                                        napi_value object,
                                        uint32_t index,
                                        napi_value value) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, value);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  auto set_maybe = obj->Set(context, index, val);

  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, set_maybe.FromMaybe(false), napi_generic_failure);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_has_element(napi_env env,
                                        napi_value object,
                                        uint32_t index,
                                        bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Maybe<bool> has_maybe = obj->Has(context, index);

  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, has_maybe, napi_generic_failure);

  *result = has_maybe.FromMaybe(false);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_element(napi_env env,
                                        napi_value object,
                                        uint32_t index,
                                        napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  auto get_maybe = obj->Get(context, index);

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, get_maybe, napi_generic_failure);

  *result = v8impl::JsValueFromV8LocalValue(get_maybe.ToLocalChecked());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_delete_element(napi_env env,
                                           napi_value object,
                                           uint32_t index,
                                           bool* result) {
  NAPI_PREAMBLE(env);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);
  v8::Maybe<bool> delete_maybe = obj->Delete(context, index);
  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, delete_maybe, napi_generic_failure);

  if (result != nullptr) *result = delete_maybe.FromMaybe(false);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL
napi_define_properties(napi_env env,
                       napi_value object,
                       size_t property_count,
                       const napi_property_descriptor* properties) {
  NAPI_PREAMBLE(env);
  if (property_count > 0) {
    CHECK_ARG(env, properties);
  }

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Object> obj;
  CHECK_TO_OBJECT(env, context, obj, object);

  for (size_t i = 0; i < property_count; i++) {
    const napi_property_descriptor* p = &properties[i];

    v8::Local<v8::Name> property_name;
    STATUS_CALL(v8impl::V8NameFromPropertyDescriptor(env, p, &property_name));

    if (p->getter != nullptr || p->setter != nullptr) {
      v8::Local<v8::Function> local_getter;
      v8::Local<v8::Function> local_setter;

      if (p->getter != nullptr) {
        STATUS_CALL(v8impl::FunctionCallbackWrapper::NewFunction(
            env, p->getter, p->data, &local_getter));
      }
      if (p->setter != nullptr) {
        STATUS_CALL(v8impl::FunctionCallbackWrapper::NewFunction(
            env, p->setter, p->data, &local_setter));
      }

      v8::PropertyDescriptor descriptor(local_getter, local_setter);
      descriptor.set_enumerable((p->attributes & napi_enumerable) != 0);
      descriptor.set_configurable((p->attributes & napi_configurable) != 0);

      auto define_maybe =
          obj->DefineProperty(context, property_name, descriptor);

      RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
          env, define_maybe.FromMaybe(false), napi_invalid_arg);
    } else if (p->method != nullptr) {
      v8::Local<v8::Function> method;
      STATUS_CALL(v8impl::FunctionCallbackWrapper::NewFunction(
          env, p->method, p->data, &method));
      v8::PropertyDescriptor descriptor(method,
                                        (p->attributes & napi_writable) != 0);
      descriptor.set_enumerable((p->attributes & napi_enumerable) != 0);
      descriptor.set_configurable((p->attributes & napi_configurable) != 0);

      auto define_maybe =
          obj->DefineProperty(context, property_name, descriptor);

      RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
          env, define_maybe.FromMaybe(false), napi_generic_failure);
    } else {
      v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(p->value);
      v8::Maybe<bool> define_maybe = v8::Just(false);

      if ((p->attributes & napi_enumerable) &&
          (p->attributes & napi_writable) &&
          (p->attributes & napi_configurable)) {
        // Use a fast path for this type of data property.
        define_maybe = obj->CreateDataProperty(context, property_name, value);
      } else {
        v8::PropertyDescriptor descriptor(value,
                                          (p->attributes & napi_writable) != 0);
        descriptor.set_enumerable((p->attributes & napi_enumerable) != 0);
        descriptor.set_configurable((p->attributes & napi_configurable) != 0);

        define_maybe = obj->DefineProperty(context, property_name, descriptor);
      }

      RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
          env, define_maybe.FromMaybe(false), napi_invalid_arg);
    }
  }

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_object_freeze(napi_env env, napi_value object) {
  NAPI_PREAMBLE(env);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Maybe<bool> set_frozen =
      obj->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);

  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, set_frozen.FromMaybe(false), napi_generic_failure);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_object_seal(napi_env env, napi_value object) {
  NAPI_PREAMBLE(env);

  v8::Local<v8::Context> context = env->context();
  v8::Local<v8::Object> obj;

  CHECK_TO_OBJECT(env, context, obj, object);

  v8::Maybe<bool> set_sealed =
      obj->SetIntegrityLevel(context, v8::IntegrityLevel::kSealed);

  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, set_sealed.FromMaybe(false), napi_generic_failure);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_is_array(napi_env env,
                                     napi_value value,
                                     bool* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  *result = val->IsArray();
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_array_length(napi_env env,
                                             napi_value value,
                                             uint32_t* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsArray(), napi_array_expected);

  v8::Local<v8::Array> arr = val.As<v8::Array>();
  *result = arr->Length();

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_strict_equals(napi_env env,
                                          napi_value lhs,
                                          napi_value rhs,
                                          bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, lhs);
  CHECK_ARG(env, rhs);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> a = v8impl::V8LocalValueFromJsValue(lhs);
  v8::Local<v8::Value> b = v8impl::V8LocalValueFromJsValue(rhs);

  *result = a->StrictEquals(b);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_prototype(napi_env env,
                                          napi_value object,
                                          napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Object> obj;
  CHECK_TO_OBJECT(env, context, obj, object);

  // This doesn't invokes Proxy's [[GetPrototypeOf]] handler.
  v8::Local<v8::Value> val = obj->GetPrototype();
  *result = v8impl::JsValueFromV8LocalValue(val);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_create_object(napi_env env, napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(v8::Object::New(env->isolate));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_array(napi_env env, napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(v8::Array::New(env->isolate));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_array_with_length(napi_env env,
                                                     size_t length,
                                                     napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result =
      v8impl::JsValueFromV8LocalValue(v8::Array::New(env->isolate, length));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_string_latin1(napi_env env,
                                                 const char* str,
                                                 size_t length,
                                                 napi_value* result) {
  return v8impl::NewString(env, str, length, result, [&](v8::Isolate* isolate) {
    return v8::String::NewFromOneByte(isolate,
                                      reinterpret_cast<const uint8_t*>(str),
                                      v8::NewStringType::kNormal,
                                      length);
  });
}

napi_status NAPI_CDECL napi_create_string_utf8(napi_env env,
                                               const char* str,
                                               size_t length,
                                               napi_value* result) {
  return v8impl::NewString(env, str, length, result, [&](v8::Isolate* isolate) {
    return v8::String::NewFromUtf8(
        isolate, str, v8::NewStringType::kNormal, static_cast<int>(length));
  });
}

napi_status NAPI_CDECL napi_create_string_utf16(napi_env env,
                                                const char16_t* str,
                                                size_t length,
                                                napi_value* result) {
  return v8impl::NewString(env, str, length, result, [&](v8::Isolate* isolate) {
    return v8::String::NewFromTwoByte(isolate,
                                      reinterpret_cast<const uint16_t*>(str),
                                      v8::NewStringType::kNormal,
                                      length);
  });
}

napi_status NAPI_CDECL node_api_create_external_string_latin1(
    napi_env env,
    char* str,
    size_t length,
    node_api_basic_finalize basic_finalize_callback,
    void* finalize_hint,
    napi_value* result,
    bool* copied) {
  napi_finalize finalize_callback =
      reinterpret_cast<napi_finalize>(basic_finalize_callback);
  return v8impl::NewExternalString(
      env,
      str,
      length,
      finalize_callback,
      finalize_hint,
      result,
      copied,
      napi_create_string_latin1,
      [&](v8::Isolate* isolate) {
        if (length == NAPI_AUTO_LENGTH) {
          length = (std::string_view(str)).length();
        }
        auto resource = new v8impl::ExternalOneByteStringResource(
            env, str, length, finalize_callback, finalize_hint);
        return v8::String::NewExternalOneByte(isolate, resource);
      });
}

napi_status NAPI_CDECL node_api_create_external_string_utf16(
    napi_env env,
    char16_t* str,
    size_t length,
    node_api_basic_finalize basic_finalize_callback,
    void* finalize_hint,
    napi_value* result,
    bool* copied) {
  napi_finalize finalize_callback =
      reinterpret_cast<napi_finalize>(basic_finalize_callback);
  return v8impl::NewExternalString(
      env,
      str,
      length,
      finalize_callback,
      finalize_hint,
      result,
      copied,
      napi_create_string_utf16,
      [&](v8::Isolate* isolate) {
        if (length == NAPI_AUTO_LENGTH) {
          length = (std::u16string_view(str)).length();
        }
        auto resource = new v8impl::ExternalStringResource(
            env, str, length, finalize_callback, finalize_hint);
        return v8::String::NewExternalTwoByte(isolate, resource);
      });
}

napi_status NAPI_CDECL node_api_create_property_key_utf16(napi_env env,
                                                          const char16_t* str,
                                                          size_t length,
                                                          napi_value* result) {
  return v8impl::NewString(env, str, length, result, [&](v8::Isolate* isolate) {
    return v8::String::NewFromTwoByte(isolate,
                                      reinterpret_cast<const uint16_t*>(str),
                                      v8::NewStringType::kInternalized,
                                      static_cast<int>(length));
  });
}

napi_status NAPI_CDECL napi_create_double(napi_env env,
                                          double value,
                                          napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result =
      v8impl::JsValueFromV8LocalValue(v8::Number::New(env->isolate, value));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_int32(napi_env env,
                                         int32_t value,
                                         napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result =
      v8impl::JsValueFromV8LocalValue(v8::Integer::New(env->isolate, value));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_uint32(napi_env env,
                                          uint32_t value,
                                          napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(
      v8::Integer::NewFromUnsigned(env->isolate, value));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_int64(napi_env env,
                                         int64_t value,
                                         napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(
      v8::Number::New(env->isolate, static_cast<double>(value)));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_bigint_int64(napi_env env,
                                                int64_t value,
                                                napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result =
      v8impl::JsValueFromV8LocalValue(v8::BigInt::New(env->isolate, value));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_bigint_uint64(napi_env env,
                                                 uint64_t value,
                                                 napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(
      v8::BigInt::NewFromUnsigned(env->isolate, value));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_bigint_words(napi_env env,
                                                int sign_bit,
                                                size_t word_count,
                                                const uint64_t* words,
                                                napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, words);
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();

  RETURN_STATUS_IF_FALSE(env, word_count <= INT_MAX, napi_invalid_arg);

  v8::MaybeLocal<v8::BigInt> b =
      v8::BigInt::NewFromWords(context, sign_bit, word_count, words);

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, b, napi_generic_failure);

  *result = v8impl::JsValueFromV8LocalValue(b.ToLocalChecked());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_boolean(napi_env env,
                                        bool value,
                                        napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  v8::Isolate* isolate = env->isolate;

  if (value) {
    *result = v8impl::JsValueFromV8LocalValue(v8::True(isolate));
  } else {
    *result = v8impl::JsValueFromV8LocalValue(v8::False(isolate));
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_symbol(napi_env env,
                                          napi_value description,
                                          napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  v8::Isolate* isolate = env->isolate;

  if (description == nullptr) {
    *result = v8impl::JsValueFromV8LocalValue(v8::Symbol::New(isolate));
  } else {
    v8::Local<v8::Value> desc = v8impl::V8LocalValueFromJsValue(description);
    RETURN_STATUS_IF_FALSE(env, desc->IsString(), napi_string_expected);

    *result = v8impl::JsValueFromV8LocalValue(
        v8::Symbol::New(isolate, desc.As<v8::String>()));
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL node_api_symbol_for(napi_env env,
                                           const char* utf8description,
                                           size_t length,
                                           napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  napi_value js_description_string;
  STATUS_CALL(napi_create_string_utf8(
      env, utf8description, length, &js_description_string));
  v8::Local<v8::String> description_string =
      v8impl::V8LocalValueFromJsValue(js_description_string).As<v8::String>();

  *result = v8impl::JsValueFromV8LocalValue(
      v8::Symbol::For(env->isolate, description_string));

  return napi_clear_last_error(env);
}

static inline napi_status set_error_code(napi_env env,
                                         v8::Local<v8::Value> error,
                                         napi_value code,
                                         const char* code_cstring) {
  if ((code != nullptr) || (code_cstring != nullptr)) {
    v8::Local<v8::Context> context = env->context();
    v8::Local<v8::Object> err_object = error.As<v8::Object>();

    v8::Local<v8::Value> code_value = v8impl::V8LocalValueFromJsValue(code);
    if (code != nullptr) {
      code_value = v8impl::V8LocalValueFromJsValue(code);
      RETURN_STATUS_IF_FALSE(env, code_value->IsString(), napi_string_expected);
    } else {
      CHECK_NEW_FROM_UTF8(env, code_value, code_cstring);
    }

    v8::Local<v8::Name> code_key;
    CHECK_NEW_FROM_UTF8(env, code_key, "code");

    v8::Maybe<bool> set_maybe = err_object->Set(context, code_key, code_value);
    RETURN_STATUS_IF_FALSE(
        env, set_maybe.FromMaybe(false), napi_generic_failure);
  }
  return napi_ok;
}

napi_status NAPI_CDECL napi_create_error(napi_env env,
                                         napi_value code,
                                         napi_value msg,
                                         napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> message_value = v8impl::V8LocalValueFromJsValue(msg);
  RETURN_STATUS_IF_FALSE(env, message_value->IsString(), napi_string_expected);

  v8::Local<v8::Value> error_obj =
      v8::Exception::Error(message_value.As<v8::String>());
  STATUS_CALL(set_error_code(env, error_obj, code, nullptr));

  *result = v8impl::JsValueFromV8LocalValue(error_obj);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_type_error(napi_env env,
                                              napi_value code,
                                              napi_value msg,
                                              napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> message_value = v8impl::V8LocalValueFromJsValue(msg);
  RETURN_STATUS_IF_FALSE(env, message_value->IsString(), napi_string_expected);

  v8::Local<v8::Value> error_obj =
      v8::Exception::TypeError(message_value.As<v8::String>());
  STATUS_CALL(set_error_code(env, error_obj, code, nullptr));

  *result = v8impl::JsValueFromV8LocalValue(error_obj);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_range_error(napi_env env,
                                               napi_value code,
                                               napi_value msg,
                                               napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> message_value = v8impl::V8LocalValueFromJsValue(msg);
  RETURN_STATUS_IF_FALSE(env, message_value->IsString(), napi_string_expected);

  v8::Local<v8::Value> error_obj =
      v8::Exception::RangeError(message_value.As<v8::String>());
  STATUS_CALL(set_error_code(env, error_obj, code, nullptr));

  *result = v8impl::JsValueFromV8LocalValue(error_obj);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL node_api_create_syntax_error(napi_env env,
                                                    napi_value code,
                                                    napi_value msg,
                                                    napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, msg);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> message_value = v8impl::V8LocalValueFromJsValue(msg);
  RETURN_STATUS_IF_FALSE(env, message_value->IsString(), napi_string_expected);

  v8::Local<v8::Value> error_obj =
      v8::Exception::SyntaxError(message_value.As<v8::String>());
  STATUS_CALL(set_error_code(env, error_obj, code, nullptr));

  *result = v8impl::JsValueFromV8LocalValue(error_obj);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_typeof(napi_env env,
                                   napi_value value,
                                   napi_valuetype* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> v = v8impl::V8LocalValueFromJsValue(value);

  if (v->IsNumber()) {
    *result = napi_number;
  } else if (v->IsBigInt()) {
    *result = napi_bigint;
  } else if (v->IsString()) {
    *result = napi_string;
  } else if (v->IsFunction()) {
    // This test has to come before IsObject because IsFunction
    // implies IsObject
    *result = napi_function;
  } else if (v->IsExternal()) {
    // This test has to come before IsObject because IsExternal
    // implies IsObject
    *result = napi_external;
  } else if (v->IsObject()) {
    *result = napi_object;
  } else if (v->IsBoolean()) {
    *result = napi_boolean;
  } else if (v->IsUndefined()) {
    *result = napi_undefined;
  } else if (v->IsSymbol()) {
    *result = napi_symbol;
  } else if (v->IsNull()) {
    *result = napi_null;
  } else {
    // Should not get here unless V8 has added some new kind of value.
    return napi_set_last_error(env, napi_invalid_arg);
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_undefined(napi_env env, napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(v8::Undefined(env->isolate));

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_null(napi_env env, napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(v8::Null(env->isolate));

  return napi_clear_last_error(env);
}

// Gets all callback info in a single call. (Ugly, but faster.)
napi_status NAPI_CDECL napi_get_cb_info(
    napi_env env,               // [in] NAPI environment handle
    napi_callback_info cbinfo,  // [in] Opaque callback-info handle
    size_t* argc,      // [in-out] Specifies the size of the provided argv array
                       // and receives the actual count of args.
    napi_value* argv,  // [out] Array of values
    napi_value* this_arg,  // [out] Receives the JS 'this' arg for the call
    void** data) {         // [out] Receives the data pointer for the callback.
  CHECK_ENV(env);
  CHECK_ARG(env, cbinfo);

  v8impl::CallbackWrapper* info =
      reinterpret_cast<v8impl::CallbackWrapper*>(cbinfo);

  if (argv != nullptr) {
    CHECK_ARG(env, argc);
    info->Args(argv, *argc);
  }
  if (argc != nullptr) {
    *argc = info->ArgsLength();
  }
  if (this_arg != nullptr) {
    *this_arg = info->This();
  }
  if (data != nullptr) {
    *data = info->Data();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_new_target(napi_env env,
                                           napi_callback_info cbinfo,
                                           napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, cbinfo);
  CHECK_ARG(env, result);

  v8impl::CallbackWrapper* info =
      reinterpret_cast<v8impl::CallbackWrapper*>(cbinfo);

  *result = info->GetNewTarget();
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_call_function(napi_env env,
                                          napi_value recv,
                                          napi_value func,
                                          size_t argc,
                                          const napi_value* argv,
                                          napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, recv);
  if (argc > 0) {
    CHECK_ARG(env, argv);
  }

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Value> v8recv = v8impl::V8LocalValueFromJsValue(recv);

  v8::Local<v8::Function> v8func;
  CHECK_TO_FUNCTION(env, v8func, func);

  auto maybe = v8func->Call(
      context,
      v8recv,
      argc,
      reinterpret_cast<v8::Local<v8::Value>*>(const_cast<napi_value*>(argv)));

  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, maybe, napi_generic_failure);
  if (result != nullptr) {
    *result = v8impl::JsValueFromV8LocalValue(maybe.ToLocalChecked());
  }
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_global(napi_env env, napi_value* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsValueFromV8LocalValue(env->context()->Global());

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_throw(napi_env env, napi_value error) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, error);

  v8::Isolate* isolate = env->isolate;

  isolate->ThrowException(v8impl::V8LocalValueFromJsValue(error));
  // any VM calls after this point and before returning
  // to the javascript invoker will fail
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_throw_error(napi_env env,
                                        const char* code,
                                        const char* msg) {
  NAPI_PREAMBLE(env);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::String> str;
  CHECK_NEW_FROM_UTF8(env, str, msg);

  v8::Local<v8::Value> error_obj = v8::Exception::Error(str);
  STATUS_CALL(set_error_code(env, error_obj, nullptr, code));

  isolate->ThrowException(error_obj);
  // any VM calls after this point and before returning
  // to the javascript invoker will fail
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_throw_type_error(napi_env env,
                                             const char* code,
                                             const char* msg) {
  NAPI_PREAMBLE(env);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::String> str;
  CHECK_NEW_FROM_UTF8(env, str, msg);

  v8::Local<v8::Value> error_obj = v8::Exception::TypeError(str);
  STATUS_CALL(set_error_code(env, error_obj, nullptr, code));

  isolate->ThrowException(error_obj);
  // any VM calls after this point and before returning
  // to the javascript invoker will fail
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_throw_range_error(napi_env env,
                                              const char* code,
                                              const char* msg) {
  NAPI_PREAMBLE(env);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::String> str;
  CHECK_NEW_FROM_UTF8(env, str, msg);

  v8::Local<v8::Value> error_obj = v8::Exception::RangeError(str);
  STATUS_CALL(set_error_code(env, error_obj, nullptr, code));

  isolate->ThrowException(error_obj);
  // any VM calls after this point and before returning
  // to the javascript invoker will fail
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL node_api_throw_syntax_error(napi_env env,
                                                   const char* code,
                                                   const char* msg) {
  NAPI_PREAMBLE(env);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::String> str;
  CHECK_NEW_FROM_UTF8(env, str, msg);

  v8::Local<v8::Value> error_obj = v8::Exception::SyntaxError(str);
  STATUS_CALL(set_error_code(env, error_obj, nullptr, code));

  isolate->ThrowException(error_obj);
  // any VM calls after this point and before returning
  // to the javascript invoker will fail
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_is_error(napi_env env,
                                     napi_value value,
                                     bool* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot
  // throw JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  *result = val->IsNativeError();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_double(napi_env env,
                                             napi_value value,
                                             double* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsNumber(), napi_number_expected);

  *result = val.As<v8::Number>()->Value();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_int32(napi_env env,
                                            napi_value value,
                                            int32_t* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  if (val->IsInt32()) {
    *result = val.As<v8::Int32>()->Value();
  } else {
    RETURN_STATUS_IF_FALSE(env, val->IsNumber(), napi_number_expected);

    // Empty context: https://github.com/nodejs/node/issues/14379
    v8::Local<v8::Context> context;
    *result = val->Int32Value(context).FromJust();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_uint32(napi_env env,
                                             napi_value value,
                                             uint32_t* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  if (val->IsUint32()) {
    *result = val.As<v8::Uint32>()->Value();
  } else {
    RETURN_STATUS_IF_FALSE(env, val->IsNumber(), napi_number_expected);

    // Empty context: https://github.com/nodejs/node/issues/14379
    v8::Local<v8::Context> context;
    *result = val->Uint32Value(context).FromJust();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_int64(napi_env env,
                                            napi_value value,
                                            int64_t* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  // This is still a fast path very likely to be taken.
  if (val->IsInt32()) {
    *result = val.As<v8::Int32>()->Value();
    return napi_clear_last_error(env);
  }

  RETURN_STATUS_IF_FALSE(env, val->IsNumber(), napi_number_expected);

  // v8::Value::IntegerValue() converts NaN, +Inf, and -Inf to INT64_MIN,
  // inconsistent with v8::Value::Int32Value() which converts those values to 0.
  // Special-case all non-finite values to match that behavior.
  double doubleValue = val.As<v8::Number>()->Value();
  if (std::isfinite(doubleValue)) {
    // Empty context: https://github.com/nodejs/node/issues/14379
    v8::Local<v8::Context> context;
    *result = val->IntegerValue(context).FromJust();
  } else {
    *result = 0;
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_bigint_int64(napi_env env,
                                                   napi_value value,
                                                   int64_t* result,
                                                   bool* lossless) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  CHECK_ARG(env, lossless);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  RETURN_STATUS_IF_FALSE(env, val->IsBigInt(), napi_bigint_expected);

  *result = val.As<v8::BigInt>()->Int64Value(lossless);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_bigint_uint64(napi_env env,
                                                    napi_value value,
                                                    uint64_t* result,
                                                    bool* lossless) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);
  CHECK_ARG(env, lossless);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  RETURN_STATUS_IF_FALSE(env, val->IsBigInt(), napi_bigint_expected);

  *result = val.As<v8::BigInt>()->Uint64Value(lossless);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_bigint_words(napi_env env,
                                                   napi_value value,
                                                   int* sign_bit,
                                                   size_t* word_count,
                                                   uint64_t* words) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, word_count);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);

  RETURN_STATUS_IF_FALSE(env, val->IsBigInt(), napi_bigint_expected);

  v8::Local<v8::BigInt> big = val.As<v8::BigInt>();

  int word_count_int = *word_count;

  if (sign_bit == nullptr && words == nullptr) {
    word_count_int = big->WordCount();
  } else {
    CHECK_ARG(env, sign_bit);
    CHECK_ARG(env, words);
    big->ToWordsArray(sign_bit, &word_count_int, words);
  }

  *word_count = word_count_int;

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_value_bool(napi_env env,
                                           napi_value value,
                                           bool* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsBoolean(), napi_boolean_expected);

  *result = val.As<v8::Boolean>()->Value();

  return napi_clear_last_error(env);
}

// Copies a JavaScript string into a LATIN-1 string buffer. The result is the
// number of bytes (excluding the null terminator) copied into buf.
// A sufficient buffer size should be greater than the length of string,
// reserving space for null terminator.
// If bufsize is insufficient, the string will be truncated and null terminated.
// If buf is NULL, this method returns the length of the string (in bytes)
// via the result parameter.
// The result argument is optional unless buf is NULL.
napi_status NAPI_CDECL napi_get_value_string_latin1(
    napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsString(), napi_string_expected);

  if (!buf) {
    CHECK_ARG(env, result);
    *result = val.As<v8::String>()->Length();
  } else if (bufsize != 0) {
    int copied =
        val.As<v8::String>()->WriteOneByte(env->isolate,
                                           reinterpret_cast<uint8_t*>(buf),
                                           0,
                                           bufsize - 1,
                                           v8::String::NO_NULL_TERMINATION);

    buf[copied] = '\0';
    if (result != nullptr) {
      *result = copied;
    }
  } else if (result != nullptr) {
    *result = 0;
  }

  return napi_clear_last_error(env);
}

// Copies a JavaScript string into a UTF-8 string buffer. The result is the
// number of bytes (excluding the null terminator) copied into buf.
// A sufficient buffer size should be greater than the length of string,
// reserving space for null terminator.
// If bufsize is insufficient, the string will be truncated and null terminated.
// If buf is NULL, this method returns the length of the string (in bytes)
// via the result parameter.
// The result argument is optional unless buf is NULL.
napi_status NAPI_CDECL napi_get_value_string_utf8(
    napi_env env, napi_value value, char* buf, size_t bufsize, size_t* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsString(), napi_string_expected);

  if (!buf) {
    CHECK_ARG(env, result);
    *result = val.As<v8::String>()->Utf8Length(env->isolate);
  } else if (bufsize != 0) {
    int copied = val.As<v8::String>()->WriteUtf8(
        env->isolate,
        buf,
        bufsize - 1,
        nullptr,
        v8::String::REPLACE_INVALID_UTF8 | v8::String::NO_NULL_TERMINATION);

    buf[copied] = '\0';
    if (result != nullptr) {
      *result = copied;
    }
  } else if (result != nullptr) {
    *result = 0;
  }

  return napi_clear_last_error(env);
}

// Copies a JavaScript string into a UTF-16 string buffer. The result is the
// number of 2-byte code units (excluding the null terminator) copied into buf.
// A sufficient buffer size should be greater than the length of string,
// reserving space for null terminator.
// If bufsize is insufficient, the string will be truncated and null terminated.
// If buf is NULL, this method returns the length of the string (in 2-byte
// code units) via the result parameter.
// The result argument is optional unless buf is NULL.
napi_status NAPI_CDECL napi_get_value_string_utf16(napi_env env,
                                                   napi_value value,
                                                   char16_t* buf,
                                                   size_t bufsize,
                                                   size_t* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsString(), napi_string_expected);

  if (!buf) {
    CHECK_ARG(env, result);
    // V8 assumes UTF-16 length is the same as the number of characters.
    *result = val.As<v8::String>()->Length();
  } else if (bufsize != 0) {
    int copied = val.As<v8::String>()->Write(env->isolate,
                                             reinterpret_cast<uint16_t*>(buf),
                                             0,
                                             bufsize - 1,
                                             v8::String::NO_NULL_TERMINATION);

    buf[copied] = '\0';
    if (result != nullptr) {
      *result = copied;
    }
  } else if (result != nullptr) {
    *result = 0;
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_coerce_to_bool(napi_env env,
                                           napi_value value,
                                           napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::Boolean> b =
      v8impl::V8LocalValueFromJsValue(value)->ToBoolean(isolate);
  *result = v8impl::JsValueFromV8LocalValue(b);
  return GET_RETURN_STATUS(env);
}

#define GEN_COERCE_FUNCTION(UpperCaseName, MixedCaseName, LowerCaseName)       \
  napi_status NAPI_CDECL napi_coerce_to_##LowerCaseName(                       \
      napi_env env, napi_value value, napi_value* result) {                    \
    NAPI_PREAMBLE(env);                                                        \
    CHECK_ARG(env, value);                                                     \
    CHECK_ARG(env, result);                                                    \
                                                                               \
    v8::Local<v8::Context> context = env->context();                           \
    v8::Local<v8::MixedCaseName> str;                                          \
                                                                               \
    CHECK_TO_##UpperCaseName(env, context, str, value);                        \
                                                                               \
    *result = v8impl::JsValueFromV8LocalValue(str);                            \
    return GET_RETURN_STATUS(env);                                             \
  }

GEN_COERCE_FUNCTION(NUMBER, Number, number)
GEN_COERCE_FUNCTION(OBJECT, Object, object)
GEN_COERCE_FUNCTION(STRING, String, string)

#undef GEN_COERCE_FUNCTION

napi_status NAPI_CDECL napi_wrap(napi_env env,
                                 napi_value js_object,
                                 void* native_object,
                                 node_api_basic_finalize basic_finalize_cb,
                                 void* finalize_hint,
                                 napi_ref* result) {
  napi_finalize finalize_cb =
      reinterpret_cast<napi_finalize>(basic_finalize_cb);
  return v8impl::Wrap(
      env, js_object, native_object, finalize_cb, finalize_hint, result);
}

napi_status NAPI_CDECL napi_unwrap(napi_env env,
                                   napi_value obj,
                                   void** result) {
  return v8impl::Unwrap(env, obj, result, v8impl::KeepWrap);
}

napi_status NAPI_CDECL napi_remove_wrap(napi_env env,
                                        napi_value obj,
                                        void** result) {
  return v8impl::Unwrap(env, obj, result, v8impl::RemoveWrap);
}

napi_status NAPI_CDECL
napi_create_external(napi_env env,
                     void* data,
                     node_api_basic_finalize basic_finalize_cb,
                     void* finalize_hint,
                     napi_value* result) {
  napi_finalize finalize_cb =
      reinterpret_cast<napi_finalize>(basic_finalize_cb);
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Local<v8::External> external_value =
      v8impl::ExternalWrapper::New(env, data);

  if (finalize_cb) {
    // The Reference object will delete itself after invoking the finalizer
    // callback.
    v8impl::Reference::New(env,
                           external_value,
                           0,
                           v8impl::Ownership::kRuntime,
                           finalize_cb,
                           data,
                           finalize_hint);
  }

  *result = v8impl::JsValueFromV8LocalValue(external_value);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_type_tag_object(napi_env env,
                                            napi_value object_or_external,
                                            const napi_type_tag* type_tag) {
  NAPI_PREAMBLE(env);
  v8::Local<v8::Context> context = env->context();

  CHECK_ARG(env, object_or_external);
  v8::Local<v8::Value> val =
      v8impl::V8LocalValueFromJsValue(object_or_external);
  if (val->IsExternal()) {
    v8impl::ExternalWrapper* wrapper =
        v8impl::ExternalWrapper::From(val.As<v8::External>());
    RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
        env, wrapper->TypeTag(type_tag), napi_invalid_arg);
    return GET_RETURN_STATUS(env);
  }

  v8::Local<v8::Object> obj;
  CHECK_TO_OBJECT_WITH_PREAMBLE(env, context, obj, object_or_external);
  CHECK_ARG_WITH_PREAMBLE(env, type_tag);

  auto key = NAPI_PRIVATE_KEY(context, type_tag);
  auto maybe_has = obj->HasPrivate(context, key);
  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, maybe_has, napi_generic_failure);
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, !maybe_has.FromJust(), napi_invalid_arg);

  auto tag = v8::BigInt::NewFromWords(
      context, 0, 2, reinterpret_cast<const uint64_t*>(type_tag));
  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, tag, napi_generic_failure);

  auto maybe_set = obj->SetPrivate(context, key, tag.ToLocalChecked());
  CHECK_MAYBE_NOTHING_WITH_PREAMBLE(env, maybe_set, napi_generic_failure);
  RETURN_STATUS_IF_FALSE_WITH_PREAMBLE(
      env, maybe_set.FromJust(), napi_generic_failure);

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_check_object_type_tag(napi_env env,
                                                  napi_value object_or_external,
                                                  const napi_type_tag* type_tag,
                                                  bool* result) {
  NAPI_PREAMBLE(env);
  v8::Local<v8::Context> context = env->context();

  CHECK_ARG(env, object_or_external);
  v8::Local<v8::Value> obj_val =
      v8impl::V8LocalValueFromJsValue(object_or_external);
  if (obj_val->IsExternal()) {
    v8impl::ExternalWrapper* wrapper =
        v8impl::ExternalWrapper::From(obj_val.As<v8::External>());
    *result = wrapper->CheckTypeTag(type_tag);
    return GET_RETURN_STATUS(env);
  }

  v8::Local<v8::Object> obj;
  CHECK_TO_OBJECT_WITH_PREAMBLE(env, context, obj, object_or_external);
  CHECK_ARG_WITH_PREAMBLE(env, type_tag);
  CHECK_ARG_WITH_PREAMBLE(env, result);

  auto maybe_value =
      obj->GetPrivate(context, NAPI_PRIVATE_KEY(context, type_tag));
  CHECK_MAYBE_EMPTY_WITH_PREAMBLE(env, maybe_value, napi_generic_failure);
  v8::Local<v8::Value> val = maybe_value.ToLocalChecked();

  // We consider the type check to have failed unless we reach the line below
  // where we set whether the type check succeeded or not based on the
  // comparison of the two type tags.
  *result = false;
  if (val->IsBigInt()) {
    int sign;
    int size = 2;
    napi_type_tag tag;
    val.As<v8::BigInt>()->ToWordsArray(
        &sign, &size, reinterpret_cast<uint64_t*>(&tag));
    if (sign == 0) {
      if (size == 2) {
        *result =
            (tag.lower == type_tag->lower && tag.upper == type_tag->upper);
      } else if (size == 1) {
        *result = (tag.lower == type_tag->lower && 0 == type_tag->upper);
      } else if (size == 0) {
        *result = (0 == type_tag->lower && 0 == type_tag->upper);
      }
    }
  }

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_value_external(napi_env env,
                                               napi_value value,
                                               void** result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsExternal(), napi_invalid_arg);

  v8::Local<v8::External> external_value = val.As<v8::External>();
  *result = v8impl::ExternalWrapper::From(external_value)->Data();

  return napi_clear_last_error(env);
}

// Set initial_refcount to 0 for a weak reference, >0 for a strong reference.
napi_status NAPI_CDECL napi_create_reference(napi_env env,
                                             napi_value value,
                                             uint32_t initial_refcount,
                                             napi_ref* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> v8_value = v8impl::V8LocalValueFromJsValue(value);
  if (env->module_api_version != NAPI_VERSION_EXPERIMENTAL) {
    if (!(v8_value->IsObject() || v8_value->IsFunction() ||
          v8_value->IsSymbol())) {
      return napi_set_last_error(env, napi_invalid_arg);
    }
  }

  v8impl::Reference* reference = v8impl::Reference::New(
      env, v8_value, initial_refcount, v8impl::Ownership::kUserland);

  *result = reinterpret_cast<napi_ref>(reference);
  return napi_clear_last_error(env);
}

// Deletes a reference. The referenced value is released, and may be GC'd unless
// there are other references to it.
napi_status NAPI_CDECL napi_delete_reference(napi_env env, napi_ref ref) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, ref);

  delete reinterpret_cast<v8impl::Reference*>(ref);

  return napi_clear_last_error(env);
}

// Increments the reference count, optionally returning the resulting count.
// After this call the reference will be a strong reference because its
// refcount is >0, and the referenced object is effectively "pinned".
// Calling this when the refcount is 0 and the object is unavailable
// results in an error.
napi_status NAPI_CDECL napi_reference_ref(napi_env env,
                                          napi_ref ref,
                                          uint32_t* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, ref);

  v8impl::Reference* reference = reinterpret_cast<v8impl::Reference*>(ref);
  uint32_t count = reference->Ref();

  if (result != nullptr) {
    *result = count;
  }

  return napi_clear_last_error(env);
}

// Decrements the reference count, optionally returning the resulting count. If
// the result is 0 the reference is now weak and the object may be GC'd at any
// time if there are no other references. Calling this when the refcount is
// already 0 results in an error.
napi_status NAPI_CDECL napi_reference_unref(napi_env env,
                                            napi_ref ref,
                                            uint32_t* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, ref);

  v8impl::Reference* reference = reinterpret_cast<v8impl::Reference*>(ref);

  if (reference->RefCount() == 0) {
    return napi_set_last_error(env, napi_generic_failure);
  }

  uint32_t count = reference->Unref();

  if (result != nullptr) {
    *result = count;
  }

  return napi_clear_last_error(env);
}

// Attempts to get a referenced value. If the reference is weak, the value might
// no longer be available, in that case the call is still successful but the
// result is NULL.
napi_status NAPI_CDECL napi_get_reference_value(napi_env env,
                                                napi_ref ref,
                                                napi_value* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, ref);
  CHECK_ARG(env, result);

  v8impl::Reference* reference = reinterpret_cast<v8impl::Reference*>(ref);
  *result = v8impl::JsValueFromV8LocalValue(reference->Get());

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_open_handle_scope(napi_env env,
                                              napi_handle_scope* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsHandleScopeFromV8HandleScope(
      new v8impl::HandleScopeWrapper(env->isolate));
  env->open_handle_scopes++;
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_close_handle_scope(napi_env env,
                                               napi_handle_scope scope) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, scope);
  if (env->open_handle_scopes == 0) {
    return napi_handle_scope_mismatch;
  }

  env->open_handle_scopes--;
  delete v8impl::V8HandleScopeFromJsHandleScope(scope);
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_open_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = v8impl::JsEscapableHandleScopeFromV8EscapableHandleScope(
      new v8impl::EscapableHandleScopeWrapper(env->isolate));
  env->open_handle_scopes++;
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_close_escapable_handle_scope(
    napi_env env, napi_escapable_handle_scope scope) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, scope);
  if (env->open_handle_scopes == 0) {
    return napi_handle_scope_mismatch;
  }

  delete v8impl::V8EscapableHandleScopeFromJsEscapableHandleScope(scope);
  env->open_handle_scopes--;
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_escape_handle(napi_env env,
                                          napi_escapable_handle_scope scope,
                                          napi_value escapee,
                                          napi_value* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, scope);
  CHECK_ARG(env, escapee);
  CHECK_ARG(env, result);

  v8impl::EscapableHandleScopeWrapper* s =
      v8impl::V8EscapableHandleScopeFromJsEscapableHandleScope(scope);
  if (!s->escape_called()) {
    *result = v8impl::JsValueFromV8LocalValue(
        s->Escape(v8impl::V8LocalValueFromJsValue(escapee)));
    return napi_clear_last_error(env);
  }
  return napi_set_last_error(env, napi_escape_called_twice);
}

napi_status NAPI_CDECL napi_new_instance(napi_env env,
                                         napi_value constructor,
                                         size_t argc,
                                         const napi_value* argv,
                                         napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, constructor);
  if (argc > 0) {
    CHECK_ARG(env, argv);
  }
  CHECK_ARG(env, result);

  v8::Local<v8::Context> context = env->context();

  v8::Local<v8::Function> ctor;
  CHECK_TO_FUNCTION(env, ctor, constructor);

  auto maybe = ctor->NewInstance(
      context,
      argc,
      reinterpret_cast<v8::Local<v8::Value>*>(const_cast<napi_value*>(argv)));

  CHECK_MAYBE_EMPTY(env, maybe, napi_pending_exception);

  *result = v8impl::JsValueFromV8LocalValue(maybe.ToLocalChecked());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_instanceof(napi_env env,
                                       napi_value object,
                                       napi_value constructor,
                                       bool* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, object);
  CHECK_ARG(env, result);

  *result = false;

  v8::Local<v8::Object> ctor;
  v8::Local<v8::Context> context = env->context();

  CHECK_TO_OBJECT(env, context, ctor, constructor);

  if (!ctor->IsFunction()) {
    napi_throw_type_error(
        env, "ERR_NAPI_CONS_FUNCTION", "Constructor must be a function");

    return napi_set_last_error(env, napi_function_expected);
  }

  napi_status status = napi_generic_failure;

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(object);
  auto maybe_result = val->InstanceOf(context, ctor);
  CHECK_MAYBE_NOTHING(env, maybe_result, status);
  *result = maybe_result.FromJust();
  return GET_RETURN_STATUS(env);
}

// Methods to support catching exceptions
napi_status NAPI_CDECL napi_is_exception_pending(napi_env env, bool* result) {
  // NAPI_PREAMBLE is not used here: this function must execute when there is a
  // pending exception.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  *result = !env->last_exception.IsEmpty();
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_and_clear_last_exception(napi_env env,
                                                         napi_value* result) {
  // NAPI_PREAMBLE is not used here: this function must execute when there is a
  // pending exception.
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, result);

  if (env->last_exception.IsEmpty()) {
    return napi_get_undefined(env, result);
  } else {
    *result = v8impl::JsValueFromV8LocalValue(
        v8::Local<v8::Value>::New(env->isolate, env->last_exception));
    env->last_exception.Reset();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_is_arraybuffer(napi_env env,
                                           napi_value value,
                                           bool* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  *result = val->IsArrayBuffer();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_arraybuffer(napi_env env,
                                               size_t byte_length,
                                               void** data,
                                               napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::Isolate* isolate = env->isolate;
  v8::Local<v8::ArrayBuffer> buffer =
      v8::ArrayBuffer::New(isolate, byte_length);

  // Optionally return a pointer to the buffer's data, to avoid another call to
  // retrieve it.
  if (data != nullptr) {
    *data = buffer->Data();
  }

  *result = v8impl::JsValueFromV8LocalValue(buffer);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL
napi_create_external_arraybuffer(napi_env env,
                                 void* external_data,
                                 size_t byte_length,
                                 node_api_basic_finalize finalize_cb,
                                 void* finalize_hint,
                                 napi_value* result) {
  // The API contract here is that the cleanup function runs on the JS thread,
  // and is able to use napi_env. Implementing that properly is hard, so use the
  // `Buffer` variant for easier implementation.
  napi_value buffer;
  STATUS_CALL(napi_create_external_buffer(
      env, byte_length, external_data, finalize_cb, finalize_hint, &buffer));
  return napi_get_typedarray_info(
      env, buffer, nullptr, nullptr, nullptr, result, nullptr);
}

napi_status NAPI_CDECL napi_get_arraybuffer_info(napi_env env,
                                                 napi_value arraybuffer,
                                                 void** data,
                                                 size_t* byte_length) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, arraybuffer);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(arraybuffer);
  RETURN_STATUS_IF_FALSE(env, value->IsArrayBuffer(), napi_invalid_arg);

  v8::Local<v8::ArrayBuffer> ab = value.As<v8::ArrayBuffer>();

  if (data != nullptr) {
    *data = ab->Data();
  }

  if (byte_length != nullptr) {
    *byte_length = ab->ByteLength();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_is_typedarray(napi_env env,
                                          napi_value value,
                                          bool* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  *result = val->IsTypedArray();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_typedarray(napi_env env,
                                              napi_typedarray_type type,
                                              size_t length,
                                              napi_value arraybuffer,
                                              size_t byte_offset,
                                              napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, arraybuffer);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(arraybuffer);
  RETURN_STATUS_IF_FALSE(env, value->IsArrayBuffer(), napi_invalid_arg);

  v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
  v8::Local<v8::TypedArray> typedArray;

  switch (type) {
    case napi_int8_array:
      CREATE_TYPED_ARRAY(
          env, Int8Array, 1, buffer, byte_offset, length, typedArray);
      break;
    case napi_uint8_array:
      CREATE_TYPED_ARRAY(
          env, Uint8Array, 1, buffer, byte_offset, length, typedArray);
      break;
    case napi_uint8_clamped_array:
      CREATE_TYPED_ARRAY(
          env, Uint8ClampedArray, 1, buffer, byte_offset, length, typedArray);
      break;
    case napi_int16_array:
      CREATE_TYPED_ARRAY(
          env, Int16Array, 2, buffer, byte_offset, length, typedArray);
      break;
    case napi_uint16_array:
      CREATE_TYPED_ARRAY(
          env, Uint16Array, 2, buffer, byte_offset, length, typedArray);
      break;
    case napi_int32_array:
      CREATE_TYPED_ARRAY(
          env, Int32Array, 4, buffer, byte_offset, length, typedArray);
      break;
    case napi_uint32_array:
      CREATE_TYPED_ARRAY(
          env, Uint32Array, 4, buffer, byte_offset, length, typedArray);
      break;
    case napi_float32_array:
      CREATE_TYPED_ARRAY(
          env, Float32Array, 4, buffer, byte_offset, length, typedArray);
      break;
    case napi_float64_array:
      CREATE_TYPED_ARRAY(
          env, Float64Array, 8, buffer, byte_offset, length, typedArray);
      break;
    case napi_bigint64_array:
      CREATE_TYPED_ARRAY(
          env, BigInt64Array, 8, buffer, byte_offset, length, typedArray);
      break;
    case napi_biguint64_array:
      CREATE_TYPED_ARRAY(
          env, BigUint64Array, 8, buffer, byte_offset, length, typedArray);
      break;
    default:
      return napi_set_last_error(env, napi_invalid_arg);
  }

  *result = v8impl::JsValueFromV8LocalValue(typedArray);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_get_typedarray_info(napi_env env,
                                                napi_value typedarray,
                                                napi_typedarray_type* type,
                                                size_t* length,
                                                void** data,
                                                napi_value* arraybuffer,
                                                size_t* byte_offset) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, typedarray);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(typedarray);
  RETURN_STATUS_IF_FALSE(env, value->IsTypedArray(), napi_invalid_arg);

  v8::Local<v8::TypedArray> array = value.As<v8::TypedArray>();

  if (type != nullptr) {
    if (value->IsInt8Array()) {
      *type = napi_int8_array;
    } else if (value->IsUint8Array()) {
      *type = napi_uint8_array;
    } else if (value->IsUint8ClampedArray()) {
      *type = napi_uint8_clamped_array;
    } else if (value->IsInt16Array()) {
      *type = napi_int16_array;
    } else if (value->IsUint16Array()) {
      *type = napi_uint16_array;
    } else if (value->IsInt32Array()) {
      *type = napi_int32_array;
    } else if (value->IsUint32Array()) {
      *type = napi_uint32_array;
    } else if (value->IsFloat32Array()) {
      *type = napi_float32_array;
    } else if (value->IsFloat64Array()) {
      *type = napi_float64_array;
    } else if (value->IsBigInt64Array()) {
      *type = napi_bigint64_array;
    } else if (value->IsBigUint64Array()) {
      *type = napi_biguint64_array;
    }
  }

  if (length != nullptr) {
    *length = array->Length();
  }

  v8::Local<v8::ArrayBuffer> buffer;
  if (data != nullptr || arraybuffer != nullptr) {
    // Calling Buffer() may have the side effect of allocating the buffer,
    // so only do this when it’s needed.
    buffer = array->Buffer();
  }

  if (data != nullptr) {
    *data = static_cast<uint8_t*>(buffer->Data()) + array->ByteOffset();
  }

  if (arraybuffer != nullptr) {
    *arraybuffer = v8impl::JsValueFromV8LocalValue(buffer);
  }

  if (byte_offset != nullptr) {
    *byte_offset = array->ByteOffset();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_dataview(napi_env env,
                                            size_t byte_length,
                                            napi_value arraybuffer,
                                            size_t byte_offset,
                                            napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, arraybuffer);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(arraybuffer);
  RETURN_STATUS_IF_FALSE(env, value->IsArrayBuffer(), napi_invalid_arg);

  v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
  if (byte_length + byte_offset > buffer->ByteLength()) {
    napi_throw_range_error(env,
                           "ERR_NAPI_INVALID_DATAVIEW_ARGS",
                           "byte_offset + byte_length should be less than or "
                           "equal to the size in bytes of the array passed in");
    return napi_set_last_error(env, napi_pending_exception);
  }
  v8::Local<v8::DataView> DataView =
      v8::DataView::New(buffer, byte_offset, byte_length);

  *result = v8impl::JsValueFromV8LocalValue(DataView);
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_is_dataview(napi_env env,
                                        napi_value value,
                                        bool* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  *result = val->IsDataView();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_dataview_info(napi_env env,
                                              napi_value dataview,
                                              size_t* byte_length,
                                              void** data,
                                              napi_value* arraybuffer,
                                              size_t* byte_offset) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, dataview);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(dataview);
  RETURN_STATUS_IF_FALSE(env, value->IsDataView(), napi_invalid_arg);

  v8::Local<v8::DataView> array = value.As<v8::DataView>();

  if (byte_length != nullptr) {
    *byte_length = array->ByteLength();
  }

  v8::Local<v8::ArrayBuffer> buffer;
  if (data != nullptr || arraybuffer != nullptr) {
    // Calling Buffer() may have the side effect of allocating the buffer,
    // so only do this when it’s needed.
    buffer = array->Buffer();
  }

  if (data != nullptr) {
    *data = static_cast<uint8_t*>(buffer->Data()) + array->ByteOffset();
  }

  if (arraybuffer != nullptr) {
    *arraybuffer = v8impl::JsValueFromV8LocalValue(buffer);
  }

  if (byte_offset != nullptr) {
    *byte_offset = array->ByteOffset();
  }

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_version(node_api_basic_env env,
                                        uint32_t* result) {
  CHECK_ENV(env);
  CHECK_ARG(env, result);
  *result = NODE_API_SUPPORTED_VERSION_MAX;
  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_promise(napi_env env,
                                           napi_deferred* deferred,
                                           napi_value* promise) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, deferred);
  CHECK_ARG(env, promise);

  auto maybe = v8::Promise::Resolver::New(env->context());
  CHECK_MAYBE_EMPTY(env, maybe, napi_generic_failure);

  auto v8_resolver = maybe.ToLocalChecked();
  auto v8_deferred = new v8impl::Persistent<v8::Value>();
  v8_deferred->Reset(env->isolate, v8_resolver);

  *deferred = v8impl::JsDeferredFromNodePersistent(v8_deferred);
  *promise = v8impl::JsValueFromV8LocalValue(v8_resolver->GetPromise());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_resolve_deferred(napi_env env,
                                             napi_deferred deferred,
                                             napi_value resolution) {
  return v8impl::ConcludeDeferred(env, deferred, resolution, true);
}

napi_status NAPI_CDECL napi_reject_deferred(napi_env env,
                                            napi_deferred deferred,
                                            napi_value resolution) {
  return v8impl::ConcludeDeferred(env, deferred, resolution, false);
}

napi_status NAPI_CDECL napi_is_promise(napi_env env,
                                       napi_value value,
                                       bool* is_promise) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, is_promise);

  *is_promise = v8impl::V8LocalValueFromJsValue(value)->IsPromise();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_create_date(napi_env env,
                                        double time,
                                        napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, result);

  v8::MaybeLocal<v8::Value> maybe_date = v8::Date::New(env->context(), time);
  CHECK_MAYBE_EMPTY(env, maybe_date, napi_generic_failure);

  *result = v8impl::JsValueFromV8LocalValue(maybe_date.ToLocalChecked());

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_is_date(napi_env env,
                                    napi_value value,
                                    bool* is_date) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, is_date);

  *is_date = v8impl::V8LocalValueFromJsValue(value)->IsDate();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_date_value(napi_env env,
                                           napi_value value,
                                           double* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, value);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> val = v8impl::V8LocalValueFromJsValue(value);
  RETURN_STATUS_IF_FALSE(env, val->IsDate(), napi_date_expected);

  v8::Local<v8::Date> date = val.As<v8::Date>();
  *result = date->ValueOf();

  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL napi_run_script(napi_env env,
                                       napi_value script,
                                       napi_value* result) {
  NAPI_PREAMBLE(env);
  CHECK_ARG(env, script);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> v8_script = v8impl::V8LocalValueFromJsValue(script);

  if (!v8_script->IsString()) {
    return napi_set_last_error(env, napi_string_expected);
  }

  v8::Local<v8::Context> context = env->context();

  auto maybe_script = v8::Script::Compile(context, v8_script.As<v8::String>());
  CHECK_MAYBE_EMPTY(env, maybe_script, napi_generic_failure);

  auto script_result = maybe_script.ToLocalChecked()->Run(context);
  CHECK_MAYBE_EMPTY(env, script_result, napi_generic_failure);

  *result = v8impl::JsValueFromV8LocalValue(script_result.ToLocalChecked());
  return GET_RETURN_STATUS(env);
}

napi_status NAPI_CDECL
napi_add_finalizer(napi_env env,
                   napi_value js_object,
                   void* finalize_data,
                   node_api_basic_finalize basic_finalize_cb,
                   void* finalize_hint,
                   napi_ref* result) {
  // Omit NAPI_PREAMBLE and GET_RETURN_STATUS because V8 calls here cannot throw
  // JS exceptions.
  napi_finalize finalize_cb =
      reinterpret_cast<napi_finalize>(basic_finalize_cb);
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, js_object);
  CHECK_ARG(env, finalize_cb);

  v8::Local<v8::Value> v8_value = v8impl::V8LocalValueFromJsValue(js_object);
  RETURN_STATUS_IF_FALSE(env, v8_value->IsObject(), napi_invalid_arg);

  // Create a self-deleting reference if the optional out-param result is not
  // set.
  v8impl::Ownership ownership = result == nullptr
                                    ? v8impl::Ownership::kRuntime
                                    : v8impl::Ownership::kUserland;
  v8impl::Reference* reference = v8impl::Reference::New(
      env, v8_value, 0, ownership, finalize_cb, finalize_data, finalize_hint);

  if (result != nullptr) {
    *result = reinterpret_cast<napi_ref>(reference);
  }
  return napi_clear_last_error(env);
}

#ifdef NAPI_EXPERIMENTAL

napi_status NAPI_CDECL node_api_post_finalizer(node_api_basic_env basic_env,
                                               napi_finalize finalize_cb,
                                               void* finalize_data,
                                               void* finalize_hint) {
  napi_env env = const_cast<napi_env>(basic_env);
  CHECK_ENV(env);
  env->EnqueueFinalizer(v8impl::TrackedFinalizer::New(
      env, finalize_cb, finalize_data, finalize_hint));
  return napi_clear_last_error(env);
}

#endif

napi_status NAPI_CDECL napi_adjust_external_memory(node_api_basic_env env,
                                                   int64_t change_in_bytes,
                                                   int64_t* adjusted_value) {
  CHECK_ENV(env);
  CHECK_ARG(env, adjusted_value);

  *adjusted_value =
      env->isolate->AdjustAmountOfExternalAllocatedMemory(change_in_bytes);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_set_instance_data(node_api_basic_env basic_env,
                                              void* data,
                                              napi_finalize finalize_cb,
                                              void* finalize_hint) {
  napi_env env = const_cast<napi_env>(basic_env);
  CHECK_ENV(env);

  v8impl::RefBase* old_data = static_cast<v8impl::RefBase*>(env->instance_data);
  if (old_data != nullptr) {
    // Our contract so far has been to not finalize any old data there may be.
    // So we simply delete it.
    delete old_data;
  }

  env->instance_data = v8impl::RefBase::New(
      env, 0, v8impl::Ownership::kRuntime, finalize_cb, data, finalize_hint);

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_get_instance_data(node_api_basic_env env,
                                              void** data) {
  CHECK_ENV(env);
  CHECK_ARG(env, data);

  v8impl::RefBase* idata = static_cast<v8impl::RefBase*>(env->instance_data);

  *data = (idata == nullptr ? nullptr : idata->Data());

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_detach_arraybuffer(napi_env env,
                                               napi_value arraybuffer) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, arraybuffer);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(arraybuffer);
  RETURN_STATUS_IF_FALSE(
      env, value->IsArrayBuffer(), napi_arraybuffer_expected);

  v8::Local<v8::ArrayBuffer> it = value.As<v8::ArrayBuffer>();
  RETURN_STATUS_IF_FALSE(
      env, it->IsDetachable(), napi_detachable_arraybuffer_expected);

  it->Detach(v8::Local<v8::Value>()).Check();

  return napi_clear_last_error(env);
}

napi_status NAPI_CDECL napi_is_detached_arraybuffer(napi_env env,
                                                    napi_value arraybuffer,
                                                    bool* result) {
  CHECK_ENV_NOT_IN_GC(env);
  CHECK_ARG(env, arraybuffer);
  CHECK_ARG(env, result);

  v8::Local<v8::Value> value = v8impl::V8LocalValueFromJsValue(arraybuffer);

  *result =
      value->IsArrayBuffer() && value.As<v8::ArrayBuffer>()->WasDetached();

  return napi_clear_last_error(env);
}
