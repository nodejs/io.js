// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-result.h"

#include "src/base/strings.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

PRINTF_FORMAT(3, 0)
void VPrintFToString(std::string* str, size_t str_offset, const char* format,
                     va_list args) {
  DCHECK_LE(str_offset, str->size());
  size_t len = str_offset + strlen(format);
  // Allocate increasingly large buffers until the message fits.
  for (;; len = base::bits::RoundUpToPowerOfTwo64(len + 1)) {
    DCHECK_GE(kMaxInt, len);
    str->resize(len);
    va_list args_copy;
    va_copy(args_copy, args);
    int written =
        base::VSNPrintF(base::Vector<char>(&str->front() + str_offset,
                                           static_cast<int>(len - str_offset)),
                        format, args_copy);
    va_end(args_copy);
    if (written < 0) continue;  // not enough space.
    str->resize(str_offset + written);
    return;
  }
}

PRINTF_FORMAT(3, 4)
void PrintFToString(std::string* str, size_t str_offset, const char* format,
                    ...) {
  va_list args;
  va_start(args, format);
  VPrintFToString(str, str_offset, format, args);
  va_end(args);
}

}  // namespace

// static
std::string WasmError::FormatError(const char* format, va_list args) {
  std::string result;
  VPrintFToString(&result, 0, format, args);
  return result;
}

void ErrorThrower::Format(ErrorType type, const char* format, va_list args) {
  DCHECK_NE(kNone, type);
  // Only report the first error.
  if (error()) return;

  size_t context_len = 0;
  if (context_) {
    PrintFToString(&error_msg_, 0, "%s: ", context_);
    context_len = error_msg_.size();
  }
  VPrintFToString(&error_msg_, context_len, format, args);
  error_type_ = type;
}

void ErrorThrower::TypeError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Format(kTypeError, format, arguments);
  va_end(arguments);
}

void ErrorThrower::RangeError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Format(kRangeError, format, arguments);
  va_end(arguments);
}

void ErrorThrower::CompileError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Format(kCompileError, format, arguments);
  va_end(arguments);
}

void ErrorThrower::LinkError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Format(kLinkError, format, arguments);
  va_end(arguments);
}

void ErrorThrower::RuntimeError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Format(kRuntimeError, format, arguments);
  va_end(arguments);
}

Handle<Object> ErrorThrower::Reify() {
  Handle<JSFunction> constructor;
  switch (error_type_) {
    case kNone:
      UNREACHABLE();
    case kTypeError:
      constructor = isolate_->type_error_function();
      break;
    case kRangeError:
      constructor = isolate_->range_error_function();
      break;
    case kCompileError:
      constructor = isolate_->wasm_compile_error_function();
      break;
    case kLinkError:
      constructor = isolate_->wasm_link_error_function();
      break;
    case kRuntimeError:
      constructor = isolate_->wasm_runtime_error_function();
      break;
  }
  DirectHandle<String> message =
      isolate_->factory()
          ->NewStringFromUtf8(base::VectorOf(error_msg_))
          .ToHandleChecked();
  Reset();
  return isolate_->factory()->NewError(constructor, message);
}

void ErrorThrower::Reset() {
  error_type_ = kNone;
  error_msg_.clear();
}

ErrorThrower::~ErrorThrower() {
  if (!error() || isolate_->has_exception()) return;

  HandleScope handle_scope{isolate_};
  isolate_->Throw(*Reify());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
