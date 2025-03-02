// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_OSTREAMS_H_
#define V8_UTILS_OSTREAMS_H_

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <streambuf>

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE OFStreamBase : public std::streambuf {
 public:
  explicit OFStreamBase(FILE* f);
  ~OFStreamBase() override = default;

 protected:
  FILE* const f_;

  int sync() override;
  int_type overflow(int_type c) override;
  std::streamsize xsputn(const char* s, std::streamsize n) override;
};

// Output buffer and stream writing into debugger's command window.
class V8_EXPORT_PRIVATE DbgStreamBuf : public std::streambuf {
 public:
  DbgStreamBuf();
  ~DbgStreamBuf() override;

 private:
  int sync() override;
  int overflow(int c) override;

  char data_[256];
};

class DbgStdoutStream : public std::ostream {
 public:
  DbgStdoutStream();
  ~DbgStdoutStream() override = default;

 private:
  DbgStreamBuf streambuf_;
};

// An output stream writing to a file.
class V8_EXPORT_PRIVATE OFStream : public std::ostream {
 public:
  explicit OFStream(FILE* f);
  ~OFStream() override = default;

 private:
  OFStreamBase buf_;
};

#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
class V8_EXPORT_PRIVATE AndroidLogStream : public std::streambuf {
 public:
  virtual ~AndroidLogStream();

 protected:
  std::streamsize xsputn(const char* s, std::streamsize n) override;

 private:
  std::string line_buffer_;
};

class StdoutStream : public std::ostream {
 public:
  StdoutStream() : std::ostream(&stream_) {}

 private:
  friend class StderrStream;

  static V8_EXPORT_PRIVATE base::RecursiveMutex* GetStdoutMutex();

  AndroidLogStream stream_;
  base::RecursiveMutexGuard mutex_guard_{GetStdoutMutex()};
};
#else
class StdoutStream : public OFStream {
 public:
  StdoutStream() : OFStream(stdout) {}

 private:
  friend class StderrStream;
  static V8_EXPORT_PRIVATE base::RecursiveMutex* GetStdoutMutex();

  base::RecursiveMutexGuard mutex_guard_{GetStdoutMutex()};
};
#endif

class StderrStream : public OFStream {
 public:
  StderrStream() : OFStream(stderr) {}

 private:
  base::RecursiveMutexGuard mutex_guard_{StdoutStream::GetStdoutMutex()};
};

// Wrappers to disambiguate uint16_t and base::uc16.
struct AsUC16 {
  explicit AsUC16(uint16_t v) : value(v) {}
  uint16_t value;
};

struct AsUC32 {
  explicit AsUC32(int32_t v) : value(v) {}
  int32_t value;
};

struct AsReversiblyEscapedUC16 {
  explicit AsReversiblyEscapedUC16(uint16_t v) : value(v) {}
  uint16_t value;
};

struct AsEscapedUC16ForJSON {
  explicit AsEscapedUC16ForJSON(uint16_t v) : value(v) {}
  uint16_t value;
};

// Output the given value as hex, with a minimum width and optional prefix (0x).
// E.g. AsHex(23, 3, true) produces "0x017". Produces an empty string if both
// {min_width} and the value are 0.
struct AsHex {
  explicit AsHex(uint64_t v, uint8_t min_width = 1, bool with_prefix = false)
      : value(v), min_width(min_width), with_prefix(with_prefix) {}
  uint64_t value;
  uint8_t min_width;
  bool with_prefix;

  static AsHex Address(Address a) {
    return AsHex(a, kSystemPointerHexDigits, true);
  }
};

// Output the given value as hex, separated in individual bytes.
// E.g. AsHexBytes(0x231712, 4) produces "12 17 23 00" if output as little
// endian (default), and "00 23 17 12" as big endian. Produces an empty string
// if both {min_bytes} and the value are 0.
struct AsHexBytes {
  enum ByteOrder { kLittleEndian, kBigEndian };
  explicit AsHexBytes(uint64_t v, uint8_t min_bytes = 1,
                      ByteOrder byte_order = kLittleEndian)
      : value(v), min_bytes(min_bytes), byte_order(byte_order) {}
  uint64_t value;
  uint8_t min_bytes;
  ByteOrder byte_order;
};

template <typename T>
  requires requires(T t, std::ostream& os) { os << *t; }
struct PrintIteratorRange {
  T start;
  T end;
  const char* separator = ", ";
  const char* startBracket = "[";
  const char* endBracket = "]";

  PrintIteratorRange(T start, T end) : start(start), end(end) {}
  PrintIteratorRange& WithoutBrackets() {
    startBracket = "";
    endBracket = "";
    return *this;
  }
  PrintIteratorRange& WithSeparator(const char* new_separator) {
    this->separator = new_separator;
    return *this;
  }
};

// Print any collection which can be iterated via std::begin and std::end.
// {Iterator} is the common type of {std::begin} and {std::end} called on a
// {const T&}. This function is only instantiable if that type exists.
template <typename T>
auto PrintCollection(const T& collection) -> PrintIteratorRange<
    typename std::common_type<decltype(std::begin(collection)),
                              decltype(std::end(collection))>::type> {
  return {std::begin(collection), std::end(collection)};
}

// Writes the given character to the output escaping everything outside of
// printable/space ASCII range. Additionally escapes '\' making escaping
// reversible.
std::ostream& operator<<(std::ostream& os, const AsReversiblyEscapedUC16& c);

// Same as AsReversiblyEscapedUC16 with additional escaping of \n, \r, " and '.
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const AsEscapedUC16ForJSON& c);

// Writes the given character to the output escaping everything outside
// of printable ASCII range.
std::ostream& operator<<(std::ostream& os, const AsUC16& c);

// Writes the given character to the output escaping everything outside
// of printable ASCII range.
std::ostream& operator<<(std::ostream& os, const AsUC32& c);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, const AsHex& v);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const AsHexBytes& v);

template <typename T>
std::ostream& operator<<(std::ostream& os, const PrintIteratorRange<T>& range) {
  const char* separator = "";
  os << range.startBracket;
  for (T it = range.start; it != range.end; ++it, separator = range.separator) {
    os << separator << *it;
  }
  os << range.endBracket;
  return os;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_OSTREAMS_H_
