// Copyright 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The Google C++ Testing and Mocking Framework (Google Test)
//
// This file implements just enough of the matcher interface to allow
// EXPECT_DEATH and friends to accept a matcher argument.

// IWYU pragma: private, include "gtest/gtest.h"
// IWYU pragma: friend gtest/.*
// IWYU pragma: friend gmock/.*

#ifndef GOOGLETEST_INCLUDE_GTEST_GTEST_MATCHERS_H_
#define GOOGLETEST_INCLUDE_GTEST_GTEST_MATCHERS_H_

#include <atomic>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>

#include "gtest/gtest-printers.h"
#include "gtest/internal/gtest-internal.h"
#include "gtest/internal/gtest-port.h"

// MSVC warning C5046 is new as of VS2017 version 15.8.
#if defined(_MSC_VER) && _MSC_VER >= 1915
#define GTEST_MAYBE_5046_ 5046
#else
#define GTEST_MAYBE_5046_
#endif

GTEST_DISABLE_MSC_WARNINGS_PUSH_(
    4251 GTEST_MAYBE_5046_ /* class A needs to have dll-interface to be used by
                              clients of class B */
    /* Symbol involving type with internal linkage not defined */)

namespace testing {

// To implement a matcher Foo for type T, define:
//   1. a class FooMatcherMatcher that implements the matcher interface:
//     using is_gtest_matcher = void;
//     bool MatchAndExplain(const T&, std::ostream*) const;
//       (MatchResultListener* can also be used instead of std::ostream*)
//     void DescribeTo(std::ostream*) const;
//     void DescribeNegationTo(std::ostream*) const;
//
//   2. a factory function that creates a Matcher<T> object from a
//      FooMatcherMatcher.

class MatchResultListener {
 public:
  // Creates a listener object with the given underlying ostream.  The
  // listener does not own the ostream, and does not dereference it
  // in the constructor or destructor.
  explicit MatchResultListener(::std::ostream* os) : stream_(os) {}
  virtual ~MatchResultListener() = 0;  // Makes this class abstract.

  // Streams x to the underlying ostream; does nothing if the ostream
  // is NULL.
  template <typename T>
  MatchResultListener& operator<<(const T& x) {
    if (stream_ != nullptr) *stream_ << x;
    return *this;
  }

  // Returns the underlying ostream.
  ::std::ostream* stream() { return stream_; }

  // Returns true if and only if the listener is interested in an explanation
  // of the match result.  A matcher's MatchAndExplain() method can use
  // this information to avoid generating the explanation when no one
  // intends to hear it.
  bool IsInterested() const { return stream_ != nullptr; }

 private:
  ::std::ostream* const stream_;

  MatchResultListener(const MatchResultListener&) = delete;
  MatchResultListener& operator=(const MatchResultListener&) = delete;
};

inline MatchResultListener::~MatchResultListener() = default;

// An instance of a subclass of this knows how to describe itself as a
// matcher.
class GTEST_API_ MatcherDescriberInterface {
 public:
  virtual ~MatcherDescriberInterface() = default;

  // Describes this matcher to an ostream.  The function should print
  // a verb phrase that describes the property a value matching this
  // matcher should have.  The subject of the verb phrase is the value
  // being matched.  For example, the DescribeTo() method of the Gt(7)
  // matcher prints "is greater than 7".
  virtual void DescribeTo(::std::ostream* os) const = 0;

  // Describes the negation of this matcher to an ostream.  For
  // example, if the description of this matcher is "is greater than
  // 7", the negated description could be "is not greater than 7".
  // You are not required to override this when implementing
  // MatcherInterface, but it is highly advised so that your matcher
  // can produce good error messages.
  virtual void DescribeNegationTo(::std::ostream* os) const {
    *os << "not (";
    DescribeTo(os);
    *os << ")";
  }
};

// The implementation of a matcher.
template <typename T>
class MatcherInterface : public MatcherDescriberInterface {
 public:
  // Returns true if and only if the matcher matches x; also explains the
  // match result to 'listener' if necessary (see the next paragraph), in
  // the form of a non-restrictive relative clause ("which ...",
  // "whose ...", etc) that describes x.  For example, the
  // MatchAndExplain() method of the Pointee(...) matcher should
  // generate an explanation like "which points to ...".
  //
  // Implementations of MatchAndExplain() should add an explanation of
  // the match result *if and only if* they can provide additional
  // information that's not already present (or not obvious) in the
  // print-out of x and the matcher's description.  Whether the match
  // succeeds is not a factor in deciding whether an explanation is
  // needed, as sometimes the caller needs to print a failure message
  // when the match succeeds (e.g. when the matcher is used inside
  // Not()).
  //
  // For example, a "has at least 10 elements" matcher should explain
  // what the actual element count is, regardless of the match result,
  // as it is useful information to the reader; on the other hand, an
  // "is empty" matcher probably only needs to explain what the actual
  // size is when the match fails, as it's redundant to say that the
  // size is 0 when the value is already known to be empty.
  //
  // You should override this method when defining a new matcher.
  //
  // It's the responsibility of the caller (Google Test) to guarantee
  // that 'listener' is not NULL.  This helps to simplify a matcher's
  // implementation when it doesn't care about the performance, as it
  // can talk to 'listener' without checking its validity first.
  // However, in order to implement dummy listeners efficiently,
  // listener->stream() may be NULL.
  virtual bool MatchAndExplain(T x, MatchResultListener* listener) const = 0;

  // Inherits these methods from MatcherDescriberInterface:
  //   virtual void DescribeTo(::std::ostream* os) const = 0;
  //   virtual void DescribeNegationTo(::std::ostream* os) const;
};

namespace internal {

// A match result listener that ignores the explanation.
class DummyMatchResultListener : public MatchResultListener {
 public:
  DummyMatchResultListener() : MatchResultListener(nullptr) {}

 private:
  DummyMatchResultListener(const DummyMatchResultListener&) = delete;
  DummyMatchResultListener& operator=(const DummyMatchResultListener&) = delete;
};

// A match result listener that forwards the explanation to a given
// ostream.  The difference between this and MatchResultListener is
// that the former is concrete.
class StreamMatchResultListener : public MatchResultListener {
 public:
  explicit StreamMatchResultListener(::std::ostream* os)
      : MatchResultListener(os) {}

 private:
  StreamMatchResultListener(const StreamMatchResultListener&) = delete;
  StreamMatchResultListener& operator=(const StreamMatchResultListener&) =
      delete;
};

struct SharedPayloadBase {
  std::atomic<int> ref{1};
  void Ref() { ref.fetch_add(1, std::memory_order_relaxed); }
  bool Unref() { return ref.fetch_sub(1, std::memory_order_acq_rel) == 1; }
};

template <typename T>
struct SharedPayload : SharedPayloadBase {
  explicit SharedPayload(const T& v) : value(v) {}
  explicit SharedPayload(T&& v) : value(std::move(v)) {}

  static void Destroy(SharedPayloadBase* shared) {
    delete static_cast<SharedPayload*>(shared);
  }

  T value;
};

// An internal class for implementing Matcher<T>, which will derive
// from it.  We put functionalities common to all Matcher<T>
// specializations here to avoid code duplication.
template <typename T>
class MatcherBase : private MatcherDescriberInterface {
 public:
  // Returns true if and only if the matcher matches x; also explains the
  // match result to 'listener'.
  bool MatchAndExplain(const T& x, MatchResultListener* listener) const {
    GTEST_CHECK_(vtable_ != nullptr);
    return vtable_->match_and_explain(*this, x, listener);
  }

  // Returns true if and only if this matcher matches x.
  bool Matches(const T& x) const {
    DummyMatchResultListener dummy;
    return MatchAndExplain(x, &dummy);
  }

  // Describes this matcher to an ostream.
  void DescribeTo(::std::ostream* os) const final {
    GTEST_CHECK_(vtable_ != nullptr);
    vtable_->describe(*this, os, false);
  }

  // Describes the negation of this matcher to an ostream.
  void DescribeNegationTo(::std::ostream* os) const final {
    GTEST_CHECK_(vtable_ != nullptr);
    vtable_->describe(*this, os, true);
  }

  // Explains why x matches, or doesn't match, the matcher.
  void ExplainMatchResultTo(const T& x, ::std::ostream* os) const {
    StreamMatchResultListener listener(os);
    MatchAndExplain(x, &listener);
  }

  // Returns the describer for this matcher object; retains ownership
  // of the describer, which is only guaranteed to be alive when
  // this matcher object is alive.
  const MatcherDescriberInterface* GetDescriber() const {
    if (vtable_ == nullptr) return nullptr;
    return vtable_->get_describer(*this);
  }

 protected:
  MatcherBase() : vtable_(nullptr), buffer_() {}

  // Constructs a matcher from its implementation.
  template <typename U>
  explicit MatcherBase(const MatcherInterface<U>* impl)
      : vtable_(nullptr), buffer_() {
    Init(impl);
  }

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  MatcherBase(M&& m) : vtable_(nullptr), buffer_() {  // NOLINT
    Init(std::forward<M>(m));
  }

  MatcherBase(const MatcherBase& other)
      : vtable_(other.vtable_), buffer_(other.buffer_) {
    if (IsShared()) buffer_.shared->Ref();
  }

  MatcherBase& operator=(const MatcherBase& other) {
    if (this == &other) return *this;
    Destroy();
    vtable_ = other.vtable_;
    buffer_ = other.buffer_;
    if (IsShared()) buffer_.shared->Ref();
    return *this;
  }

  MatcherBase(MatcherBase&& other)
      : vtable_(other.vtable_), buffer_(other.buffer_) {
    other.vtable_ = nullptr;
  }

  MatcherBase& operator=(MatcherBase&& other) {
    if (this == &other) return *this;
    Destroy();
    vtable_ = other.vtable_;
    buffer_ = other.buffer_;
    other.vtable_ = nullptr;
    return *this;
  }

  ~MatcherBase() override { Destroy(); }

 private:
  struct VTable {
    bool (*match_and_explain)(const MatcherBase&, const T&,
                              MatchResultListener*);
    void (*describe)(const MatcherBase&, std::ostream*, bool negation);
    // Returns the captured object if it implements the interface, otherwise
    // returns the MatcherBase itself.
    const MatcherDescriberInterface* (*get_describer)(const MatcherBase&);
    // Called on shared instances when the reference count reaches 0.
    void (*shared_destroy)(SharedPayloadBase*);
  };

  bool IsShared() const {
    return vtable_ != nullptr && vtable_->shared_destroy != nullptr;
  }

  // If the implementation uses a listener, call that.
  template <typename P>
  static auto MatchAndExplainImpl(const MatcherBase& m, const T& value,
                                  MatchResultListener* listener)
      -> decltype(P::Get(m).MatchAndExplain(value, listener->stream())) {
    return P::Get(m).MatchAndExplain(value, listener->stream());
  }

  template <typename P>
  static auto MatchAndExplainImpl(const MatcherBase& m, const T& value,
                                  MatchResultListener* listener)
      -> decltype(P::Get(m).MatchAndExplain(value, listener)) {
    return P::Get(m).MatchAndExplain(value, listener);
  }

  template <typename P>
  static void DescribeImpl(const MatcherBase& m, std::ostream* os,
                           bool negation) {
    if (negation) {
      P::Get(m).DescribeNegationTo(os);
    } else {
      P::Get(m).DescribeTo(os);
    }
  }

  template <typename P>
  static const MatcherDescriberInterface* GetDescriberImpl(
      const MatcherBase& m) {
    // If the impl is a MatcherDescriberInterface, then return it.
    // Otherwise use MatcherBase itself.
    // This allows us to implement the GetDescriber() function without support
    // from the impl, but some users really want to get their impl back when
    // they call GetDescriber().
    // We use std::get on a tuple as a workaround of not having `if constexpr`.
    return std::get<(
        std::is_convertible<decltype(&P::Get(m)),
                            const MatcherDescriberInterface*>::value
            ? 1
            : 0)>(std::make_tuple(&m, &P::Get(m)));
  }

  template <typename P>
  const VTable* GetVTable() {
    static constexpr VTable kVTable = {&MatchAndExplainImpl<P>,
                                       &DescribeImpl<P>, &GetDescriberImpl<P>,
                                       P::shared_destroy};
    return &kVTable;
  }

  union Buffer {
    // Add some types to give Buffer some common alignment/size use cases.
    void* ptr;
    double d;
    int64_t i;
    // And add one for the out-of-line cases.
    SharedPayloadBase* shared;
  };

  void Destroy() {
    if (IsShared() && buffer_.shared->Unref()) {
      vtable_->shared_destroy(buffer_.shared);
    }
  }

  template <typename M>
  static constexpr bool IsInlined() {
    return sizeof(M) <= sizeof(Buffer) && alignof(M) <= alignof(Buffer) &&
           std::is_trivially_copy_constructible<M>::value &&
           std::is_trivially_destructible<M>::value;
  }

  template <typename M, bool = MatcherBase::IsInlined<M>()>
  struct ValuePolicy {
    static const M& Get(const MatcherBase& m) {
      // When inlined along with Init, need to be explicit to avoid violating
      // strict aliasing rules.
      const M* ptr =
          static_cast<const M*>(static_cast<const void*>(&m.buffer_));
      return *ptr;
    }
    static void Init(MatcherBase& m, M impl) {
      ::new (static_cast<void*>(&m.buffer_)) M(impl);
    }
    static constexpr auto shared_destroy = nullptr;
  };

  template <typename M>
  struct ValuePolicy<M, false> {
    using Shared = SharedPayload<M>;
    static const M& Get(const MatcherBase& m) {
      return static_cast<Shared*>(m.buffer_.shared)->value;
    }
    template <typename Arg>
    static void Init(MatcherBase& m, Arg&& arg) {
      m.buffer_.shared = new Shared(std::forward<Arg>(arg));
    }
    static constexpr auto shared_destroy = &Shared::Destroy;
  };

  template <typename U, bool B>
  struct ValuePolicy<const MatcherInterface<U>*, B> {
    using M = const MatcherInterface<U>;
    using Shared = SharedPayload<std::unique_ptr<M>>;
    static const M& Get(const MatcherBase& m) {
      return *static_cast<Shared*>(m.buffer_.shared)->value;
    }
    static void Init(MatcherBase& m, M* impl) {
      m.buffer_.shared = new Shared(std::unique_ptr<M>(impl));
    }

    static constexpr auto shared_destroy = &Shared::Destroy;
  };

  template <typename M>
  void Init(M&& m) {
    using MM = typename std::decay<M>::type;
    using Policy = ValuePolicy<MM>;
    vtable_ = GetVTable<Policy>();
    Policy::Init(*this, std::forward<M>(m));
  }

  const VTable* vtable_;
  Buffer buffer_;
};

}  // namespace internal

// A Matcher<T> is a copyable and IMMUTABLE (except by assignment)
// object that can check whether a value of type T matches.  The
// implementation of Matcher<T> is just a std::shared_ptr to const
// MatcherInterface<T>.  Don't inherit from Matcher!
template <typename T>
class Matcher : public internal::MatcherBase<T> {
 public:
  // Constructs a null matcher.  Needed for storing Matcher objects in STL
  // containers.  A default-constructed matcher is not yet initialized.  You
  // cannot use it until a valid value has been assigned to it.
  explicit Matcher() {}  // NOLINT

  // Constructs a matcher from its implementation.
  explicit Matcher(const MatcherInterface<const T&>* impl)
      : internal::MatcherBase<T>(impl) {}

  template <typename U>
  explicit Matcher(
      const MatcherInterface<U>* impl,
      typename std::enable_if<!std::is_same<U, const U&>::value>::type* =
          nullptr)
      : internal::MatcherBase<T>(impl) {}

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  Matcher(M&& m) : internal::MatcherBase<T>(std::forward<M>(m)) {}  // NOLINT

  // Implicit constructor here allows people to write
  // EXPECT_CALL(foo, Bar(5)) instead of EXPECT_CALL(foo, Bar(Eq(5))) sometimes
  Matcher(T value);  // NOLINT
};

// The following two specializations allow the user to write str
// instead of Eq(str) and "foo" instead of Eq("foo") when a std::string
// matcher is expected.
template <>
class GTEST_API_ Matcher<const std::string&>
    : public internal::MatcherBase<const std::string&> {
 public:
  Matcher() = default;

  explicit Matcher(const MatcherInterface<const std::string&>* impl)
      : internal::MatcherBase<const std::string&>(impl) {}

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  Matcher(M&& m)  // NOLINT
      : internal::MatcherBase<const std::string&>(std::forward<M>(m)) {}

  // Allows the user to write str instead of Eq(str) sometimes, where
  // str is a std::string object.
  Matcher(const std::string& s);  // NOLINT

  // Allows the user to write "foo" instead of Eq("foo") sometimes.
  Matcher(const char* s);  // NOLINT
};

template <>
class GTEST_API_ Matcher<std::string>
    : public internal::MatcherBase<std::string> {
 public:
  Matcher() = default;

  explicit Matcher(const MatcherInterface<const std::string&>* impl)
      : internal::MatcherBase<std::string>(impl) {}
  explicit Matcher(const MatcherInterface<std::string>* impl)
      : internal::MatcherBase<std::string>(impl) {}

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  Matcher(M&& m)  // NOLINT
      : internal::MatcherBase<std::string>(std::forward<M>(m)) {}

  // Allows the user to write str instead of Eq(str) sometimes, where
  // str is a string object.
  Matcher(const std::string& s);  // NOLINT

  // Allows the user to write "foo" instead of Eq("foo") sometimes.
  Matcher(const char* s);  // NOLINT
};

#if GTEST_INTERNAL_HAS_STRING_VIEW
// The following two specializations allow the user to write str
// instead of Eq(str) and "foo" instead of Eq("foo") when a absl::string_view
// matcher is expected.
template <>
class GTEST_API_ Matcher<const internal::StringView&>
    : public internal::MatcherBase<const internal::StringView&> {
 public:
  Matcher() = default;

  explicit Matcher(const MatcherInterface<const internal::StringView&>* impl)
      : internal::MatcherBase<const internal::StringView&>(impl) {}

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  Matcher(M&& m)  // NOLINT
      : internal::MatcherBase<const internal::StringView&>(std::forward<M>(m)) {
  }

  // Allows the user to write str instead of Eq(str) sometimes, where
  // str is a std::string object.
  Matcher(const std::string& s);  // NOLINT

  // Allows the user to write "foo" instead of Eq("foo") sometimes.
  Matcher(const char* s);  // NOLINT

  // Allows the user to pass absl::string_views or std::string_views directly.
  Matcher(internal::StringView s);  // NOLINT
};

template <>
class GTEST_API_ Matcher<internal::StringView>
    : public internal::MatcherBase<internal::StringView> {
 public:
  Matcher() = default;

  explicit Matcher(const MatcherInterface<const internal::StringView&>* impl)
      : internal::MatcherBase<internal::StringView>(impl) {}
  explicit Matcher(const MatcherInterface<internal::StringView>* impl)
      : internal::MatcherBase<internal::StringView>(impl) {}

  template <typename M, typename = typename std::remove_reference<
                            M>::type::is_gtest_matcher>
  Matcher(M&& m)  // NOLINT
      : internal::MatcherBase<internal::StringView>(std::forward<M>(m)) {}

  // Allows the user to write str instead of Eq(str) sometimes, where
  // str is a std::string object.
  Matcher(const std::string& s);  // NOLINT

  // Allows the user to write "foo" instead of Eq("foo") sometimes.
  Matcher(const char* s);  // NOLINT

  // Allows the user to pass absl::string_views or std::string_views directly.
  Matcher(internal::StringView s);  // NOLINT
};
#endif  // GTEST_INTERNAL_HAS_STRING_VIEW

// Prints a matcher in a human-readable format.
template <typename T>
std::ostream& operator<<(std::ostream& os, const Matcher<T>& matcher) {
  matcher.DescribeTo(&os);
  return os;
}

// The PolymorphicMatcher class template makes it easy to implement a
// polymorphic matcher (i.e. a matcher that can match values of more
// than one type, e.g. Eq(n) and NotNull()).
//
// To define a polymorphic matcher, a user should provide an Impl
// class that has a DescribeTo() method and a DescribeNegationTo()
// method, and define a member function (or member function template)
//
//   bool MatchAndExplain(const Value& value,
//                        MatchResultListener* listener) const;
//
// See the definition of NotNull() for a complete example.
template <class Impl>
class PolymorphicMatcher {
 public:
  explicit PolymorphicMatcher(const Impl& an_impl) : impl_(an_impl) {}

  // Returns a mutable reference to the underlying matcher
  // implementation object.
  Impl& mutable_impl() { return impl_; }

  // Returns an immutable reference to the underlying matcher
  // implementation object.
  const Impl& impl() const { return impl_; }

  template <typename T>
  operator Matcher<T>() const {
    return Matcher<T>(new MonomorphicImpl<const T&>(impl_));
  }

 private:
  template <typename T>
  class MonomorphicImpl : public MatcherInterface<T> {
   public:
    explicit MonomorphicImpl(const Impl& impl) : impl_(impl) {}

    void DescribeTo(::std::ostream* os) const override { impl_.DescribeTo(os); }

    void DescribeNegationTo(::std::ostream* os) const override {
      impl_.DescribeNegationTo(os);
    }

    bool MatchAndExplain(T x, MatchResultListener* listener) const override {
      return impl_.MatchAndExplain(x, listener);
    }

   private:
    const Impl impl_;
  };

  Impl impl_;
};

// Creates a matcher from its implementation.
// DEPRECATED: Especially in the generic code, prefer:
//   Matcher<T>(new MyMatcherImpl<const T&>(...));
//
// MakeMatcher may create a Matcher that accepts its argument by value, which
// leads to unnecessary copies & lack of support for non-copyable types.
template <typename T>
inline Matcher<T> MakeMatcher(const MatcherInterface<T>* impl) {
  return Matcher<T>(impl);
}

// Creates a polymorphic matcher from its implementation.  This is
// easier to use than the PolymorphicMatcher<Impl> constructor as it
// doesn't require you to explicitly write the template argument, e.g.
//
//   MakePolymorphicMatcher(foo);
// vs
//   PolymorphicMatcher<TypeOfFoo>(foo);
template <class Impl>
inline PolymorphicMatcher<Impl> MakePolymorphicMatcher(const Impl& impl) {
  return PolymorphicMatcher<Impl>(impl);
}

namespace internal {
// Implements a matcher that compares a given value with a
// pre-supplied value using one of the ==, <=, <, etc, operators.  The
// two values being compared don't have to have the same type.
//
// The matcher defined here is polymorphic (for example, Eq(5) can be
// used to match an int, a short, a double, etc).  Therefore we use
// a template type conversion operator in the implementation.
//
// The following template definition assumes that the Rhs parameter is
// a "bare" type (i.e. neither 'const T' nor 'T&').
template <typename D, typename Rhs, typename Op>
class ComparisonBase {
 public:
  explicit ComparisonBase(const Rhs& rhs) : rhs_(rhs) {}

  using is_gtest_matcher = void;

  template <typename Lhs>
  bool MatchAndExplain(const Lhs& lhs, std::ostream*) const {
    return Op()(lhs, Unwrap(rhs_));
  }
  void DescribeTo(std::ostream* os) const {
    *os << D::Desc() << " ";
    UniversalPrint(Unwrap(rhs_), os);
  }
  void DescribeNegationTo(std::ostream* os) const {
    *os << D::NegatedDesc() << " ";
    UniversalPrint(Unwrap(rhs_), os);
  }

 private:
  template <typename T>
  static const T& Unwrap(const T& v) {
    return v;
  }
  template <typename T>
  static const T& Unwrap(std::reference_wrapper<T> v) {
    return v;
  }

  Rhs rhs_;
};

template <typename Rhs>
class EqMatcher : public ComparisonBase<EqMatcher<Rhs>, Rhs, std::equal_to<>> {
 public:
  explicit EqMatcher(const Rhs& rhs)
      : ComparisonBase<EqMatcher<Rhs>, Rhs, std::equal_to<>>(rhs) {}
  static const char* Desc() { return "is equal to"; }
  static const char* NegatedDesc() { return "isn't equal to"; }
};
template <typename Rhs>
class NeMatcher
    : public ComparisonBase<NeMatcher<Rhs>, Rhs, std::not_equal_to<>> {
 public:
  explicit NeMatcher(const Rhs& rhs)
      : ComparisonBase<NeMatcher<Rhs>, Rhs, std::not_equal_to<>>(rhs) {}
  static const char* Desc() { return "isn't equal to"; }
  static const char* NegatedDesc() { return "is equal to"; }
};
template <typename Rhs>
class LtMatcher : public ComparisonBase<LtMatcher<Rhs>, Rhs, std::less<>> {
 public:
  explicit LtMatcher(const Rhs& rhs)
      : ComparisonBase<LtMatcher<Rhs>, Rhs, std::less<>>(rhs) {}
  static const char* Desc() { return "is <"; }
  static const char* NegatedDesc() { return "isn't <"; }
};
template <typename Rhs>
class GtMatcher : public ComparisonBase<GtMatcher<Rhs>, Rhs, std::greater<>> {
 public:
  explicit GtMatcher(const Rhs& rhs)
      : ComparisonBase<GtMatcher<Rhs>, Rhs, std::greater<>>(rhs) {}
  static const char* Desc() { return "is >"; }
  static const char* NegatedDesc() { return "isn't >"; }
};
template <typename Rhs>
class LeMatcher
    : public ComparisonBase<LeMatcher<Rhs>, Rhs, std::less_equal<>> {
 public:
  explicit LeMatcher(const Rhs& rhs)
      : ComparisonBase<LeMatcher<Rhs>, Rhs, std::less_equal<>>(rhs) {}
  static const char* Desc() { return "is <="; }
  static const char* NegatedDesc() { return "isn't <="; }
};
template <typename Rhs>
class GeMatcher
    : public ComparisonBase<GeMatcher<Rhs>, Rhs, std::greater_equal<>> {
 public:
  explicit GeMatcher(const Rhs& rhs)
      : ComparisonBase<GeMatcher<Rhs>, Rhs, std::greater_equal<>>(rhs) {}
  static const char* Desc() { return "is >="; }
  static const char* NegatedDesc() { return "isn't >="; }
};

template <typename T, typename = typename std::enable_if<
                          std::is_constructible<std::string, T>::value>::type>
using StringLike = T;

// Implements polymorphic matchers MatchesRegex(regex) and
// ContainsRegex(regex), which can be used as a Matcher<T> as long as
// T can be converted to a string.
class MatchesRegexMatcher {
 public:
  MatchesRegexMatcher(const RE* regex, bool full_match)
      : regex_(regex), full_match_(full_match) {}

#if GTEST_INTERNAL_HAS_STRING_VIEW
  bool MatchAndExplain(const internal::StringView& s,
                       MatchResultListener* listener) const {
    return MatchAndExplain(std::string(s), listener);
  }
#endif  // GTEST_INTERNAL_HAS_STRING_VIEW

  // Accepts pointer types, particularly:
  //   const char*
  //   char*
  //   const wchar_t*
  //   wchar_t*
  template <typename CharType>
  bool MatchAndExplain(CharType* s, MatchResultListener* listener) const {
    return s != nullptr && MatchAndExplain(std::string(s), listener);
  }

  // Matches anything that can convert to std::string.
  //
  // This is a template, not just a plain function with const std::string&,
  // because absl::string_view has some interfering non-explicit constructors.
  template <class MatcheeStringType>
  bool MatchAndExplain(const MatcheeStringType& s,
                       MatchResultListener* /* listener */) const {
    const std::string s2(s);
    return full_match_ ? RE::FullMatch(s2, *regex_)
                       : RE::PartialMatch(s2, *regex_);
  }

  void DescribeTo(::std::ostream* os) const {
    *os << (full_match_ ? "matches" : "contains") << " regular expression ";
    UniversalPrinter<std::string>::Print(regex_->pattern(), os);
  }

  void DescribeNegationTo(::std::ostream* os) const {
    *os << "doesn't " << (full_match_ ? "match" : "contain")
        << " regular expression ";
    UniversalPrinter<std::string>::Print(regex_->pattern(), os);
  }

 private:
  const std::shared_ptr<const RE> regex_;
  const bool full_match_;
};
}  // namespace internal

// Matches a string that fully matches regular expression 'regex'.
// The matcher takes ownership of 'regex'.
inline PolymorphicMatcher<internal::MatchesRegexMatcher> MatchesRegex(
    const internal::RE* regex) {
  return MakePolymorphicMatcher(internal::MatchesRegexMatcher(regex, true));
}
template <typename T = std::string>
PolymorphicMatcher<internal::MatchesRegexMatcher> MatchesRegex(
    const internal::StringLike<T>& regex) {
  return MatchesRegex(new internal::RE(std::string(regex)));
}

// Matches a string that contains regular expression 'regex'.
// The matcher takes ownership of 'regex'.
inline PolymorphicMatcher<internal::MatchesRegexMatcher> ContainsRegex(
    const internal::RE* regex) {
  return MakePolymorphicMatcher(internal::MatchesRegexMatcher(regex, false));
}
template <typename T = std::string>
PolymorphicMatcher<internal::MatchesRegexMatcher> ContainsRegex(
    const internal::StringLike<T>& regex) {
  return ContainsRegex(new internal::RE(std::string(regex)));
}

// Creates a polymorphic matcher that matches anything equal to x.
// Note: if the parameter of Eq() were declared as const T&, Eq("foo")
// wouldn't compile.
template <typename T>
inline internal::EqMatcher<T> Eq(T x) {
  return internal::EqMatcher<T>(x);
}

// Constructs a Matcher<T> from a 'value' of type T.  The constructed
// matcher matches any value that's equal to 'value'.
template <typename T>
Matcher<T>::Matcher(T value) {
  *this = Eq(value);
}

// Creates a monomorphic matcher that matches anything with type Lhs
// and equal to rhs.  A user may need to use this instead of Eq(...)
// in order to resolve an overloading ambiguity.
//
// TypedEq<T>(x) is just a convenient short-hand for Matcher<T>(Eq(x))
// or Matcher<T>(x), but more readable than the latter.
//
// We could define similar monomorphic matchers for other comparison
// operations (e.g. TypedLt, TypedGe, and etc), but decided not to do
// it yet as those are used much less than Eq() in practice.  A user
// can always write Matcher<T>(Lt(5)) to be explicit about the type,
// for example.
template <typename Lhs, typename Rhs>
inline Matcher<Lhs> TypedEq(const Rhs& rhs) {
  return Eq(rhs);
}

// Creates a polymorphic matcher that matches anything >= x.
template <typename Rhs>
inline internal::GeMatcher<Rhs> Ge(Rhs x) {
  return internal::GeMatcher<Rhs>(x);
}

// Creates a polymorphic matcher that matches anything > x.
template <typename Rhs>
inline internal::GtMatcher<Rhs> Gt(Rhs x) {
  return internal::GtMatcher<Rhs>(x);
}

// Creates a polymorphic matcher that matches anything <= x.
template <typename Rhs>
inline internal::LeMatcher<Rhs> Le(Rhs x) {
  return internal::LeMatcher<Rhs>(x);
}

// Creates a polymorphic matcher that matches anything < x.
template <typename Rhs>
inline internal::LtMatcher<Rhs> Lt(Rhs x) {
  return internal::LtMatcher<Rhs>(x);
}

// Creates a polymorphic matcher that matches anything != x.
template <typename Rhs>
inline internal::NeMatcher<Rhs> Ne(Rhs x) {
  return internal::NeMatcher<Rhs>(x);
}
}  // namespace testing

GTEST_DISABLE_MSC_WARNINGS_POP_()  //  4251 5046

#endif  // GOOGLETEST_INCLUDE_GTEST_GTEST_MATCHERS_H_
