// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <stack>

#include "cbor.h"
#include "json_platform.h"

namespace v8_crdtp {
namespace json {
// =============================================================================
// json::NewJSONEncoder - for encoding streaming parser events as JSON
// =============================================================================

namespace {
// Prints |value| to |out| with 4 hex digits, most significant chunk first.
template <typename C>
void PrintHex(uint16_t value, C* out) {
  for (int ii = 3; ii >= 0; --ii) {
    int four_bits = 0xf & (value >> (4 * ii));
    out->push_back(four_bits + ((four_bits <= 9) ? '0' : ('a' - 10)));
  }
}

// In the writer below, we maintain a stack of State instances.
// It is just enough to emit the appropriate delimiters and brackets
// in JSON.
enum class Container {
  // Used for the top-level, initial state.
  NONE,
  // Inside a JSON object.
  MAP,
  // Inside a JSON array.
  ARRAY
};

class State {
 public:
  explicit State(Container container) : container_(container) {}
  void StartElement(std::vector<uint8_t>* out) { StartElementTmpl(out); }
  void StartElement(std::string* out) { StartElementTmpl(out); }
  Container container() const { return container_; }

 private:
  template <typename C>
  void StartElementTmpl(C* out) {
    assert(container_ != Container::NONE || size_ == 0);
    if (size_ != 0) {
      char delim = (!(size_ & 1) || container_ == Container::ARRAY) ? ',' : ':';
      out->push_back(delim);
    }
    ++size_;
  }

  Container container_ = Container::NONE;
  int size_ = 0;
};

constexpr char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz0123456789+/";

template <typename C>
void Base64Encode(const span<uint8_t>& in, C* out) {
  // The following three cases are based on the tables in the example
  // section in https://en.wikipedia.org/wiki/Base64. We process three
  // input bytes at a time, emitting 4 output bytes at a time.
  size_t ii = 0;

  // While possible, process three input bytes.
  for (; ii + 3 <= in.size(); ii += 3) {
    uint32_t twentyfour_bits = (in[ii] << 16) | (in[ii + 1] << 8) | in[ii + 2];
    out->push_back(kBase64Table[(twentyfour_bits >> 18)]);
    out->push_back(kBase64Table[(twentyfour_bits >> 12) & 0x3f]);
    out->push_back(kBase64Table[(twentyfour_bits >> 6) & 0x3f]);
    out->push_back(kBase64Table[twentyfour_bits & 0x3f]);
  }
  if (ii + 2 <= in.size()) {  // Process two input bytes.
    uint32_t twentyfour_bits = (in[ii] << 16) | (in[ii + 1] << 8);
    out->push_back(kBase64Table[(twentyfour_bits >> 18)]);
    out->push_back(kBase64Table[(twentyfour_bits >> 12) & 0x3f]);
    out->push_back(kBase64Table[(twentyfour_bits >> 6) & 0x3f]);
    out->push_back('=');  // Emit padding.
    return;
  }
  if (ii + 1 <= in.size()) {  // Process a single input byte.
    uint32_t twentyfour_bits = (in[ii] << 16);
    out->push_back(kBase64Table[(twentyfour_bits >> 18)]);
    out->push_back(kBase64Table[(twentyfour_bits >> 12) & 0x3f]);
    out->push_back('=');  // Emit padding.
    out->push_back('=');  // Emit padding.
  }
}

// Implements a handler for JSON parser events to emit a JSON string.
template <typename C>
class JSONEncoder : public ParserHandler {
 public:
  JSONEncoder(C* out, Status* status) : out_(out), status_(status) {
    *status_ = Status();
    state_.emplace(Container::NONE);
  }

  void HandleMapBegin() override {
    if (!status_->ok())
      return;
    assert(!state_.empty());
    state_.top().StartElement(out_);
    state_.emplace(Container::MAP);
    Emit('{');
  }

  void HandleMapEnd() override {
    if (!status_->ok())
      return;
    assert(state_.size() >= 2 && state_.top().container() == Container::MAP);
    state_.pop();
    Emit('}');
  }

  void HandleArrayBegin() override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    state_.emplace(Container::ARRAY);
    Emit('[');
  }

  void HandleArrayEnd() override {
    if (!status_->ok())
      return;
    assert(state_.size() >= 2 && state_.top().container() == Container::ARRAY);
    state_.pop();
    Emit(']');
  }

  void HandleString16(span<uint16_t> chars) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    Emit('"');
    for (const uint16_t ch : chars) {
      if (ch == '"') {
        Emit('\\');
        Emit('"');
      } else if (ch == '\\') {
        Emit('\\');
        Emit('\\');
      } else if (ch >= 32 && ch <= 127) {
        Emit(ch);
      } else if (ch == '\n') {
        Emit('\\');
        Emit('n');
      } else if (ch == '\r') {
        Emit('\\');
        Emit('r');
      } else if (ch == '\t') {
        Emit('\\');
        Emit('t');
      } else if (ch == '\b') {
        Emit('\\');
        Emit('b');
      } else if (ch == '\f') {
        Emit('\\');
        Emit('f');
      } else {
        Emit('\\');
        Emit('u');
        PrintHex(ch, out_);
      }
    }
    Emit('"');
  }

  void HandleString8(span<uint8_t> chars) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    Emit('"');
    for (size_t ii = 0; ii < chars.size(); ++ii) {
      uint8_t c = chars[ii];
      if (c == '"') {
        Emit('\\');
        Emit('"');
      } else if (c == '\\') {
        Emit('\\');
        Emit('\\');
      } else if (c >= 32 && c <= 127) {
        Emit(c);
      } else if (c == '\n') {
        Emit('\\');
        Emit('n');
      } else if (c == '\r') {
        Emit('\\');
        Emit('r');
      } else if (c == '\t') {
        Emit('\\');
        Emit('t');
      } else if (c == '\b') {
        Emit('\\');
        Emit('b');
      } else if (c == '\f') {
        Emit('\\');
        Emit('f');
      } else if (c < 32) {
        Emit('\\');
        Emit('u');
        PrintHex(static_cast<uint16_t>(c), out_);
      } else {
        // Inspect the leading byte to figure out how long the utf8
        // byte sequence is; while doing this initialize |codepoint|
        // with the first few bits.
        // See table in: https://en.wikipedia.org/wiki/UTF-8
        // byte one is 110x xxxx -> 2 byte utf8 sequence
        // byte one is 1110 xxxx -> 3 byte utf8 sequence
        // byte one is 1111 0xxx -> 4 byte utf8 sequence
        uint32_t codepoint;
        int num_bytes_left;
        if ((c & 0xe0) == 0xc0) {  // 2 byte utf8 sequence
          num_bytes_left = 1;
          codepoint = c & 0x1f;
        } else if ((c & 0xf0) == 0xe0) {  // 3 byte utf8 sequence
          num_bytes_left = 2;
          codepoint = c & 0x0f;
        } else if ((c & 0xf8) == 0xf0) {  // 4 byte utf8 sequence
          codepoint = c & 0x07;
          num_bytes_left = 3;
        } else {
          continue;  // invalid leading byte
        }

        // If we have enough bytes in our input, decode the remaining ones
        // belonging to this Unicode character into |codepoint|.
        if (ii + num_bytes_left >= chars.size())
          continue;
        bool invalid_byte_seen = false;
        while (num_bytes_left > 0) {
          c = chars[++ii];
          --num_bytes_left;
          // Check the next byte is a continuation byte, that is 10xx xxxx.
          if ((c & 0xc0) != 0x80)
            invalid_byte_seen = true;
          codepoint = (codepoint << 6) | (c & 0x3f);
        }
        if (invalid_byte_seen)
          continue;

        // Disallow overlong encodings for ascii characters, as these
        // would include " and other characters significant to JSON
        // string termination / control.
        if (codepoint <= 0x7f)
          continue;
        // Invalid in UTF8, and can't be represented in UTF16 anyway.
        if (codepoint > 0x10ffff)
          continue;

        // So, now we transcode to UTF16,
        // using the math described at https://en.wikipedia.org/wiki/UTF-16,
        // for either one or two 16 bit characters.
        if (codepoint <= 0xffff) {
          Emit("\\u");
          PrintHex(static_cast<uint16_t>(codepoint), out_);
          continue;
        }
        codepoint -= 0x10000;
        // high surrogate
        Emit("\\u");
        PrintHex(static_cast<uint16_t>((codepoint >> 10) + 0xd800), out_);
        // low surrogate
        Emit("\\u");
        PrintHex(static_cast<uint16_t>((codepoint & 0x3ff) + 0xdc00), out_);
      }
    }
    Emit('"');
  }

  void HandleBinary(span<uint8_t> bytes) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    Emit('"');
    Base64Encode(bytes, out_);
    Emit('"');
  }

  void HandleDouble(double value) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    // JSON cannot represent NaN or Infinity. So, for compatibility,
    // we behave like the JSON object in web browsers: emit 'null'.
    if (!std::isfinite(value)) {
      Emit("null");
      return;
    }
    // If |value| is a scalar, emit it as an int. Taken from json_writer.cc in
    // Chromium.
    if (value < static_cast<double>(std::numeric_limits<int64_t>::max()) &&
        value >= std::numeric_limits<int64_t>::min() &&
        std::floor(value) == value) {
      Emit(std::to_string(static_cast<int64_t>(value)));
      return;
    }
    std::string str_value = json::platform::DToStr(value);
    // The following is somewhat paranoid, but also taken from json_writer.cc
    // in Chromium:
    // Ensure that the number has a .0 if there's no decimal or 'e'.  This
    // makes sure that when we read the JSON back, it's interpreted as a
    // real rather than an int.
    if (str_value.find_first_of(".eE") == std::string::npos)
      str_value.append(".0");

    // DToStr may fail to emit a 0 before the decimal dot. E.g. this is
    // the case in base::NumberToString in Chromium (which is based on
    // dmg_fp). So, much like
    // https://cs.chromium.org/chromium/src/base/json/json_writer.cc
    // we probe for this and emit the leading 0 anyway if necessary.
    if (str_value[0] == '.') {
      Emit('0');
      Emit(str_value);
    } else if (str_value[0] == '-' && str_value[1] == '.') {
      Emit("-0");
      // Skip the '-' from the original string and emit the rest.
      out_->insert(out_->end(), str_value.begin() + 1, str_value.end());
    } else {
      Emit(str_value);
    }
  }

  void HandleInt32(int32_t value) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    Emit(std::to_string(value));
  }

  void HandleBool(bool value) override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    if (value)
      Emit("true");
    else
      Emit("false");
  }

  void HandleNull() override {
    if (!status_->ok())
      return;
    state_.top().StartElement(out_);
    Emit("null");
  }

  void HandleError(Status error) override {
    assert(!error.ok());
    *status_ = error;
    out_->clear();
  }

 private:
  inline void Emit(char c) { out_->push_back(c); }
  template <size_t N>
  inline void Emit(const char (&str)[N]) {
    out_->insert(out_->end(), str, str + N - 1);
  }
  inline void Emit(const std::string& str) {
    out_->insert(out_->end(), str.begin(), str.end());
  }

  C* out_;
  Status* status_;
  std::stack<State> state_;
};
}  // namespace

std::unique_ptr<ParserHandler> NewJSONEncoder(std::vector<uint8_t>* out,
                                              Status* status) {
  return std::unique_ptr<ParserHandler>(
      new JSONEncoder<std::vector<uint8_t>>(out, status));
}

std::unique_ptr<ParserHandler> NewJSONEncoder(std::string* out,
                                              Status* status) {
  return std::unique_ptr<ParserHandler>(
      new JSONEncoder<std::string>(out, status));
}

// =============================================================================
// json::ParseJSON - for receiving streaming parser events for JSON.
// =============================================================================

namespace {
const int kStackLimit = 300;

enum Token {
  ObjectBegin,
  ObjectEnd,
  ArrayBegin,
  ArrayEnd,
  StringLiteral,
  Number,
  BoolTrue,
  BoolFalse,
  NullToken,
  ListSeparator,
  ObjectPairSeparator,
  InvalidToken,
  NoInput
};

const char* const kNullString = "null";
const char* const kTrueString = "true";
const char* const kFalseString = "false";

template <typename Char>
class JsonParser {
 public:
  explicit JsonParser(ParserHandler* handler) : handler_(handler) {}

  void Parse(const Char* start, size_t length) {
    start_pos_ = start;
    const Char* end = start + length;
    const Char* tokenEnd = nullptr;
    ParseValue(start, end, &tokenEnd, 0);
    if (error_)
      return;
    if (tokenEnd != end) {
      HandleError(Error::JSON_PARSER_UNPROCESSED_INPUT_REMAINS, tokenEnd);
    }
  }

 private:
  bool CharsToDouble(const uint16_t* chars, size_t length, double* result) {
    std::string buffer;
    buffer.reserve(length + 1);
    for (size_t ii = 0; ii < length; ++ii) {
      bool is_ascii = !(chars[ii] & ~0x7F);
      if (!is_ascii)
        return false;
      buffer.push_back(static_cast<char>(chars[ii]));
    }
    return platform::StrToD(buffer.c_str(), result);
  }

  bool CharsToDouble(const uint8_t* chars, size_t length, double* result) {
    std::string buffer(reinterpret_cast<const char*>(chars), length);
    return platform::StrToD(buffer.c_str(), result);
  }

  static bool ParseConstToken(const Char* start,
                              const Char* end,
                              const Char** token_end,
                              const char* token) {
    // |token| is \0 terminated, it's one of the constants at top of the file.
    while (start < end && *token != '\0' && *start++ == *token++) {
    }
    if (*token != '\0')
      return false;
    *token_end = start;
    return true;
  }

  static bool ReadInt(const Char* start,
                      const Char* end,
                      const Char** token_end,
                      bool allow_leading_zeros) {
    if (start == end)
      return false;
    bool has_leading_zero = '0' == *start;
    int length = 0;
    while (start < end && '0' <= *start && *start <= '9') {
      ++start;
      ++length;
    }
    if (!length)
      return false;
    if (!allow_leading_zeros && length > 1 && has_leading_zero)
      return false;
    *token_end = start;
    return true;
  }

  static bool ParseNumberToken(const Char* start,
                               const Char* end,
                               const Char** token_end) {
    // We just grab the number here. We validate the size in DecodeNumber.
    // According to RFC4627, a valid number is: [minus] int [frac] [exp]
    if (start == end)
      return false;
    Char c = *start;
    if ('-' == c)
      ++start;

    if (!ReadInt(start, end, &start, /*allow_leading_zeros=*/false))
      return false;
    if (start == end) {
      *token_end = start;
      return true;
    }

    // Optional fraction part
    c = *start;
    if ('.' == c) {
      ++start;
      if (!ReadInt(start, end, &start, /*allow_leading_zeros=*/true))
        return false;
      if (start == end) {
        *token_end = start;
        return true;
      }
      c = *start;
    }

    // Optional exponent part
    if ('e' == c || 'E' == c) {
      ++start;
      if (start == end)
        return false;
      c = *start;
      if ('-' == c || '+' == c) {
        ++start;
        if (start == end)
          return false;
      }
      if (!ReadInt(start, end, &start, /*allow_leading_zeros=*/true))
        return false;
    }

    *token_end = start;
    return true;
  }

  static bool ReadHexDigits(const Char* start,
                            const Char* end,
                            const Char** token_end,
                            int digits) {
    if (end - start < digits)
      return false;
    for (int i = 0; i < digits; ++i) {
      Char c = *start++;
      if (!(('0' <= c && c <= '9') || ('a' <= c && c <= 'f') ||
            ('A' <= c && c <= 'F')))
        return false;
    }
    *token_end = start;
    return true;
  }

  static bool ParseStringToken(const Char* start,
                               const Char* end,
                               const Char** token_end) {
    while (start < end) {
      Char c = *start++;
      if ('\\' == c) {
        if (start == end)
          return false;
        c = *start++;
        // Make sure the escaped char is valid.
        switch (c) {
          case 'x':
            if (!ReadHexDigits(start, end, &start, 2))
              return false;
            break;
          case 'u':
            if (!ReadHexDigits(start, end, &start, 4))
              return false;
            break;
          case '\\':
          case '/':
          case 'b':
          case 'f':
          case 'n':
          case 'r':
          case 't':
          case 'v':
          case '"':
            break;
          default:
            return false;
        }
      } else if ('"' == c) {
        *token_end = start;
        return true;
      }
    }
    return false;
  }

  static bool SkipComment(const Char* start,
                          const Char* end,
                          const Char** comment_end) {
    if (start == end)
      return false;

    if (*start != '/' || start + 1 >= end)
      return false;
    ++start;

    if (*start == '/') {
      // Single line comment, read to newline.
      for (++start; start < end; ++start) {
        if (*start == '\n' || *start == '\r') {
          *comment_end = start + 1;
          return true;
        }
      }
      *comment_end = end;
      // Comment reaches end-of-input, which is fine.
      return true;
    }

    if (*start == '*') {
      Char previous = '\0';
      // Block comment, read until end marker.
      for (++start; start < end; previous = *start++) {
        if (previous == '*' && *start == '/') {
          *comment_end = start + 1;
          return true;
        }
      }
      // Block comment must close before end-of-input.
      return false;
    }

    return false;
  }

  static bool IsSpaceOrNewLine(Char c) {
    // \v = vertial tab; \f = form feed page break.
    return c == ' ' || c == '\n' || c == '\v' || c == '\f' || c == '\r' ||
           c == '\t';
  }

  static void SkipWhitespaceAndComments(const Char* start,
                                        const Char* end,
                                        const Char** whitespace_end) {
    while (start < end) {
      if (IsSpaceOrNewLine(*start)) {
        ++start;
      } else if (*start == '/') {
        const Char* comment_end = nullptr;
        if (!SkipComment(start, end, &comment_end))
          break;
        start = comment_end;
      } else {
        break;
      }
    }
    *whitespace_end = start;
  }

  static Token ParseToken(const Char* start,
                          const Char* end,
                          const Char** tokenStart,
                          const Char** token_end) {
    SkipWhitespaceAndComments(start, end, tokenStart);
    start = *tokenStart;

    if (start == end)
      return NoInput;

    switch (*start) {
      case 'n':
        if (ParseConstToken(start, end, token_end, kNullString))
          return NullToken;
        break;
      case 't':
        if (ParseConstToken(start, end, token_end, kTrueString))
          return BoolTrue;
        break;
      case 'f':
        if (ParseConstToken(start, end, token_end, kFalseString))
          return BoolFalse;
        break;
      case '[':
        *token_end = start + 1;
        return ArrayBegin;
      case ']':
        *token_end = start + 1;
        return ArrayEnd;
      case ',':
        *token_end = start + 1;
        return ListSeparator;
      case '{':
        *token_end = start + 1;
        return ObjectBegin;
      case '}':
        *token_end = start + 1;
        return ObjectEnd;
      case ':':
        *token_end = start + 1;
        return ObjectPairSeparator;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '-':
        if (ParseNumberToken(start, end, token_end))
          return Number;
        break;
      case '"':
        if (ParseStringToken(start + 1, end, token_end))
          return StringLiteral;
        break;
    }
    return InvalidToken;
  }

  static int HexToInt(Char c) {
    if ('0' <= c && c <= '9')
      return c - '0';
    if ('A' <= c && c <= 'F')
      return c - 'A' + 10;
    if ('a' <= c && c <= 'f')
      return c - 'a' + 10;
    assert(false);  // Unreachable.
    return 0;
  }

  static bool DecodeString(const Char* start,
                           const Char* end,
                           std::vector<uint16_t>* output) {
    if (start == end)
      return true;
    if (start > end)
      return false;
    output->reserve(end - start);
    while (start < end) {
      uint16_t c = *start++;
      // If the |Char| we're dealing with is really a byte, then
      // we have utf8 here, and we need to check for multibyte characters
      // and transcode them to utf16 (either one or two utf16 chars).
      if (sizeof(Char) == sizeof(uint8_t) && c > 0x7f) {
        // Inspect the leading byte to figure out how long the utf8
        // byte sequence is; while doing this initialize |codepoint|
        // with the first few bits.
        // See table in: https://en.wikipedia.org/wiki/UTF-8
        // byte one is 110x xxxx -> 2 byte utf8 sequence
        // byte one is 1110 xxxx -> 3 byte utf8 sequence
        // byte one is 1111 0xxx -> 4 byte utf8 sequence
        uint32_t codepoint;
        int num_bytes_left;
        if ((c & 0xe0) == 0xc0) {  // 2 byte utf8 sequence
          num_bytes_left = 1;
          codepoint = c & 0x1f;
        } else if ((c & 0xf0) == 0xe0) {  // 3 byte utf8 sequence
          num_bytes_left = 2;
          codepoint = c & 0x0f;
        } else if ((c & 0xf8) == 0xf0) {  // 4 byte utf8 sequence
          codepoint = c & 0x07;
          num_bytes_left = 3;
        } else {
          return false;  // invalid leading byte
        }

        // If we have enough bytes in our inpput, decode the remaining ones
        // belonging to this Unicode character into |codepoint|.
        if (start + num_bytes_left > end)
          return false;
        while (num_bytes_left > 0) {
          c = *start++;
          --num_bytes_left;
          // Check the next byte is a continuation byte, that is 10xx xxxx.
          if ((c & 0xc0) != 0x80)
            return false;
          codepoint = (codepoint << 6) | (c & 0x3f);
        }

        // Disallow overlong encodings for ascii characters, as these
        // would include " and other characters significant to JSON
        // string termination / control.
        if (codepoint <= 0x7f)
          return false;
        // Invalid in UTF8, and can't be represented in UTF16 anyway.
        if (codepoint > 0x10ffff)
          return false;

        // So, now we transcode to UTF16,
        // using the math described at https://en.wikipedia.org/wiki/UTF-16,
        // for either one or two 16 bit characters.
        if (codepoint <= 0xffff) {
          output->push_back(codepoint);
          continue;
        }
        codepoint -= 0x10000;
        output->push_back((codepoint >> 10) + 0xd800);    // high surrogate
        output->push_back((codepoint & 0x3ff) + 0xdc00);  // low surrogate
        continue;
      }
      if ('\\' != c) {
        output->push_back(c);
        continue;
      }
      if (start == end)
        return false;
      c = *start++;

      if (c == 'x') {
        // \x is not supported.
        return false;
      }

      switch (c) {
        case '"':
        case '/':
        case '\\':
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'v':
          c = '\v';
          break;
        case 'u':
          c = (HexToInt(*start) << 12) + (HexToInt(*(start + 1)) << 8) +
              (HexToInt(*(start + 2)) << 4) + HexToInt(*(start + 3));
          start += 4;
          break;
        default:
          return false;
      }
      output->push_back(c);
    }
    return true;
  }

  void ParseValue(const Char* start,
                  const Char* end,
                  const Char** value_token_end,
                  int depth) {
    if (depth > kStackLimit) {
      HandleError(Error::JSON_PARSER_STACK_LIMIT_EXCEEDED, start);
      return;
    }
    const Char* token_start = nullptr;
    const Char* token_end = nullptr;
    Token token = ParseToken(start, end, &token_start, &token_end);
    switch (token) {
      case NoInput:
        HandleError(Error::JSON_PARSER_NO_INPUT, token_start);
        return;
      case InvalidToken:
        HandleError(Error::JSON_PARSER_INVALID_TOKEN, token_start);
        return;
      case NullToken:
        handler_->HandleNull();
        break;
      case BoolTrue:
        handler_->HandleBool(true);
        break;
      case BoolFalse:
        handler_->HandleBool(false);
        break;
      case Number: {
        double value;
        if (!CharsToDouble(token_start, token_end - token_start, &value)) {
          HandleError(Error::JSON_PARSER_INVALID_NUMBER, token_start);
          return;
        }
        if (value >= std::numeric_limits<int32_t>::min() &&
            value <= std::numeric_limits<int32_t>::max() &&
            static_cast<int32_t>(value) == value)
          handler_->HandleInt32(static_cast<int32_t>(value));
        else
          handler_->HandleDouble(value);
        break;
      }
      case StringLiteral: {
        std::vector<uint16_t> value;
        bool ok = DecodeString(token_start + 1, token_end - 1, &value);
        if (!ok) {
          HandleError(Error::JSON_PARSER_INVALID_STRING, token_start);
          return;
        }
        handler_->HandleString16(span<uint16_t>(value.data(), value.size()));
        break;
      }
      case ArrayBegin: {
        handler_->HandleArrayBegin();
        start = token_end;
        token = ParseToken(start, end, &token_start, &token_end);
        while (token != ArrayEnd) {
          ParseValue(start, end, &token_end, depth + 1);
          if (error_)
            return;

          // After a list value, we expect a comma or the end of the list.
          start = token_end;
          token = ParseToken(start, end, &token_start, &token_end);
          if (token == ListSeparator) {
            start = token_end;
            token = ParseToken(start, end, &token_start, &token_end);
            if (token == ArrayEnd) {
              HandleError(Error::JSON_PARSER_UNEXPECTED_ARRAY_END, token_start);
              return;
            }
          } else if (token != ArrayEnd) {
            // Unexpected value after list value. Bail out.
            HandleError(Error::JSON_PARSER_COMMA_OR_ARRAY_END_EXPECTED,
                        token_start);
            return;
          }
        }
        handler_->HandleArrayEnd();
        break;
      }
      case ObjectBegin: {
        handler_->HandleMapBegin();
        start = token_end;
        token = ParseToken(start, end, &token_start, &token_end);
        while (token != ObjectEnd) {
          if (token != StringLiteral) {
            HandleError(Error::JSON_PARSER_STRING_LITERAL_EXPECTED,
                        token_start);
            return;
          }
          std::vector<uint16_t> key;
          if (!DecodeString(token_start + 1, token_end - 1, &key)) {
            HandleError(Error::JSON_PARSER_INVALID_STRING, token_start);
            return;
          }
          handler_->HandleString16(span<uint16_t>(key.data(), key.size()));
          start = token_end;

          token = ParseToken(start, end, &token_start, &token_end);
          if (token != ObjectPairSeparator) {
            HandleError(Error::JSON_PARSER_COLON_EXPECTED, token_start);
            return;
          }
          start = token_end;

          ParseValue(start, end, &token_end, depth + 1);
          if (error_)
            return;
          start = token_end;

          // After a key/value pair, we expect a comma or the end of the
          // object.
          token = ParseToken(start, end, &token_start, &token_end);
          if (token == ListSeparator) {
            start = token_end;
            token = ParseToken(start, end, &token_start, &token_end);
            if (token == ObjectEnd) {
              HandleError(Error::JSON_PARSER_UNEXPECTED_MAP_END, token_start);
              return;
            }
          } else if (token != ObjectEnd) {
            // Unexpected value after last object value. Bail out.
            HandleError(Error::JSON_PARSER_COMMA_OR_MAP_END_EXPECTED,
                        token_start);
            return;
          }
        }
        handler_->HandleMapEnd();
        break;
      }

      default:
        // We got a token that's not a value.
        HandleError(Error::JSON_PARSER_VALUE_EXPECTED, token_start);
        return;
    }

    SkipWhitespaceAndComments(token_end, end, value_token_end);
  }

  void HandleError(Error error, const Char* pos) {
    assert(error != Error::OK);
    if (!error_) {
      handler_->HandleError(
          Status{error, static_cast<size_t>(pos - start_pos_)});
      error_ = true;
    }
  }

  const Char* start_pos_ = nullptr;
  bool error_ = false;
  ParserHandler* handler_;
};
}  // namespace

void ParseJSON(span<uint8_t> chars, ParserHandler* handler) {
  JsonParser<uint8_t> parser(handler);
  parser.Parse(chars.data(), chars.size());
}

void ParseJSON(span<uint16_t> chars, ParserHandler* handler) {
  JsonParser<uint16_t> parser(handler);
  parser.Parse(chars.data(), chars.size());
}

// =============================================================================
// json::ConvertCBORToJSON, json::ConvertJSONToCBOR - for transcoding
// =============================================================================
template <typename C>
Status ConvertCBORToJSONTmpl(span<uint8_t> cbor, C* json) {
  Status status;
  std::unique_ptr<ParserHandler> json_writer = NewJSONEncoder(json, &status);
  cbor::ParseCBOR(cbor, json_writer.get());
  return status;
}

Status ConvertCBORToJSON(span<uint8_t> cbor, std::vector<uint8_t>* json) {
  return ConvertCBORToJSONTmpl(cbor, json);
}

Status ConvertCBORToJSON(span<uint8_t> cbor, std::string* json) {
  return ConvertCBORToJSONTmpl(cbor, json);
}

template <typename T>
Status ConvertJSONToCBORTmpl(span<T> json, std::vector<uint8_t>* cbor) {
  Status status;
  std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(cbor, &status);
  ParseJSON(json, encoder.get());
  return status;
}

Status ConvertJSONToCBOR(span<uint8_t> json, std::vector<uint8_t>* cbor) {
  return ConvertJSONToCBORTmpl(json, cbor);
}

Status ConvertJSONToCBOR(span<uint16_t> json, std::vector<uint8_t>* cbor) {
  return ConvertJSONToCBORTmpl(json, cbor);
}
}  // namespace json
}  // namespace v8_crdtp
