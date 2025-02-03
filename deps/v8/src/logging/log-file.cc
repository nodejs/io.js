// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logging/log-file.h"

#include <atomic>
#include <memory>
#include <optional>

#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/isolate-utils.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"
#include "src/objects/string-inl.h"
#include "src/objects/tagged.h"
#include "src/strings/string-stream.h"
#include "src/utils/version.h"

namespace v8 {
namespace internal {

const char* const LogFile::kLogToTemporaryFile = "+";
const char* const LogFile::kLogToConsole = "-";

// static
FILE* LogFile::CreateOutputHandle(std::string file_name) {
  // If we're logging anything, we need to open the log file.
  if (!v8_flags.log) {
    return nullptr;
  } else if (LogFile::IsLoggingToConsole(file_name)) {
    return stdout;
  } else if (LogFile::IsLoggingToTemporaryFile(file_name)) {
    return base::OS::OpenTemporaryFile();
  } else {
    return base::OS::FOpen(file_name.c_str(), base::OS::LogFileOpenMode);
  }
}

// static
bool LogFile::IsLoggingToConsole(std::string file_name) {
  return file_name.compare(LogFile::kLogToConsole) == 0;
}

// static
bool LogFile::IsLoggingToTemporaryFile(std::string file_name) {
  return file_name.compare(LogFile::kLogToTemporaryFile) == 0;
}

LogFile::LogFile(V8FileLogger* logger, std::string file_name)
    : logger_(logger),
      file_name_(file_name),
      output_handle_(LogFile::CreateOutputHandle(file_name)),
      os_(output_handle_ == nullptr ? stdout : output_handle_),
      format_buffer_(NewArray<char>(kMessageBufferSize)) {
  if (output_handle_) WriteLogHeader();
}

void LogFile::WriteLogHeader() {
  LogFile::MessageBuilder msg(this);
  LogSeparator kNext = LogSeparator::kSeparator;
  msg << "v8-version" << kNext << Version::GetMajor() << kNext
      << Version::GetMinor() << kNext << Version::GetBuild() << kNext
      << Version::GetPatch();
  if (strlen(Version::GetEmbedder()) != 0) {
    msg << kNext << Version::GetEmbedder();
  }
  msg << kNext << Version::IsCandidate();
  msg.WriteToLogFile();
  msg << "v8-platform" << kNext << V8_OS_STRING << kNext << V8_TARGET_OS_STRING;
  msg.WriteToLogFile();
}

std::unique_ptr<LogFile::MessageBuilder> LogFile::NewMessageBuilder() {
  // Fast check of is_logging() without taking the lock. Bail out immediately if
  // logging isn't enabled.
  if (!logger_->is_logging()) return {};

  std::unique_ptr<LogFile::MessageBuilder> result(
      new LogFile::MessageBuilder(this));

  // The first invocation of is_logging() might still read an old value. It is
  // fine if a background thread starts logging a bit later, but we want to
  // avoid background threads continue logging after logging was already closed.
  if (!logger_->is_logging()) return {};
  DCHECK_NOT_NULL(format_buffer_.get());

  return result;
}

FILE* LogFile::Close() {
  FILE* result = nullptr;
  if (output_handle_ != nullptr) {
    fflush(output_handle_);
    result = output_handle_;
  }
  output_handle_ = nullptr;
  format_buffer_.reset();
  return result;
}

std::string LogFile::file_name() const { return file_name_; }

LogFile::MessageBuilder::MessageBuilder(LogFile* log)
    : log_(log), lock_guard_(&log_->mutex_) {}

void LogFile::MessageBuilder::AppendString(Tagged<String> str,
                                           std::optional<int> length_limit) {
  if (str.is_null()) return;

  DisallowGarbageCollection no_gc;  // Ensure string stays valid.
  SharedStringAccessGuardIfNeeded access_guard(str);
  int length = str->length();
  if (length_limit) length = std::min(length, *length_limit);
  for (int i = 0; i < length; i++) {
    uint16_t c = str->Get(i, access_guard);
    if (c <= 0xFF) {
      AppendCharacter(static_cast<char>(c));
    } else {
      // Escape non-ascii characters.
      AppendRawFormatString("\\u%04x", c & 0xFFFF);
    }
  }
}

void LogFile::MessageBuilder::AppendString(base::Vector<const char> str) {
  for (auto i = str.begin(); i < str.end(); i++) AppendCharacter(*i);
}

void LogFile::MessageBuilder::AppendString(const char* str) {
  if (str == nullptr) return;
  AppendString(str, strlen(str));
}

void LogFile::MessageBuilder::AppendString(const char* str, size_t length,
                                           bool is_one_byte) {
  if (str == nullptr) return;
  if (is_one_byte) {
    for (size_t i = 0; i < length; i++) {
      DCHECK_IMPLIES(is_one_byte, str[i] != '\0');
      AppendCharacter(str[i]);
    }
  } else {
    DCHECK_EQ(length % 2, 0);
    for (size_t i = 0; i + 1 < length; i += 2) {
      AppendTwoByteCharacter(str[i], str[i + 1]);
    }
  }
}

void LogFile::MessageBuilder::AppendFormatString(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int length = FormatStringIntoBuffer(format, args);
  va_end(args);
  for (int i = 0; i < length; i++) {
    DCHECK_NE(log_->format_buffer_[i], '\0');
    AppendCharacter(log_->format_buffer_[i]);
  }
}

void LogFile::MessageBuilder::AppendTwoByteCharacter(char c1, char c2) {
  if (c2 == 0) {
    AppendCharacter(c1);
  } else {
    // Escape non-printable characters.
    AppendRawFormatString("\\u%02x%02x", c1 & 0xFF, c2 & 0xFF);
  }
}
void LogFile::MessageBuilder::AppendCharacter(char c) {
  if (std::isprint(c)) {
    if (c == ',') {
      // Escape commas to avoid adding column separators.
      AppendRawString("\\x2C");
    } else if (c == '\\') {
      AppendRawString("\\\\");
    } else {
      // Safe, printable ascii character.
      AppendRawCharacter(c);
    }
  } else if (c == '\n') {
    // Escape newlines to avoid adding row separators.
    AppendRawString("\\n");
  } else {
    // Escape non-printable characters.
    AppendRawFormatString("\\x%02x", c & 0xFF);
  }
}

void LogFile::MessageBuilder::AppendSymbolName(Tagged<Symbol> symbol) {
  DCHECK(!symbol.is_null());
  OFStream& os = log_->os_;
  os << "symbol(";
  if (!IsUndefined(symbol->description())) {
    os << "\"";
    AppendSymbolNameDetails(Cast<String>(symbol->description()), false);
    os << "\" ";
  }
  os << "hash " << std::hex << symbol->hash() << std::dec << ")";
}

void LogFile::MessageBuilder::AppendSymbolNameDetails(Tagged<String> str,
                                                      bool show_impl_info) {
  if (str.is_null()) return;

  DisallowGarbageCollection no_gc;  // Ensure string stays valid.
  OFStream& os = log_->os_;
  int limit = str->length();
  if (limit > 0x1000) limit = 0x1000;
  if (show_impl_info) {
    os << (str->IsOneByteRepresentation() ? 'a' : '2');
    if (StringShape(str).IsExternal()) os << 'e';
    if (StringShape(str).IsInternalized()) os << '#';
    os << ':' << str->length() << ':';
  }
  AppendString(str, limit);
}

int LogFile::MessageBuilder::FormatStringIntoBuffer(const char* format,
                                                    va_list args) {
  base::Vector<char> buf(log_->format_buffer_.get(),
                         LogFile::kMessageBufferSize);
  int length = base::VSNPrintF(buf, format, args);
  // |length| is -1 if output was truncated.
  if (length == -1) length = LogFile::kMessageBufferSize;
  DCHECK_LE(length, LogFile::kMessageBufferSize);
  DCHECK_GE(length, 0);
  return length;
}

void LogFile::MessageBuilder::AppendRawFormatString(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int length = FormatStringIntoBuffer(format, args);
  va_end(args);
  for (int i = 0; i < length; i++) {
    DCHECK_NE(log_->format_buffer_[i], '\0');
    AppendRawCharacter(log_->format_buffer_[i]);
  }
}

void LogFile::MessageBuilder::AppendRawString(const char* str) {
  log_->os_ << str;
}

void LogFile::MessageBuilder::AppendRawCharacter(char c) { log_->os_ << c; }

void LogFile::MessageBuilder::WriteToLogFile() { log_->os_ << std::endl; }

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<<<const char*>(
    const char* string) {
  this->AppendString(string);
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<<<void*>(
    void* pointer) {
  OFStream& os = log_->os_;
  // Manually format the pointer since on Windows we do not consistently
  // get a "0x" prefix.
  os << "0x" << std::hex << reinterpret_cast<intptr_t>(pointer) << std::dec;
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<<<char>(char c) {
  this->AppendCharacter(c);
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<< <Tagged<String>>(
    Tagged<String> string) {
  this->AppendString(string);
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<< <Tagged<Symbol>>(
    Tagged<Symbol> symbol) {
  this->AppendSymbolName(symbol);
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<< <Tagged<Name>>(
    Tagged<Name> name) {
  if (IsString(name)) {
    this->AppendString(Cast<String>(name));
  } else {
    this->AppendSymbolName(Cast<Symbol>(name));
  }
  return *this;
}

template <>
LogFile::MessageBuilder& LogFile::MessageBuilder::operator<<<LogSeparator>(
    LogSeparator separator) {
  // Skip escaping to create a new column.
  this->AppendRawCharacter(',');
  return *this;
}

}  // namespace internal
}  // namespace v8
