// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_LOGGING_H_
#define V8_BASE_LOGGING_H_

#include <cstdint>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/base/abort-mode.h"
#include "src/base/base-export.h"
#include "src/base/build_config.h"
#include "src/base/compiler-specific.h"
#include "src/base/immediate-crash.h"
#include "src/base/template-utils.h"

V8_BASE_EXPORT V8_NOINLINE void V8_Dcheck(const char* file, int line,
                                          const char* message);

#ifdef DEBUG
// In debug, include file, line, and full error message for all
// FATAL() calls.
[[noreturn]] PRINTF_FORMAT(3, 4) V8_BASE_EXPORT V8_NOINLINE
    void V8_Fatal(const char* file, int line, const char* format, ...);
#define FATAL(...) V8_Fatal(__FILE__, __LINE__, __VA_ARGS__)

// The following can be used instead of FATAL() to prevent calling
// IMMEDIATE_CRASH in official mode. Please only use if needed for testing.
// See v8:13945
#define GRACEFUL_FATAL(...) FATAL(__VA_ARGS__)

#else
[[noreturn]] PRINTF_FORMAT(1, 2) V8_BASE_EXPORT V8_NOINLINE
    void V8_Fatal(const char* format, ...);
#define GRACEFUL_FATAL(...) V8_Fatal(__VA_ARGS__)

#if !defined(OFFICIAL_BUILD)
// In non-official release, include full error message, but drop file & line
// numbers. It saves binary size to drop the |file| & |line| as opposed to just
// passing in "", 0 for them.
#define FATAL(...) V8_Fatal(__VA_ARGS__)
#else
// FATAL(msg) -> IMMEDIATE_CRASH()
// FATAL(msg, ...) -> V8_Fatal(msg, ...)
#define FATAL_HELPER(_7, _6, _5, _4, _3, _2, _1, _0, ...) _0
#define FATAL_DISCARD_ARG(arg) IMMEDIATE_CRASH()
#define FATAL(...)                                                            \
  FATAL_HELPER(__VA_ARGS__, V8_Fatal, V8_Fatal, V8_Fatal, V8_Fatal, V8_Fatal, \
               V8_Fatal, FATAL_DISCARD_ARG)                                   \
  (__VA_ARGS__)
#endif  // !defined(OFFICIAL_BUILD)
#endif  // DEBUG

namespace v8::base {
// These string constants are pattern-matched by fuzzers.
constexpr const char* kUnimplementedCodeMessage = "unimplemented code";
constexpr const char* kUnreachableCodeMessage = "unreachable code";
}  // namespace v8::base

#define UNIMPLEMENTED() FATAL(::v8::base::kUnimplementedCodeMessage)
#define UNREACHABLE() FATAL(::v8::base::kUnreachableCodeMessage)
// g++ versions <= 8 cannot use UNREACHABLE() in a constexpr function.
// TODO(miladfarca): Remove once all compilers handle this properly.
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ <= 8)
#define CONSTEXPR_UNREACHABLE() abort()
#else
#define CONSTEXPR_UNREACHABLE() UNREACHABLE()
#endif

namespace v8 {
namespace base {

class CheckMessageStream : public std::ostringstream {};

// Overwrite the default function that prints a stack trace.
V8_BASE_EXPORT void SetPrintStackTrace(void (*print_stack_trace_)());

// Override the default function that handles DCHECKs.
V8_BASE_EXPORT void SetDcheckFunction(void (*dcheck_Function)(const char*, int,
                                                              const char*));

// Override the default function invoked during V8_Fatal.
V8_BASE_EXPORT void SetFatalFunction(void (*fatal_Function)(const char*, int,
                                                            const char*));

enum class OOMType {
  // We ran out of memory in the JavaScript heap.
  kJavaScript,
  // The process ran out of memory.
  kProcess,
};

// A simpler version of V8::FatalProcessOutOfMemory that is available in
// src/base. Will simply terminate the process with an OOM message that is
// recognizes as such by fuzzers and other tooling.
[[noreturn]] V8_BASE_EXPORT void FatalOOM(OOMType type, const char* msg);

// In official builds, assume all check failures can be debugged given just the
// stack trace.
#if !defined(DEBUG) && defined(OFFICIAL_BUILD)
#define CHECK_FAILED_HANDLER(message) FATAL("ignored")
#else
#define CHECK_FAILED_HANDLER(message) FATAL("Check failed: %s.", message)
#endif

// CHECK dies with a fatal error if condition is not true.  It is *not*
// controlled by DEBUG, so the check will be executed regardless of
// compilation mode.
//
// We make sure CHECK et al. always evaluates their arguments, as
// doing CHECK(FunctionWithSideEffect()) is a common idiom.
#define CHECK_WITH_MSG(condition, message) \
  do {                                     \
    if (V8_UNLIKELY(!(condition))) {       \
      CHECK_FAILED_HANDLER(message);       \
    }                                      \
  } while (false)
#define CHECK(condition) CHECK_WITH_MSG(condition, #condition)

#ifdef DEBUG

#define DCHECK_WITH_MSG_AND_LOC(condition, message, loc)                \
  do {                                                                  \
    if (V8_UNLIKELY(!(condition))) {                                    \
      V8_Dcheck(loc.FileName(), static_cast<int>(loc.Line()), message); \
    }                                                                   \
  } while (false)
#define DCHECK_WITH_MSG(condition, message)   \
  do {                                        \
    if (V8_UNLIKELY(!(condition))) {          \
      V8_Dcheck(__FILE__, __LINE__, message); \
    }                                         \
  } while (false)
#define DCHECK_WITH_LOC(condition, loc) \
  DCHECK_WITH_MSG_AND_LOC(condition, #condition, loc)
#define DCHECK(condition) DCHECK_WITH_MSG(condition, #condition)

// Helper macro for binary operators.
// Don't use this macro directly in your code, use CHECK_EQ et al below.
#define CHECK_OP(name, op, lhs, rhs)                                      \
  do {                                                                    \
    if (std::string* _msg = ::v8::base::Check##name##Impl<                \
            typename ::v8::base::pass_value_or_ref<decltype(lhs)>::type,  \
            typename ::v8::base::pass_value_or_ref<decltype(rhs)>::type>( \
            (lhs), (rhs), #lhs " " #op " " #rhs)) {                       \
      FATAL("Check failed: %s.", _msg->c_str());                          \
      delete _msg;                                                        \
    }                                                                     \
  } while (false)

#define DCHECK_OP(name, op, lhs, rhs)                                     \
  do {                                                                    \
    if (std::string* _msg = ::v8::base::Check##name##Impl<                \
            typename ::v8::base::pass_value_or_ref<decltype(lhs)>::type,  \
            typename ::v8::base::pass_value_or_ref<decltype(rhs)>::type>( \
            (lhs), (rhs), #lhs " " #op " " #rhs)) {                       \
      V8_Dcheck(__FILE__, __LINE__, _msg->c_str());                       \
      delete _msg;                                                        \
    }                                                                     \
  } while (false)

#else

// Make all CHECK functions discard their log strings to reduce code
// bloat for official release builds.

#define CHECK_OP(name, op, lhs, rhs)                                         \
  do {                                                                       \
    bool _cmp = ::v8::base::Cmp##name##Impl<                                 \
        typename ::v8::base::pass_value_or_ref<decltype(lhs)>::type,         \
        typename ::v8::base::pass_value_or_ref<decltype(rhs)>::type>((lhs),  \
                                                                     (rhs)); \
    CHECK_WITH_MSG(_cmp, #lhs " " #op " " #rhs);                             \
  } while (false)

#define DCHECK_WITH_MSG(condition, msg) void(0);

#endif

namespace detail {
template <typename... Ts>
std::string PrintToString(Ts&&... ts) {
  CheckMessageStream oss;
  (..., (oss << std::forward<Ts>(ts)));
  return oss.str();
}

template <typename T>
auto GetUnderlyingEnumTypeForPrinting(T val) {
  auto underlying_val = static_cast<std::underlying_type_t<T>>(val);
  // For single-byte enums, return a 16-bit integer to avoid printing the value
  // as a character.
  if constexpr (sizeof(underlying_val) == 1) {
    constexpr bool kIsSigned = std::is_signed_v<decltype(underlying_val)>;
    using int_t = std::conditional_t<kIsSigned, int16_t, uint16_t>;
    return static_cast<int_t>(underlying_val);
  } else {
    return underlying_val;
  }
}

}  // namespace detail

// Define default PrintCheckOperand<T> for non-printable types.
template <typename T>
std::string PrintCheckOperand(T val) {
  return "<unprintable>";
}

// Define PrintCheckOperand<T> for each T which defines operator<< for ostream,
// except types explicitly specialized below.
template <typename T>
  requires(!std::is_function_v<typename std::remove_pointer<T>::type> &&
           !std::is_enum_v<T> && has_output_operator<T, CheckMessageStream>)
std::string PrintCheckOperand(T val) {
  return detail::PrintToString(std::forward<T>(val));
}

// Provide an overload for functions and function pointers. Function pointers
// don't implicitly convert to void* but do implicitly convert to bool, so
// without this function pointers are always printed as 1 or 0. (MSVC isn't
// standards-conforming here and converts function pointers to regular
// pointers, so this is a no-op for MSVC.)
template <typename T>
  requires(std::is_function_v<typename std::remove_pointer_t<T>>)
std::string PrintCheckOperand(T val) {
  return PrintCheckOperand(reinterpret_cast<const void*>(val));
}

// Define PrintCheckOperand<T> for enums.
template <typename T>
  requires(std::is_enum_v<T>)
std::string PrintCheckOperand(T val) {
  std::string int_str =
      detail::PrintToString(detail::GetUnderlyingEnumTypeForPrinting(val));
  if constexpr (has_output_operator<T, CheckMessageStream>) {
    std::string val_str = detail::PrintToString(val);
    // Printing the original enum might have printed a single non-printable
    // character. Ignore it in that case. Also ignore if it printed the same as
    // the integral representation.
    // TODO(clemensb): Can we somehow statically find out if the output operator
    // is the default one, printing the integral value?
    if ((val_str.length() == 1 && !std::isprint(val_str[0])) ||
        val_str == int_str) {
      return int_str;
    }
    return detail::PrintToString(val_str, " (", int_str, ")");
  } else {
    return int_str;
  }
}

// Define PrintCheckOperand<T> for forward iterable containers without an output
// operator.
template <typename T>
  requires(!has_output_operator<T, CheckMessageStream> &&
           requires(T t) {
             { t.begin() } -> std::forward_iterator;
           })
std::string PrintCheckOperand(T container) {
  CheckMessageStream oss;
  oss << "{";
  bool first = true;
  for (const auto& val : container) {
    if (!first) {
      oss << ",";
    } else {
      first = false;
    }
    oss << PrintCheckOperand(val);
  }
  oss << "}";
  return oss.str();
}

// Define specializations for character types, defined in logging.cc.
#define DEFINE_PRINT_CHECK_OPERAND_CHAR(type)                       \
  template <>                                                       \
  V8_BASE_EXPORT std::string PrintCheckOperand<type>(type ch);      \
  template <>                                                       \
  V8_BASE_EXPORT std::string PrintCheckOperand<type*>(type * cstr); \
  template <>                                                       \
  V8_BASE_EXPORT std::string PrintCheckOperand<const type*>(const type* cstr);

DEFINE_PRINT_CHECK_OPERAND_CHAR(char)
DEFINE_PRINT_CHECK_OPERAND_CHAR(signed char)
DEFINE_PRINT_CHECK_OPERAND_CHAR(unsigned char)
#undef DEFINE_PRINT_CHECK_OPERAND_CHAR

// Build the error message string.  This is separate from the "Impl"
// function template because it is not performance critical and so can
// be out of line, while the "Impl" code should be inline. Caller
// takes ownership of the returned string.
template <typename Lhs, typename Rhs>
V8_NOINLINE std::string* MakeCheckOpString(Lhs lhs, Rhs rhs, char const* msg) {
  std::string lhs_str = PrintCheckOperand<Lhs>(lhs);
  std::string rhs_str = PrintCheckOperand<Rhs>(rhs);
  CheckMessageStream ss;
  ss << msg;
  constexpr size_t kMaxInlineLength = 50;
  if (lhs_str.size() <= kMaxInlineLength &&
      rhs_str.size() <= kMaxInlineLength) {
    ss << " (" << lhs_str << " vs. " << rhs_str << ")";
  } else {
    ss << "\n   " << lhs_str << "\n vs.\n   " << rhs_str << "\n";
  }
  return new std::string(ss.str());
}

// Commonly used instantiations of MakeCheckOpString<>. Explicitly instantiated
// in logging.cc.
#define EXPLICIT_CHECK_OP_INSTANTIATION(type)                                \
  extern template V8_BASE_EXPORT std::string* MakeCheckOpString<type, type>( \
      type, type, char const*);                                              \
  extern template V8_BASE_EXPORT std::string PrintCheckOperand<type>(type);

EXPLICIT_CHECK_OP_INSTANTIATION(int)
EXPLICIT_CHECK_OP_INSTANTIATION(long)       // NOLINT(runtime/int)
EXPLICIT_CHECK_OP_INSTANTIATION(long long)  // NOLINT(runtime/int)
EXPLICIT_CHECK_OP_INSTANTIATION(unsigned int)
EXPLICIT_CHECK_OP_INSTANTIATION(unsigned long)       // NOLINT(runtime/int)
EXPLICIT_CHECK_OP_INSTANTIATION(unsigned long long)  // NOLINT(runtime/int)
EXPLICIT_CHECK_OP_INSTANTIATION(void const*)
#undef EXPLICIT_CHECK_OP_INSTANTIATION

// comparison_underlying_type provides the underlying integral type of an enum,
// or std::decay<T>::type if T is not an enum. Booleans are converted to
// "unsigned int", to allow "unsigned int == bool" comparisons.
template <typename T>
struct comparison_underlying_type {
  // std::underlying_type must only be used with enum types, thus use this
  // {Dummy} type if the given type is not an enum.
  enum Dummy {};
  using decay = typename std::decay<T>::type;
  static constexpr bool is_enum = std::is_enum_v<decay>;
  using underlying = typename std::underlying_type<
      typename std::conditional<is_enum, decay, Dummy>::type>::type;
  using type_or_bool =
      typename std::conditional<is_enum, underlying, decay>::type;
  using type = typename std::conditional<std::is_same_v<type_or_bool, bool>,
                                         unsigned int, type_or_bool>::type;
};
// Cast a value to its underlying type
#define MAKE_UNDERLYING(Type, value) \
  static_cast<typename comparison_underlying_type<Type>::type>(value)

// is_signed_vs_unsigned::value is true if both types are integral, Lhs is
// signed, and Rhs is unsigned. False in all other cases.
template <typename Lhs, typename Rhs>
struct is_signed_vs_unsigned {
  using lhs_underlying = typename comparison_underlying_type<Lhs>::type;
  using rhs_underlying = typename comparison_underlying_type<Rhs>::type;
  static constexpr bool value = std::is_integral_v<lhs_underlying> &&
                                std::is_integral_v<rhs_underlying> &&
                                std::is_signed_v<lhs_underlying> &&
                                std::is_unsigned_v<rhs_underlying>;
};
// Same thing, other way around: Lhs is unsigned, Rhs signed.
template <typename Lhs, typename Rhs>
struct is_unsigned_vs_signed : public is_signed_vs_unsigned<Rhs, Lhs> {};

static_assert(!is_signed_vs_unsigned<unsigned, int>::value);
static_assert(is_unsigned_vs_signed<unsigned, int>::value);
static_assert(is_signed_vs_unsigned<int, unsigned>::value);
static_assert(!is_unsigned_vs_signed<int, unsigned>::value);
static_assert(!is_signed_vs_unsigned<unsigned, unsigned>::value);
static_assert(!is_signed_vs_unsigned<int, int>::value);

// Define the default implementation of Cmp##NAME##Impl to be used by
// CHECK##NAME##Impl.
// Note the specializations below for integral types with mismatching
// signedness.
#define DEFINE_CMP_IMPL(NAME, op)                              \
  template <typename Lhs, typename Rhs>                        \
  V8_INLINE constexpr bool Cmp##NAME##Impl(Lhs lhs, Rhs rhs) { \
    return lhs op rhs;                                         \
  }
DEFINE_CMP_IMPL(EQ, ==)
DEFINE_CMP_IMPL(NE, !=)
DEFINE_CMP_IMPL(LE, <=)
DEFINE_CMP_IMPL(LT, <)
DEFINE_CMP_IMPL(GE, >=)
DEFINE_CMP_IMPL(GT, >)
#undef DEFINE_CMP_IMPL

// Specialize the compare functions for signed vs. unsigned comparisons (via the
// `requires` clause).
#define MAKE_UNSIGNED(Type, value)         \
  static_cast<typename std::make_unsigned< \
      typename comparison_underlying_type<Type>::type>::type>(value)
#define DEFINE_SIGNED_MISMATCH_COMP(CHECK, NAME, IMPL)         \
  template <typename Lhs, typename Rhs>                        \
    requires(CHECK<Lhs, Rhs>::value)                           \
  V8_INLINE constexpr bool Cmp##NAME##Impl(Lhs lhs, Rhs rhs) { \
    return IMPL;                                               \
  }
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, EQ,
                            lhs >= 0 && MAKE_UNSIGNED(Lhs, lhs) ==
                                            MAKE_UNDERLYING(Rhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, LT,
                            lhs < 0 || MAKE_UNSIGNED(Lhs, lhs) <
                                           MAKE_UNDERLYING(Rhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, LE,
                            lhs <= 0 || MAKE_UNSIGNED(Lhs, lhs) <=
                                            MAKE_UNDERLYING(Rhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, NE, !CmpEQImpl(lhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, GT, !CmpLEImpl(lhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_signed_vs_unsigned, GE, !CmpLTImpl(lhs, rhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, EQ, CmpEQImpl(rhs, lhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, NE, CmpNEImpl(rhs, lhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, LT, CmpGTImpl(rhs, lhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, LE, CmpGEImpl(rhs, lhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, GT, CmpLTImpl(rhs, lhs))
DEFINE_SIGNED_MISMATCH_COMP(is_unsigned_vs_signed, GE, CmpLEImpl(rhs, lhs))
#undef MAKE_UNSIGNED
#undef DEFINE_SIGNED_MISMATCH_COMP

// Define the implementation of Check##NAME##Impl, using Cmp##NAME##Impl defined
// above.
#define DEFINE_CHECK_OP_IMPL(NAME)                                      \
  template <typename Lhs, typename Rhs>                                 \
  V8_INLINE constexpr std::string* Check##NAME##Impl(Lhs lhs, Rhs rhs,  \
                                                     char const* msg) { \
    using LhsPassT = typename pass_value_or_ref<Lhs>::type;             \
    using RhsPassT = typename pass_value_or_ref<Rhs>::type;             \
    bool cmp = Cmp##NAME##Impl<LhsPassT, RhsPassT>(lhs, rhs);           \
    return V8_LIKELY(cmp)                                               \
               ? nullptr                                                \
               : MakeCheckOpString<LhsPassT, RhsPassT>(lhs, rhs, msg);  \
  }
DEFINE_CHECK_OP_IMPL(EQ)
DEFINE_CHECK_OP_IMPL(NE)
DEFINE_CHECK_OP_IMPL(LE)
DEFINE_CHECK_OP_IMPL(LT)
DEFINE_CHECK_OP_IMPL(GE)
DEFINE_CHECK_OP_IMPL(GT)
#undef DEFINE_CHECK_OP_IMPL

#define CHECK_EQ(lhs, rhs) CHECK_OP(EQ, ==, lhs, rhs)
#define CHECK_NE(lhs, rhs) CHECK_OP(NE, !=, lhs, rhs)
#define CHECK_LE(lhs, rhs) CHECK_OP(LE, <=, lhs, rhs)
#define CHECK_LT(lhs, rhs) CHECK_OP(LT, <, lhs, rhs)
#define CHECK_GE(lhs, rhs) CHECK_OP(GE, >=, lhs, rhs)
#define CHECK_GT(lhs, rhs) CHECK_OP(GT, >, lhs, rhs)
#define CHECK_NULL(val) CHECK((val) == nullptr)
#define CHECK_NOT_NULL(val) CHECK((val) != nullptr)
#define CHECK_IMPLIES(lhs, rhs) \
  CHECK_WITH_MSG(!(lhs) || (rhs), #lhs " implies " #rhs)
// Performs a single (unsigned) comparison to check that {index} is
// in range [0, limit).
#define CHECK_BOUNDS(index, limit)                                    \
  CHECK_LT(static_cast<std::make_unsigned_t<decltype(index)>>(index), \
           static_cast<std::make_unsigned_t<decltype(limit)>>(limit))

}  // namespace base
}  // namespace v8


// The DCHECK macro is equivalent to CHECK except that it only
// generates code in debug builds.
#ifdef DEBUG
#define DCHECK_EQ(lhs, rhs) DCHECK_OP(EQ, ==, lhs, rhs)
#define DCHECK_NE(lhs, rhs) DCHECK_OP(NE, !=, lhs, rhs)
#define DCHECK_GT(lhs, rhs) DCHECK_OP(GT, >, lhs, rhs)
#define DCHECK_GE(lhs, rhs) DCHECK_OP(GE, >=, lhs, rhs)
#define DCHECK_LT(lhs, rhs) DCHECK_OP(LT, <, lhs, rhs)
#define DCHECK_LE(lhs, rhs) DCHECK_OP(LE, <=, lhs, rhs)
#define DCHECK_NULL(val) DCHECK((val) == nullptr)
#define DCHECK_NOT_NULL(val) DCHECK((val) != nullptr)
#define DCHECK_IMPLIES(lhs, rhs) \
  DCHECK_WITH_MSG(!(lhs) || (rhs), #lhs " implies " #rhs)
#define DCHECK_BOUNDS(index, limit)                                    \
  DCHECK_LT(static_cast<std::make_unsigned_t<decltype(index)>>(index), \
            static_cast<std::make_unsigned_t<decltype(limit)>>(limit))
#else
#define DCHECK(condition)      ((void) 0)
#define DCHECK_WITH_LOC(condition, location) ((void)0)
#define DCHECK_WITH_MSG_AND_LOC(condition, message, location) ((void)0)
#define DCHECK_EQ(v1, v2)      ((void) 0)
#define DCHECK_NE(v1, v2)      ((void) 0)
#define DCHECK_GT(v1, v2)      ((void) 0)
#define DCHECK_GE(v1, v2)      ((void) 0)
#define DCHECK_LT(v1, v2)      ((void) 0)
#define DCHECK_LE(v1, v2)      ((void) 0)
#define DCHECK_NULL(val)       ((void) 0)
#define DCHECK_NOT_NULL(val)   ((void) 0)
#define DCHECK_IMPLIES(v1, v2) ((void) 0)
#define DCHECK_BOUNDS(index, limit) ((void)0)
#endif

#endif  // V8_BASE_LOGGING_H_
