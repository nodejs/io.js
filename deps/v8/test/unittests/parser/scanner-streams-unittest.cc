// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/strings.h"
#include "src/heap/factory-inl.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/scanner.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using ScannerStreamsTest = v8::TestWithIsolate;
// Implement ExternalSourceStream based on const char**.
// This will take each string as one chunk. The last chunk must be empty.
class ChunkSource : public v8::ScriptCompiler::ExternalSourceStream {
 public:
  template <typename Char>
  explicit ChunkSource(const Char** chunks) : current_(0) {
    do {
      chunks_.push_back({reinterpret_cast<const uint8_t*>(*chunks),
                         (sizeof(Char) / sizeof(uint8_t)) *
                             std::char_traits<Char>::length(*chunks)});
      chunks++;
    } while (chunks_.back().len > 0);
  }
  explicit ChunkSource(const char* chunks) : current_(0) {
    do {
      chunks_.push_back(
          {reinterpret_cast<const uint8_t*>(chunks), strlen(chunks)});
      chunks += strlen(chunks) + 1;
    } while (chunks_.back().len > 0);
  }
  ChunkSource(const uint8_t* data, size_t char_size, size_t len,
              bool extra_chunky)
      : current_(0) {
    // If extra_chunky, we'll use increasingly large chunk sizes.  If not, we'll
    // have a single chunk of full length. Make sure that chunks are always
    // aligned to char-size though.
    size_t chunk_size = extra_chunky ? char_size : len;
    for (size_t i = 0; i < len; i += chunk_size, chunk_size += char_size) {
      chunks_.push_back({data + i, std::min(chunk_size, len - i)});
    }
    chunks_.push_back({nullptr, 0});
  }
  ~ChunkSource() override = default;
  size_t GetMoreData(const uint8_t** src) override {
    DCHECK_LT(current_, chunks_.size());
    Chunk& next = chunks_[current_++];
    uint8_t* chunk = new uint8_t[next.len];
    if (next.len > 0) {
      i::MemMove(chunk, next.ptr, next.len);
    }
    *src = chunk;
    return next.len;
  }

 private:
  struct Chunk {
    const uint8_t* ptr;
    size_t len;
  };
  std::vector<Chunk> chunks_;
  size_t current_;
};

// Checks that Lock() / Unlock() pairs are balanced. Not thread-safe.
class LockChecker {
 public:
  LockChecker() : lock_depth_(0) {}
  ~LockChecker() { CHECK_EQ(0, lock_depth_); }

  void Lock() const { lock_depth_++; }

  void Unlock() const {
    CHECK_GT(lock_depth_, 0);
    lock_depth_--;
  }

  bool IsLocked() const { return lock_depth_ > 0; }

  int LockDepth() const { return lock_depth_; }

 protected:
  mutable int lock_depth_;
};

class TestExternalResource : public v8::String::ExternalStringResource,
                             public LockChecker {
 public:
  explicit TestExternalResource(uint16_t* data, int length)
      : LockChecker(), data_(data), length_(static_cast<size_t>(length)) {}

  const uint16_t* data() const override {
    CHECK(IsLocked());
    return data_;
  }

  size_t length() const override { return length_; }

  bool IsCacheable() const override { return false; }
  void Lock() const override { LockChecker::Lock(); }
  void Unlock() const override { LockChecker::Unlock(); }

 private:
  uint16_t* data_;
  size_t length_;
};

class TestExternalOneByteResource
    : public v8::String::ExternalOneByteStringResource,
      public LockChecker {
 public:
  TestExternalOneByteResource(const char* data, size_t length)
      : data_(data), length_(length) {}

  const char* data() const override {
    CHECK(IsLocked());
    return data_;
  }
  size_t length() const override { return length_; }

  bool IsCacheable() const override { return false; }
  void Lock() const override { LockChecker::Lock(); }
  void Unlock() const override { LockChecker::Unlock(); }

 private:
  const char* data_;
  size_t length_;
};

// A test string with all lengths of utf-8 encodings.
const char unicode_utf8[] =
    "abc"               // 3x ascii
    "\xc3\xa4"          // a Umlaut, code point 228
    "\xe2\xa8\xa0"      // >> (math symbol), code point 10784
    "\xf0\x9f\x92\xa9"  // best character, code point 128169,
                        //     as utf-16 surrogates: 55357 56489
    "def";              // 3x ascii again.
const uint16_t unicode_ucs2[] = {97,    98,  99,  228, 10784, 55357,
                                 56489, 100, 101, 102, 0};

i::Handle<i::String> NewExternalTwoByteStringFromResource(
    i::Isolate* isolate, TestExternalResource* resource) {
  i::Factory* factory = isolate->factory();
  // String creation accesses the resource.
  resource->Lock();
  i::Handle<i::String> uc16_string(
      factory->NewExternalStringFromTwoByte(resource).ToHandleChecked());
  resource->Unlock();
  return uc16_string;
}

}  // anonymous namespace

TEST_F(ScannerStreamsTest, Utf8StreamAsciiOnly) {
  const char* chunks[] = {"abc", "def", "ghi", ""};
  ChunkSource chunk_source(chunks);
  std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
      v8::internal::ScannerStream::For(
          &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

  // Read the data without dying.
  v8::base::uc32 c;
  do {
    c = stream->Advance();
  } while (c != v8::internal::Utf16CharacterStream::kEndOfInput);
}

TEST_F(ScannerStreamsTest, Utf8StreamMaxNonSurrogateCharCode) {
  const char* chunks[] = {"\uffff\uffff", ""};
  ChunkSource chunk_source(chunks);
  std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
      v8::internal::ScannerStream::For(
          &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

  // Read the correct character.
  uint16_t max = unibrow::Utf16::kMaxNonSurrogateCharCode;
  CHECK_EQ(max, static_cast<uint32_t>(stream->Advance()));
  CHECK_EQ(max, static_cast<uint32_t>(stream->Advance()));
  CHECK_EQ(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
}

TEST_F(ScannerStreamsTest, Utf8StreamBOM) {
  // Construct test string w/ UTF-8 BOM (byte order mark)
  char data[3 + arraysize(unicode_utf8)] = {"\xef\xbb\xbf"};
  strncpy(data + 3, unicode_utf8, arraysize(unicode_utf8));

  const char* chunks[] = {data, "\0"};
  ChunkSource chunk_source(chunks);
  std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
      v8::internal::ScannerStream::For(
          &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

  // Read the data without tripping over the BOM.
  for (size_t i = 0; unicode_ucs2[i]; i++) {
    CHECK_EQ(unicode_ucs2[i], stream->Advance());
  }
  CHECK_EQ(v8::internal::Utf16CharacterStream::kEndOfInput, stream->Advance());

  // Make sure seek works.
  stream->Seek(0);
  CHECK_EQ(unicode_ucs2[0], stream->Advance());

  stream->Seek(5);
  CHECK_EQ(unicode_ucs2[5], stream->Advance());

  // Try again, but make sure we have to seek 'backwards'.
  while (v8::internal::Utf16CharacterStream::kEndOfInput != stream->Advance()) {
    // Do nothing. We merely advance the stream to the end of its input.
  }
  stream->Seek(5);
  CHECK_EQ(unicode_ucs2[5], stream->Advance());
}

TEST_F(ScannerStreamsTest, Utf8SplitBOM) {
  // Construct chunks with a BOM split into two chunks.
  char partial_bom[] = "\xef\xbb";
  char data[1 + arraysize(unicode_utf8)] = {"\xbf"};
  strncpy(data + 1, unicode_utf8, arraysize(unicode_utf8));

  {
    const char* chunks[] = {partial_bom, data, "\0"};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    // Read the data without tripping over the BOM.
    for (size_t i = 0; unicode_ucs2[i]; i++) {
      CHECK_EQ(unicode_ucs2[i], stream->Advance());
    }
  }

  // And now with single-byte BOM chunks.
  char bom_byte_1[] = "\xef";
  char bom_byte_2[] = "\xbb";
  {
    const char* chunks[] = {bom_byte_1, bom_byte_2, data, "\0"};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    // Read the data without tripping over the BOM.
    for (size_t i = 0; unicode_ucs2[i]; i++) {
      CHECK_EQ(unicode_ucs2[i], stream->Advance());
    }
  }
}

TEST_F(ScannerStreamsTest, Utf8SplitMultiBOM) {
  // Construct chunks with a split BOM followed by another split BOM.
  const char* chunks[] = {"\xef\xbb", "\xbf\xef\xbb", "\xbf", ""};
  ChunkSource chunk_source(chunks);
  std::unique_ptr<i::Utf16CharacterStream> stream(
      v8::internal::ScannerStream::For(
          &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

  // Read the data, ensuring we get exactly one of the two BOMs back.
  CHECK_EQ(0xFEFF, stream->Advance());
  CHECK_EQ(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
}

TEST_F(ScannerStreamsTest, Utf8AdvanceUntil) {
  // Test utf-8 advancing until a certain char.

  const char line_term = '\n';
  const size_t kLen = arraysize(unicode_utf8);
  char data[kLen + 1];
  strncpy(data, unicode_utf8, kLen);
  data[kLen - 1] = line_term;
  data[kLen] = '\0';

  {
    const char* chunks[] = {data, "\0"};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    int32_t res = stream->AdvanceUntil(
        [](int32_t c0_) { return unibrow::IsLineTerminator(c0_); });
    CHECK_EQ(line_term, res);
  }
}

TEST_F(ScannerStreamsTest, AdvanceMatchAdvanceUntil) {
  // Test if single advance and advanceUntil behave the same

  char data[] = {'a', 'b', '\n', 'c', '\0'};

  {
    const char* chunks[] = {data, "\0"};
    ChunkSource chunk_source_a(chunks);

    std::unique_ptr<v8::internal::Utf16CharacterStream> stream_advance(
        v8::internal::ScannerStream::For(
            &chunk_source_a, v8::ScriptCompiler::StreamedSource::UTF8));

    ChunkSource chunk_source_au(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream_advance_until(
        v8::internal::ScannerStream::For(
            &chunk_source_au, v8::ScriptCompiler::StreamedSource::UTF8));

    int32_t au_c0_ = stream_advance_until->AdvanceUntil(
        [](int32_t c0_) { return unibrow::IsLineTerminator(c0_); });

    int32_t a_c0_ = '0';
    while (!unibrow::IsLineTerminator(a_c0_)) {
      a_c0_ = stream_advance->Advance();
    }

    // Check both advances methods have the same output
    CHECK_EQ(a_c0_, au_c0_);

    // Check if both set the cursor to the correct position by advancing both
    // streams by one character.
    a_c0_ = stream_advance->Advance();
    au_c0_ = stream_advance_until->Advance();
    CHECK_EQ(a_c0_, au_c0_);
  }
}

TEST_F(ScannerStreamsTest, Utf8AdvanceUntilOverChunkBoundaries) {
  // Test utf-8 advancing until a certain char, crossing chunk boundaries.

  // Split the test string at each byte and pass it to the stream. This way,
  // we'll have a split at each possible boundary.
  size_t len = strlen(unicode_utf8);
  char buffer[arraysize(unicode_utf8) + 4];
  for (size_t i = 1; i < len; i++) {
    // Copy source string into buffer, splitting it at i.
    // Then add three chunks, 0..i-1, i..strlen-1, empty.
    memcpy(buffer, unicode_utf8, i);
    memcpy(buffer + i + 1, unicode_utf8 + i, len - i);
    buffer[i] = '\0';
    buffer[len + 1] = '\n';
    buffer[len + 2] = '\0';
    buffer[len + 3] = '\0';
    const char* chunks[] = {buffer, buffer + i + 1, buffer + len + 2};

    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    int32_t res = stream->AdvanceUntil(
        [](int32_t c0_) { return unibrow::IsLineTerminator(c0_); });
    CHECK_EQ(buffer[len + 1], res);
  }
}

TEST_F(ScannerStreamsTest, Utf8ChunkBoundaries) {
  // Test utf-8 parsing at chunk boundaries.

  // Split the test string at each byte and pass it to the stream. This way,
  // we'll have a split at each possible boundary.
  size_t len = strlen(unicode_utf8);
  char buffer[arraysize(unicode_utf8) + 3];
  for (size_t i = 1; i < len; i++) {
    // Copy source string into buffer, splitting it at i.
    // Then add three chunks, 0..i-1, i..strlen-1, empty.
    memcpy(buffer, unicode_utf8, i);
    memcpy(buffer + i + 1, unicode_utf8 + i, len - i);
    buffer[i] = '\0';
    buffer[len + 1] = '\0';
    buffer[len + 2] = '\0';
    const char* chunks[] = {buffer, buffer + i + 1, buffer + len + 2};

    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    for (size_t j = 0; unicode_ucs2[j]; j++) {
      CHECK_EQ(unicode_ucs2[j], stream->Advance());
    }
    CHECK_EQ(v8::internal::Utf16CharacterStream::kEndOfInput,
             stream->Advance());
  }
}

TEST_F(ScannerStreamsTest, Utf8SingleByteChunks) {
  // Have each byte as a single-byte chunk.
  size_t len = strlen(unicode_utf8);
  char buffer[arraysize(unicode_utf8) + 4];
  for (size_t i = 1; i < len - 1; i++) {
    // Copy source string into buffer, make a single-byte chunk at i.
    memcpy(buffer, unicode_utf8, i);
    memcpy(buffer + i + 3, unicode_utf8 + i + 1, len - i - 1);
    buffer[i] = '\0';
    buffer[i + 1] = unicode_utf8[i];
    buffer[i + 2] = '\0';
    buffer[len + 2] = '\0';
    buffer[len + 3] = '\0';
    const char* chunks[] = {buffer, buffer + i + 1, buffer + i + 3,
                            buffer + len + 3};

    ChunkSource chunk_source(chunks);
    std::unique_ptr<v8::internal::Utf16CharacterStream> stream(
        v8::internal::ScannerStream::For(
            &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));

    for (size_t j = 0; unicode_ucs2[j]; j++) {
      CHECK_EQ(unicode_ucs2[j], stream->Advance());
    }
    CHECK_EQ(v8::internal::Utf16CharacterStream::kEndOfInput,
             stream->Advance());
  }
}

#define CHECK_EQU(v1, v2) CHECK_EQ(static_cast<int>(v1), static_cast<int>(v2))

void TestCharacterStream(const char* reference, i::Utf16CharacterStream* stream,
                         unsigned length, unsigned start, unsigned end) {
  // Read streams one char at a time
  unsigned i;
  for (i = start; i < end; i++) {
    CHECK_EQU(i, stream->pos());
    CHECK_EQU(reference[i], stream->Advance());
  }
  CHECK_EQU(end, stream->pos());
  CHECK_EQU(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
  CHECK_EQU(end + 1, stream->pos());
  stream->Back();

  // Pushback, re-read, pushback again.
  while (i > end / 4) {
    int32_t c0 = reference[i - 1];
    CHECK_EQU(i, stream->pos());
    stream->Back();
    i--;
    CHECK_EQU(i, stream->pos());
    int32_t c1 = stream->Advance();
    i++;
    CHECK_EQU(i, stream->pos());
    CHECK_EQ(c0, c1);
    stream->Back();
    i--;
    CHECK_EQU(i, stream->pos());
  }

  // Seek + read streams one char at a time.
  unsigned halfway = end / 2;
  stream->Seek(stream->pos() + halfway - i);
  for (i = halfway; i < end; i++) {
    CHECK_EQU(i, stream->pos());
    CHECK_EQU(reference[i], stream->Advance());
  }
  CHECK_EQU(i, stream->pos());
  CHECK(i::Scanner::IsInvalid(stream->Advance()));

  // Seek back, then seek beyond end of stream.
  stream->Seek(start);
  if (start < length) {
    CHECK_EQU(stream->Advance(), reference[start]);
  } else {
    CHECK(i::Scanner::IsInvalid(stream->Advance()));
  }
  stream->Seek(length + 5);
  CHECK(i::Scanner::IsInvalid(stream->Advance()));
}

void TestCloneCharacterStream(const char* reference,
                              i::Utf16CharacterStream* stream,
                              unsigned length) {
  // Test original stream through to the end.
  TestCharacterStream(reference, stream, length, 0, length);

  // Clone the stream after it completes.
  std::unique_ptr<i::Utf16CharacterStream> clone = stream->Clone();

  // Test that the clone through to the end.
  TestCharacterStream(reference, clone.get(), length, 0, length);

  // Rewind original stream to a third.
  stream->Seek(length / 3);

  // Rewind clone stream to two thirds.
  clone->Seek(2 * length / 3);

  // Test seeking clone didn't affect original stream.
  TestCharacterStream(reference, stream, length, length / 3, length);

  // Test seeking original stream didn't affect clone.
  TestCharacterStream(reference, clone.get(), length, 2 * length / 3, length);
}

#undef CHECK_EQU

void TestCharacterStreams(const char* one_byte_source, unsigned length,
                          unsigned start = 0, unsigned end = 0) {
  if (end == 0) end = length;

  i::Isolate* isolate =
      reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());
  i::Factory* factory = isolate->factory();

  // 2-byte external string
  std::unique_ptr<v8::base::uc16[]> uc16_buffer(new v8::base::uc16[length]);
  v8::base::Vector<const v8::base::uc16> two_byte_vector(
      uc16_buffer.get(), static_cast<int>(length));
  {
    for (unsigned i = 0; i < length; i++) {
      uc16_buffer[i] = static_cast<v8::base::uc16>(one_byte_source[i]);
    }
    TestExternalResource resource(uc16_buffer.get(), length);
    i::Handle<i::String> uc16_string(
        NewExternalTwoByteStringFromResource(isolate, &resource));
    std::unique_ptr<i::Utf16CharacterStream> uc16_stream(
        i::ScannerStream::For(isolate, uc16_string, start, end));
    TestCharacterStream(one_byte_source, uc16_stream.get(), length, start, end);

    // This avoids the GC from trying to free a stack allocated resource.
    if (IsExternalString(*uc16_string))
      i::Cast<i::ExternalTwoByteString>(uc16_string)
          ->SetResource(isolate, nullptr);
  }

  // 1-byte external string
  v8::base::Vector<const uint8_t> one_byte_vector =
      v8::base::OneByteVector(one_byte_source, static_cast<int>(length));
  i::Handle<i::String> one_byte_string =
      factory->NewStringFromOneByte(one_byte_vector).ToHandleChecked();
  {
    TestExternalOneByteResource one_byte_resource(one_byte_source, length);
    i::Handle<i::String> ext_one_byte_string(
        factory->NewExternalStringFromOneByte(&one_byte_resource)
            .ToHandleChecked());
    std::unique_ptr<i::Utf16CharacterStream> one_byte_stream(
        i::ScannerStream::For(isolate, ext_one_byte_string, start, end));
    TestCharacterStream(one_byte_source, one_byte_stream.get(), length, start,
                        end);
    // This avoids the GC from trying to free a stack allocated resource.
    if (IsExternalString(*ext_one_byte_string))
      i::Cast<i::ExternalOneByteString>(ext_one_byte_string)
          ->SetResource(isolate, nullptr);
  }

  // 1-byte generic i::String
  {
    std::unique_ptr<i::Utf16CharacterStream> string_stream(
        i::ScannerStream::For(isolate, one_byte_string, start, end));
    TestCharacterStream(one_byte_source, string_stream.get(), length, start,
                        end);
  }

  // 2-byte generic i::String
  {
    i::Handle<i::String> two_byte_string =
        factory->NewStringFromTwoByte(two_byte_vector).ToHandleChecked();
    std::unique_ptr<i::Utf16CharacterStream> two_byte_string_stream(
        i::ScannerStream::For(isolate, two_byte_string, start, end));
    TestCharacterStream(one_byte_source, two_byte_string_stream.get(), length,
                        start, end);
  }

  // Streaming has no notion of start/end, so let's skip streaming tests for
  // these cases.
  if (start != 0 || end != length) return;

  // 1-byte streaming stream, single + many chunks.
  {
    const uint8_t* data = one_byte_vector.begin();
    const uint8_t* data_end = one_byte_vector.end();

    ChunkSource single_chunk(data, 1, data_end - data, false);
    std::unique_ptr<i::Utf16CharacterStream> one_byte_streaming_stream(
        i::ScannerStream::For(&single_chunk,
                              v8::ScriptCompiler::StreamedSource::ONE_BYTE));
    TestCharacterStream(one_byte_source, one_byte_streaming_stream.get(),
                        length, start, end);

    ChunkSource many_chunks(data, 1, data_end - data, true);
    one_byte_streaming_stream.reset(i::ScannerStream::For(
        &many_chunks, v8::ScriptCompiler::StreamedSource::ONE_BYTE));
    TestCharacterStream(one_byte_source, one_byte_streaming_stream.get(),
                        length, start, end);
  }

  // UTF-8 streaming stream, single + many chunks.
  {
    const uint8_t* data = one_byte_vector.begin();
    const uint8_t* data_end = one_byte_vector.end();
    ChunkSource chunks(data, 1, data_end - data, false);
    std::unique_ptr<i::Utf16CharacterStream> utf8_streaming_stream(
        i::ScannerStream::For(&chunks,
                              v8::ScriptCompiler::StreamedSource::UTF8));
    TestCharacterStream(one_byte_source, utf8_streaming_stream.get(), length,
                        start, end);

    ChunkSource many_chunks(data, 1, data_end - data, true);
    utf8_streaming_stream.reset(i::ScannerStream::For(
        &many_chunks, v8::ScriptCompiler::StreamedSource::UTF8));
    TestCharacterStream(one_byte_source, utf8_streaming_stream.get(), length,
                        start, end);
  }

  // 2-byte streaming stream, single + many chunks.
  {
    const uint8_t* data =
        reinterpret_cast<const uint8_t*>(two_byte_vector.begin());
    const uint8_t* data_end =
        reinterpret_cast<const uint8_t*>(two_byte_vector.end());
    ChunkSource chunks(data, 2, data_end - data, false);
    std::unique_ptr<i::Utf16CharacterStream> two_byte_streaming_stream(
        i::ScannerStream::For(&chunks,
                              v8::ScriptCompiler::StreamedSource::TWO_BYTE));
    TestCharacterStream(one_byte_source, two_byte_streaming_stream.get(),
                        length, start, end);

    ChunkSource many_chunks(data, 2, data_end - data, true);
    two_byte_streaming_stream.reset(i::ScannerStream::For(
        &many_chunks, v8::ScriptCompiler::StreamedSource::TWO_BYTE));
    TestCharacterStream(one_byte_source, two_byte_streaming_stream.get(),
                        length, start, end);
  }
}

TEST_F(ScannerStreamsTest, CharacterStreams) {
  v8::HandleScope handles(isolate());
  v8::Local<v8::Context> context = v8::Context::New(isolate());
  v8::Context::Scope context_scope(context);

  TestCharacterStreams("abcdefghi", 9);
  TestCharacterStreams("abc\0\n\r\x7f", 7);
  TestCharacterStreams("\0", 1);
  TestCharacterStreams("", 0);

  // 4k large buffer.
  char buffer[4096 + 1];
  for (unsigned i = 0; i < arraysize(buffer); i++) {
    buffer[i] = static_cast<char>(i & 0x7F);
  }
  buffer[arraysize(buffer) - 1] = '\0';
  TestCharacterStreams(buffer, arraysize(buffer) - 1);
  TestCharacterStreams(buffer, arraysize(buffer) - 1, 576, 3298);
}

// Regression test for crbug.com/651333. Read invalid utf-8.
TEST_F(ScannerStreamsTest, Regress651333) {
  const uint8_t bytes[] =
      "A\xf1"
      "ad";  // Anad, with n == n-with-tilde.
  const uint16_t unicode[] = {65, 65533, 97, 100};

  // Run the test for all sub-strings 0..N of bytes, to make sure we hit the
  // error condition in and at chunk boundaries.
  for (size_t len = 0; len < arraysize(bytes); len++) {
    // Read len bytes from bytes, and compare against the expected unicode
    // characters. Expect kBadChar ( == Unicode replacement char == code point
    // 65533) instead of the incorrectly coded Latin1 char.
    ChunkSource chunks(bytes, 1, len, false);
    std::unique_ptr<i::Utf16CharacterStream> stream(i::ScannerStream::For(
        &chunks, v8::ScriptCompiler::StreamedSource::UTF8));
    for (size_t i = 0; i < len; i++) {
      CHECK_EQ(unicode[i], stream->Advance());
    }
    CHECK_EQ(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
  }
}

void TestChunkStreamAgainstReference(
    const char* cases[],
    const std::vector<std::vector<uint16_t>>& unicode_expected) {
  for (size_t c = 0; c < unicode_expected.size(); ++c) {
    ChunkSource chunk_source(cases[c]);
    std::unique_ptr<i::Utf16CharacterStream> stream(i::ScannerStream::For(
        &chunk_source, v8::ScriptCompiler::StreamedSource::UTF8));
    for (size_t i = 0; i < unicode_expected[c].size(); i++) {
      CHECK_EQ(unicode_expected[c][i], stream->Advance());
    }
    CHECK_EQ(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
    stream->Seek(0);
    for (size_t i = 0; i < unicode_expected[c].size(); i++) {
      CHECK_EQ(unicode_expected[c][i], stream->Advance());
    }
    CHECK_EQ(i::Utf16CharacterStream::kEndOfInput, stream->Advance());
  }
}

TEST_F(ScannerStreamsTest, Regress6377) {
  const char* cases[] = {
      "\xf0\x90\0"  // first chunk - start of 4-byte seq
      "\x80\x80"    // second chunk - end of 4-byte seq
      "a\0",        // and an 'a'

      "\xe0\xbf\0"  // first chunk - start of 3-byte seq
      "\xbf"        // second chunk - one-byte end of 3-byte seq
      "a\0",        // and an 'a'

      "\xc3\0"  // first chunk - start of 2-byte seq
      "\xbf"    // second chunk - end of 2-byte seq
      "a\0",    // and an 'a'

      "\xf0\x90\x80\0"  // first chunk - start of 4-byte seq
      "\x80"            // second chunk - one-byte end of 4-byte seq
      "a\xc3\0"         // and an 'a' + start of 2-byte seq
      "\xbf\0",         // third chunk - end of 2-byte seq
  };
  const std::vector<std::vector<uint16_t>> unicode_expected = {
      {0xD800, 0xDC00, 97},
      {0xFFF, 97},
      {0xFF, 97},
      {0xD800, 0xDC00, 97, 0xFF},
  };
  CHECK_EQ(unicode_expected.size(), arraysize(cases));
  TestChunkStreamAgainstReference(cases, unicode_expected);
}

TEST_F(ScannerStreamsTest, Regress6836) {
  const char* cases[] = {
      // 0xC2 is a lead byte, but there's no continuation. The bug occurs when
      // this happens near the chunk end.
      "X\xc2Y\0",
      // Last chunk ends with a 2-byte char lead.
      "X\xc2\0",
      // Last chunk ends with a 3-byte char lead and only one continuation
      // character.
      "X\xe0\xbf\0",
  };
  const std::vector<std::vector<uint16_t>> unicode_expected = {
      {0x58, 0xFFFD, 0x59},
      {0x58, 0xFFFD},
      {0x58, 0xFFFD},
  };
  CHECK_EQ(unicode_expected.size(), arraysize(cases));
  TestChunkStreamAgainstReference(cases, unicode_expected);
}

TEST_F(ScannerStreamsTest, TestOverlongAndInvalidSequences) {
  const char* cases[] = {
      // Overlong 2-byte sequence.
      "X\xc0\xbfY\0",
      // Another overlong 2-byte sequence.
      "X\xc1\xbfY\0",
      // Overlong 3-byte sequence.
      "X\xe0\x9f\xbfY\0",
      // Overlong 4-byte sequence.
      "X\xf0\x89\xbf\xbfY\0",
      // Invalid 3-byte sequence (reserved for surrogates).
      "X\xed\xa0\x80Y\0",
      // Invalid 4-bytes sequence (value out of range).
      "X\xf4\x90\x80\x80Y\0",
  };
  const std::vector<std::vector<uint16_t>> unicode_expected = {
      {0x58, 0xFFFD, 0xFFFD, 0x59},
      {0x58, 0xFFFD, 0xFFFD, 0x59},
      {0x58, 0xFFFD, 0xFFFD, 0xFFFD, 0x59},
      {0x58, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0x59},
      {0x58, 0xFFFD, 0xFFFD, 0xFFFD, 0x59},
      {0x58, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0x59},
  };
  CHECK_EQ(unicode_expected.size(), arraysize(cases));
  TestChunkStreamAgainstReference(cases, unicode_expected);
}

TEST_F(ScannerStreamsTest, RelocatingCharacterStream) {
  // This test relies on the invariant that GC will move objects.
  if (i::v8_flags.single_generation) return;
  i::v8_flags.manual_evacuation_candidates_selection = true;
  v8::internal::ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  const char* string = "abcd";
  int length = static_cast<int>(strlen(string));
  std::unique_ptr<v8::base::uc16[]> uc16_buffer(new v8::base::uc16[length]);
  for (int i = 0; i < length; i++) {
    uc16_buffer[i] = string[i];
  }
  v8::base::Vector<const v8::base::uc16> two_byte_vector(uc16_buffer.get(),
                                                         length);
  i::Handle<i::String> two_byte_string =
      i_isolate()
          ->factory()
          ->NewStringFromTwoByte(two_byte_vector, i::AllocationType::kYoung)
          .ToHandleChecked();
  std::unique_ptr<i::Utf16CharacterStream> two_byte_string_stream(
      i::ScannerStream::For(i_isolate(), two_byte_string, 0, length));
  CHECK_EQ('a', two_byte_string_stream->Advance());
  CHECK_EQ('b', two_byte_string_stream->Advance());
  CHECK_EQ(size_t{2}, two_byte_string_stream->pos());
  i::Tagged<i::String> raw = *two_byte_string;
  // We need to invoke GC without stack, otherwise no compaction is performed.
  i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate()->heap());
  // 1st GC moves `two_byte_string` to old space and 2nd GC evacuates it within
  // old space.
  InvokeMajorGC();
  i::MemoryChunk::FromHeapObject(*two_byte_string)
      ->SetFlagNonExecutable(
          i::MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
  InvokeMajorGC();
  // GC moved the string.
  CHECK_NE(raw, *two_byte_string);
  CHECK_EQ('c', two_byte_string_stream->Advance());
  CHECK_EQ('d', two_byte_string_stream->Advance());
}

TEST_F(ScannerStreamsTest, RelocatingUnbufferedCharacterStream) {
  // This test relies on the invariant that GC will move objects.
  if (i::v8_flags.single_generation) return;
  i::v8_flags.manual_evacuation_candidates_selection = true;
  v8::internal::ManualGCScope manual_gc_scope(i_isolate());
  v8::HandleScope scope(isolate());

  const char16_t* string = u"abc\u2603";
  int length = static_cast<int>(std::char_traits<char16_t>::length(string));
  std::unique_ptr<v8::base::uc16[]> uc16_buffer(new v8::base::uc16[length]);
  for (int i = 0; i < length; i++) {
    uc16_buffer[i] = string[i];
  }
  v8::base::Vector<const v8::base::uc16> two_byte_vector(uc16_buffer.get(),
                                                         length);
  i::Handle<i::String> two_byte_string =
      i_isolate()
          ->factory()
          ->NewStringFromTwoByte(two_byte_vector, i::AllocationType::kYoung)
          .ToHandleChecked();
  std::unique_ptr<i::Utf16CharacterStream> two_byte_string_stream(
      i::ScannerStream::For(i_isolate(), two_byte_string, 0, length));

  // Seek to offset 2 so that the buffer_pos_ is not zero initially.
  two_byte_string_stream->Seek(2);
  CHECK_EQ('c', two_byte_string_stream->Advance());
  CHECK_EQ(size_t{3}, two_byte_string_stream->pos());

  i::Tagged<i::String> raw = *two_byte_string;
  // We need to invoke GC without stack, otherwise no compaction is performed.
  i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(
      i_isolate()->heap());
  // 1st GC moves `two_byte_string` to old space and 2nd GC evacuates it within
  // old space.
  InvokeMajorGC();
  i::MemoryChunk::FromHeapObject(*two_byte_string)
      ->SetFlagNonExecutable(
          i::MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
  InvokeMajorGC();
  // GC moved the string and buffer was updated to the correct location.
  CHECK_NE(raw, *two_byte_string);

  // Check that we correctly moved based on buffer_pos_, not based on a position
  // of zero.
  CHECK_EQ(u'\u2603', two_byte_string_stream->Advance());
  CHECK_EQ(size_t{4}, two_byte_string_stream->pos());
}

TEST_F(ScannerStreamsTest, CloneCharacterStreams) {
  v8::HandleScope handles(isolate());
  v8::Local<v8::Context> context = v8::Context::New(isolate());
  v8::Context::Scope context_scope(context);

  i::Factory* factory = i_isolate()->factory();

  const char* one_byte_source = "abcdefghi";
  unsigned length = static_cast<unsigned>(strlen(one_byte_source));

  // Check that cloning a character stream does not update

  // 2-byte external string
  std::unique_ptr<v8::base::uc16[]> uc16_buffer(new v8::base::uc16[length]);
  v8::base::Vector<const v8::base::uc16> two_byte_vector(
      uc16_buffer.get(), static_cast<int>(length));
  {
    for (unsigned i = 0; i < length; i++) {
      uc16_buffer[i] = static_cast<v8::base::uc16>(one_byte_source[i]);
    }
    TestExternalResource resource(uc16_buffer.get(), length);
    i::Handle<i::String> uc16_string(
        NewExternalTwoByteStringFromResource(i_isolate(), &resource));
    std::unique_ptr<i::Utf16CharacterStream> uc16_stream(
        i::ScannerStream::For(i_isolate(), uc16_string, 0, length));

    CHECK(resource.IsLocked());
    CHECK_EQ(1, resource.LockDepth());
    std::unique_ptr<i::Utf16CharacterStream> cloned = uc16_stream->Clone();
    CHECK_EQ(2, resource.LockDepth());
    uc16_stream = std::move(cloned);
    CHECK_EQ(1, resource.LockDepth());

    TestCloneCharacterStream(one_byte_source, uc16_stream.get(), length);

    // This avoids the GC from trying to free a stack allocated resource.
    if (IsExternalString(*uc16_string))
      i::Cast<i::ExternalTwoByteString>(uc16_string)
          ->SetResource(i_isolate(), nullptr);
  }

  // 1-byte external string
  v8::base::Vector<const uint8_t> one_byte_vector =
      v8::base::OneByteVector(one_byte_source, static_cast<int>(length));
  i::Handle<i::String> one_byte_string =
      factory->NewStringFromOneByte(one_byte_vector).ToHandleChecked();
  {
    TestExternalOneByteResource one_byte_resource(one_byte_source, length);
    i::Handle<i::String> ext_one_byte_string(
        factory->NewExternalStringFromOneByte(&one_byte_resource)
            .ToHandleChecked());
    std::unique_ptr<i::Utf16CharacterStream> one_byte_stream(
        i::ScannerStream::For(i_isolate(), ext_one_byte_string, 0, length));
    TestCloneCharacterStream(one_byte_source, one_byte_stream.get(), length);
    // This avoids the GC from trying to free a stack allocated resource.
    if (IsExternalString(*ext_one_byte_string))
      i::Cast<i::ExternalOneByteString>(ext_one_byte_string)
          ->SetResource(i_isolate(), nullptr);
  }

  // Relocatable streams are't clonable.
  {
    std::unique_ptr<i::Utf16CharacterStream> string_stream(
        i::ScannerStream::For(i_isolate(), one_byte_string, 0, length));
    CHECK(!string_stream->can_be_cloned());

    i::Handle<i::String> two_byte_string =
        factory->NewStringFromTwoByte(two_byte_vector).ToHandleChecked();
    std::unique_ptr<i::Utf16CharacterStream> two_byte_string_stream(
        i::ScannerStream::For(i_isolate(), two_byte_string, 0, length));
    CHECK(!two_byte_string_stream->can_be_cloned());
  }

  // Chunk sources are cloneable.
  {
    const char* chunks[] = {"1234", "5678", ""};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<i::Utf16CharacterStream> one_byte_streaming_stream(
        i::ScannerStream::For(&chunk_source,
                              v8::ScriptCompiler::StreamedSource::ONE_BYTE));
    TestCloneCharacterStream("12345678", one_byte_streaming_stream.get(), 8);
  }
  {
    const char* chunks[] = {"1234", "5678", ""};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<i::Utf16CharacterStream> utf8_streaming_stream(
        i::ScannerStream::For(&chunk_source,
                              v8::ScriptCompiler::StreamedSource::UTF8));
    CHECK(utf8_streaming_stream->can_be_cloned());
    TestCloneCharacterStream("12345678", utf8_streaming_stream.get(), 8);
  }
  {
    const char16_t* chunks[] = {u"1234", u"5678", u""};
    ChunkSource chunk_source(chunks);
    std::unique_ptr<i::Utf16CharacterStream> two_byte_streaming_stream(
        i::ScannerStream::For(&chunk_source,
                              v8::ScriptCompiler::StreamedSource::TWO_BYTE));
    CHECK(two_byte_streaming_stream->can_be_cloned());
    TestCloneCharacterStream("12345678", two_byte_streaming_stream.get(), 8);
  }
}
