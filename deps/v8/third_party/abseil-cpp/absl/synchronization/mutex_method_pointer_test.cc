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

#include "absl/synchronization/mutex.h"

#include <cstdlib>
#include <string>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace {

class IncompleteClass;

#ifdef _MSC_VER
// These tests verify expectations about sizes of MSVC pointers to methods.
// Pointers to methods are distinguished by whether their class hierarchies
// contain single inheritance, multiple inheritance, or virtual inheritance.

// Declare classes of the various MSVC inheritance types.
class __single_inheritance SingleInheritance{};
class __multiple_inheritance MultipleInheritance;
class __virtual_inheritance VirtualInheritance;

TEST(MutexMethodPointerTest, MicrosoftMethodPointerSize) {
  void (SingleInheritance::*single_inheritance_method_pointer)();
  void (MultipleInheritance::*multiple_inheritance_method_pointer)();
  void (VirtualInheritance::*virtual_inheritance_method_pointer)();

#if defined(_M_IX86) || defined(_M_ARM)
  static_assert(sizeof(single_inheritance_method_pointer) == 4,
                "Unexpected sizeof(single_inheritance_method_pointer).");
  static_assert(sizeof(multiple_inheritance_method_pointer) == 8,
                "Unexpected sizeof(multiple_inheritance_method_pointer).");
  static_assert(sizeof(virtual_inheritance_method_pointer) == 12,
                "Unexpected sizeof(virtual_inheritance_method_pointer).");
#elif defined(_M_X64) || defined(__aarch64__)
  static_assert(sizeof(single_inheritance_method_pointer) == 8,
                "Unexpected sizeof(single_inheritance_method_pointer).");
  static_assert(sizeof(multiple_inheritance_method_pointer) == 16,
                "Unexpected sizeof(multiple_inheritance_method_pointer).");
  static_assert(sizeof(virtual_inheritance_method_pointer) == 16,
                "Unexpected sizeof(virtual_inheritance_method_pointer).");
#endif
  void (IncompleteClass::*incomplete_class_method_pointer)();
  static_assert(sizeof(incomplete_class_method_pointer) >=
                    sizeof(virtual_inheritance_method_pointer),
                "Failed invariant: sizeof(incomplete_class_method_pointer) >= "
                "sizeof(virtual_inheritance_method_pointer)!");
}

class Callback {
  bool x = true;

 public:
  Callback() {}
  bool method() {
    x = !x;
    return x;
  }
};

class M2 {
  bool x = true;

 public:
  M2() {}
  bool method2() {
    x = !x;
    return x;
  }
};

class MultipleInheritance : public Callback, public M2 {};

TEST(MutexMethodPointerTest, ConditionWithMultipleInheritanceMethod) {
  // This test ensures that Condition can deal with method pointers from classes
  // with multiple inheritance.
  MultipleInheritance object = MultipleInheritance();
  absl::Condition condition(&object, &MultipleInheritance::method);
  EXPECT_FALSE(condition.Eval());
  EXPECT_TRUE(condition.Eval());
}

class __virtual_inheritance VirtualInheritance : virtual public Callback {
  bool x = false;

 public:
  VirtualInheritance() {}
  bool method() {
    x = !x;
    return x;
  }
};

TEST(MutexMethodPointerTest, ConditionWithVirtualInheritanceMethod) {
  // This test ensures that Condition can deal with method pointers from classes
  // with virtual inheritance.
  VirtualInheritance object = VirtualInheritance();
  absl::Condition condition(&object, &VirtualInheritance::method);
  EXPECT_TRUE(condition.Eval());
  EXPECT_FALSE(condition.Eval());
}
#endif  // #ifdef _MSC_VER

TEST(MutexMethodPointerTest, ConditionWithIncompleteClassMethod) {
  using IncompleteClassMethodPointer = void (IncompleteClass::*)();

  union CallbackSlot {
    void (*anonymous_function_pointer)();
    IncompleteClassMethodPointer incomplete_class_method_pointer;
  };

  static_assert(sizeof(CallbackSlot) >= sizeof(IncompleteClassMethodPointer),
                "The callback slot is not big enough for method pointers.");
  static_assert(
      sizeof(CallbackSlot) == sizeof(IncompleteClassMethodPointer),
      "The callback slot is not big enough for anonymous function pointers.");

#if defined(_MSC_VER)
  static_assert(sizeof(IncompleteClassMethodPointer) <= 24,
                "The pointer to a method of an incomplete class is too big.");
#endif
}

}  // namespace
