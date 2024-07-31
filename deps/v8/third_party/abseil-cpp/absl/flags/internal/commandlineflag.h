//
// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_
#define ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_

#include "absl/base/config.h"
#include "absl/base/internal/fast_type_id.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// An alias for flag fast type id. This value identifies the flag value type
// similarly to typeid(T), without relying on RTTI being available. In most
// cases this id is enough to uniquely identify the flag's value type. In a few
// cases we'll have to resort to using actual RTTI implementation if it is
// available.
using FlagFastTypeId = absl::base_internal::FastTypeIdType;

// Options that control SetCommandLineOptionWithMode.
enum FlagSettingMode {
  // update the flag's value unconditionally (can call this multiple times).
  SET_FLAGS_VALUE,
  // update the flag's value, but *only if* it has not yet been updated
  // with SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef".
  SET_FLAG_IF_DEFAULT,
  // set the flag's default value to this.  If the flag has not been updated
  // yet (via SET_FLAGS_VALUE, SET_FLAG_IF_DEFAULT, or "FLAGS_xxx = nondef")
  // change the flag's current value to the new default value as well.
  SET_FLAGS_DEFAULT
};

// Options that control ParseFrom: Source of a value.
enum ValueSource {
  // Flag is being set by value specified on a command line.
  kCommandLine,
  // Flag is being set by value specified in the code.
  kProgrammaticChange,
};

// Handle to FlagState objects. Specific flag state objects will restore state
// of a flag produced this flag state from method CommandLineFlag::SaveState().
class FlagStateInterface {
 public:
  virtual ~FlagStateInterface();

  // Restores the flag originated this object to the saved state.
  virtual void Restore() const = 0;
};

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_COMMANDLINEFLAG_H_
