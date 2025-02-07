// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_JSON_PLATFORM_H_
#define CRDTP_JSON_PLATFORM_H_

#include <string>

namespace crdtp {
namespace json {
// These routines are implemented in json_platform.cc, or in a
// platform-dependent (code-base dependent) custom replacement.
// E.g., json_platform_chromium.cc, json_platform_v8.cc.
namespace platform {
// Parses |str| into |result|. Returns false iff there are
// leftover characters or parsing errors.
bool StrToD(const char* str, double* result);

// Prints |value| in a format suitable for JSON.
std::string DToStr(double value);
}  // namespace platform
}  // namespace json
}  // namespace crdtp

#endif  // CRDTP_JSON_PLATFORM_H_
