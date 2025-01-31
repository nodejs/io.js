// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"
#include "status_test_support.h"
#include "test_platform.h"

namespace crdtp {
// =============================================================================
// Status and Error codes
// =============================================================================

TEST(StatusTest, StatusToASCIIString) {
  Status ok_status;
  EXPECT_EQ("OK", ok_status.ToASCIIString());
  Status json_error(Error::JSON_PARSER_COLON_EXPECTED, 42);
  EXPECT_EQ("JSON: colon expected at position 42", json_error.ToASCIIString());
  Status cbor_error(Error::CBOR_TRAILING_JUNK, 21);
  EXPECT_EQ("CBOR: trailing junk at position 21", cbor_error.ToASCIIString());
}

TEST(StatusTest, StatusTestSupport) {
  Status ok_status;
  EXPECT_THAT(ok_status, StatusIsOk());
  Status json_error(Error::JSON_PARSER_COLON_EXPECTED, 42);
  EXPECT_THAT(json_error, StatusIs(Error::JSON_PARSER_COLON_EXPECTED, 42));
}
}  // namespace crdtp
