//
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
//
// -----------------------------------------------------------------------------
// File: reflection.h
// -----------------------------------------------------------------------------
//
// This file defines the routines to access and operate on an Abseil Flag's
// reflection handle.

#ifndef ABSL_FLAGS_REFLECTION_H_
#define ABSL_FLAGS_REFLECTION_H_

#include <string>

#include "absl/base/config.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/internal/commandlineflag.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {
class FlagSaverImpl;
}  // namespace flags_internal

// FindCommandLineFlag()
//
// Returns the reflection handle of an Abseil flag of the specified name, or
// `nullptr` if not found. This function will emit a warning if the name of a
// 'retired' flag is specified.
absl::CommandLineFlag* FindCommandLineFlag(absl::string_view name);

// Returns current state of the Flags registry in a form of mapping from flag
// name to a flag reflection handle.
absl::flat_hash_map<absl::string_view, absl::CommandLineFlag*> GetAllFlags();

//------------------------------------------------------------------------------
// FlagSaver
//------------------------------------------------------------------------------
//
// A FlagSaver object stores the state of flags in the scope where the FlagSaver
// is defined, allowing modification of those flags within that scope and
// automatic restoration of the flags to their previous state upon leaving the
// scope.
//
// A FlagSaver can be used within tests to temporarily change the test
// environment and restore the test case to its previous state.
//
// Example:
//
//   void MyFunc() {
//    absl::FlagSaver fs;
//    ...
//    absl::SetFlag(&FLAGS_myFlag, otherValue);
//    ...
//  } // scope of FlagSaver left, flags return to previous state
//
// This class is thread-safe.

class FlagSaver {
 public:
  FlagSaver();
  ~FlagSaver();

  FlagSaver(const FlagSaver&) = delete;
  void operator=(const FlagSaver&) = delete;

 private:
  flags_internal::FlagSaverImpl* impl_;
};

//-----------------------------------------------------------------------------

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_REFLECTION_H_
