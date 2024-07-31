// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is a clone of "base/optional.h" in chromium.
// Keep in sync, especially when fixing bugs.
// Copyright 2017 the V8 project authors. All rights reserved.

#ifndef V8_BASE_OPTIONAL_H_
#define V8_BASE_OPTIONAL_H_

#include <optional>

namespace v8 {
namespace base {

// These aliases are deprecated, use std::optional directly.
template <typename T>
using Optional = std::optional<T>;

using std::in_place;
using std::make_optional;
using std::nullopt;
using std::nullopt_t;

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_OPTIONAL_H_
