// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_MAGLEV_MAGLEV_TEST_H_
#define V8_UNITTESTS_MAGLEV_MAGLEV_TEST_H_

#ifdef V8_ENABLE_MAGLEV

#include "src/compiler/js-heap-broker.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevTest : public TestWithNativeContextAndZone {
 public:
  MaglevTest();
  ~MaglevTest() override;

  compiler::JSHeapBroker* broker() { return &broker_; }

 private:
  compiler::JSHeapBroker broker_;
  compiler::JSHeapBrokerScopeForTesting broker_scope_;
  std::unique_ptr<PersistentHandlesScope> persistent_scope_;
  compiler::CurrentHeapBrokerScope current_broker_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV

#endif  // V8_UNITTESTS_MAGLEV_MAGLEV_TEST_H_
