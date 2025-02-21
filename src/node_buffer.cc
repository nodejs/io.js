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

#include "node_buffer.h"
#include "node.h"
#include "node_blob.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_i18n.h"
#include "node_internals.h"

#include "env-inl.h"
#include "simdutf.h"
#include "string_bytes.h"
#include "string_search.h"
#include "util-inl.h"
#include "v8.h"

#include <cstring>
#include <climits>

#define THROW_AND_RETURN_UNLESS_BUFFER(env, obj)                            \
  THROW_AND_RETURN_IF_NOT_BUFFER(env, obj, "argument")                      \

#define THROW_AND_RETURN_IF_OOB(r)                                          \
  do {                                                                      \
    Maybe<bool> m = (r);                                                    \
    if (m.IsNothing()) return;                                              \
    if (!m.FromJust())                                                      \
      return node::THROW_ERR_OUT_OF_RANGE(env, "Index out of range");       \
  } while (0)                                                               \

namespace node {
namespace Buffer {

using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::BackingStore;
using v8::Context;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::Global;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::SharedArrayBuffer;
using v8::String;
using v8::Uint32;
using v8::Uint32Array;
using v8::Uint8Array;
using v8::Value;

namespace {

class CallbackInfo {
 public:
  static inline Local<ArrayBuffer> CreateTrackedArrayBuffer(
      Environment* env,
      char* data,
      size_t length,
      FreeCallback callback,
      void* hint);

  CallbackInfo(const CallbackInfo&) = delete;
  CallbackInfo& operator=(const CallbackInfo&) = delete;

 private:
  static void CleanupHook(void* data);
  inline void OnBackingStoreFree();
  inline void CallAndResetCallback();
  inline CallbackInfo(Environment* env,
                      FreeCallback callback,
                      char* data,
                      void* hint);
  Global<ArrayBuffer> persistent_;
  Mutex mutex_;  // Protects callback_.
  FreeCallback callback_;
  char* const data_;
  void* const hint_;
  Environment* const env_;
};


Local<ArrayBuffer> CallbackInfo::CreateTrackedArrayBuffer(
    Environment* env,
    char* data,
    size_t length,
    FreeCallback callback,
    void* hint) {
  CHECK_NOT_NULL(callback);
  CHECK_IMPLIES(data == nullptr, length == 0);

  CallbackInfo* self = new CallbackInfo(env, callback, data, hint);
  std::unique_ptr<BackingStore> bs =
      ArrayBuffer::NewBackingStore(data, length, [](void*, size_t, void* arg) {
        static_cast<CallbackInfo*>(arg)->OnBackingStoreFree();
      }, self);
  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));

  // V8 simply ignores the BackingStore deleter callback if data == nullptr,
  // but our API contract requires it being called.
  if (data == nullptr) {
    ab->Detach();
    self->OnBackingStoreFree();  // This calls `callback` asynchronously.
  } else {
    // Store the ArrayBuffer so that we can detach it later.
    self->persistent_.Reset(env->isolate(), ab);
    self->persistent_.SetWeak();
  }

  return ab;
}


CallbackInfo::CallbackInfo(Environment* env,
                           FreeCallback callback,
                           char* data,
                           void* hint)
    : callback_(callback),
      data_(data),
      hint_(hint),
      env_(env) {
  env->AddCleanupHook(CleanupHook, this);
  env->isolate()->AdjustAmountOfExternalAllocatedMemory(sizeof(*this));
}

void CallbackInfo::CleanupHook(void* data) {
  CallbackInfo* self = static_cast<CallbackInfo*>(data);

  {
    HandleScope handle_scope(self->env_->isolate());
    Local<ArrayBuffer> ab = self->persistent_.Get(self->env_->isolate());
    if (!ab.IsEmpty() && ab->IsDetachable()) {
      ab->Detach();
      self->persistent_.Reset();
    }
  }

  // Call the callback in this case, but don't delete `this` yet because the
  // BackingStore deleter callback will do so later.
  self->CallAndResetCallback();
}

void CallbackInfo::CallAndResetCallback() {
  FreeCallback callback;
  {
    Mutex::ScopedLock lock(mutex_);
    callback = callback_;
    callback_ = nullptr;
  }
  if (callback != nullptr) {
    // Clean up all Environment-related state and run the callback.
    env_->RemoveCleanupHook(CleanupHook, this);
    int64_t change_in_bytes = -static_cast<int64_t>(sizeof(*this));
    env_->isolate()->AdjustAmountOfExternalAllocatedMemory(change_in_bytes);

    callback(data_, hint_);
  }
}

void CallbackInfo::OnBackingStoreFree() {
  // This method should always release the memory for `this`.
  std::unique_ptr<CallbackInfo> self { this };
  Mutex::ScopedLock lock(mutex_);
  // If callback_ == nullptr, that means that the callback has already run from
  // the cleanup hook, and there is nothing left to do here besides to clean
  // up the memory involved. In particular, the underlying `Environment` may
  // be gone at this point, so don’t attempt to call SetImmediateThreadsafe().
  if (callback_ == nullptr) return;

  env_->SetImmediateThreadsafe([self = std::move(self)](Environment* env) {
    CHECK_EQ(self->env_, env);  // Consistency check.

    self->CallAndResetCallback();
  });
}


// Parse index for external array data. An empty Maybe indicates
// a pending exception. `false` indicates that the index is out-of-bounds.
inline MUST_USE_RESULT Maybe<bool> ParseArrayIndex(Environment* env,
                                                   Local<Value> arg,
                                                   size_t def,
                                                   size_t* ret) {
  if (arg->IsUndefined()) {
    *ret = def;
    return Just(true);
  }

  int64_t tmp_i;
  if (!arg->IntegerValue(env->context()).To(&tmp_i))
    return Nothing<bool>();

  if (tmp_i < 0)
    return Just(false);

  // Check that the result fits in a size_t.
  // coverity[pointless_expression]
  if (static_cast<uint64_t>(tmp_i) > std::numeric_limits<size_t>::max())
    return Just(false);

  *ret = static_cast<size_t>(tmp_i);
  return Just(true);
}

}  // anonymous namespace

// Buffer methods

bool HasInstance(Local<Value> val) {
  return val->IsArrayBufferView();
}


bool HasInstance(Local<Object> obj) {
  return obj->IsArrayBufferView();
}


char* Data(Local<Value> val) {
  CHECK(val->IsArrayBufferView());
  Local<ArrayBufferView> ui = val.As<ArrayBufferView>();
  // GetBackingStore() is slow, and this would be faster if we just did
  // ui->Buffer()->Data().  However these two are not equivalent in the case of
  // zero-length buffers: the former returns a "valid" address, while the
  // latter returns `NULL`. At least one library in the ecosystem (see the
  // referenced issue) abuses zero-length buffers to wrap arbitrary pointers,
  // which is broken by this difference. It is unfortunate that every library
  // needs to take a performance hit because of this edge-case, so this change
  // is only being backported to older Node.js releases.
  //
  // See: https://github.com/nodejs/node/issues/44554
  return static_cast<char*>(ui->Buffer()->GetBackingStore()->Data()) +
         ui->ByteOffset();
}


char* Data(Local<Object> obj) {
  return Data(obj.As<Value>());
}


size_t Length(Local<Value> val) {
  CHECK(val->IsArrayBufferView());
  Local<ArrayBufferView> ui = val.As<ArrayBufferView>();
  return ui->ByteLength();
}


size_t Length(Local<Object> obj) {
  CHECK(obj->IsArrayBufferView());
  Local<ArrayBufferView> ui = obj.As<ArrayBufferView>();
  return ui->ByteLength();
}


MaybeLocal<Uint8Array> New(Environment* env,
                           Local<ArrayBuffer> ab,
                           size_t byte_offset,
                           size_t length) {
  CHECK(!env->buffer_prototype_object().IsEmpty());
  Local<Uint8Array> ui = Uint8Array::New(ab, byte_offset, length);
  Maybe<bool> mb =
      ui->SetPrototype(env->context(), env->buffer_prototype_object());
  if (mb.IsNothing())
    return MaybeLocal<Uint8Array>();
  return ui;
}

MaybeLocal<Uint8Array> New(Isolate* isolate,
                           Local<ArrayBuffer> ab,
                           size_t byte_offset,
                           size_t length) {
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) {
    THROW_ERR_BUFFER_CONTEXT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Uint8Array>();
  }
  return New(env, ab, byte_offset, length);
}


MaybeLocal<Object> New(Isolate* isolate,
                       Local<String> string,
                       enum encoding enc) {
  EscapableHandleScope scope(isolate);

  size_t length;
  if (!StringBytes::Size(isolate, string, enc).To(&length))
    return Local<Object>();
  size_t actual = 0;
  std::unique_ptr<BackingStore> store;

  if (length > 0) {
    store = ArrayBuffer::NewBackingStore(isolate, length);

    if (UNLIKELY(!store)) {
      THROW_ERR_MEMORY_ALLOCATION_FAILED(isolate);
      return Local<Object>();
    }

    actual = StringBytes::Write(
        isolate,
        static_cast<char*>(store->Data()),
        length,
        string,
        enc);
    CHECK(actual <= length);

    if (LIKELY(actual > 0)) {
      if (actual < length)
        store = BackingStore::Reallocate(isolate, std::move(store), actual);
      Local<ArrayBuffer> buf = ArrayBuffer::New(isolate, std::move(store));
      Local<Object> obj;
      if (UNLIKELY(!New(isolate, buf, 0, actual).ToLocal(&obj)))
        return MaybeLocal<Object>();
      return scope.Escape(obj);
    }
  }

  return scope.EscapeMaybe(New(isolate, 0));
}


MaybeLocal<Object> New(Isolate* isolate, size_t length) {
  EscapableHandleScope handle_scope(isolate);
  Local<Object> obj;
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) {
    THROW_ERR_BUFFER_CONTEXT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Object>();
  }
  if (Buffer::New(env, length).ToLocal(&obj))
    return handle_scope.Escape(obj);
  return Local<Object>();
}


MaybeLocal<Object> New(Environment* env, size_t length) {
  Isolate* isolate(env->isolate());
  EscapableHandleScope scope(isolate);

  // V8 currently only allows a maximum Typed Array index of max Smi.
  if (length > kMaxLength) {
    isolate->ThrowException(ERR_BUFFER_TOO_LARGE(isolate));
    return Local<Object>();
  }

  Local<ArrayBuffer> ab;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    std::unique_ptr<BackingStore> bs =
        ArrayBuffer::NewBackingStore(isolate, length);

    CHECK(bs);

    ab = ArrayBuffer::New(isolate, std::move(bs));
  }

  MaybeLocal<Object> obj =
      New(env, ab, 0, ab->ByteLength())
          .FromMaybe(Local<Uint8Array>());

  return scope.EscapeMaybe(obj);
}


MaybeLocal<Object> Copy(Isolate* isolate, const char* data, size_t length) {
  EscapableHandleScope handle_scope(isolate);
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) {
    THROW_ERR_BUFFER_CONTEXT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Object>();
  }
  Local<Object> obj;
  if (Buffer::Copy(env, data, length).ToLocal(&obj))
    return handle_scope.Escape(obj);
  return Local<Object>();
}


MaybeLocal<Object> Copy(Environment* env, const char* data, size_t length) {
  Isolate* isolate(env->isolate());
  EscapableHandleScope scope(isolate);

  // V8 currently only allows a maximum Typed Array index of max Smi.
  if (length > kMaxLength) {
    isolate->ThrowException(ERR_BUFFER_TOO_LARGE(isolate));
    return Local<Object>();
  }

  Local<ArrayBuffer> ab;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    std::unique_ptr<BackingStore> bs =
        ArrayBuffer::NewBackingStore(isolate, length);

    CHECK(bs);

    memcpy(bs->Data(), data, length);

    ab = ArrayBuffer::New(isolate, std::move(bs));
  }

  MaybeLocal<Object> obj =
      New(env, ab, 0, ab->ByteLength())
          .FromMaybe(Local<Uint8Array>());

  return scope.EscapeMaybe(obj);
}


MaybeLocal<Object> New(Isolate* isolate,
                       char* data,
                       size_t length,
                       FreeCallback callback,
                       void* hint) {
  EscapableHandleScope handle_scope(isolate);
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) {
    callback(data, hint);
    THROW_ERR_BUFFER_CONTEXT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Object>();
  }
  return handle_scope.EscapeMaybe(
      Buffer::New(env, data, length, callback, hint));
}


MaybeLocal<Object> New(Environment* env,
                       char* data,
                       size_t length,
                       FreeCallback callback,
                       void* hint) {
  EscapableHandleScope scope(env->isolate());

  if (length > kMaxLength) {
    env->isolate()->ThrowException(ERR_BUFFER_TOO_LARGE(env->isolate()));
    callback(data, hint);
    return Local<Object>();
  }

  Local<ArrayBuffer> ab =
      CallbackInfo::CreateTrackedArrayBuffer(env, data, length, callback, hint);
  if (ab->SetPrivate(env->context(),
                     env->untransferable_object_private_symbol(),
                     True(env->isolate())).IsNothing()) {
    return Local<Object>();
  }
  MaybeLocal<Uint8Array> maybe_ui = Buffer::New(env, ab, 0, length);

  Local<Uint8Array> ui;
  if (!maybe_ui.ToLocal(&ui))
    return MaybeLocal<Object>();

  return scope.Escape(ui);
}

// Warning: This function needs `data` to be allocated with malloc() and not
// necessarily isolate's ArrayBuffer::Allocator.
MaybeLocal<Object> New(Isolate* isolate, char* data, size_t length) {
  EscapableHandleScope handle_scope(isolate);
  Environment* env = Environment::GetCurrent(isolate);
  if (env == nullptr) {
    free(data);
    THROW_ERR_BUFFER_CONTEXT_NOT_AVAILABLE(isolate);
    return MaybeLocal<Object>();
  }
  Local<Object> obj;
  if (Buffer::New(env, data, length).ToLocal(&obj))
    return handle_scope.Escape(obj);
  return Local<Object>();
}

// The contract for this function is that `data` is allocated with malloc()
// and not necessarily isolate's ArrayBuffer::Allocator.
MaybeLocal<Object> New(Environment* env,
                       char* data,
                       size_t length) {
  if (length > 0) {
    CHECK_NOT_NULL(data);
    // V8 currently only allows a maximum Typed Array index of max Smi.
    if (length > kMaxLength) {
      Isolate* isolate(env->isolate());
      isolate->ThrowException(ERR_BUFFER_TOO_LARGE(isolate));
      free(data);
      return Local<Object>();
    }
  }

  EscapableHandleScope handle_scope(env->isolate());

  auto free_callback = [](void* data, size_t length, void* deleter_data) {
    free(data);
  };
  std::unique_ptr<BackingStore> bs =
      v8::ArrayBuffer::NewBackingStore(data, length, free_callback, nullptr);

  Local<ArrayBuffer> ab = v8::ArrayBuffer::New(env->isolate(), std::move(bs));

  Local<Object> obj;
  if (Buffer::New(env, ab, 0, length).ToLocal(&obj))
    return handle_scope.Escape(obj);
  return Local<Object>();
}

namespace {

void CreateFromString(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());
  CHECK(args[1]->IsInt32());

  enum encoding enc = static_cast<enum encoding>(args[1].As<Int32>()->Value());
  Local<Object> buf;
  if (New(args.GetIsolate(), args[0].As<String>(), enc).ToLocal(&buf))
    args.GetReturnValue().Set(buf);
}


template <encoding encoding>
void StringSlice(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  THROW_AND_RETURN_UNLESS_BUFFER(env, args.This());
  ArrayBufferViewContents<char> buffer(args.This());

  if (buffer.length() == 0)
    return args.GetReturnValue().SetEmptyString();

  size_t start = 0;
  size_t end = 0;
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[0], 0, &start));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[1], buffer.length(), &end));
  if (end < start) end = start;
  THROW_AND_RETURN_IF_OOB(Just(end <= buffer.length()));
  size_t length = end - start;

  Local<Value> error;
  MaybeLocal<Value> maybe_ret =
      StringBytes::Encode(isolate,
                          buffer.data() + start,
                          length,
                          encoding,
                          &error);
  Local<Value> ret;
  if (!maybe_ret.ToLocal(&ret)) {
    CHECK(!error.IsEmpty());
    isolate->ThrowException(error);
    return;
  }
  args.GetReturnValue().Set(ret);
}

// Convert the input into an encoded string
void DecodeUTF8(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);  // list, flags

  CHECK_GE(args.Length(), 1);

  if (!(args[0]->IsArrayBuffer() || args[0]->IsSharedArrayBuffer() ||
        args[0]->IsArrayBufferView())) {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(),
        "The \"list\" argument must be an instance of SharedArrayBuffer, "
        "ArrayBuffer or ArrayBufferView.");
  }

  ArrayBufferViewContents<char> buffer(args[0]);

  bool ignore_bom = args[1]->IsTrue();
  bool has_fatal = args[2]->IsTrue();

  const char* data = buffer.data();
  size_t length = buffer.length();

  if (has_fatal) {
    auto result = simdutf::validate_utf8_with_errors(data, length);

    if (result.error) {
      return node::THROW_ERR_ENCODING_INVALID_ENCODED_DATA(
          env->isolate(), "The encoded data was not valid for encoding utf-8");
    }
  }

  if (!ignore_bom && length >= 3) {
    if (memcmp(data, "\xEF\xBB\xBF", 3) == 0) {
      data += 3;
      length -= 3;
    }
  }

  if (length == 0) return args.GetReturnValue().SetEmptyString();

  Local<Value> error;
  MaybeLocal<Value> maybe_ret =
      StringBytes::Encode(env->isolate(), data, length, UTF8, &error);
  Local<Value> ret;

  if (!maybe_ret.ToLocal(&ret)) {
    CHECK(!error.IsEmpty());
    env->isolate()->ThrowException(error);
    return;
  }

  args.GetReturnValue().Set(ret);
}

// bytesCopied = copy(buffer, target[, targetStart][, sourceStart][, sourceEnd])
void Copy(const FunctionCallbackInfo<Value> &args) {
  Environment* env = Environment::GetCurrent(args);

  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[1]);
  ArrayBufferViewContents<char> source(args[0]);
  Local<Object> target_obj = args[1].As<Object>();
  SPREAD_BUFFER_ARG(target_obj, target);

  size_t target_start = 0;
  size_t source_start = 0;
  size_t source_end = 0;

  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[2], 0, &target_start));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[3], 0, &source_start));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[4], source.length(),
                                          &source_end));

  // Copy 0 bytes; we're done
  if (target_start >= target_length || source_start >= source_end)
    return args.GetReturnValue().Set(0);

  if (source_start > source.length())
    return THROW_ERR_OUT_OF_RANGE(
        env, "The value of \"sourceStart\" is out of range.");

  if (source_end - source_start > target_length - target_start)
    source_end = source_start + target_length - target_start;

  uint32_t to_copy = std::min(
      std::min(source_end - source_start, target_length - target_start),
      source.length() - source_start);

  memmove(target_data + target_start, source.data() + source_start, to_copy);
  args.GetReturnValue().Set(to_copy);
}


void Fill(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> ctx = env->context();

  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  SPREAD_BUFFER_ARG(args[0], ts_obj);

  size_t start = 0;
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[2], 0, &start));
  size_t end;
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[3], 0, &end));

  size_t fill_length = end - start;
  Local<String> str_obj;
  size_t str_length;
  enum encoding enc;

  // OOB Check. Throw the error in JS.
  if (start > end || fill_length + start > ts_obj_length)
    return args.GetReturnValue().Set(-2);

  // First check if Buffer has been passed.
  if (Buffer::HasInstance(args[1])) {
    SPREAD_BUFFER_ARG(args[1], fill_obj);
    str_length = fill_obj_length;
    memcpy(
        ts_obj_data + start, fill_obj_data, std::min(str_length, fill_length));
    goto start_fill;
  }

  // Then coerce everything that's not a string.
  if (!args[1]->IsString()) {
    uint32_t val;
    if (!args[1]->Uint32Value(ctx).To(&val)) return;
    int value = val & 255;
    memset(ts_obj_data + start, value, fill_length);
    return;
  }

  str_obj = args[1]->ToString(env->context()).ToLocalChecked();
  enc = ParseEncoding(env->isolate(), args[4], UTF8);

  // Can't use StringBytes::Write() in all cases. For example if attempting
  // to write a two byte character into a one byte Buffer.
  if (enc == UTF8) {
    str_length = str_obj->Utf8Length(env->isolate());
    node::Utf8Value str(env->isolate(), args[1]);
    memcpy(ts_obj_data + start, *str, std::min(str_length, fill_length));

  } else if (enc == UCS2) {
    str_length = str_obj->Length() * sizeof(uint16_t);
    node::TwoByteValue str(env->isolate(), args[1]);
    if (IsBigEndian())
      SwapBytes16(reinterpret_cast<char*>(&str[0]), str_length);

    memcpy(ts_obj_data + start, *str, std::min(str_length, fill_length));

  } else {
    // Write initial String to Buffer, then use that memory to copy remainder
    // of string. Correct the string length for cases like HEX where less than
    // the total string length is written.
    str_length = StringBytes::Write(
        env->isolate(), ts_obj_data + start, fill_length, str_obj, enc);
  }

start_fill:

  if (str_length >= fill_length)
    return;

  // If str_length is zero, then either an empty buffer was provided, or Write()
  // indicated that no bytes could be written. If no bytes could be written,
  // then return -1 because the fill value is invalid. This will trigger a throw
  // in JavaScript. Silently failing should be avoided because it can lead to
  // buffers with unexpected contents.
  if (str_length == 0)
    return args.GetReturnValue().Set(-1);

  size_t in_there = str_length;
  char* ptr = ts_obj_data + start + str_length;

  while (in_there < fill_length - in_there) {
    memcpy(ptr, ts_obj_data + start, in_there);
    ptr += in_there;
    in_there *= 2;
  }

  if (in_there < fill_length) {
    memcpy(ptr, ts_obj_data + start, fill_length - in_there);
  }
}


template <encoding encoding>
void StringWrite(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  THROW_AND_RETURN_UNLESS_BUFFER(env, args.This());
  SPREAD_BUFFER_ARG(args.This(), ts_obj);

  THROW_AND_RETURN_IF_NOT_STRING(env, args[0], "argument");

  Local<String> str = args[0]->ToString(env->context()).ToLocalChecked();

  size_t offset = 0;
  size_t max_length = 0;

  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[1], 0, &offset));
  if (offset > ts_obj_length) {
    return node::THROW_ERR_BUFFER_OUT_OF_BOUNDS(
        env, "\"offset\" is outside of buffer bounds");
  }

  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[2], ts_obj_length - offset,
                                          &max_length));

  max_length = std::min(ts_obj_length - offset, max_length);

  if (max_length == 0)
    return args.GetReturnValue().Set(0);

  uint32_t written = StringBytes::Write(
      env->isolate(), ts_obj_data + offset, max_length, str, encoding);
  args.GetReturnValue().Set(written);
}

void ByteLengthUtf8(const FunctionCallbackInfo<Value> &args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsString());

  // Fast case: avoid StringBytes on UTF8 string. Jump to v8.
  args.GetReturnValue().Set(args[0].As<String>()->Utf8Length(env->isolate()));
}

// Normalize val to be an integer in the range of [1, -1] since
// implementations of memcmp() can vary by platform.
static int normalizeCompareVal(int val, size_t a_length, size_t b_length) {
  if (val == 0) {
    if (a_length > b_length)
      return 1;
    else if (a_length < b_length)
      return -1;
  } else {
    if (val > 0)
      return 1;
    else
      return -1;
  }
  return val;
}

void CompareOffset(const FunctionCallbackInfo<Value> &args) {
  Environment* env = Environment::GetCurrent(args);

  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[1]);
  ArrayBufferViewContents<char> source(args[0]);
  ArrayBufferViewContents<char> target(args[1]);

  size_t target_start = 0;
  size_t source_start = 0;
  size_t source_end = 0;
  size_t target_end = 0;

  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[2], 0, &target_start));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[3], 0, &source_start));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[4], target.length(),
                                          &target_end));
  THROW_AND_RETURN_IF_OOB(ParseArrayIndex(env, args[5], source.length(),
                                          &source_end));

  if (source_start > source.length())
    return THROW_ERR_OUT_OF_RANGE(
        env, "The value of \"sourceStart\" is out of range.");
  if (target_start > target.length())
    return THROW_ERR_OUT_OF_RANGE(
        env, "The value of \"targetStart\" is out of range.");

  CHECK_LE(source_start, source_end);
  CHECK_LE(target_start, target_end);

  size_t to_cmp =
      std::min(std::min(source_end - source_start, target_end - target_start),
               source.length() - source_start);

  int val = normalizeCompareVal(to_cmp > 0 ?
                                  memcmp(source.data() + source_start,
                                         target.data() + target_start,
                                         to_cmp) : 0,
                                source_end - source_start,
                                target_end - target_start);

  args.GetReturnValue().Set(val);
}

void Compare(const FunctionCallbackInfo<Value> &args) {
  Environment* env = Environment::GetCurrent(args);

  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[1]);
  ArrayBufferViewContents<char> a(args[0]);
  ArrayBufferViewContents<char> b(args[1]);

  size_t cmp_length = std::min(a.length(), b.length());

  int val = normalizeCompareVal(cmp_length > 0 ?
                                memcmp(a.data(), b.data(), cmp_length) : 0,
                                a.length(), b.length());
  args.GetReturnValue().Set(val);
}


// Computes the offset for starting an indexOf or lastIndexOf search.
// Returns either a valid offset in [0...<length - 1>], ie inside the Buffer,
// or -1 to signal that there is no possible match.
int64_t IndexOfOffset(size_t length,
                      int64_t offset_i64,
                      int64_t needle_length,
                      bool is_forward) {
  int64_t length_i64 = static_cast<int64_t>(length);
  if (offset_i64 < 0) {
    if (offset_i64 + length_i64 >= 0) {
      // Negative offsets count backwards from the end of the buffer.
      return length_i64 + offset_i64;
    } else if (is_forward || needle_length == 0) {
      // indexOf from before the start of the buffer: search the whole buffer.
      return 0;
    } else {
      // lastIndexOf from before the start of the buffer: no match.
      return -1;
    }
  } else {
    if (offset_i64 + needle_length <= length_i64) {
      // Valid positive offset.
      return offset_i64;
    } else if (needle_length == 0) {
      // Out of buffer bounds, but empty needle: point to end of buffer.
      return length_i64;
    } else if (is_forward) {
      // indexOf from past the end of the buffer: no match.
      return -1;
    } else {
      // lastIndexOf from past the end of the buffer: search the whole buffer.
      return length_i64 - 1;
    }
  }
}

void IndexOfString(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK(args[1]->IsString());
  CHECK(args[2]->IsNumber());
  CHECK(args[3]->IsInt32());
  CHECK(args[4]->IsBoolean());

  enum encoding enc = static_cast<enum encoding>(args[3].As<Int32>()->Value());

  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  ArrayBufferViewContents<char> buffer(args[0]);

  Local<String> needle = args[1].As<String>();
  int64_t offset_i64 = args[2].As<Integer>()->Value();
  bool is_forward = args[4]->IsTrue();

  const char* haystack = buffer.data();
  // Round down to the nearest multiple of 2 in case of UCS2.
  const size_t haystack_length = (enc == UCS2) ?
      buffer.length() &~ 1 : buffer.length();  // NOLINT(whitespace/operators)

  size_t needle_length;
  if (!StringBytes::Size(isolate, needle, enc).To(&needle_length)) return;

  int64_t opt_offset = IndexOfOffset(haystack_length,
                                     offset_i64,
                                     needle_length,
                                     is_forward);

  if (needle_length == 0) {
    // Match String#indexOf() and String#lastIndexOf() behavior.
    args.GetReturnValue().Set(static_cast<double>(opt_offset));
    return;
  }

  if (haystack_length == 0) {
    return args.GetReturnValue().Set(-1);
  }

  if (opt_offset <= -1) {
    return args.GetReturnValue().Set(-1);
  }
  size_t offset = static_cast<size_t>(opt_offset);
  CHECK_LT(offset, haystack_length);
  if ((is_forward && needle_length + offset > haystack_length) ||
      needle_length > haystack_length) {
    return args.GetReturnValue().Set(-1);
  }

  size_t result = haystack_length;

  if (enc == UCS2) {
    String::Value needle_value(isolate, needle);
    if (*needle_value == nullptr)
      return args.GetReturnValue().Set(-1);

    if (haystack_length < 2 || needle_value.length() < 1) {
      return args.GetReturnValue().Set(-1);
    }

    if (IsBigEndian()) {
      StringBytes::InlineDecoder decoder;
      if (decoder.Decode(env, needle, enc).IsNothing()) return;
      const uint16_t* decoded_string =
          reinterpret_cast<const uint16_t*>(decoder.out());

      if (decoded_string == nullptr)
        return args.GetReturnValue().Set(-1);

      result = SearchString(reinterpret_cast<const uint16_t*>(haystack),
                            haystack_length / 2,
                            decoded_string,
                            decoder.size() / 2,
                            offset / 2,
                            is_forward);
    } else {
      result = SearchString(reinterpret_cast<const uint16_t*>(haystack),
                            haystack_length / 2,
                            reinterpret_cast<const uint16_t*>(*needle_value),
                            needle_value.length(),
                            offset / 2,
                            is_forward);
    }
    result *= 2;
  } else if (enc == UTF8) {
    String::Utf8Value needle_value(isolate, needle);
    if (*needle_value == nullptr)
      return args.GetReturnValue().Set(-1);

    result = SearchString(reinterpret_cast<const uint8_t*>(haystack),
                          haystack_length,
                          reinterpret_cast<const uint8_t*>(*needle_value),
                          needle_length,
                          offset,
                          is_forward);
  } else if (enc == LATIN1) {
    uint8_t* needle_data = node::UncheckedMalloc<uint8_t>(needle_length);
    if (needle_data == nullptr) {
      return args.GetReturnValue().Set(-1);
    }
    needle->WriteOneByte(
        isolate, needle_data, 0, needle_length, String::NO_NULL_TERMINATION);

    result = SearchString(reinterpret_cast<const uint8_t*>(haystack),
                          haystack_length,
                          needle_data,
                          needle_length,
                          offset,
                          is_forward);
    free(needle_data);
  }

  args.GetReturnValue().Set(
      result == haystack_length ? -1 : static_cast<int>(result));
}

void IndexOfBuffer(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[1]->IsObject());
  CHECK(args[2]->IsNumber());
  CHECK(args[3]->IsInt32());
  CHECK(args[4]->IsBoolean());

  enum encoding enc = static_cast<enum encoding>(args[3].As<Int32>()->Value());

  THROW_AND_RETURN_UNLESS_BUFFER(Environment::GetCurrent(args), args[0]);
  THROW_AND_RETURN_UNLESS_BUFFER(Environment::GetCurrent(args), args[1]);
  ArrayBufferViewContents<char> haystack_contents(args[0]);
  ArrayBufferViewContents<char> needle_contents(args[1]);
  int64_t offset_i64 = args[2].As<Integer>()->Value();
  bool is_forward = args[4]->IsTrue();

  const char* haystack = haystack_contents.data();
  const size_t haystack_length = haystack_contents.length();
  const char* needle = needle_contents.data();
  const size_t needle_length = needle_contents.length();

  int64_t opt_offset = IndexOfOffset(haystack_length,
                                     offset_i64,
                                     needle_length,
                                     is_forward);

  if (needle_length == 0) {
    // Match String#indexOf() and String#lastIndexOf() behavior.
    args.GetReturnValue().Set(static_cast<double>(opt_offset));
    return;
  }

  if (haystack_length == 0) {
    return args.GetReturnValue().Set(-1);
  }

  if (opt_offset <= -1) {
    return args.GetReturnValue().Set(-1);
  }
  size_t offset = static_cast<size_t>(opt_offset);
  CHECK_LT(offset, haystack_length);
  if ((is_forward && needle_length + offset > haystack_length) ||
      needle_length > haystack_length) {
    return args.GetReturnValue().Set(-1);
  }

  size_t result = haystack_length;

  if (enc == UCS2) {
    if (haystack_length < 2 || needle_length < 2) {
      return args.GetReturnValue().Set(-1);
    }
    result = SearchString(
        reinterpret_cast<const uint16_t*>(haystack),
        haystack_length / 2,
        reinterpret_cast<const uint16_t*>(needle),
        needle_length / 2,
        offset / 2,
        is_forward);
    result *= 2;
  } else {
    result = SearchString(
        reinterpret_cast<const uint8_t*>(haystack),
        haystack_length,
        reinterpret_cast<const uint8_t*>(needle),
        needle_length,
        offset,
        is_forward);
  }

  args.GetReturnValue().Set(
      result == haystack_length ? -1 : static_cast<int>(result));
}

void IndexOfNumber(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[1]->IsUint32());
  CHECK(args[2]->IsNumber());
  CHECK(args[3]->IsBoolean());

  THROW_AND_RETURN_UNLESS_BUFFER(Environment::GetCurrent(args), args[0]);
  ArrayBufferViewContents<char> buffer(args[0]);

  uint32_t needle = args[1].As<Uint32>()->Value();
  int64_t offset_i64 = args[2].As<Integer>()->Value();
  bool is_forward = args[3]->IsTrue();

  int64_t opt_offset =
      IndexOfOffset(buffer.length(), offset_i64, 1, is_forward);
  if (opt_offset <= -1 || buffer.length() == 0) {
    return args.GetReturnValue().Set(-1);
  }
  size_t offset = static_cast<size_t>(opt_offset);
  CHECK_LT(offset, buffer.length());

  const void* ptr;
  if (is_forward) {
    ptr = memchr(buffer.data() + offset, needle, buffer.length() - offset);
  } else {
    ptr = node::stringsearch::MemrchrFill(buffer.data(), needle, offset + 1);
  }
  const char* ptr_char = static_cast<const char*>(ptr);
  args.GetReturnValue().Set(ptr ? static_cast<int>(ptr_char - buffer.data())
                                : -1);
}


void Swap16(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  SPREAD_BUFFER_ARG(args[0], ts_obj);
  SwapBytes16(ts_obj_data, ts_obj_length);
  args.GetReturnValue().Set(args[0]);
}


void Swap32(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  SPREAD_BUFFER_ARG(args[0], ts_obj);
  SwapBytes32(ts_obj_data, ts_obj_length);
  args.GetReturnValue().Set(args[0]);
}


void Swap64(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_UNLESS_BUFFER(env, args[0]);
  SPREAD_BUFFER_ARG(args[0], ts_obj);
  SwapBytes64(ts_obj_data, ts_obj_length);
  args.GetReturnValue().Set(args[0]);
}


// Encode a single string to a UTF-8 Uint8Array (not Buffer).
// Used in TextEncoder.prototype.encode.
static void EncodeUtf8String(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());

  Local<String> str = args[0].As<String>();
  size_t length = str->Utf8Length(isolate);

  Local<ArrayBuffer> ab;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    std::unique_ptr<BackingStore> bs =
        ArrayBuffer::NewBackingStore(isolate, length);

    CHECK(bs);

    str->WriteUtf8(isolate,
                   static_cast<char*>(bs->Data()),
                   -1,  // We are certain that `data` is sufficiently large
                   nullptr,
                   String::NO_NULL_TERMINATION | String::REPLACE_INVALID_UTF8);

    ab = ArrayBuffer::New(isolate, std::move(bs));
  }

  auto array = Uint8Array::New(ab, 0, length);
  args.GetReturnValue().Set(array);
}


static void EncodeInto(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  CHECK_GE(args.Length(), 3);
  CHECK(args[0]->IsString());
  CHECK(args[1]->IsUint8Array());
  CHECK(args[2]->IsUint32Array());

  Local<String> source = args[0].As<String>();

  Local<Uint8Array> dest = args[1].As<Uint8Array>();
  Local<ArrayBuffer> buf = dest->Buffer();
  char* write_result = static_cast<char*>(buf->Data()) + dest->ByteOffset();
  size_t dest_length = dest->ByteLength();

  // results = [ read, written ]
  Local<Uint32Array> result_arr = args[2].As<Uint32Array>();
  uint32_t* results = reinterpret_cast<uint32_t*>(
      static_cast<char*>(result_arr->Buffer()->Data()) +
      result_arr->ByteOffset());

  int nchars;
  int written = source->WriteUtf8(
      isolate,
      write_result,
      dest_length,
      &nchars,
      String::NO_NULL_TERMINATION | String::REPLACE_INVALID_UTF8);
  results[0] = nchars;
  results[1] = written;
}

static void IsUtf8(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsTypedArray() || args[0]->IsArrayBuffer() ||
        args[0]->IsSharedArrayBuffer());
  ArrayBufferViewContents<char> abv(args[0]);

  if (abv.WasDetached()) {
    return node::THROW_ERR_INVALID_STATE(
        env, "Cannot validate on a detached buffer");
  }

  args.GetReturnValue().Set(simdutf::validate_utf8(abv.data(), abv.length()));
}

static void IsAscii(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsTypedArray() || args[0]->IsArrayBuffer() ||
        args[0]->IsSharedArrayBuffer());
  ArrayBufferViewContents<char> abv(args[0]);

  if (abv.WasDetached()) {
    return node::THROW_ERR_INVALID_STATE(
        env, "Cannot validate on a detached buffer");
  }

  args.GetReturnValue().Set(simdutf::validate_ascii(abv.data(), abv.length()));
}

void SetBufferPrototype(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());
  Local<Object> proto = args[0].As<Object>();
  env->set_buffer_prototype_object(proto);
}

void GetZeroFillToggle(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  NodeArrayBufferAllocator* allocator = env->isolate_data()->node_allocator();
  Local<ArrayBuffer> ab;
  // It can be a nullptr when running inside an isolate where we
  // do not own the ArrayBuffer allocator.
  if (allocator == nullptr) {
    // Create a dummy Uint32Array - the JS land can only toggle the C++ land
    // setting when the allocator uses our toggle. With this the toggle in JS
    // land results in no-ops.
    ab = ArrayBuffer::New(env->isolate(), sizeof(uint32_t));
  } else {
    uint32_t* zero_fill_field = allocator->zero_fill_field();
    std::unique_ptr<BackingStore> backing =
        ArrayBuffer::NewBackingStore(zero_fill_field,
                                     sizeof(*zero_fill_field),
                                     [](void*, size_t, void*) {},
                                     nullptr);
    ab = ArrayBuffer::New(env->isolate(), std::move(backing));
  }

  ab->SetPrivate(
      env->context(),
      env->untransferable_object_private_symbol(),
      True(env->isolate())).Check();

  args.GetReturnValue().Set(Uint32Array::New(ab, 0, 1));
}

void DetachArrayBuffer(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (args[0]->IsArrayBuffer()) {
    Local<ArrayBuffer> buf = args[0].As<ArrayBuffer>();
    if (buf->IsDetachable()) {
      std::shared_ptr<BackingStore> store = buf->GetBackingStore();
      buf->Detach();
      args.GetReturnValue().Set(ArrayBuffer::New(env->isolate(), store));
    }
  }
}

namespace {

std::pair<void*, size_t> DecomposeBufferToParts(Local<Value> buffer) {
  void* pointer;
  size_t byte_length;
  if (buffer->IsArrayBuffer()) {
    Local<ArrayBuffer> ab = buffer.As<ArrayBuffer>();
    pointer = ab->Data();
    byte_length = ab->ByteLength();
  } else if (buffer->IsSharedArrayBuffer()) {
    Local<SharedArrayBuffer> ab = buffer.As<SharedArrayBuffer>();
    pointer = ab->Data();
    byte_length = ab->ByteLength();
  } else {
    UNREACHABLE();  // Caller must validate.
  }
  return {pointer, byte_length};
}

}  // namespace

void CopyArrayBuffer(const FunctionCallbackInfo<Value>& args) {
  // args[0] == Destination ArrayBuffer
  // args[1] == Destination ArrayBuffer Offset
  // args[2] == Source ArrayBuffer
  // args[3] == Source ArrayBuffer Offset
  // args[4] == bytesToCopy

  CHECK(args[0]->IsArrayBuffer() || args[0]->IsSharedArrayBuffer());
  CHECK(args[1]->IsUint32());
  CHECK(args[2]->IsArrayBuffer() || args[2]->IsSharedArrayBuffer());
  CHECK(args[3]->IsUint32());
  CHECK(args[4]->IsUint32());

  void* destination;
  size_t destination_byte_length;
  std::tie(destination, destination_byte_length) =
      DecomposeBufferToParts(args[0]);

  void* source;
  size_t source_byte_length;
  std::tie(source, source_byte_length) = DecomposeBufferToParts(args[2]);

  uint32_t destination_offset = args[1].As<Uint32>()->Value();
  uint32_t source_offset = args[3].As<Uint32>()->Value();
  size_t bytes_to_copy = args[4].As<Uint32>()->Value();

  CHECK_GE(destination_byte_length - destination_offset, bytes_to_copy);
  CHECK_GE(source_byte_length - source_offset, bytes_to_copy);

  uint8_t* dest = static_cast<uint8_t*>(destination) + destination_offset;
  uint8_t* src = static_cast<uint8_t*>(source) + source_offset;
  memcpy(dest, src, bytes_to_copy);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  SetMethod(context, target, "setBufferPrototype", SetBufferPrototype);
  SetMethodNoSideEffect(context, target, "createFromString", CreateFromString);
  SetMethodNoSideEffect(context, target, "decodeUTF8", DecodeUTF8);

  SetMethodNoSideEffect(context, target, "byteLengthUtf8", ByteLengthUtf8);
  SetMethod(context, target, "copy", Copy);
  SetMethodNoSideEffect(context, target, "compare", Compare);
  SetMethodNoSideEffect(context, target, "compareOffset", CompareOffset);
  SetMethod(context, target, "fill", Fill);
  SetMethodNoSideEffect(context, target, "indexOfBuffer", IndexOfBuffer);
  SetMethodNoSideEffect(context, target, "indexOfNumber", IndexOfNumber);
  SetMethodNoSideEffect(context, target, "indexOfString", IndexOfString);

  SetMethod(context, target, "detachArrayBuffer", DetachArrayBuffer);
  SetMethod(context, target, "copyArrayBuffer", CopyArrayBuffer);

  SetMethod(context, target, "swap16", Swap16);
  SetMethod(context, target, "swap32", Swap32);
  SetMethod(context, target, "swap64", Swap64);

  SetMethod(context, target, "encodeInto", EncodeInto);
  SetMethodNoSideEffect(context, target, "encodeUtf8String", EncodeUtf8String);

  SetMethodNoSideEffect(context, target, "isUtf8", IsUtf8);
  SetMethodNoSideEffect(context, target, "isAscii", IsAscii);

  target
      ->Set(context,
            FIXED_ONE_BYTE_STRING(isolate, "kMaxLength"),
            Number::New(isolate, kMaxLength))
      .Check();

  target
      ->Set(context,
            FIXED_ONE_BYTE_STRING(isolate, "kStringMaxLength"),
            Integer::New(isolate, String::kMaxLength))
      .Check();

  SetMethodNoSideEffect(context, target, "asciiSlice", StringSlice<ASCII>);
  SetMethodNoSideEffect(context, target, "base64Slice", StringSlice<BASE64>);
  SetMethodNoSideEffect(
      context, target, "base64urlSlice", StringSlice<BASE64URL>);
  SetMethodNoSideEffect(context, target, "latin1Slice", StringSlice<LATIN1>);
  SetMethodNoSideEffect(context, target, "hexSlice", StringSlice<HEX>);
  SetMethodNoSideEffect(context, target, "ucs2Slice", StringSlice<UCS2>);
  SetMethodNoSideEffect(context, target, "utf8Slice", StringSlice<UTF8>);

  SetMethod(context, target, "asciiWrite", StringWrite<ASCII>);
  SetMethod(context, target, "base64Write", StringWrite<BASE64>);
  SetMethod(context, target, "base64urlWrite", StringWrite<BASE64URL>);
  SetMethod(context, target, "latin1Write", StringWrite<LATIN1>);
  SetMethod(context, target, "hexWrite", StringWrite<HEX>);
  SetMethod(context, target, "ucs2Write", StringWrite<UCS2>);
  SetMethod(context, target, "utf8Write", StringWrite<UTF8>);

  SetMethod(context, target, "getZeroFillToggle", GetZeroFillToggle);
}

}  // anonymous namespace

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(SetBufferPrototype);
  registry->Register(CreateFromString);
  registry->Register(DecodeUTF8);

  registry->Register(ByteLengthUtf8);
  registry->Register(Copy);
  registry->Register(Compare);
  registry->Register(CompareOffset);
  registry->Register(Fill);
  registry->Register(IndexOfBuffer);
  registry->Register(IndexOfNumber);
  registry->Register(IndexOfString);

  registry->Register(Swap16);
  registry->Register(Swap32);
  registry->Register(Swap64);

  registry->Register(EncodeInto);
  registry->Register(EncodeUtf8String);

  registry->Register(IsUtf8);
  registry->Register(IsAscii);

  registry->Register(StringSlice<ASCII>);
  registry->Register(StringSlice<BASE64>);
  registry->Register(StringSlice<BASE64URL>);
  registry->Register(StringSlice<LATIN1>);
  registry->Register(StringSlice<HEX>);
  registry->Register(StringSlice<UCS2>);
  registry->Register(StringSlice<UTF8>);

  registry->Register(StringWrite<ASCII>);
  registry->Register(StringWrite<BASE64>);
  registry->Register(StringWrite<BASE64URL>);
  registry->Register(StringWrite<LATIN1>);
  registry->Register(StringWrite<HEX>);
  registry->Register(StringWrite<UCS2>);
  registry->Register(StringWrite<UTF8>);
  registry->Register(GetZeroFillToggle);

  registry->Register(DetachArrayBuffer);
  registry->Register(CopyArrayBuffer);
}

}  // namespace Buffer
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(buffer, node::Buffer::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(buffer,
                                node::Buffer::RegisterExternalReferences)
