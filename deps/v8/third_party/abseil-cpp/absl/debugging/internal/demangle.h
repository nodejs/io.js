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

#ifndef ABSL_DEBUGGING_INTERNAL_DEMANGLE_H_
#define ABSL_DEBUGGING_INTERNAL_DEMANGLE_H_

#include <string>
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// Demangle `mangled`.  On success, return true and write the
// demangled symbol name to `out`.  Otherwise, return false.
// `out` is modified even if demangling is unsuccessful.
//
// This function provides an alternative to libstdc++'s abi::__cxa_demangle,
// which is not async signal safe (it uses malloc internally).  It's intended to
// be used in async signal handlers to symbolize stack traces.
//
// Note that this demangler doesn't support full demangling.  More
// specifically, it doesn't print types of function parameters and
// types of template arguments.  It just skips them.  However, it's
// still very useful to extract basic information such as class,
// function, constructor, destructor, and operator names.
//
// See the implementation note in demangle.cc if you are interested.
//
// Example:
//
// | Mangled Name  | Demangle    | DemangleString
// |---------------|-------------|-----------------------
// | _Z1fv         | f()         | f()
// | _Z1fi         | f()         | f(int)
// | _Z3foo3bar    | foo()       | foo(bar)
// | _Z1fIiEvi     | f<>()       | void f<int>(int)
// | _ZN1N1fE      | N::f        | N::f
// | _ZN3Foo3BarEv | Foo::Bar()  | Foo::Bar()
// | _Zrm1XS_"     | operator%() | operator%(X, X)
// | _ZN3FooC1Ev   | Foo::Foo()  | Foo::Foo()
// | _Z1fSs        | f()         | f(std::basic_string<char,
// |               |             |   std::char_traits<char>,
// |               |             |   std::allocator<char> >)
//
// See the unit test for more examples.
//
// Demangle also recognizes Rust mangled names by delegating the parsing of
// anything that starts with _R to DemangleRustSymbolEncoding (demangle_rust.h).
//
// Note: we might want to write demanglers for ABIs other than Itanium
// C++ ABI in the future.
bool Demangle(const char* mangled, char* out, size_t out_size);

// A wrapper around `abi::__cxa_demangle()`.  On success, returns the demangled
// name.  On failure, returns the input mangled name.
//
// This function is not async-signal-safe.
std::string DemangleString(const char* mangled);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_DEMANGLE_H_
