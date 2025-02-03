// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef __cplusplus
#error This is a C compile test
#endif

// This test ensures that headers that are included in legacy C code are
// compatible with C. Abseil is a C++ library. We do not desire to expand C
// compatibility or keep C compatibility forever. This test only exists to
// ensure C compatibility until it is no longer required. Do not add new code
// that requires C compatibility.
#include "absl/base/attributes.h"     // IWYU pragma: keep
#include "absl/base/config.h"         // IWYU pragma: keep
#include "absl/base/optimization.h"   // IWYU pragma: keep
#include "absl/base/policy_checks.h"  // IWYU pragma: keep
#include "absl/base/port.h"           // IWYU pragma: keep

int main() { return 0; }
