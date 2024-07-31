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

#ifndef ABSL_BASE_INTERNAL_INLINE_VARIABLE_H_
#define ABSL_BASE_INTERNAL_INLINE_VARIABLE_H_

#include <type_traits>

#include "absl/base/internal/identity.h"

// File:
//   This file define a macro that allows the creation of or emulation of C++17
//   inline variables based on whether or not the feature is supported.

////////////////////////////////////////////////////////////////////////////////
// Macro: ABSL_INTERNAL_INLINE_CONSTEXPR(type, name, init)
//
// Description:
//   Expands to the equivalent of an inline constexpr instance of the specified
//   `type` and `name`, initialized to the value `init`. If the compiler being
//   used is detected as supporting actual inline variables as a language
//   feature, then the macro expands to an actual inline variable definition.
//
// Requires:
//   `type` is a type that is usable in an extern variable declaration.
//
// Requires: `name` is a valid identifier
//
// Requires:
//   `init` is an expression that can be used in the following definition:
//     constexpr type name = init;
//
// Usage:
//
//   // Equivalent to: `inline constexpr size_t variant_npos = -1;`
//   ABSL_INTERNAL_INLINE_CONSTEXPR(size_t, variant_npos, -1);
//
// Differences in implementation:
//   For a direct, language-level inline variable, decltype(name) will be the
//   type that was specified along with const qualification, whereas for
//   emulated inline variables, decltype(name) may be different (in practice
//   it will likely be a reference type).
////////////////////////////////////////////////////////////////////////////////

#ifdef __cpp_inline_variables

// Clang's -Wmissing-variable-declarations option erroneously warned that
// inline constexpr objects need to be pre-declared. This has now been fixed,
// but we will need to support this workaround for people building with older
// versions of clang.
//
// Bug: https://bugs.llvm.org/show_bug.cgi?id=35862
//
// Note:
//   type_identity_t is used here so that the const and name are in the
//   appropriate place for pointer types, reference types, function pointer
//   types, etc..
#if defined(__clang__)
#define ABSL_INTERNAL_EXTERN_DECL(type, name) \
  extern const ::absl::internal::type_identity_t<type> name;
#else  // Otherwise, just define the macro to do nothing.
#define ABSL_INTERNAL_EXTERN_DECL(type, name)
#endif  // defined(__clang__)

// See above comment at top of file for details.
#define ABSL_INTERNAL_INLINE_CONSTEXPR(type, name, init) \
  ABSL_INTERNAL_EXTERN_DECL(type, name)                  \
  inline constexpr ::absl::internal::type_identity_t<type> name = init

#else

// See above comment at top of file for details.
//
// Note:
//   type_identity_t is used here so that the const and name are in the
//   appropriate place for pointer types, reference types, function pointer
//   types, etc..
#define ABSL_INTERNAL_INLINE_CONSTEXPR(var_type, name, init)                 \
  template <class /*AbslInternalDummy*/ = void>                              \
  struct AbslInternalInlineVariableHolder##name {                            \
    static constexpr ::absl::internal::type_identity_t<var_type> kInstance = \
        init;                                                                \
  };                                                                         \
                                                                             \
  template <class AbslInternalDummy>                                         \
  constexpr ::absl::internal::type_identity_t<var_type>                      \
      AbslInternalInlineVariableHolder##name<AbslInternalDummy>::kInstance;  \
                                                                             \
  static constexpr const ::absl::internal::type_identity_t<var_type>&        \
      name = /* NOLINT */                                                    \
      AbslInternalInlineVariableHolder##name<>::kInstance;                   \
  static_assert(sizeof(void (*)(decltype(name))) != 0,                       \
                "Silence unused variable warnings.")

#endif  // __cpp_inline_variables

#endif  // ABSL_BASE_INTERNAL_INLINE_VARIABLE_H_
