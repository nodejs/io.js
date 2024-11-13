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
//
// -----------------------------------------------------------------------------
// File: log/internal/check_op.h
// -----------------------------------------------------------------------------
//
// This file declares helpers routines and macros used to implement `CHECK`
// macros.

#ifndef ABSL_LOG_INTERNAL_CHECK_OP_H_
#define ABSL_LOG_INTERNAL_CHECK_OP_H_

#include <stdint.h>

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/log/internal/nullguard.h"
#include "absl/log/internal/nullstream.h"
#include "absl/log/internal/strip.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/string_view.h"

// `ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL` wraps string literals that
// should be stripped when `ABSL_MIN_LOG_LEVEL` exceeds `kFatal`.
#ifdef ABSL_MIN_LOG_LEVEL
#define ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(literal)         \
  (::absl::LogSeverity::kFatal >=                               \
           static_cast<::absl::LogSeverity>(ABSL_MIN_LOG_LEVEL) \
       ? (literal)                                              \
       : "")
#else
#define ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(literal) (literal)
#endif

#ifdef NDEBUG
// `NDEBUG` is defined, so `DCHECK_EQ(x, y)` and so on do nothing.  However, we
// still want the compiler to parse `x` and `y`, because we don't want to lose
// potentially useful errors and warnings.
#define ABSL_LOG_INTERNAL_DCHECK_NOP(x, y)   \
  while (false && ((void)(x), (void)(y), 0)) \
  ::absl::log_internal::NullStream().InternalStream()
#endif

#define ABSL_LOG_INTERNAL_CHECK_OP(name, op, val1, val1_text, val2, val2_text) \
  while (::std::string* absl_log_internal_check_op_result                      \
             ABSL_LOG_INTERNAL_ATTRIBUTE_UNUSED_IF_STRIP_LOG =                 \
                 ::absl::log_internal::name##Impl(                             \
                     ::absl::log_internal::GetReferenceableValue(val1),        \
                     ::absl::log_internal::GetReferenceableValue(val2),        \
                     ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(                   \
                         val1_text " " #op " " val2_text)))                    \
    ABSL_LOG_INTERNAL_CONDITION_FATAL(STATELESS, true)                         \
  ABSL_LOG_INTERNAL_CHECK(*absl_log_internal_check_op_result).InternalStream()
#define ABSL_LOG_INTERNAL_QCHECK_OP(name, op, val1, val1_text, val2, \
                                    val2_text)                       \
  while (::std::string* absl_log_internal_qcheck_op_result =         \
             ::absl::log_internal::name##Impl(                       \
                 ::absl::log_internal::GetReferenceableValue(val1),  \
                 ::absl::log_internal::GetReferenceableValue(val2),  \
                 ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(             \
                     val1_text " " #op " " val2_text)))              \
    ABSL_LOG_INTERNAL_CONDITION_QFATAL(STATELESS, true)              \
  ABSL_LOG_INTERNAL_QCHECK(*absl_log_internal_qcheck_op_result).InternalStream()
#define ABSL_LOG_INTERNAL_CHECK_STROP(func, op, expected, s1, s1_text, s2,     \
                                      s2_text)                                 \
  while (::std::string* absl_log_internal_check_strop_result =                 \
             ::absl::log_internal::Check##func##expected##Impl(                \
                 (s1), (s2),                                                   \
                 ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(s1_text " " #op        \
                                                                " " s2_text))) \
    ABSL_LOG_INTERNAL_CONDITION_FATAL(STATELESS, true)                         \
  ABSL_LOG_INTERNAL_CHECK(*absl_log_internal_check_strop_result)               \
      .InternalStream()
#define ABSL_LOG_INTERNAL_QCHECK_STROP(func, op, expected, s1, s1_text, s2,    \
                                       s2_text)                                \
  while (::std::string* absl_log_internal_qcheck_strop_result =                \
             ::absl::log_internal::Check##func##expected##Impl(                \
                 (s1), (s2),                                                   \
                 ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(s1_text " " #op        \
                                                                " " s2_text))) \
    ABSL_LOG_INTERNAL_CONDITION_QFATAL(STATELESS, true)                        \
  ABSL_LOG_INTERNAL_QCHECK(*absl_log_internal_qcheck_strop_result)             \
      .InternalStream()
// This one is tricky:
// * We must evaluate `val` exactly once, yet we need to do two things with it:
//   evaluate `.ok()` and (sometimes) `.ToString()`.
// * `val` might be an `absl::Status` or some `absl::StatusOr<T>`.
// * `val` might be e.g. `ATemporary().GetStatus()`, which may return a
//   reference to a member of `ATemporary` that is only valid until the end of
//   the full expression.
// * We don't want this file to depend on `absl::Status` `#include`s or linkage,
//   nor do we want to move the definition to status and introduce a dependency
//   in the other direction.  We can be assured that callers must already have a
//   `Status` and the necessary `#include`s and linkage.
// * Callsites should be small and fast (at least when `val.ok()`): one branch,
//   minimal stack footprint.
//   * In particular, the string concat stuff should be out-of-line and emitted
//     in only one TU to save linker input size
// * We want the `val.ok()` check inline so static analyzers and optimizers can
//   see it.
// * As usual, no braces so we can stream into the expansion with `operator<<`.
// * Also as usual, it must expand to a single (partial) statement with no
//   ambiguous-else problems.
// * When stripped by `ABSL_MIN_LOG_LEVEL`, we must discard the `<expr> is OK`
//   string literal and abort without doing any streaming.  We don't need to
//   strip the call to stringify the non-ok `Status` as long as we don't log it;
//   dropping the `Status`'s message text is out of scope.
#define ABSL_LOG_INTERNAL_CHECK_OK(val, val_text)                        \
  for (::std::pair<const ::absl::Status*, ::std::string*>                \
           absl_log_internal_check_ok_goo;                               \
       absl_log_internal_check_ok_goo.first =                            \
           ::absl::log_internal::AsStatus(val),                          \
       absl_log_internal_check_ok_goo.second =                           \
           ABSL_PREDICT_TRUE(absl_log_internal_check_ok_goo.first->ok()) \
               ? nullptr                                                 \
               : ::absl::status_internal::MakeCheckFailString(           \
                     absl_log_internal_check_ok_goo.first,               \
                     ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(val_text     \
                                                            " is OK")),  \
       !ABSL_PREDICT_TRUE(absl_log_internal_check_ok_goo.first->ok());)  \
    ABSL_LOG_INTERNAL_CONDITION_FATAL(STATELESS, true)                   \
  ABSL_LOG_INTERNAL_CHECK(*absl_log_internal_check_ok_goo.second)        \
      .InternalStream()
#define ABSL_LOG_INTERNAL_QCHECK_OK(val, val_text)                        \
  for (::std::pair<const ::absl::Status*, ::std::string*>                 \
           absl_log_internal_qcheck_ok_goo;                               \
       absl_log_internal_qcheck_ok_goo.first =                            \
           ::absl::log_internal::AsStatus(val),                           \
       absl_log_internal_qcheck_ok_goo.second =                           \
           ABSL_PREDICT_TRUE(absl_log_internal_qcheck_ok_goo.first->ok()) \
               ? nullptr                                                  \
               : ::absl::status_internal::MakeCheckFailString(            \
                     absl_log_internal_qcheck_ok_goo.first,               \
                     ABSL_LOG_INTERNAL_STRIP_STRING_LITERAL(val_text      \
                                                            " is OK")),   \
       !ABSL_PREDICT_TRUE(absl_log_internal_qcheck_ok_goo.first->ok());)  \
    ABSL_LOG_INTERNAL_CONDITION_QFATAL(STATELESS, true)                   \
  ABSL_LOG_INTERNAL_QCHECK(*absl_log_internal_qcheck_ok_goo.second)       \
      .InternalStream()

namespace absl {
ABSL_NAMESPACE_BEGIN

class Status;
template <typename T>
class StatusOr;

namespace status_internal {
ABSL_ATTRIBUTE_PURE_FUNCTION std::string* MakeCheckFailString(
    const absl::Status* status, const char* prefix);
}  // namespace status_internal

namespace log_internal {

// Convert a Status or a StatusOr to its underlying status value.
//
// (This implementation does not require a dep on absl::Status to work.)
inline const absl::Status* AsStatus(const absl::Status& s) { return &s; }
template <typename T>
const absl::Status* AsStatus(const absl::StatusOr<T>& s) {
  return &s.status();
}

// A helper class for formatting `expr (V1 vs. V2)` in a `CHECK_XX` statement.
// See `MakeCheckOpString` for sample usage.
class CheckOpMessageBuilder final {
 public:
  // Inserts `exprtext` and ` (` to the stream.
  explicit CheckOpMessageBuilder(const char* exprtext);
  ~CheckOpMessageBuilder() = default;
  // For inserting the first variable.
  std::ostream& ForVar1() { return stream_; }
  // For inserting the second variable (adds an intermediate ` vs. `).
  std::ostream& ForVar2();
  // Get the result (inserts the closing `)`).
  std::string* NewString();

 private:
  std::ostringstream stream_;
};

// This formats a value for a failing `CHECK_XX` statement.  Ordinarily, it uses
// the definition for `operator<<`, with a few special cases below.
template <typename T>
inline void MakeCheckOpValueString(std::ostream& os, const T& v) {
  os << log_internal::NullGuard<T>::Guard(v);
}

// Overloads for char types provide readable values for unprintable characters.
void MakeCheckOpValueString(std::ostream& os, char v);
void MakeCheckOpValueString(std::ostream& os, signed char v);
void MakeCheckOpValueString(std::ostream& os, unsigned char v);
void MakeCheckOpValueString(std::ostream& os, const void* p);

namespace detect_specialization {

// MakeCheckOpString is being specialized for every T and U pair that is being
// passed to the CHECK_op macros. However, there is a lot of redundancy in these
// specializations that creates unnecessary library and binary bloat.
// The number of instantiations tends to be O(n^2) because we have two
// independent inputs. This technique works by reducing `n`.
//
// Most user-defined types being passed to CHECK_op end up being printed as a
// builtin type. For example, enums tend to be implicitly converted to its
// underlying type when calling operator<<, and pointers are printed with the
// `const void*` overload.
// To reduce the number of instantiations we coerce these values before calling
// MakeCheckOpString instead of inside it.
//
// To detect if this coercion is needed, we duplicate all the relevant
// operator<< overloads as specified in the standard, just in a different
// namespace. If the call to `stream << value` becomes ambiguous, it means that
// one of these overloads is the one selected by overload resolution. We then
// do overload resolution again just with our overload set to see which one gets
// selected. That tells us which type to coerce to.
// If the augmented call was not ambiguous, it means that none of these were
// selected and we can't coerce the input.
//
// As a secondary step to reduce code duplication, we promote integral types to
// their 64-bit variant. This does not change the printed value, but reduces the
// number of instantiations even further. Promoting an integer is very cheap at
// the call site.
int64_t operator<<(std::ostream&, short value);           // NOLINT
int64_t operator<<(std::ostream&, unsigned short value);  // NOLINT
int64_t operator<<(std::ostream&, int value);
int64_t operator<<(std::ostream&, unsigned int value);
int64_t operator<<(std::ostream&, long value);                 // NOLINT
uint64_t operator<<(std::ostream&, unsigned long value);       // NOLINT
int64_t operator<<(std::ostream&, long long value);            // NOLINT
uint64_t operator<<(std::ostream&, unsigned long long value);  // NOLINT
float operator<<(std::ostream&, float value);
double operator<<(std::ostream&, double value);
long double operator<<(std::ostream&, long double value);
bool operator<<(std::ostream&, bool value);
const void* operator<<(std::ostream&, const void* value);
const void* operator<<(std::ostream&, std::nullptr_t);

// These `char` overloads are specified like this in the standard, so we have to
// write them exactly the same to ensure the call is ambiguous.
// If we wrote it in a different way (eg taking std::ostream instead of the
// template) then one call might have a higher rank than the other and it would
// not be ambiguous.
template <typename Traits>
char operator<<(std::basic_ostream<char, Traits>&, char);
template <typename Traits>
signed char operator<<(std::basic_ostream<char, Traits>&, signed char);
template <typename Traits>
unsigned char operator<<(std::basic_ostream<char, Traits>&, unsigned char);
template <typename Traits>
const char* operator<<(std::basic_ostream<char, Traits>&, const char*);
template <typename Traits>
const signed char* operator<<(std::basic_ostream<char, Traits>&,
                              const signed char*);
template <typename Traits>
const unsigned char* operator<<(std::basic_ostream<char, Traits>&,
                                const unsigned char*);

// This overload triggers when the call is not ambiguous.
// It means that T is being printed with some overload not on this list.
// We keep the value as `const T&`.
template <typename T, typename = decltype(std::declval<std::ostream&>()
                                          << std::declval<const T&>())>
const T& Detect(int);

// This overload triggers when the call is ambiguous.
// It means that T is either one from this list or printed as one from this
// list. Eg an enum that decays to `int` for printing.
// We ask the overload set to give us the type we want to convert it to.
template <typename T>
decltype(detect_specialization::operator<<(std::declval<std::ostream&>(),
                                           std::declval<const T&>()))
Detect(char);

// A sink for AbslStringify which redirects everything to a std::ostream.
class StringifySink {
 public:
  explicit StringifySink(std::ostream& os ABSL_ATTRIBUTE_LIFETIME_BOUND);

  void Append(absl::string_view text);
  void Append(size_t length, char ch);
  friend void AbslFormatFlush(StringifySink* sink, absl::string_view text);

 private:
  std::ostream& os_;
};

// Wraps a type implementing AbslStringify, and implements operator<<.
template <typename T>
class StringifyToStreamWrapper {
 public:
  explicit StringifyToStreamWrapper(const T& v ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : v_(v) {}

  friend std::ostream& operator<<(std::ostream& os,
                                  const StringifyToStreamWrapper& wrapper) {
    StringifySink sink(os);
    AbslStringify(sink, wrapper.v_);
    return os;
  }

 private:
  const T& v_;
};

// This overload triggers when T implements AbslStringify.
// StringifyToStreamWrapper is used to allow MakeCheckOpString to use
// operator<<.
template <typename T>
std::enable_if_t<HasAbslStringify<T>::value,
                 StringifyToStreamWrapper<T>>
Detect(...);  // Ellipsis has lowest preference when int passed.
}  // namespace detect_specialization

template <typename T>
using CheckOpStreamType = decltype(detect_specialization::Detect<T>(0));

// Build the error message string.  Specify no inlining for code size.
template <typename T1, typename T2>
ABSL_ATTRIBUTE_RETURNS_NONNULL std::string* MakeCheckOpString(
    T1 v1, T2 v2, const char* exprtext) ABSL_ATTRIBUTE_NOINLINE;

template <typename T1, typename T2>
std::string* MakeCheckOpString(T1 v1, T2 v2, const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  MakeCheckOpValueString(comb.ForVar1(), v1);
  MakeCheckOpValueString(comb.ForVar2(), v2);
  return comb.NewString();
}

// Add a few commonly used instantiations as extern to reduce size of objects
// files.
#define ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(x) \
  extern template std::string* MakeCheckOpString(x, x, const char*)
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(bool);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(int64_t);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(uint64_t);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(float);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(double);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(char);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(unsigned char);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const std::string&);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const absl::string_view&);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const signed char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const unsigned char*);
ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN(const void*);
#undef ABSL_LOG_INTERNAL_DEFINE_MAKE_CHECK_OP_STRING_EXTERN

// `ABSL_LOG_INTERNAL_CHECK_OP_IMPL_RESULT` skips formatting the Check_OP result
// string iff `ABSL_MIN_LOG_LEVEL` exceeds `kFatal`, instead returning an empty
// string.
#ifdef ABSL_MIN_LOG_LEVEL
#define ABSL_LOG_INTERNAL_CHECK_OP_IMPL_RESULT(U1, U2, v1, v2, exprtext) \
  ((::absl::LogSeverity::kFatal >=                                       \
    static_cast<::absl::LogSeverity>(ABSL_MIN_LOG_LEVEL))                \
       ? MakeCheckOpString<U1, U2>(v1, v2, exprtext)                     \
       : new std::string())
#else
#define ABSL_LOG_INTERNAL_CHECK_OP_IMPL_RESULT(U1, U2, v1, v2, exprtext) \
  MakeCheckOpString<U1, U2>(v1, v2, exprtext)
#endif

// Helper functions for `ABSL_LOG_INTERNAL_CHECK_OP` macro family.  The
// `(int, int)` override works around the issue that the compiler will not
// instantiate the template version of the function on values of unnamed enum
// type.
#define ABSL_LOG_INTERNAL_CHECK_OP_IMPL(name, op)                          \
  template <typename T1, typename T2>                                      \
  inline constexpr ::std::string* name##Impl(const T1& v1, const T2& v2,   \
                                             const char* exprtext) {       \
    using U1 = CheckOpStreamType<T1>;                                      \
    using U2 = CheckOpStreamType<T2>;                                      \
    return ABSL_PREDICT_TRUE(v1 op v2)                                     \
               ? nullptr                                                   \
               : ABSL_LOG_INTERNAL_CHECK_OP_IMPL_RESULT(U1, U2, U1(v1),    \
                                                        U2(v2), exprtext); \
  }                                                                        \
  inline constexpr ::std::string* name##Impl(int v1, int v2,               \
                                             const char* exprtext) {       \
    return name##Impl<int, int>(v1, v2, exprtext);                         \
  }

ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_EQ, ==)
ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_NE, !=)
ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_LE, <=)
ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_LT, <)
ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_GE, >=)
ABSL_LOG_INTERNAL_CHECK_OP_IMPL(Check_GT, >)
#undef ABSL_LOG_INTERNAL_CHECK_OP_IMPL_RESULT
#undef ABSL_LOG_INTERNAL_CHECK_OP_IMPL

std::string* CheckstrcmptrueImpl(const char* s1, const char* s2,
                                 const char* exprtext);
std::string* CheckstrcmpfalseImpl(const char* s1, const char* s2,
                                  const char* exprtext);
std::string* CheckstrcasecmptrueImpl(const char* s1, const char* s2,
                                     const char* exprtext);
std::string* CheckstrcasecmpfalseImpl(const char* s1, const char* s2,
                                      const char* exprtext);

// `CHECK_EQ` and friends want to pass their arguments by reference, however
// this winds up exposing lots of cases where people have defined and
// initialized static const data members but never declared them (i.e. in a .cc
// file), meaning they are not referenceable.  This function avoids that problem
// for integers (the most common cases) by overloading for every primitive
// integer type, even the ones we discourage, and returning them by value.
template <typename T>
inline constexpr const T& GetReferenceableValue(const T& t) {
  return t;
}
inline constexpr char GetReferenceableValue(char t) { return t; }
inline constexpr unsigned char GetReferenceableValue(unsigned char t) {
  return t;
}
inline constexpr signed char GetReferenceableValue(signed char t) { return t; }
inline constexpr short GetReferenceableValue(short t) { return t; }  // NOLINT
inline constexpr unsigned short GetReferenceableValue(               // NOLINT
    unsigned short t) {                                              // NOLINT
  return t;
}
inline constexpr int GetReferenceableValue(int t) { return t; }
inline constexpr unsigned int GetReferenceableValue(unsigned int t) {
  return t;
}
inline constexpr long GetReferenceableValue(long t) { return t; }  // NOLINT
inline constexpr unsigned long GetReferenceableValue(              // NOLINT
    unsigned long t) {                                             // NOLINT
  return t;
}
inline constexpr long long GetReferenceableValue(long long t) {  // NOLINT
  return t;
}
inline constexpr unsigned long long GetReferenceableValue(  // NOLINT
    unsigned long long t) {                                 // NOLINT
  return t;
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_CHECK_OP_H_
