// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_UTILS_H_
#define V8_TORQUE_UTILS_H_

#include <algorithm>
#include <optional>
#include <ostream>
#include <queue>
#include <streambuf>
#include <string>
#include <unordered_set>

#include "src/base/contextual.h"
#include "src/base/hashing.h"
#include "src/torque/source-positions.h"

namespace v8::internal::torque {

std::string StringLiteralUnquote(const std::string& s);
std::string StringLiteralQuote(const std::string& s);

// Decodes "file://" URIs into file paths which can then be used
// with the standard stream API.
V8_EXPORT_PRIVATE std::optional<std::string> FileUriDecode(
    const std::string& s);

struct TorqueMessage {
  enum class Kind { kError, kLint };

  std::string message;
  std::optional<SourcePosition> position;
  Kind kind;
};

DECLARE_CONTEXTUAL_VARIABLE(TorqueMessages, std::vector<TorqueMessage>);

template <class... Args>
std::string ToString(Args&&... args) {
  std::stringstream stream;
  USE((stream << std::forward<Args>(args))...);
  return stream.str();
}

class V8_EXPORT_PRIVATE MessageBuilder {
 public:
  MessageBuilder() = delete;
  MessageBuilder(const std::string& message, TorqueMessage::Kind kind);

  MessageBuilder& Position(SourcePosition position) {
    message_.position = position;
    return *this;
  }

  [[noreturn]] void Throw() const;

  ~MessageBuilder() {
    // This will also get called in case the error is thrown.
    Report();
  }

 private:
  void Report() const;

  TorqueMessage message_;
  std::vector<TorqueMessage> extra_messages_;
};

// Used for throwing exceptions. Retrieve TorqueMessage from the contextual
// for specific error information.
struct TorqueAbortCompilation {};

template <class... Args>
static MessageBuilder Message(TorqueMessage::Kind kind, Args&&... args) {
  return MessageBuilder(ToString(std::forward<Args>(args)...), kind);
}

template <class... Args>
MessageBuilder Error(Args&&... args) {
  return Message(TorqueMessage::Kind::kError, std::forward<Args>(args)...);
}
template <class... Args>
MessageBuilder Lint(Args&&... args) {
  return Message(TorqueMessage::Kind::kLint, std::forward<Args>(args)...);
}

bool IsLowerCamelCase(const std::string& s);
bool IsUpperCamelCase(const std::string& s);
bool IsSnakeCase(const std::string& s);
bool IsValidNamespaceConstName(const std::string& s);
bool IsValidTypeName(const std::string& s);

template <class... Args>
[[noreturn]] void ReportError(Args&&... args) {
  Error(std::forward<Args>(args)...).Throw();
}

std::string CapifyStringWithUnderscores(const std::string& camellified_string);
std::string CamelifyString(const std::string& underscore_string);
std::string SnakeifyString(const std::string& camel_string);
std::string DashifyString(const std::string& underscore_string);
std::string UnderlinifyPath(std::string path);

bool StartsWithSingleUnderscore(const std::string& str);

void ReplaceFileContentsIfDifferent(const std::string& file_path,
                                    const std::string& contents);

template <class T>
class Deduplicator {
 public:
  const T* Add(T x) { return &*(storage_.insert(std::move(x)).first); }

 private:
  std::unordered_set<T, base::hash<T>> storage_;
};

template <class T>
T& DereferenceIfPointer(T* x) {
  return *x;
}
template <class T>
T&& DereferenceIfPointer(T&& x) {
  return std::forward<T>(x);
}

template <class T, class L>
struct ListPrintAdaptor {
  const T& list;
  const std::string& separator;
  L transformer;

  friend std::ostream& operator<<(std::ostream& os, const ListPrintAdaptor& l) {
    bool first = true;
    for (auto& e : l.list) {
      if (first) {
        first = false;
      } else {
        os << l.separator;
      }
      os << DereferenceIfPointer(l.transformer(e));
    }
    return os;
  }
};

template <class T>
auto PrintList(const T& list, const std::string& separator = ", ") {
  using ElementType = decltype(*list.begin());
  auto id = [](ElementType el) { return el; };
  return ListPrintAdaptor<T, decltype(id)>{list, separator, id};
}

template <class T, class L>
auto PrintList(const T& list, const std::string& separator, L&& transformer) {
  return ListPrintAdaptor<T, L&&>{list, separator,
                                  std::forward<L>(transformer)};
}

template <class C, class T>
void PrintCommaSeparatedList(std::ostream& os, const T& list, C&& transform) {
  os << PrintList(list, ", ", std::forward<C>(transform));
}

template <class T>
void PrintCommaSeparatedList(std::ostream& os, const T& list) {
  os << PrintList(list, ", ");
}

struct BottomOffset {
  size_t offset;

  BottomOffset& operator=(std::size_t other_offset) {
    this->offset = other_offset;
    return *this;
  }
  BottomOffset& operator++() {
    ++offset;
    return *this;
  }
  BottomOffset operator+(size_t x) const { return BottomOffset{offset + x}; }
  BottomOffset operator-(size_t x) const {
    DCHECK_LE(x, offset);
    return BottomOffset{offset - x};
  }
  bool operator<(const BottomOffset& other) const {
    return offset < other.offset;
  }
  bool operator<=(const BottomOffset& other) const {
    return offset <= other.offset;
  }
  bool operator==(const BottomOffset& other) const {
    return offset == other.offset;
  }
  bool operator!=(const BottomOffset& other) const {
    return offset != other.offset;
  }
};

inline std::ostream& operator<<(std::ostream& out, BottomOffset from_bottom) {
  return out << "BottomOffset{" << from_bottom.offset << "}";
}

// An iterator-style range of stack slots.
class StackRange {
 public:
  StackRange(BottomOffset begin, BottomOffset end) : begin_(begin), end_(end) {
    DCHECK_LE(begin_, end_);
  }

  bool operator==(const StackRange& other) const {
    return begin_ == other.begin_ && end_ == other.end_;
  }

  void Extend(StackRange adjacent) {
    DCHECK_EQ(end_, adjacent.begin_);
    end_ = adjacent.end_;
  }

  size_t Size() const { return end_.offset - begin_.offset; }
  BottomOffset begin() const { return begin_; }
  BottomOffset end() const { return end_; }

 private:
  BottomOffset begin_;
  BottomOffset end_;
};

inline std::ostream& operator<<(std::ostream& out, StackRange range) {
  return out << "StackRange{" << range.begin() << ", " << range.end() << "}";
}

template <class T>
class Stack {
 public:
  using value_type = T;
  Stack() = default;
  Stack(std::initializer_list<T> initializer)
      : Stack(std::vector<T>(initializer)) {}
  explicit Stack(std::vector<T> v) : elements_(std::move(v)) {}
  size_t Size() const { return elements_.size(); }
  const T& Peek(BottomOffset from_bottom) const {
    return elements_.at(from_bottom.offset);
  }
  void Poke(BottomOffset from_bottom, T x) {
    elements_.at(from_bottom.offset) = std::move(x);
  }
  void Push(T x) {
    elements_.push_back(std::move(x));
  }
  StackRange TopRange(size_t slot_count) const {
    DCHECK_GE(Size(), slot_count);
    return StackRange{AboveTop() - slot_count, AboveTop()};
  }
  StackRange PushMany(const std::vector<T>& v) {
    for (const T& x : v) {
      Push(x);
    }
    return TopRange(v.size());
  }
  const T& Top() const { return Peek(AboveTop() - 1); }
  T Pop() {
    T result = std::move(elements_.back());
    elements_.pop_back();
    return result;
  }
  std::vector<T> PopMany(size_t count) {
    DCHECK_GE(elements_.size(), count);
    std::vector<T> result;
    result.reserve(count);
    for (auto it = elements_.end() - count; it != elements_.end(); ++it) {
      result.push_back(std::move(*it));
    }
    elements_.resize(elements_.size() - count);
    return result;
  }
  // The invalid offset above the top element. This is useful for StackRange.
  BottomOffset AboveTop() const { return BottomOffset{Size()}; }
  // Delete the slots in {range}, moving higher slots to fill the gap.
  void DeleteRange(StackRange range) {
    DCHECK_LE(range.end(), AboveTop());
    if (range.Size() == 0) return;
    for (BottomOffset i = range.end(); i < AboveTop(); ++i) {
      elements_[i.offset - range.Size()] = std::move(elements_[i.offset]);
    }
    elements_.resize(elements_.size() - range.Size());
  }

  bool operator==(const Stack& other) const {
    return elements_ == other.elements_;
  }
  bool operator!=(const Stack& other) const {
    return elements_ != other.elements_;
  }

  T* begin() { return elements_.data(); }
  T* end() { return begin() + elements_.size(); }
  const T* begin() const { return elements_.data(); }
  const T* end() const { return begin() + elements_.size(); }

 private:
  std::vector<T> elements_;
};

template <class T>
T* CheckNotNull(T* x) {
  CHECK_NOT_NULL(x);
  return x;
}

template <class T>
inline std::ostream& operator<<(std::ostream& os, const Stack<T>& t) {
  os << "Stack{";
  PrintCommaSeparatedList(os, t);
  os << "}";
  return os;
}

static const char* const kBaseNamespaceName = "base";
static const char* const kTestNamespaceName = "test";

// Erase elements of a container that has a constant-time erase function, like
// std::set or std::list. Calling this on std::vector would have quadratic
// complexity.
template <class Container, class F>
void EraseIf(Container* container, F f) {
  for (auto it = container->begin(); it != container->end();) {
    if (f(*it)) {
      it = container->erase(it);
    } else {
      ++it;
    }
  }
}

class NullStreambuf : public std::streambuf {
 public:
  int overflow(int c) override {
    setp(buffer_, buffer_ + sizeof(buffer_));
    return (c == traits_type::eof()) ? '\0' : c;
  }

 private:
  char buffer_[64];
};

class NullOStream : public std::ostream {
 public:
  NullOStream() : std::ostream(&buffer_) {}

 private:
  NullStreambuf buffer_;
};

inline bool StringStartsWith(const std::string& s, const std::string& prefix) {
  if (s.size() < prefix.size()) return false;
  return s.substr(0, prefix.size()) == prefix;
}
inline bool StringEndsWith(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  return s.substr(s.size() - suffix.size()) == suffix;
}

class V8_NODISCARD IfDefScope {
 public:
  IfDefScope(std::ostream& os, std::string d);
  ~IfDefScope();
  IfDefScope(const IfDefScope&) = delete;
  IfDefScope& operator=(const IfDefScope&) = delete;

 private:
  std::ostream& os_;
  std::string d_;
};

class V8_NODISCARD NamespaceScope {
 public:
  NamespaceScope(std::ostream& os,
                 std::initializer_list<std::string> namespaces);
  ~NamespaceScope();
  NamespaceScope(const NamespaceScope&) = delete;
  NamespaceScope& operator=(const NamespaceScope&) = delete;

 private:
  std::ostream& os_;
  std::vector<std::string> d_;
};

class V8_NODISCARD IncludeGuardScope {
 public:
  IncludeGuardScope(std::ostream& os, std::string file_name);
  ~IncludeGuardScope();
  IncludeGuardScope(const IncludeGuardScope&) = delete;
  IncludeGuardScope& operator=(const IncludeGuardScope&) = delete;

 private:
  std::ostream& os_;
  std::string d_;
};

class V8_NODISCARD IncludeObjectMacrosScope {
 public:
  explicit IncludeObjectMacrosScope(std::ostream& os);
  ~IncludeObjectMacrosScope();
  IncludeObjectMacrosScope(const IncludeObjectMacrosScope&) = delete;
  IncludeObjectMacrosScope& operator=(const IncludeObjectMacrosScope&) = delete;

 private:
  std::ostream& os_;
};

// A value of ResidueClass is a congruence class of integers modulo a power
// of 2.
// In contrast to common modulo arithmetic, we also allow addition and
// multiplication of congruence classes with different modulus. In this case, we
// do an abstract-interpretation style approximation to produce an as small as
// possible congruence class. ResidueClass is used to represent partial
// knowledge about offsets and sizes to validate alignment constraints.
// ResidueClass(x,m) = {y \in Z | x == y mod 2^m} = {x+k2^m | k \in Z} where Z
// is the set of all integers.
// Notation: 2^x is 2 to the power of x.
class ResidueClass {
 public:
  ResidueClass(size_t value, size_t modulus_log_2 =
                                 kMaxModulusLog2)  // NOLINT(runtime/explicit)
      : value_(value),
        modulus_log_2_(std::min(modulus_log_2, kMaxModulusLog2)) {
    if (modulus_log_2_ < kMaxModulusLog2) {
      value_ %= size_t{1} << modulus_log_2_;
    }
  }

  // 0 modulo 1, in other words, the class of all integers.
  static ResidueClass Unknown() { return ResidueClass{0, 0}; }

  // If the modulus corresponds to the size of size_t, it represents a concrete
  // value.
  std::optional<size_t> SingleValue() const {
    if (modulus_log_2_ == kMaxModulusLog2) return value_;
    return std::nullopt;
  }

  friend ResidueClass operator+(const ResidueClass& a, const ResidueClass& b) {
    return ResidueClass{a.value_ + b.value_,
                        std::min(a.modulus_log_2_, b.modulus_log_2_)};
  }

  // Reasoning for the choice of the new modulus:
  // {x+k2^a | k \in Z} * {y+l2^b | l \in Z}
  // = {xy + xl2^b + yk2^a + kl2^(a+b)| k,l \in Z},
  // which is a subset of {xy + k2^c | k \in Z}
  // if 2^c is a common divisor of x2^b, y2^a and hence also of 2^(a+b) since
  // x<2^a and y<2^b.
  // So we use the gcd of x2^b and y2^a as the new modulus.
  friend ResidueClass operator*(const ResidueClass& a, const ResidueClass& b) {
    return ResidueClass{a.value_ * b.value_,
                        std::min(a.modulus_log_2_ + b.AlignmentLog2(),
                                 b.modulus_log_2_ + a.AlignmentLog2())};
  }

  friend std::ostream& operator<<(std::ostream& os, const ResidueClass& a);

  ResidueClass& operator+=(const ResidueClass& other) {
    *this = *this + other;
    return *this;
  }

  ResidueClass& operator*=(const ResidueClass& other) {
    *this = *this * other;
    return *this;
  }

  // 2^AlignmentLog2() is the larget power of 2 that divides all elements of the
  // congruence class.
  size_t AlignmentLog2() const;
  size_t Alignment() const {
    DCHECK_LT(AlignmentLog2(), kMaxModulusLog2);
    return size_t{1} << AlignmentLog2();
  }

 private:
  // The value is the representative of the congruence class. It's always
  // smaller than 2^modulus_log_2_.
  size_t value_;
  // Base 2 logarithm of the modulus.
  size_t modulus_log_2_;

  // size_t values are modulo 2^kMaxModulusLog2, so we don't consider larger
  // modulus.
  static const size_t kMaxModulusLog2 = 8 * sizeof(size_t);
};

template <typename T>
class Worklist {
 public:
  bool IsEmpty() const {
    DCHECK_EQ(queue_.size(), contained_.size());
    return queue_.empty();
  }

  bool Enqueue(T value) {
    if (contained_.find(value) != contained_.end()) return false;
    queue_.push(value);
    contained_.insert(value);
    DCHECK_EQ(queue_.size(), contained_.size());
    return true;
  }

  T Dequeue() {
    DCHECK(!IsEmpty());
    T value = queue_.front();
    queue_.pop();
    contained_.erase(value);
    DCHECK_EQ(queue_.size(), contained_.size());
    return value;
  }

 private:
  std::queue<T> queue_;
  std::unordered_set<T> contained_;
};

template <class T, class U, class F>
std::vector<T> TransformVector(const std::vector<U>& v, F f) {
  std::vector<T> result;
  std::transform(v.begin(), v.end(), std::back_inserter(result), f);
  return result;
}
template <class T, class U>
std::vector<T> TransformVector(const std::vector<U>& v) {
  return TransformVector<T>(v, [](const U& x) -> T { return x; });
}

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_UTILS_H_
