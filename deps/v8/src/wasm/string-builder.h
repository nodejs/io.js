// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_STRING_BUILDER_H_
#define V8_WASM_STRING_BUILDER_H_

#include <cstring>
#include <string>
#include <vector>

#include "src/common/globals.h"

namespace v8 {
namespace internal {
namespace wasm {

// Similar to std::ostringstream, but about 4x faster.
// This base class works best for small-ish strings (up to kChunkSize); for
// producing large amounts of text, you probably want a subclass like
// MultiLineStringBuilder.
class StringBuilder {
 public:
  StringBuilder() : on_growth_(kReplacePreviousChunk) {}
  explicit StringBuilder(const StringBuilder&) = delete;
  StringBuilder& operator=(const StringBuilder&) = delete;
  ~StringBuilder() {
    for (char* chunk : chunks_) delete[] chunk;
    if (on_growth_ == kReplacePreviousChunk && start_ != stack_buffer_) {
      delete[] start_;
    }
  }

  // Reserves space for {n} characters and returns a pointer to its beginning.
  // Clients *must* write all {n} characters after calling this!
  // Don't call this directly, use operator<< overloads instead.
  char* allocate(size_t n) {
    if (remaining_bytes_ < n) Grow(n);
    char* result = cursor_;
    cursor_ += n;
    remaining_bytes_ -= n;
    return result;
  }
  // Convenience wrappers.
  void write(const uint8_t* data, size_t n) {
    char* ptr = allocate(n);
    memcpy(ptr, data, n);
  }
  void write(const char* data, size_t n) {
    char* ptr = allocate(n);
    memcpy(ptr, data, n);
  }

  const char* start() const { return start_; }
  const char* cursor() const { return cursor_; }
  size_t length() const { return static_cast<size_t>(cursor_ - start_); }
  void rewind_to_start() {
    remaining_bytes_ += length();
    cursor_ = start_;
  }

  // Erases the last character that was written. Calling this repeatedly
  // isn't safe due to internal chunking of the backing store.
  void backspace() {
    DCHECK_GT(cursor_, start_);
    cursor_--;
    remaining_bytes_++;
  }

 protected:
  enum OnGrowth : bool { kKeepOldChunks, kReplacePreviousChunk };

  // Useful for subclasses that divide the text into ranges, e.g. lines.
  explicit StringBuilder(OnGrowth on_growth) : on_growth_(on_growth) {}
  void start_here() { start_ = cursor_; }

  size_t approximate_size_mb() {
    static_assert(kChunkSize == size_t{MB});
    return chunks_.size();
  }

 private:
  void Grow(size_t requested) {
    size_t used = length();
    size_t required = used + requested;
    size_t chunk_size;
    if (on_growth_ == kKeepOldChunks) {
      // Usually grow by kChunkSize, unless super-long lines need even more.
      chunk_size = required < kChunkSize ? kChunkSize : required * 2;
    } else {
      // When we only have one chunk, always (at least) double its size
      // when it grows, to minimize both wasted memory and growth effort.
      chunk_size = required * 2;
    }

    char* new_chunk = new char[chunk_size];
    memcpy(new_chunk, start_, used);
    if (on_growth_ == kKeepOldChunks) {
      chunks_.push_back(new_chunk);
    } else if (start_ != stack_buffer_) {
      delete[] start_;
    }
    start_ = new_chunk;
    cursor_ = new_chunk + used;
    remaining_bytes_ = chunk_size - used;
  }

  // Start small, to be cheap for the common case.
  static constexpr size_t kStackSize = 256;
  // If we have to grow, grow in big steps.
  static constexpr size_t kChunkSize = 1024 * 1024;

  char stack_buffer_[kStackSize];
  std::vector<char*> chunks_;  // A very simple Zone, essentially.
  char* start_ = stack_buffer_;
  char* cursor_ = stack_buffer_;
  size_t remaining_bytes_ = kStackSize;
  const OnGrowth on_growth_;
};

inline StringBuilder& operator<<(StringBuilder& sb, const char* str) {
  size_t len = strlen(str);
  char* ptr = sb.allocate(len);
  memcpy(ptr, str, len);
  return sb;
}

inline StringBuilder& operator<<(StringBuilder& sb, char c) {
  *sb.allocate(1) = c;
  return sb;
}

inline StringBuilder& operator<<(StringBuilder& sb, const std::string& s) {
  sb.write(s.data(), s.length());
  return sb;
}

inline StringBuilder& operator<<(StringBuilder& sb, uint32_t n) {
  if (n == 0) {
    *sb.allocate(1) = '0';
    return sb;
  }
  static constexpr size_t kBufferSize = 10;  // Just enough for a uint32.
  char buffer[kBufferSize];
  char* end = buffer + kBufferSize;
  char* out = end;
  while (n != 0) {
    *(--out) = '0' + (n % 10);
    n /= 10;
  }
  sb.write(out, static_cast<size_t>(end - out));
  return sb;
}

inline StringBuilder& operator<<(StringBuilder& sb, int value) {
  if (value >= 0) {
    sb << static_cast<uint32_t>(value);
  } else {
    sb << "-" << ((~static_cast<uint32_t>(value)) + 1);
  }
  return sb;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_STRING_BUILDER_H_
