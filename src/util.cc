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

#include "util.h"  // NOLINT(build/include_inline)
#include <cmath>
#include <cstdint>
#include "util-inl.h"

#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_internals.h"
#include "node_snapshot_builder.h"
#include "node_v8_platform-inl.h"
#include "string_bytes.h"
#include "v8-value.h"

#ifdef _WIN32
#include <io.h>  // _S_IREAD _S_IWRITE
#include <time.h>
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif  // S_IRUSR
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif  // S_IWUSR
#else
#include <sys/time.h>
#include <sys/types.h>
#endif

#include <simdutf.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

static std::atomic_int seq = {0};  // Sequence number for diagnostic filenames.

// F_OK etc. constants
#ifdef _WIN32
#include "uv.h"
#else
#include <unistd.h>
#endif

// The access modes can be any of F_OK, R_OK, W_OK or X_OK. Some might not be
// available on specific systems. They can be used in combination as well
// (F_OK | R_OK | W_OK | X_OK).
constexpr int kMaximumAccessMode = F_OK | W_OK | R_OK | X_OK;
constexpr int kMinimumAccessMode = std::min({F_OK, W_OK, R_OK, X_OK});

constexpr int kDefaultCopyMode = 0;
// The copy modes can be any of UV_FS_COPYFILE_EXCL, UV_FS_COPYFILE_FICLONE or
// UV_FS_COPYFILE_FICLONE_FORCE. They can be used in combination as well
// (US_FS_COPYFILE_EXCL | US_FS_COPYFILE_FICLONE |
// US_FS_COPYFILE_FICLONE_FORCE).
constexpr int kMinimumCopyMode = std::min({kDefaultCopyMode,
                                           UV_FS_COPYFILE_EXCL,
                                           UV_FS_COPYFILE_FICLONE,
                                           UV_FS_COPYFILE_FICLONE_FORCE});
constexpr int kMaximumCopyMode =
    UV_FS_COPYFILE_EXCL | UV_FS_COPYFILE_FICLONE | UV_FS_COPYFILE_FICLONE_FORCE;

namespace node {

using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::Context;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Template;
using v8::Value;

template <typename T>
static void MakeUtf8String(Isolate* isolate,
                           Local<Value> value,
                           MaybeStackBuffer<T>* target) {
  Local<String> string;
  if (!value->ToString(isolate->GetCurrentContext()).ToLocal(&string)) return;
  String::ValueView value_view(isolate, string);

  auto value_length = value_view.length();

  if (value_view.is_one_byte()) {
    auto const_char = reinterpret_cast<const char*>(value_view.data8());
    auto expected_length =
        target->capacity() < (static_cast<size_t>(value_length) * 2 + 1)
            ? simdutf::utf8_length_from_latin1(const_char, value_length)
            : value_length * 2;

    // Add +1 for null termination.
    target->AllocateSufficientStorage(expected_length + 1);
    const auto actual_length = simdutf::convert_latin1_to_utf8(
        const_char, value_length, target->out());
    target->SetLengthAndZeroTerminate(actual_length);
    return;
  }

  // Add +1 for null termination.
  size_t storage = (3 * value_length) + 1;
  target->AllocateSufficientStorage(storage);

  // TODO(@anonrig): Use simdutf to speed up non-one-byte strings once it's
  // implemented
  const int flags =
      String::NO_NULL_TERMINATION | String::REPLACE_INVALID_UTF8;
  const int length =
      string->WriteUtf8(isolate, target->out(), storage, nullptr, flags);
  target->SetLengthAndZeroTerminate(length);
}

Utf8Value::Utf8Value(Isolate* isolate, Local<Value> value) {
  if (value.IsEmpty())
    return;

  MakeUtf8String(isolate, value, this);
}


TwoByteValue::TwoByteValue(Isolate* isolate, Local<Value> value) {
  if (value.IsEmpty()) {
    return;
  }

  Local<String> string;
  if (!value->ToString(isolate->GetCurrentContext()).ToLocal(&string)) return;

  // Allocate enough space to include the null terminator
  const size_t storage = string->Length() + 1;
  AllocateSufficientStorage(storage);

  const int flags = String::NO_NULL_TERMINATION;
  const int length = string->Write(isolate, out(), 0, storage, flags);
  SetLengthAndZeroTerminate(length);
}

BufferValue::BufferValue(Isolate* isolate, Local<Value> value) {
  // Slightly different take on Utf8Value. If value is a String,
  // it will return a Utf8 encoded string. If value is a Buffer,
  // it will copy the data out of the Buffer as is.
  if (value.IsEmpty()) {
    // Dereferencing this object will return nullptr.
    Invalidate();
    return;
  }

  if (value->IsString()) {
    MakeUtf8String(isolate, value, this);
  } else if (value->IsArrayBufferView()) {
    const size_t len = value.As<ArrayBufferView>()->ByteLength();
    // Leave place for the terminating '\0' byte.
    AllocateSufficientStorage(len + 1);
    value.As<ArrayBufferView>()->CopyContents(out(), len);
    SetLengthAndZeroTerminate(len);
  } else {
    Invalidate();
  }
}

void LowMemoryNotification() {
  if (per_process::v8_initialized) {
    auto isolate = Isolate::TryGetCurrent();
    if (isolate != nullptr) {
      isolate->LowMemoryNotification();
    }
  }
}

std::string GetProcessTitle(const char* default_title) {
  std::string buf(16, '\0');

  for (;;) {
    const int rc = uv_get_process_title(buf.data(), buf.size());

    if (rc == 0)
      break;

    // If uv_setup_args() was not called, `uv_get_process_title()` will always
    // return `UV_ENOBUFS`, no matter the input size. Guard against a possible
    // infinite loop by limiting the buffer size.
    if (rc != UV_ENOBUFS || buf.size() >= 1024 * 1024)
      return default_title;

    buf.resize(2 * buf.size());
  }

  // Strip excess trailing nul bytes. Using strlen() here is safe,
  // uv_get_process_title() always zero-terminates the result.
  buf.resize(strlen(buf.data()));

  return buf;
}

std::string GetHumanReadableProcessName() {
  return SPrintF("%s[%d]", GetProcessTitle("Node.js"), uv_os_getpid());
}

std::vector<std::string_view> SplitString(const std::string_view in,
                                          const std::string_view delim) {
  std::vector<std::string_view> out;

  for (auto first = in.data(), second = in.data(), last = first + in.size();
       second != last && first != last;
       first = second + 1) {
    second =
        std::find_first_of(first, last, std::cbegin(delim), std::cend(delim));

    if (first != second) {
      out.emplace_back(first, second - first);
    }
  }

  return out;
}

void ThrowErrStringTooLong(Isolate* isolate) {
  isolate->ThrowException(ERR_STRING_TOO_LONG(isolate));
}

double GetCurrentTimeInMicroseconds() {
  constexpr double kMicrosecondsPerSecond = 1e6;
  uv_timeval64_t tv;
  CHECK_EQ(0, uv_gettimeofday(&tv));
  return kMicrosecondsPerSecond * tv.tv_sec + tv.tv_usec;
}

int WriteFileSync(const char* path, uv_buf_t buf) {
  return WriteFileSync(path, &buf, 1);
}

int WriteFileSync(const char* path, uv_buf_t* bufs, size_t buf_count) {
  uv_fs_t req;
  int fd = uv_fs_open(nullptr,
                      &req,
                      path,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IWUSR | S_IRUSR,
                      nullptr);
  uv_fs_req_cleanup(&req);
  if (fd < 0) {
    return fd;
  }

  int err = uv_fs_write(nullptr, &req, fd, bufs, buf_count, 0, nullptr);
  uv_fs_req_cleanup(&req);
  if (err < 0) {
    return err;
  }

  err = uv_fs_close(nullptr, &req, fd, nullptr);
  uv_fs_req_cleanup(&req);
  return err;
}

int WriteFileSync(v8::Isolate* isolate,
                  const char* path,
                  v8::Local<v8::String> string) {
  node::Utf8Value utf8(isolate, string);
  uv_buf_t buf = uv_buf_init(utf8.out(), utf8.length());
  return WriteFileSync(path, buf);
}

int ReadFileSync(std::string* result, const char* path) {
  uv_fs_t req;
  auto defer_req_cleanup = OnScopeLeave([&req]() {
    uv_fs_req_cleanup(&req);
  });

  uv_file file = uv_fs_open(nullptr, &req, path, O_RDONLY, 0, nullptr);
  if (req.result < 0) {
    // req will be cleaned up by scope leave.
    return req.result;
  }
  uv_fs_req_cleanup(&req);

  auto defer_close = OnScopeLeave([file]() {
    uv_fs_t close_req;
    CHECK_EQ(0, uv_fs_close(nullptr, &close_req, file, nullptr));
    uv_fs_req_cleanup(&close_req);
  });

  *result = std::string("");
  char buffer[4096];
  uv_buf_t buf = uv_buf_init(buffer, sizeof(buffer));

  while (true) {
    const int r =
        uv_fs_read(nullptr, &req, file, &buf, 1, result->length(), nullptr);
    if (req.result < 0) {
      // req will be cleaned up by scope leave.
      return req.result;
    }
    uv_fs_req_cleanup(&req);
    if (r <= 0) {
      break;
    }
    result->append(buf.base, r);
  }
  return 0;
}

std::vector<char> ReadFileSync(FILE* fp) {
  CHECK_EQ(ftell(fp), 0);
  int err = fseek(fp, 0, SEEK_END);
  CHECK_EQ(err, 0);
  size_t size = ftell(fp);
  CHECK_NE(size, static_cast<size_t>(-1L));
  err = fseek(fp, 0, SEEK_SET);
  CHECK_EQ(err, 0);

  std::vector<char> contents(size);
  size_t num_read = fread(contents.data(), size, 1, fp);
  CHECK_EQ(num_read, 1);
  return contents;
}

void DiagnosticFilename::LocalTime(TIME_TYPE* tm_struct) {
#ifdef _WIN32
  GetLocalTime(tm_struct);
#else  // UNIX, macOS
  struct timeval time_val;
  gettimeofday(&time_val, nullptr);
  localtime_r(&time_val.tv_sec, tm_struct);
#endif
}

// Defined in node_internals.h
std::string DiagnosticFilename::MakeFilename(
    uint64_t thread_id,
    const char* prefix,
    const char* ext) {
  std::ostringstream oss;
  TIME_TYPE tm_struct;
  LocalTime(&tm_struct);
  oss << prefix;
#ifdef _WIN32
  oss << "." << std::setfill('0') << std::setw(4) << tm_struct.wYear;
  oss << std::setfill('0') << std::setw(2) << tm_struct.wMonth;
  oss << std::setfill('0') << std::setw(2) << tm_struct.wDay;
  oss << "." << std::setfill('0') << std::setw(2) << tm_struct.wHour;
  oss << std::setfill('0') << std::setw(2) << tm_struct.wMinute;
  oss << std::setfill('0') << std::setw(2) << tm_struct.wSecond;
#else  // UNIX, macOS
  oss << "."
            << std::setfill('0')
            << std::setw(4)
            << tm_struct.tm_year + 1900;
  oss << std::setfill('0')
            << std::setw(2)
            << tm_struct.tm_mon + 1;
  oss << std::setfill('0')
            << std::setw(2)
            << tm_struct.tm_mday;
  oss << "."
            << std::setfill('0')
            << std::setw(2)
            << tm_struct.tm_hour;
  oss << std::setfill('0')
            << std::setw(2)
            << tm_struct.tm_min;
  oss << std::setfill('0')
            << std::setw(2)
            << tm_struct.tm_sec;
#endif
  oss << "." << uv_os_getpid();
  oss << "." << thread_id;
  oss << "." << std::setfill('0') << std::setw(3) << ++seq;
  oss << "." << ext;
  return oss.str();
}

Local<v8::FunctionTemplate> NewFunctionTemplate(
    v8::Isolate* isolate,
    v8::FunctionCallback callback,
    Local<v8::Signature> signature,
    v8::ConstructorBehavior behavior,
    v8::SideEffectType side_effect_type,
    const v8::CFunction* c_function) {
  return v8::FunctionTemplate::New(isolate,
                                   callback,
                                   Local<v8::Value>(),
                                   signature,
                                   0,
                                   behavior,
                                   side_effect_type,
                                   c_function);
}

void SetMethod(Local<v8::Context> context,
               Local<v8::Object> that,
               const std::string_view name,
               v8::FunctionCallback callback) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Function> function =
      NewFunctionTemplate(isolate,
                          callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect)
          ->GetFunction(context)
          .ToLocalChecked();
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(context, name_string, function).Check();
  function->SetName(name_string);  // NODE_SET_METHOD() compatibility.
}

void SetMethod(v8::Isolate* isolate,
               v8::Local<v8::Template> that,
               const std::string_view name,
               v8::FunctionCallback callback) {
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetFastMethod(Isolate* isolate,
                   Local<Template> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::CFunction* c_function) {
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          slow_callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect,
                          c_function);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetFastMethod(Local<v8::Context> context,
                   Local<v8::Object> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::CFunction* c_function) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Function> function =
      NewFunctionTemplate(isolate,
                          slow_callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect,
                          c_function)
          ->GetFunction(context)
          .ToLocalChecked();
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(context, name_string, function).Check();
}

void SetFastMethodNoSideEffect(Local<v8::Context> context,
                               Local<v8::Object> that,
                               const std::string_view name,
                               v8::FunctionCallback slow_callback,
                               const v8::CFunction* c_function) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Function> function =
      NewFunctionTemplate(isolate,
                          slow_callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasNoSideEffect,
                          c_function)
          ->GetFunction(context)
          .ToLocalChecked();
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(context, name_string, function).Check();
}

void SetFastMethodNoSideEffect(Isolate* isolate,
                               Local<Template> that,
                               const std::string_view name,
                               v8::FunctionCallback slow_callback,
                               const v8::CFunction* c_function) {
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          slow_callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasNoSideEffect,
                          c_function);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetFastMethod(Isolate* isolate,
                   Local<Template> that,
                   const std::string_view name,
                   v8::FunctionCallback slow_callback,
                   const v8::MemorySpan<const v8::CFunction>& methods) {
  Local<v8::FunctionTemplate> t = FunctionTemplate::NewWithCFunctionOverloads(
      isolate,
      slow_callback,
      Local<Value>(),
      Local<v8::Signature>(),
      0,
      v8::ConstructorBehavior::kThrow,
      v8::SideEffectType::kHasSideEffect,
      methods);

  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetFastMethodNoSideEffect(
    Isolate* isolate,
    Local<Template> that,
    const std::string_view name,
    v8::FunctionCallback slow_callback,
    const v8::MemorySpan<const v8::CFunction>& methods) {
  Local<v8::FunctionTemplate> t = FunctionTemplate::NewWithCFunctionOverloads(
      isolate,
      slow_callback,
      Local<Value>(),
      Local<v8::Signature>(),
      0,
      v8::ConstructorBehavior::kThrow,
      v8::SideEffectType::kHasNoSideEffect,
      methods);

  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetMethodNoSideEffect(Local<v8::Context> context,
                           Local<v8::Object> that,
                           const std::string_view name,
                           v8::FunctionCallback callback) {
  Isolate* isolate = context->GetIsolate();
  Local<v8::Function> function =
      NewFunctionTemplate(isolate,
                          callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasNoSideEffect)
          ->GetFunction(context)
          .ToLocalChecked();
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(context, name_string, function).Check();
  function->SetName(name_string);  // NODE_SET_METHOD() compatibility.
}

void SetMethodNoSideEffect(Isolate* isolate,
                           Local<v8::Template> that,
                           const std::string_view name,
                           v8::FunctionCallback callback) {
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          callback,
                          Local<v8::Signature>(),
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasNoSideEffect);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->Set(name_string, t);
}

void SetProtoMethod(v8::Isolate* isolate,
                    Local<v8::FunctionTemplate> that,
                    const std::string_view name,
                    v8::FunctionCallback callback) {
  Local<v8::Signature> signature = v8::Signature::New(isolate, that);
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          callback,
                          signature,
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->PrototypeTemplate()->Set(name_string, t);
  t->SetClassName(name_string);  // NODE_SET_PROTOTYPE_METHOD() compatibility.
}

void SetProtoMethodNoSideEffect(v8::Isolate* isolate,
                                Local<v8::FunctionTemplate> that,
                                const std::string_view name,
                                v8::FunctionCallback callback) {
  Local<v8::Signature> signature = v8::Signature::New(isolate, that);
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          callback,
                          signature,
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasNoSideEffect);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->PrototypeTemplate()->Set(name_string, t);
  t->SetClassName(name_string);  // NODE_SET_PROTOTYPE_METHOD() compatibility.
}

void SetInstanceMethod(v8::Isolate* isolate,
                       Local<v8::FunctionTemplate> that,
                       const std::string_view name,
                       v8::FunctionCallback callback) {
  Local<v8::Signature> signature = v8::Signature::New(isolate, that);
  Local<v8::FunctionTemplate> t =
      NewFunctionTemplate(isolate,
                          callback,
                          signature,
                          v8::ConstructorBehavior::kThrow,
                          v8::SideEffectType::kHasSideEffect);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate, name.data(), type, name.size())
          .ToLocalChecked();
  that->InstanceTemplate()->Set(name_string, t);
  t->SetClassName(name_string);
}

void SetConstructorFunction(Local<v8::Context> context,
                            Local<v8::Object> that,
                            const char* name,
                            Local<v8::FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag) {
  Isolate* isolate = context->GetIsolate();
  SetConstructorFunction(
      context, that, OneByteString(isolate, name), tmpl, flag);
}

void SetConstructorFunction(Local<Context> context,
                            Local<Object> that,
                            Local<String> name,
                            Local<FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag) {
  if (flag == SetConstructorFunctionFlag::SET_CLASS_NAME) [[likely]] {
    tmpl->SetClassName(name);
  }
  that->Set(context, name, tmpl->GetFunction(context).ToLocalChecked()).Check();
}

void SetConstructorFunction(Isolate* isolate,
                            Local<Template> that,
                            const char* name,
                            Local<FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag) {
  SetConstructorFunction(
      isolate, that, OneByteString(isolate, name), tmpl, flag);
}

void SetConstructorFunction(Isolate* isolate,
                            Local<Template> that,
                            Local<String> name,
                            Local<FunctionTemplate> tmpl,
                            SetConstructorFunctionFlag flag) {
  if (flag == SetConstructorFunctionFlag::SET_CLASS_NAME) [[likely]] {
    tmpl->SetClassName(name);
  }
  that->Set(name, tmpl);
}

Local<String> UnionBytes::ToStringChecked(Isolate* isolate) const {
  if (is_one_byte()) {
    return String::NewExternalOneByte(isolate, one_byte_resource_)
        .ToLocalChecked();
  } else {
    return String::NewExternalTwoByte(isolate, two_byte_resource_)
        .ToLocalChecked();
  }
}

RAIIIsolateWithoutEntering::RAIIIsolateWithoutEntering(const SnapshotData* data)
    : allocator_{ArrayBuffer::Allocator::NewDefaultAllocator()} {
  isolate_ = Isolate::Allocate();
  CHECK_NOT_NULL(isolate_);
  per_process::v8_platform.Platform()->RegisterIsolate(isolate_,
                                                       uv_default_loop());
  Isolate::CreateParams params;
  if (data != nullptr) {
    SnapshotBuilder::InitializeIsolateParams(data, &params);
  }
  params.array_buffer_allocator = allocator_.get();
  Isolate::Initialize(isolate_, params);
}

RAIIIsolateWithoutEntering::~RAIIIsolateWithoutEntering() {
  per_process::v8_platform.Platform()->UnregisterIsolate(isolate_);
  isolate_->Dispose();
}

RAIIIsolate::RAIIIsolate(const SnapshotData* data)
    : isolate_{data}, isolate_scope_{isolate_.get()} {}

RAIIIsolate::~RAIIIsolate() {}

// Returns a string representation of the input value, including type.
// JavaScript implementation is available in lib/internal/errors.js
std::string DetermineSpecificErrorType(Environment* env,
                                       v8::Local<v8::Value> input) {
  if (input->IsFunction()) {
    return "function";
  } else if (input->IsString()) {
    auto value = Utf8Value(env->isolate(), input).ToString();
    if (value.size() > 28) {
      value = value.substr(0, 25) + "...";
    }
    if (value.find('\'') == std::string::npos) {
      return SPrintF("type string ('%s')", value);
    }

    // Stringify the input value.
    Local<String> stringified =
        v8::JSON::Stringify(env->context(), input).ToLocalChecked();
    Utf8Value stringified_value(env->isolate(), stringified);
    return SPrintF("type string (%s)", stringified_value.out());
  } else if (input->IsObject()) {
    v8::Local<v8::String> constructor_name =
        input.As<v8::Object>()->GetConstructorName();
    Utf8Value name(env->isolate(), constructor_name);
    return SPrintF("an instance of %s", name.out());
  } else if (input->IsSymbol()) {
    v8::MaybeLocal<v8::String> str =
        input.As<v8::Symbol>()->ToDetailString(env->context());
    v8::Local<v8::String> js_str;
    if (!str.ToLocal(&js_str)) {
      return "Symbol";
    }
    Utf8Value name(env->isolate(), js_str);
    // Symbol(xxx)
    return name.out();
  }

  Utf8Value utf8_value(env->isolate(),
                       input->ToString(env->context()).ToLocalChecked());

  if (input->IsNumber() || input->IsInt32() || input->IsUint32()) {
    auto value = input.As<v8::Number>()->Value();
    if (std::isnan(value)) {
      return "type number (NaN)";
    } else if (std::isinf(value)) {
      return "type number (Infinity)";
    }
    return SPrintF("type number (%s)", utf8_value.out());
  } else if (input->IsBigInt() || input->IsBoolean() || input->IsSymbol()) {
    Utf8Value type(env->isolate(), input->TypeOf(env->isolate()));
    return SPrintF("type %s (%s)", type.out(), utf8_value.out());
  }

  // For example: null, undefined
  return utf8_value.ToString();
}

v8::Maybe<int32_t> GetValidatedFd(Environment* env,
                                  v8::Local<v8::Value> input) {
  if (!input->IsInt32() && !input->IsNumber()) {
    std::string error_type = node::DetermineSpecificErrorType(env, input);
    THROW_ERR_INVALID_ARG_TYPE(env,
                               "The \"fd\" argument must be of type "
                               "number. Received %s",
                               error_type.c_str());
    return v8::Nothing<int32_t>();
  }

  const double fd = input.As<v8::Number>()->Value();
  const bool is_out_of_range = fd < 0 || fd > INT32_MAX;

  if (is_out_of_range || !IsSafeJsInt(input)) {
    Utf8Value utf8_value(
        env->isolate(), input->ToDetailString(env->context()).ToLocalChecked());
    if (is_out_of_range && !std::isinf(fd)) {
      THROW_ERR_OUT_OF_RANGE(env,
                             "The value of \"fd\" is out of range. "
                             "It must be >= 0 && <= %s. Received %s",
                             std::to_string(INT32_MAX),
                             utf8_value.out());
    } else {
      THROW_ERR_OUT_OF_RANGE(
          env,
          "The value of \"fd\" is out of range. It must be an integer. "
          "Received %s",
          utf8_value.out());
    }
    return v8::Nothing<int32_t>();
  }

  return v8::Just(static_cast<int32_t>(fd));
}

v8::Maybe<int> GetValidFileMode(Environment* env,
                                v8::Local<v8::Value> input,
                                uv_fs_type type) {
  // Allow only int32 or null/undefined values.
  if (input->IsNumber()) {
    // We cast the input to v8::Number to avoid overflows.
    auto num = input.As<v8::Number>()->Value();

    // Handle infinity and NaN values
    if (std::isinf(num) || std::isnan(num)) {
      THROW_ERR_OUT_OF_RANGE(env, "mode is out of range");
      return v8::Nothing<int>();
    }
  } else if (!input->IsNullOrUndefined()) {
    THROW_ERR_INVALID_ARG_TYPE(env, "mode must be int32 or null/undefined");
    return v8::Nothing<int>();
  }

  int min = kMinimumAccessMode;
  int max = kMaximumAccessMode;
  int def = F_OK;

  CHECK(type == UV_FS_ACCESS || type == UV_FS_COPYFILE);

  if (type == UV_FS_COPYFILE) {
    min = kMinimumCopyMode;
    max = kMaximumCopyMode;
    def = input->IsNullOrUndefined() ? kDefaultCopyMode
                                     : input.As<v8::Int32>()->Value();
  }

  if (input->IsNullOrUndefined()) {
    return v8::Just(def);
  }

  const int mode = input.As<v8::Int32>()->Value();
  if (mode < min || mode > max) {
    THROW_ERR_OUT_OF_RANGE(
        env, "mode is out of range: >= %d && <= %d", min, max);
    return v8::Nothing<int>();
  }

  return v8::Just(mode);
}

}  // namespace node
