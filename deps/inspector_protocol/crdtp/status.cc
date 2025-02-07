// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"

namespace crdtp {
// =============================================================================
// Status and Error codes
// =============================================================================

std::string Status::Message() const {
  switch (error) {
    case Error::OK:
      return "OK";
    case Error::JSON_PARSER_UNPROCESSED_INPUT_REMAINS:
      return "JSON: unprocessed input remains";
    case Error::JSON_PARSER_STACK_LIMIT_EXCEEDED:
      return "JSON: stack limit exceeded";
    case Error::JSON_PARSER_NO_INPUT:
      return "JSON: no input";
    case Error::JSON_PARSER_INVALID_TOKEN:
      return "JSON: invalid token";
    case Error::JSON_PARSER_INVALID_NUMBER:
      return "JSON: invalid number";
    case Error::JSON_PARSER_INVALID_STRING:
      return "JSON: invalid string";
    case Error::JSON_PARSER_UNEXPECTED_ARRAY_END:
      return "JSON: unexpected array end";
    case Error::JSON_PARSER_COMMA_OR_ARRAY_END_EXPECTED:
      return "JSON: comma or array end expected";
    case Error::JSON_PARSER_STRING_LITERAL_EXPECTED:
      return "JSON: string literal expected";
    case Error::JSON_PARSER_COLON_EXPECTED:
      return "JSON: colon expected";
    case Error::JSON_PARSER_UNEXPECTED_MAP_END:
      return "JSON: unexpected map end";
    case Error::JSON_PARSER_COMMA_OR_MAP_END_EXPECTED:
      return "JSON: comma or map end expected";
    case Error::JSON_PARSER_VALUE_EXPECTED:
      return "JSON: value expected";

    case Error::CBOR_INVALID_INT32:
      return "CBOR: invalid int32";
    case Error::CBOR_INVALID_DOUBLE:
      return "CBOR: invalid double";
    case Error::CBOR_INVALID_ENVELOPE:
      return "CBOR: invalid envelope";
    case Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH:
      return "CBOR: envelope contents length mismatch";
    case Error::CBOR_MAP_OR_ARRAY_EXPECTED_IN_ENVELOPE:
      return "CBOR: map or array expected in envelope";
    case Error::CBOR_INVALID_STRING8:
      return "CBOR: invalid string8";
    case Error::CBOR_INVALID_STRING16:
      return "CBOR: invalid string16";
    case Error::CBOR_INVALID_BINARY:
      return "CBOR: invalid binary";
    case Error::CBOR_UNSUPPORTED_VALUE:
      return "CBOR: unsupported value";
    case Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE:
      return "CBOR: unexpected EOF reading envelope";
    case Error::CBOR_INVALID_START_BYTE:
      return "CBOR: invalid start byte";
    case Error::CBOR_UNEXPECTED_EOF_EXPECTED_VALUE:
      return "CBOR: unexpected EOF expected value";
    case Error::CBOR_UNEXPECTED_EOF_IN_ARRAY:
      return "CBOR: unexpected EOF in array";
    case Error::CBOR_UNEXPECTED_EOF_IN_MAP:
      return "CBOR: unexpected EOF in map";
    case Error::CBOR_INVALID_MAP_KEY:
      return "CBOR: invalid map key";
    case Error::CBOR_DUPLICATE_MAP_KEY:
      return "CBOR: duplicate map key";
    case Error::CBOR_STACK_LIMIT_EXCEEDED:
      return "CBOR: stack limit exceeded";
    case Error::CBOR_TRAILING_JUNK:
      return "CBOR: trailing junk";
    case Error::CBOR_MAP_START_EXPECTED:
      return "CBOR: map start expected";
    case Error::CBOR_MAP_STOP_EXPECTED:
      return "CBOR: map stop expected";
    case Error::CBOR_ARRAY_START_EXPECTED:
      return "CBOR: array start expected";
    case Error::CBOR_ENVELOPE_SIZE_LIMIT_EXCEEDED:
      return "CBOR: envelope size limit exceeded";

    case Error::MESSAGE_MUST_BE_AN_OBJECT:
      return "Message must be an object";
    case Error::MESSAGE_MUST_HAVE_INTEGER_ID_PROPERTY:
      return "Message must have integer 'id' property";
    case Error::MESSAGE_MUST_HAVE_STRING_METHOD_PROPERTY:
      return "Message must have string 'method' property";
    case Error::MESSAGE_MAY_HAVE_STRING_SESSION_ID_PROPERTY:
      return "Message may have string 'sessionId' property";
    case Error::MESSAGE_MAY_HAVE_OBJECT_PARAMS_PROPERTY:
      return "Message may have object 'params' property";
    case Error::MESSAGE_HAS_UNKNOWN_PROPERTY:
      return "Message has property other than "
             "'id', 'method', 'sessionId', 'params'";

    case Error::BINDINGS_MANDATORY_FIELD_MISSING:
      return "BINDINGS: mandatory field missing";
    case Error::BINDINGS_BOOL_VALUE_EXPECTED:
      return "BINDINGS: bool value expected";
    case Error::BINDINGS_INT32_VALUE_EXPECTED:
      return "BINDINGS: int32 value expected";
    case Error::BINDINGS_DOUBLE_VALUE_EXPECTED:
      return "BINDINGS: double value expected";
    case Error::BINDINGS_STRING_VALUE_EXPECTED:
      return "BINDINGS: string value expected";
    case Error::BINDINGS_STRING8_VALUE_EXPECTED:
      return "BINDINGS: string8 value expected";
    case Error::BINDINGS_BINARY_VALUE_EXPECTED:
      return "BINDINGS: binary value expected";
    case Error::BINDINGS_DICTIONARY_VALUE_EXPECTED:
      return "BINDINGS: dictionary value expected";
    case Error::BINDINGS_INVALID_BASE64_STRING:
      return "BINDINGS: invalid base64 string";
  }
  // Some compilers can't figure out that we can't get here.
  return "INVALID ERROR CODE";
}

std::string Status::ToASCIIString() const {
  if (ok())
    return "OK";
  return Message() + " at position " + std::to_string(pos);
}
}  // namespace crdtp
