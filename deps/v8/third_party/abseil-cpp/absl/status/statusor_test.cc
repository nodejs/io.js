// Copyright 2020 The Abseil Authors.
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

#include "absl/status/statusor.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/any.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"

namespace {

using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::AnyWith;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::StartsWith;
using ::testing::VariantWith;

#ifdef GTEST_HAS_STATUS_MATCHERS
using ::testing::status::IsOk;
using ::testing::status::IsOkAndHolds;
#else  // GTEST_HAS_STATUS_MATCHERS
inline const ::absl::Status& GetStatus(const ::absl::Status& status) {
  return status;
}

template <typename T>
inline const ::absl::Status& GetStatus(const ::absl::StatusOr<T>& status) {
  return status.status();
}

// Monomorphic implementation of matcher IsOkAndHolds(m).  StatusOrType is a
// reference to StatusOr<T>.
template <typename StatusOrType>
class IsOkAndHoldsMatcherImpl
    : public ::testing::MatcherInterface<StatusOrType> {
 public:
  typedef
      typename std::remove_reference<StatusOrType>::type::value_type value_type;

  template <typename InnerMatcher>
  explicit IsOkAndHoldsMatcherImpl(InnerMatcher&& inner_matcher)
      : inner_matcher_(::testing::SafeMatcherCast<const value_type&>(
            std::forward<InnerMatcher>(inner_matcher))) {}

  void DescribeTo(std::ostream* os) const override {
    *os << "is OK and has a value that ";
    inner_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "isn't OK or has a value that ";
    inner_matcher_.DescribeNegationTo(os);
  }

  bool MatchAndExplain(
      StatusOrType actual_value,
      ::testing::MatchResultListener* result_listener) const override {
    if (!actual_value.ok()) {
      *result_listener << "which has status " << actual_value.status();
      return false;
    }

    ::testing::StringMatchResultListener inner_listener;
    const bool matches =
        inner_matcher_.MatchAndExplain(*actual_value, &inner_listener);
    const std::string inner_explanation = inner_listener.str();
    if (!inner_explanation.empty()) {
      *result_listener << "which contains value "
                       << ::testing::PrintToString(*actual_value) << ", "
                       << inner_explanation;
    }
    return matches;
  }

 private:
  const ::testing::Matcher<const value_type&> inner_matcher_;
};

// Implements IsOkAndHolds(m) as a polymorphic matcher.
template <typename InnerMatcher>
class IsOkAndHoldsMatcher {
 public:
  explicit IsOkAndHoldsMatcher(InnerMatcher inner_matcher)
      : inner_matcher_(std::move(inner_matcher)) {}

  // Converts this polymorphic matcher to a monomorphic matcher of the
  // given type.  StatusOrType can be either StatusOr<T> or a
  // reference to StatusOr<T>.
  template <typename StatusOrType>
  operator ::testing::Matcher<StatusOrType>() const {  // NOLINT
    return ::testing::Matcher<StatusOrType>(
        new IsOkAndHoldsMatcherImpl<const StatusOrType&>(inner_matcher_));
  }

 private:
  const InnerMatcher inner_matcher_;
};

// Monomorphic implementation of matcher IsOk() for a given type T.
// T can be Status, StatusOr<>, or a reference to either of them.
template <typename T>
class MonoIsOkMatcherImpl : public ::testing::MatcherInterface<T> {
 public:
  void DescribeTo(std::ostream* os) const override { *os << "is OK"; }
  void DescribeNegationTo(std::ostream* os) const override {
    *os << "is not OK";
  }
  bool MatchAndExplain(T actual_value,
                       ::testing::MatchResultListener*) const override {
    return GetStatus(actual_value).ok();
  }
};

// Implements IsOk() as a polymorphic matcher.
class IsOkMatcher {
 public:
  template <typename T>
  operator ::testing::Matcher<T>() const {  // NOLINT
    return ::testing::Matcher<T>(new MonoIsOkMatcherImpl<T>());
  }
};

// Macros for testing the results of functions that return absl::Status or
// absl::StatusOr<T> (for any type T).
#define EXPECT_OK(expression) EXPECT_THAT(expression, IsOk())

// Returns a gMock matcher that matches a StatusOr<> whose status is
// OK and whose value matches the inner matcher.
template <typename InnerMatcher>
IsOkAndHoldsMatcher<typename std::decay<InnerMatcher>::type> IsOkAndHolds(
    InnerMatcher&& inner_matcher) {
  return IsOkAndHoldsMatcher<typename std::decay<InnerMatcher>::type>(
      std::forward<InnerMatcher>(inner_matcher));
}

// Returns a gMock matcher that matches a Status or StatusOr<> which is OK.
inline IsOkMatcher IsOk() { return IsOkMatcher(); }
#endif  // GTEST_HAS_STATUS_MATCHERS

struct CopyDetector {
  CopyDetector() = default;
  explicit CopyDetector(int xx) : x(xx) {}
  CopyDetector(CopyDetector&& d) noexcept
      : x(d.x), copied(false), moved(true) {}
  CopyDetector(const CopyDetector& d) : x(d.x), copied(true), moved(false) {}
  CopyDetector& operator=(const CopyDetector& c) {
    x = c.x;
    copied = true;
    moved = false;
    return *this;
  }
  CopyDetector& operator=(CopyDetector&& c) noexcept {
    x = c.x;
    copied = false;
    moved = true;
    return *this;
  }
  int x = 0;
  bool copied = false;
  bool moved = false;
};

testing::Matcher<const CopyDetector&> CopyDetectorHas(int a, bool b, bool c) {
  return AllOf(Field(&CopyDetector::x, a), Field(&CopyDetector::moved, b),
               Field(&CopyDetector::copied, c));
}

class Base1 {
 public:
  virtual ~Base1() {}
  int pad;
};

class Base2 {
 public:
  virtual ~Base2() {}
  int yetotherpad;
};

class Derived : public Base1, public Base2 {
 public:
  virtual ~Derived() {}
  int evenmorepad;
};

class CopyNoAssign {
 public:
  explicit CopyNoAssign(int value) : foo(value) {}
  CopyNoAssign(const CopyNoAssign& other) : foo(other.foo) {}
  int foo;

 private:
  const CopyNoAssign& operator=(const CopyNoAssign&);
};

absl::StatusOr<std::unique_ptr<int>> ReturnUniquePtr() {
  // Uses implicit constructor from T&&
  return absl::make_unique<int>(0);
}

TEST(StatusOr, ElementType) {
  static_assert(std::is_same<absl::StatusOr<int>::value_type, int>(), "");
  static_assert(std::is_same<absl::StatusOr<char>::value_type, char>(), "");
}

TEST(StatusOr, TestMoveOnlyInitialization) {
  absl::StatusOr<std::unique_ptr<int>> thing(ReturnUniquePtr());
  ASSERT_TRUE(thing.ok());
  EXPECT_EQ(0, **thing);
  int* previous = thing->get();

  thing = ReturnUniquePtr();
  EXPECT_TRUE(thing.ok());
  EXPECT_EQ(0, **thing);
  EXPECT_NE(previous, thing->get());
}

TEST(StatusOr, TestMoveOnlyValueExtraction) {
  absl::StatusOr<std::unique_ptr<int>> thing(ReturnUniquePtr());
  ASSERT_TRUE(thing.ok());
  std::unique_ptr<int> ptr = *std::move(thing);
  EXPECT_EQ(0, *ptr);

  thing = std::move(ptr);
  ptr = std::move(*thing);
  EXPECT_EQ(0, *ptr);
}

TEST(StatusOr, TestMoveOnlyInitializationFromTemporaryByValueOrDie) {
  std::unique_ptr<int> ptr(*ReturnUniquePtr());
  EXPECT_EQ(0, *ptr);
}

TEST(StatusOr, TestValueOrDieOverloadForConstTemporary) {
  static_assert(
      std::is_same<
          const int&&,
          decltype(std::declval<const absl::StatusOr<int>&&>().value())>(),
      "value() for const temporaries should return const T&&");
}

TEST(StatusOr, TestMoveOnlyConversion) {
  absl::StatusOr<std::unique_ptr<const int>> const_thing(ReturnUniquePtr());
  EXPECT_TRUE(const_thing.ok());
  EXPECT_EQ(0, **const_thing);

  // Test rvalue converting assignment
  const int* const_previous = const_thing->get();
  const_thing = ReturnUniquePtr();
  EXPECT_TRUE(const_thing.ok());
  EXPECT_EQ(0, **const_thing);
  EXPECT_NE(const_previous, const_thing->get());
}

TEST(StatusOr, TestMoveOnlyVector) {
  // Sanity check that absl::StatusOr<MoveOnly> works in vector.
  std::vector<absl::StatusOr<std::unique_ptr<int>>> vec;
  vec.push_back(ReturnUniquePtr());
  vec.resize(2);
  auto another_vec = std::move(vec);
  EXPECT_EQ(0, **another_vec[0]);
  EXPECT_EQ(absl::UnknownError(""), another_vec[1].status());
}

TEST(StatusOr, TestDefaultCtor) {
  absl::StatusOr<int> thing;
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kUnknown);
}

TEST(StatusOr, StatusCtorForwards) {
  absl::Status status(absl::StatusCode::kInternal, "Some error");

  EXPECT_EQ(absl::StatusOr<int>(status).status().message(), "Some error");
  EXPECT_EQ(status.message(), "Some error");

  EXPECT_EQ(absl::StatusOr<int>(std::move(status)).status().message(),
            "Some error");
  EXPECT_NE(status.message(), "Some error");
}

TEST(BadStatusOrAccessTest, CopyConstructionWhatOk) {
  absl::Status error =
      absl::InternalError("some arbitrary message too big for the sso buffer");
  absl::BadStatusOrAccess e1{error};
  absl::BadStatusOrAccess e2{e1};
  EXPECT_THAT(e1.what(), HasSubstr(error.ToString()));
  EXPECT_THAT(e2.what(), HasSubstr(error.ToString()));
}

TEST(BadStatusOrAccessTest, CopyAssignmentWhatOk) {
  absl::Status error =
      absl::InternalError("some arbitrary message too big for the sso buffer");
  absl::BadStatusOrAccess e1{error};
  absl::BadStatusOrAccess e2{absl::InternalError("other")};
  e2 = e1;
  EXPECT_THAT(e1.what(), HasSubstr(error.ToString()));
  EXPECT_THAT(e2.what(), HasSubstr(error.ToString()));
}

TEST(BadStatusOrAccessTest, MoveConstructionWhatOk) {
  absl::Status error =
      absl::InternalError("some arbitrary message too big for the sso buffer");
  absl::BadStatusOrAccess e1{error};
  absl::BadStatusOrAccess e2{std::move(e1)};
  EXPECT_THAT(e2.what(), HasSubstr(error.ToString()));
}

TEST(BadStatusOrAccessTest, MoveAssignmentWhatOk) {
  absl::Status error =
      absl::InternalError("some arbitrary message too big for the sso buffer");
  absl::BadStatusOrAccess e1{error};
  absl::BadStatusOrAccess e2{absl::InternalError("other")};
  e2 = std::move(e1);
  EXPECT_THAT(e2.what(), HasSubstr(error.ToString()));
}

// Define `EXPECT_DEATH_OR_THROW` to test the behavior of `StatusOr::value`,
// which either throws `BadStatusOrAccess` or `LOG(FATAL)` based on whether
// exceptions are enabled.
#ifdef ABSL_HAVE_EXCEPTIONS
#define EXPECT_DEATH_OR_THROW(statement, status_)                  \
  EXPECT_THROW(                                                    \
      {                                                            \
        try {                                                      \
          statement;                                               \
        } catch (const absl::BadStatusOrAccess& e) {               \
          EXPECT_EQ(e.status(), status_);                          \
          EXPECT_THAT(e.what(), HasSubstr(e.status().ToString())); \
          throw;                                                   \
        }                                                          \
      },                                                           \
      absl::BadStatusOrAccess);
#else  // ABSL_HAVE_EXCEPTIONS
#define EXPECT_DEATH_OR_THROW(statement, status) \
  EXPECT_DEATH_IF_SUPPORTED(statement, status.ToString());
#endif  // ABSL_HAVE_EXCEPTIONS

TEST(StatusOrDeathTest, TestDefaultCtorValue) {
  absl::StatusOr<int> thing;
  EXPECT_DEATH_OR_THROW(thing.value(), absl::UnknownError(""));
  const absl::StatusOr<int> thing2;
  EXPECT_DEATH_OR_THROW(thing2.value(), absl::UnknownError(""));
}

TEST(StatusOrDeathTest, TestValueNotOk) {
  absl::StatusOr<int> thing(absl::CancelledError());
  EXPECT_DEATH_OR_THROW(thing.value(), absl::CancelledError());
}

TEST(StatusOrDeathTest, TestValueNotOkConst) {
  const absl::StatusOr<int> thing(absl::UnknownError(""));
  EXPECT_DEATH_OR_THROW(thing.value(), absl::UnknownError(""));
}

TEST(StatusOrDeathTest, TestPointerDefaultCtorValue) {
  absl::StatusOr<int*> thing;
  EXPECT_DEATH_OR_THROW(thing.value(), absl::UnknownError(""));
}

TEST(StatusOrDeathTest, TestPointerValueNotOk) {
  absl::StatusOr<int*> thing(absl::CancelledError());
  EXPECT_DEATH_OR_THROW(thing.value(), absl::CancelledError());
}

TEST(StatusOrDeathTest, TestPointerValueNotOkConst) {
  const absl::StatusOr<int*> thing(absl::CancelledError());
  EXPECT_DEATH_OR_THROW(thing.value(), absl::CancelledError());
}

#if GTEST_HAS_DEATH_TEST
TEST(StatusOrDeathTest, TestStatusCtorStatusOk) {
  EXPECT_DEBUG_DEATH(
      {
        // This will DCHECK
        absl::StatusOr<int> thing(absl::OkStatus());
        // In optimized mode, we are actually going to get error::INTERNAL for
        // status here, rather than crashing, so check that.
        EXPECT_FALSE(thing.ok());
        EXPECT_EQ(thing.status().code(), absl::StatusCode::kInternal);
      },
      "An OK status is not a valid constructor argument");
}

TEST(StatusOrDeathTest, TestPointerStatusCtorStatusOk) {
  EXPECT_DEBUG_DEATH(
      {
        absl::StatusOr<int*> thing(absl::OkStatus());
        // In optimized mode, we are actually going to get error::INTERNAL for
        // status here, rather than crashing, so check that.
        EXPECT_FALSE(thing.ok());
        EXPECT_EQ(thing.status().code(), absl::StatusCode::kInternal);
      },
      "An OK status is not a valid constructor argument");
}
#endif

TEST(StatusOr, ValueAccessor) {
  const int kIntValue = 110;
  {
    absl::StatusOr<int> status_or(kIntValue);
    EXPECT_EQ(kIntValue, status_or.value());
    EXPECT_EQ(kIntValue, std::move(status_or).value());
  }
  {
    absl::StatusOr<CopyDetector> status_or(kIntValue);
    EXPECT_THAT(status_or,
                IsOkAndHolds(CopyDetectorHas(kIntValue, false, false)));
    CopyDetector copy_detector = status_or.value();
    EXPECT_THAT(copy_detector, CopyDetectorHas(kIntValue, false, true));
    copy_detector = std::move(status_or).value();
    EXPECT_THAT(copy_detector, CopyDetectorHas(kIntValue, true, false));
  }
}

TEST(StatusOr, BadValueAccess) {
  const absl::Status kError = absl::CancelledError("message");
  absl::StatusOr<int> status_or(kError);
  EXPECT_DEATH_OR_THROW(status_or.value(), kError);
}

TEST(StatusOr, TestStatusCtor) {
  absl::StatusOr<int> thing(absl::CancelledError());
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestValueCtor) {
  const int kI = 4;
  const absl::StatusOr<int> thing(kI);
  EXPECT_TRUE(thing.ok());
  EXPECT_EQ(kI, *thing);
}

struct Foo {
  const int x;
  explicit Foo(int y) : x(y) {}
};

TEST(StatusOr, InPlaceConstruction) {
  EXPECT_THAT(absl::StatusOr<Foo>(absl::in_place, 10),
              IsOkAndHolds(Field(&Foo::x, 10)));
}

struct InPlaceHelper {
  InPlaceHelper(std::initializer_list<int> xs, std::unique_ptr<int> yy)
      : x(xs), y(std::move(yy)) {}
  const std::vector<int> x;
  std::unique_ptr<int> y;
};

TEST(StatusOr, InPlaceInitListConstruction) {
  absl::StatusOr<InPlaceHelper> status_or(absl::in_place, {10, 11, 12},
                                          absl::make_unique<int>(13));
  EXPECT_THAT(status_or, IsOkAndHolds(AllOf(
                             Field(&InPlaceHelper::x, ElementsAre(10, 11, 12)),
                             Field(&InPlaceHelper::y, Pointee(13)))));
}

TEST(StatusOr, Emplace) {
  absl::StatusOr<Foo> status_or_foo(10);
  status_or_foo.emplace(20);
  EXPECT_THAT(status_or_foo, IsOkAndHolds(Field(&Foo::x, 20)));
  status_or_foo = absl::InvalidArgumentError("msg");
  EXPECT_FALSE(status_or_foo.ok());
  EXPECT_EQ(status_or_foo.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status_or_foo.status().message(), "msg");
  status_or_foo.emplace(20);
  EXPECT_THAT(status_or_foo, IsOkAndHolds(Field(&Foo::x, 20)));
}

TEST(StatusOr, EmplaceInitializerList) {
  absl::StatusOr<InPlaceHelper> status_or(absl::in_place, {10, 11, 12},
                                          absl::make_unique<int>(13));
  status_or.emplace({1, 2, 3}, absl::make_unique<int>(4));
  EXPECT_THAT(status_or,
              IsOkAndHolds(AllOf(Field(&InPlaceHelper::x, ElementsAre(1, 2, 3)),
                                 Field(&InPlaceHelper::y, Pointee(4)))));
  status_or = absl::InvalidArgumentError("msg");
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status_or.status().message(), "msg");
  status_or.emplace({1, 2, 3}, absl::make_unique<int>(4));
  EXPECT_THAT(status_or,
              IsOkAndHolds(AllOf(Field(&InPlaceHelper::x, ElementsAre(1, 2, 3)),
                                 Field(&InPlaceHelper::y, Pointee(4)))));
}

TEST(StatusOr, TestCopyCtorStatusOk) {
  const int kI = 4;
  const absl::StatusOr<int> original(kI);
  const absl::StatusOr<int> copy(original);
  EXPECT_OK(copy.status());
  EXPECT_EQ(*original, *copy);
}

TEST(StatusOr, TestCopyCtorStatusNotOk) {
  absl::StatusOr<int> original(absl::CancelledError());
  absl::StatusOr<int> copy(original);
  EXPECT_EQ(copy.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestCopyCtorNonAssignable) {
  const int kI = 4;
  CopyNoAssign value(kI);
  absl::StatusOr<CopyNoAssign> original(value);
  absl::StatusOr<CopyNoAssign> copy(original);
  EXPECT_OK(copy.status());
  EXPECT_EQ(original->foo, copy->foo);
}

TEST(StatusOr, TestCopyCtorStatusOKConverting) {
  const int kI = 4;
  absl::StatusOr<int> original(kI);
  absl::StatusOr<double> copy(original);
  EXPECT_OK(copy.status());
  EXPECT_DOUBLE_EQ(*original, *copy);
}

TEST(StatusOr, TestCopyCtorStatusNotOkConverting) {
  absl::StatusOr<int> original(absl::CancelledError());
  absl::StatusOr<double> copy(original);
  EXPECT_EQ(copy.status(), original.status());
}

TEST(StatusOr, TestAssignmentStatusOk) {
  // Copy assignmment
  {
    const auto p = std::make_shared<int>(17);
    absl::StatusOr<std::shared_ptr<int>> source(p);

    absl::StatusOr<std::shared_ptr<int>> target;
    target = source;

    ASSERT_TRUE(target.ok());
    EXPECT_OK(target.status());
    EXPECT_EQ(p, *target);

    ASSERT_TRUE(source.ok());
    EXPECT_OK(source.status());
    EXPECT_EQ(p, *source);
  }

  // Move asssignment
  {
    const auto p = std::make_shared<int>(17);
    absl::StatusOr<std::shared_ptr<int>> source(p);

    absl::StatusOr<std::shared_ptr<int>> target;
    target = std::move(source);

    ASSERT_TRUE(target.ok());
    EXPECT_OK(target.status());
    EXPECT_EQ(p, *target);

    ASSERT_TRUE(source.ok());
    EXPECT_OK(source.status());
    EXPECT_EQ(nullptr, *source);
  }
}

TEST(StatusOr, TestAssignmentStatusNotOk) {
  // Copy assignment
  {
    const absl::Status expected = absl::CancelledError();
    absl::StatusOr<int> source(expected);

    absl::StatusOr<int> target;
    target = source;

    EXPECT_FALSE(target.ok());
    EXPECT_EQ(expected, target.status());

    EXPECT_FALSE(source.ok());
    EXPECT_EQ(expected, source.status());
  }

  // Move assignment
  {
    const absl::Status expected = absl::CancelledError();
    absl::StatusOr<int> source(expected);

    absl::StatusOr<int> target;
    target = std::move(source);

    EXPECT_FALSE(target.ok());
    EXPECT_EQ(expected, target.status());

    EXPECT_FALSE(source.ok());
    EXPECT_EQ(source.status().code(), absl::StatusCode::kInternal);
  }
}

TEST(StatusOr, TestAssignmentStatusOKConverting) {
  // Copy assignment
  {
    const int kI = 4;
    absl::StatusOr<int> source(kI);

    absl::StatusOr<double> target;
    target = source;

    ASSERT_TRUE(target.ok());
    EXPECT_OK(target.status());
    EXPECT_DOUBLE_EQ(kI, *target);

    ASSERT_TRUE(source.ok());
    EXPECT_OK(source.status());
    EXPECT_DOUBLE_EQ(kI, *source);
  }

  // Move assignment
  {
    const auto p = new int(17);
    absl::StatusOr<std::unique_ptr<int>> source(absl::WrapUnique(p));

    absl::StatusOr<std::shared_ptr<int>> target;
    target = std::move(source);

    ASSERT_TRUE(target.ok());
    EXPECT_OK(target.status());
    EXPECT_EQ(p, target->get());

    ASSERT_TRUE(source.ok());
    EXPECT_OK(source.status());
    EXPECT_EQ(nullptr, source->get());
  }
}

struct A {
  int x;
};

struct ImplicitConstructibleFromA {
  int x;
  bool moved;
  ImplicitConstructibleFromA(const A& a)  // NOLINT
      : x(a.x), moved(false) {}
  ImplicitConstructibleFromA(A&& a)  // NOLINT
      : x(a.x), moved(true) {}
};

TEST(StatusOr, ImplicitConvertingConstructor) {
  EXPECT_THAT(
      absl::implicit_cast<absl::StatusOr<ImplicitConstructibleFromA>>(
          absl::StatusOr<A>(A{11})),
      IsOkAndHolds(AllOf(Field(&ImplicitConstructibleFromA::x, 11),
                         Field(&ImplicitConstructibleFromA::moved, true))));
  absl::StatusOr<A> a(A{12});
  EXPECT_THAT(
      absl::implicit_cast<absl::StatusOr<ImplicitConstructibleFromA>>(a),
      IsOkAndHolds(AllOf(Field(&ImplicitConstructibleFromA::x, 12),
                         Field(&ImplicitConstructibleFromA::moved, false))));
}

struct ExplicitConstructibleFromA {
  int x;
  bool moved;
  explicit ExplicitConstructibleFromA(const A& a) : x(a.x), moved(false) {}
  explicit ExplicitConstructibleFromA(A&& a) : x(a.x), moved(true) {}
};

TEST(StatusOr, ExplicitConvertingConstructor) {
  EXPECT_FALSE(
      (std::is_convertible<const absl::StatusOr<A>&,
                           absl::StatusOr<ExplicitConstructibleFromA>>::value));
  EXPECT_FALSE(
      (std::is_convertible<absl::StatusOr<A>&&,
                           absl::StatusOr<ExplicitConstructibleFromA>>::value));
  EXPECT_THAT(
      absl::StatusOr<ExplicitConstructibleFromA>(absl::StatusOr<A>(A{11})),
      IsOkAndHolds(AllOf(Field(&ExplicitConstructibleFromA::x, 11),
                         Field(&ExplicitConstructibleFromA::moved, true))));
  absl::StatusOr<A> a(A{12});
  EXPECT_THAT(
      absl::StatusOr<ExplicitConstructibleFromA>(a),
      IsOkAndHolds(AllOf(Field(&ExplicitConstructibleFromA::x, 12),
                         Field(&ExplicitConstructibleFromA::moved, false))));
}

struct ImplicitConstructibleFromBool {
  ImplicitConstructibleFromBool(bool y) : x(y) {}  // NOLINT
  bool x = false;
};

struct ConvertibleToBool {
  explicit ConvertibleToBool(bool y) : x(y) {}
  operator bool() const { return x; }  // NOLINT
  bool x = false;
};

TEST(StatusOr, ImplicitBooleanConstructionWithImplicitCasts) {
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<ConvertibleToBool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<ConvertibleToBool>(false)),
              IsOkAndHolds(false));
  EXPECT_THAT(
      absl::implicit_cast<absl::StatusOr<ImplicitConstructibleFromBool>>(
          absl::StatusOr<bool>(false)),
      IsOkAndHolds(Field(&ImplicitConstructibleFromBool::x, false)));
  EXPECT_FALSE((std::is_convertible<
                absl::StatusOr<ConvertibleToBool>,
                absl::StatusOr<ImplicitConstructibleFromBool>>::value));
}

TEST(StatusOr, BooleanConstructionWithImplicitCasts) {
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<ConvertibleToBool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<ConvertibleToBool>(false)),
              IsOkAndHolds(false));
  EXPECT_THAT(
      absl::StatusOr<ImplicitConstructibleFromBool>{
          absl::StatusOr<bool>(false)},
      IsOkAndHolds(Field(&ImplicitConstructibleFromBool::x, false)));
  EXPECT_THAT(
      absl::StatusOr<ImplicitConstructibleFromBool>{
          absl::StatusOr<bool>(absl::InvalidArgumentError(""))},
      Not(IsOk()));

  EXPECT_THAT(
      absl::StatusOr<ImplicitConstructibleFromBool>{
          absl::StatusOr<ConvertibleToBool>(ConvertibleToBool{false})},
      IsOkAndHolds(Field(&ImplicitConstructibleFromBool::x, false)));
  EXPECT_THAT(
      absl::StatusOr<ImplicitConstructibleFromBool>{
          absl::StatusOr<ConvertibleToBool>(absl::InvalidArgumentError(""))},
      Not(IsOk()));
}

TEST(StatusOr, ConstImplicitCast) {
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<bool>>(
                  absl::StatusOr<const bool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<bool>>(
                  absl::StatusOr<const bool>(false)),
              IsOkAndHolds(false));
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<const bool>>(
                  absl::StatusOr<bool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<const bool>>(
                  absl::StatusOr<bool>(false)),
              IsOkAndHolds(false));
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<const std::string>>(
                  absl::StatusOr<std::string>("foo")),
              IsOkAndHolds("foo"));
  EXPECT_THAT(absl::implicit_cast<absl::StatusOr<std::string>>(
                  absl::StatusOr<const std::string>("foo")),
              IsOkAndHolds("foo"));
  EXPECT_THAT(
      absl::implicit_cast<absl::StatusOr<std::shared_ptr<const std::string>>>(
          absl::StatusOr<std::shared_ptr<std::string>>(
              std::make_shared<std::string>("foo"))),
      IsOkAndHolds(Pointee(std::string("foo"))));
}

TEST(StatusOr, ConstExplicitConstruction) {
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<const bool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::StatusOr<bool>(absl::StatusOr<const bool>(false)),
              IsOkAndHolds(false));
  EXPECT_THAT(absl::StatusOr<const bool>(absl::StatusOr<bool>(true)),
              IsOkAndHolds(true));
  EXPECT_THAT(absl::StatusOr<const bool>(absl::StatusOr<bool>(false)),
              IsOkAndHolds(false));
}

struct ExplicitConstructibleFromInt {
  int x;
  explicit ExplicitConstructibleFromInt(int y) : x(y) {}
};

TEST(StatusOr, ExplicitConstruction) {
  EXPECT_THAT(absl::StatusOr<ExplicitConstructibleFromInt>(10),
              IsOkAndHolds(Field(&ExplicitConstructibleFromInt::x, 10)));
}

TEST(StatusOr, ImplicitConstruction) {
  // Check implicit casting works.
  auto status_or =
      absl::implicit_cast<absl::StatusOr<absl::variant<int, std::string>>>(10);
  EXPECT_THAT(status_or, IsOkAndHolds(VariantWith<int>(10)));
}

TEST(StatusOr, ImplicitConstructionFromInitliazerList) {
  // Note: dropping the explicit std::initializer_list<int> is not supported
  // by absl::StatusOr or absl::optional.
  auto status_or =
      absl::implicit_cast<absl::StatusOr<std::vector<int>>>({{10, 20, 30}});
  EXPECT_THAT(status_or, IsOkAndHolds(ElementsAre(10, 20, 30)));
}

TEST(StatusOr, UniquePtrImplicitConstruction) {
  auto status_or = absl::implicit_cast<absl::StatusOr<std::unique_ptr<Base1>>>(
      absl::make_unique<Derived>());
  EXPECT_THAT(status_or, IsOkAndHolds(Ne(nullptr)));
}

TEST(StatusOr, NestedStatusOrCopyAndMoveConstructorTests) {
  absl::StatusOr<absl::StatusOr<CopyDetector>> status_or = CopyDetector(10);
  absl::StatusOr<absl::StatusOr<CopyDetector>> status_error =
      absl::InvalidArgumentError("foo");
  EXPECT_THAT(status_or,
              IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, true, false))));
  absl::StatusOr<absl::StatusOr<CopyDetector>> a = status_or;
  EXPECT_THAT(a, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, false, true))));
  absl::StatusOr<absl::StatusOr<CopyDetector>> a_err = status_error;
  EXPECT_THAT(a_err, Not(IsOk()));

  const absl::StatusOr<absl::StatusOr<CopyDetector>>& cref = status_or;
  absl::StatusOr<absl::StatusOr<CopyDetector>> b = cref;  // NOLINT
  EXPECT_THAT(b, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, false, true))));
  const absl::StatusOr<absl::StatusOr<CopyDetector>>& cref_err = status_error;
  absl::StatusOr<absl::StatusOr<CopyDetector>> b_err = cref_err;  // NOLINT
  EXPECT_THAT(b_err, Not(IsOk()));

  absl::StatusOr<absl::StatusOr<CopyDetector>> c = std::move(status_or);
  EXPECT_THAT(c, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, true, false))));
  absl::StatusOr<absl::StatusOr<CopyDetector>> c_err = std::move(status_error);
  EXPECT_THAT(c_err, Not(IsOk()));
}

TEST(StatusOr, NestedStatusOrCopyAndMoveAssignment) {
  absl::StatusOr<absl::StatusOr<CopyDetector>> status_or = CopyDetector(10);
  absl::StatusOr<absl::StatusOr<CopyDetector>> status_error =
      absl::InvalidArgumentError("foo");
  absl::StatusOr<absl::StatusOr<CopyDetector>> a;
  a = status_or;
  EXPECT_THAT(a, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, false, true))));
  a = status_error;
  EXPECT_THAT(a, Not(IsOk()));

  const absl::StatusOr<absl::StatusOr<CopyDetector>>& cref = status_or;
  a = cref;
  EXPECT_THAT(a, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, false, true))));
  const absl::StatusOr<absl::StatusOr<CopyDetector>>& cref_err = status_error;
  a = cref_err;
  EXPECT_THAT(a, Not(IsOk()));
  a = std::move(status_or);
  EXPECT_THAT(a, IsOkAndHolds(IsOkAndHolds(CopyDetectorHas(10, true, false))));
  a = std::move(status_error);
  EXPECT_THAT(a, Not(IsOk()));
}

struct Copyable {
  Copyable() {}
  Copyable(const Copyable&) {}
  Copyable& operator=(const Copyable&) { return *this; }
};

struct MoveOnly {
  MoveOnly() {}
  MoveOnly(MoveOnly&&) {}
  MoveOnly& operator=(MoveOnly&&) { return *this; }
};

struct NonMovable {
  NonMovable() {}
  NonMovable(const NonMovable&) = delete;
  NonMovable(NonMovable&&) = delete;
  NonMovable& operator=(const NonMovable&) = delete;
  NonMovable& operator=(NonMovable&&) = delete;
};

TEST(StatusOr, CopyAndMoveAbility) {
  EXPECT_TRUE(std::is_copy_constructible<Copyable>::value);
  EXPECT_TRUE(std::is_copy_assignable<Copyable>::value);
  EXPECT_TRUE(std::is_move_constructible<Copyable>::value);
  EXPECT_TRUE(std::is_move_assignable<Copyable>::value);
  EXPECT_FALSE(std::is_copy_constructible<MoveOnly>::value);
  EXPECT_FALSE(std::is_copy_assignable<MoveOnly>::value);
  EXPECT_TRUE(std::is_move_constructible<MoveOnly>::value);
  EXPECT_TRUE(std::is_move_assignable<MoveOnly>::value);
  EXPECT_FALSE(std::is_copy_constructible<NonMovable>::value);
  EXPECT_FALSE(std::is_copy_assignable<NonMovable>::value);
  EXPECT_FALSE(std::is_move_constructible<NonMovable>::value);
  EXPECT_FALSE(std::is_move_assignable<NonMovable>::value);
}

TEST(StatusOr, StatusOrAnyCopyAndMoveConstructorTests) {
  absl::StatusOr<absl::any> status_or = CopyDetector(10);
  absl::StatusOr<absl::any> status_error = absl::InvalidArgumentError("foo");
  EXPECT_THAT(
      status_or,
      IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, true, false))));
  absl::StatusOr<absl::any> a = status_or;
  EXPECT_THAT(
      a, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, false, true))));
  absl::StatusOr<absl::any> a_err = status_error;
  EXPECT_THAT(a_err, Not(IsOk()));

  const absl::StatusOr<absl::any>& cref = status_or;
  // No lint for no-change copy.
  absl::StatusOr<absl::any> b = cref;  // NOLINT
  EXPECT_THAT(
      b, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, false, true))));
  const absl::StatusOr<absl::any>& cref_err = status_error;
  // No lint for no-change copy.
  absl::StatusOr<absl::any> b_err = cref_err;  // NOLINT
  EXPECT_THAT(b_err, Not(IsOk()));

  absl::StatusOr<absl::any> c = std::move(status_or);
  EXPECT_THAT(
      c, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, true, false))));
  absl::StatusOr<absl::any> c_err = std::move(status_error);
  EXPECT_THAT(c_err, Not(IsOk()));
}

TEST(StatusOr, StatusOrAnyCopyAndMoveAssignment) {
  absl::StatusOr<absl::any> status_or = CopyDetector(10);
  absl::StatusOr<absl::any> status_error = absl::InvalidArgumentError("foo");
  absl::StatusOr<absl::any> a;
  a = status_or;
  EXPECT_THAT(
      a, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, false, true))));
  a = status_error;
  EXPECT_THAT(a, Not(IsOk()));

  const absl::StatusOr<absl::any>& cref = status_or;
  a = cref;
  EXPECT_THAT(
      a, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, false, true))));
  const absl::StatusOr<absl::any>& cref_err = status_error;
  a = cref_err;
  EXPECT_THAT(a, Not(IsOk()));
  a = std::move(status_or);
  EXPECT_THAT(
      a, IsOkAndHolds(AnyWith<CopyDetector>(CopyDetectorHas(10, true, false))));
  a = std::move(status_error);
  EXPECT_THAT(a, Not(IsOk()));
}

TEST(StatusOr, StatusOrCopyAndMoveTestsConstructor) {
  absl::StatusOr<CopyDetector> status_or(10);
  ASSERT_THAT(status_or, IsOkAndHolds(CopyDetectorHas(10, false, false)));
  absl::StatusOr<CopyDetector> a(status_or);
  EXPECT_THAT(a, IsOkAndHolds(CopyDetectorHas(10, false, true)));
  const absl::StatusOr<CopyDetector>& cref = status_or;
  absl::StatusOr<CopyDetector> b(cref);  // NOLINT
  EXPECT_THAT(b, IsOkAndHolds(CopyDetectorHas(10, false, true)));
  absl::StatusOr<CopyDetector> c(std::move(status_or));
  EXPECT_THAT(c, IsOkAndHolds(CopyDetectorHas(10, true, false)));
}

TEST(StatusOr, StatusOrCopyAndMoveTestsAssignment) {
  absl::StatusOr<CopyDetector> status_or(10);
  ASSERT_THAT(status_or, IsOkAndHolds(CopyDetectorHas(10, false, false)));
  absl::StatusOr<CopyDetector> a;
  a = status_or;
  EXPECT_THAT(a, IsOkAndHolds(CopyDetectorHas(10, false, true)));
  const absl::StatusOr<CopyDetector>& cref = status_or;
  absl::StatusOr<CopyDetector> b;
  b = cref;
  EXPECT_THAT(b, IsOkAndHolds(CopyDetectorHas(10, false, true)));
  absl::StatusOr<CopyDetector> c;
  c = std::move(status_or);
  EXPECT_THAT(c, IsOkAndHolds(CopyDetectorHas(10, true, false)));
}

TEST(StatusOr, AbslAnyAssignment) {
  EXPECT_FALSE((std::is_assignable<absl::StatusOr<absl::any>,
                                   absl::StatusOr<int>>::value));
  absl::StatusOr<absl::any> status_or;
  status_or = absl::InvalidArgumentError("foo");
  EXPECT_THAT(status_or, Not(IsOk()));
}

TEST(StatusOr, ImplicitAssignment) {
  absl::StatusOr<absl::variant<int, std::string>> status_or;
  status_or = 10;
  EXPECT_THAT(status_or, IsOkAndHolds(VariantWith<int>(10)));
}

TEST(StatusOr, SelfDirectInitAssignment) {
  absl::StatusOr<std::vector<int>> status_or = {{10, 20, 30}};
  status_or = *status_or;
  EXPECT_THAT(status_or, IsOkAndHolds(ElementsAre(10, 20, 30)));
}

TEST(StatusOr, ImplicitCastFromInitializerList) {
  absl::StatusOr<std::vector<int>> status_or = {{10, 20, 30}};
  EXPECT_THAT(status_or, IsOkAndHolds(ElementsAre(10, 20, 30)));
}

TEST(StatusOr, UniquePtrImplicitAssignment) {
  absl::StatusOr<std::unique_ptr<Base1>> status_or;
  status_or = absl::make_unique<Derived>();
  EXPECT_THAT(status_or, IsOkAndHolds(Ne(nullptr)));
}

TEST(StatusOr, Pointer) {
  struct A {};
  struct B : public A {};
  struct C : private A {};

  EXPECT_TRUE((std::is_constructible<absl::StatusOr<A*>, B*>::value));
  EXPECT_TRUE((std::is_convertible<B*, absl::StatusOr<A*>>::value));
  EXPECT_FALSE((std::is_constructible<absl::StatusOr<A*>, C*>::value));
  EXPECT_FALSE((std::is_convertible<C*, absl::StatusOr<A*>>::value));
}

TEST(StatusOr, TestAssignmentStatusNotOkConverting) {
  // Copy assignment
  {
    const absl::Status expected = absl::CancelledError();
    absl::StatusOr<int> source(expected);

    absl::StatusOr<double> target;
    target = source;

    EXPECT_FALSE(target.ok());
    EXPECT_EQ(expected, target.status());

    EXPECT_FALSE(source.ok());
    EXPECT_EQ(expected, source.status());
  }

  // Move assignment
  {
    const absl::Status expected = absl::CancelledError();
    absl::StatusOr<int> source(expected);

    absl::StatusOr<double> target;
    target = std::move(source);

    EXPECT_FALSE(target.ok());
    EXPECT_EQ(expected, target.status());

    EXPECT_FALSE(source.ok());
    EXPECT_EQ(source.status().code(), absl::StatusCode::kInternal);
  }
}

TEST(StatusOr, SelfAssignment) {
  // Copy-assignment, status OK
  {
    // A string long enough that it's likely to defeat any inline representation
    // optimization.
    const std::string long_str(128, 'a');

    absl::StatusOr<std::string> so = long_str;
    so = *&so;

    ASSERT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(long_str, *so);
  }

  // Copy-assignment, error status
  {
    absl::StatusOr<int> so = absl::NotFoundError("taco");
    so = *&so;

    EXPECT_FALSE(so.ok());
    EXPECT_EQ(so.status().code(), absl::StatusCode::kNotFound);
    EXPECT_EQ(so.status().message(), "taco");
  }

  // Move-assignment with copyable type, status OK
  {
    absl::StatusOr<int> so = 17;

    // Fool the compiler, which otherwise complains.
    auto& same = so;
    so = std::move(same);

    ASSERT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(17, *so);
  }

  // Move-assignment with copyable type, error status
  {
    absl::StatusOr<int> so = absl::NotFoundError("taco");

    // Fool the compiler, which otherwise complains.
    auto& same = so;
    so = std::move(same);

    EXPECT_FALSE(so.ok());
    EXPECT_EQ(so.status().code(), absl::StatusCode::kNotFound);
    EXPECT_EQ(so.status().message(), "taco");
  }

  // Move-assignment with non-copyable type, status OK
  {
    const auto raw = new int(17);
    absl::StatusOr<std::unique_ptr<int>> so = absl::WrapUnique(raw);

    // Fool the compiler, which otherwise complains.
    auto& same = so;
    so = std::move(same);

    ASSERT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(raw, so->get());
  }

  // Move-assignment with non-copyable type, error status
  {
    absl::StatusOr<std::unique_ptr<int>> so = absl::NotFoundError("taco");

    // Fool the compiler, which otherwise complains.
    auto& same = so;
    so = std::move(same);

    EXPECT_FALSE(so.ok());
    EXPECT_EQ(so.status().code(), absl::StatusCode::kNotFound);
    EXPECT_EQ(so.status().message(), "taco");
  }
}

// These types form the overload sets of the constructors and the assignment
// operators of `MockValue`. They distinguish construction from assignment,
// lvalue from rvalue.
struct FromConstructibleAssignableLvalue {};
struct FromConstructibleAssignableRvalue {};
struct FromImplicitConstructibleOnly {};
struct FromAssignableOnly {};

// This class is for testing the forwarding value assignments of `StatusOr`.
// `from_rvalue` indicates whether the constructor or the assignment taking
// rvalue reference is called. `from_assignment` indicates whether any
// assignment is called.
struct MockValue {
  // Constructs `MockValue` from `FromConstructibleAssignableLvalue`.
  MockValue(const FromConstructibleAssignableLvalue&)  // NOLINT
      : from_rvalue(false), assigned(false) {}
  // Constructs `MockValue` from `FromConstructibleAssignableRvalue`.
  MockValue(FromConstructibleAssignableRvalue&&)  // NOLINT
      : from_rvalue(true), assigned(false) {}
  // Constructs `MockValue` from `FromImplicitConstructibleOnly`.
  // `MockValue` is not assignable from `FromImplicitConstructibleOnly`.
  MockValue(const FromImplicitConstructibleOnly&)  // NOLINT
      : from_rvalue(false), assigned(false) {}
  // Assigns `FromConstructibleAssignableLvalue`.
  MockValue& operator=(const FromConstructibleAssignableLvalue&) {
    from_rvalue = false;
    assigned = true;
    return *this;
  }
  // Assigns `FromConstructibleAssignableRvalue` (rvalue only).
  MockValue& operator=(FromConstructibleAssignableRvalue&&) {
    from_rvalue = true;
    assigned = true;
    return *this;
  }
  // Assigns `FromAssignableOnly`, but not constructible from
  // `FromAssignableOnly`.
  MockValue& operator=(const FromAssignableOnly&) {
    from_rvalue = false;
    assigned = true;
    return *this;
  }
  bool from_rvalue;
  bool assigned;
};

// operator=(U&&)
TEST(StatusOr, PerfectForwardingAssignment) {
  // U == T
  constexpr int kValue1 = 10, kValue2 = 20;
  absl::StatusOr<CopyDetector> status_or;
  CopyDetector lvalue(kValue1);
  status_or = lvalue;
  EXPECT_THAT(status_or, IsOkAndHolds(CopyDetectorHas(kValue1, false, true)));
  status_or = CopyDetector(kValue2);
  EXPECT_THAT(status_or, IsOkAndHolds(CopyDetectorHas(kValue2, true, false)));

  // U != T
  EXPECT_TRUE(
      (std::is_assignable<absl::StatusOr<MockValue>&,
                          const FromConstructibleAssignableLvalue&>::value));
  EXPECT_TRUE((std::is_assignable<absl::StatusOr<MockValue>&,
                                  FromConstructibleAssignableLvalue&&>::value));
  EXPECT_FALSE(
      (std::is_assignable<absl::StatusOr<MockValue>&,
                          const FromConstructibleAssignableRvalue&>::value));
  EXPECT_TRUE((std::is_assignable<absl::StatusOr<MockValue>&,
                                  FromConstructibleAssignableRvalue&&>::value));
  EXPECT_TRUE(
      (std::is_assignable<absl::StatusOr<MockValue>&,
                          const FromImplicitConstructibleOnly&>::value));
  EXPECT_FALSE((std::is_assignable<absl::StatusOr<MockValue>&,
                                   const FromAssignableOnly&>::value));

  absl::StatusOr<MockValue> from_lvalue(FromConstructibleAssignableLvalue{});
  EXPECT_FALSE(from_lvalue->from_rvalue);
  EXPECT_FALSE(from_lvalue->assigned);
  from_lvalue = FromConstructibleAssignableLvalue{};
  EXPECT_FALSE(from_lvalue->from_rvalue);
  EXPECT_TRUE(from_lvalue->assigned);

  absl::StatusOr<MockValue> from_rvalue(FromConstructibleAssignableRvalue{});
  EXPECT_TRUE(from_rvalue->from_rvalue);
  EXPECT_FALSE(from_rvalue->assigned);
  from_rvalue = FromConstructibleAssignableRvalue{};
  EXPECT_TRUE(from_rvalue->from_rvalue);
  EXPECT_TRUE(from_rvalue->assigned);

  absl::StatusOr<MockValue> from_implicit_constructible(
      FromImplicitConstructibleOnly{});
  EXPECT_FALSE(from_implicit_constructible->from_rvalue);
  EXPECT_FALSE(from_implicit_constructible->assigned);
  // construct a temporary `StatusOr` object and invoke the `StatusOr` move
  // assignment operator.
  from_implicit_constructible = FromImplicitConstructibleOnly{};
  EXPECT_FALSE(from_implicit_constructible->from_rvalue);
  EXPECT_FALSE(from_implicit_constructible->assigned);
}

TEST(StatusOr, TestStatus) {
  absl::StatusOr<int> good(4);
  EXPECT_TRUE(good.ok());
  absl::StatusOr<int> bad(absl::CancelledError());
  EXPECT_FALSE(bad.ok());
  EXPECT_EQ(bad.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, OperatorStarRefQualifiers) {
  static_assert(
      std::is_same<const int&,
                   decltype(*std::declval<const absl::StatusOr<int>&>())>(),
      "Unexpected ref-qualifiers");
  static_assert(
      std::is_same<int&, decltype(*std::declval<absl::StatusOr<int>&>())>(),
      "Unexpected ref-qualifiers");
  static_assert(
      std::is_same<const int&&,
                   decltype(*std::declval<const absl::StatusOr<int>&&>())>(),
      "Unexpected ref-qualifiers");
  static_assert(
      std::is_same<int&&, decltype(*std::declval<absl::StatusOr<int>&&>())>(),
      "Unexpected ref-qualifiers");
}

TEST(StatusOr, OperatorStar) {
  const absl::StatusOr<std::string> const_lvalue("hello");
  EXPECT_EQ("hello", *const_lvalue);

  absl::StatusOr<std::string> lvalue("hello");
  EXPECT_EQ("hello", *lvalue);

  // Note: Recall that std::move() is equivalent to a static_cast to an rvalue
  // reference type.
  const absl::StatusOr<std::string> const_rvalue("hello");
  EXPECT_EQ("hello", *std::move(const_rvalue));  // NOLINT

  absl::StatusOr<std::string> rvalue("hello");
  EXPECT_EQ("hello", *std::move(rvalue));
}

TEST(StatusOr, OperatorArrowQualifiers) {
  static_assert(
      std::is_same<
          const int*,
          decltype(std::declval<const absl::StatusOr<int>&>().operator->())>(),
      "Unexpected qualifiers");
  static_assert(
      std::is_same<
          int*, decltype(std::declval<absl::StatusOr<int>&>().operator->())>(),
      "Unexpected qualifiers");
  static_assert(
      std::is_same<
          const int*,
          decltype(std::declval<const absl::StatusOr<int>&&>().operator->())>(),
      "Unexpected qualifiers");
  static_assert(
      std::is_same<
          int*, decltype(std::declval<absl::StatusOr<int>&&>().operator->())>(),
      "Unexpected qualifiers");
}

TEST(StatusOr, OperatorArrow) {
  const absl::StatusOr<std::string> const_lvalue("hello");
  EXPECT_EQ(std::string("hello"), const_lvalue->c_str());

  absl::StatusOr<std::string> lvalue("hello");
  EXPECT_EQ(std::string("hello"), lvalue->c_str());
}

TEST(StatusOr, RValueStatus) {
  absl::StatusOr<int> so(absl::NotFoundError("taco"));
  const absl::Status s = std::move(so).status();

  EXPECT_EQ(s.code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(s.message(), "taco");

  // Check that !ok() still implies !status().ok(), even after moving out of the
  // object. See the note on the rvalue ref-qualified status method.
  EXPECT_FALSE(so.ok());  // NOLINT
  EXPECT_FALSE(so.status().ok());
  EXPECT_EQ(so.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(so.status().message(), "Status accessed after move.");
}

TEST(StatusOr, TestValue) {
  const int kI = 4;
  absl::StatusOr<int> thing(kI);
  EXPECT_EQ(kI, *thing);
}

TEST(StatusOr, TestValueConst) {
  const int kI = 4;
  const absl::StatusOr<int> thing(kI);
  EXPECT_EQ(kI, *thing);
}

TEST(StatusOr, TestPointerDefaultCtor) {
  absl::StatusOr<int*> thing;
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kUnknown);
}

TEST(StatusOr, TestPointerStatusCtor) {
  absl::StatusOr<int*> thing(absl::CancelledError());
  EXPECT_FALSE(thing.ok());
  EXPECT_EQ(thing.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestPointerValueCtor) {
  const int kI = 4;

  // Construction from a non-null pointer
  {
    absl::StatusOr<const int*> so(&kI);
    EXPECT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(&kI, *so);
  }

  // Construction from a null pointer constant
  {
    absl::StatusOr<const int*> so(nullptr);
    EXPECT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(nullptr, *so);
  }

  // Construction from a non-literal null pointer
  {
    const int* const p = nullptr;

    absl::StatusOr<const int*> so(p);
    EXPECT_TRUE(so.ok());
    EXPECT_OK(so.status());
    EXPECT_EQ(nullptr, *so);
  }
}

TEST(StatusOr, TestPointerCopyCtorStatusOk) {
  const int kI = 0;
  absl::StatusOr<const int*> original(&kI);
  absl::StatusOr<const int*> copy(original);
  EXPECT_OK(copy.status());
  EXPECT_EQ(*original, *copy);
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOk) {
  absl::StatusOr<int*> original(absl::CancelledError());
  absl::StatusOr<int*> copy(original);
  EXPECT_EQ(copy.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestPointerCopyCtorStatusOKConverting) {
  Derived derived;
  absl::StatusOr<Derived*> original(&derived);
  absl::StatusOr<Base2*> copy(original);
  EXPECT_OK(copy.status());
  EXPECT_EQ(static_cast<const Base2*>(*original), *copy);
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOkConverting) {
  absl::StatusOr<Derived*> original(absl::CancelledError());
  absl::StatusOr<Base2*> copy(original);
  EXPECT_EQ(copy.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestPointerAssignmentStatusOk) {
  const int kI = 0;
  absl::StatusOr<const int*> source(&kI);
  absl::StatusOr<const int*> target;
  target = source;
  EXPECT_OK(target.status());
  EXPECT_EQ(*source, *target);
}

TEST(StatusOr, TestPointerAssignmentStatusNotOk) {
  absl::StatusOr<int*> source(absl::CancelledError());
  absl::StatusOr<int*> target;
  target = source;
  EXPECT_EQ(target.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestPointerAssignmentStatusOKConverting) {
  Derived derived;
  absl::StatusOr<Derived*> source(&derived);
  absl::StatusOr<Base2*> target;
  target = source;
  EXPECT_OK(target.status());
  EXPECT_EQ(static_cast<const Base2*>(*source), *target);
}

TEST(StatusOr, TestPointerAssignmentStatusNotOkConverting) {
  absl::StatusOr<Derived*> source(absl::CancelledError());
  absl::StatusOr<Base2*> target;
  target = source;
  EXPECT_EQ(target.status(), source.status());
}

TEST(StatusOr, TestPointerStatus) {
  const int kI = 0;
  absl::StatusOr<const int*> good(&kI);
  EXPECT_TRUE(good.ok());
  absl::StatusOr<const int*> bad(absl::CancelledError());
  EXPECT_EQ(bad.status().code(), absl::StatusCode::kCancelled);
}

TEST(StatusOr, TestPointerValue) {
  const int kI = 0;
  absl::StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, *thing);
}

TEST(StatusOr, TestPointerValueConst) {
  const int kI = 0;
  const absl::StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, *thing);
}

TEST(StatusOr, StatusOrVectorOfUniquePointerCanReserveAndResize) {
  using EvilType = std::vector<std::unique_ptr<int>>;
  static_assert(std::is_copy_constructible<EvilType>::value, "");
  std::vector<::absl::StatusOr<EvilType>> v(5);
  v.reserve(v.capacity() + 10);
  v.resize(v.capacity() + 10);
}

TEST(StatusOr, ConstPayload) {
  // A reduced version of a problematic type found in the wild. All of the
  // operations below should compile.
  absl::StatusOr<const int> a;

  // Copy-construction
  absl::StatusOr<const int> b(a);

  // Copy-assignment
  EXPECT_FALSE(std::is_copy_assignable<absl::StatusOr<const int>>::value);

  // Move-construction
  absl::StatusOr<const int> c(std::move(a));

  // Move-assignment
  EXPECT_FALSE(std::is_move_assignable<absl::StatusOr<const int>>::value);
}

TEST(StatusOr, MapToStatusOrUniquePtr) {
  // A reduced version of a problematic type found in the wild. All of the
  // operations below should compile.
  using MapType = std::map<std::string, absl::StatusOr<std::unique_ptr<int>>>;

  MapType a;

  // Move-construction
  MapType b(std::move(a));

  // Move-assignment
  a = std::move(b);
}

TEST(StatusOr, ValueOrOk) {
  const absl::StatusOr<int> status_or = 0;
  EXPECT_EQ(status_or.value_or(-1), 0);
}

TEST(StatusOr, ValueOrDefault) {
  const absl::StatusOr<int> status_or = absl::CancelledError();
  EXPECT_EQ(status_or.value_or(-1), -1);
}

TEST(StatusOr, MoveOnlyValueOrOk) {
  EXPECT_THAT(absl::StatusOr<std::unique_ptr<int>>(absl::make_unique<int>(0))
                  .value_or(absl::make_unique<int>(-1)),
              Pointee(0));
}

TEST(StatusOr, MoveOnlyValueOrDefault) {
  EXPECT_THAT(absl::StatusOr<std::unique_ptr<int>>(absl::CancelledError())
                  .value_or(absl::make_unique<int>(-1)),
              Pointee(-1));
}

static absl::StatusOr<int> MakeStatus() { return 100; }

TEST(StatusOr, TestIgnoreError) { MakeStatus().IgnoreError(); }

TEST(StatusOr, EqualityOperator) {
  constexpr size_t kNumCases = 4;
  std::array<absl::StatusOr<int>, kNumCases> group1 = {
      absl::StatusOr<int>(1), absl::StatusOr<int>(2),
      absl::StatusOr<int>(absl::InvalidArgumentError("msg")),
      absl::StatusOr<int>(absl::InternalError("msg"))};
  std::array<absl::StatusOr<int>, kNumCases> group2 = {
      absl::StatusOr<int>(1), absl::StatusOr<int>(2),
      absl::StatusOr<int>(absl::InvalidArgumentError("msg")),
      absl::StatusOr<int>(absl::InternalError("msg"))};
  for (size_t i = 0; i < kNumCases; ++i) {
    for (size_t j = 0; j < kNumCases; ++j) {
      if (i == j) {
        EXPECT_TRUE(group1[i] == group2[j]);
        EXPECT_FALSE(group1[i] != group2[j]);
      } else {
        EXPECT_FALSE(group1[i] == group2[j]);
        EXPECT_TRUE(group1[i] != group2[j]);
      }
    }
  }
}

struct MyType {
  bool operator==(const MyType&) const { return true; }
};

enum class ConvTraits { kNone = 0, kImplicit = 1, kExplicit = 2 };

// This class has conversion operator to `StatusOr<T>` based on value of
// `conv_traits`.
template <typename T, ConvTraits conv_traits = ConvTraits::kNone>
struct StatusOrConversionBase {};

template <typename T>
struct StatusOrConversionBase<T, ConvTraits::kImplicit> {
  operator absl::StatusOr<T>() const& {  // NOLINT
    return absl::InvalidArgumentError("conversion to absl::StatusOr");
  }
  operator absl::StatusOr<T>() && {  // NOLINT
    return absl::InvalidArgumentError("conversion to absl::StatusOr");
  }
};

template <typename T>
struct StatusOrConversionBase<T, ConvTraits::kExplicit> {
  explicit operator absl::StatusOr<T>() const& {
    return absl::InvalidArgumentError("conversion to absl::StatusOr");
  }
  explicit operator absl::StatusOr<T>() && {
    return absl::InvalidArgumentError("conversion to absl::StatusOr");
  }
};

// This class has conversion operator to `T` based on the value of
// `conv_traits`.
template <typename T, ConvTraits conv_traits = ConvTraits::kNone>
struct ConversionBase {};

template <typename T>
struct ConversionBase<T, ConvTraits::kImplicit> {
  operator T() const& { return t; }         // NOLINT
  operator T() && { return std::move(t); }  // NOLINT
  T t;
};

template <typename T>
struct ConversionBase<T, ConvTraits::kExplicit> {
  explicit operator T() const& { return t; }
  explicit operator T() && { return std::move(t); }
  T t;
};

// This class has conversion operator to `absl::Status` based on the value of
// `conv_traits`.
template <ConvTraits conv_traits = ConvTraits::kNone>
struct StatusConversionBase {};

template <>
struct StatusConversionBase<ConvTraits::kImplicit> {
  operator absl::Status() const& {  // NOLINT
    return absl::InternalError("conversion to Status");
  }
  operator absl::Status() && {  // NOLINT
    return absl::InternalError("conversion to Status");
  }
};

template <>
struct StatusConversionBase<ConvTraits::kExplicit> {
  explicit operator absl::Status() const& {  // NOLINT
    return absl::InternalError("conversion to Status");
  }
  explicit operator absl::Status() && {  // NOLINT
    return absl::InternalError("conversion to Status");
  }
};

static constexpr int kConvToStatus = 1;
static constexpr int kConvToStatusOr = 2;
static constexpr int kConvToT = 4;
static constexpr int kConvExplicit = 8;

constexpr ConvTraits GetConvTraits(int bit, int config) {
  return (config & bit) == 0
             ? ConvTraits::kNone
             : ((config & kConvExplicit) == 0 ? ConvTraits::kImplicit
                                              : ConvTraits::kExplicit);
}

// This class conditionally has conversion operator to `absl::Status`, `T`,
// `StatusOr<T>`, based on values of the template parameters.
template <typename T, int config>
struct CustomType
    : StatusOrConversionBase<T, GetConvTraits(kConvToStatusOr, config)>,
      ConversionBase<T, GetConvTraits(kConvToT, config)>,
      StatusConversionBase<GetConvTraits(kConvToStatus, config)> {};

struct ConvertibleToAnyStatusOr {
  template <typename T>
  operator absl::StatusOr<T>() const {  // NOLINT
    return absl::InvalidArgumentError("Conversion to absl::StatusOr");
  }
};

// Test the rank of overload resolution for `StatusOr<T>` constructor and
// assignment, from highest to lowest:
// 1. T/Status
// 2. U that has conversion operator to absl::StatusOr<T>
// 3. U that is convertible to Status
// 4. U that is convertible to T
TEST(StatusOr, ConstructionFromT) {
  // Construct absl::StatusOr<T> from T when T is convertible to
  // absl::StatusOr<T>
  {
    ConvertibleToAnyStatusOr v;
    absl::StatusOr<ConvertibleToAnyStatusOr> statusor(v);
    EXPECT_TRUE(statusor.ok());
  }
  {
    ConvertibleToAnyStatusOr v;
    absl::StatusOr<ConvertibleToAnyStatusOr> statusor = v;
    EXPECT_TRUE(statusor.ok());
  }
  // Construct absl::StatusOr<T> from T when T is explicitly convertible to
  // Status
  {
    CustomType<MyType, kConvToStatus | kConvExplicit> v;
    absl::StatusOr<CustomType<MyType, kConvToStatus | kConvExplicit>> statusor(
        v);
    EXPECT_TRUE(statusor.ok());
  }
  {
    CustomType<MyType, kConvToStatus | kConvExplicit> v;
    absl::StatusOr<CustomType<MyType, kConvToStatus | kConvExplicit>> statusor =
        v;
    EXPECT_TRUE(statusor.ok());
  }
}

// Construct absl::StatusOr<T> from U when U is explicitly convertible to T
TEST(StatusOr, ConstructionFromTypeConvertibleToT) {
  {
    CustomType<MyType, kConvToT | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_TRUE(statusor.ok());
  }
  {
    CustomType<MyType, kConvToT> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_TRUE(statusor.ok());
  }
}

// Construct absl::StatusOr<T> from U when U has explicit conversion operator to
// absl::StatusOr<T>
TEST(StatusOr, ConstructionFromTypeWithConversionOperatorToStatusOrT) {
  {
    CustomType<MyType, kConvToStatusOr | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToT | kConvToStatusOr | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToStatusOr | kConvToStatus | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType,
               kConvToT | kConvToStatusOr | kConvToStatus | kConvExplicit>
        v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToStatusOr> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToT | kConvToStatusOr> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToStatusOr | kConvToStatus> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToT | kConvToStatusOr | kConvToStatus> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
}

TEST(StatusOr, ConstructionFromTypeConvertibleToStatus) {
  // Construction fails because conversion to `Status` is explicit.
  {
    CustomType<MyType, kConvToStatus | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
  {
    CustomType<MyType, kConvToT | kConvToStatus | kConvExplicit> v;
    absl::StatusOr<MyType> statusor(v);
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
  {
    CustomType<MyType, kConvToStatus> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
  {
    CustomType<MyType, kConvToT | kConvToStatus> v;
    absl::StatusOr<MyType> statusor = v;
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
}

TEST(StatusOr, AssignmentFromT) {
  // Assign to absl::StatusOr<T> from T when T is convertible to
  // absl::StatusOr<T>
  {
    ConvertibleToAnyStatusOr v;
    absl::StatusOr<ConvertibleToAnyStatusOr> statusor;
    statusor = v;
    EXPECT_TRUE(statusor.ok());
  }
  // Assign to absl::StatusOr<T> from T when T is convertible to Status
  {
    CustomType<MyType, kConvToStatus> v;
    absl::StatusOr<CustomType<MyType, kConvToStatus>> statusor;
    statusor = v;
    EXPECT_TRUE(statusor.ok());
  }
}

TEST(StatusOr, AssignmentFromTypeConvertibleToT) {
  // Assign to absl::StatusOr<T> from U when U is convertible to T
  {
    CustomType<MyType, kConvToT> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_TRUE(statusor.ok());
  }
}

TEST(StatusOr, AssignmentFromTypeWithConversionOperatortoStatusOrT) {
  // Assign to absl::StatusOr<T> from U when U has conversion operator to
  // absl::StatusOr<T>
  {
    CustomType<MyType, kConvToStatusOr> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToT | kConvToStatusOr> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToStatusOr | kConvToStatus> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
  {
    CustomType<MyType, kConvToT | kConvToStatusOr | kConvToStatus> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_EQ(statusor, v.operator absl::StatusOr<MyType>());
  }
}

TEST(StatusOr, AssignmentFromTypeConvertibleToStatus) {
  // Assign to absl::StatusOr<T> from U when U is convertible to Status
  {
    CustomType<MyType, kConvToStatus> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
  {
    CustomType<MyType, kConvToT | kConvToStatus> v;
    absl::StatusOr<MyType> statusor;
    statusor = v;
    EXPECT_FALSE(statusor.ok());
    EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
  }
}

TEST(StatusOr, StatusAssignmentFromStatusError) {
  absl::StatusOr<absl::Status> statusor;
  statusor.AssignStatus(absl::CancelledError());

  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(statusor.status(), absl::CancelledError());
}

#if GTEST_HAS_DEATH_TEST
TEST(StatusOr, StatusAssignmentFromStatusOk) {
  EXPECT_DEBUG_DEATH(
      {
        absl::StatusOr<absl::Status> statusor;
        // This will DCHECK.
        statusor.AssignStatus(absl::OkStatus());
        // In optimized mode, we are actually going to get error::INTERNAL for
        // status here, rather than crashing, so check that.
        EXPECT_FALSE(statusor.ok());
        EXPECT_EQ(statusor.status().code(), absl::StatusCode::kInternal);
      },
      "An OK status is not a valid constructor argument to StatusOr<T>");
}
#endif

TEST(StatusOr, StatusAssignmentFromTypeConvertibleToStatus) {
  CustomType<MyType, kConvToStatus> v;
  absl::StatusOr<MyType> statusor;
  statusor.AssignStatus(v);

  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(statusor.status(), static_cast<absl::Status>(v));
}

struct PrintTestStruct {
  friend std::ostream& operator<<(std::ostream& os, const PrintTestStruct&) {
    return os << "ostream";
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const PrintTestStruct&) {
    sink.Append("stringify");
  }
};

TEST(StatusOr, OkPrinting) {
  absl::StatusOr<PrintTestStruct> print_me = PrintTestStruct{};
  std::stringstream stream;
  stream << print_me;
  EXPECT_EQ(stream.str(), "ostream");
  EXPECT_EQ(absl::StrCat(print_me), "stringify");
}

TEST(StatusOr, ErrorPrinting) {
  absl::StatusOr<PrintTestStruct> print_me = absl::UnknownError("error");
  std::stringstream stream;
  stream << print_me;
  const auto error_matcher =
      AllOf(HasSubstr("UNKNOWN"), HasSubstr("error"),
            AnyOf(AllOf(StartsWith("("), EndsWith(")")),
                  AllOf(StartsWith("["), EndsWith("]"))));
  EXPECT_THAT(stream.str(), error_matcher);
  EXPECT_THAT(absl::StrCat(print_me), error_matcher);
}

}  // namespace
