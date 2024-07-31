//
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
//

// This file declares INTERNAL parts of the Join API that are inlined/templated
// or otherwise need to be available at compile time. The main abstractions
// defined in this file are:
//
//   - A handful of default Formatters
//   - JoinAlgorithm() overloads
//   - JoinRange() overloads
//   - JoinTuple()
//
// DO NOT INCLUDE THIS FILE DIRECTLY. Use this file by including
// absl/strings/str_join.h
//
// IWYU pragma: private, include "absl/strings/str_join.h"

#ifndef ABSL_STRINGS_INTERNAL_STR_JOIN_INTERNAL_H_
#define ABSL_STRINGS_INTERNAL_STR_JOIN_INTERNAL_H_

#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/internal/ostringstream.h"
#include "absl/strings/internal/resize_uninitialized.h"
#include "absl/strings/str_cat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

//
// Formatter objects
//
// The following are implementation classes for standard Formatter objects. The
// factory functions that users will call to create and use these formatters are
// defined and documented in strings/join.h.
//

// The default formatter. Converts alpha-numeric types to strings.
struct AlphaNumFormatterImpl {
  // This template is needed in order to support passing in a dereferenced
  // vector<bool>::iterator
  template <typename T>
  void operator()(std::string* out, const T& t) const {
    StrAppend(out, AlphaNum(t));
  }

  void operator()(std::string* out, const AlphaNum& t) const {
    StrAppend(out, t);
  }
};

// A type that's used to overload the JoinAlgorithm() function (defined below)
// for ranges that do not require additional formatting (e.g., a range of
// strings).

struct NoFormatter : public AlphaNumFormatterImpl {};

// Formats types to strings using the << operator.
class StreamFormatterImpl {
 public:
  // The method isn't const because it mutates state. Making it const will
  // render StreamFormatterImpl thread-hostile.
  template <typename T>
  void operator()(std::string* out, const T& t) {
    // The stream is created lazily to avoid paying the relatively high cost
    // of its construction when joining an empty range.
    if (strm_) {
      strm_->clear();  // clear the bad, fail and eof bits in case they were set
      strm_->str(out);
    } else {
      strm_.reset(new strings_internal::OStringStream(out));
    }
    *strm_ << t;
  }

 private:
  std::unique_ptr<strings_internal::OStringStream> strm_;
};

// Formats a std::pair<>. The 'first' member is formatted using f1_ and the
// 'second' member is formatted using f2_. sep_ is the separator.
template <typename F1, typename F2>
class PairFormatterImpl {
 public:
  PairFormatterImpl(F1 f1, absl::string_view sep, F2 f2)
      : f1_(std::move(f1)), sep_(sep), f2_(std::move(f2)) {}

  template <typename T>
  void operator()(std::string* out, const T& p) {
    f1_(out, p.first);
    out->append(sep_);
    f2_(out, p.second);
  }

  template <typename T>
  void operator()(std::string* out, const T& p) const {
    f1_(out, p.first);
    out->append(sep_);
    f2_(out, p.second);
  }

 private:
  F1 f1_;
  std::string sep_;
  F2 f2_;
};

// Wraps another formatter and dereferences the argument to operator() then
// passes the dereferenced argument to the wrapped formatter. This can be
// useful, for example, to join a std::vector<int*>.
template <typename Formatter>
class DereferenceFormatterImpl {
 public:
  DereferenceFormatterImpl() : f_() {}
  explicit DereferenceFormatterImpl(Formatter&& f)
      : f_(std::forward<Formatter>(f)) {}

  template <typename T>
  void operator()(std::string* out, const T& t) {
    f_(out, *t);
  }

  template <typename T>
  void operator()(std::string* out, const T& t) const {
    f_(out, *t);
  }

 private:
  Formatter f_;
};

// DefaultFormatter<T> is a traits class that selects a default Formatter to use
// for the given type T. The ::Type member names the Formatter to use. This is
// used by the strings::Join() functions that do NOT take a Formatter argument,
// in which case a default Formatter must be chosen.
//
// AlphaNumFormatterImpl is the default in the base template, followed by
// specializations for other types.
template <typename ValueType>
struct DefaultFormatter {
  typedef AlphaNumFormatterImpl Type;
};
template <>
struct DefaultFormatter<const char*> {
  typedef AlphaNumFormatterImpl Type;
};
template <>
struct DefaultFormatter<char*> {
  typedef AlphaNumFormatterImpl Type;
};
template <>
struct DefaultFormatter<std::string> {
  typedef NoFormatter Type;
};
template <>
struct DefaultFormatter<absl::string_view> {
  typedef NoFormatter Type;
};
template <typename ValueType>
struct DefaultFormatter<ValueType*> {
  typedef DereferenceFormatterImpl<typename DefaultFormatter<ValueType>::Type>
      Type;
};

template <typename ValueType>
struct DefaultFormatter<std::unique_ptr<ValueType>>
    : public DefaultFormatter<ValueType*> {};

//
// JoinAlgorithm() functions
//

// The main joining algorithm. This simply joins the elements in the given
// iterator range, each separated by the given separator, into an output string,
// and formats each element using the provided Formatter object.
template <typename Iterator, typename Formatter>
std::string JoinAlgorithm(Iterator start, Iterator end, absl::string_view s,
                          Formatter&& f) {
  std::string result;
  absl::string_view sep("");
  for (Iterator it = start; it != end; ++it) {
    result.append(sep.data(), sep.size());
    f(&result, *it);
    sep = s;
  }
  return result;
}

// A joining algorithm that's optimized for a forward iterator range of
// string-like objects that do not need any additional formatting. This is to
// optimize the common case of joining, say, a std::vector<string> or a
// std::vector<absl::string_view>.
//
// This is an overload of the previous JoinAlgorithm() function. Here the
// Formatter argument is of type NoFormatter. Since NoFormatter is an internal
// type, this overload is only invoked when strings::Join() is called with a
// range of string-like objects (e.g., std::string, absl::string_view), and an
// explicit Formatter argument was NOT specified.
//
// The optimization is that the needed space will be reserved in the output
// string to avoid the need to resize while appending. To do this, the iterator
// range will be traversed twice: once to calculate the total needed size, and
// then again to copy the elements and delimiters to the output string.
template <typename Iterator,
          typename = typename std::enable_if<std::is_convertible<
              typename std::iterator_traits<Iterator>::iterator_category,
              std::forward_iterator_tag>::value>::type>
std::string JoinAlgorithm(Iterator start, Iterator end, absl::string_view s,
                          NoFormatter) {
  std::string result;
  if (start != end) {
    // Sums size
    auto&& start_value = *start;
    size_t result_size = start_value.size();
    for (Iterator it = start; ++it != end;) {
      result_size += s.size();
      result_size += (*it).size();
    }

    if (result_size > 0) {
      STLStringResizeUninitialized(&result, result_size);

      // Joins strings
      char* result_buf = &*result.begin();

      memcpy(result_buf, start_value.data(), start_value.size());
      result_buf += start_value.size();
      for (Iterator it = start; ++it != end;) {
        memcpy(result_buf, s.data(), s.size());
        result_buf += s.size();
        auto&& value = *it;
        memcpy(result_buf, value.data(), value.size());
        result_buf += value.size();
      }
    }
  }

  return result;
}

// JoinTupleLoop implements a loop over the elements of a std::tuple, which
// are heterogeneous. The primary template matches the tuple interior case. It
// continues the iteration after appending a separator (for nonzero indices)
// and formatting an element of the tuple. The specialization for the I=N case
// matches the end-of-tuple, and terminates the iteration.
template <size_t I, size_t N>
struct JoinTupleLoop {
  template <typename Tup, typename Formatter>
  void operator()(std::string* out, const Tup& tup, absl::string_view sep,
                  Formatter&& fmt) {
    if (I > 0) out->append(sep.data(), sep.size());
    fmt(out, std::get<I>(tup));
    JoinTupleLoop<I + 1, N>()(out, tup, sep, fmt);
  }
};
template <size_t N>
struct JoinTupleLoop<N, N> {
  template <typename Tup, typename Formatter>
  void operator()(std::string*, const Tup&, absl::string_view, Formatter&&) {}
};

template <typename... T, typename Formatter>
std::string JoinAlgorithm(const std::tuple<T...>& tup, absl::string_view sep,
                          Formatter&& fmt) {
  std::string result;
  JoinTupleLoop<0, sizeof...(T)>()(&result, tup, sep, fmt);
  return result;
}

template <typename Iterator>
std::string JoinRange(Iterator first, Iterator last,
                      absl::string_view separator) {
  // No formatter was explicitly given, so a default must be chosen.
  typedef typename std::iterator_traits<Iterator>::value_type ValueType;
  typedef typename DefaultFormatter<ValueType>::Type Formatter;
  return JoinAlgorithm(first, last, separator, Formatter());
}

template <typename Range, typename Formatter>
std::string JoinRange(const Range& range, absl::string_view separator,
                      Formatter&& fmt) {
  using std::begin;
  using std::end;
  return JoinAlgorithm(begin(range), end(range), separator, fmt);
}

template <typename Range>
std::string JoinRange(const Range& range, absl::string_view separator) {
  using std::begin;
  using std::end;
  return JoinRange(begin(range), end(range), separator);
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_JOIN_INTERNAL_H_
