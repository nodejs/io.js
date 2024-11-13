# Copyright 2016 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use this to run several variants of the tests.
ALL_VARIANT_FLAGS = {
    "assert_types": [["--assert-types"]],
    "code_serializer": [["--cache=code"]],
    "default": [[]],
    "future": [["--future"]],
    "gc_stats": [["--gc-stats=1"]],
    # Alias of exhaustive variants, but triggering new test framework features.
    "infra_staging": [[]],
    "interpreted_regexp": [["--regexp-interpret-all"]],
    "stress_regexp_jit": [["--regexp-tier-up-ticks=0"]],
    "experimental_regexp": [["--default-to-experimental-regexp-engine"]],
    "jitless": [["--jitless", "--wasm-jitless-if-available-for-testing"]],
    "sparkplug": [["--sparkplug"]],
    "maglev": [["--maglev"]],
    "maglev_future": [["--maglev", "--maglev-future"]],
    "maglev_no_turbofan": [[
        "--maglev", "--no-turbofan",
        "--optimize-on-next-call-optimizes-to-maglev"
    ]],
    "stress_maglev": [[
        "--maglev", "--stress-maglev",
        "--optimize-on-next-call-optimizes-to-maglev"
    ]],
    "stress_maglev_future": [[
        "--maglev", "--maglev-future", "--stress-maglev",
        "--optimize-on-next-call-optimizes-to-maglev"
    ]],
    "stress_maglev_no_turbofan": [[
        "--maglev", "--no-turbofan", "--stress-maglev",
        "--optimize-on-next-call-optimizes-to-maglev"
    ]],
    # We test both the JS and Wasm Turboshaft pipelines under the same variant.
    # For extended Wasm Turboshaft coverage, we add --no-liftoff to the options.
    "turboshaft": [[
        "--turboshaft",
        "--turboshaft-future",
        "--turboshaft-wasm",
        "--no-wasm-generic-wrapper",
        "--no-wasm-to-js-generic-wrapper",
        "--no-liftoff",
    ]],
    "concurrent_sparkplug": [["--concurrent-sparkplug", "--sparkplug"]],
    "always_sparkplug": [["--always-sparkplug", "--sparkplug"]],
    # This combines two orthogonal variants always_sparkplug and
    # stress_regexp_jit to use bot resources more effectively.
    "always_sparkplug_and_stress_regexp_jit": [[
        "--always-sparkplug",
        "--sparkplug",
        "--regexp-tier-up-ticks=0",
    ]],
    "minor_ms": [["--minor-ms"]],
    "no_lfa": [["--no-lazy-feedback-allocation"]],
    # No optimization means disable all optimizations. OptimizeFunctionOnNextCall
    # would not force optimization too. It turns into a Nop. Please see
    # https://chromium-review.googlesource.com/c/452620/ for more discussion.
    # For WebAssembly, we test "Liftoff-only" in the nooptimization variant and
    # "TurboFan-only" in the stress variant. The WebAssembly configuration is
    # independent of JS optimizations, so we can combine those configs. We
    # disable lazy compilation to have one test variant that tests eager
    # compilation. "Liftoff-only" and eager compilation is not a problem,
    # because test functions do typically not get optimized to TurboFan anyways.
    "nooptimization": [[
        "--disable-optimizing-compilers", "--no-wasm-lazy-compilation"
    ]],
    "rehash_snapshot": [["--rehash-snapshot"]],
    "slow_path": [["--force-slow-path"]],
    "stress": [[
        "--no-liftoff", "--stress-lazy-source-positions",
        "--no-wasm-generic-wrapper", "--no-wasm-lazy-compilation",
        "--no-wasm-to-js-generic-wrapper"
    ]],
    "stress_concurrent_allocation": [["--stress-concurrent-allocation"]],
    "stress_concurrent_inlining": [["--stress-concurrent-inlining"]],
    "stress_js_bg_compile_wasm_code_gc": [[
        "--stress-background-compile", "--stress-wasm-code-gc"
    ]],
    "stress_incremental_marking": [["--stress-incremental-marking"]],
    "stress_snapshot": [["--stress-snapshot"]],
    # Trigger stress sampling allocation profiler with sample interval = 2^14
    "stress_sampling": [["--stress-sampling-allocation-profiler=16384"]],
    "no_wasm_traps": [["--no-wasm-trap-handler"]],
    "instruction_scheduling": [["--turbo-instruction-scheduling"]],
    "stress_instruction_scheduling": [["--turbo-stress-instruction-scheduling"]
                                     ],
    # Google3 variants.
    "google3_icu": [[]],
    "google3_noicu": [[]],
}

# Note these are specifically for the case when Turbofan is either fully
# disabled (i.e. not part of the binary), or when all codegen is disallowed (in
# jitless mode).
kIncompatibleFlagsForNoTurbofan = [
    "--turbofan", "--always-turbofan", "--liftoff", "--validate-asm",
    "--maglev", "--stress-concurrent-inlining"
]

# Flags that lead to a contradiction with the flags provided by the respective
# variant. This depends on the flags specified in ALL_VARIANT_FLAGS and on the
# implications defined in flag-definitions.h.
INCOMPATIBLE_FLAGS_PER_VARIANT = {
    "jitless":
        kIncompatibleFlagsForNoTurbofan + [
            "--track-field-types", "--sparkplug", "--concurrent-sparkplug",
            "--always-sparkplug", "--regexp-tier-up",
            "--no-regexp-interpret-all", "--interpreted-frames-native-stack"
        ],
    "nooptimization": [
        "--turbofan", "--always-turbofan", "--turboshaft",
        "--turboshaft-future", "--maglev", "--no-liftoff", "--wasm-tier-up",
        "--wasm-dynamic-tiering", "--validate-asm", "--track-field-types",
        "--stress-concurrent-inlining"
    ],
    "slow_path": ["--no-force-slow-path"],
    "stress_concurrent_allocation": [
        "--single-threaded", "--single-threaded-gc", "--predictable"
    ],
    "stress_concurrent_inlining": [
        "--single-threaded", "--predictable", "--lazy-feedback-allocation",
        "--assert-types", "--turboshaft-assert-types",
        "--no-concurrent-recompilation", "--no-turbofan", "--jitless"
    ],
    # The fast API tests initialize an embedder object that never needs to be
    # serialized to the snapshot, so we don't have a
    # SerializeInternalFieldsCallback for it, so they are incompatible with
    # stress_snapshot.
    "stress_snapshot": ["--expose-fast-api"],
    "stress": [
        # 'stress' disables Liftoff, which conflicts with flags that require
        # Liftoff support.
        "--liftoff-only",
        "--wasm-dynamic-tiering"
    ],
    "sparkplug": ["--jitless", "--no-sparkplug"],
    "turboshaft": [
        # 'turboshaft' disables Liftoff, which conflicts with flags that require
        # Liftoff support.
        "--liftoff-only",
        "--wasm-dynamic-tiering"
    ],
    "concurrent_sparkplug": ["--jitless"],
    "maglev": ["--jitless", "--no-maglev"],
    "maglev_future": ["--jitless", "--no-maglev", "--no-maglev-future"],
    "maglev_no_turbofan": [
        "--jitless",
        "--no-maglev",
        "--turbofan",
        "--always-turbofan",
        "--stress-concurrent-inlining",
    ],
    "stress_maglev": ["--jitless"],
    "stress_maglev_future": ["--jitless", "--no-maglev", "--no-maglev-future"],
    "stress_maglev_no_turbofan": [
        "--jitless",
        "--no-maglev",
        "--turbofan",
        "--always-turbofan",
        "--stress-concurrent-inlining",
    ],
    "always_sparkplug": ["--jitless", "--no-sparkplug"],
    "always_sparkplug_and_stress_regexp_jit": ["--jitless", "--no-sparkplug"],
    "code_serializer": [
        "--cache=after-execute", "--cache=full-code-cache", "--cache=none"
    ],
    "interpreted_regexp": ["--regexp-tier-up"],
    "stress_regexp_jit": ["--regexp-interpret-all"],
    "experimental_regexp": ["--no-enable-experimental-regexp-engine"],
    "assert_types": [
        "--concurrent-recompilation", "--stress_concurrent_inlining",
        "--no-assert-types"
    ],
    "--turboshaft-assert-types": [
        "--concurrent-recompilation", "--stress_concurrent_inlining",
        "--no-turboshaft-assert-types"
    ],
}

# Flags that lead to a contradiction under certain build variables.
# This corresponds to the build variables usable in status files as generated
# in _get_statusfile_variables in base_runner.py.
# The conflicts might be directly contradictory flags or be caused by the
# implications defined in flag-definitions.h.
# The keys of the following map support negation through '!', e.g. rule
#
#   "!code_comments": [...]
#
# applies when the code_comments build variable is NOT set.
INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE = {
    "!code_comments": ["--code-comments"],
    "!DEBUG_defined": [
        "--check_handle_count",
        "--code_stats",
        "--dump_wasm_module",
        "--enable_testing_opcode_in_wasm",
        "--gc_verbose",
        "--print_ast",
        "--print_break_location",
        "--print_global_handles",
        "--print_handles",
        "--print_scopes",
        "--regexp_possessive_quantifier",
        "--trace_backing_store",
        "--trace_contexts",
        "--trace_isolates",
        "--trace_lazy",
        "--trace_liftoff",
        "--trace_module_status",
        "--trace_normalization",
        "--trace_turbo_escape",
        "--trace_wasm_compiler",
        "--trace_wasm_decoder",
        "--trace_wasm_instances",
        "--trace_wasm_lazy_compilation",
        "--trace_wasm_native_heap",
        "--trace_wasm_serialization",
        "--trace_wasm_stack_switching",
        "--trace_wasm_streaming",
        "--trap_on_abort",
    ],
    "!verify_heap": ["--verify-heap"],
    "!debug_code": ["--debug-code"],
    "!disassembler": [
        "--print_all_code", "--print_code", "--print_opt_code",
        "--print_code_verbose", "--print_builtin_code", "--print_regexp_code"
    ],
    "!slow_dchecks": ["--enable-slow-asserts"],
    "!gdbjit": ["--gdbjit", "--gdbjit_full", "--gdbjit_dump"],
    "!has_maglev": ["--maglev"],
    "!has_turbofan": kIncompatibleFlagsForNoTurbofan,
    "has_jitless": INCOMPATIBLE_FLAGS_PER_VARIANT["jitless"],
    "lite_mode": INCOMPATIBLE_FLAGS_PER_VARIANT["jitless"],
    "verify_predictable": [
        "--parallel-compile-tasks-for-eager-toplevel",
        "--parallel-compile-tasks-for-lazy", "--concurrent-recompilation",
        "--stress-concurrent-allocation", "--stress-concurrent-inlining"
    ],
    "dict_property_const_tracking": ["--stress-concurrent-inlining"],
}

# Flags that lead to a contradiction when a certain extra-flag is present.
# Such extra-flags are defined for example in infra/testing/builders.pyl or in
# standard_runner.py.
# The conflicts might be directly contradictory flags or be caused by the
# implications defined in flag-definitions.h.
INCOMPATIBLE_FLAGS_PER_EXTRA_FLAG = {
    "--concurrent-recompilation": [
        "--predictable", "--assert-types", "--turboshaft-assert-types",
        "--single-threaded"
    ],
    "--parallel-compile-tasks-for-eager-toplevel": ["--predictable"],
    "--parallel-compile-tasks-for-lazy": ["--predictable"],
    "--gc-interval=*": ["--gc-interval=*"],
    "--stress_concurrent_allocation":
        INCOMPATIBLE_FLAGS_PER_VARIANT["stress_concurrent_allocation"],
    "--stress-concurrent-inlining":
        INCOMPATIBLE_FLAGS_PER_VARIANT["stress_concurrent_inlining"],
    "--turboshaft-assert-types": [
        "--concurrent-recompilation", "--stress-concurrent-inlining"
    ],
}

SLOW_VARIANTS = set([
  'stress',
  'stress_snapshot',
  'nooptimization',
])

FAST_VARIANTS = set([
  'default'
])


def _variant_order_key(v):
  if v in SLOW_VARIANTS:
    return 0
  if v in FAST_VARIANTS:
    return 100
  return 50

ALL_VARIANTS = sorted(ALL_VARIANT_FLAGS.keys(),
                      key=_variant_order_key)

# For internal integrity checking.
REQUIRED_BUILD_VARIABLES = [
    var.lstrip('!') for var in INCOMPATIBLE_FLAGS_PER_BUILD_VARIABLE.keys()
]

# Check {SLOW,FAST}_VARIANTS entries
for variants in [SLOW_VARIANTS, FAST_VARIANTS]:
  for v in variants:
    assert v in ALL_VARIANT_FLAGS
