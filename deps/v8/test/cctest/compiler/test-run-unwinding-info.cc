// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8config.h"

// Test enabled only on supported architectures.
#if V8_OS_LINUX &&                                                 \
    (defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM) || \
     defined(V8_TARGET_ARCH_ARM64))

#include "src/flags/flags.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/function-tester.h"

namespace v8 {
namespace internal {
namespace compiler {

TEST(RunUnwindingInfo) {
  v8_flags.always_turbofan = true;
  v8_flags.perf_prof_unwinding_info = true;

  FunctionTester tester(
      "(function (x) {\n"
      "  function f(x) { return x*x; }\n"
      "  return x > 0 ? x+1 : f(x);\n"
      "})");

  tester.Call(tester.Val(-1));

  CHECK(tester.function->code(tester.main_isolate())->has_unwinding_info());
}

// TODO(ssanfilippo) Build low-level graph and check that state is correctly
// restored in the following situation:
//
//                         +-----------------+
//                         |     no frame    |---+
//  check that a           +-----------------+   |
//  a noframe state        | construct frame |<--+
//  is restored here  -->  +-----------------+   |
//                         | construct frame |<--+
//                         +-----------------+
//
// Same for <construct>/<destruct>/<destruct> (a <construct> status is restored)

// TODO(ssanfilippo) Intentionally reach a BB with different initial states
// and check that the UnwindingInforWriter fails in debug mode:
//
//      +----------------+
//  +---|     State A    |
//  |   +----------------+
//  |   |  State B != A  |---+
//  |   +----------------+   |
//  +-->|  Failure here  |<--+
//      +----------------+

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif
