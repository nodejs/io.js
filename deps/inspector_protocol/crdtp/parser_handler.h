// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_PARSER_HANDLER_H_
#define CRDTP_PARSER_HANDLER_H_

#include <cstdint>
#include "span.h"
#include "status.h"

namespace crdtp {
// Handler interface for parser events emitted by a streaming parser.
// See cbor::NewCBOREncoder, cbor::ParseCBOR, json::NewJSONEncoder,
// json::ParseJSON.
class ParserHandler {
 public:
  virtual ~ParserHandler() = default;
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
}  // namespace crdtp

#endif  // CRDTP_PARSER_HANDLER_H_
