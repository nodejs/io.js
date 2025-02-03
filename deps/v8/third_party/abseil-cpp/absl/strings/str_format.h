//
// Copyright 2018 The Abseil Authors.
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
// File: str_format.h
// -----------------------------------------------------------------------------
//
// The `str_format` library is a typesafe replacement for the family of
// `printf()` string formatting routines within the `<cstdio>` standard library
// header. Like the `printf` family, `str_format` uses a "format string" to
// perform argument substitutions based on types. See the `FormatSpec` section
// below for format string documentation.
//
// Example:
//
//   std::string s = absl::StrFormat(
//                      "%s %s You have $%d!", "Hello", name, dollars);
//
// The library consists of the following basic utilities:
//
//   * `absl::StrFormat()`, a type-safe replacement for `std::sprintf()`, to
//     write a format string to a `string` value.
//   * `absl::StrAppendFormat()` to append a format string to a `string`
//   * `absl::StreamFormat()` to more efficiently write a format string to a
//     stream, such as`std::cout`.
//   * `absl::PrintF()`, `absl::FPrintF()` and `absl::SNPrintF()` as
//     drop-in replacements for `std::printf()`, `std::fprintf()` and
//     `std::snprintf()`.
//
//     Note: An `absl::SPrintF()` drop-in replacement is not supported as it
//     is generally unsafe due to buffer overflows. Use `absl::StrFormat` which
//     returns the string as output instead of expecting a pre-allocated buffer.
//
// Additionally, you can provide a format string (and its associated arguments)
// using one of the following abstractions:
//
//   * A `FormatSpec` class template fully encapsulates a format string and its
//     type arguments and is usually provided to `str_format` functions as a
//     variadic argument of type `FormatSpec<Arg...>`. The `FormatSpec<Args...>`
//     template is evaluated at compile-time, providing type safety.
//   * A `ParsedFormat` instance, which encapsulates a specific, pre-compiled
//     format string for a specific set of type(s), and which can be passed
//     between API boundaries. (The `FormatSpec` type should not be used
//     directly except as an argument type for wrapper functions.)
//
// The `str_format` library provides the ability to output its format strings to
// arbitrary sink types:
//
//   * A generic `Format()` function to write outputs to arbitrary sink types,
//     which must implement a `FormatRawSink` interface.
//
//   * A `FormatUntyped()` function that is similar to `Format()` except it is
//     loosely typed. `FormatUntyped()` is not a template and does not perform
//     any compile-time checking of the format string; instead, it returns a
//     boolean from a runtime check.
//
// In addition, the `str_format` library provides extension points for
// augmenting formatting to new types.  See "StrFormat Extensions" below.

#ifndef ABSL_STRINGS_STR_FORMAT_H_
#define ABSL_STRINGS_STR_FORMAT_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/strings/internal/str_format/arg.h"  // IWYU pragma: export
#include "absl/strings/internal/str_format/bind.h"  // IWYU pragma: export
#include "absl/strings/internal/str_format/checker.h"  // IWYU pragma: export
#include "absl/strings/internal/str_format/extension.h"  // IWYU pragma: export
#include "absl/strings/internal/str_format/parser.h"  // IWYU pragma: export
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// UntypedFormatSpec
//
// A type-erased class that can be used directly within untyped API entry
// points. An `UntypedFormatSpec` is specifically used as an argument to
// `FormatUntyped()`.
//
// Example:
//
//   absl::UntypedFormatSpec format("%d");
//   std::string out;
//   CHECK(absl::FormatUntyped(&out, format, {absl::FormatArg(1)}));
class UntypedFormatSpec {
 public:
  UntypedFormatSpec() = delete;
  UntypedFormatSpec(const UntypedFormatSpec&) = delete;
  UntypedFormatSpec& operator=(const UntypedFormatSpec&) = delete;

  explicit UntypedFormatSpec(string_view s) : spec_(s) {}

 protected:
  explicit UntypedFormatSpec(
      absl::Nonnull<const str_format_internal::ParsedFormatBase*> pc)
      : spec_(pc) {}

 private:
  friend str_format_internal::UntypedFormatSpecImpl;
  str_format_internal::UntypedFormatSpecImpl spec_;
};

// FormatStreamed()
//
// Takes a streamable argument and returns an object that can print it
// with '%s'. Allows printing of types that have an `operator<<` but no
// intrinsic type support within `StrFormat()` itself.
//
// Example:
//
//   absl::StrFormat("%s", absl::FormatStreamed(obj));
template <typename T>
str_format_internal::StreamedWrapper<T> FormatStreamed(const T& v) {
  return str_format_internal::StreamedWrapper<T>(v);
}

// FormatCountCapture
//
// This class provides a way to safely wrap `StrFormat()` captures of `%n`
// conversions, which denote the number of characters written by a formatting
// operation to this point, into an integer value.
//
// This wrapper is designed to allow safe usage of `%n` within `StrFormat(); in
// the `printf()` family of functions, `%n` is not safe to use, as the `int *`
// buffer can be used to capture arbitrary data.
//
// Example:
//
//   int n = 0;
//   std::string s = absl::StrFormat("%s%d%n", "hello", 123,
//                       absl::FormatCountCapture(&n));
//   EXPECT_EQ(8, n);
class FormatCountCapture {
 public:
  explicit FormatCountCapture(absl::Nonnull<int*> p) : p_(p) {}

 private:
  // FormatCountCaptureHelper is used to define FormatConvertImpl() for this
  // class.
  friend struct str_format_internal::FormatCountCaptureHelper;
  // Unused() is here because of the false positive from -Wunused-private-field
  // p_ is used in the templated function of the friend FormatCountCaptureHelper
  // class.
  absl::Nonnull<int*> Unused() { return p_; }
  absl::Nonnull<int*> p_;
};

// FormatSpec
//
// The `FormatSpec` type defines the makeup of a format string within the
// `str_format` library. It is a variadic class template that is evaluated at
// compile-time, according to the format string and arguments that are passed to
// it.
//
// You should not need to manipulate this type directly. You should only name it
// if you are writing wrapper functions which accept format arguments that will
// be provided unmodified to functions in this library. Such a wrapper function
// might be a class method that provides format arguments and/or internally uses
// the result of formatting.
//
// For a `FormatSpec` to be valid at compile-time, it must be provided as
// either:
//
// * A `constexpr` literal or `absl::string_view`, which is how it is most often
//   used.
// * A `ParsedFormat` instantiation, which ensures the format string is
//   valid before use. (See below.)
//
// Example:
//
//   // Provided as a string literal.
//   absl::StrFormat("Welcome to %s, Number %d!", "The Village", 6);
//
//   // Provided as a constexpr absl::string_view.
//   constexpr absl::string_view formatString = "Welcome to %s, Number %d!";
//   absl::StrFormat(formatString, "The Village", 6);
//
//   // Provided as a pre-compiled ParsedFormat object.
//   // Note that this example is useful only for illustration purposes.
//   absl::ParsedFormat<'s', 'd'> formatString("Welcome to %s, Number %d!");
//   absl::StrFormat(formatString, "TheVillage", 6);
//
// A format string generally follows the POSIX syntax as used within the POSIX
// `printf` specification. (Exceptions are noted below.)
//
// (See http://pubs.opengroup.org/onlinepubs/9699919799/functions/fprintf.html)
//
// In specific, the `FormatSpec` supports the following type specifiers:
//   * `c` for characters
//   * `s` for strings
//   * `d` or `i` for integers
//   * `o` for unsigned integer conversions into octal
//   * `x` or `X` for unsigned integer conversions into hex
//   * `u` for unsigned integers
//   * `f` or `F` for floating point values into decimal notation
//   * `e` or `E` for floating point values into exponential notation
//   * `a` or `A` for floating point values into hex exponential notation
//   * `g` or `G` for floating point values into decimal or exponential
//     notation based on their precision
//   * `p` for pointer address values
//   * `n` for the special case of writing out the number of characters
//     written to this point. The resulting value must be captured within an
//     `absl::FormatCountCapture` type.
//   * `v` for values using the default format for a deduced type. These deduced
//     types include many of the primitive types denoted here as well as
//     user-defined types containing the proper extensions. (See below for more
//     information.)
//
// Implementation-defined behavior:
//   * A null pointer provided to "%s" or "%p" is output as "(nil)".
//   * A non-null pointer provided to "%p" is output in hex as if by %#x or
//     %#lx.
//
// NOTE: `o`, `x\X` and `u` will convert signed values to their unsigned
// counterpart before formatting.
//
// Examples:
//     "%c", 'a'                -> "a"
//     "%c", 32                 -> " "
//     "%s", "C"                -> "C"
//     "%s", std::string("C++") -> "C++"
//     "%d", -10                -> "-10"
//     "%o", 10                 -> "12"
//     "%x", 16                 -> "10"
//     "%f", 123456789          -> "123456789.000000"
//     "%e", .01                -> "1.00000e-2"
//     "%a", -3.0               -> "-0x1.8p+1"
//     "%g", .01                -> "1e-2"
//     "%p", (void*)&value      -> "0x7ffdeb6ad2a4"
//
//     int n = 0;
//     std::string s = absl::StrFormat(
//         "%s%d%n", "hello", 123, absl::FormatCountCapture(&n));
//     EXPECT_EQ(8, n);
//
// NOTE: the `v` specifier (for "value") is a type specifier not present in the
// POSIX specification. %v will format values according to their deduced type.
// `v` uses `d` for signed integer values, `u` for unsigned integer values, `g`
// for floating point values, and formats boolean values as "true"/"false"
// (instead of 1 or 0 for booleans formatted using d). `const char*` is not
// supported; please use `std::string` and `string_view`. `char` is also not
// supported due to ambiguity of the type. This specifier does not support
// modifiers.
//
// The `FormatSpec` intrinsically supports all of these fundamental C++ types:
//
// *   Characters: `char`, `signed char`, `unsigned char`, `wchar_t`
// *   Integers: `int`, `short`, `unsigned short`, `unsigned`, `long`,
//         `unsigned long`, `long long`, `unsigned long long`
// *   Enums: printed as their underlying integral value
// *   Floating-point: `float`, `double`, `long double`
//
// However, in the `str_format` library, a format conversion specifies a broader
// C++ conceptual category instead of an exact type. For example, `%s` binds to
// any string-like argument, so `std::string`, `std::wstring`,
// `absl::string_view`, `const char*`, and `const wchar_t*` are all accepted.
// Likewise, `%d` accepts any integer-like argument, etc.

template <typename... Args>
using FormatSpec = str_format_internal::FormatSpecTemplate<
    str_format_internal::ArgumentToConv<Args>()...>;

// ParsedFormat
//
// A `ParsedFormat` is a class template representing a preparsed `FormatSpec`,
// with template arguments specifying the conversion characters used within the
// format string. Such characters must be valid format type specifiers, and
// these type specifiers are checked at compile-time.
//
// Instances of `ParsedFormat` can be created, copied, and reused to speed up
// formatting loops. A `ParsedFormat` may either be constructed statically, or
// dynamically through its `New()` factory function, which only constructs a
// runtime object if the format is valid at that time.
//
// Example:
//
//   // Verified at compile time.
//   absl::ParsedFormat<'s', 'd'> format_string("Welcome to %s, Number %d!");
//   absl::StrFormat(format_string, "TheVillage", 6);
//
//   // Verified at runtime.
//   auto format_runtime = absl::ParsedFormat<'d'>::New(format_string);
//   if (format_runtime) {
//     value = absl::StrFormat(*format_runtime, i);
//   } else {
//     ... error case ...
//   }

#if defined(__cpp_nontype_template_parameter_auto)
// If C++17 is available, an 'extended' format is also allowed that can specify
// multiple conversion characters per format argument, using a combination of
// `absl::FormatConversionCharSet` enum values (logically a set union)
//  via the `|` operator. (Single character-based arguments are still accepted,
// but cannot be combined). Some common conversions also have predefined enum
// values, such as `absl::FormatConversionCharSet::kIntegral`.
//
// Example:
//   // Extended format supports multiple conversion characters per argument,
//   // specified via a combination of `FormatConversionCharSet` enums.
//   using MyFormat = absl::ParsedFormat<absl::FormatConversionCharSet::d |
//                                       absl::FormatConversionCharSet::x>;
//   MyFormat GetFormat(bool use_hex) {
//     if (use_hex) return MyFormat("foo %x bar");
//     return MyFormat("foo %d bar");
//   }
//   // `format` can be used with any value that supports 'd' and 'x',
//   // like `int`.
//   auto format = GetFormat(use_hex);
//   value = StringF(format, i);
template <auto... Conv>
using ParsedFormat = absl::str_format_internal::ExtendedParsedFormat<
    absl::str_format_internal::ToFormatConversionCharSet(Conv)...>;
#else
template <char... Conv>
using ParsedFormat = str_format_internal::ExtendedParsedFormat<
    absl::str_format_internal::ToFormatConversionCharSet(Conv)...>;
#endif  // defined(__cpp_nontype_template_parameter_auto)

// StrFormat()
//
// Returns a `string` given a `printf()`-style format string and zero or more
// additional arguments. Use it as you would `sprintf()`. `StrFormat()` is the
// primary formatting function within the `str_format` library, and should be
// used in most cases where you need type-safe conversion of types into
// formatted strings.
//
// The format string generally consists of ordinary character data along with
// one or more format conversion specifiers (denoted by the `%` character).
// Ordinary character data is returned unchanged into the result string, while
// each conversion specification performs a type substitution from
// `StrFormat()`'s other arguments. See the comments for `FormatSpec` for full
// information on the makeup of this format string.
//
// Example:
//
//   std::string s = absl::StrFormat(
//       "Welcome to %s, Number %d!", "The Village", 6);
//   EXPECT_EQ("Welcome to The Village, Number 6!", s);
//
// Returns an empty string in case of error.
template <typename... Args>
ABSL_MUST_USE_RESULT std::string StrFormat(const FormatSpec<Args...>& format,
                                           const Args&... args) {
  return str_format_internal::FormatPack(
      str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// StrAppendFormat()
//
// Appends to a `dst` string given a format string, and zero or more additional
// arguments, returning `*dst` as a convenience for chaining purposes. Appends
// nothing in case of error (but possibly alters its capacity).
//
// Example:
//
//   std::string orig("For example PI is approximately ");
//   std::cout << StrAppendFormat(&orig, "%12.6f", 3.14);
template <typename... Args>
std::string& StrAppendFormat(absl::Nonnull<std::string*> dst,
                             const FormatSpec<Args...>& format,
                             const Args&... args) {
  return str_format_internal::AppendPack(
      dst, str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// StreamFormat()
//
// Writes to an output stream given a format string and zero or more arguments,
// generally in a manner that is more efficient than streaming the result of
// `absl::StrFormat()`. The returned object must be streamed before the full
// expression ends.
//
// Example:
//
//   std::cout << StreamFormat("%12.6f", 3.14);
template <typename... Args>
ABSL_MUST_USE_RESULT str_format_internal::Streamable StreamFormat(
    const FormatSpec<Args...>& format, const Args&... args) {
  return str_format_internal::Streamable(
      str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// PrintF()
//
// Writes to stdout given a format string and zero or more arguments. This
// function is functionally equivalent to `std::printf()` (and type-safe);
// prefer `absl::PrintF()` over `std::printf()`.
//
// Example:
//
//   std::string_view s = "Ulaanbaatar";
//   absl::PrintF("The capital of Mongolia is %s", s);
//
//   Outputs: "The capital of Mongolia is Ulaanbaatar"
//
template <typename... Args>
int PrintF(const FormatSpec<Args...>& format, const Args&... args) {
  return str_format_internal::FprintF(
      stdout, str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// FPrintF()
//
// Writes to a file given a format string and zero or more arguments. This
// function is functionally equivalent to `std::fprintf()` (and type-safe);
// prefer `absl::FPrintF()` over `std::fprintf()`.
//
// Example:
//
//   std::string_view s = "Ulaanbaatar";
//   absl::FPrintF(stdout, "The capital of Mongolia is %s", s);
//
//   Outputs: "The capital of Mongolia is Ulaanbaatar"
//
template <typename... Args>
int FPrintF(absl::Nonnull<std::FILE*> output, const FormatSpec<Args...>& format,
            const Args&... args) {
  return str_format_internal::FprintF(
      output, str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// SNPrintF()
//
// Writes to a sized buffer given a format string and zero or more arguments.
// This function is functionally equivalent to `std::snprintf()` (and
// type-safe); prefer `absl::SNPrintF()` over `std::snprintf()`.
//
// In particular, a successful call to `absl::SNPrintF()` writes at most `size`
// bytes of the formatted output to `output`, including a NUL-terminator, and
// returns the number of bytes that would have been written if truncation did
// not occur. In the event of an error, a negative value is returned and `errno`
// is set.
//
// Example:
//
//   std::string_view s = "Ulaanbaatar";
//   char output[128];
//   absl::SNPrintF(output, sizeof(output),
//                  "The capital of Mongolia is %s", s);
//
//   Post-condition: output == "The capital of Mongolia is Ulaanbaatar"
//
template <typename... Args>
int SNPrintF(absl::Nonnull<char*> output, std::size_t size,
             const FormatSpec<Args...>& format, const Args&... args) {
  return str_format_internal::SnprintF(
      output, size, str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// -----------------------------------------------------------------------------
// Custom Output Formatting Functions
// -----------------------------------------------------------------------------

// FormatRawSink
//
// FormatRawSink is a type erased wrapper around arbitrary sink objects
// specifically used as an argument to `Format()`.
//
// All the object has to do define an overload of `AbslFormatFlush()` for the
// sink, usually by adding a ADL-based free function in the same namespace as
// the sink:
//
//   void AbslFormatFlush(MySink* dest, absl::string_view part);
//
// where `dest` is the pointer passed to `absl::Format()`. The function should
// append `part` to `dest`.
//
// FormatRawSink does not own the passed sink object. The passed object must
// outlive the FormatRawSink.
class FormatRawSink {
 public:
  // Implicitly convert from any type that provides the hook function as
  // described above.
  template <typename T,
            typename = typename std::enable_if<std::is_constructible<
                str_format_internal::FormatRawSinkImpl, T*>::value>::type>
  FormatRawSink(absl::Nonnull<T*> raw)  // NOLINT
      : sink_(raw) {}

 private:
  friend str_format_internal::FormatRawSinkImpl;
  str_format_internal::FormatRawSinkImpl sink_;
};

// Format()
//
// Writes a formatted string to an arbitrary sink object (implementing the
// `absl::FormatRawSink` interface), using a format string and zero or more
// additional arguments.
//
// By default, `std::string`, `std::ostream`, and `absl::Cord` are supported as
// destination objects. If a `std::string` is used the formatted string is
// appended to it.
//
// `absl::Format()` is a generic version of `absl::StrAppendFormat()`, for
// custom sinks. The format string, like format strings for `StrFormat()`, is
// checked at compile-time.
//
// On failure, this function returns `false` and the state of the sink is
// unspecified.
template <typename... Args>
bool Format(FormatRawSink raw_sink, const FormatSpec<Args...>& format,
            const Args&... args) {
  return str_format_internal::FormatUntyped(
      str_format_internal::FormatRawSinkImpl::Extract(raw_sink),
      str_format_internal::UntypedFormatSpecImpl::Extract(format),
      {str_format_internal::FormatArgImpl(args)...});
}

// FormatArg
//
// A type-erased handle to a format argument specifically used as an argument to
// `FormatUntyped()`. You may construct `FormatArg` by passing
// reference-to-const of any printable type. `FormatArg` is both copyable and
// assignable. The source data must outlive the `FormatArg` instance. See
// example below.
//
using FormatArg = str_format_internal::FormatArgImpl;

// FormatUntyped()
//
// Writes a formatted string to an arbitrary sink object (implementing the
// `absl::FormatRawSink` interface), using an `UntypedFormatSpec` and zero or
// more additional arguments.
//
// This function acts as the most generic formatting function in the
// `str_format` library. The caller provides a raw sink, an unchecked format
// string, and (usually) a runtime specified list of arguments; no compile-time
// checking of formatting is performed within this function. As a result, a
// caller should check the return value to verify that no error occurred.
// On failure, this function returns `false` and the state of the sink is
// unspecified.
//
// The arguments are provided in an `absl::Span<const absl::FormatArg>`.
// Each `absl::FormatArg` object binds to a single argument and keeps a
// reference to it. The values used to create the `FormatArg` objects must
// outlive this function call.
//
// Example:
//
//   std::optional<std::string> FormatDynamic(
//       const std::string& in_format,
//       const vector<std::string>& in_args) {
//     std::string out;
//     std::vector<absl::FormatArg> args;
//     for (const auto& v : in_args) {
//       // It is important that 'v' is a reference to the objects in in_args.
//       // The values we pass to FormatArg must outlive the call to
//       // FormatUntyped.
//       args.emplace_back(v);
//     }
//     absl::UntypedFormatSpec format(in_format);
//     if (!absl::FormatUntyped(&out, format, args)) {
//       return std::nullopt;
//     }
//     return std::move(out);
//   }
//
ABSL_MUST_USE_RESULT inline bool FormatUntyped(
    FormatRawSink raw_sink, const UntypedFormatSpec& format,
    absl::Span<const FormatArg> args) {
  return str_format_internal::FormatUntyped(
      str_format_internal::FormatRawSinkImpl::Extract(raw_sink),
      str_format_internal::UntypedFormatSpecImpl::Extract(format), args);
}

//------------------------------------------------------------------------------
// StrFormat Extensions
//------------------------------------------------------------------------------
//
// AbslStringify()
//
// A simpler customization API for formatting user-defined types using
// absl::StrFormat(). The API relies on detecting an overload in the
// user-defined type's namespace of a free (non-member) `AbslStringify()`
// function as a friend definition with the following signature:
//
// template <typename Sink>
// void AbslStringify(Sink& sink, const X& value);
//
// An `AbslStringify()` overload for a type should only be declared in the same
// file and namespace as said type.
//
// Note that unlike with AbslFormatConvert(), AbslStringify() does not allow
// customization of allowed conversion characters. AbslStringify() uses `%v` as
// the underlying conversion specififer. Additionally, AbslStringify() supports
// use with absl::StrCat while AbslFormatConvert() does not.
//
// Example:
//
// struct Point {
//   // To add formatting support to `Point`, we simply need to add a free
//   // (non-member) function `AbslStringify()`. This method prints in the
//   // request format using the underlying `%v` specifier. You can add such a
//   // free function using a friend declaration within the body of the class.
//   // The sink parameter is a templated type to avoid requiring dependencies.
//   template <typename Sink>
//   friend void AbslStringify(Sink& sink, const Point& p) {
//     absl::Format(&sink, "(%v, %v)", p.x, p.y);
//   }
//
//   int x;
//   int y;
// };
//
// AbslFormatConvert()
//
// The StrFormat library provides a customization API for formatting
// user-defined types using absl::StrFormat(). The API relies on detecting an
// overload in the user-defined type's namespace of a free (non-member)
// `AbslFormatConvert()` function, usually as a friend definition with the
// following signature:
//
// absl::FormatConvertResult<...> AbslFormatConvert(
//     const X& value,
//     const absl::FormatConversionSpec& spec,
//     absl::FormatSink *sink);
//
// An `AbslFormatConvert()` overload for a type should only be declared in the
// same file and namespace as said type.
//
// The abstractions within this definition include:
//
// * An `absl::FormatConversionSpec` to specify the fields to pull from a
//   user-defined type's format string
// * An `absl::FormatSink` to hold the converted string data during the
//   conversion process.
// * An `absl::FormatConvertResult` to hold the status of the returned
//   formatting operation
//
// The return type encodes all the conversion characters that your
// AbslFormatConvert() routine accepts.  The return value should be {true}.
// A return value of {false} will result in `StrFormat()` returning
// an empty string.  This result will be propagated to the result of
// `FormatUntyped`.
//
// Example:
//
// struct Point {
//   // To add formatting support to `Point`, we simply need to add a free
//   // (non-member) function `AbslFormatConvert()`.  This method interprets
//   // `spec` to print in the request format. The allowed conversion characters
//   // can be restricted via the type of the result, in this example
//   // string and integral formatting are allowed (but not, for instance
//   // floating point characters like "%f").  You can add such a free function
//   // using a friend declaration within the body of the class:
//   friend absl::FormatConvertResult<absl::FormatConversionCharSet::kString |
//                                    absl::FormatConversionCharSet::kIntegral>
//   AbslFormatConvert(const Point& p, const absl::FormatConversionSpec& spec,
//                     absl::FormatSink* s) {
//     if (spec.conversion_char() == absl::FormatConversionChar::s) {
//       absl::Format(s, "x=%vy=%v", p.x, p.y);
//     } else {
//       absl::Format(s, "%v,%v", p.x, p.y);
//     }
//     return {true};
//   }
//
//   int x;
//   int y;
// };

// clang-format off

// FormatConversionChar
//
// Specifies the formatting character provided in the format string
// passed to `StrFormat()`.
enum class FormatConversionChar : uint8_t {
  c, s,                    // text
  d, i, o, u, x, X,        // int
  f, F, e, E, g, G, a, A,  // float
  n, p, v                  // misc
};
// clang-format on

// FormatConversionSpec
//
// Specifies modifications to the conversion of the format string, through use
// of one or more format flags in the source format string.
class FormatConversionSpec {
 public:
  // FormatConversionSpec::is_basic()
  //
  // Indicates that width and precision are not specified, and no additional
  // flags are set for this conversion character in the format string.
  bool is_basic() const { return impl_.is_basic(); }

  // FormatConversionSpec::has_left_flag()
  //
  // Indicates whether the result should be left justified for this conversion
  // character in the format string. This flag is set through use of a '-'
  // character in the format string. E.g. "%-s"
  bool has_left_flag() const { return impl_.has_left_flag(); }

  // FormatConversionSpec::has_show_pos_flag()
  //
  // Indicates whether a sign column is prepended to the result for this
  // conversion character in the format string, even if the result is positive.
  // This flag is set through use of a '+' character in the format string.
  // E.g. "%+d"
  bool has_show_pos_flag() const { return impl_.has_show_pos_flag(); }

  // FormatConversionSpec::has_sign_col_flag()
  //
  // Indicates whether a mandatory sign column is added to the result for this
  // conversion character. This flag is set through use of a space character
  // (' ') in the format string. E.g. "% i"
  bool has_sign_col_flag() const { return impl_.has_sign_col_flag(); }

  // FormatConversionSpec::has_alt_flag()
  //
  // Indicates whether an "alternate" format is applied to the result for this
  // conversion character. Alternative forms depend on the type of conversion
  // character, and unallowed alternatives are undefined. This flag is set
  // through use of a '#' character in the format string. E.g. "%#h"
  bool has_alt_flag() const { return impl_.has_alt_flag(); }

  // FormatConversionSpec::has_zero_flag()
  //
  // Indicates whether zeroes should be prepended to the result for this
  // conversion character instead of spaces. This flag is set through use of the
  // '0' character in the format string. E.g. "%0f"
  bool has_zero_flag() const { return impl_.has_zero_flag(); }

  // FormatConversionSpec::conversion_char()
  //
  // Returns the underlying conversion character.
  FormatConversionChar conversion_char() const {
    return impl_.conversion_char();
  }

  // FormatConversionSpec::width()
  //
  // Returns the specified width (indicated through use of a non-zero integer
  // value or '*' character) of the conversion character. If width is
  // unspecified, it returns a negative value.
  int width() const { return impl_.width(); }

  // FormatConversionSpec::precision()
  //
  // Returns the specified precision (through use of the '.' character followed
  // by a non-zero integer value or '*' character) of the conversion character.
  // If precision is unspecified, it returns a negative value.
  int precision() const { return impl_.precision(); }

 private:
  explicit FormatConversionSpec(
      str_format_internal::FormatConversionSpecImpl impl)
      : impl_(impl) {}

  friend str_format_internal::FormatConversionSpecImpl;

  absl::str_format_internal::FormatConversionSpecImpl impl_;
};

// Type safe OR operator for FormatConversionCharSet to allow accepting multiple
// conversion chars in custom format converters.
constexpr FormatConversionCharSet operator|(FormatConversionCharSet a,
                                            FormatConversionCharSet b) {
  return static_cast<FormatConversionCharSet>(static_cast<uint64_t>(a) |
                                              static_cast<uint64_t>(b));
}

// FormatConversionCharSet
//
// Specifies the _accepted_ conversion types as a template parameter to
// FormatConvertResult for custom implementations of `AbslFormatConvert`.
// Note the helper predefined alias definitions (kIntegral, etc.) below.
enum class FormatConversionCharSet : uint64_t {
  // text
  c = str_format_internal::FormatConversionCharToConvInt('c'),
  s = str_format_internal::FormatConversionCharToConvInt('s'),
  // integer
  d = str_format_internal::FormatConversionCharToConvInt('d'),
  i = str_format_internal::FormatConversionCharToConvInt('i'),
  o = str_format_internal::FormatConversionCharToConvInt('o'),
  u = str_format_internal::FormatConversionCharToConvInt('u'),
  x = str_format_internal::FormatConversionCharToConvInt('x'),
  X = str_format_internal::FormatConversionCharToConvInt('X'),
  // Float
  f = str_format_internal::FormatConversionCharToConvInt('f'),
  F = str_format_internal::FormatConversionCharToConvInt('F'),
  e = str_format_internal::FormatConversionCharToConvInt('e'),
  E = str_format_internal::FormatConversionCharToConvInt('E'),
  g = str_format_internal::FormatConversionCharToConvInt('g'),
  G = str_format_internal::FormatConversionCharToConvInt('G'),
  a = str_format_internal::FormatConversionCharToConvInt('a'),
  A = str_format_internal::FormatConversionCharToConvInt('A'),
  // misc
  n = str_format_internal::FormatConversionCharToConvInt('n'),
  p = str_format_internal::FormatConversionCharToConvInt('p'),
  v = str_format_internal::FormatConversionCharToConvInt('v'),

  // Used for width/precision '*' specification.
  kStar = static_cast<uint64_t>(
      absl::str_format_internal::FormatConversionCharSetInternal::kStar),
  // Some predefined values:
  kIntegral = d | i | u | o | x | X,
  kFloating = a | e | f | g | A | E | F | G,
  kNumeric = kIntegral | kFloating,
  kString = s,
  kPointer = p,
};

// FormatSink
//
// A format sink is a generic abstraction to which conversions may write their
// formatted string data. `absl::FormatConvert()` uses this sink to write its
// formatted string.
//
class FormatSink {
 public:
  // FormatSink::Append()
  //
  // Appends `count` copies of `ch` to the format sink.
  void Append(size_t count, char ch) { sink_->Append(count, ch); }

  // Overload of FormatSink::Append() for appending the characters of a string
  // view to a format sink.
  void Append(string_view v) { sink_->Append(v); }

  // FormatSink::PutPaddedString()
  //
  // Appends `precision` number of bytes of `v` to the format sink. If this is
  // less than `width`, spaces will be appended first (if `left` is false), or
  // after (if `left` is true) to ensure the total amount appended is
  // at least `width`.
  bool PutPaddedString(string_view v, int width, int precision, bool left) {
    return sink_->PutPaddedString(v, width, precision, left);
  }

  // Support `absl::Format(&sink, format, args...)`.
  friend void AbslFormatFlush(absl::Nonnull<FormatSink*> sink,
                              absl::string_view v) {
    sink->Append(v);
  }

 private:
  friend str_format_internal::FormatSinkImpl;
  explicit FormatSink(absl::Nonnull<str_format_internal::FormatSinkImpl*> s)
      : sink_(s) {}
  absl::Nonnull<str_format_internal::FormatSinkImpl*> sink_;
};

// FormatConvertResult
//
// Indicates whether a call to AbslFormatConvert() was successful.
// This return type informs the StrFormat extension framework (through
// ADL but using the return type) of what conversion characters are supported.
// It is strongly discouraged to return {false}, as this will result in an
// empty string in StrFormat.
template <FormatConversionCharSet C>
struct FormatConvertResult {
  bool value;
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_STR_FORMAT_H_
