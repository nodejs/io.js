// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "error_support.h"

#include <cassert>

namespace crdtp {

void ErrorSupport::Push() {
  stack_.emplace_back();
}

void ErrorSupport::Pop() {
  stack_.pop_back();
}

void ErrorSupport::SetName(const char* name) {
  assert(!stack_.empty());
  stack_.back().type = NAME;
  stack_.back().name = name;
}

void ErrorSupport::SetIndex(size_t index) {
  assert(!stack_.empty());
  stack_.back().type = INDEX;
  stack_.back().index = index;
}

void ErrorSupport::AddError(const char* error) {
  assert(!stack_.empty());
  if (!errors_.empty())
    errors_ += "; ";
  for (size_t ii = 0; ii < stack_.size(); ++ii) {
    if (ii)
      errors_ += ".";
    const Segment& s = stack_[ii];
    switch (s.type) {
      case NAME:
        errors_ += s.name;
        continue;
      case INDEX:
        errors_ += std::to_string(s.index);
        continue;
      default:
        assert(s.type != EMPTY);
        continue;
    }
  }
  errors_ += ": ";
  errors_ += error;
}

span<uint8_t> ErrorSupport::Errors() const {
  return SpanFrom(errors_);
}

}  // namespace crdtp
