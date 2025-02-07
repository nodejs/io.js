//
//  Copyright 2019 The Abseil Authors.
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
//
// -----------------------------------------------------------------------------
// File: flag.h
// -----------------------------------------------------------------------------
//
// This header file defines the `absl::Flag<T>` type for holding command-line
// flag data, and abstractions to create, get and set such flag data.
//
// It is important to note that this type is **unspecified** (an implementation
// detail) and you do not construct or manipulate actual `absl::Flag<T>`
// instances. Instead, you define and declare flags using the
// `ABSL_FLAG()` and `ABSL_DECLARE_FLAG()` macros, and get and set flag values
// using the `absl::GetFlag()` and `absl::SetFlag()` functions.

#ifndef ABSL_FLAGS_FLAG_H_
#define ABSL_FLAGS_FLAG_H_

#include <cstdint>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/config.h"
#include "absl/flags/internal/flag.h"
#include "absl/flags/internal/registry.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Flag
//
// An `absl::Flag` holds a command-line flag value, providing a runtime
// parameter to a binary. Such flags should be defined in the global namespace
// and (preferably) in the module containing the binary's `main()` function.
//
// You should not construct and cannot use the `absl::Flag` type directly;
// instead, you should declare flags using the `ABSL_DECLARE_FLAG()` macro
// within a header file, and define your flag using `ABSL_FLAG()` within your
// header's associated `.cc` file. Such flags will be named `FLAGS_name`.
//
// Example:
//
//    .h file
//
//      // Declares usage of a flag named "FLAGS_count"
//      ABSL_DECLARE_FLAG(int, count);
//
//    .cc file
//
//      // Defines a flag named "FLAGS_count" with a default `int` value of 0.
//      ABSL_FLAG(int, count, 0, "Count of items to process");
//
// No public methods of `absl::Flag<T>` are part of the Abseil Flags API.
//
// For type support of Abseil Flags, see the marshalling.h header file, which
// discusses supported standard types, optional flags, and additional Abseil
// type support.

template <typename T>
using Flag = flags_internal::Flag<T>;

// GetFlag()
//
// Returns the value (of type `T`) of an `absl::Flag<T>` instance, by value. Do
// not construct an `absl::Flag<T>` directly and call `absl::GetFlag()`;
// instead, refer to flag's constructed variable name (e.g. `FLAGS_name`).
// Because this function returns by value and not by reference, it is
// thread-safe, but note that the operation may be expensive; as a result, avoid
// `absl::GetFlag()` within any tight loops.
//
// Example:
//
//   // FLAGS_count is a Flag of type `int`
//   int my_count = absl::GetFlag(FLAGS_count);
//
//   // FLAGS_firstname is a Flag of type `std::string`
//   std::string first_name = absl::GetFlag(FLAGS_firstname);
template <typename T>
ABSL_MUST_USE_RESULT T GetFlag(const absl::Flag<T>& flag) {
  return flags_internal::FlagImplPeer::InvokeGet<T>(flag);
}

// SetFlag()
//
// Sets the value of an `absl::Flag` to the value `v`. Do not construct an
// `absl::Flag<T>` directly and call `absl::SetFlag()`; instead, use the
// flag's variable name (e.g. `FLAGS_name`). This function is
// thread-safe, but is potentially expensive. Avoid setting flags in general,
// but especially within performance-critical code.
template <typename T>
void SetFlag(absl::Flag<T>* flag, const T& v) {
  flags_internal::FlagImplPeer::InvokeSet(*flag, v);
}

// Overload of `SetFlag()` to allow callers to pass in a value that is
// convertible to `T`. E.g., use this overload to pass a "const char*" when `T`
// is `std::string`.
template <typename T, typename V>
void SetFlag(absl::Flag<T>* flag, const V& v) {
  T value(v);
  flags_internal::FlagImplPeer::InvokeSet(*flag, value);
}

// GetFlagReflectionHandle()
//
// Returns the reflection handle corresponding to specified Abseil Flag
// instance. Use this handle to access flag's reflection information, like name,
// location, default value etc.
//
// Example:
//
//   std::string = absl::GetFlagReflectionHandle(FLAGS_count).DefaultValue();

template <typename T>
const CommandLineFlag& GetFlagReflectionHandle(const absl::Flag<T>& f) {
  return flags_internal::FlagImplPeer::InvokeReflect(f);
}

ABSL_NAMESPACE_END
}  // namespace absl


// ABSL_FLAG()
//
// This macro defines an `absl::Flag<T>` instance of a specified type `T`:
//
//   ABSL_FLAG(T, name, default_value, help);
//
// where:
//
//   * `T` is a supported flag type (see the list of types in `marshalling.h`),
//   * `name` designates the name of the flag (as a global variable
//     `FLAGS_name`),
//   * `default_value` is an expression holding the default value for this flag
//     (which must be implicitly convertible to `T`),
//   * `help` is the help text, which can also be an expression.
//
// This macro expands to a flag named 'FLAGS_name' of type 'T':
//
//   absl::Flag<T> FLAGS_name = ...;
//
// Note that all such instances are created as global variables.
//
// For `ABSL_FLAG()` values that you wish to expose to other translation units,
// it is recommended to define those flags within the `.cc` file associated with
// the header where the flag is declared.
//
// Note: do not construct objects of type `absl::Flag<T>` directly. Only use the
// `ABSL_FLAG()` macro for such construction.
#define ABSL_FLAG(Type, name, default_value, help) \
  ABSL_FLAG_IMPL(Type, name, default_value, help)

// ABSL_FLAG().OnUpdate()
//
// Defines a flag of type `T` with a callback attached:
//
//   ABSL_FLAG(T, name, default_value, help).OnUpdate(callback);
//
// `callback` should be convertible to `void (*)()`.
//
// After any setting of the flag value, the callback will be called at least
// once. A rapid sequence of changes may be merged together into the same
// callback. No concurrent calls to the callback will be made for the same
// flag. Callbacks are allowed to read the current value of the flag but must
// not mutate that flag.
//
// The update mechanism guarantees "eventual consistency"; if the callback
// derives an auxiliary data structure from the flag value, it is guaranteed
// that eventually the flag value and the derived data structure will be
// consistent.
//
// Note: ABSL_FLAG.OnUpdate() does not have a public definition. Hence, this
// comment serves as its API documentation.

// -----------------------------------------------------------------------------
// Implementation details below this section
// -----------------------------------------------------------------------------

// ABSL_FLAG_IMPL macro definition conditional on ABSL_FLAGS_STRIP_NAMES
#define ABSL_FLAG_IMPL_FLAG_PTR(flag) flag
#define ABSL_FLAG_IMPL_HELP_ARG(name)                      \
  absl::flags_internal::HelpArg<AbslFlagHelpGenFor##name>( \
      FLAGS_help_storage_##name)
#define ABSL_FLAG_IMPL_DEFAULT_ARG(Type, name) \
  absl::flags_internal::DefaultArg<Type, AbslFlagDefaultGenFor##name>(0)

#if ABSL_FLAGS_STRIP_NAMES
#define ABSL_FLAG_IMPL_FLAGNAME(txt) ""
#define ABSL_FLAG_IMPL_FILENAME() ""
#define ABSL_FLAG_IMPL_REGISTRAR(T, flag)                                      \
  absl::flags_internal::FlagRegistrar<T, false>(ABSL_FLAG_IMPL_FLAG_PTR(flag), \
                                                nullptr)
#else
#define ABSL_FLAG_IMPL_FLAGNAME(txt) txt
#define ABSL_FLAG_IMPL_FILENAME() __FILE__
#define ABSL_FLAG_IMPL_REGISTRAR(T, flag)                                     \
  absl::flags_internal::FlagRegistrar<T, true>(ABSL_FLAG_IMPL_FLAG_PTR(flag), \
                                               __FILE__)
#endif

// ABSL_FLAG_IMPL macro definition conditional on ABSL_FLAGS_STRIP_HELP

#if ABSL_FLAGS_STRIP_HELP
#define ABSL_FLAG_IMPL_FLAGHELP(txt) absl::flags_internal::kStrippedFlagHelp
#else
#define ABSL_FLAG_IMPL_FLAGHELP(txt) txt
#endif

// AbslFlagHelpGenFor##name is used to encapsulate both immediate (method Const)
// and lazy (method NonConst) evaluation of help message expression. We choose
// between the two via the call to HelpArg in absl::Flag instantiation below.
// If help message expression is constexpr evaluable compiler will optimize
// away this whole struct.
// TODO(rogeeff): place these generated structs into local namespace and apply
// ABSL_INTERNAL_UNIQUE_SHORT_NAME.
// TODO(rogeeff): Apply __attribute__((nodebug)) to FLAGS_help_storage_##name
#define ABSL_FLAG_IMPL_DECLARE_HELP_WRAPPER(name, txt)                       \
  struct AbslFlagHelpGenFor##name {                                          \
    /* The expression is run in the caller as part of the   */               \
    /* default value argument. That keeps temporaries alive */               \
    /* long enough for NonConst to work correctly.          */               \
    static constexpr absl::string_view Value(                                \
        absl::string_view absl_flag_help = ABSL_FLAG_IMPL_FLAGHELP(txt)) {   \
      return absl_flag_help;                                                 \
    }                                                                        \
    static std::string NonConst() { return std::string(Value()); }           \
  };                                                                         \
  constexpr auto FLAGS_help_storage_##name ABSL_INTERNAL_UNIQUE_SMALL_NAME() \
      ABSL_ATTRIBUTE_SECTION_VARIABLE(flags_help_cold) =                     \
          absl::flags_internal::HelpStringAsArray<AbslFlagHelpGenFor##name>( \
              0);

#define ABSL_FLAG_IMPL_DECLARE_DEF_VAL_WRAPPER(name, Type, default_value)     \
  struct AbslFlagDefaultGenFor##name {                                        \
    Type value = absl::flags_internal::InitDefaultValue<Type>(default_value); \
    static void Gen(void* absl_flag_default_loc) {                            \
      new (absl_flag_default_loc) Type(AbslFlagDefaultGenFor##name{}.value);  \
    }                                                                         \
  };

// ABSL_FLAG_IMPL
//
// Note: Name of registrar object is not arbitrary. It is used to "grab"
// global name for FLAGS_no<flag_name> symbol, thus preventing the possibility
// of defining two flags with names foo and nofoo.
#define ABSL_FLAG_IMPL(Type, name, default_value, help)                       \
  extern ::absl::Flag<Type> FLAGS_##name;                                     \
  namespace absl /* block flags in namespaces */ {}                           \
  ABSL_FLAG_IMPL_DECLARE_DEF_VAL_WRAPPER(name, Type, default_value)           \
  ABSL_FLAG_IMPL_DECLARE_HELP_WRAPPER(name, help)                             \
  ABSL_CONST_INIT absl::Flag<Type> FLAGS_##name{                              \
      ABSL_FLAG_IMPL_FLAGNAME(#name), ABSL_FLAG_IMPL_FILENAME(),              \
      ABSL_FLAG_IMPL_HELP_ARG(name), ABSL_FLAG_IMPL_DEFAULT_ARG(Type, name)}; \
  extern absl::flags_internal::FlagRegistrarEmpty FLAGS_no##name;             \
  absl::flags_internal::FlagRegistrarEmpty FLAGS_no##name =                   \
      ABSL_FLAG_IMPL_REGISTRAR(Type, FLAGS_##name)

// ABSL_RETIRED_FLAG
//
// Designates the flag (which is usually pre-existing) as "retired." A retired
// flag is a flag that is now unused by the program, but may still be passed on
// the command line, usually by production scripts. A retired flag is ignored
// and code can't access it at runtime.
//
// This macro registers a retired flag with given name and type, with a name
// identical to the name of the original flag you are retiring. The retired
// flag's type can change over time, so that you can retire code to support a
// custom flag type.
//
// This macro has the same signature as `ABSL_FLAG`. To retire a flag, simply
// replace an `ABSL_FLAG` definition with `ABSL_RETIRED_FLAG`, leaving the
// arguments unchanged (unless of course you actually want to retire the flag
// type at this time as well).
//
// `default_value` and `explanation` are unused.
// TODO(rogeeff): replace RETIRED_FLAGS with FLAGS once forward declarations of
// retired flags are cleaned up.
#define ABSL_RETIRED_FLAG(type, name, default_value, explanation)      \
  static absl::flags_internal::RetiredFlag<type> RETIRED_FLAGS_##name; \
  ABSL_ATTRIBUTE_UNUSED static const auto RETIRED_FLAGS_REG_##name =   \
      (RETIRED_FLAGS_##name.Retire(#name),                             \
       ::absl::flags_internal::FlagRegistrarEmpty{})

#endif  // ABSL_FLAGS_FLAG_H_
