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
// -----------------------------------------------------------------------------
// kConstInit
// -----------------------------------------------------------------------------
//
// A constructor tag used to mark an object as safe for use as a global
// variable, avoiding the usual lifetime issues that can affect globals.

#ifndef ABSL_BASE_CONST_INIT_H_
#define ABSL_BASE_CONST_INIT_H_

#include "absl/base/config.h"

// In general, objects with static storage duration (such as global variables)
// can trigger tricky object lifetime situations.  Attempting to access them
// from the constructors or destructors of other global objects can result in
// undefined behavior, unless their constructors and destructors are designed
// with this issue in mind.
//
// The normal way to deal with this issue in C++11 is to use constant
// initialization and trivial destructors.
//
// Constant initialization is guaranteed to occur before any other code
// executes.  Constructors that are declared 'constexpr' are eligible for
// constant initialization.  You can annotate a variable declaration with the
// ABSL_CONST_INIT macro to express this intent.  For compilers that support
// it, this annotation will cause a compilation error for declarations that
// aren't subject to constant initialization (perhaps because a runtime value
// was passed as a constructor argument).
//
// On program shutdown, lifetime issues can be avoided on global objects by
// ensuring that they contain  trivial destructors.  A class has a trivial
// destructor unless it has a user-defined destructor, a virtual method or base
// class, or a data member or base class with a non-trivial destructor of its
// own.  Objects with static storage duration and a trivial destructor are not
// cleaned up on program shutdown, and are thus safe to access from other code
// running during shutdown.
//
// For a few core Abseil classes, we make a best effort to allow for safe global
// instances, even though these classes have non-trivial destructors.  These
// objects can be created with the absl::kConstInit tag.  For example:
//   ABSL_CONST_INIT absl::Mutex global_mutex(absl::kConstInit);
//
// The line above declares a global variable of type absl::Mutex which can be
// accessed at any point during startup or shutdown.  global_mutex's destructor
// will still run, but will not invalidate the object.  Note that C++ specifies
// that accessing an object after its destructor has run results in undefined
// behavior, but this pattern works on the toolchains we support.
//
// The absl::kConstInit tag should only be used to define objects with static
// or thread_local storage duration.

namespace absl {
ABSL_NAMESPACE_BEGIN

enum ConstInitType {
  kConstInit,
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_CONST_INIT_H_
