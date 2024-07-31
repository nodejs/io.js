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
// File: seed_gen_exception.h
// -----------------------------------------------------------------------------
//
// This header defines an exception class which may be thrown if unpredictable
// events prevent the derivation of suitable seed-material for constructing a
// bit generator conforming to [rand.req.urng] (eg. entropy cannot be read from
// /dev/urandom on a Unix-based system).
//
// Note: if exceptions are disabled, `std::terminate()` is called instead.

#ifndef ABSL_RANDOM_SEED_GEN_EXCEPTION_H_
#define ABSL_RANDOM_SEED_GEN_EXCEPTION_H_

#include <exception>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

//------------------------------------------------------------------------------
// SeedGenException
//------------------------------------------------------------------------------
class SeedGenException : public std::exception {
 public:
  SeedGenException() = default;
  ~SeedGenException() override;
  const char* what() const noexcept override;
};

namespace random_internal {

// throw delegator
[[noreturn]] void ThrowSeedGenException();

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_SEED_GEN_EXCEPTION_H_
