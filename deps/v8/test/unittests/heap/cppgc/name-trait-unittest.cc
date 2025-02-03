// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/name-trait.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "src/base/build_config.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

struct NoName : public GarbageCollected<NoName> {
  virtual void Trace(Visitor*) const {}
};

struct OtherNoName : public GarbageCollected<OtherNoName> {
  virtual void Trace(Visitor*) const {}
};

class ClassWithName final : public GarbageCollected<ClassWithName>,
                            public NameProvider {
 public:
  explicit ClassWithName(const char* name) : name_(name) {}
  virtual void Trace(Visitor*) const {}
  const char* GetHumanReadableName() const final { return name_; }

 private:
  const char* name_;
};

}  // namespace

TEST(NameTraitTest, InternalNamesHiddenInOfficialBuild) {
  // Use a runtime test instead of static_assert to allow local builds but block
  // enabling the feature accidentally through the waterfall.
  //
  // Do not include such type information in official builds to
  // (a) save binary size on string literals, and
  // (b) avoid exposing internal types until it has been clarified whether
  //     exposing internals in DevTools is fine.
#if defined(OFFICIAL_BUILD)
  EXPECT_FALSE(NameProvider::SupportsCppClassNamesAsObjectNames());
#endif
}

TEST(NameTraitTest, DefaultName) {
  EXPECT_STREQ(
      NameProvider::SupportsCppClassNamesAsObjectNames()
          ? "cppgc::internal::(anonymous namespace)::NoName"
          : "InternalNode",
      NameTrait<NoName>::GetName(
          nullptr, HeapObjectNameForUnnamedObject::kUseClassNameIfSupported)
          .value);
  EXPECT_STREQ(
      NameProvider::SupportsCppClassNamesAsObjectNames()
          ? "cppgc::internal::(anonymous namespace)::OtherNoName"
          : "InternalNode",
      NameTrait<OtherNoName>::GetName(
          nullptr, HeapObjectNameForUnnamedObject::kUseClassNameIfSupported)
          .value);
  // The following ignores `NameProvider::SupportsCppClassNamesAsObjectNames()`
  // and just always returns the hidden name, independent of the build support.
  EXPECT_STREQ("InternalNode",
               NameTrait<NoName>::GetName(
                   nullptr, HeapObjectNameForUnnamedObject::kUseHiddenName)
                   .value);
  EXPECT_STREQ("InternalNode",
               NameTrait<OtherNoName>::GetName(
                   nullptr, HeapObjectNameForUnnamedObject::kUseHiddenName)
                   .value);
}

TEST(NameTraitTest, CustomName) {
  ClassWithName with_name("CustomName");
  EXPECT_STREQ(
      "CustomName",
      NameTrait<ClassWithName>::GetName(
          &with_name, HeapObjectNameForUnnamedObject::kUseClassNameIfSupported)
          .value);
  EXPECT_STREQ("CustomName",
               NameTrait<ClassWithName>::GetName(
                   &with_name, HeapObjectNameForUnnamedObject::kUseHiddenName)
                   .value);
}

namespace {

class TraitTester : public NameTraitBase {
 public:
  // Expose type signature parser to allow testing various inputs.
  using NameTraitBase::GetNameFromTypeSignature;
};

}  // namespace

TEST(NameTraitTest, NoTypeAvailable) {
  HeapObjectName name = TraitTester::GetNameFromTypeSignature(nullptr);
  EXPECT_STREQ(NameProvider::kNoNameDeducible, name.value);
  EXPECT_FALSE(name.name_was_hidden);
}

TEST(NameTraitTest, ParsingPrettyFunction) {
  // Test assumes that __PRETTY_FUNCTION__ and friends return a string
  // containing the the type as [T = <type>].
  HeapObjectName name = TraitTester::GetNameFromTypeSignature(
      "Some signature of a method [T = ClassNameInSignature]");
  EXPECT_STREQ("ClassNameInSignature", name.value);
  EXPECT_FALSE(name.name_was_hidden);
  // While object names are generally leaky, the test needs to be cleaned up
  // gracefully.
  delete[] name.value;
}

class HeapObjectHeaderNameTest : public testing::TestWithHeap {};

TEST_F(HeapObjectHeaderNameTest, LookupNameThroughGCInfo) {
  auto* no_name = MakeGarbageCollected<NoName>(GetAllocationHandle());
  auto no_name_tuple = HeapObjectHeader::FromObject(no_name).GetName();
  EXPECT_STREQ(NameProvider::kHiddenName, no_name_tuple.value);
  EXPECT_TRUE(no_name_tuple.name_was_hidden);

  ClassNameAsHeapObjectNameScope class_names_scope(*Heap::From(GetHeap()));
  no_name_tuple = HeapObjectHeader::FromObject(no_name).GetName();
  if (NameProvider::SupportsCppClassNamesAsObjectNames()) {
    EXPECT_STREQ("cppgc::internal::(anonymous namespace)::NoName",
                 no_name_tuple.value);
    EXPECT_FALSE(no_name_tuple.name_was_hidden);
  } else {
    EXPECT_STREQ(NameProvider::kHiddenName, no_name_tuple.value);
    EXPECT_FALSE(no_name_tuple.name_was_hidden);
  }

  auto* other_no_name =
      MakeGarbageCollected<OtherNoName>(GetAllocationHandle());
  auto other_no_name_tuple =
      HeapObjectHeader::FromObject(other_no_name).GetName();
  if (NameProvider::SupportsCppClassNamesAsObjectNames()) {
    EXPECT_STREQ("cppgc::internal::(anonymous namespace)::OtherNoName",
                 other_no_name_tuple.value);
    EXPECT_FALSE(other_no_name_tuple.name_was_hidden);
  } else {
    EXPECT_STREQ(NameProvider::kHiddenName, other_no_name_tuple.value);
    EXPECT_FALSE(other_no_name_tuple.name_was_hidden);
  }

  auto* class_with_name =
      MakeGarbageCollected<ClassWithName>(GetAllocationHandle(), "CustomName");
  auto class_with_name_tuple =
      HeapObjectHeader::FromObject(class_with_name).GetName();
  EXPECT_STREQ("CustomName", class_with_name_tuple.value);
  EXPECT_FALSE(class_with_name_tuple.name_was_hidden);
}

}  // namespace internal
}  // namespace cppgc
