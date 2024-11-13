// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/debugging/internal/demangle_rust.h"

#include <cstddef>
#include <string>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

// If DemangleRustSymbolEncoding(mangled, <buffer with room for buffer_size
// chars>, buffer_size) returns true and seems not to have overrun its output
// buffer, returns the string written by DemangleRustSymbolEncoding; otherwise
// returns an error message.
std::string ResultOfDemangling(const char* mangled, size_t buffer_size) {
  // Fill the buffer with something other than NUL so we test whether Demangle
  // appends trailing NUL as expected.
  std::string buffer(buffer_size + 1, '~');
  constexpr char kCanaryCharacter = 0x7f;  // arbitrary unlikely value
  buffer[buffer_size] = kCanaryCharacter;
  if (!DemangleRustSymbolEncoding(mangled, &buffer[0], buffer_size)) {
    return "Failed parse";
  }
  if (buffer[buffer_size] != kCanaryCharacter) {
    return "Buffer overrun by output: " + buffer.substr(0, buffer_size + 1)
        + "...";
  }
  return buffer.data();  // Not buffer itself: this trims trailing padding.
}

// Tests that DemangleRustSymbolEncoding converts mangled into plaintext given
// enough output buffer space but returns false and avoids overrunning a buffer
// that is one byte too short.
//
// The lambda wrapping allows ASSERT_EQ to branch out the first time an
// expectation is not satisfied, preventing redundant errors for the same bug.
//
// We test first with excess space so that if the algorithm just computes the
// wrong answer, it will be clear from the error log that the bounds checks are
// unlikely to be the code at fault.
#define EXPECT_DEMANGLING(mangled, plaintext) \
  do { \
    [] { \
      constexpr size_t plenty_of_space = sizeof(plaintext) + 128; \
      constexpr size_t just_enough_space = sizeof(plaintext); \
      constexpr size_t one_byte_too_few = sizeof(plaintext) - 1; \
      const char* expected_plaintext = plaintext; \
      const char* expected_error = "Failed parse"; \
      ASSERT_EQ(ResultOfDemangling(mangled, plenty_of_space), \
                expected_plaintext); \
      ASSERT_EQ(ResultOfDemangling(mangled, just_enough_space), \
                expected_plaintext); \
      ASSERT_EQ(ResultOfDemangling(mangled, one_byte_too_few), \
                expected_error); \
    }(); \
  } while (0)

// Tests that DemangleRustSymbolEncoding rejects the given input (typically, a
// truncation of a real Rust symbol name).
#define EXPECT_DEMANGLING_FAILS(mangled) \
    do { \
      constexpr size_t plenty_of_space = 1024; \
      const char* expected_error = "Failed parse"; \
      EXPECT_EQ(ResultOfDemangling(mangled, plenty_of_space), expected_error); \
    } while (0)

// Piping grep -C 1 _R demangle_test.cc into your favorite c++filt
// implementation allows you to verify that the goldens below are reasonable.

TEST(DemangleRust, EmptyDemangling) {
  EXPECT_TRUE(DemangleRustSymbolEncoding("_RC0", nullptr, 0));
}

TEST(DemangleRust, FunctionAtCrateLevel) {
  EXPECT_DEMANGLING("_RNvC10crate_name9func_name", "crate_name::func_name");
  EXPECT_DEMANGLING(
      "_RNvCs09azAZ_10crate_name9func_name", "crate_name::func_name");
}

TEST(DemangleRust, TruncationsOfFunctionAtCrateLevel) {
  EXPECT_DEMANGLING_FAILS("_R");
  EXPECT_DEMANGLING_FAILS("_RN");
  EXPECT_DEMANGLING_FAILS("_RNvC");
  EXPECT_DEMANGLING_FAILS("_RNvC10");
  EXPECT_DEMANGLING_FAILS("_RNvC10crate_nam");
  EXPECT_DEMANGLING_FAILS("_RNvC10crate_name");
  EXPECT_DEMANGLING_FAILS("_RNvC10crate_name9");
  EXPECT_DEMANGLING_FAILS("_RNvC10crate_name9func_nam");
  EXPECT_DEMANGLING_FAILS("_RNvCs");
  EXPECT_DEMANGLING_FAILS("_RNvCs09azAZ");
  EXPECT_DEMANGLING_FAILS("_RNvCs09azAZ_");
}

TEST(DemangleRust, VendorSuffixes) {
  EXPECT_DEMANGLING("_RNvC10crate_name9func_name.!@#", "crate_name::func_name");
  EXPECT_DEMANGLING("_RNvC10crate_name9func_name$!@#", "crate_name::func_name");
}

TEST(DemangleRust, UnicodeIdentifiers) {
  EXPECT_DEMANGLING("_RNvC7ice_cap17Eyjafjallajökull",
                    "ice_cap::Eyjafjallajökull");
  EXPECT_DEMANGLING("_RNvC7ice_caps_u19Eyjafjallajkull_jtb",
                    "ice_cap::Eyjafjallajökull");
}

TEST(DemangleRust, FunctionInModule) {
  EXPECT_DEMANGLING("_RNvNtCs09azAZ_10crate_name11module_name9func_name",
                    "crate_name::module_name::func_name");
}

TEST(DemangleRust, FunctionInFunction) {
  EXPECT_DEMANGLING(
      "_RNvNvCs09azAZ_10crate_name15outer_func_name15inner_func_name",
      "crate_name::outer_func_name::inner_func_name");
}

TEST(DemangleRust, ClosureInFunction) {
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_name0",
      "crate_name::func_name::{closure#0}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_name0Cs123_12client_crate",
      "crate_name::func_name::{closure#0}");
}

TEST(DemangleRust, ClosureNumbering) {
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_names_0Cs123_12client_crate",
      "crate_name::func_name::{closure#1}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_names0_0Cs123_12client_crate",
      "crate_name::func_name::{closure#2}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_names9_0Cs123_12client_crate",
      "crate_name::func_name::{closure#11}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_namesa_0Cs123_12client_crate",
      "crate_name::func_name::{closure#12}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_namesz_0Cs123_12client_crate",
      "crate_name::func_name::{closure#37}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_namesA_0Cs123_12client_crate",
      "crate_name::func_name::{closure#38}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_namesZ_0Cs123_12client_crate",
      "crate_name::func_name::{closure#63}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_names10_0Cs123_12client_crate",
      "crate_name::func_name::{closure#64}");
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_namesg6_0Cs123_12client_crate",
      "crate_name::func_name::{closure#1000}");
}

TEST(DemangleRust, ClosureNumberOverflowingInt) {
  EXPECT_DEMANGLING(
      "_RNCNvCs09azAZ_10crate_name9func_names1234567_0Cs123_12client_crate",
      "crate_name::func_name::{closure#?}");
}

TEST(DemangleRust, UnexpectedlyNamedClosure) {
  EXPECT_DEMANGLING(
      "_RNCNvCs123_10crate_name9func_name12closure_nameCs456_12client_crate",
      "crate_name::func_name::{closure:closure_name#0}");
  EXPECT_DEMANGLING(
      "_RNCNvCs123_10crate_name9func_names2_12closure_nameCs456_12client_crate",
      "crate_name::func_name::{closure:closure_name#4}");
}

TEST(DemangleRust, ItemNestedInsideClosure) {
  EXPECT_DEMANGLING(
      "_RNvNCNvCs123_10crate_name9func_name015inner_func_nameCs_12client_crate",
      "crate_name::func_name::{closure#0}::inner_func_name");
}

TEST(DemangleRust, Shim) {
  EXPECT_DEMANGLING(
      "_RNSNvCs123_10crate_name9func_name6vtableCs456_12client_crate",
      "crate_name::func_name::{shim:vtable#0}");
}

TEST(DemangleRust, UnknownUppercaseNamespace) {
  EXPECT_DEMANGLING(
      "_RNXNvCs123_10crate_name9func_name14mystery_objectCs456_12client_crate",
      "crate_name::func_name::{X:mystery_object#0}");
}

TEST(DemangleRust, NestedUppercaseNamespaces) {
  EXPECT_DEMANGLING(
      "_RNCNXNYCs123_10crate_names0_1ys1_1xs2_0Cs456_12client_crate",
      "crate_name::{Y:y#2}::{X:x#3}::{closure#4}");
}

TEST(DemangleRust, TraitDefinition) {
  EXPECT_DEMANGLING(
      "_RNvYNtC7crate_a9my_structNtC7crate_b8my_trait1f",
      "<crate_a::my_struct as crate_b::my_trait>::f");
}

TEST(DemangleRust, BasicTypeNames) {
  EXPECT_DEMANGLING("_RNvYaNtC1c1t1f", "<i8 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYbNtC1c1t1f", "<bool as c::t>::f");
  EXPECT_DEMANGLING("_RNvYcNtC1c1t1f", "<char as c::t>::f");
  EXPECT_DEMANGLING("_RNvYdNtC1c1t1f", "<f64 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYeNtC1c1t1f", "<str as c::t>::f");
  EXPECT_DEMANGLING("_RNvYfNtC1c1t1f", "<f32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYhNtC1c1t1f", "<u8 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYiNtC1c1t1f", "<isize as c::t>::f");
  EXPECT_DEMANGLING("_RNvYjNtC1c1t1f", "<usize as c::t>::f");
  EXPECT_DEMANGLING("_RNvYlNtC1c1t1f", "<i32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYmNtC1c1t1f", "<u32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYnNtC1c1t1f", "<i128 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYoNtC1c1t1f", "<u128 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYpNtC1c1t1f", "<_ as c::t>::f");
  EXPECT_DEMANGLING("_RNvYsNtC1c1t1f", "<i16 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYtNtC1c1t1f", "<u16 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYuNtC1c1t1f", "<() as c::t>::f");
  EXPECT_DEMANGLING("_RNvYvNtC1c1t1f", "<... as c::t>::f");
  EXPECT_DEMANGLING("_RNvYxNtC1c1t1f", "<i64 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYyNtC1c1t1f", "<u64 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYzNtC1c1t1f", "<! as c::t>::f");

  EXPECT_DEMANGLING_FAILS("_RNvYkNtC1c1t1f");
}

TEST(DemangleRust, SliceTypes) {
  EXPECT_DEMANGLING("_RNvYSlNtC1c1t1f", "<[i32] as c::t>::f");
  EXPECT_DEMANGLING("_RNvYSNtC1d1sNtC1c1t1f", "<[d::s] as c::t>::f");
}

TEST(DemangleRust, ImmutableReferenceTypes) {
  EXPECT_DEMANGLING("_RNvYRlNtC1c1t1f", "<&i32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYRNtC1d1sNtC1c1t1f", "<&d::s as c::t>::f");
}

TEST(DemangleRust, MutableReferenceTypes) {
  EXPECT_DEMANGLING("_RNvYQlNtC1c1t1f", "<&mut i32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYQNtC1d1sNtC1c1t1f", "<&mut d::s as c::t>::f");
}

TEST(DemangleRust, ConstantRawPointerTypes) {
  EXPECT_DEMANGLING("_RNvYPlNtC1c1t1f", "<*const i32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYPNtC1d1sNtC1c1t1f", "<*const d::s as c::t>::f");
}

TEST(DemangleRust, MutableRawPointerTypes) {
  EXPECT_DEMANGLING("_RNvYOlNtC1c1t1f", "<*mut i32 as c::t>::f");
  EXPECT_DEMANGLING("_RNvYONtC1d1sNtC1c1t1f", "<*mut d::s as c::t>::f");
}

TEST(DemangleRust, TupleLength0) {
  EXPECT_DEMANGLING("_RNvYTENtC1c1t1f", "<() as c::t>::f");
}

TEST(DemangleRust, TupleLength1) {
  EXPECT_DEMANGLING("_RNvYTlENtC1c1t1f", "<(i32,) as c::t>::f");
  EXPECT_DEMANGLING("_RNvYTNtC1d1sENtC1c1t1f", "<(d::s,) as c::t>::f");
}

TEST(DemangleRust, TupleLength2) {
  EXPECT_DEMANGLING("_RNvYTlmENtC1c1t1f", "<(i32, u32) as c::t>::f");
  EXPECT_DEMANGLING("_RNvYTNtC1d1xNtC1e1yENtC1c1t1f",
                    "<(d::x, e::y) as c::t>::f");
}

TEST(DemangleRust, TupleLength3) {
  EXPECT_DEMANGLING("_RNvYTlmnENtC1c1t1f", "<(i32, u32, i128) as c::t>::f");
  EXPECT_DEMANGLING("_RNvYTNtC1d1xNtC1e1yNtC1f1zENtC1c1t1f",
                    "<(d::x, e::y, f::z) as c::t>::f");
}

TEST(DemangleRust, LongerTuplesAbbreviated) {
  EXPECT_DEMANGLING("_RNvYTlmnoENtC1c1t1f",
                    "<(i32, u32, i128, ...) as c::t>::f");
  EXPECT_DEMANGLING("_RNvYTlmnNtC1d1xNtC1e1yENtC1c1t1f",
                    "<(i32, u32, i128, ...) as c::t>::f");
}

TEST(DemangleRust, PathBackrefToCrate) {
  EXPECT_DEMANGLING("_RNvYNtC8my_crate9my_structNtB4_8my_trait1f",
                    "<my_crate::my_struct as my_crate::my_trait>::f");
}

TEST(DemangleRust, PathBackrefToNestedPath) {
  EXPECT_DEMANGLING("_RNvYNtNtC1c1m1sNtB4_1t1f", "<c::m::s as c::m::t>::f");
}

TEST(DemangleRust, PathBackrefAsInstantiatingCrate) {
  EXPECT_DEMANGLING("_RNCNvC8my_crate7my_func0B3_",
                    "my_crate::my_func::{closure#0}");
}

TEST(DemangleRust, TypeBackrefsNestedInTuple) {
  EXPECT_DEMANGLING("_RNvYTTRlB4_ERB3_ENtC1c1t1f",
                    "<((&i32, &i32), &(&i32, &i32)) as c::t>::f");
}

TEST(DemangleRust, NoInfiniteLoopOnBackrefToTheWhole) {
  EXPECT_DEMANGLING_FAILS("_RB_");
  EXPECT_DEMANGLING_FAILS("_RNvB_1sNtC1c1t1f");
}

TEST(DemangleRust, NoCrashOnForwardBackref) {
  EXPECT_DEMANGLING_FAILS("_RB0_");
  EXPECT_DEMANGLING_FAILS("_RB1_");
  EXPECT_DEMANGLING_FAILS("_RB2_");
  EXPECT_DEMANGLING_FAILS("_RB3_");
  EXPECT_DEMANGLING_FAILS("_RB4_");
}

TEST(DemangleRust, PathBackrefsDoNotRecurseDuringSilence) {
  // B_ points at the value f (the whole mangling), so the cycle would lead to
  // parse failure if the parser tried to parse what was pointed to.
  EXPECT_DEMANGLING("_RNvYTlmnNtB_1sENtC1c1t1f",
                    "<(i32, u32, i128, ...) as c::t>::f");
}

TEST(DemangleRust, TypeBackrefsDoNotRecurseDuringSilence) {
  // B2_ points at the tuple type, likewise making a cycle that the parser
  // avoids following.
  EXPECT_DEMANGLING("_RNvYTlmnB2_ENtC1c1t1f",
                    "<(i32, u32, i128, ...) as c::t>::f");
}

TEST(DemangleRust, ConstBackrefsDoNotRecurseDuringSilence) {
  // B_ points at the whole I...E mangling, which does not parse as a const.
  EXPECT_DEMANGLING("_RINvC1c1fAlB_E", "c::f::<>");
}

TEST(DemangleRust, ReturnFromBackrefToInputPosition256) {
  // Show that we can resume at input positions that don't fit into a byte.
  EXPECT_DEMANGLING("_RNvYNtC1c238very_long_type_"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABC"
                    "NtB4_1t1f",
                    "<c::very_long_type_"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"
                    "ABCDEFGHIJabcdefghijABC"
                    " as c::t>::f");
}

TEST(DemangleRust, EmptyGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fE", "c::f::<>");
}

TEST(DemangleRust, OneSimpleTypeInGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1flE",  // c::f::<i32>
                    "c::f::<>");
}

TEST(DemangleRust, OneTupleInGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fTlmEE",  // c::f::<(i32, u32)>
                    "c::f::<>");
}

TEST(DemangleRust, OnePathInGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fNtC1d1sE",  // c::f::<d::s>
                    "c::f::<>");
}

TEST(DemangleRust, LongerGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1flmRNtC1d1sE",  // c::f::<i32, u32, &d::s>
                    "c::f::<>");
}

TEST(DemangleRust, BackrefInGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fRlB7_NtB2_1sE",  // c::f::<&i32, &i32, c::s>
                    "c::f::<>");
}

TEST(DemangleRust, NestedGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fINtB2_1slEmE",  // c::f::<c::s::<i32>, u32>
                    "c::f::<>");
}

TEST(DemangleRust, MonomorphicEntityNestedInsideGeneric) {
  EXPECT_DEMANGLING("_RNvINvC1c1fppE1g",  // c::f::<_, _>::g
                    "c::f::<>::g");
}

TEST(DemangleRust, ArrayTypeWithSimpleElementType) {
  EXPECT_DEMANGLING("_RNvYAlj1f_NtC1c1t1f", "<[i32; 0x1f] as c::t>::f");
}

TEST(DemangleRust, ArrayTypeWithComplexElementType) {
  EXPECT_DEMANGLING("_RNvYAINtC1c1slEj1f_NtB6_1t1f",
                    "<[c::s::<>; 0x1f] as c::t>::f");
}

TEST(DemangleRust, NestedArrayType) {
  EXPECT_DEMANGLING("_RNvYAAlj1f_j2e_NtC1c1t1f",
                    "<[[i32; 0x1f]; 0x2e] as c::t>::f");
}

TEST(DemangleRust, BackrefArraySize) {
  EXPECT_DEMANGLING("_RNvYAAlj1f_B5_NtC1c1t1f",
                    "<[[i32; 0x1f]; 0x1f] as c::t>::f");
}

TEST(DemangleRust, ZeroArraySize) {
  EXPECT_DEMANGLING("_RNvYAlj0_NtC1c1t1f", "<[i32; 0x0] as c::t>::f");
}

TEST(DemangleRust, SurprisingMinusesInArraySize) {
  // Compilers shouldn't do this stuff, but existing demanglers accept it.
  EXPECT_DEMANGLING("_RNvYAljn0_NtC1c1t1f", "<[i32; -0x0] as c::t>::f");
  EXPECT_DEMANGLING("_RNvYAljn42_NtC1c1t1f", "<[i32; -0x42] as c::t>::f");
}

TEST(DemangleRust, NumberAsGenericArg) {
  EXPECT_DEMANGLING("_RINvC1c1fKl8_E",  // c::f::<0x8>
                    "c::f::<>");
}

TEST(DemangleRust, NumberAsFirstOfTwoGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fKl8_mE",  // c::f::<0x8, u32>
                    "c::f::<>");
}

TEST(DemangleRust, NumberAsSecondOfTwoGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fmKl8_E",  // c::f::<u32, 0x8>
                    "c::f::<>");
}

TEST(DemangleRust, NumberPlaceholder) {
  EXPECT_DEMANGLING("_RNvINvC1c1fKpE1g",  // c::f::<_>::g
                    "c::f::<>::g");
}

TEST(DemangleRust, InherentImplWithoutDisambiguator) {
  EXPECT_DEMANGLING("_RNvMNtC8my_crate6my_modNtB2_9my_struct7my_func",
                    "<my_crate::my_mod::my_struct>::my_func");
}

TEST(DemangleRust, InherentImplWithDisambiguator) {
  EXPECT_DEMANGLING("_RNvMs_NtC8my_crate6my_modNtB4_9my_struct7my_func",
                    "<my_crate::my_mod::my_struct>::my_func");
}

TEST(DemangleRust, TraitImplWithoutDisambiguator) {
  EXPECT_DEMANGLING("_RNvXC8my_crateNtB2_9my_structNtB2_8my_trait7my_func",
                    "<my_crate::my_struct as my_crate::my_trait>::my_func");
}

TEST(DemangleRust, TraitImplWithDisambiguator) {
  EXPECT_DEMANGLING("_RNvXs_C8my_crateNtB4_9my_structNtB4_8my_trait7my_func",
                    "<my_crate::my_struct as my_crate::my_trait>::my_func");
}

TEST(DemangleRust, TraitImplWithNonpathSelfType) {
  EXPECT_DEMANGLING("_RNvXC8my_crateRlNtB2_8my_trait7my_func",
                    "<&i32 as my_crate::my_trait>::my_func");
}

TEST(DemangleRust, ThunkType) {
  EXPECT_DEMANGLING("_RNvYFEuNtC1c1t1f",  // <fn() as c::t>::f
                    "<fn... as c::t>::f");
}

TEST(DemangleRust, NontrivialFunctionReturnType) {
  EXPECT_DEMANGLING(
      "_RNvYFERTlmENtC1c1t1f",  // <fn() -> &(i32, u32) as c::t>::f
      "<fn... as c::t>::f");
}

TEST(DemangleRust, OneParameterType) {
  EXPECT_DEMANGLING("_RNvYFlEuNtC1c1t1f",  // <fn(i32) as c::t>::f
                    "<fn... as c::t>::f");
}

TEST(DemangleRust, TwoParameterTypes) {
  EXPECT_DEMANGLING("_RNvYFlmEuNtC1c1t1f",  // <fn(i32, u32) as c::t>::f
                    "<fn... as c::t>::f");
}

TEST(DemangleRust, ExternC) {
  EXPECT_DEMANGLING("_RNvYFKCEuNtC1c1t1f",  // <extern "C" fn() as c::t>>::f
                    "<fn... as c::t>::f");
}

TEST(DemangleRust, ExternOther) {
  EXPECT_DEMANGLING(
      "_RNvYFK5not_CEuNtC1c1t1f",  // <extern "not-C" fn() as c::t>::f
      "<fn... as c::t>::f");
}

TEST(DemangleRust, Unsafe) {
  EXPECT_DEMANGLING("_RNvYFUEuNtC1c1t1f",  // <unsafe fn() as c::t>::f
                    "<fn... as c::t>::f");
}

TEST(DemangleRust, Binder) {
  EXPECT_DEMANGLING(
      // <for<'a> fn(&'a i32) -> &'a i32 as c::t>::f
      "_RNvYFG_RL0_lEB5_NtC1c1t1f",
      "<fn... as c::t>::f");
}

TEST(DemangleRust, AllFnSigFeaturesInOrder) {
  EXPECT_DEMANGLING(
      // <for<'a> unsafe extern "C" fn(&'a i32) -> &'a i32 as c::t>::f
      "_RNvYFG_UKCRL0_lEB8_NtC1c1t1f",
      "<fn... as c::t>::f");
}

TEST(DemangleRust, LifetimeInGenericArgs) {
  EXPECT_DEMANGLING("_RINvC1c1fINtB2_1sL_EE",  // c::f::<c::s::<'_>>
                    "c::f::<>");
}

TEST(DemangleRust, EmptyDynTrait) {
  // This shouldn't happen, but the grammar allows it and existing demanglers
  // accept it.
  EXPECT_DEMANGLING("_RNvYDEL_NtC1c1t1f",
                    "<dyn  as c::t>::f");
}

TEST(DemangleRust, SimpleDynTrait) {
  EXPECT_DEMANGLING("_RNvYDNtC1c1tEL_NtC1d1u1f",
                    "<dyn c::t as d::u>::f");
}

TEST(DemangleRust, DynTraitWithOneAssociatedType) {
  EXPECT_DEMANGLING(
      "_RNvYDNtC1c1tp1xlEL_NtC1d1u1f",  // <dyn c::t<x = i32> as d::u>::f
      "<dyn c::t<> as d::u>::f");
}

TEST(DemangleRust, DynTraitWithTwoAssociatedTypes) {
  EXPECT_DEMANGLING(
      // <dyn c::t<x = i32, y = u32> as d::u>::f
      "_RNvYDNtC1c1tp1xlp1ymEL_NtC1d1u1f",
      "<dyn c::t<> as d::u>::f");
}

TEST(DemangleRust, DynTraitPlusAutoTrait) {
  EXPECT_DEMANGLING(
      "_RNvYDNtC1c1tNtNtC3std6marker4SendEL_NtC1d1u1f",
      "<dyn c::t + std::marker::Send as d::u>::f");
}

TEST(DemangleRust, DynTraitPlusTwoAutoTraits) {
  EXPECT_DEMANGLING(
      "_RNvYDNtC1c1tNtNtC3std6marker4CopyNtBc_4SyncEL_NtC1d1u1f",
      "<dyn c::t + std::marker::Copy + std::marker::Sync as d::u>::f");
}

TEST(DemangleRust, HigherRankedDynTrait) {
  EXPECT_DEMANGLING(
      // <dyn for<'a> c::t::<&'a i32> as d::u>::f
      "_RNvYDG_INtC1c1tRL0_lEEL_NtC1d1u1f",
      "<dyn c::t::<> as d::u>::f");
}

}  // namespace
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
