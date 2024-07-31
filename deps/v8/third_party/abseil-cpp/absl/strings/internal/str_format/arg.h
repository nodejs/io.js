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

#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_

#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/int128.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/internal/str_format/extension.h"
#include "absl/strings/string_view.h"

#if defined(ABSL_HAVE_STD_STRING_VIEW)
#include <string_view>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

class Cord;
class FormatCountCapture;
class FormatSink;

template <absl::FormatConversionCharSet C>
struct FormatConvertResult;
class FormatConversionSpec;

namespace str_format_internal {

template <FormatConversionCharSet C>
struct ArgConvertResult {
  bool value;
};

using IntegralConvertResult = ArgConvertResult<FormatConversionCharSetUnion(
    FormatConversionCharSetInternal::c,
    FormatConversionCharSetInternal::kNumeric,
    FormatConversionCharSetInternal::kStar,
    FormatConversionCharSetInternal::v)>;
using FloatingConvertResult = ArgConvertResult<FormatConversionCharSetUnion(
    FormatConversionCharSetInternal::kFloating,
    FormatConversionCharSetInternal::v)>;
using CharConvertResult = ArgConvertResult<FormatConversionCharSetUnion(
    FormatConversionCharSetInternal::c,
    FormatConversionCharSetInternal::kNumeric,
    FormatConversionCharSetInternal::kStar)>;

template <typename T, typename = void>
struct HasUserDefinedConvert : std::false_type {};

template <typename T>
struct HasUserDefinedConvert<T, void_t<decltype(AbslFormatConvert(
                                    std::declval<const T&>(),
                                    std::declval<const FormatConversionSpec&>(),
                                    std::declval<FormatSink*>()))>>
    : std::true_type {};

// These declarations prevent ADL lookup from continuing in absl namespaces,
// we are deliberately using these as ADL hooks and want them to consider
// non-absl namespaces only.
void AbslFormatConvert();
void AbslStringify();

template <typename T>
bool ConvertIntArg(T v, FormatConversionSpecImpl conv, FormatSinkImpl* sink);

// Forward declarations of internal `ConvertIntArg` function template
// instantiations are here to avoid including the template body in the headers
// and instantiating it in large numbers of translation units. Explicit
// instantiations can be found in "absl/strings/internal/str_format/arg.cc"
extern template bool ConvertIntArg<char>(char v, FormatConversionSpecImpl conv,
                                         FormatSinkImpl* sink);
extern template bool ConvertIntArg<signed char>(signed char v,
                                                FormatConversionSpecImpl conv,
                                                FormatSinkImpl* sink);
extern template bool ConvertIntArg<unsigned char>(unsigned char v,
                                                  FormatConversionSpecImpl conv,
                                                  FormatSinkImpl* sink);
extern template bool ConvertIntArg<wchar_t>(wchar_t v,
                                            FormatConversionSpecImpl conv,
                                            FormatSinkImpl* sink);
extern template bool ConvertIntArg<short>(short v,  // NOLINT
                                          FormatConversionSpecImpl conv,
                                          FormatSinkImpl* sink);
extern template bool ConvertIntArg<unsigned short>(   // NOLINT
    unsigned short v, FormatConversionSpecImpl conv,  // NOLINT
    FormatSinkImpl* sink);
extern template bool ConvertIntArg<int>(int v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
extern template bool ConvertIntArg<unsigned int>(unsigned int v,
                                                 FormatConversionSpecImpl conv,
                                                 FormatSinkImpl* sink);
extern template bool ConvertIntArg<long>(                           // NOLINT
    long v, FormatConversionSpecImpl conv, FormatSinkImpl* sink);   // NOLINT
extern template bool ConvertIntArg<unsigned long>(unsigned long v,  // NOLINT
                                                  FormatConversionSpecImpl conv,
                                                  FormatSinkImpl* sink);
extern template bool ConvertIntArg<long long>(long long v,  // NOLINT
                                              FormatConversionSpecImpl conv,
                                              FormatSinkImpl* sink);
extern template bool ConvertIntArg<unsigned long long>(   // NOLINT
    unsigned long long v, FormatConversionSpecImpl conv,  // NOLINT
    FormatSinkImpl* sink);

template <typename T>
auto FormatConvertImpl(const T& v, FormatConversionSpecImpl conv,
                       FormatSinkImpl* sink)
    -> decltype(AbslFormatConvert(v,
                                  std::declval<const FormatConversionSpec&>(),
                                  std::declval<FormatSink*>())) {
  using FormatConversionSpecT =
      absl::enable_if_t<sizeof(const T& (*)()) != 0, FormatConversionSpec>;
  using FormatSinkT =
      absl::enable_if_t<sizeof(const T& (*)()) != 0, FormatSink>;
  auto fcs = conv.Wrap<FormatConversionSpecT>();
  auto fs = sink->Wrap<FormatSinkT>();
  return AbslFormatConvert(v, fcs, &fs);
}

template <typename T>
auto FormatConvertImpl(const T& v, FormatConversionSpecImpl conv,
                       FormatSinkImpl* sink)
    -> std::enable_if_t<std::is_enum<T>::value &&
                            std::is_void<decltype(AbslStringify(
                                std::declval<FormatSink&>(), v))>::value,
                        IntegralConvertResult> {
  if (conv.conversion_char() == FormatConversionCharInternal::v) {
    using FormatSinkT =
        absl::enable_if_t<sizeof(const T& (*)()) != 0, FormatSink>;
    auto fs = sink->Wrap<FormatSinkT>();
    AbslStringify(fs, v);
    return {true};
  } else {
    return {ConvertIntArg(
        static_cast<typename std::underlying_type<T>::type>(v), conv, sink)};
  }
}

template <typename T>
auto FormatConvertImpl(const T& v, FormatConversionSpecImpl,
                       FormatSinkImpl* sink)
    -> std::enable_if_t<!std::is_enum<T>::value &&
                            !std::is_same<T, absl::Cord>::value &&
                            std::is_void<decltype(AbslStringify(
                                std::declval<FormatSink&>(), v))>::value,
                        ArgConvertResult<FormatConversionCharSetInternal::v>> {
  using FormatSinkT =
      absl::enable_if_t<sizeof(const T& (*)()) != 0, FormatSink>;
  auto fs = sink->Wrap<FormatSinkT>();
  AbslStringify(fs, v);
  return {true};
}

template <typename T>
class StreamedWrapper;

// If 'v' can be converted (in the printf sense) according to 'conv',
// then convert it, appending to `sink` and return `true`.
// Otherwise fail and return `false`.

// AbslFormatConvert(v, conv, sink) is intended to be found by ADL on 'v'
// as an extension mechanism. These FormatConvertImpl functions are the default
// implementations.
// The ADL search is augmented via the 'Sink*' parameter, which also
// serves as a disambiguator to reject possible unintended 'AbslFormatConvert'
// functions in the namespaces associated with 'v'.

// Raw pointers.
struct VoidPtr {
  VoidPtr() = default;
  template <typename T,
            decltype(reinterpret_cast<uintptr_t>(std::declval<T*>())) = 0>
  VoidPtr(T* ptr)  // NOLINT
      : value(ptr ? reinterpret_cast<uintptr_t>(ptr) : 0) {}
  uintptr_t value;
};

template <FormatConversionCharSet C>
constexpr FormatConversionCharSet ExtractCharSet(FormatConvertResult<C>) {
  return C;
}

template <FormatConversionCharSet C>
constexpr FormatConversionCharSet ExtractCharSet(ArgConvertResult<C>) {
  return C;
}

ArgConvertResult<FormatConversionCharSetInternal::p> FormatConvertImpl(
    VoidPtr v, FormatConversionSpecImpl conv, FormatSinkImpl* sink);

// Strings.
using StringConvertResult = ArgConvertResult<FormatConversionCharSetUnion(
    FormatConversionCharSetInternal::s,
    FormatConversionCharSetInternal::v)>;
StringConvertResult FormatConvertImpl(const std::string& v,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* sink);
StringConvertResult FormatConvertImpl(const std::wstring& v,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* sink);
StringConvertResult FormatConvertImpl(string_view v,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* sink);
#if defined(ABSL_HAVE_STD_STRING_VIEW)
StringConvertResult FormatConvertImpl(std::wstring_view v,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* sink);
#if !defined(ABSL_USES_STD_STRING_VIEW)
inline StringConvertResult FormatConvertImpl(std::string_view v,
                                             FormatConversionSpecImpl conv,
                                             FormatSinkImpl* sink) {
  return FormatConvertImpl(absl::string_view(v.data(), v.size()), conv, sink);
}
#endif  // !ABSL_USES_STD_STRING_VIEW
#endif  // ABSL_HAVE_STD_STRING_VIEW

using StringPtrConvertResult = ArgConvertResult<FormatConversionCharSetUnion(
    FormatConversionCharSetInternal::s,
    FormatConversionCharSetInternal::p)>;
StringPtrConvertResult FormatConvertImpl(const char* v,
                                         FormatConversionSpecImpl conv,
                                         FormatSinkImpl* sink);
StringPtrConvertResult FormatConvertImpl(const wchar_t* v,
                                         FormatConversionSpecImpl conv,
                                         FormatSinkImpl* sink);
// This overload is needed to disambiguate, since `nullptr` could match either
// of the other overloads equally well.
StringPtrConvertResult FormatConvertImpl(std::nullptr_t,
                                         FormatConversionSpecImpl conv,
                                         FormatSinkImpl* sink);

template <class AbslCord, typename std::enable_if<std::is_same<
                              AbslCord, absl::Cord>::value>::type* = nullptr>
StringConvertResult FormatConvertImpl(const AbslCord& value,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* sink) {
  bool is_left = conv.has_left_flag();
  size_t space_remaining = 0;

  int width = conv.width();
  if (width >= 0) space_remaining = static_cast<size_t>(width);

  size_t to_write = value.size();

  int precision = conv.precision();
  if (precision >= 0)
    to_write = (std::min)(to_write, static_cast<size_t>(precision));

  space_remaining = Excess(to_write, space_remaining);

  if (space_remaining > 0 && !is_left) sink->Append(space_remaining, ' ');

  for (string_view piece : value.Chunks()) {
    if (piece.size() > to_write) {
      piece.remove_suffix(piece.size() - to_write);
      to_write = 0;
    } else {
      to_write -= piece.size();
    }
    sink->Append(piece);
    if (to_write == 0) {
      break;
    }
  }

  if (space_remaining > 0 && is_left) sink->Append(space_remaining, ' ');
  return {true};
}

bool ConvertBoolArg(bool v, FormatSinkImpl* sink);

// Floats.
FloatingConvertResult FormatConvertImpl(float v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
FloatingConvertResult FormatConvertImpl(double v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
FloatingConvertResult FormatConvertImpl(long double v,
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);

// Chars.
CharConvertResult FormatConvertImpl(char v, FormatConversionSpecImpl conv,
                                    FormatSinkImpl* sink);
CharConvertResult FormatConvertImpl(wchar_t v,
                                    FormatConversionSpecImpl conv,
                                    FormatSinkImpl* sink);

// Ints.
IntegralConvertResult FormatConvertImpl(signed char v,
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned char v,
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(short v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned short v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(int v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned v,
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(long v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned long v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(long long v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(unsigned long long v,  // NOLINT
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(int128 v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);
IntegralConvertResult FormatConvertImpl(uint128 v,
                                        FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink);

// This function needs to be a template due to ambiguity regarding type
// conversions.
template <typename T, enable_if_t<std::is_same<T, bool>::value, int> = 0>
IntegralConvertResult FormatConvertImpl(T v, FormatConversionSpecImpl conv,
                                        FormatSinkImpl* sink) {
  if (conv.conversion_char() == FormatConversionCharInternal::v) {
    return {ConvertBoolArg(v, sink)};
  }

  return FormatConvertImpl(static_cast<int>(v), conv, sink);
}

// We provide this function to help the checker, but it is never defined.
// FormatArgImpl will use the underlying Convert functions instead.
template <typename T>
typename std::enable_if<std::is_enum<T>::value &&
                            !HasUserDefinedConvert<T>::value &&
                            !HasAbslStringify<T>::value,
                        IntegralConvertResult>::type
FormatConvertImpl(T v, FormatConversionSpecImpl conv, FormatSinkImpl* sink);

template <typename T>
StringConvertResult FormatConvertImpl(const StreamedWrapper<T>& v,
                                      FormatConversionSpecImpl conv,
                                      FormatSinkImpl* out) {
  std::ostringstream oss;
  oss << v.v_;
  if (!oss) return {false};
  return str_format_internal::FormatConvertImpl(oss.str(), conv, out);
}

// Use templates and dependent types to delay evaluation of the function
// until after FormatCountCapture is fully defined.
struct FormatCountCaptureHelper {
  template <class T = int>
  static ArgConvertResult<FormatConversionCharSetInternal::n> ConvertHelper(
      const FormatCountCapture& v, FormatConversionSpecImpl conv,
      FormatSinkImpl* sink) {
    const absl::enable_if_t<sizeof(T) != 0, FormatCountCapture>& v2 = v;

    if (conv.conversion_char() !=
        str_format_internal::FormatConversionCharInternal::n) {
      return {false};
    }
    *v2.p_ = static_cast<int>(sink->size());
    return {true};
  }
};

template <class T = int>
ArgConvertResult<FormatConversionCharSetInternal::n> FormatConvertImpl(
    const FormatCountCapture& v, FormatConversionSpecImpl conv,
    FormatSinkImpl* sink) {
  return FormatCountCaptureHelper::ConvertHelper(v, conv, sink);
}

// Helper friend struct to hide implementation details from the public API of
// FormatArgImpl.
struct FormatArgImplFriend {
  template <typename Arg>
  static bool ToInt(Arg arg, int* out) {
    // A value initialized FormatConversionSpecImpl has a `none` conv, which
    // tells the dispatcher to run the `int` conversion.
    return arg.dispatcher_(arg.data_, {}, out);
  }

  template <typename Arg>
  static bool Convert(Arg arg, FormatConversionSpecImpl conv,
                      FormatSinkImpl* out) {
    return arg.dispatcher_(arg.data_, conv, out);
  }

  template <typename Arg>
  static typename Arg::Dispatcher GetVTablePtrForTest(Arg arg) {
    return arg.dispatcher_;
  }
};

template <typename Arg>
constexpr FormatConversionCharSet ArgumentToConv() {
  using ConvResult = decltype(str_format_internal::FormatConvertImpl(
      std::declval<const Arg&>(),
      std::declval<const FormatConversionSpecImpl&>(),
      std::declval<FormatSinkImpl*>()));
  return absl::str_format_internal::ExtractCharSet(ConvResult{});
}

// A type-erased handle to a format argument.
class FormatArgImpl {
 private:
  enum { kInlinedSpace = 8 };

  using VoidPtr = str_format_internal::VoidPtr;

  union Data {
    const void* ptr;
    const volatile void* volatile_ptr;
    char buf[kInlinedSpace];
  };

  using Dispatcher = bool (*)(Data, FormatConversionSpecImpl, void* out);

  template <typename T>
  struct store_by_value
      : std::integral_constant<bool, (sizeof(T) <= kInlinedSpace) &&
                                         (std::is_integral<T>::value ||
                                          std::is_floating_point<T>::value ||
                                          std::is_pointer<T>::value ||
                                          std::is_same<VoidPtr, T>::value)> {};

  enum StoragePolicy { ByPointer, ByVolatilePointer, ByValue };
  template <typename T>
  struct storage_policy
      : std::integral_constant<StoragePolicy,
                               (std::is_volatile<T>::value
                                    ? ByVolatilePointer
                                    : (store_by_value<T>::value ? ByValue
                                                                : ByPointer))> {
  };

  // To reduce the number of vtables we will decay values before hand.
  // Anything with a user-defined Convert will get its own vtable.
  // For everything else:
  //   - Decay char* and char arrays into `const char*`
  //   - Decay wchar_t* and wchar_t arrays into `const wchar_t*`
  //   - Decay any other pointer to `const void*`
  //   - Decay all enums to the integral promotion of their underlying type.
  //   - Decay function pointers to void*.
  template <typename T, typename = void>
  struct DecayType {
    static constexpr bool kHasUserDefined =
        str_format_internal::HasUserDefinedConvert<T>::value ||
        HasAbslStringify<T>::value;
    using type = typename std::conditional<
        !kHasUserDefined && std::is_convertible<T, const char*>::value,
        const char*,
        typename std::conditional<
            !kHasUserDefined && std::is_convertible<T, const wchar_t*>::value,
            const wchar_t*,
            typename std::conditional<
                !kHasUserDefined && std::is_convertible<T, VoidPtr>::value,
                VoidPtr,
                const T&>::type>::type>::type;
  };
  template <typename T>
  struct DecayType<
      T, typename std::enable_if<
             !str_format_internal::HasUserDefinedConvert<T>::value &&
             !HasAbslStringify<T>::value && std::is_enum<T>::value>::type> {
    using type = decltype(+typename std::underlying_type<T>::type());
  };

 public:
  template <typename T>
  explicit FormatArgImpl(const T& value) {
    using D = typename DecayType<T>::type;
    static_assert(
        std::is_same<D, const T&>::value || storage_policy<D>::value == ByValue,
        "Decayed types must be stored by value");
    Init(static_cast<D>(value));
  }

 private:
  friend struct str_format_internal::FormatArgImplFriend;
  template <typename T, StoragePolicy = storage_policy<T>::value>
  struct Manager;

  template <typename T>
  struct Manager<T, ByPointer> {
    static Data SetValue(const T& value) {
      Data data;
      data.ptr = std::addressof(value);
      return data;
    }

    static const T& Value(Data arg) { return *static_cast<const T*>(arg.ptr); }
  };

  template <typename T>
  struct Manager<T, ByVolatilePointer> {
    static Data SetValue(const T& value) {
      Data data;
      data.volatile_ptr = &value;
      return data;
    }

    static const T& Value(Data arg) {
      return *static_cast<const T*>(arg.volatile_ptr);
    }
  };

  template <typename T>
  struct Manager<T, ByValue> {
    static Data SetValue(const T& value) {
      Data data;
      memcpy(data.buf, &value, sizeof(value));
      return data;
    }

    static T Value(Data arg) {
      T value;
      memcpy(&value, arg.buf, sizeof(T));
      return value;
    }
  };

  template <typename T>
  void Init(const T& value) {
    data_ = Manager<T>::SetValue(value);
    dispatcher_ = &Dispatch<T>;
  }

  template <typename T>
  static int ToIntVal(const T& val) {
    using CommonType = typename std::conditional<std::is_signed<T>::value,
                                                 int64_t, uint64_t>::type;
    if (static_cast<CommonType>(val) >
        static_cast<CommonType>((std::numeric_limits<int>::max)())) {
      return (std::numeric_limits<int>::max)();
    } else if (std::is_signed<T>::value &&
               static_cast<CommonType>(val) <
                   static_cast<CommonType>((std::numeric_limits<int>::min)())) {
      return (std::numeric_limits<int>::min)();
    }
    return static_cast<int>(val);
  }

  template <typename T>
  static bool ToInt(Data arg, int* out, std::true_type /* is_integral */,
                    std::false_type) {
    *out = ToIntVal(Manager<T>::Value(arg));
    return true;
  }

  template <typename T>
  static bool ToInt(Data arg, int* out, std::false_type,
                    std::true_type /* is_enum */) {
    *out = ToIntVal(static_cast<typename std::underlying_type<T>::type>(
        Manager<T>::Value(arg)));
    return true;
  }

  template <typename T>
  static bool ToInt(Data, int*, std::false_type, std::false_type) {
    return false;
  }

  template <typename T>
  static bool Dispatch(Data arg, FormatConversionSpecImpl spec, void* out) {
    // A `none` conv indicates that we want the `int` conversion.
    if (ABSL_PREDICT_FALSE(spec.conversion_char() ==
                           FormatConversionCharInternal::kNone)) {
      return ToInt<T>(arg, static_cast<int*>(out), std::is_integral<T>(),
                      std::is_enum<T>());
    }
    if (ABSL_PREDICT_FALSE(!Contains(ArgumentToConv<T>(),
                                     spec.conversion_char()))) {
      return false;
    }
    return str_format_internal::FormatConvertImpl(
               Manager<T>::Value(arg), spec,
               static_cast<FormatSinkImpl*>(out))
        .value;
  }

  Data data_;
  Dispatcher dispatcher_;
};

#define ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(T, E)                     \
  E template bool FormatArgImpl::Dispatch<T>(Data, FormatConversionSpecImpl, \
                                             void*)

#define ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_NO_WSTRING_VIEW_(...)   \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(str_format_internal::VoidPtr,     \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(bool, __VA_ARGS__);               \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(char, __VA_ARGS__);               \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(signed char, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned char, __VA_ARGS__);      \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(short, __VA_ARGS__); /* NOLINT */ \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned short,      /* NOLINT */ \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(int, __VA_ARGS__);                \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned int, __VA_ARGS__);       \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long, __VA_ARGS__); /* NOLINT */  \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned long,      /* NOLINT */  \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long long, /* NOLINT */           \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(unsigned long long, /* NOLINT */  \
                                             __VA_ARGS__);                     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(int128, __VA_ARGS__);             \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(uint128, __VA_ARGS__);            \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(float, __VA_ARGS__);              \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(double, __VA_ARGS__);             \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(long double, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(const char*, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(std::string, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(string_view, __VA_ARGS__);        \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(const wchar_t*, __VA_ARGS__);     \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(std::wstring, __VA_ARGS__)

#if defined(ABSL_HAVE_STD_STRING_VIEW)
#define ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_(...)       \
  ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_NO_WSTRING_VIEW_( \
      __VA_ARGS__);                                                \
  ABSL_INTERNAL_FORMAT_DISPATCH_INSTANTIATE_(std::wstring_view, __VA_ARGS__)
#else
#define ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_(...) \
  ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_NO_WSTRING_VIEW_(__VA_ARGS__)
#endif

ABSL_INTERNAL_FORMAT_DISPATCH_OVERLOADS_EXPAND_(extern);


}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_ARG_H_
