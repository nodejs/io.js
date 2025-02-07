// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_STATUS_TEST_SUPPORT_H_
#define CRDTP_STATUS_TEST_SUPPORT_H_

#include <ostream>
#include "status.h"
#include "test_platform.h"

namespace crdtp {
// Supports gtest, to conveniently match Status objects and
// get useful error messages when tests fail.
// Typically used with EXPECT_THAT, e.g.
//
// EXPECT_THAT(status, StatusIs(Error::JSON_PARSER_COLON_EXPECTED, 42));
//
// EXPECT_THAT(status, StatusIsOk());

// Prints a |status|, including its generated error message, error code, and
// position. This is used by gtest for pretty printing actual vs. expected.
void PrintTo(const Status& status, std::ostream* os);

// Matches any status with |status.ok()|.
testing::Matcher<Status> StatusIsOk();

// Matches any status with |error| and |pos|.
testing::Matcher<Status> StatusIs(Error error, size_t pos);
}  // namespace crdtp

#endif  // CRDTP_STATUS_TEST_SUPPORT_H_
