// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_PROTOCOL_ENCODING_ENCODING_H_
#define V8_INSPECTOR_PROTOCOL_ENCODING_ENCODING_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace v8_inspector_protocol_encoding {
// This library is designed to be portable. The only allowed dependency
// are the C/C++ standard libraries, up to C++11. We support both 32 bit
// and 64 architectures.
//
// Types used below:
// uint8_t: a byte, e.g. for raw bytes or UTF8 characters
// uint16_t: two bytes, e.g. for UTF16 characters
// For input parameters:
//   span<uint8_t>: pointer to bytes and length
//   span<uint16_t>: pointer to UTF16 chars and length
// For output parameters:
//   std::vector<uint8_t> - Owned segment of bytes / utf8 characters and length.
//   std::string - Same, for compatibility, even though char is signed.

// =============================================================================
// span - sequence of bytes
// =============================================================================

// This template is similar to std::span, which will be included in C++20.
template <typename T>
class span {
 public:
  using index_type = size_t;

  span() : data_(nullptr), size_(0) {}
  span(const T* data, index_type size) : data_(data), size_(size) {}

  const T* data() const { return data_; }

  const T* begin() const { return data_; }
  const T* end() const { return data_ + size_; }

  const T& operator[](index_type idx) const { return data_[idx]; }

  span<T> subspan(index_type offset, index_type count) const {
    return span(data_ + offset, count);
  }

  span<T> subspan(index_type offset) const {
    return span(data_ + offset, size_ - offset);
  }

  bool empty() const { return size_ == 0; }

  index_type size() const { return size_; }
  index_type size_bytes() const { return size_ * sizeof(T); }

 private:
  const T* data_;
  index_type size_;
};

template <typename T>
span<T> SpanFrom(const std::vector<T>& v) {
  return span<T>(v.data(), v.size());
}

template <size_t N>
span<uint8_t> SpanFrom(const char (&str)[N]) {
  return span<uint8_t>(reinterpret_cast<const uint8_t*>(str), N - 1);
}

inline span<uint8_t> SpanFrom(const char* str) {
  return str ? span<uint8_t>(reinterpret_cast<const uint8_t*>(str), strlen(str))
             : span<uint8_t>();
}

inline span<uint8_t> SpanFrom(const std::string& v) {
  return span<uint8_t>(reinterpret_cast<const uint8_t*>(v.data()), v.size());
}

// Less than / equality comparison functions for sorting / searching for byte
// spans. These are similar to absl::string_view's < and == operators.
inline bool SpanLessThan(span<uint8_t> x, span<uint8_t> y) noexcept {
  auto min_size = std::min(x.size(), y.size());
  const int r = min_size == 0 ? 0 : memcmp(x.data(), y.data(), min_size);
  return (r < 0) || (r == 0 && x.size() < y.size());
}

inline bool SpanEquals(span<uint8_t> x, span<uint8_t> y) noexcept {
  auto len = x.size();
  if (len != y.size())
    return false;
  return x.data() == y.data() || len == 0 ||
         std::memcmp(x.data(), y.data(), len) == 0;
}

// =============================================================================
// Status and Error codes
// =============================================================================
enum class Error {
  OK = 0,
  // JSON parsing errors - json_parser.{h,cc}.
  JSON_PARSER_UNPROCESSED_INPUT_REMAINS = 0x01,
  JSON_PARSER_STACK_LIMIT_EXCEEDED = 0x02,
  JSON_PARSER_NO_INPUT = 0x03,
  JSON_PARSER_INVALID_TOKEN = 0x04,
  JSON_PARSER_INVALID_NUMBER = 0x05,
  JSON_PARSER_INVALID_STRING = 0x06,
  JSON_PARSER_UNEXPECTED_ARRAY_END = 0x07,
  JSON_PARSER_COMMA_OR_ARRAY_END_EXPECTED = 0x08,
  JSON_PARSER_STRING_LITERAL_EXPECTED = 0x09,
  JSON_PARSER_COLON_EXPECTED = 0x0a,
  JSON_PARSER_UNEXPECTED_MAP_END = 0x0b,
  JSON_PARSER_COMMA_OR_MAP_END_EXPECTED = 0x0c,
  JSON_PARSER_VALUE_EXPECTED = 0x0d,

  CBOR_INVALID_INT32 = 0x0e,
  CBOR_INVALID_DOUBLE = 0x0f,
  CBOR_INVALID_ENVELOPE = 0x10,
  CBOR_INVALID_STRING8 = 0x11,
  CBOR_INVALID_STRING16 = 0x12,
  CBOR_INVALID_BINARY = 0x13,
  CBOR_UNSUPPORTED_VALUE = 0x14,
  CBOR_NO_INPUT = 0x15,
  CBOR_INVALID_START_BYTE = 0x16,
  CBOR_UNEXPECTED_EOF_EXPECTED_VALUE = 0x17,
  CBOR_UNEXPECTED_EOF_IN_ARRAY = 0x18,
  CBOR_UNEXPECTED_EOF_IN_MAP = 0x19,
  CBOR_INVALID_MAP_KEY = 0x1a,
  CBOR_STACK_LIMIT_EXCEEDED = 0x1b,
  CBOR_TRAILING_JUNK = 0x1c,
  CBOR_MAP_START_EXPECTED = 0x1d,
  CBOR_MAP_STOP_EXPECTED = 0x1e,
  CBOR_ENVELOPE_SIZE_LIMIT_EXCEEDED = 0x1f,
};

// A status value with position that can be copied. The default status
// is OK. Usually, error status values should come with a valid position.
struct Status {
  static constexpr size_t npos() { return std::numeric_limits<size_t>::max(); }

  bool ok() const { return error == Error::OK; }

  Error error = Error::OK;
  size_t pos = npos();
  Status(Error error, size_t pos) : error(error), pos(pos) {}
  Status() = default;

  // Returns a 7 bit US-ASCII string, either "OK" or an error message
  // that includes the position.
  std::string ToASCIIString() const;

 private:
  std::string ToASCIIString(const char* msg) const;
};

// Handler interface for parser events emitted by a streaming parser.
// See cbor::NewCBOREncoder, cbor::ParseCBOR, json::NewJSONEncoder,
// json::ParseJSON.
class StreamingParserHandler {
 public:
  virtual ~StreamingParserHandler() = default;
  virtual void HandleMapBegin() = 0;
  virtual void HandleMapEnd() = 0;
  virtual void HandleArrayBegin() = 0;
  virtual void HandleArrayEnd() = 0;
  virtual void HandleString8(span<uint8_t> chars) = 0;
  virtual void HandleString16(span<uint16_t> chars) = 0;
  virtual void HandleBinary(span<uint8_t> bytes) = 0;
  virtual void HandleDouble(double value) = 0;
  virtual void HandleInt32(int32_t value) = 0;
  virtual void HandleBool(bool value) = 0;
  virtual void HandleNull() = 0;

  // The parser may send one error even after other events have already
  // been received. Client code is reponsible to then discard the
  // already processed events.
  // |error| must be an eror, as in, |error.is_ok()| can't be true.
  virtual void HandleError(Status error) = 0;
};

namespace cbor {
// The binary encoding for the inspector protocol follows the CBOR specification
// (RFC 7049). Additional constraints:
// - Only indefinite length maps and arrays are supported.
// - Maps and arrays are wrapped with an envelope, that is, a
//   CBOR tag with value 24 followed by a byte string specifying
//   the byte length of the enclosed map / array. The byte string
//   must use a 32 bit wide length.
// - At the top level, a message must be an indefinite length map
//   wrapped by an envelope.
// - Maximal size for messages is 2^32 (4 GiB).
// - For scalars, we support only the int32_t range, encoded as
//   UNSIGNED/NEGATIVE (major types 0 / 1).
// - UTF16 strings, including with unbalanced surrogate pairs, are encoded
//   as CBOR BYTE_STRING (major type 2). For such strings, the number of
//   bytes encoded must be even.
// - UTF8 strings (major type 3) are supported.
// - 7 bit US-ASCII strings must always be encoded as UTF8 strings, never
//   as UTF16 strings.
// - Arbitrary byte arrays, in the inspector protocol called 'binary',
//   are encoded as BYTE_STRING (major type 2), prefixed with a byte
//   indicating base64 when rendered as JSON.

// =============================================================================
// Detecting CBOR content
// =============================================================================

// The first byte for an envelope, which we use for wrapping dictionaries
// and arrays; and the byte that indicates a byte string with 32 bit length.
// These two bytes start an envelope, and thereby also any CBOR message
// produced or consumed by this protocol. See also |EnvelopeEncoder| below.
uint8_t InitialByteForEnvelope();
uint8_t InitialByteFor32BitLengthByteString();

// Checks whether |msg| is a cbor message.
bool IsCBORMessage(span<uint8_t> msg);

// =============================================================================
// Encoding individual CBOR items
// =============================================================================

// Some constants for CBOR tokens that only take a single byte on the wire.
uint8_t EncodeTrue();
uint8_t EncodeFalse();
uint8_t EncodeNull();
uint8_t EncodeIndefiniteLengthArrayStart();
uint8_t EncodeIndefiniteLengthMapStart();
uint8_t EncodeStop();

// Encodes |value| as |UNSIGNED| (major type 0) iff >= 0, or |NEGATIVE|
// (major type 1) iff < 0.
void EncodeInt32(int32_t value, std::vector<uint8_t>* out);
void EncodeInt32(int32_t value, std::string* out);

// Encodes a UTF16 string as a BYTE_STRING (major type 2). Each utf16
// character in |in| is emitted with most significant byte first,
// appending to |out|.
void EncodeString16(span<uint16_t> in, std::vector<uint8_t>* out);
void EncodeString16(span<uint16_t> in, std::string* out);

// Encodes a UTF8 string |in| as STRING (major type 3).
void EncodeString8(span<uint8_t> in, std::vector<uint8_t>* out);
void EncodeString8(span<uint8_t> in, std::string* out);

// Encodes the given |latin1| string as STRING8.
// If any non-ASCII character is present, it will be represented
// as a 2 byte UTF8 sequence.
void EncodeFromLatin1(span<uint8_t> latin1, std::vector<uint8_t>* out);
void EncodeFromLatin1(span<uint8_t> latin1, std::string* out);

// Encodes the given |utf16| string as STRING8 if it's entirely US-ASCII.
// Otherwise, encodes as STRING16.
void EncodeFromUTF16(span<uint16_t> utf16, std::vector<uint8_t>* out);
void EncodeFromUTF16(span<uint16_t> utf16, std::string* out);

// Encodes arbitrary binary data in |in| as a BYTE_STRING (major type 2) with
// definitive length, prefixed with tag 22 indicating expected conversion to
// base64 (see RFC 7049, Table 3 and Section 2.4.4.2).
void EncodeBinary(span<uint8_t> in, std::vector<uint8_t>* out);
void EncodeBinary(span<uint8_t> in, std::string* out);

// Encodes / decodes a double as Major type 7 (SIMPLE_VALUE),
// with additional info = 27, followed by 8 bytes in big endian.
void EncodeDouble(double value, std::vector<uint8_t>* out);
void EncodeDouble(double value, std::string* out);

// =============================================================================
// cbor::EnvelopeEncoder - for wrapping submessages
// =============================================================================

// An envelope indicates the byte length of a wrapped item.
// We use this for maps and array, which allows the decoder
// to skip such (nested) values whole sale.
// It's implemented as a CBOR tag (major type 6) with additional
// info = 24, followed by a byte string with a 32 bit length value;
// so the maximal structure that we can wrap is 2^32 bits long.
// See also: https://tools.ietf.org/html/rfc7049#section-2.4.4.1
class EnvelopeEncoder {
 public:
  // Emits the envelope start bytes and records the position for the
  // byte size in |byte_size_pos_|. Also emits empty bytes for the
  // byte sisze so that encoding can continue.
  void EncodeStart(std::vector<uint8_t>* out);
  void EncodeStart(std::string* out);
  // This records the current size in |out| at position byte_size_pos_.
  // Returns true iff successful.
  bool EncodeStop(std::vector<uint8_t>* out);
  bool EncodeStop(std::string* out);

 private:
  size_t byte_size_pos_ = 0;
};

// =============================================================================
// cbor::NewCBOREncoder - for encoding from a streaming parser
// =============================================================================

// This can be used to convert to CBOR, by passing the return value to a parser
// that drives it. The handler will encode into |out|, and iff an error occurs
// it will set |status| to an error and clear |out|. Otherwise, |status.ok()|
// will be |true|.
std::unique_ptr<StreamingParserHandler> NewCBOREncoder(
    std::vector<uint8_t>* out,
    Status* status);
std::unique_ptr<StreamingParserHandler> NewCBOREncoder(std::string* out,
                                                       Status* status);

// =============================================================================
// cbor::CBORTokenizer - for parsing individual CBOR items
// =============================================================================

// Tags for the tokens within a CBOR message that CBORTokenizer understands.
// Note that this is not the same terminology as the CBOR spec (RFC 7049),
// but rather, our adaptation. For instance, we lump unsigned and signed
// major type into INT32 here (and disallow values outside the int32_t range).
enum class CBORTokenTag {
  // Encountered an error in the structure of the message. Consult
  // status() for details.
  ERROR_VALUE,
  // Booleans and NULL.
  TRUE_VALUE,
  FALSE_VALUE,
  NULL_VALUE,
  // An int32_t (signed 32 bit integer).
  INT32,
  // A double (64 bit floating point).
  DOUBLE,
  // A UTF8 string.
  STRING8,
  // A UTF16 string.
  STRING16,
  // A binary string.
  BINARY,
  // Starts an indefinite length map; after the map start we expect
  // alternating keys and values, followed by STOP.
  MAP_START,
  // Starts an indefinite length array; after the array start we
  // expect values, followed by STOP.
  ARRAY_START,
  // Ends a map or an array.
  STOP,
  // An envelope indicator, wrapping a map or array.
  // Internally this carries the byte length of the wrapped
  // map or array. While CBORTokenizer::Next() will read / skip the entire
  // envelope, CBORTokenizer::EnterEnvelope() reads the tokens
  // inside of it.
  ENVELOPE,
  // We've reached the end there is nothing else to read.
  DONE,
};

// The major types from RFC 7049 Section 2.1.
enum class MajorType {
  UNSIGNED = 0,
  NEGATIVE = 1,
  BYTE_STRING = 2,
  STRING = 3,
  ARRAY = 4,
  MAP = 5,
  TAG = 6,
  SIMPLE_VALUE = 7
};

// CBORTokenizer segments a CBOR message, presenting the tokens therein as
// numbers, strings, etc. This is not a complete CBOR parser, but makes it much
// easier to implement one (e.g. ParseCBOR, above). It can also be used to parse
// messages partially.
class CBORTokenizer {
 public:
  explicit CBORTokenizer(span<uint8_t> bytes);
  ~CBORTokenizer();

  // Identifies the current token that we're looking at,
  // or ERROR_VALUE (in which ase ::Status() has details)
  // or DONE (if we're past the last token).
  CBORTokenTag TokenTag() const;

  // Advances to the next token.
  void Next();
  // Can only be called if TokenTag() == CBORTokenTag::ENVELOPE.
  // While Next() would skip past the entire envelope / what it's
  // wrapping, EnterEnvelope positions the cursor inside of the envelope,
  // letting the client explore the nested structure.
  void EnterEnvelope();

  // If TokenTag() is CBORTokenTag::ERROR_VALUE, then Status().error describes
  // the error more precisely; otherwise it'll be set to Error::OK.
  // In either case, Status().pos is the current position.
  struct Status Status() const;

  // The following methods retrieve the token values. They can only
  // be called if TokenTag() matches.

  // To be called only if ::TokenTag() == CBORTokenTag::INT32.
  int32_t GetInt32() const;

  // To be called only if ::TokenTag() == CBORTokenTag::DOUBLE.
  double GetDouble() const;

  // To be called only if ::TokenTag() == CBORTokenTag::STRING8.
  span<uint8_t> GetString8() const;

  // Wire representation for STRING16 is low byte first (little endian).
  // To be called only if ::TokenTag() == CBORTokenTag::STRING16.
  span<uint8_t> GetString16WireRep() const;

  // To be called only if ::TokenTag() == CBORTokenTag::BINARY.
  span<uint8_t> GetBinary() const;

  // To be called only if ::TokenTag() == CBORTokenTag::ENVELOPE.
  span<uint8_t> GetEnvelopeContents() const;

 private:
  void ReadNextToken(bool enter_envelope);
  void SetToken(CBORTokenTag token, size_t token_byte_length);
  void SetError(Error error);

  span<uint8_t> bytes_;
  CBORTokenTag token_tag_;
  struct Status status_;
  size_t token_byte_length_;
  MajorType token_start_type_;
  uint64_t token_start_internal_value_;
};

// =============================================================================
// cbor::ParseCBOR - for receiving streaming parser events for CBOR messages
// =============================================================================

// Parses a CBOR encoded message from |bytes|, sending events to
// |out|. If an error occurs, sends |out->HandleError|, and parsing stops.
// The client is responsible for discarding the already received information in
// that case.
void ParseCBOR(span<uint8_t> bytes, StreamingParserHandler* out);

// =============================================================================
// cbor::AppendString8EntryToMap - for limited in-place editing of messages
// =============================================================================

// Modifies the |cbor| message by appending a new key/value entry at the end
// of the map. Patches up the envelope size; Status.ok() iff successful.
// If not successful, |cbor| may be corrupted after this call.
Status AppendString8EntryToCBORMap(span<uint8_t> string8_key,
                                   span<uint8_t> string8_value,
                                   std::vector<uint8_t>* cbor);
Status AppendString8EntryToCBORMap(span<uint8_t> string8_key,
                                   span<uint8_t> string8_value,
                                   std::string* cbor);

namespace internals {  // Exposed only for writing tests.
size_t ReadTokenStart(span<uint8_t> bytes,
                      cbor::MajorType* type,
                      uint64_t* value);

void WriteTokenStart(cbor::MajorType type,
                     uint64_t value,
                     std::vector<uint8_t>* encoded);
void WriteTokenStart(cbor::MajorType type,
                     uint64_t value,
                     std::string* encoded);
}  // namespace internals
}  // namespace cbor

namespace json {
// Client code must provide an instance. Implementation should delegate
// to whatever is appropriate.
class Platform {
 public:
  virtual ~Platform() = default;
  // Parses |str| into |result|. Returns false iff there are
  // leftover characters or parsing errors.
  virtual bool StrToD(const char* str, double* result) const = 0;

  // Prints |value| in a format suitable for JSON.
  virtual std::unique_ptr<char[]> DToStr(double value) const = 0;
};

// =============================================================================
// json::NewJSONEncoder - for encoding streaming parser events as JSON
// =============================================================================

// Returns a handler object which will write ascii characters to |out|.
// |status->ok()| will be false iff the handler routine HandleError() is called.
// In that case, we'll stop emitting output.
// Except for calling the HandleError routine at any time, the client
// code must call the Handle* methods in an order in which they'd occur
// in valid JSON; otherwise we may crash (the code uses assert).
std::unique_ptr<StreamingParserHandler> NewJSONEncoder(
    const Platform* platform,
    std::vector<uint8_t>* out,
    Status* status);
std::unique_ptr<StreamingParserHandler> NewJSONEncoder(const Platform* platform,
                                                       std::string* out,
                                                       Status* status);

// =============================================================================
// json::ParseJSON - for receiving streaming parser events for JSON
// =============================================================================

void ParseJSON(const Platform& platform,
               span<uint8_t> chars,
               StreamingParserHandler* handler);
void ParseJSON(const Platform& platform,
               span<uint16_t> chars,
               StreamingParserHandler* handler);

// =============================================================================
// json::ConvertCBORToJSON, json::ConvertJSONToCBOR - for transcoding
// =============================================================================
Status ConvertCBORToJSON(const Platform& platform,
                         span<uint8_t> cbor,
                         std::string* json);
Status ConvertCBORToJSON(const Platform& platform,
                         span<uint8_t> cbor,
                         std::vector<uint8_t>* json);
Status ConvertJSONToCBOR(const Platform& platform,
                         span<uint8_t> json,
                         std::vector<uint8_t>* cbor);
Status ConvertJSONToCBOR(const Platform& platform,
                         span<uint16_t> json,
                         std::vector<uint8_t>* cbor);
Status ConvertJSONToCBOR(const Platform& platform,
                         span<uint8_t> json,
                         std::string* cbor);
Status ConvertJSONToCBOR(const Platform& platform,
                         span<uint16_t> json,
                         std::string* cbor);
}  // namespace json
}  // namespace v8_inspector_protocol_encoding

#endif  // V8_INSPECTOR_PROTOCOL_ENCODING_ENCODING_H_
