// Copyright 2020 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Simple tool to print the list of targets that were compiled in when building
// this tool.

#include <stdint.h>
#include <stdio.h>

#include "hwy/highway.h"

namespace {

void PrintTargets(const char* msg, int64_t targets) {
  fprintf(stderr, "%s", msg);
  // For each bit other than the sign bit:
  for (int64_t x = targets & hwy::LimitsMax<int64_t>(); x != 0;
       x = x & (x - 1)) {
    // Extract value of least-significant bit.
    fprintf(stderr, " %s", hwy::TargetName(x & (~x + 1)));
  }
  fprintf(stderr, "\n");
}

}  // namespace

int main() {
#ifdef HWY_COMPILE_ONLY_EMU128
  const int only_emu128 = 1;
#else
  const int only_emu128 = 0;
#endif
#ifdef HWY_COMPILE_ONLY_SCALAR
  const int only_scalar = 1;
#else
  const int only_scalar = 0;
#endif
#ifdef HWY_COMPILE_ONLY_STATIC
  const int only_static = 1;
#else
  const int only_static = 0;
#endif
#ifdef HWY_COMPILE_ALL_ATTAINABLE
  const int all_attain = 1;
#else
  const int all_attain = 0;
#endif
#ifdef HWY_IS_TEST
  const int is_test = 1;
#else
  const int is_test = 0;
#endif

  fprintf(stderr,
          "Config: emu128:%d scalar:%d static:%d all_attain:%d is_test:%d\n",
          only_emu128, only_scalar, only_static, all_attain, is_test);
  PrintTargets("Compiled HWY_TARGETS:  ", HWY_TARGETS);
  PrintTargets("HWY_ATTAINABLE_TARGETS:", HWY_ATTAINABLE_TARGETS);
  PrintTargets("HWY_BASELINE_TARGETS:  ", HWY_BASELINE_TARGETS);
  PrintTargets("HWY_STATIC_TARGET:     ", HWY_STATIC_TARGET);
  PrintTargets("HWY_BROKEN_TARGETS:    ", HWY_BROKEN_TARGETS);
  PrintTargets("HWY_DISABLED_TARGETS:  ", HWY_DISABLED_TARGETS);
  PrintTargets("Current CPU supports:  ", hwy::SupportedTargets());
  return 0;
}
