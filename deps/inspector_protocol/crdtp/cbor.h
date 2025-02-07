// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_CBOR_H_
#define CRDTP_CBOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "export.h"
#include "parser_handler.h"
#include "span.h"

namespace crdtp {
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
// - Maximal size for messages is 2^32 (4 GB).
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

// Checks whether |msg| is a cbor message.
CRDTP_EXPORT bool IsCBORMessage(span<uint8_t> msg);

// Performs a leightweight check of |msg|.
// Disallows:
// - Empty message
// - Not starting with the two bytes 0xd8, 0x5a
// - Empty envelope (all length bytes are 0)
// - Not starting with a map after the envelope stanza
// DevTools messages should pass this check.
CRDTP_EXPORT Status CheckCBORMessage(span<uint8_t> msg);

// =============================================================================
// Encoding individual CBOR items
// =============================================================================

// Some constants for CBOR tokens that only take a single byte on the wire.
CRDTP_EXPORT uint8_t EncodeTrue();
CRDTP_EXPORT uint8_t EncodeFalse();
CRDTP_EXPORT uint8_t EncodeNull();
CRDTP_EXPORT uint8_t EncodeIndefiniteLengthArrayStart();
CRDTP_EXPORT uint8_t EncodeIndefiniteLengthMapStart();
CRDTP_EXPORT uint8_t EncodeStop();

// Encodes |value| as |UNSIGNED| (major type 0) iff >= 0, or |NEGATIVE|
// (major type 1) iff < 0.
CRDTP_EXPORT void EncodeInt32(int32_t value, std::vector<uint8_t>* out);

// Encodes a UTF16 string as a BYTE_STRING (major type 2). Each utf16
// character in |in| is emitted with most significant byte first,
// appending to |out|.
CRDTP_EXPORT void EncodeString16(span<uint16_t> in, std::vector<uint8_t>* out);

// Encodes a UTF8 string |in| as STRING (major type 3).
CRDTP_EXPORT void EncodeString8(span<uint8_t> in, std::vector<uint8_t>* out);

// Encodes the given |latin1| string as STRING8.
// If any non-ASCII character is present, it will be represented
// as a 2 byte UTF8 sequence.
CRDTP_EXPORT void EncodeFromLatin1(span<uint8_t> latin1,
                                   std::vector<uint8_t>* out);

// Encodes the given |utf16| string as STRING8 if it's entirely US-ASCII.
// Otherwise, encodes as STRING16.
CRDTP_EXPORT void EncodeFromUTF16(span<uint16_t> utf16,
                                  std::vector<uint8_t>* out);

// Encodes arbitrary binary data in |in| as a BYTE_STRING (major type 2) with
// definitive length, prefixed with tag 22 indicating expected conversion to
// base64 (see RFC 7049, Table 3 and Section 2.4.4.2).
CRDTP_EXPORT void EncodeBinary(span<uint8_t> in, std::vector<uint8_t>* out);

// Encodes / decodes a double as Major type 7 (SIMPLE_VALUE),
// with additional info = 27, followed by 8 bytes in big endian.
CRDTP_EXPORT void EncodeDouble(double value, std::vector<uint8_t>* out);

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
class CRDTP_EXPORT EnvelopeEncoder {
 public:
  // Emits the envelope start bytes and records the position for the
  // byte size in |byte_size_pos_|. Also emits empty bytes for the
  // byte sisze so that encoding can continue.
  void EncodeStart(std::vector<uint8_t>* out);
  // This records the current size in |out| at position byte_size_pos_.
  // Returns true iff successful.
  bool EncodeStop(std::vector<uint8_t>* out);

 private:
  size_t byte_size_pos_ = 0;
};

class CRDTP_EXPORT EnvelopeHeader {
 public:
  EnvelopeHeader() = default;
  ~EnvelopeHeader() = default;

  // Parse envelope. Implies that `in` accomodates the entire size of envelope.
  static StatusOr<EnvelopeHeader> Parse(span<uint8_t> in);
  // Parse envelope, but allow `in` to only include the beginning of the
  // envelope.
  static StatusOr<EnvelopeHeader> ParseFromFragment(span<uint8_t> in);

  size_t header_size() const { return header_size_; }
  size_t content_size() const { return content_size_; }
  size_t outer_size() const { return header_size_ + content_size_; }

 private:
  EnvelopeHeader(size_t header_size, size_t content_size)
      : header_size_(header_size), content_size_(content_size) {}

  size_t header_size_ = 0;
  size_t content_size_ = 0;
};

// =============================================================================
// cbor::NewCBOREncoder - for encoding from a streaming parser
// =============================================================================

// This can be used to convert to CBOR, by passing the return value to a parser
// that drives it. The handler will encode into |out|, and iff an error occurs
// it will set |status| to an error and clear |out|. Otherwise, |status.ok()|
// will be |true|.
CRDTP_EXPORT std::unique_ptr<ParserHandler> NewCBOREncoder(
    std::vector<uint8_t>* out,
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
class CRDTP_EXPORT CBORTokenizer {
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
  // Returns the envelope including its payload; message which
  // can be passed to the CBORTokenizer constructor, which will
  // then see the envelope token first (looking at it a second time,
  // basically).
  span<uint8_t> GetEnvelope() const;

  // To be called only if ::TokenTag() == CBORTokenTag::ENVELOPE.
  // Returns only the payload inside the envelope, e.g., a map
  // or an array. This is not a complete message by our
  // IsCBORMessage definition, since it doesn't include the
  // enclosing envelope (the header, basically).
  span<uint8_t> GetEnvelopeContents() const;

  // To be called only if ::TokenTag() == CBORTokenTag::ENVELOPE.
  // Returns the envelope header.
  const EnvelopeHeader& GetEnvelopeHeader() const;

 private:
  void ReadNextToken();
  void SetToken(CBORTokenTag token, size_t token_byte_length);
  void SetError(Error error);

  const span<uint8_t> bytes_;
  CBORTokenTag token_tag_;
  struct Status status_;
  size_t token_byte_length_ = 0;
  MajorType token_start_type_;
  uint64_t token_start_internal_value_;
  EnvelopeHeader envelope_header_;
};

// =============================================================================
// cbor::ParseCBOR - for receiving streaming parser events for CBOR messages
// =============================================================================

// Parses a CBOR encoded message from |bytes|, sending events to
// |out|. If an error occurs, sends |out->HandleError|, and parsing stops.
// The client is responsible for discarding the already received information in
// that case.
CRDTP_EXPORT void ParseCBOR(span<uint8_t> bytes, ParserHandler* out);

// =============================================================================
// cbor::AppendString8EntryToMap - for limited in-place editing of messages
// =============================================================================

// Modifies the |cbor| message by appending a new key/value entry at the end
// of the map. Patches up the envelope size; Status.ok() iff successful.
// If not successful, |cbor| may be corrupted after this call.
CRDTP_EXPORT Status AppendString8EntryToCBORMap(span<uint8_t> string8_key,
                                                span<uint8_t> string8_value,
                                                std::vector<uint8_t>* cbor);

namespace internals {  // Exposed only for writing tests.
CRDTP_EXPORT size_t ReadTokenStart(span<uint8_t> bytes,
                                   cbor::MajorType* type,
                                   uint64_t* value);

CRDTP_EXPORT void WriteTokenStart(cbor::MajorType type,
                                  uint64_t value,
                                  std::vector<uint8_t>* encoded);
}  // namespace internals
}  // namespace cbor
}  // namespace crdtp

#endif  // CRDTP_CBOR_H_
