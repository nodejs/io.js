// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status_test_support.h"

namespace crdtp {
void PrintTo(const Status& status, std::ostream* os) {
  *os << status.ToASCIIString() << " (error: 0x" << std::hex
      << static_cast<int>(status.error) << ", "
      << "pos: " << std::dec << status.pos << ")";
}

namespace {
class StatusIsMatcher : public testing::MatcherInterface<Status> {
 public:
  explicit StatusIsMatcher(Status status) : expected_(status) {}

  bool MatchAndExplain(Status status,
                       testing::MatchResultListener* listener) const override {
    return status.error == expected_.error && status.pos == expected_.pos;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "equals to ";
    PrintTo(expected_, os);
  }

 private:
  Status expected_;
};

class StatusIsOkMatcher : public testing::MatcherInterface<Status> {
  bool MatchAndExplain(Status status,
                       testing::MatchResultListener* listener) const override {
    return status.ok();
  }

  void DescribeTo(std::ostream* os) const override { *os << "is ok"; }
};
}  // namespace

testing::Matcher<Status> StatusIsOk() {
  return MakeMatcher(new StatusIsOkMatcher());
}

testing::Matcher<Status> StatusIs(Error error, size_t pos) {
  return MakeMatcher(new StatusIsMatcher(Status(error, pos)));
}
}  // namespace crdtp
