// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is Chromium specific, to make the tests work. It will work
// in the standalone (upstream) build, as well as in Chromium. In other code
// bases (e.g. v8), a custom file with these two functions and with appropriate
// includes may need to be provided, so it isn't necessarily part of a roll.
//
// Put another way: The tests, e.g. json_test.cc include *only* test_platform.h,
// which provides CHECK and gunit functionality, and UTF8<->UTF16 conversion
// functions.

#ifndef CRDTP_TEST_PLATFORM_H_
#define CRDTP_TEST_PLATFORM_H_

#include <cstdint>
#include <string>
#include <vector>
#include "base/check_op.h"  // Provides CHECK and CHECK_EQ, etc.
#include "span.h"
#include "testing/gmock/include/gmock/gmock.h"  // Provides Gunit
#include "testing/gtest/include/gtest/gtest.h"  // Provides Gmock

// Provides UTF8<->UTF16 conversion routines (implemented in .cc file).
namespace crdtp {
std::string UTF16ToUTF8(span<uint16_t> in);
std::vector<uint16_t> UTF8ToUTF16(span<uint8_t> in);
}  // namespace crdtp

#endif  // CRDTP_TEST_PLATFORM_H_
