// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_FEATURE_FLAGS_H_
#define V8_WASM_WASM_FEATURE_FLAGS_H_

// See https://github.com/WebAssembly/proposals for an overview of current
// WebAssembly proposals.

// Experimental features (disabled by default).
#define FOREACH_WASM_EXPERIMENTAL_FEATURE_FLAG(V) /*     (force 80 columns) */ \
  /* No official proposal (yet?). */                                           \
  /* V8 side owner: clemensb */                                                \
  V(compilation_hints, "compilation hints section", false)                     \
                                                                               \
  /* Instruction Tracing tool convention (early prototype, might change) */    \
  /* Tool convention: https://github.com/WebAssembly/tool-conventions */       \
  /* V8 side owner: jabraham */                                                \
  V(instruction_tracing, "instruction tracing section", false)                 \
                                                                               \
  /* Non-specified, V8-only experimental additions to the GC proposal */       \
  /* V8 side owner: jkummerow */                                               \
  V(assume_ref_cast_succeeds,                                                  \
    "assume ref.cast always succeeds and skip the related type check "         \
    "(unsafe)",                                                                \
    false)                                                                     \
  V(ref_cast_nop, "enable unsafe ref.cast_nop instruction", false)             \
  V(skip_null_checks,                                                          \
    "skip null checks for call.ref and array and struct operations (unsafe)",  \
    false)                                                                     \
  V(skip_bounds_checks, "skip array bounds checks (unsafe)", false)            \
                                                                               \
  /* Branch Hinting proposal. */                                               \
  /* https://github.com/WebAssembly/branch-hinting */                          \
  /* V8 side owner: jkummerow */                                               \
  V(branch_hinting, "branch hinting", false)                                   \
                                                                               \
  /* Stack Switching proposal. */                                              \
  /* https://github.com/WebAssembly/stack-switching */                         \
  /* V8 side owner: thibaudm, fgm */                                           \
  V(stack_switching, "stack switching", false)                                 \
                                                                               \
  /* JavaScript Promise Integration proposal. */                               \
  /* https://github.com/WebAssembly/js-promise-integration */                  \
  /* V8 side owner: thibaudm, fgm */                                           \
  V(jspi, "javascript promise integration", false)                             \
                                                                               \
  /* Shared-Everything Threads proposal. */                                    \
  /* https://github.com/WebAssembly/shared-everything-threads */               \
  /* V8 side owner: manoskouk */                                               \
  V(shared, "shared-everything threads", false)

// #############################################################################
// Staged features (disabled by default, but enabled via --wasm-staging (also
// exposed as chrome://flags/#enable-experimental-webassembly-features). Staged
// features get limited fuzzer coverage, and should come with their own tests.
// They are not run through all fuzzers though and don't get much exposure in
// the wild. Staged features are not necessarily fully stabilized. They should
// be shipped with enough lead time to the next branch to allow for
// stabilization.
// Consider adding a chromium-side use counter if you want to track usage in the
// wild (also see {V8::UseCounterFeature}).
#define FOREACH_WASM_STAGING_FEATURE_FLAG(V) /*          (force 80 columns) */ \
  /* Type reflection proposal. */                                              \
  /* https://github.com/webassembly/js-types */                                \
  /* V8 side owner: ahaas */                                                   \
  /* Staged in v7.8. */                                                        \
  V(type_reflection, "wasm type reflection in JS", false)                      \
                                                                               \
  /* Memory64 proposal. */                                                     \
  /* https://github.com/WebAssembly/memory64 */                                \
  /* V8 side owner: clemensb */                                                \
  V(memory64, "memory64", false)                                               \
                                                                               \
  /* Reference-Typed Strings Proposal. */                                      \
  /* https://github.com/WebAssembly/stringref */                               \
  /* V8 side owner: jkummerow */                                               \
  V(stringref, "reference-typed strings", false)                               \
                                                                               \
  /* Imported Strings Proposal. */                                             \
  /* https://github.com/WebAssembly/js-string-builtins */                      \
  /* V8 side owner: jkummerow */                                               \
  V(imported_strings, "imported strings", false)                               \
                                                                               \
  /* Not user-visible, defined here so an Origin Trial can control it. */      \
  /* V8 side owner: manoskouk, clemensb */                                     \
  /* Staged in v11.3 */                                                        \
  /* Shipped for wasm-gc modules as part of wasm-gc in v11.9 */                \
  /* Launch bug: https://crbug.com/1424350 */                                  \
  V(inlining, "wasm-into-wasm inlining", false)                                \
                                                                               \
  /* Exnref */                                                                 \
  /* This flag enables the new exception handling proposal */                  \
  /* V8 side owner: thibaudm */                                                \
  V(exnref, "exnref", false)

// #############################################################################
// Shipped features (enabled by default). Remove the feature flag once they hit
// stable and are expected to stay enabled.
#define FOREACH_WASM_SHIPPED_FEATURE_FLAG(V) /*          (force 80 columns) */ \
  /* Multi-memory Proposal. */                                                 \
  /* https://github.com/WebAssembly/multi-memory */                            \
  /* V8 side owner: clemensb */                                                \
  /* Staged in v11.7. */                                                       \
  /* Shipped in v12.0. */                                                      \
  /* ITS: */                                                                   \
  /* https://groups.google.com/a/chromium.org/g/blink-dev/c/WSrXwhKeSas */     \
  V(multi_memory, "multi-memory", true)                                        \
                                                                               \
  /* Legacy exception handling proposal. */                                    \
  /* https://github.com/WebAssembly/exception-handling */                      \
  /* V8 side owner: thibaudm */                                                \
  /* Staged in v8.9 */                                                         \
  /* Shipped in v9.5 */                                                        \
  V(legacy_eh, "legacy exception handling opcodes", true)

// Combination of all available wasm feature flags.
#define FOREACH_WASM_FEATURE_FLAG(V)        \
  FOREACH_WASM_EXPERIMENTAL_FEATURE_FLAG(V) \
  FOREACH_WASM_STAGING_FEATURE_FLAG(V)      \
  FOREACH_WASM_SHIPPED_FEATURE_FLAG(V)

// Consistency check: Experimental and staged features are off by default.
#define CHECK_WASM_FEATURE_OFF_BY_DEFAULT(name, desc, enabled) \
  static_assert(enabled == false);
#define CHECK_WASM_FEATURE_ON_BY_DEFAULT(name, desc, enabled) \
  static_assert(enabled == true);
FOREACH_WASM_EXPERIMENTAL_FEATURE_FLAG(CHECK_WASM_FEATURE_OFF_BY_DEFAULT)
FOREACH_WASM_STAGING_FEATURE_FLAG(CHECK_WASM_FEATURE_OFF_BY_DEFAULT)
FOREACH_WASM_SHIPPED_FEATURE_FLAG(CHECK_WASM_FEATURE_ON_BY_DEFAULT)
#undef CHECK_WASM_FEATURE_OFF_BY_DEFAULT
#undef CHECK_WASM_FEATURE_ON_BY_DEFAULT

#endif  // V8_WASM_WASM_FEATURE_FLAGS_H_
