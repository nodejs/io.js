// Copyright 2017 The Abseil Authors.
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

#include "absl/base/internal/exception_safety_testing.h"

#ifdef ABSL_HAVE_EXCEPTIONS

#include <cstddef>
#include <exception>
#include <iostream>
#include <list>
#include <type_traits>
#include <vector>

#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"

namespace testing {

namespace {

using ::testing::exceptions_internal::SetCountdown;
using ::testing::exceptions_internal::TestException;
using ::testing::exceptions_internal::UnsetCountdown;

// EXPECT_NO_THROW can't inspect the thrown inspection in general.
template <typename F>
void ExpectNoThrow(const F& f) {
  try {
    f();
  } catch (const TestException& e) {
    ADD_FAILURE() << "Unexpected exception thrown from " << e.what();
  }
}

TEST(ThrowingValueTest, Throws) {
  SetCountdown();
  EXPECT_THROW(ThrowingValue<> bomb, TestException);

  // It's not guaranteed that every operator only throws *once*.  The default
  // ctor only throws once, though, so use it to make sure we only throw when
  // the countdown hits 0
  SetCountdown(2);
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  ExpectNoThrow([]() { ThrowingValue<> bomb; });
  EXPECT_THROW(ThrowingValue<> bomb, TestException);

  UnsetCountdown();
}

// Tests that an operation throws when the countdown is at 0, doesn't throw when
// the countdown doesn't hit 0, and doesn't modify the state of the
// ThrowingValue if it throws
template <typename F>
void TestOp(const F& f) {
  ExpectNoThrow(f);

  SetCountdown();
  EXPECT_THROW(f(), TestException);
  UnsetCountdown();
}

TEST(ThrowingValueTest, ThrowingCtors) {
  ThrowingValue<> bomb;

  TestOp([]() { ThrowingValue<> bomb(1); });
  TestOp([&]() { ThrowingValue<> bomb1 = bomb; });
  TestOp([&]() { ThrowingValue<> bomb1 = std::move(bomb); });
}

TEST(ThrowingValueTest, ThrowingAssignment) {
  ThrowingValue<> bomb, bomb1;

  TestOp([&]() { bomb = bomb1; });
  TestOp([&]() { bomb = std::move(bomb1); });

  // Test that when assignment throws, the assignment should fail (lhs != rhs)
  // and strong guarantee fails (lhs != lhs_copy).
  {
    ThrowingValue<> lhs(39), rhs(42);
    ThrowingValue<> lhs_copy(lhs);
    SetCountdown();
    EXPECT_THROW(lhs = rhs, TestException);
    UnsetCountdown();
    EXPECT_NE(lhs, rhs);
    EXPECT_NE(lhs_copy, lhs);
  }
  {
    ThrowingValue<> lhs(39), rhs(42);
    ThrowingValue<> lhs_copy(lhs), rhs_copy(rhs);
    SetCountdown();
    EXPECT_THROW(lhs = std::move(rhs), TestException);
    UnsetCountdown();
    EXPECT_NE(lhs, rhs_copy);
    EXPECT_NE(lhs_copy, lhs);
  }
}

TEST(ThrowingValueTest, ThrowingComparisons) {
  ThrowingValue<> bomb1, bomb2;
  TestOp([&]() { return bomb1 == bomb2; });
  TestOp([&]() { return bomb1 != bomb2; });
  TestOp([&]() { return bomb1 < bomb2; });
  TestOp([&]() { return bomb1 <= bomb2; });
  TestOp([&]() { return bomb1 > bomb2; });
  TestOp([&]() { return bomb1 >= bomb2; });
}

TEST(ThrowingValueTest, ThrowingArithmeticOps) {
  ThrowingValue<> bomb1(1), bomb2(2);

  TestOp([&bomb1]() { +bomb1; });
  TestOp([&bomb1]() { -bomb1; });
  TestOp([&bomb1]() { ++bomb1; });
  TestOp([&bomb1]() { bomb1++; });
  TestOp([&bomb1]() { --bomb1; });
  TestOp([&bomb1]() { bomb1--; });

  TestOp([&]() { bomb1 + bomb2; });
  TestOp([&]() { bomb1 - bomb2; });
  TestOp([&]() { bomb1* bomb2; });
  TestOp([&]() { bomb1 / bomb2; });
  TestOp([&]() { bomb1 << 1; });
  TestOp([&]() { bomb1 >> 1; });
}

TEST(ThrowingValueTest, ThrowingLogicalOps) {
  ThrowingValue<> bomb1, bomb2;

  TestOp([&bomb1]() { !bomb1; });
  TestOp([&]() { bomb1&& bomb2; });
  TestOp([&]() { bomb1 || bomb2; });
}

TEST(ThrowingValueTest, ThrowingBitwiseOps) {
  ThrowingValue<> bomb1, bomb2;

  TestOp([&bomb1]() { ~bomb1; });
  TestOp([&]() { bomb1 & bomb2; });
  TestOp([&]() { bomb1 | bomb2; });
  TestOp([&]() { bomb1 ^ bomb2; });
}

TEST(ThrowingValueTest, ThrowingCompoundAssignmentOps) {
  ThrowingValue<> bomb1(1), bomb2(2);

  TestOp([&]() { bomb1 += bomb2; });
  TestOp([&]() { bomb1 -= bomb2; });
  TestOp([&]() { bomb1 *= bomb2; });
  TestOp([&]() { bomb1 /= bomb2; });
  TestOp([&]() { bomb1 %= bomb2; });
  TestOp([&]() { bomb1 &= bomb2; });
  TestOp([&]() { bomb1 |= bomb2; });
  TestOp([&]() { bomb1 ^= bomb2; });
  TestOp([&]() { bomb1 *= bomb2; });
}

TEST(ThrowingValueTest, ThrowingStreamOps) {
  ThrowingValue<> bomb;

  TestOp([&]() {
    std::istringstream stream;
    stream >> bomb;
  });
  TestOp([&]() {
    std::stringstream stream;
    stream << bomb;
  });
}

// Tests the operator<< of ThrowingValue by forcing ConstructorTracker to emit
// a nonfatal failure that contains the string representation of the Thrower
TEST(ThrowingValueTest, StreamOpsOutput) {
  using ::testing::TypeSpec;
  exceptions_internal::ConstructorTracker ct(exceptions_internal::countdown);

  // Test default spec list (kEverythingThrows)
  EXPECT_NONFATAL_FAILURE(
      {
        using Thrower = ThrowingValue<TypeSpec{}>;
        auto thrower = Thrower(123);
        thrower.~Thrower();
      },
      "ThrowingValue<>(123)");

  // Test with one item in spec list (kNoThrowCopy)
  EXPECT_NONFATAL_FAILURE(
      {
        using Thrower = ThrowingValue<TypeSpec::kNoThrowCopy>;
        auto thrower = Thrower(234);
        thrower.~Thrower();
      },
      "ThrowingValue<kNoThrowCopy>(234)");

  // Test with multiple items in spec list (kNoThrowMove, kNoThrowNew)
  EXPECT_NONFATAL_FAILURE(
      {
        using Thrower =
            ThrowingValue<TypeSpec::kNoThrowMove | TypeSpec::kNoThrowNew>;
        auto thrower = Thrower(345);
        thrower.~Thrower();
      },
      "ThrowingValue<kNoThrowMove | kNoThrowNew>(345)");

  // Test with all items in spec list (kNoThrowCopy, kNoThrowMove, kNoThrowNew)
  EXPECT_NONFATAL_FAILURE(
      {
        using Thrower = ThrowingValue<static_cast<TypeSpec>(-1)>;
        auto thrower = Thrower(456);
        thrower.~Thrower();
      },
      "ThrowingValue<kNoThrowCopy | kNoThrowMove | kNoThrowNew>(456)");
}

template <typename F>
void TestAllocatingOp(const F& f) {
  ExpectNoThrow(f);

  SetCountdown();
  EXPECT_THROW(f(), exceptions_internal::TestBadAllocException);
  UnsetCountdown();
}

TEST(ThrowingValueTest, ThrowingAllocatingOps) {
  // make_unique calls unqualified operator new, so these exercise the
  // ThrowingValue overloads.
  TestAllocatingOp([]() { return absl::make_unique<ThrowingValue<>>(1); });
  TestAllocatingOp([]() { return absl::make_unique<ThrowingValue<>[]>(2); });
}

TEST(ThrowingValueTest, NonThrowingMoveCtor) {
  ThrowingValue<TypeSpec::kNoThrowMove> nothrow_ctor;

  SetCountdown();
  ExpectNoThrow([&nothrow_ctor]() {
    ThrowingValue<TypeSpec::kNoThrowMove> nothrow1 = std::move(nothrow_ctor);
  });
  UnsetCountdown();
}

TEST(ThrowingValueTest, NonThrowingMoveAssign) {
  ThrowingValue<TypeSpec::kNoThrowMove> nothrow_assign1, nothrow_assign2;

  SetCountdown();
  ExpectNoThrow([&nothrow_assign1, &nothrow_assign2]() {
    nothrow_assign1 = std::move(nothrow_assign2);
  });
  UnsetCountdown();
}

TEST(ThrowingValueTest, ThrowingCopyCtor) {
  ThrowingValue<> tv;

  TestOp([&]() { ThrowingValue<> tv_copy(tv); });
}

TEST(ThrowingValueTest, ThrowingCopyAssign) {
  ThrowingValue<> tv1, tv2;

  TestOp([&]() { tv1 = tv2; });
}

TEST(ThrowingValueTest, NonThrowingCopyCtor) {
  ThrowingValue<TypeSpec::kNoThrowCopy> nothrow_ctor;

  SetCountdown();
  ExpectNoThrow([&nothrow_ctor]() {
    ThrowingValue<TypeSpec::kNoThrowCopy> nothrow1(nothrow_ctor);
  });
  UnsetCountdown();
}

TEST(ThrowingValueTest, NonThrowingCopyAssign) {
  ThrowingValue<TypeSpec::kNoThrowCopy> nothrow_assign1, nothrow_assign2;

  SetCountdown();
  ExpectNoThrow([&nothrow_assign1, &nothrow_assign2]() {
    nothrow_assign1 = nothrow_assign2;
  });
  UnsetCountdown();
}

TEST(ThrowingValueTest, ThrowingSwap) {
  ThrowingValue<> bomb1, bomb2;
  TestOp([&]() { std::swap(bomb1, bomb2); });
}

TEST(ThrowingValueTest, NonThrowingSwap) {
  ThrowingValue<TypeSpec::kNoThrowMove> bomb1, bomb2;
  ExpectNoThrow([&]() { std::swap(bomb1, bomb2); });
}

TEST(ThrowingValueTest, NonThrowingAllocation) {
  ThrowingValue<TypeSpec::kNoThrowNew>* allocated;
  ThrowingValue<TypeSpec::kNoThrowNew>* array;

  ExpectNoThrow([&allocated]() {
    allocated = new ThrowingValue<TypeSpec::kNoThrowNew>(1);
    delete allocated;
  });
  ExpectNoThrow([&array]() {
    array = new ThrowingValue<TypeSpec::kNoThrowNew>[2];
    delete[] array;
  });
}

TEST(ThrowingValueTest, NonThrowingDelete) {
  auto* allocated = new ThrowingValue<>(1);
  auto* array = new ThrowingValue<>[2];

  SetCountdown();
  ExpectNoThrow([allocated]() { delete allocated; });
  SetCountdown();
  ExpectNoThrow([array]() { delete[] array; });

  UnsetCountdown();
}

TEST(ThrowingValueTest, NonThrowingPlacementDelete) {
  constexpr int kArrayLen = 2;
  // We intentionally create extra space to store the tag allocated by placement
  // new[].
  constexpr size_t kExtraSpaceLen = sizeof(size_t) * 2;

  alignas(ThrowingValue<>) unsigned char buf[sizeof(ThrowingValue<>)];
  alignas(ThrowingValue<>) unsigned char
      array_buf[kExtraSpaceLen + sizeof(ThrowingValue<>[kArrayLen])];
  auto* placed = new (&buf) ThrowingValue<>(1);
  auto placed_array = new (&array_buf) ThrowingValue<>[kArrayLen];
  auto* placed_array_end = reinterpret_cast<unsigned char*>(placed_array) +
                           sizeof(ThrowingValue<>[kArrayLen]);
  EXPECT_LE(placed_array_end, array_buf + sizeof(array_buf));

  SetCountdown();
  ExpectNoThrow([placed, &buf]() {
    placed->~ThrowingValue<>();
    ThrowingValue<>::operator delete(placed, &buf);
  });

  SetCountdown();
  ExpectNoThrow([&, placed_array]() {
    for (int i = 0; i < kArrayLen; ++i) placed_array[i].~ThrowingValue<>();
    ThrowingValue<>::operator delete[](placed_array, &array_buf);
  });

  UnsetCountdown();
}

TEST(ThrowingValueTest, NonThrowingDestructor) {
  auto* allocated = new ThrowingValue<>();

  SetCountdown();
  ExpectNoThrow([allocated]() { delete allocated; });
  UnsetCountdown();
}

TEST(ThrowingBoolTest, ThrowingBool) {
  ThrowingBool t = true;

  // Test that it's contextually convertible to bool
  if (t) {  // NOLINT(whitespace/empty_if_body)
  }
  EXPECT_TRUE(t);

  TestOp([&]() { (void)!t; });
}

TEST(ThrowingAllocatorTest, MemoryManagement) {
  // Just exercise the memory management capabilities under LSan to make sure we
  // don't leak.
  ThrowingAllocator<int> int_alloc;
  int* ip = int_alloc.allocate(1);
  int_alloc.deallocate(ip, 1);
  int* i_array = int_alloc.allocate(2);
  int_alloc.deallocate(i_array, 2);

  ThrowingAllocator<ThrowingValue<>> tv_alloc;
  ThrowingValue<>* ptr = tv_alloc.allocate(1);
  tv_alloc.deallocate(ptr, 1);
  ThrowingValue<>* tv_array = tv_alloc.allocate(2);
  tv_alloc.deallocate(tv_array, 2);
}

TEST(ThrowingAllocatorTest, CallsGlobalNew) {
  ThrowingAllocator<ThrowingValue<>, AllocSpec::kNoThrowAllocate> nothrow_alloc;
  ThrowingValue<>* ptr;

  SetCountdown();
  // This will only throw if ThrowingValue::new is called.
  ExpectNoThrow([&]() { ptr = nothrow_alloc.allocate(1); });
  nothrow_alloc.deallocate(ptr, 1);

  UnsetCountdown();
}

TEST(ThrowingAllocatorTest, ThrowingConstructors) {
  ThrowingAllocator<int> int_alloc;
  int* ip = nullptr;

  SetCountdown();
  EXPECT_THROW(ip = int_alloc.allocate(1), TestException);
  ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });

  *ip = 1;
  SetCountdown();
  EXPECT_THROW(int_alloc.construct(ip, 2), TestException);
  EXPECT_EQ(*ip, 1);
  int_alloc.deallocate(ip, 1);

  UnsetCountdown();
}

TEST(ThrowingAllocatorTest, NonThrowingConstruction) {
  {
    ThrowingAllocator<int, AllocSpec::kNoThrowAllocate> int_alloc;
    int* ip = nullptr;

    SetCountdown();
    ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });

    SetCountdown();
    ExpectNoThrow([&]() { int_alloc.construct(ip, 2); });

    EXPECT_EQ(*ip, 2);
    int_alloc.deallocate(ip, 1);

    UnsetCountdown();
  }

  {
    ThrowingAllocator<int> int_alloc;
    int* ip = nullptr;
    ExpectNoThrow([&]() { ip = int_alloc.allocate(1); });
    ExpectNoThrow([&]() { int_alloc.construct(ip, 2); });
    EXPECT_EQ(*ip, 2);
    int_alloc.deallocate(ip, 1);
  }

  {
    ThrowingAllocator<ThrowingValue<>, AllocSpec::kNoThrowAllocate>
        nothrow_alloc;
    ThrowingValue<>* ptr;

    SetCountdown();
    ExpectNoThrow([&]() { ptr = nothrow_alloc.allocate(1); });

    SetCountdown();
    ExpectNoThrow(
        [&]() { nothrow_alloc.construct(ptr, 2, testing::nothrow_ctor); });

    EXPECT_EQ(ptr->Get(), 2);
    nothrow_alloc.destroy(ptr);
    nothrow_alloc.deallocate(ptr, 1);

    UnsetCountdown();
  }

  {
    ThrowingAllocator<int> a;

    SetCountdown();
    ExpectNoThrow([&]() { ThrowingAllocator<double> a1 = a; });

    SetCountdown();
    ExpectNoThrow([&]() { ThrowingAllocator<double> a1 = std::move(a); });

    UnsetCountdown();
  }
}

TEST(ThrowingAllocatorTest, ThrowingAllocatorConstruction) {
  ThrowingAllocator<int> a;
  TestOp([]() { ThrowingAllocator<int> a; });
  TestOp([&]() { a.select_on_container_copy_construction(); });
}

TEST(ThrowingAllocatorTest, State) {
  ThrowingAllocator<int> a1, a2;
  EXPECT_NE(a1, a2);

  auto a3 = a1;
  EXPECT_EQ(a3, a1);
  int* ip = a1.allocate(1);
  EXPECT_EQ(a3, a1);
  a3.deallocate(ip, 1);
  EXPECT_EQ(a3, a1);
}

TEST(ThrowingAllocatorTest, InVector) {
  std::vector<ThrowingValue<>, ThrowingAllocator<ThrowingValue<>>> v;
  for (int i = 0; i < 20; ++i) v.push_back({});
  for (int i = 0; i < 20; ++i) v.pop_back();
}

TEST(ThrowingAllocatorTest, InList) {
  std::list<ThrowingValue<>, ThrowingAllocator<ThrowingValue<>>> l;
  for (int i = 0; i < 20; ++i) l.push_back({});
  for (int i = 0; i < 20; ++i) l.pop_back();
  for (int i = 0; i < 20; ++i) l.push_front({});
  for (int i = 0; i < 20; ++i) l.pop_front();
}

template <typename TesterInstance, typename = void>
struct NullaryTestValidator : public std::false_type {};

template <typename TesterInstance>
struct NullaryTestValidator<
    TesterInstance,
    absl::void_t<decltype(std::declval<TesterInstance>().Test())>>
    : public std::true_type {};

template <typename TesterInstance>
bool HasNullaryTest(const TesterInstance&) {
  return NullaryTestValidator<TesterInstance>::value;
}

void DummyOp(void*) {}

template <typename TesterInstance, typename = void>
struct UnaryTestValidator : public std::false_type {};

template <typename TesterInstance>
struct UnaryTestValidator<
    TesterInstance,
    absl::void_t<decltype(std::declval<TesterInstance>().Test(DummyOp))>>
    : public std::true_type {};

template <typename TesterInstance>
bool HasUnaryTest(const TesterInstance&) {
  return UnaryTestValidator<TesterInstance>::value;
}

TEST(ExceptionSafetyTesterTest, IncompleteTypesAreNotTestable) {
  using T = exceptions_internal::UninitializedT;
  auto op = [](T* t) {};
  auto inv = [](T*) { return testing::AssertionSuccess(); };
  auto fac = []() { return absl::make_unique<T>(); };

  // Test that providing operation and inveriants still does not allow for the
  // the invocation of .Test() and .Test(op) because it lacks a factory
  auto without_fac =
      testing::MakeExceptionSafetyTester().WithOperation(op).WithContracts(
          inv, testing::strong_guarantee);
  EXPECT_FALSE(HasNullaryTest(without_fac));
  EXPECT_FALSE(HasUnaryTest(without_fac));

  // Test that providing contracts and factory allows the invocation of
  // .Test(op) but does not allow for .Test() because it lacks an operation
  auto without_op = testing::MakeExceptionSafetyTester()
                        .WithContracts(inv, testing::strong_guarantee)
                        .WithFactory(fac);
  EXPECT_FALSE(HasNullaryTest(without_op));
  EXPECT_TRUE(HasUnaryTest(without_op));

  // Test that providing operation and factory still does not allow for the
  // the invocation of .Test() and .Test(op) because it lacks contracts
  auto without_inv =
      testing::MakeExceptionSafetyTester().WithOperation(op).WithFactory(fac);
  EXPECT_FALSE(HasNullaryTest(without_inv));
  EXPECT_FALSE(HasUnaryTest(without_inv));
}

struct ExampleStruct {};

std::unique_ptr<ExampleStruct> ExampleFunctionFactory() {
  return absl::make_unique<ExampleStruct>();
}

void ExampleFunctionOperation(ExampleStruct*) {}

testing::AssertionResult ExampleFunctionContract(ExampleStruct*) {
  return testing::AssertionSuccess();
}

struct {
  std::unique_ptr<ExampleStruct> operator()() const {
    return ExampleFunctionFactory();
  }
} example_struct_factory;

struct {
  void operator()(ExampleStruct*) const {}
} example_struct_operation;

struct {
  testing::AssertionResult operator()(ExampleStruct* example_struct) const {
    return ExampleFunctionContract(example_struct);
  }
} example_struct_contract;

auto example_lambda_factory = []() { return ExampleFunctionFactory(); };

auto example_lambda_operation = [](ExampleStruct*) {};

auto example_lambda_contract = [](ExampleStruct* example_struct) {
  return ExampleFunctionContract(example_struct);
};

// Testing that function references, pointers, structs with operator() and
// lambdas can all be used with ExceptionSafetyTester
TEST(ExceptionSafetyTesterTest, MixedFunctionTypes) {
  // function reference
  EXPECT_TRUE(testing::MakeExceptionSafetyTester()
                  .WithFactory(ExampleFunctionFactory)
                  .WithOperation(ExampleFunctionOperation)
                  .WithContracts(ExampleFunctionContract)
                  .Test());

  // function pointer
  EXPECT_TRUE(testing::MakeExceptionSafetyTester()
                  .WithFactory(&ExampleFunctionFactory)
                  .WithOperation(&ExampleFunctionOperation)
                  .WithContracts(&ExampleFunctionContract)
                  .Test());

  // struct
  EXPECT_TRUE(testing::MakeExceptionSafetyTester()
                  .WithFactory(example_struct_factory)
                  .WithOperation(example_struct_operation)
                  .WithContracts(example_struct_contract)
                  .Test());

  // lambda
  EXPECT_TRUE(testing::MakeExceptionSafetyTester()
                  .WithFactory(example_lambda_factory)
                  .WithOperation(example_lambda_operation)
                  .WithContracts(example_lambda_contract)
                  .Test());
}

struct NonNegative {
  bool operator==(const NonNegative& other) const { return i == other.i; }
  int i;
};

testing::AssertionResult CheckNonNegativeInvariants(NonNegative* g) {
  if (g->i >= 0) {
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure()
         << "i should be non-negative but is " << g->i;
}

struct {
  template <typename T>
  void operator()(T* t) const {
    (*t)();
  }
} invoker;

auto tester =
    testing::MakeExceptionSafetyTester().WithOperation(invoker).WithContracts(
        CheckNonNegativeInvariants);
auto strong_tester = tester.WithContracts(testing::strong_guarantee);

struct FailsBasicGuarantee : public NonNegative {
  void operator()() {
    --i;
    ThrowingValue<> bomb;
    ++i;
  }
};

TEST(ExceptionCheckTest, BasicGuaranteeFailure) {
  EXPECT_FALSE(tester.WithInitialValue(FailsBasicGuarantee{}).Test());
}

struct FollowsBasicGuarantee : public NonNegative {
  void operator()() {
    ++i;
    ThrowingValue<> bomb;
  }
};

TEST(ExceptionCheckTest, BasicGuarantee) {
  EXPECT_TRUE(tester.WithInitialValue(FollowsBasicGuarantee{}).Test());
}

TEST(ExceptionCheckTest, StrongGuaranteeFailure) {
  EXPECT_FALSE(strong_tester.WithInitialValue(FailsBasicGuarantee{}).Test());
  EXPECT_FALSE(strong_tester.WithInitialValue(FollowsBasicGuarantee{}).Test());
}

struct BasicGuaranteeWithExtraContracts : public NonNegative {
  // After operator(), i is incremented.  If operator() throws, i is set to 9999
  void operator()() {
    int old_i = i;
    i = kExceptionSentinel;
    ThrowingValue<> bomb;
    i = ++old_i;
  }

  static constexpr int kExceptionSentinel = 9999;
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr int BasicGuaranteeWithExtraContracts::kExceptionSentinel;
#endif

TEST(ExceptionCheckTest, BasicGuaranteeWithExtraContracts) {
  auto tester_with_val =
      tester.WithInitialValue(BasicGuaranteeWithExtraContracts{});
  EXPECT_TRUE(tester_with_val.Test());
  EXPECT_TRUE(
      tester_with_val
          .WithContracts([](BasicGuaranteeWithExtraContracts* o) {
            if (o->i == BasicGuaranteeWithExtraContracts::kExceptionSentinel) {
              return testing::AssertionSuccess();
            }
            return testing::AssertionFailure()
                   << "i should be "
                   << BasicGuaranteeWithExtraContracts::kExceptionSentinel
                   << ", but is " << o->i;
          })
          .Test());
}

struct FollowsStrongGuarantee : public NonNegative {
  void operator()() { ThrowingValue<> bomb; }
};

TEST(ExceptionCheckTest, StrongGuarantee) {
  EXPECT_TRUE(tester.WithInitialValue(FollowsStrongGuarantee{}).Test());
  EXPECT_TRUE(strong_tester.WithInitialValue(FollowsStrongGuarantee{}).Test());
}

struct HasReset : public NonNegative {
  void operator()() {
    i = -1;
    ThrowingValue<> bomb;
    i = 1;
  }

  void reset() { i = 0; }
};

testing::AssertionResult CheckHasResetContracts(HasReset* h) {
  h->reset();
  return testing::AssertionResult(h->i == 0);
}

TEST(ExceptionCheckTest, ModifyingChecker) {
  auto set_to_1000 = [](FollowsBasicGuarantee* g) {
    g->i = 1000;
    return testing::AssertionSuccess();
  };
  auto is_1000 = [](FollowsBasicGuarantee* g) {
    return testing::AssertionResult(g->i == 1000);
  };
  auto increment = [](FollowsStrongGuarantee* g) {
    ++g->i;
    return testing::AssertionSuccess();
  };

  EXPECT_FALSE(tester.WithInitialValue(FollowsBasicGuarantee{})
                   .WithContracts(set_to_1000, is_1000)
                   .Test());
  EXPECT_TRUE(strong_tester.WithInitialValue(FollowsStrongGuarantee{})
                  .WithContracts(increment)
                  .Test());
  EXPECT_TRUE(testing::MakeExceptionSafetyTester()
                  .WithInitialValue(HasReset{})
                  .WithContracts(CheckHasResetContracts)
                  .Test(invoker));
}

TEST(ExceptionSafetyTesterTest, ResetsCountdown) {
  auto test =
      testing::MakeExceptionSafetyTester()
          .WithInitialValue(ThrowingValue<>())
          .WithContracts([](ThrowingValue<>*) { return AssertionSuccess(); })
          .WithOperation([](ThrowingValue<>*) {});
  ASSERT_TRUE(test.Test());
  // If the countdown isn't reset because there were no exceptions thrown, then
  // this will fail with a termination from an unhandled exception
  EXPECT_TRUE(test.Test());
}

struct NonCopyable : public NonNegative {
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable() : NonNegative{0} {}

  void operator()() { ThrowingValue<> bomb; }
};

TEST(ExceptionCheckTest, NonCopyable) {
  auto factory = []() { return absl::make_unique<NonCopyable>(); };
  EXPECT_TRUE(tester.WithFactory(factory).Test());
  EXPECT_TRUE(strong_tester.WithFactory(factory).Test());
}

struct NonEqualityComparable : public NonNegative {
  void operator()() { ThrowingValue<> bomb; }

  void ModifyOnThrow() {
    ++i;
    ThrowingValue<> bomb;
    static_cast<void>(bomb);
    --i;
  }
};

TEST(ExceptionCheckTest, NonEqualityComparable) {
  auto nec_is_strong = [](NonEqualityComparable* nec) {
    return testing::AssertionResult(nec->i == NonEqualityComparable().i);
  };
  auto strong_nec_tester = tester.WithInitialValue(NonEqualityComparable{})
                               .WithContracts(nec_is_strong);

  EXPECT_TRUE(strong_nec_tester.Test());
  EXPECT_FALSE(strong_nec_tester.Test(
      [](NonEqualityComparable* n) { n->ModifyOnThrow(); }));
}

template <typename T>
struct ExhaustivenessTester {
  void operator()() {
    successes |= 1;
    T b1;
    static_cast<void>(b1);
    successes |= (1 << 1);
    T b2;
    static_cast<void>(b2);
    successes |= (1 << 2);
    T b3;
    static_cast<void>(b3);
    successes |= (1 << 3);
  }

  bool operator==(const ExhaustivenessTester<ThrowingValue<>>&) const {
    return true;
  }

  static unsigned char successes;
};

struct {
  template <typename T>
  testing::AssertionResult operator()(ExhaustivenessTester<T>*) const {
    return testing::AssertionSuccess();
  }
} CheckExhaustivenessTesterContracts;

template <typename T>
unsigned char ExhaustivenessTester<T>::successes = 0;

TEST(ExceptionCheckTest, Exhaustiveness) {
  auto exhaust_tester = testing::MakeExceptionSafetyTester()
                            .WithContracts(CheckExhaustivenessTesterContracts)
                            .WithOperation(invoker);

  EXPECT_TRUE(
      exhaust_tester.WithInitialValue(ExhaustivenessTester<int>{}).Test());
  EXPECT_EQ(ExhaustivenessTester<int>::successes, 0xF);

  EXPECT_TRUE(
      exhaust_tester.WithInitialValue(ExhaustivenessTester<ThrowingValue<>>{})
          .WithContracts(testing::strong_guarantee)
          .Test());
  EXPECT_EQ(ExhaustivenessTester<ThrowingValue<>>::successes, 0xF);
}

struct LeaksIfCtorThrows : private exceptions_internal::TrackedObject {
  LeaksIfCtorThrows() : TrackedObject(ABSL_PRETTY_FUNCTION) {
    ++counter;
    ThrowingValue<> v;
    static_cast<void>(v);
    --counter;
  }
  LeaksIfCtorThrows(const LeaksIfCtorThrows&) noexcept
      : TrackedObject(ABSL_PRETTY_FUNCTION) {}
  static int counter;
};
int LeaksIfCtorThrows::counter = 0;

TEST(ExceptionCheckTest, TestLeakyCtor) {
  testing::TestThrowingCtor<LeaksIfCtorThrows>();
  EXPECT_EQ(LeaksIfCtorThrows::counter, 1);
  LeaksIfCtorThrows::counter = 0;
}

struct Tracked : private exceptions_internal::TrackedObject {
  Tracked() : TrackedObject(ABSL_PRETTY_FUNCTION) {}
};

TEST(ConstructorTrackerTest, CreatedBefore) {
  Tracked a, b, c;
  exceptions_internal::ConstructorTracker ct(exceptions_internal::countdown);
}

TEST(ConstructorTrackerTest, CreatedAfter) {
  exceptions_internal::ConstructorTracker ct(exceptions_internal::countdown);
  Tracked a, b, c;
}

TEST(ConstructorTrackerTest, NotDestroyedAfter) {
  alignas(Tracked) unsigned char storage[sizeof(Tracked)];
  EXPECT_NONFATAL_FAILURE(
      {
        exceptions_internal::ConstructorTracker ct(
            exceptions_internal::countdown);
        new (&storage) Tracked();
      },
      "not destroyed");
}

TEST(ConstructorTrackerTest, DestroyedTwice) {
  exceptions_internal::ConstructorTracker ct(exceptions_internal::countdown);
  EXPECT_NONFATAL_FAILURE(
      {
        Tracked t;
        t.~Tracked();
      },
      "re-destroyed");
}

TEST(ConstructorTrackerTest, ConstructedTwice) {
  exceptions_internal::ConstructorTracker ct(exceptions_internal::countdown);
  alignas(Tracked) unsigned char storage[sizeof(Tracked)];
  EXPECT_NONFATAL_FAILURE(
      {
        new (&storage) Tracked();
        new (&storage) Tracked();
        reinterpret_cast<Tracked*>(&storage)->~Tracked();
      },
      "re-constructed");
}

TEST(ThrowingValueTraitsTest, RelationalOperators) {
  ThrowingValue<> a, b;
  EXPECT_TRUE((std::is_convertible<decltype(a == b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a != b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a < b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a <= b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a > b), bool>::value));
  EXPECT_TRUE((std::is_convertible<decltype(a >= b), bool>::value));
}

TEST(ThrowingAllocatorTraitsTest, Assignablility) {
  EXPECT_TRUE(absl::is_move_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(absl::is_copy_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<ThrowingAllocator<int>>::value);
  EXPECT_TRUE(std::is_nothrow_copy_assignable<ThrowingAllocator<int>>::value);
}

}  // namespace

}  // namespace testing

#endif  // ABSL_HAVE_EXCEPTIONS
