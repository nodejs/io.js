// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/container/internal/common_policy_traits.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::testing::MockFunction;
using ::testing::AnyNumber;
using ::testing::ReturnRef;

using Slot = int;

struct PolicyWithoutOptionalOps {
  using slot_type = Slot;
  using key_type = Slot;
  using init_type = Slot;

  struct PolicyFunctions {
    std::function<void(void*, Slot*, Slot)> construct;
    std::function<void(void*, Slot*)> destroy;
    std::function<Slot&(Slot*)> element;
  };

  static PolicyFunctions* functions() {
    static PolicyFunctions* functions = new PolicyFunctions();
    return functions;
  }

  static void construct(void* a, Slot* b, Slot c) {
    functions()->construct(a, b, c);
  }
  static void destroy(void* a, Slot* b) { functions()->destroy(a, b); }
  static Slot& element(Slot* b) { return functions()->element(b); }
};

struct PolicyWithOptionalOps : PolicyWithoutOptionalOps {
  struct TransferFunctions {
    std::function<void(void*, Slot*, Slot*)> transfer;
  };

  static TransferFunctions* transfer_fn() {
    static TransferFunctions* transfer_fn = new TransferFunctions();
    return transfer_fn;
  }
  static void transfer(void* a, Slot* b, Slot* c) {
    transfer_fn()->transfer(a, b, c);
  }
};

struct PolicyWithMemcpyTransferAndTrivialDestroy : PolicyWithoutOptionalOps {
  static std::true_type transfer(void*, Slot*, Slot*) { return {}; }
  static std::true_type destroy(void*, Slot*) { return {}; }
};

struct Test : ::testing::Test {
  Test() {
    PolicyWithoutOptionalOps::functions()->construct = [&](void* a1, Slot* a2,
                                                           Slot a3) {
      construct.Call(a1, a2, std::move(a3));
    };
    PolicyWithoutOptionalOps::functions()->destroy = [&](void* a1, Slot* a2) {
      destroy.Call(a1, a2);
    };

    PolicyWithoutOptionalOps::functions()->element = [&](Slot* a1) -> Slot& {
      return element.Call(a1);
    };

    PolicyWithOptionalOps::transfer_fn()->transfer =
        [&](void* a1, Slot* a2, Slot* a3) { return transfer.Call(a1, a2, a3); };
  }

  std::allocator<Slot> alloc;
  int a = 53;

  MockFunction<void(void*, Slot*, Slot)> construct;
  MockFunction<void(void*, Slot*)> destroy;

  MockFunction<Slot&(Slot*)> element;

  MockFunction<void(void*, Slot*, Slot*)> transfer;
};

TEST_F(Test, construct) {
  EXPECT_CALL(construct, Call(&alloc, &a, 53));
  common_policy_traits<PolicyWithoutOptionalOps>::construct(&alloc, &a, 53);
}

TEST_F(Test, destroy) {
  EXPECT_CALL(destroy, Call(&alloc, &a));
  common_policy_traits<PolicyWithoutOptionalOps>::destroy(&alloc, &a);
}

TEST_F(Test, element) {
  int b = 0;
  EXPECT_CALL(element, Call(&a)).WillOnce(ReturnRef(b));
  EXPECT_EQ(&b, &common_policy_traits<PolicyWithoutOptionalOps>::element(&a));
}

TEST_F(Test, without_transfer) {
  int b = 42;
  EXPECT_CALL(element, Call(&a)).Times(AnyNumber()).WillOnce(ReturnRef(a));
  EXPECT_CALL(element, Call(&b)).WillOnce(ReturnRef(b));
  EXPECT_CALL(construct, Call(&alloc, &a, b)).Times(AnyNumber());
  EXPECT_CALL(destroy, Call(&alloc, &b)).Times(AnyNumber());
  common_policy_traits<PolicyWithoutOptionalOps>::transfer(&alloc, &a, &b);
}

TEST_F(Test, with_transfer) {
  int b = 42;
  EXPECT_CALL(transfer, Call(&alloc, &a, &b));
  common_policy_traits<PolicyWithOptionalOps>::transfer(&alloc, &a, &b);
}

TEST(TransferUsesMemcpy, Basic) {
  EXPECT_FALSE(
      common_policy_traits<PolicyWithOptionalOps>::transfer_uses_memcpy());
  EXPECT_TRUE(
      common_policy_traits<
          PolicyWithMemcpyTransferAndTrivialDestroy>::transfer_uses_memcpy());
}

TEST(DestroyIsTrivial, Basic) {
  EXPECT_FALSE(common_policy_traits<PolicyWithOptionalOps>::destroy_is_trivial<
               std::allocator<char>>());
  EXPECT_TRUE(common_policy_traits<PolicyWithMemcpyTransferAndTrivialDestroy>::
                  destroy_is_trivial<std::allocator<char>>());
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
