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
// File: bind_front.h
// -----------------------------------------------------------------------------
//
// `absl::bind_front()` returns a functor by binding a number of arguments to
// the front of a provided (usually more generic) functor. Unlike `std::bind`,
// it does not require the use of argument placeholders. The simpler syntax of
// `absl::bind_front()` allows you to avoid known misuses with `std::bind()`.
//
// `absl::bind_front()` is meant as a drop-in replacement for C++20's upcoming
// `std::bind_front()`, which similarly resolves these issues with
// `std::bind()`. Both `bind_front()` alternatives, unlike `std::bind()`, allow
// partial function application. (See
// https://en.wikipedia.org/wiki/Partial_application).

#ifndef ABSL_FUNCTIONAL_BIND_FRONT_H_
#define ABSL_FUNCTIONAL_BIND_FRONT_H_

#if defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 201907L
#include <functional>  // For std::bind_front.
#endif  // defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 201907L

#include <utility>

#include "absl/functional/internal/front_binder.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// bind_front()
//
// Binds the first N arguments of an invocable object and stores them by value.
//
// Like `std::bind()`, `absl::bind_front()` is implicitly convertible to
// `std::function`.  In particular, it may be used as a simpler replacement for
// `std::bind()` in most cases, as it does not require placeholders to be
// specified. More importantly, it provides more reliable correctness guarantees
// than `std::bind()`; while `std::bind()` will silently ignore passing more
// parameters than expected, for example, `absl::bind_front()` will report such
// mis-uses as errors. In C++20, `absl::bind_front` is replaced by
// `std::bind_front`.
//
// absl::bind_front(a...) can be seen as storing the results of
// std::make_tuple(a...).
//
// Example: Binding a free function.
//
//   int Minus(int a, int b) { return a - b; }
//
//   assert(absl::bind_front(Minus)(3, 2) == 3 - 2);
//   assert(absl::bind_front(Minus, 3)(2) == 3 - 2);
//   assert(absl::bind_front(Minus, 3, 2)() == 3 - 2);
//
// Example: Binding a member function.
//
//   struct Math {
//     int Double(int a) const { return 2 * a; }
//   };
//
//   Math math;
//
//   assert(absl::bind_front(&Math::Double)(&math, 3) == 2 * 3);
//   // Stores a pointer to math inside the functor.
//   assert(absl::bind_front(&Math::Double, &math)(3) == 2 * 3);
//   // Stores a copy of math inside the functor.
//   assert(absl::bind_front(&Math::Double, math)(3) == 2 * 3);
//   // Stores std::unique_ptr<Math> inside the functor.
//   assert(absl::bind_front(&Math::Double,
//                           std::unique_ptr<Math>(new Math))(3) == 2 * 3);
//
// Example: Using `absl::bind_front()`, instead of `std::bind()`, with
//          `std::function`.
//
//   class FileReader {
//    public:
//     void ReadFileAsync(const std::string& filename, std::string* content,
//                        const std::function<void()>& done) {
//       // Calls Executor::Schedule(std::function<void()>).
//       Executor::DefaultExecutor()->Schedule(
//           absl::bind_front(&FileReader::BlockingRead, this,
//                            filename, content, done));
//     }
//
//    private:
//     void BlockingRead(const std::string& filename, std::string* content,
//                       const std::function<void()>& done) {
//       CHECK_OK(file::GetContents(filename, content, {}));
//       done();
//     }
//   };
//
// `absl::bind_front()` stores bound arguments explicitly using the type passed
// rather than implicitly based on the type accepted by its functor.
//
// Example: Binding arguments explicitly.
//
//   void LogStringView(absl::string_view sv) {
//     LOG(INFO) << sv;
//   }
//
//   Executor* e = Executor::DefaultExecutor();
//   std::string s = "hello";
//   absl::string_view sv = s;
//
//   // absl::bind_front(LogStringView, arg) makes a copy of arg and stores it.
//   e->Schedule(absl::bind_front(LogStringView, sv)); // ERROR: dangling
//                                                     // string_view.
//
//   e->Schedule(absl::bind_front(LogStringView, s));  // OK: stores a copy of
//                                                     // s.
//
// To store some of the arguments passed to `absl::bind_front()` by reference,
//  use std::ref()` and `std::cref()`.
//
// Example: Storing some of the bound arguments by reference.
//
//   class Service {
//    public:
//     void Serve(const Request& req, std::function<void()>* done) {
//       // The request protocol buffer won't be deleted until done is called.
//       // It's safe to store a reference to it inside the functor.
//       Executor::DefaultExecutor()->Schedule(
//           absl::bind_front(&Service::BlockingServe, this, std::cref(req),
//           done));
//     }
//
//    private:
//     void BlockingServe(const Request& req, std::function<void()>* done);
//   };
//
// Example: Storing bound arguments by reference.
//
//   void Print(const std::string& a, const std::string& b) {
//     std::cerr << a << b;
//   }
//
//   std::string hi = "Hello, ";
//   std::vector<std::string> names = {"Chuk", "Gek"};
//   // Doesn't copy hi.
//   for_each(names.begin(), names.end(),
//            absl::bind_front(Print, std::ref(hi)));
//
//   // DO NOT DO THIS: the functor may outlive "hi", resulting in
//   // dangling references.
//   foo->DoInFuture(absl::bind_front(Print, std::ref(hi), "Guest"));  // BAD!
//   auto f = absl::bind_front(Print, std::ref(hi), "Guest"); // BAD!
//
// Example: Storing reference-like types.
//
//   void Print(absl::string_view a, const std::string& b) {
//     std::cerr << a << b;
//   }
//
//   std::string hi = "Hello, ";
//   // Copies "hi".
//   absl::bind_front(Print, hi)("Chuk");
//
//   // Compile error: std::reference_wrapper<const string> is not implicitly
//   // convertible to string_view.
//   // absl::bind_front(Print, std::cref(hi))("Chuk");
//
//   // Doesn't copy "hi".
//   absl::bind_front(Print, absl::string_view(hi))("Chuk");
//
#if defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 201907L
using std::bind_front;
#else   // defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 201907L
template <class F, class... BoundArgs>
constexpr functional_internal::bind_front_t<F, BoundArgs...> bind_front(
    F&& func, BoundArgs&&... args) {
  return functional_internal::bind_front_t<F, BoundArgs...>(
      absl::in_place, std::forward<F>(func), std::forward<BoundArgs>(args)...);
}
#endif  // defined(__cpp_lib_bind_front) && __cpp_lib_bind_front >= 201907L

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FUNCTIONAL_BIND_FRONT_H_
