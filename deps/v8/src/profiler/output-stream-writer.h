// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_OUTPUT_STREAM_WRITER_H_
#define V8_PROFILER_OUTPUT_STREAM_WRITER_H_

#include <algorithm>
#include <string>

#include "include/v8-profiler.h"
#include "src/base/logging.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

template <int bytes>
struct MaxDecimalDigitsIn;
template <>
struct MaxDecimalDigitsIn<1> {
  static const int kSigned = 3;
  static const int kUnsigned = 3;
};
template <>
struct MaxDecimalDigitsIn<4> {
  static const int kSigned = 11;
  static const int kUnsigned = 10;
};
template <>
struct MaxDecimalDigitsIn<8> {
  static const int kSigned = 20;
  static const int kUnsigned = 20;
};

class OutputStreamWriter {
 public:
  explicit OutputStreamWriter(v8::OutputStream* stream)
      : stream_(stream),
        chunk_size_(stream->GetChunkSize()),
        chunk_(chunk_size_),
        chunk_pos_(0),
        aborted_(false) {
    DCHECK_GT(chunk_size_, 0);
  }
  bool aborted() { return aborted_; }
  void AddCharacter(char c) {
    DCHECK_NE(c, '\0');
    DCHECK(chunk_pos_ < chunk_size_);
    chunk_[chunk_pos_++] = c;
    MaybeWriteChunk();
  }
  void AddString(const char* s) {
    size_t len = strlen(s);
    DCHECK_GE(kMaxInt, len);
    AddSubstring(s, static_cast<int>(len));
  }
  void AddSubstring(const char* s, int n) {
    if (n <= 0) return;
    DCHECK_LE(n, strlen(s));
    const char* s_end = s + n;
    while (s < s_end) {
      int s_chunk_size =
          std::min(chunk_size_ - chunk_pos_, static_cast<int>(s_end - s));
      DCHECK_GT(s_chunk_size, 0);
      MemCopy(chunk_.begin() + chunk_pos_, s, s_chunk_size);
      s += s_chunk_size;
      chunk_pos_ += s_chunk_size;
      MaybeWriteChunk();
    }
  }
  void AddNumber(unsigned n) { AddNumberImpl<unsigned>(n, "%u"); }
  void AddSize(size_t n) { AddNumberImpl<size_t>(n, "%zu"); }
  void Finalize() {
    if (aborted_) return;
    DCHECK(chunk_pos_ < chunk_size_);
    if (chunk_pos_ != 0) {
      WriteChunk();
    }
    stream_->EndOfStream();
  }

 private:
  template <typename T>
  void AddNumberImpl(T n, const char* format) {
    // Buffer for the longest value plus trailing \0
    static const int kMaxNumberSize =
        MaxDecimalDigitsIn<sizeof(T)>::kUnsigned + 1;
    if (chunk_size_ - chunk_pos_ >= kMaxNumberSize) {
      int result =
          SNPrintF(chunk_.SubVector(chunk_pos_, chunk_size_), format, n);
      DCHECK_NE(result, -1);
      chunk_pos_ += result;
      MaybeWriteChunk();
    } else {
      base::EmbeddedVector<char, kMaxNumberSize> buffer;
      int result = SNPrintF(buffer, format, n);
      USE(result);
      DCHECK_NE(result, -1);
      AddString(buffer.begin());
    }
  }
  void MaybeWriteChunk() {
    DCHECK(chunk_pos_ <= chunk_size_);
    if (chunk_pos_ == chunk_size_) {
      WriteChunk();
    }
  }
  void WriteChunk() {
    if (aborted_) return;
    if (stream_->WriteAsciiChunk(chunk_.begin(), chunk_pos_) ==
        v8::OutputStream::kAbort)
      aborted_ = true;
    chunk_pos_ = 0;
  }

  v8::OutputStream* stream_;
  int chunk_size_;
  base::ScopedVector<char> chunk_;
  int chunk_pos_;
  bool aborted_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_OUTPUT_STREAM_WRITER_H_
