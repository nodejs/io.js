// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/scanner-character-streams.h"

#include <memory>
#include <vector>

#include "include/v8-callbacks.h"
#include "include/v8-primitive.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/execution/isolate-utils.h"
#include "src/handles/handles.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/scanner.h"
#include "src/strings/unicode-inl.h"

namespace v8 {
namespace internal {

class V8_NODISCARD ScopedExternalStringLock {
 public:
  explicit ScopedExternalStringLock(Tagged<ExternalString> string) {
    DCHECK(!string.is_null());
    if (IsExternalOneByteString(string)) {
      resource_ = Cast<ExternalOneByteString>(string)->resource();
    } else {
      DCHECK(IsExternalTwoByteString(string));
      resource_ = Cast<ExternalTwoByteString>(string)->resource();
    }
    DCHECK(resource_);
    resource_->Lock();
  }

  // Copying a lock increases the locking depth.
  ScopedExternalStringLock(const ScopedExternalStringLock& other) V8_NOEXCEPT
      : resource_(other.resource_) {
    resource_->Lock();
  }

  ~ScopedExternalStringLock() { resource_->Unlock(); }

 private:
  // Not nullptr.
  const v8::String::ExternalStringResourceBase* resource_;
};

namespace {
const unibrow::uchar kUtf8Bom = 0xFEFF;
}  // namespace

template <typename Char>
struct Range {
  const Char* start;
  const Char* end;

  size_t length() { return static_cast<size_t>(end - start); }
  bool unaligned_start() const {
    return reinterpret_cast<intptr_t>(start) % sizeof(Char) == 1;
  }
};

// A Char stream backed by an on-heap SeqOneByteString or SeqTwoByteString.
template <typename Char>
class OnHeapStream {
 public:
  using String = typename CharTraits<Char>::String;

  OnHeapStream(Handle<String> string, size_t start_offset, size_t end)
      : string_(string), start_offset_(start_offset), length_(end) {}

  OnHeapStream(const OnHeapStream&) V8_NOEXCEPT : start_offset_(0), length_(0) {
    UNREACHABLE();
  }

  // The no_gc argument is only here because of the templated way this class
  // is used along with other implementations that require V8 heap access.
  Range<Char> GetDataAt(size_t pos, RuntimeCallStats* stats,
                        DisallowGarbageCollection* no_gc) {
    return {&string_->GetChars(*no_gc)[start_offset_ + std::min(length_, pos)],
            &string_->GetChars(*no_gc)[start_offset_ + length_]};
  }

  static const bool kCanBeCloned = false;
  static const bool kCanAccessHeap = true;

 private:
  Handle<String> string_;
  const size_t start_offset_;
  const size_t length_;
};

// A Char stream backed by an off-heap ExternalOneByteString or
// ExternalTwoByteString.
template <typename Char>
class ExternalStringStream {
  using ExternalString = typename CharTraits<Char>::ExternalString;

 public:
  ExternalStringStream(Tagged<ExternalString> string, size_t start_offset,
                       size_t length)
      : lock_(string),
        data_(string->GetChars() + start_offset),
        length_(length) {}

  ExternalStringStream(const ExternalStringStream& other) V8_NOEXCEPT
      : lock_(other.lock_),
        data_(other.data_),
        length_(other.length_) {}

  // The no_gc argument is only here because of the templated way this class
  // is used along with other implementations that require V8 heap access.
  Range<Char> GetDataAt(size_t pos, RuntimeCallStats* stats,
                        DisallowGarbageCollection* no_gc = nullptr) {
    return {&data_[std::min(length_, pos)], &data_[length_]};
  }

  static const bool kCanBeCloned = true;
  static const bool kCanAccessHeap = false;

 private:
  ScopedExternalStringLock lock_;
  const Char* const data_;
  const size_t length_;
};

// A Char stream backed by a C array. Testing only.
template <typename Char>
class TestingStream {
 public:
  TestingStream(const Char* data, size_t length)
      : data_(data), length_(length) {}
  // The no_gc argument is only here because of the templated way this class
  // is used along with other implementations that require V8 heap access.
  Range<Char> GetDataAt(size_t pos, RuntimeCallStats* stats,
                        DisallowGarbageCollection* no_gc = nullptr) {
    return {&data_[std::min(length_, pos)], &data_[length_]};
  }

  static const bool kCanBeCloned = true;
  static const bool kCanAccessHeap = false;

 private:
  const Char* const data_;
  const size_t length_;
};

// A Char stream backed by multiple source-stream provided off-heap chunks.
template <typename Char>
class ChunkedStream {
 public:
  explicit ChunkedStream(ScriptCompiler::ExternalSourceStream* source)
      : source_(source), chunks_(std::make_shared<std::vector<Chunk>>()) {}

  ChunkedStream(const ChunkedStream& other) V8_NOEXCEPT
      : source_(nullptr),
        chunks_(other.chunks_) {}

  // The no_gc argument is only here because of the templated way this class
  // is used along with other implementations that require V8 heap access.
  Range<Char> GetDataAt(size_t pos, RuntimeCallStats* stats,
                        DisallowGarbageCollection* no_gc = nullptr) {
    Chunk& chunk = FindChunk(pos, stats);
    size_t buffer_end = chunk.length;
    size_t buffer_pos = std::min(buffer_end, pos - chunk.position);
    return {&chunk.data.get()[buffer_pos], &chunk.data.get()[buffer_end]};
  }

  static const bool kCanBeCloned = true;
  static const bool kCanAccessHeap = false;

 private:
  struct Chunk {
    Chunk(const Char* const data, size_t position, size_t length)
        : data(data), position(position), length(length) {}
    std::unique_ptr<const Char[]> data;
    // The logical position of data.
    const size_t position;
    const size_t length;
    size_t end_position() const { return position + length; }
  };

  Chunk& FindChunk(size_t position, RuntimeCallStats* stats) {
    while (V8_UNLIKELY(chunks_->empty())) FetchChunk(size_t{0}, stats);

    // Walk forwards while the position is in front of the current chunk.
    while (position >= chunks_->back().end_position() &&
           chunks_->back().length > 0) {
      FetchChunk(chunks_->back().end_position(), stats);
    }

    // Walk backwards.
    for (auto reverse_it = chunks_->rbegin(); reverse_it != chunks_->rend();
         ++reverse_it) {
      if (reverse_it->position <= position) return *reverse_it;
    }

    UNREACHABLE();
  }

  virtual void ProcessChunk(const uint8_t* data, size_t position,
                            size_t length) {
    // Incoming data has to be aligned to Char size.
    DCHECK_EQ(0, length % sizeof(Char));
    chunks_->emplace_back(reinterpret_cast<const Char*>(data), position,
                          length / sizeof(Char));
  }

  void FetchChunk(size_t position, RuntimeCallStats* stats) {
    // Cloned ChunkedStreams have a null source, and therefore can't fetch any
    // new data.
    DCHECK_NOT_NULL(source_);

    const uint8_t* data = nullptr;
    size_t length;
    {
      RCS_SCOPE(stats, RuntimeCallCounterId::kGetMoreDataCallback);
      length = source_->GetMoreData(&data);
    }
    ProcessChunk(data, position, length);
  }

  ScriptCompiler::ExternalSourceStream* source_;

 protected:
  std::shared_ptr<std::vector<struct Chunk>> chunks_;
};

// Provides a buffered utf-16 view on the bytes from the underlying ByteStream.
// Chars are buffered if either the underlying stream isn't utf-16 or the
// underlying utf-16 stream might move (is on-heap).
template <template <typename T> class ByteStream>
class BufferedCharacterStream : public Utf16CharacterStream {
 public:
  template <class... TArgs>
  BufferedCharacterStream(size_t pos, TArgs... args) : byte_stream_(args...) {
    buffer_pos_ = pos;
  }

  bool can_be_cloned() const final {
    return ByteStream<uint16_t>::kCanBeCloned;
  }

  std::unique_ptr<Utf16CharacterStream> Clone() const override {
    CHECK(can_be_cloned());
    return std::unique_ptr<Utf16CharacterStream>(
        new BufferedCharacterStream<ByteStream>(*this));
  }

 protected:
  bool ReadBlock(size_t position) final {
    buffer_pos_ = position;
    buffer_start_ = &buffer_[0];
    buffer_cursor_ = buffer_start_;

    DisallowGarbageCollection no_gc;
    Range<uint8_t> range =
        byte_stream_.GetDataAt(position, runtime_call_stats(), &no_gc);
    if (range.length() == 0) {
      buffer_end_ = buffer_start_;
      return false;
    }

    size_t length = std::min({kBufferSize, range.length()});
    i::CopyChars(buffer_, range.start, length);
    buffer_end_ = &buffer_[length];
    return true;
  }

  bool can_access_heap() const final {
    return ByteStream<uint8_t>::kCanAccessHeap;
  }

 private:
  BufferedCharacterStream(const BufferedCharacterStream<ByteStream>& other)
      : byte_stream_(other.byte_stream_) {}

  static const size_t kBufferSize = 512;
  base::uc16 buffer_[kBufferSize];
  ByteStream<uint8_t> byte_stream_;
};

// Provides an unbuffered utf-16 view on the bytes from the underlying
// ByteStream.
template <template <typename T> class ByteStream>
class UnbufferedCharacterStream : public Utf16CharacterStream {
 public:
  template <class... TArgs>
  UnbufferedCharacterStream(size_t pos, TArgs... args) : byte_stream_(args...) {
    buffer_pos_ = pos;
  }

  bool can_access_heap() const final {
    return ByteStream<uint16_t>::kCanAccessHeap;
  }

  bool can_be_cloned() const final {
    return ByteStream<uint16_t>::kCanBeCloned;
  }

  std::unique_ptr<Utf16CharacterStream> Clone() const override {
    return std::unique_ptr<Utf16CharacterStream>(
        new UnbufferedCharacterStream<ByteStream>(*this));
  }

 protected:
  bool ReadBlock(size_t position) final {
    buffer_pos_ = position;
    DisallowGarbageCollection no_gc;
    Range<uint16_t> range =
        byte_stream_.GetDataAt(position, runtime_call_stats(), &no_gc);
    if (range.length() == 0) {
      // We should not set the buffer pointers to nullptr to avoid undefined
      // behavior, for example when incrementing buffer_cursor_. So instead use
      // this static array.
      static const uint16_t empty_buffer[1] = {0};
      buffer_start_ = empty_buffer;
      buffer_end_ = empty_buffer;
      buffer_cursor_ = empty_buffer;
      return false;
    }
    buffer_start_ = range.start;
    buffer_end_ = range.end;
    buffer_cursor_ = buffer_start_;

    DCHECK(!range.unaligned_start());
    DCHECK_LE(buffer_start_, buffer_end_);
    return true;
  }

  UnbufferedCharacterStream(const UnbufferedCharacterStream<ByteStream>& other)
      : byte_stream_(other.byte_stream_) {}

  ByteStream<uint16_t> byte_stream_;
};

// Provides an unbuffered utf-16 view on the bytes from the underlying
// ByteStream.
class RelocatingCharacterStream final
    : public UnbufferedCharacterStream<OnHeapStream> {
 public:
  template <class... TArgs>
  RelocatingCharacterStream(Isolate* isolate, size_t pos, TArgs... args)
      : UnbufferedCharacterStream<OnHeapStream>(pos, args...),
        isolate_(isolate) {
    isolate->main_thread_local_heap()->AddGCEpilogueCallback(
        UpdateBufferPointersCallback, this);
  }

 private:
  ~RelocatingCharacterStream() final {
    isolate_->main_thread_local_heap()->RemoveGCEpilogueCallback(
        UpdateBufferPointersCallback, this);
  }

  static void UpdateBufferPointersCallback(void* stream) {
    reinterpret_cast<RelocatingCharacterStream*>(stream)
        ->UpdateBufferPointers();
  }

  void UpdateBufferPointers() {
    DisallowGarbageCollection no_gc;
    Range<uint16_t> range =
        byte_stream_.GetDataAt(buffer_pos_, runtime_call_stats(), &no_gc);
    if (range.start != buffer_start_) {
      buffer_cursor_ = (buffer_cursor_ - buffer_start_) + range.start;
      buffer_start_ = range.start;
      buffer_end_ = range.end;
    }
  }

  Isolate* isolate_;
};

// ----------------------------------------------------------------------------
// BufferedUtf16CharacterStreams
//
// A buffered character stream based on a random access character
// source (ReadBlock can be called with pos() pointing to any position,
// even positions before the current).
//
// TODO(verwaest): Remove together with Utf8 external streaming streams.
class BufferedUtf16CharacterStream : public Utf16CharacterStream {
 public:
  BufferedUtf16CharacterStream();

 protected:
  static const size_t kBufferSize = 512;

  bool ReadBlock(size_t position) final;

  // FillBuffer should read up to kBufferSize characters at position and store
  // them into buffer_[0..]. It returns the number of characters stored.
  virtual size_t FillBuffer(size_t position) = 0;

  // Fixed sized buffer that this class reads from.
  // The base class' buffer_start_ should always point to buffer_.
  base::uc16 buffer_[kBufferSize];
};

BufferedUtf16CharacterStream::BufferedUtf16CharacterStream()
    : Utf16CharacterStream(buffer_, buffer_, buffer_, 0) {}

bool BufferedUtf16CharacterStream::ReadBlock(size_t position) {
  DCHECK_EQ(buffer_start_, buffer_);

  buffer_pos_ = position;
  buffer_cursor_ = buffer_;
  buffer_end_ = buffer_ + FillBuffer(position);
  DCHECK_EQ(pos(), position);
  DCHECK_LE(buffer_end_, buffer_start_ + kBufferSize);
  return buffer_cursor_ < buffer_end_;
}

// ----------------------------------------------------------------------------
// Windows1252CharacterStream - chunked streaming of windows-1252 data.
//
// Similar to BufferedCharacterStream, but does the translation of
// windows-1252 that are incompatible with their latin-1 equivalents.

namespace {

static const base::uc16 kWindows1252ToUC16[256] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,  // 00-07
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,  // 08-0F
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,  // 10-17
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,  // 18-1F
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,  // 20-27
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,  // 28-2F
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,  // 30-37
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,  // 38-3F
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,  // 40-47
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,  // 48-4F
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,  // 50-57
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,  // 58-5F
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,  // 60-67
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,  // 68-6F
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,  // 70-77
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,  // 78-7F
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,  // 88-8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,  // 98-9F
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,  // A0-A7
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,  // A8-AF
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,  // B0-B7
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,  // B8-BF
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,  // C0-C7
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,  // C8-CF
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,  // D0-D7
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,  // D8-DF
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,  // E0-E7
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,  // E8-EF
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,  // F0-F7
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF   // F8-FF
};

}  // namespace

class Windows1252CharacterStream final : public Utf16CharacterStream {
 public:
  Windows1252CharacterStream(
      size_t pos, ScriptCompiler::ExternalSourceStream* source_stream)
      : byte_stream_(source_stream) {
    buffer_pos_ = pos;
  }

  bool can_be_cloned() const final {
    return ChunkedStream<uint16_t>::kCanBeCloned;
  }

  std::unique_ptr<Utf16CharacterStream> Clone() const override {
    CHECK(can_be_cloned());
    return std::unique_ptr<Utf16CharacterStream>(
        new Windows1252CharacterStream(*this));
  }

 protected:
  bool ReadBlock(size_t position) final {
    buffer_pos_ = position;
    buffer_start_ = &buffer_[0];
    buffer_cursor_ = buffer_start_;

    DisallowGarbageCollection no_gc;
    Range<uint8_t> range =
        byte_stream_.GetDataAt(position, runtime_call_stats(), &no_gc);
    if (range.length() == 0) {
      buffer_end_ = buffer_start_;
      return false;
    }

    size_t length = std::min({kBufferSize, range.length()});
    std::transform(range.start, range.start + length, &buffer_[0],
                   [](uint8_t c) { return kWindows1252ToUC16[c]; });
    buffer_end_ = &buffer_[length];
    return true;
  }

  bool can_access_heap() const final {
    return ChunkedStream<uint8_t>::kCanAccessHeap;
  }

 private:
  Windows1252CharacterStream(const Windows1252CharacterStream& other)
      V8_NOEXCEPT : byte_stream_(other.byte_stream_) {}

  static const size_t kBufferSize = 512;
  base::uc16 buffer_[kBufferSize];
  ChunkedStream<uint8_t> byte_stream_;
};

// ----------------------------------------------------------------------------
// Utf8ExternalStreamingStream - chunked streaming of Utf-8 data.
//
// This implementation is fairly complex, since data arrives in chunks which
// may 'cut' arbitrarily into utf-8 characters. Also, seeking to a given
// character position is tricky because the byte position cannot be derived
// from the character position.
//
// TODO(verwaest): Decode utf8 chunks into utf16 chunks on the blink side
// instead so we don't need to buffer.

class Utf8ExternalStreamingStream final : public BufferedUtf16CharacterStream {
 public:
  Utf8ExternalStreamingStream(
      ScriptCompiler::ExternalSourceStream* source_stream)
      : chunks_(std::make_shared<std::vector<Chunk>>()),
        current_({0, {0, 0, 0, unibrow::Utf8::State::kAccept}}),
        source_stream_(source_stream) {}
  ~Utf8ExternalStreamingStream() final = default;

  bool can_access_heap() const final { return false; }

  bool can_be_cloned() const final { return true; }

  std::unique_ptr<Utf16CharacterStream> Clone() const override {
    return std::unique_ptr<Utf16CharacterStream>(
        new Utf8ExternalStreamingStream(*this));
  }

 protected:
  size_t FillBuffer(size_t position) final;

 private:
  // A position within the data stream. It stores:
  // - The 'physical' position (# of bytes in the stream),
  // - the 'logical' position (# of ucs-2 characters, also within the stream),
  // - a possibly incomplete utf-8 char at the current 'physical' position.
  struct StreamPosition {
    size_t bytes;
    size_t chars;
    uint32_t incomplete_char;
    unibrow::Utf8::State state;
  };

  // Position contains a StreamPosition and the index of the chunk the position
  // points into. (The chunk_no could be derived from pos, but that'd be
  // an expensive search through all chunks.)
  struct Position {
    size_t chunk_no;
    StreamPosition pos;
  };

  // A chunk in the list of chunks, containing:
  // - The chunk data (data pointer and length), and
  // - the position at the first byte of the chunk.
  struct Chunk {
    Chunk(const uint8_t* data, size_t length, StreamPosition start)
        : data(data), length(length), start(start) {}
    std::unique_ptr<const uint8_t[]> data;
    size_t length;
    StreamPosition start;
  };

  Utf8ExternalStreamingStream(const Utf8ExternalStreamingStream& source_stream)
      V8_NOEXCEPT : chunks_(source_stream.chunks_),
                    current_({0, {0, 0, 0, unibrow::Utf8::State::kAccept}}),
                    source_stream_(nullptr) {}

  // Within the current chunk, skip forward from current_ towards position.
  bool SkipToPosition(size_t position);
  // Within the current chunk, fill the buffer_ (while it has capacity).
  void FillBufferFromCurrentChunk();
  // Fetch a new chunk (assuming current_ is at the end of the current data).
  bool FetchChunk();
  // Search through the chunks and set current_ to point to the given position.
  // (This call is potentially expensive.)
  void SearchPosition(size_t position);

  Chunk& GetChunk(size_t chunk_no) { return (*chunks_)[chunk_no]; }

  std::shared_ptr<std::vector<Chunk>> chunks_;
  Position current_;
  ScriptCompiler::ExternalSourceStream* source_stream_;
};

bool Utf8ExternalStreamingStream::SkipToPosition(size_t position) {
  DCHECK_LE(current_.pos.chars, position);  // We can only skip forward.

  // Already there? Then return immediately.
  if (current_.pos.chars == position) return true;

  const Chunk& chunk = GetChunk(current_.chunk_no);
  DCHECK(current_.pos.bytes >= chunk.start.bytes);

  unibrow::Utf8::State state = chunk.start.state;
  uint32_t incomplete_char = chunk.start.incomplete_char;
  size_t it = current_.pos.bytes - chunk.start.bytes;
  const uint8_t* cursor = &chunk.data.get()[it];
  const uint8_t* end = &chunk.data.get()[chunk.length];

  size_t chars = current_.pos.chars;

  if (V8_UNLIKELY(current_.pos.bytes < 3 && chars == 0)) {
    while (cursor < end) {
      unibrow::uchar t =
          unibrow::Utf8::ValueOfIncremental(&cursor, &state, &incomplete_char);
      if (t == unibrow::Utf8::kIncomplete) continue;
      if (t != kUtf8Bom) {
        chars++;
        if (t > unibrow::Utf16::kMaxNonSurrogateCharCode) chars++;
      }
      break;
    }
  }

  while (cursor < end && chars < position) {
    unibrow::uchar t =
        unibrow::Utf8::ValueOfIncremental(&cursor, &state, &incomplete_char);
    if (t != unibrow::Utf8::kIncomplete) {
      chars++;
      if (t > unibrow::Utf16::kMaxNonSurrogateCharCode) chars++;
    }
  }

  current_.pos.bytes = chunk.start.bytes + (cursor - chunk.data.get());
  current_.pos.chars = chars;
  current_.pos.incomplete_char = incomplete_char;
  current_.pos.state = state;
  current_.chunk_no += (cursor == end);

  return current_.pos.chars == position;
}

void Utf8ExternalStreamingStream::FillBufferFromCurrentChunk() {
  DCHECK_LT(current_.chunk_no, chunks_->size());
  DCHECK_EQ(buffer_start_, buffer_cursor_);
  DCHECK_LT(buffer_end_ + 1, buffer_start_ + kBufferSize);

  const Chunk& chunk = GetChunk(current_.chunk_no);

  // The buffer_ is writable, but buffer_*_ members are const. So we get a
  // non-const pointer into buffer that points to the same char as buffer_end_.
  uint16_t* output_cursor = buffer_ + (buffer_end_ - buffer_start_);
  DCHECK_EQ(output_cursor, buffer_end_);

  unibrow::Utf8::State state = current_.pos.state;
  uint32_t incomplete_char = current_.pos.incomplete_char;

  // If the current chunk is the last (empty) chunk we'll have to process
  // any left-over, partial characters.
  if (chunk.length == 0) {
    unibrow::uchar t = unibrow::Utf8::ValueOfIncrementalFinish(&state);
    if (t != unibrow::Utf8::kBufferEmpty) {
      DCHECK_EQ(t, unibrow::Utf8::kBadChar);
      *output_cursor = static_cast<base::uc16>(t);
      buffer_end_++;
      current_.pos.chars++;
      current_.pos.incomplete_char = 0;
      current_.pos.state = state;
    }
    return;
  }

  size_t it = current_.pos.bytes - chunk.start.bytes;
  const uint8_t* cursor = chunk.data.get() + it;
  const uint8_t* end = chunk.data.get() + chunk.length;

  // Deal with possible BOM.
  if (V8_UNLIKELY(current_.pos.bytes < 3 && current_.pos.chars == 0)) {
    while (cursor < end) {
      unibrow::uchar t =
          unibrow::Utf8::ValueOfIncremental(&cursor, &state, &incomplete_char);
      if (V8_LIKELY(t < kUtf8Bom)) {
        *(output_cursor++) =
            static_cast<base::uc16>(t);  // The most frequent case.
      } else if (t == unibrow::Utf8::kIncomplete) {
        continue;
      } else if (t == kUtf8Bom) {
        // BOM detected at beginning of the stream. Don't copy it.
      } else if (t <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        *(output_cursor++) = static_cast<base::uc16>(t);
      } else {
        *(output_cursor++) = unibrow::Utf16::LeadSurrogate(t);
        *(output_cursor++) = unibrow::Utf16::TrailSurrogate(t);
      }
      break;
    }
  }

  const uint16_t* max_buffer_end = buffer_start_ + kBufferSize;
  while (cursor < end && output_cursor + 1 < max_buffer_end) {
    unibrow::uchar t =
        unibrow::Utf8::ValueOfIncremental(&cursor, &state, &incomplete_char);
    if (V8_LIKELY(t <= unibrow::Utf16::kMaxNonSurrogateCharCode)) {
      *(output_cursor++) =
          static_cast<base::uc16>(t);  // The most frequent case.
    } else if (t == unibrow::Utf8::kIncomplete) {
      continue;
    } else {
      *(output_cursor++) = unibrow::Utf16::LeadSurrogate(t);
      *(output_cursor++) = unibrow::Utf16::TrailSurrogate(t);
    }
    // Fast path for ascii sequences.
    size_t remaining = end - cursor;
    size_t max_buffer = max_buffer_end - output_cursor;
    int max_length = static_cast<int>(std::min(remaining, max_buffer));
    DCHECK_EQ(state, unibrow::Utf8::State::kAccept);
    int ascii_length = NonAsciiStart(cursor, max_length);
    CopyChars(output_cursor, cursor, ascii_length);
    cursor += ascii_length;
    output_cursor += ascii_length;
  }

  current_.pos.bytes = chunk.start.bytes + (cursor - chunk.data.get());
  current_.pos.chars += (output_cursor - buffer_end_);
  current_.pos.incomplete_char = incomplete_char;
  current_.pos.state = state;
  current_.chunk_no += (cursor == end);

  buffer_end_ = output_cursor;
}

bool Utf8ExternalStreamingStream::FetchChunk() {
  RCS_SCOPE(runtime_call_stats(), RuntimeCallCounterId::kGetMoreDataCallback);
  DCHECK_EQ(current_.chunk_no, chunks_->size());
  DCHECK(chunks_->empty() || chunks_->back().length != 0);

  // Clone Utf8ExternalStreamingStreams have a null source stream, and
  // therefore can't fetch any new data.
  DCHECK_NOT_NULL(source_stream_);

  // Utf8ExternalStreamingStreams that have been cloned are not allowed to fetch
  // any more.
  DCHECK_EQ(chunks_.use_count(), 1);

  const uint8_t* chunk = nullptr;
  size_t length = source_stream_->GetMoreData(&chunk);
  chunks_->emplace_back(chunk, length, current_.pos);
  return length > 0;
}

void Utf8ExternalStreamingStream::SearchPosition(size_t position) {
  // If current_ already points to the right position, we're done.
  //
  // This is expected to be the common case, since we typically call
  // FillBuffer right after the current buffer.
  if (current_.pos.chars == position) return;

  // No chunks. Fetch at least one, so we can assume !chunks_->empty() below.
  if (chunks_->empty()) {
    DCHECK_EQ(current_.chunk_no, 0u);
    DCHECK_EQ(current_.pos.bytes, 0u);
    DCHECK_EQ(current_.pos.chars, 0u);
    FetchChunk();
  }

  // Search for the last chunk whose start position is less or equal to
  // position.
  size_t chunk_no = chunks_->size() - 1;
  while (chunk_no > 0 && GetChunk(chunk_no).start.chars > position) {
    chunk_no--;
  }

  // Did we find the terminating (zero-length) chunk? Then we're seeking
  // behind the end of the data, and position does not exist.
  // Set current_ to point to the terminating chunk.
  if (GetChunk(chunk_no).length == 0) {
    current_ = {chunk_no, GetChunk(chunk_no).start};
    return;
  }

  // Did we find the non-last chunk? Then our position must be within chunk_no.
  if (chunk_no + 1 < chunks_->size()) {
    // Fancy-pants optimization for ASCII chunks within a utf-8 stream.
    // (Many web sites declare utf-8 encoding, but use only (or almost only) the
    //  ASCII subset for their JavaScript sources. We can exploit this, by
    //  checking whether the # bytes in a chunk are equal to the # chars, and if
    //  so avoid the expensive SkipToPosition.)
    bool ascii_only_chunk =
        GetChunk(chunk_no).start.incomplete_char == 0 &&
        (GetChunk(chunk_no + 1).start.bytes - GetChunk(chunk_no).start.bytes) ==
            (GetChunk(chunk_no + 1).start.chars -
             GetChunk(chunk_no).start.chars);
    if (ascii_only_chunk) {
      size_t skip = position - GetChunk(chunk_no).start.chars;
      current_ = {chunk_no,
                  {GetChunk(chunk_no).start.bytes + skip,
                   GetChunk(chunk_no).start.chars + skip, 0,
                   unibrow::Utf8::State::kAccept}};
    } else {
      current_ = {chunk_no, GetChunk(chunk_no).start};
      SkipToPosition(position);
    }

    // Since position was within the chunk, SkipToPosition should have found
    // something.
    DCHECK_EQ(position, current_.pos.chars);
    return;
  }

  // What's left: We're in the last, non-terminating chunk. Our position
  // may be in the chunk, but it may also be in 'future' chunks, which we'll
  // have to obtain.
  DCHECK_EQ(chunk_no, chunks_->size() - 1);
  current_ = {chunk_no, GetChunk(chunk_no).start};
  bool have_more_data = true;
  bool found = SkipToPosition(position);
  while (have_more_data && !found) {
    DCHECK_EQ(current_.chunk_no, chunks_->size());
    have_more_data = FetchChunk();
    found = have_more_data && SkipToPosition(position);
  }

  // We'll return with a postion != the desired position only if we're out
  // of data. In that case, we'll point to the terminating chunk.
  DCHECK_EQ(found, current_.pos.chars == position);
  DCHECK_EQ(have_more_data, chunks_->back().length != 0);
  DCHECK_IMPLIES(!found, !have_more_data);
  DCHECK_IMPLIES(!found, current_.chunk_no == chunks_->size() - 1);
}

size_t Utf8ExternalStreamingStream::FillBuffer(size_t position) {
  buffer_cursor_ = buffer_;
  buffer_end_ = buffer_;

  SearchPosition(position);
  bool out_of_data = current_.chunk_no != chunks_->size() &&
                     GetChunk(current_.chunk_no).length == 0 &&
                     current_.pos.incomplete_char == 0;

  if (out_of_data) return 0;

  // Fill the buffer, until we have at least one char (or are out of data).
  // (The embedder might give us 1-byte blocks within a utf-8 char, so we
  //  can't guarantee progress with one chunk. Thus we iterate.)
  while (!out_of_data && buffer_cursor_ == buffer_end_) {
    // At end of current data, but there might be more? Then fetch it.
    if (current_.chunk_no == chunks_->size()) {
      out_of_data = !FetchChunk();
    }
    FillBufferFromCurrentChunk();
  }

  DCHECK_EQ(current_.pos.chars - position,
            static_cast<size_t>(buffer_end_ - buffer_cursor_));
  return buffer_end_ - buffer_cursor_;
}

// ----------------------------------------------------------------------------
// ScannerStream: Create stream instances.

Utf16CharacterStream* ScannerStream::For(Isolate* isolate,
                                         Handle<String> data) {
  return ScannerStream::For(isolate, data, 0, data->length());
}

Utf16CharacterStream* ScannerStream::For(Isolate* isolate, Handle<String> data,
                                         int start_pos, int end_pos) {
  CHECK_GE(start_pos, 0);
  CHECK_LE(start_pos, end_pos);
  CHECK_LE(end_pos, data->length());
  size_t start_offset = 0;
  if (IsSlicedString(*data)) {
    Tagged<SlicedString> string = Cast<SlicedString>(*data);
    start_offset = string->offset();
    Tagged<String> parent = string->parent();
    if (IsThinString(parent)) parent = Cast<ThinString>(parent)->actual();
    data = handle(parent, isolate);
  } else {
    data = String::Flatten(isolate, data);
  }
  if (IsExternalOneByteString(*data)) {
    return new BufferedCharacterStream<ExternalStringStream>(
        static_cast<size_t>(start_pos), Cast<ExternalOneByteString>(*data),
        start_offset, static_cast<size_t>(end_pos));
  } else if (IsExternalTwoByteString(*data)) {
    return new UnbufferedCharacterStream<ExternalStringStream>(
        static_cast<size_t>(start_pos), Cast<ExternalTwoByteString>(*data),
        start_offset, static_cast<size_t>(end_pos));
  } else if (IsSeqOneByteString(*data)) {
    return new BufferedCharacterStream<OnHeapStream>(
        static_cast<size_t>(start_pos), Cast<SeqOneByteString>(data),
        start_offset, static_cast<size_t>(end_pos));
  } else if (IsSeqTwoByteString(*data)) {
    return new RelocatingCharacterStream(
        isolate, static_cast<size_t>(start_pos), Cast<SeqTwoByteString>(data),
        start_offset, static_cast<size_t>(end_pos));
  } else {
    UNREACHABLE();
  }
}

std::unique_ptr<Utf16CharacterStream> ScannerStream::ForTesting(
    const char* data) {
  return ScannerStream::ForTesting(data, strlen(data));
}

std::unique_ptr<Utf16CharacterStream> ScannerStream::ForTesting(
    const char* data, size_t length) {
  if (data == nullptr) {
    DCHECK_EQ(length, 0);

    // We don't want to pass in a null pointer into the the character stream,
    // because then the one-past-the-end pointer is undefined, so instead pass
    // through this static array.
    static const char non_null_empty_string[1] = {0};
    data = non_null_empty_string;
  }

  return std::unique_ptr<Utf16CharacterStream>(
      new BufferedCharacterStream<TestingStream>(
          0, reinterpret_cast<const uint8_t*>(data), length));
}

std::unique_ptr<Utf16CharacterStream> ScannerStream::ForTesting(
    const uint16_t* data, size_t length) {
  if (data == nullptr) {
    DCHECK_EQ(length, 0);

    // We don't want to pass in a null pointer into the the character stream,
    // because then the one-past-the-end pointer is undefined, so instead pass
    // through this static array.
    static const uint16_t non_null_empty_uint16_t_string[1] = {0};
    data = non_null_empty_uint16_t_string;
  }

  return std::unique_ptr<Utf16CharacterStream>(
      new UnbufferedCharacterStream<TestingStream>(0, data, length));
}

Utf16CharacterStream* ScannerStream::For(
    ScriptCompiler::ExternalSourceStream* source_stream,
    v8::ScriptCompiler::StreamedSource::Encoding encoding) {
  switch (encoding) {
    case v8::ScriptCompiler::StreamedSource::TWO_BYTE:
      return new UnbufferedCharacterStream<ChunkedStream>(
          static_cast<size_t>(0), source_stream);
    case v8::ScriptCompiler::StreamedSource::ONE_BYTE:
      return new BufferedCharacterStream<ChunkedStream>(static_cast<size_t>(0),
                                                        source_stream);
    case v8::ScriptCompiler::StreamedSource::WINDOWS_1252:
      return new Windows1252CharacterStream(static_cast<size_t>(0),
                                            source_stream);
    case v8::ScriptCompiler::StreamedSource::UTF8:
      return new Utf8ExternalStreamingStream(source_stream);
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
