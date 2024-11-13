// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all of the flags.  It is separated into different section,
// for Debug, Release, Logging and Profiling, etc.  To add a new flag, find the
// correct section, and use one of the DEFINE_ macros, without a trailing ';'.
//
// This include does not have a guard, because it is a template-style include,
// which can be included multiple times in different modes.  It expects to have
// a mode defined before it's included.  The modes are FLAG_MODE_... below:
//
// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#define DEFINE_IMPLICATION(whenflag, thenflag) \
  DEFINE_VALUE_IMPLICATION(whenflag, thenflag, true)

// A weak implication will be overwritten by a normal implication or by an
// explicit flag.
#define DEFINE_WEAK_IMPLICATION(whenflag, thenflag) \
  DEFINE_WEAK_VALUE_IMPLICATION(whenflag, thenflag, true)

#define DEFINE_WEAK_NEG_IMPLICATION(whenflag, thenflag) \
  DEFINE_WEAK_VALUE_IMPLICATION(whenflag, thenflag, false)

#define DEFINE_NEG_IMPLICATION(whenflag, thenflag) \
  DEFINE_VALUE_IMPLICATION(whenflag, thenflag, false)

#define DEFINE_NEG_NEG_IMPLICATION(whenflag, thenflag) \
  DEFINE_NEG_VALUE_IMPLICATION(whenflag, thenflag, false)

// With FLAG_MODE_DECLARE we declare the fields in the {FlagValues} struct.
// Read-only flags are static constants instead of fields.
#if defined(FLAG_MODE_DECLARE)
#define FLAG_FULL(ftype, ctype, nam, def, cmt) FlagValue<ctype> nam{def};
#define FLAG_READONLY(ftype, ctype, nam, def, cmt) \
  static constexpr FlagValue<ctype> nam{def};

// We need to define all of our default values so that the Flag structure can
// access them by pointer.  These are just used internally inside of one .cc,
// for MODE_META, so there is no impact on the flags interface.
#elif defined(FLAG_MODE_DEFINE_DEFAULTS)
#define FLAG_FULL(ftype, ctype, nam, def, cmt) \
  static constexpr ctype FLAGDEFAULT_##nam{def};
#define FLAG_READONLY(ftype, ctype, nam, def, cmt) \
  static constexpr ctype FLAGDEFAULT_##nam{def};

// We want to write entries into our meta data table, for internal parsing and
// printing / etc in the flag parser code.
#elif defined(FLAG_MODE_META)
#define FLAG_FULL(ftype, ctype, nam, def, cmt) \
  {Flag::TYPE_##ftype, #nam, &v8_flags.nam, &FLAGDEFAULT_##nam, cmt, false},
// Readonly flags don't pass the value pointer since the struct expects a
// mutable value. That's okay since the value always equals the default.
#define FLAG_READONLY(ftype, ctype, nam, def, cmt) \
  {Flag::TYPE_##ftype, #nam, nullptr, &FLAGDEFAULT_##nam, cmt, false},
#define FLAG_ALIAS(ftype, ctype, alias, nam)                       \
  {Flag::TYPE_##ftype,  #alias, &v8_flags.nam, &FLAGDEFAULT_##nam, \
   "alias for --" #nam, false},  // NOLINT(whitespace/indent)

// We produce the code to set flags when it is implied by another flag.
#elif defined(FLAG_MODE_DEFINE_IMPLICATIONS)
#define DEFINE_VALUE_IMPLICATION(whenflag, thenflag, value)   \
  changed |= TriggerImplication(v8_flags.whenflag, #whenflag, \
                                &v8_flags.thenflag, #thenflag, value, false);

// A weak implication will be overwritten by a normal implication or by an
// explicit flag.
#define DEFINE_WEAK_VALUE_IMPLICATION(whenflag, thenflag, value) \
  changed |= TriggerImplication(v8_flags.whenflag, #whenflag,    \
                                &v8_flags.thenflag, #thenflag, value, true);

#define DEFINE_GENERIC_IMPLICATION(whenflag, statement) \
  if (v8_flags.whenflag) statement;

#define DEFINE_NEG_VALUE_IMPLICATION(whenflag, thenflag, value)    \
  changed |= TriggerImplication(!v8_flags.whenflag, "!" #whenflag, \
                                &v8_flags.thenflag, #thenflag, value, false);

#define DEFINE_NEG_VALUE_VALUE_IMPLICATION(whenflag, whenvalue, thenflag, \
                                           thenvalue)                     \
  changed |=                                                              \
      TriggerImplication(v8_flags.whenflag != whenvalue, #whenflag,       \
                         &v8_flags.thenflag, #thenflag, thenvalue, false);

#define DEFINE_MIN_VALUE_IMPLICATION(flag, min_value)             \
  changed |= TriggerImplication(v8_flags.flag < min_value, #flag, \
                                &v8_flags.flag, #flag, min_value, false);

#define DEFINE_DISABLE_FLAG_IMPLICATION(whenflag, thenflag) \
  if (v8_flags.whenflag && v8_flags.thenflag) {             \
    PrintF(stderr, "Warning: disabling flag --" #thenflag   \
                   " due to conflicting flags\n");          \
  }                                                         \
  DEFINE_VALUE_IMPLICATION(whenflag, thenflag, false)

// We apply a generic macro to the flags.
#elif defined(FLAG_MODE_APPLY)

#define FLAG_FULL FLAG_MODE_APPLY

#else
#error No mode supplied when including flags.defs
#endif

// Dummy defines for modes where it is not relevant.
#ifndef FLAG_FULL
#define FLAG_FULL(ftype, ctype, nam, def, cmt)
#endif

#ifndef FLAG_READONLY
#define FLAG_READONLY(ftype, ctype, nam, def, cmt)
#endif

#ifndef FLAG_ALIAS
#define FLAG_ALIAS(ftype, ctype, alias, nam)
#endif

#ifndef DEFINE_VALUE_IMPLICATION
#define DEFINE_VALUE_IMPLICATION(whenflag, thenflag, value)
#endif

#ifndef DEFINE_WEAK_VALUE_IMPLICATION
#define DEFINE_WEAK_VALUE_IMPLICATION(whenflag, thenflag, value)
#endif

#ifndef DEFINE_GENERIC_IMPLICATION
#define DEFINE_GENERIC_IMPLICATION(whenflag, statement)
#endif

#ifndef DEFINE_NEG_VALUE_IMPLICATION
#define DEFINE_NEG_VALUE_IMPLICATION(whenflag, thenflag, value)
#endif
#ifndef DEFINE_NEG_VALUE_VALUE_IMPLICATION
#define DEFINE_NEG_VALUE_VALUE_IMPLICATION(whenflag, whenvalue, thenflag, \
                                           thenvalue)
#endif

#ifndef DEFINE_MIN_VALUE_IMPLICATION
#define DEFINE_MIN_VALUE_IMPLICATION(flag, min_value)
#endif

#ifndef DEFINE_DISABLE_FLAG_IMPLICATION
#define DEFINE_DISABLE_FLAG_IMPLICATION(whenflag, thenflag)
#endif

#ifndef DEBUG_BOOL
#error DEBUG_BOOL must be defined at this point.
#endif  // DEBUG_BOOL

#if V8_ENABLE_SPARKPLUG
#define ENABLE_SPARKPLUG_BY_DEFAULT true
#else
#define ENABLE_SPARKPLUG_BY_DEFAULT false
#endif

// Supported ARM configurations are:
//  "armv6":       ARMv6 + VFPv2
//  "armv7":       ARMv7 + VFPv3-D32 + NEON
//  "armv7+sudiv": ARMv7 + VFPv4-D32 + NEON + SUDIV
//  "armv8":       ARMv8 (including all of the above)
#if !defined(ARM_TEST_NO_FEATURE_PROBE) ||                            \
    (defined(CAN_USE_ARMV8_INSTRUCTIONS) &&                           \
     defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_SUDIV) && \
     defined(CAN_USE_NEON) && defined(CAN_USE_VFP3_INSTRUCTIONS))
#define ARM_ARCH_DEFAULT "armv8"
#elif defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_SUDIV) && \
    defined(CAN_USE_NEON) && defined(CAN_USE_VFP3_INSTRUCTIONS)
#define ARM_ARCH_DEFAULT "armv7+sudiv"
#elif defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(CAN_USE_NEON) && \
    defined(CAN_USE_VFP3_INSTRUCTIONS)
#define ARM_ARCH_DEFAULT "armv7"
#else
#define ARM_ARCH_DEFAULT "armv6"
#endif

#ifdef V8_OS_WIN
#define ENABLE_LOG_COLOUR false
#else
#define ENABLE_LOG_COLOUR true
#endif

#define DEFINE_BOOL(nam, def, cmt) FLAG(BOOL, bool, nam, def, cmt)
#define DEFINE_BOOL_READONLY(nam, def, cmt) \
  FLAG_READONLY(BOOL, bool, nam, def, cmt)
#define DEFINE_MAYBE_BOOL(nam, cmt) \
  FLAG(MAYBE_BOOL, std::optional<bool>, nam, std::nullopt, cmt)
#define DEFINE_INT(nam, def, cmt) FLAG(INT, int, nam, def, cmt)
#define DEFINE_UINT(nam, def, cmt) FLAG(UINT, unsigned int, nam, def, cmt)
#define DEFINE_UINT_READONLY(nam, def, cmt) \
  FLAG_READONLY(UINT, unsigned int, nam, def, cmt)
#define DEFINE_UINT64(nam, def, cmt) FLAG(UINT64, uint64_t, nam, def, cmt)
#define DEFINE_FLOAT(nam, def, cmt) FLAG(FLOAT, double, nam, def, cmt)
#define DEFINE_SIZE_T(nam, def, cmt) FLAG(SIZE_T, size_t, nam, def, cmt)
#define DEFINE_STRING(nam, def, cmt) FLAG(STRING, const char*, nam, def, cmt)
#define DEFINE_ALIAS_BOOL(alias, nam) FLAG_ALIAS(BOOL, bool, alias, nam)
#define DEFINE_ALIAS_INT(alias, nam) FLAG_ALIAS(INT, int, alias, nam)
#define DEFINE_ALIAS_FLOAT(alias, nam) FLAG_ALIAS(FLOAT, double, alias, nam)
#define DEFINE_ALIAS_SIZE_T(alias, nam) FLAG_ALIAS(SIZE_T, size_t, alias, nam)
#define DEFINE_ALIAS_STRING(alias, nam) \
  FLAG_ALIAS(STRING, const char*, alias, nam)

#ifdef DEBUG
#define DEFINE_DEBUG_BOOL DEFINE_BOOL
#else
#define DEFINE_DEBUG_BOOL DEFINE_BOOL_READONLY
#endif

//
// Flags in all modes.
//
#define FLAG FLAG_FULL

// Experimental features.
// Features that are still considered experimental and which are not ready for
// fuzz testing should be defined using this macro. The feature will then imply
// --experimental, which will indicate to the user that they are running an
// experimental configuration of V8. Experimental features are always disabled
// by default. When these features mature, the flag should first turn into a
// regular feature flag (still disabled by default) and then ideally be staged
// behind (for example) --future before being enabled by default.
DEFINE_BOOL(experimental, false,
            "Indicates that V8 is running with experimental features enabled. "
            "This flag is typically not set explicitly but instead enabled as "
            "an implication of other flags which enable experimental features.")
#define DEFINE_EXPERIMENTAL_FEATURE(nam, cmt)         \
  FLAG(BOOL, bool, nam, false, cmt " (experimental)") \
  DEFINE_IMPLICATION(nam, experimental)

// ATTENTION: This is set to true by default in d8. But for API compatibility,
// it generally defaults to false.
DEFINE_BOOL(abort_on_contradictory_flags, false,
            "Disallow flags or implications overriding each other.")
// This implication is also hard-coded into the flags processing to make sure it
// becomes active before we even process subsequent flags.
DEFINE_NEG_IMPLICATION(fuzzing, abort_on_contradictory_flags)
// As abort_on_contradictory_flags, but it will simply exit with return code 0.
DEFINE_BOOL(exit_on_contradictory_flags, false,
            "Exit with return code 0 on contradictory flags.")
// We rely on abort_on_contradictory_flags to turn on the analysis.
DEFINE_WEAK_IMPLICATION(exit_on_contradictory_flags,
                        abort_on_contradictory_flags)
// This is not really a flag, it affects the interpretation of the next flag but
// doesn't become permanently true when specified. This only works for flags
// defined in this file, but not for d8 flags defined in src/d8/d8.cc.
DEFINE_BOOL(allow_overwriting_for_next_flag, false,
            "temporary disable flag contradiction to allow overwriting just "
            "the next flag")

// Flags for language modes and experimental language features.
DEFINE_BOOL(use_strict, false, "enforce strict mode")

DEFINE_BOOL(trace_temporal, false, "trace temporal code")

DEFINE_BOOL(harmony, false, "enable all completed harmony features")
DEFINE_BOOL(harmony_shipping, true, "enable all shipped harmony features")

DEFINE_BOOL(js_staging, false, "enable all completed JavaScript features")
DEFINE_BOOL(js_shipping, true, "enable all shipped JavaScript features")

// Update bootstrapper.cc whenever adding a new feature flag.

// Features that are still work in progress (behind individual flags).
//
// The "harmony" naming is now outdated and will no longer be used for new JS
// features. Use the JAVASCRIPT macros instead.
//
// TODO(v8:14214): Remove --harmony flags once transition is complete.
#define HARMONY_INPROGRESS_BASE(V)                                             \
  V(harmony_weak_refs_with_cleanup_some,                                       \
    "harmony weak references with FinalizationRegistry.prototype.cleanupSome") \
  V(harmony_temporal, "Temporal")                                              \
  V(harmony_shadow_realm, "harmony ShadowRealm")                               \
  V(harmony_struct, "harmony structs, shared structs, and shared arrays")      \
  V(harmony_import_assertions, "harmony import assertions (deprecated)")

#define JAVASCRIPT_INPROGRESS_FEATURES_BASE(V)                               \
  V(js_explicit_resource_management, "explicit resource management")         \
  V(js_float16array,                                                         \
    "Float16Array, Math.f16round, DataView.getFloat16, DataView.setFloat16") \
  V(js_decorators, "decorators")                                             \
  V(js_source_phase_imports, "source phase imports")

#ifdef V8_INTL_SUPPORT
#define HARMONY_INPROGRESS(V)                                       \
  HARMONY_INPROGRESS_BASE(V)                                        \
  V(harmony_intl_best_fit_matcher, "Intl BestFitMatcher")           \
  /* Following two flags should ship the same time but may stage */ \
  /* differently . */                                               \
  V(harmony_remove_intl_locale_info_getters,                        \
    "Remove Obsoleted Intl Locale Info getters")

#define JAVASCRIPT_INPROGRESS_FEATURES(V) JAVASCRIPT_INPROGRESS_FEATURES_BASE(V)
#else
#define HARMONY_INPROGRESS(V) HARMONY_INPROGRESS_BASE(V)
#define JAVASCRIPT_INPROGRESS_FEATURES(V) JAVASCRIPT_INPROGRESS_FEATURES_BASE(V)
#endif

// Features that are complete (but still behind the --harmony flag).
#define HARMONY_STAGED_BASE(V)
#define JAVASCRIPT_STAGED_FEATURES_BASE(V) V(js_atomics_pause, "Atomics.pause")

#ifdef V8_INTL_SUPPORT
#define HARMONY_STAGED(V)                                    \
  HARMONY_STAGED_BASE(V)                                     \
  V(harmony_intl_locale_info_func, "Intl Locale Info API as functions")
#define JAVASCRIPT_STAGED_FEATURES(V) JAVASCRIPT_STAGED_FEATURES_BASE(V)
#else
#define HARMONY_STAGED(V) HARMONY_STAGED_BASE(V)
#define JAVASCRIPT_STAGED_FEATURES(V) JAVASCRIPT_STAGED_FEATURES_BASE(V)
#endif

// Features that are shipping (turned on by default, but internal flag remains).
#define HARMONY_SHIPPING_BASE(V)                             \
  V(harmony_iterator_helpers, "JavaScript iterator helpers") \
  V(harmony_set_methods, "harmony Set Methods")              \
  V(harmony_import_attributes, "harmony import attributes")

#define JAVASCRIPT_SHIPPING_FEATURES_BASE(V)                           \
  V(js_regexp_duplicate_named_groups, "RegExp duplicate named groups") \
  V(js_regexp_modifiers, "RegExp modifiers")                           \
  V(js_promise_try, "Promise.try")

#ifdef V8_INTL_SUPPORT
#define HARMONY_SHIPPING(V) \
  HARMONY_SHIPPING_BASE(V)  \
  V(harmony_intl_duration_format, "Intl DurationFormat API")
#define JAVASCRIPT_SHIPPING_FEATURES(V) JAVASCRIPT_SHIPPING_FEATURES_BASE(V)
#else
#define HARMONY_SHIPPING(V) HARMONY_SHIPPING_BASE(V)
#define JAVASCRIPT_SHIPPING_FEATURES(V) JAVASCRIPT_SHIPPING_FEATURES_BASE(V)
#endif

// Once a shipping feature has proved stable in the wild, it will be dropped
// from HARMONY_SHIPPING, all occurrences of the FLAG_ variable are removed,
// and associated tests are moved from the harmony directory to the appropriate
// esN directory.
//
// In-progress features are not code complete and are considered experimental,
// i.e. not ready for fuzz testing.

#define FLAG_INPROGRESS_FEATURES(id, description)                     \
  DEFINE_BOOL(id, false,                                              \
              "enable " #description " (in progress / experimental)") \
  DEFINE_IMPLICATION(id, experimental)
HARMONY_INPROGRESS(FLAG_INPROGRESS_FEATURES)
JAVASCRIPT_INPROGRESS_FEATURES(FLAG_INPROGRESS_FEATURES)
#undef FLAG_INPROGRESS_FEATURES

#define FLAG_STAGED_FEATURES(id, description)    \
  DEFINE_BOOL(id, false, "enable " #description) \
  DEFINE_IMPLICATION(harmony, id)                \
  DEFINE_IMPLICATION(js_staging, id)
HARMONY_STAGED(FLAG_STAGED_FEATURES)
JAVASCRIPT_STAGED_FEATURES(FLAG_STAGED_FEATURES)
DEFINE_IMPLICATION(harmony, js_staging)
#undef FLAG_STAGED_FEATURES

#define FLAG_SHIPPING_FEATURES(id, description)    \
  DEFINE_BOOL(id, true, "enable " #description)    \
  DEFINE_NEG_NEG_IMPLICATION(harmony_shipping, id) \
  DEFINE_NEG_NEG_IMPLICATION(js_shipping, id)
HARMONY_SHIPPING(FLAG_SHIPPING_FEATURES)
JAVASCRIPT_SHIPPING_FEATURES(FLAG_SHIPPING_FEATURES)
DEFINE_NEG_NEG_IMPLICATION(harmony_shipping, js_shipping)
#undef FLAG_SHIPPING_FEATURES

DEFINE_BOOL(builtin_subclassing, true,
            "subclassing support in built-in methods")

// If the following flag is set to `true`, the SharedArrayBuffer constructor is
// enabled per context depending on the callback set via
// `SetSharedArrayBufferConstructorEnabledCallback`. If no callback is set, the
// SharedArrayBuffer constructor is disabled.
DEFINE_BOOL(enable_sharedarraybuffer_per_context, false,
            "enable the SharedArrayBuffer constructor per context")

#ifdef V8_INTL_SUPPORT
DEFINE_BOOL(icu_timezone_data, true, "get information about timezones from ICU")
#endif

#ifdef V8_ENABLE_DOUBLE_CONST_STORE_CHECK
#define V8_ENABLE_DOUBLE_CONST_STORE_CHECK_BOOL true
#else
#define V8_ENABLE_DOUBLE_CONST_STORE_CHECK_BOOL false
#endif

#ifdef V8_ENABLE_LAZY_SOURCE_POSITIONS
#define V8_LAZY_SOURCE_POSITIONS_BOOL true
#else
#define V8_LAZY_SOURCE_POSITIONS_BOOL false
#endif

#ifdef V8_SHARED_RO_HEAP
#define V8_SHARED_RO_HEAP_BOOL true
#else
#define V8_SHARED_RO_HEAP_BOOL false
#endif

DEFINE_BOOL(stress_snapshot, false,
            "disables sharing of the read-only heap for testing")
// Incremental marking is incompatible with the stress_snapshot mode;
// specifically, serialization may clear bytecode arrays from shared function
// infos which the MarkCompactCollector (running concurrently) may still need.
// See also https://crbug.com/v8/10882.
//
// Note: This is not an issue in production because we don't clear SFI's
// there (that only happens in mksnapshot and in --stress-snapshot mode).
DEFINE_NEG_IMPLICATION(stress_snapshot, incremental_marking)

#ifdef V8_LITE_MODE
#define V8_LITE_MODE_BOOL true
#else
#define V8_LITE_MODE_BOOL false
#endif

DEFINE_BOOL(lite_mode, V8_LITE_MODE_BOOL,
            "enables trade-off of performance for memory savings")

// Lite mode implies other flags to trade-off performance for memory.
DEFINE_IMPLICATION(lite_mode, jitless)
DEFINE_IMPLICATION(lite_mode, optimize_for_size)

#ifdef V8_ENABLE_THIRD_PARTY_HEAP
#define V8_ENABLE_THIRD_PARTY_HEAP_BOOL true
#else
#define V8_ENABLE_THIRD_PARTY_HEAP_BOOL false
#endif

DEFINE_NEG_IMPLICATION(enable_third_party_heap, inline_new)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, allocation_site_pretenuring)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, turbo_allocation_folding)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, concurrent_recompilation)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, script_streaming)
DEFINE_NEG_IMPLICATION(enable_third_party_heap,
                       parallel_compile_tasks_for_eager_toplevel)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, use_marking_progress_bar)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, move_object_start)
DEFINE_NEG_IMPLICATION(enable_third_party_heap, concurrent_marking)

DEFINE_BOOL_READONLY(enable_third_party_heap, V8_ENABLE_THIRD_PARTY_HEAP_BOOL,
                     "Use third-party heap")

#ifdef V8_ALLOCATION_FOLDING
#define V8_ALLOCATION_FOLDING_BOOL true
#else
#define V8_ALLOCATION_FOLDING_BOOL false
#endif

DEFINE_BOOL_READONLY(enable_allocation_folding, V8_ALLOCATION_FOLDING_BOOL,
                     "Use allocation folding globally")
DEFINE_NEG_NEG_IMPLICATION(enable_allocation_folding, turbo_allocation_folding)

#ifdef V8_DISABLE_WRITE_BARRIERS
#define V8_DISABLE_WRITE_BARRIERS_BOOL true
#else
#define V8_DISABLE_WRITE_BARRIERS_BOOL false
#endif

DEFINE_BOOL_READONLY(disable_write_barriers, V8_DISABLE_WRITE_BARRIERS_BOOL,
                     "disable write barriers when GC is non-incremental "
                     "and heap contains single generation.")

// Disable incremental marking barriers
DEFINE_NEG_IMPLICATION(disable_write_barriers, incremental_marking)
DEFINE_NEG_IMPLICATION(disable_write_barriers, concurrent_marking)
DEFINE_NEG_IMPLICATION(disable_write_barriers, cppheap_incremental_marking)
DEFINE_NEG_IMPLICATION(disable_write_barriers, cppheap_concurrent_marking)

#ifdef V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS
#define V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS_BOOL true
#else
#define V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS_BOOL false
#endif

DEFINE_BOOL_READONLY(enable_unconditional_write_barriers,
                     V8_ENABLE_UNCONDITIONAL_WRITE_BARRIERS_BOOL,
                     "always use full write barriers")

#ifdef V8_ENABLE_SINGLE_GENERATION
#define V8_SINGLE_GENERATION_BOOL true
#else
#define V8_SINGLE_GENERATION_BOOL false
#endif

DEFINE_BOOL_READONLY(
    single_generation, V8_SINGLE_GENERATION_BOOL,
    "allocate all objects from young generation to old generation")

#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
#define V8_ENABLE_CONSERVATIVE_STACK_SCANNING_BOOL true
#else
#define V8_ENABLE_CONSERVATIVE_STACK_SCANNING_BOOL false
#endif
DEFINE_BOOL_READONLY(conservative_stack_scanning,
                     V8_ENABLE_CONSERVATIVE_STACK_SCANNING_BOOL,
                     "use conservative stack scanning")
DEFINE_IMPLICATION(conservative_stack_scanning, minor_ms)
DEFINE_NEG_IMPLICATION(conservative_stack_scanning, compact_with_stack)

#ifdef V8_ENABLE_DIRECT_HANDLE
#define V8_ENABLE_DIRECT_HANDLE_BOOL true
#else
#define V8_ENABLE_DIRECT_HANDLE_BOOL false
#endif
DEFINE_BOOL_READONLY(direct_handle, V8_ENABLE_DIRECT_HANDLE_BOOL,
                     "use direct handles with conservative stack scanning")
// Do not use direct handles without conservative stack scanning, as this would
// break the correctness of the GC.
DEFINE_NEG_NEG_IMPLICATION(conservative_stack_scanning, direct_handle)

#ifdef V8_ENABLE_LOCAL_OFF_STACK_CHECK
#define V8_ENABLE_LOCAL_OFF_STACK_CHECK_BOOL true
#else
#define V8_ENABLE_LOCAL_OFF_STACK_CHECK_BOOL false
#endif
DEFINE_BOOL_READONLY(local_off_stack_check,
                     V8_ENABLE_LOCAL_OFF_STACK_CHECK_BOOL,
                     "check for off-stack allocation of v8::Local")

#ifdef V8_ENABLE_FUTURE
#define FUTURE_BOOL true
#else
#define FUTURE_BOOL false
#endif
DEFINE_BOOL(future, FUTURE_BOOL,
            "Implies all staged features that we want to ship in the "
            "not-too-far future")

DEFINE_BOOL(force_emit_interrupt_budget_checks, false,
            "force emit tier-up logic from all non-turbofan code, even if it "
            "is the top enabled tier")
#ifdef V8_ENABLE_MAGLEV
DEFINE_BOOL(maglev, true, "enable the maglev optimizing compiler")
#if !ENABLE_MAGLEV
// Enable Maglev on Future for platforms in which it's not enabled by default
// (eg, Android).
DEFINE_WEAK_IMPLICATION(future, maglev)
#endif
DEFINE_EXPERIMENTAL_FEATURE(
    maglev_future,
    "enable maglev features that we want to ship in the not-too-far future")
DEFINE_IMPLICATION(maglev_future, maglev)
DEFINE_BOOL(
    optimize_on_next_call_optimizes_to_maglev, false,
    "make OptimizeFunctionOnNextCall optimize to maglev instead of turbofan")

// We stress maglev by setting a very low interrupt budget for maglev. This
// way, we still gather *some* feedback before compiling optimized code.
DEFINE_BOOL(stress_maglev, false, "trigger maglev compilation earlier")
DEFINE_IMPLICATION(stress_maglev, maglev)
DEFINE_WEAK_VALUE_IMPLICATION(stress_maglev, invocation_count_for_maglev, 4)

#else
DEFINE_BOOL_READONLY(maglev, false, "enable the maglev optimizing compiler")
DEFINE_BOOL_READONLY(
    maglev_future, false,
    "enable maglev features that we want to ship in the not-too-far future")
DEFINE_BOOL_READONLY(stress_maglev, false, "trigger maglev compilation earlier")
DEFINE_BOOL_READONLY(
    optimize_on_next_call_optimizes_to_maglev, false,
    "make OptimizeFunctionOnNextCall optimize to maglev instead of turbofan")
#endif  //  V8_ENABLE_MAGLEV

DEFINE_BOOL(maglev_inlining, true,
            "enable inlining in the maglev optimizing compiler")
DEFINE_BOOL(maglev_loop_peeling, true,
            "enable loop peeling in the maglev optimizing compiler")
DEFINE_BOOL(maglev_optimistic_peeled_loops, false,
            "enable speculation on loop state using peeling as fallback in the "
            "maglev optimizing compiler")
DEFINE_INT(maglev_loop_peeling_max_size, 200,
           "max loop size for loop peeling in the maglev optimizing compiler")
DEFINE_BOOL(maglev_deopt_data_on_background, true,
            "Generate deopt data on background thread")
DEFINE_BOOL(maglev_build_code_on_background, true,
            "Generate code on background thread")
DEFINE_WEAK_IMPLICATION(maglev_build_code_on_background,
                        maglev_deopt_data_on_background)
DEFINE_BOOL(maglev_destroy_on_background, true,
            "Destroy compilation jobs on background thread")
DEFINE_BOOL(maglev_inline_api_calls, false,
            "Inline CallApiCallback builtin into generated code")
DEFINE_EXPERIMENTAL_FEATURE(maglev_licm, "loop invariant code motion")
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_optimistic_peeled_loops)
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_speculative_hoist_phi_untagging)
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_inline_api_calls)
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_escape_analysis)
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_licm)
// This might be too big of a hammer but we must prohibit moving the C++
// trampolines while we are executing a C++ code.
DEFINE_NEG_IMPLICATION(maglev_inline_api_calls, compact_code_space_with_stack)

DEFINE_UINT(
    concurrent_maglev_max_threads, 2,
    "max number of threads that concurrent Maglev can use (0 for unbounded)")
DEFINE_BOOL(concurrent_maglev_high_priority_threads, false,
            "use high priority compiler threads for concurrent Maglev")

DEFINE_INT(
    max_maglev_inline_depth, 1,
    "max depth of functions that Maglev will inline excl. small functions")
DEFINE_INT(
    max_maglev_hard_inline_depth, 10,
    "max depth of functions that Maglev will inline incl. small functions")
DEFINE_INT(max_maglev_inlined_bytecode_size, 460,
           "maximum size of bytecode for a single inlining")
DEFINE_INT(max_maglev_inlined_bytecode_size_cumulative, 920,
           "maximum cumulative size of bytecode considered for inlining excl. "
           "small functions")
DEFINE_INT(max_maglev_inlined_bytecode_size_small, 27,
           "maximum size of bytecode considered for small function inlining")
DEFINE_FLOAT(min_maglev_inlining_frequency, 0.10,
             "minimum frequency for inlining")
DEFINE_WEAK_VALUE_IMPLICATION(turbofan, max_maglev_inline_depth, 1)
DEFINE_WEAK_VALUE_IMPLICATION(turbofan, max_maglev_inlined_bytecode_size, 100)
DEFINE_WEAK_VALUE_IMPLICATION(turbofan,
                              max_maglev_inlined_bytecode_size_cumulative, 920)
DEFINE_WEAK_VALUE_IMPLICATION(turbofan, min_maglev_inlining_frequency, 0.95)
DEFINE_BOOL(maglev_reuse_stack_slots, true,
            "reuse stack slots in the maglev optimizing compiler")
DEFINE_BOOL(maglev_untagged_phis, true,
            "enable phi untagging in the maglev optimizing compiler")
DEFINE_BOOL(maglev_hoist_osr_value_phi_untagging, true,
            "enable phi untagging to hoist untagging of osr values")
DEFINE_EXPERIMENTAL_FEATURE(
    maglev_speculative_hoist_phi_untagging,
    "enable phi untagging to hoist untagging of loop phi inputs (could "
    "still cause deopt loops)")
DEFINE_BOOL(maglev_cse, true, "common subexpression elimination")

DEFINE_STRING(maglev_filter, "*", "optimization filter for the maglev compiler")
DEFINE_BOOL(maglev_assert, false, "insert extra assertion in maglev code")
DEFINE_DEBUG_BOOL(maglev_assert_stack_size, true,
                  "insert stack size checks before every IR node")
DEFINE_BOOL(maglev_break_on_entry, false, "insert an int3 on maglev entries")
DEFINE_BOOL(maglev_print_feedback, true,
            "print feedback vector for maglev compiled code")
DEFINE_BOOL(maglev_print_inlined, true,
            "print bytecode / feedback vectors also for inlined code")

DEFINE_BOOL(print_maglev_code, false, "print maglev code")
DEFINE_BOOL(trace_maglev_graph_building, false, "trace maglev graph building")
DEFINE_BOOL(trace_maglev_loop_speeling, false, "trace maglev loop SPeeling")
DEFINE_WEAK_IMPLICATION(trace_maglev_graph_building, trace_maglev_loop_speeling)
DEFINE_BOOL(trace_maglev_inlining, false, "trace maglev inlining")
DEFINE_BOOL(trace_maglev_inlining_verbose, false,
            "trace maglev inlining (verbose)")
DEFINE_IMPLICATION(trace_maglev_inlining_verbose, trace_maglev_inlining)

#ifdef V8_ENABLE_MAGLEV_GRAPH_PRINTER
DEFINE_BOOL(print_maglev_deopt_verbose, false, "print verbose deopt info")
DEFINE_WEAK_IMPLICATION(trace_deopt_verbose, print_maglev_deopt_verbose)
DEFINE_BOOL(print_maglev_graph, false, "print the final maglev graph")
DEFINE_BOOL(print_maglev_graphs, false, "print maglev graph across all phases")
DEFINE_BOOL(trace_maglev_phi_untagging, false, "trace maglev phi untagging")
DEFINE_BOOL(trace_maglev_regalloc, false, "trace maglev register allocation")
#else
DEFINE_BOOL_READONLY(print_maglev_deopt_verbose, false,
                     "print verbose deopt info")
DEFINE_BOOL_READONLY(print_maglev_graph, false, "print the final maglev graph")
DEFINE_BOOL_READONLY(print_maglev_graphs, false,
                     "print maglev graph across all phases")
DEFINE_BOOL_READONLY(trace_maglev_phi_untagging, false,
                     "trace maglev phi untagging")
DEFINE_BOOL_READONLY(trace_maglev_regalloc, false,
                     "trace maglev register allocation")
#endif  // V8_ENABLE_MAGLEV_GRAPH_PRINTER

DEFINE_BOOL(maglev_stats, false, "print Maglev statistics")
DEFINE_BOOL(maglev_stats_nvp, false,
            "print Maglev statistics in machine-readable format")

// TODO(v8:7700): Remove once stable.
DEFINE_BOOL(maglev_function_context_specialization, true,
            "enable function context specialization in maglev")

DEFINE_BOOL(maglev_skip_migration_check_for_polymorphic_access, false,
            "skip generating a migration check when some maps of polymorpic "
            "property access are migration targets")

#ifdef V8_ENABLE_SPARKPLUG
DEFINE_WEAK_IMPLICATION(future, flush_baseline_code)
#endif

DEFINE_BOOL(
    enable_enumerated_keyed_access_bytecode, true,
    "enable generating GetEnumeratedKeyedProperty bytecode for keyed access")

DEFINE_BOOL_READONLY(dict_property_const_tracking,
                     V8_DICT_PROPERTY_CONST_TRACKING_BOOL,
                     "Use const tracking on dictionary properties")

DEFINE_BOOL(const_tracking_let, true,
            "Use const tracking on top-level `let` variables")

DEFINE_UINT(max_opt, 999,
            "Set the maximal optimisation tier: "
            "> 3 == any, 0 == ignition/interpreter, 1 == sparkplug/baseline, "
            "2 == maglev, 3 == turbofan")

#ifdef V8_ENABLE_TURBOFAN
DEFINE_WEAK_VALUE_IMPLICATION(max_opt < 3, turbofan, false)
#endif  // V8_ENABLE_TURBOFAN
#ifdef V8_ENABLE_MAGLEV
DEFINE_WEAK_VALUE_IMPLICATION(max_opt < 2, maglev, false)
#endif  // V8_ENABLE_MAGLEV
#ifdef V8_ENABLE_SPARKPLUG
DEFINE_WEAK_VALUE_IMPLICATION(max_opt < 1, sparkplug, false)
#endif  // V8_ENABLE_SPARKPLUG

// Flags to override efficiency and battery saver mode settings for debugging
// and testing.
DEFINE_MAYBE_BOOL(efficiency_mode,
                  "Forces efficiency mode on or off, disregarding any dynamic "
                  "signals. Efficiency mode is optimized for situations with "
                  "no latency requirements and uses fewer threads.")
DEFINE_MAYBE_BOOL(
    battery_saver_mode,
    "Forces battery saver mode on or off, disregarding any dynamic signals. "
    "Battery saver tries to conserve overal cpu cycles spent.")

// Flags to experiment with the new efficiency mode
DEFINE_BOOL(efficiency_mode_for_tiering_heuristics, true,
            "Use efficiency mode in tiering heuristics.")
DEFINE_BOOL(efficiency_mode_disable_turbofan, false,
            "Defer tier-up to turbofan while in efficiency mode.")
DEFINE_INT(efficiency_mode_delay_turbofan, 15000,
           "Delay tier-up to turbofan to a certain invocation count while in "
           "efficiency mode.")

// Flag to select wasm trace mark type
DEFINE_STRING(
    wasm_trace_native, nullptr,
    "Select which native code sequence to use for wasm trace instruction: "
    "default or cpuid")

#ifdef V8_JITLESS
#define V8_JITLESS_BOOL true
DEFINE_BOOL_READONLY(jitless, true,
                     "Disable runtime allocation of executable memory.")
#else
#define V8_JITLESS_BOOL false
DEFINE_BOOL(jitless, V8_LITE_MODE_BOOL,
            "Disable runtime allocation of executable memory.")
#endif  // V8_JITLESS

// Jitless V8 has a few implications:
// Field type tracking is only used by TurboFan.
DEFINE_NEG_IMPLICATION(jitless, track_field_types)
// No code generation at runtime.
DEFINE_IMPLICATION(jitless, regexp_interpret_all)
DEFINE_NEG_IMPLICATION(jitless, turbofan)
#ifdef V8_ENABLE_SPARKPLUG
DEFINE_NEG_IMPLICATION(jitless, sparkplug)
DEFINE_NEG_IMPLICATION(jitless, always_sparkplug)
#endif  // V8_ENABLE_SPARKPLUG
#ifdef V8_ENABLE_MAGLEV
DEFINE_NEG_IMPLICATION(jitless, maglev)
#endif  // V8_ENABLE_MAGLEV
// Doesn't work without an executable code space.
DEFINE_NEG_IMPLICATION(jitless, interpreted_frames_native_stack)

DEFINE_BOOL(
    disable_optimizing_compilers, false,
    "Disable all optimizing compilers while leaving baseline compilers enabled")
// Disable all optimizing JavaScript compilers.
// JavaScript code can execute either in Ignition or Sparkplug.
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, turbofan)
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, turboshaft)
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, maglev)
#if V8_ENABLE_WEBASSEMBLY
// Disable optimizing Wasm compilers. Wasm code must execute with Liftoff.
DEFINE_IMPLICATION(disable_optimizing_compilers, liftoff)
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, wasm_tier_up)
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, wasm_dynamic_tiering)
// Disable translation of asm.js to Wasm
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, validate_asm)
#endif  // V8_ENABLE_WEBASSEMBLY
// Field type tracking is only used by TurboFan, so can be disabled.
DEFINE_NEG_IMPLICATION(disable_optimizing_compilers, track_field_types)

DEFINE_BOOL(memory_protection_keys, true,
            "protect code memory with PKU if available")

DEFINE_BOOL(assert_types, false,
            "generate runtime type assertions to test the typer")
// TODO(tebbi): Support allocating types from background thread.
DEFINE_NEG_IMPLICATION(assert_types, concurrent_recompilation)
DEFINE_EXPERIMENTAL_FEATURE(
    turboshaft_assert_types,
    "generate runtime type assertions to test the turboshaft type system")
DEFINE_NEG_IMPLICATION(turboshaft_assert_types, concurrent_recompilation)

// Enable verification of SimplifiedLowering in debug builds.
DEFINE_BOOL(verify_simplified_lowering, DEBUG_BOOL,
            "verify graph generated by simplified lowering")

DEFINE_BOOL(trace_compilation_dependencies, false, "trace code dependencies")
// Depend on --trace-deopt-verbose for reporting dependency invalidations.
DEFINE_IMPLICATION(trace_compilation_dependencies, trace_deopt_verbose)

#ifdef V8_ALLOCATION_SITE_TRACKING
#define V8_ALLOCATION_SITE_TRACKING_BOOL true
#else
#define V8_ALLOCATION_SITE_TRACKING_BOOL false
#endif

DEFINE_BOOL_READONLY(allocation_site_tracking, V8_ALLOCATION_SITE_TRACKING_BOOL,
                     "Enable allocation site tracking")
DEFINE_NEG_NEG_IMPLICATION(allocation_site_tracking,
                           allocation_site_pretenuring)

// Flags for experimental implementation features.
DEFINE_BOOL(allocation_site_pretenuring, true,
            "pretenure with allocation sites")
DEFINE_BOOL(page_promotion, true, "promote pages based on utilization")
DEFINE_INT(page_promotion_threshold, 70,
           "min percentage of live bytes on a page to enable fast evacuation "
           "in full GCs")
DEFINE_INT(minor_ms_page_promotion_threshold, 50,
           "min percentage of live bytes on a page to enable fast evacuation "
           "in MinorMS")
DEFINE_INT(minor_ms_page_promotion_max_lab_threshold, 30,
           "max percentage of labs out of a page to still be considered for "
           "page promotion")
DEFINE_UINT(minor_ms_max_page_age, 4,
            "max age for a page after which it is force promoted to old space")
DEFINE_UINT(minor_ms_max_new_space_capacity_mb, 72,
            "max new space capacity in MBs when using MinorMS. When pointer "
            "compression is disabled, twice the capacity is used.")
DEFINE_UINT(scavenger_max_new_space_capacity_mb, 8,
            "max new space capacity in MBs when using Scavenger. When pointer "
            "compression is disabled, twice the capacity is used.")
DEFINE_BOOL(trace_page_promotions, false, "trace page promotion decisions")
DEFINE_BOOL(trace_pretenuring, false,
            "trace pretenuring decisions of HAllocate instructions")
DEFINE_BOOL(trace_pretenuring_statistics, false,
            "trace allocation site pretenuring statistics")
DEFINE_BOOL(track_field_types, true, "track field types")
DEFINE_BOOL(trace_block_coverage, false,
            "trace collected block coverage information")
DEFINE_BOOL(trace_protector_invalidation, false,
            "trace protector cell invalidations")
DEFINE_BOOL(decommit_pooled_pages, false,
            "decommit, rather than discard pooled pages")

#ifdef V8_MINORMS_STRING_SHORTCUTTING
DEFINE_BOOL(minor_ms_shortcut_strings, false,
            "short cut strings during marking")
// String shortcutting currently doesn't work with concurrent marking.
DEFINE_NEG_IMPLICATION(minor_ms_shortcut_strings, concurrent_minor_ms_marking)
#else
DEFINE_BOOL_READONLY(minor_ms_shortcut_strings, false,
                     "short cut strings during marking")
#endif

DEFINE_EXPERIMENTAL_FEATURE(feedback_normalization,
                            "feed back normalization to constructors")
// TODO(jkummerow): This currently adds too much load on the stub cache.
DEFINE_BOOL_READONLY(internalize_on_the_fly, true,
                     "internalize string keys for generic keyed ICs on the fly")

// Flag for sealed, frozen elements kind instead of dictionary elements kind
DEFINE_BOOL_READONLY(enable_sealed_frozen_elements_kind, true,
                     "Enable sealed, frozen elements kind")

// Flags for data representation optimizations
DEFINE_BOOL(unbox_double_arrays, true, "automatically unbox arrays of doubles")
DEFINE_BOOL_READONLY(string_slices, true, "use string slices")

// Tiering: Sparkplug / feedback vector allocation.
DEFINE_INT(invocation_count_for_feedback_allocation, 8,
           "invocation count required for allocating feedback vectors")

// Tiering: Maglev.
#if defined(ANDROID)
DEFINE_INT(invocation_count_for_maglev, 1000,
           "invocation count required for optimizing with Maglev")
#else
DEFINE_INT(invocation_count_for_maglev, 400,
           "invocation count required for optimizing with Maglev")
#endif  // ANDROID
DEFINE_INT(invocation_count_for_maglev_osr, 100,
           "invocation count required for maglev OSR")
DEFINE_BOOL(osr_from_maglev, false,
            "whether we try to OSR to Turbofan from OSR'd Maglev")
DEFINE_FLOAT(
    osr_from_maglev_interrupt_scale_factor, 0.8,
    "Scale interrupt budget reduction for OSR from Maglev vs. OSR to Maglev")
DEFINE_BOOL(always_osr_from_maglev, false,
            "whether we try to OSR to Turbofan from any Maglev")
DEFINE_VALUE_IMPLICATION(always_osr_from_maglev, osr_from_maglev, true)

// Tiering: Turbofan.
DEFINE_INT(invocation_count_for_turbofan, 3000,
           "invocation count required for optimizing with TurboFan")
DEFINE_INT(invocation_count_for_osr, 500, "invocation count required for OSR")
DEFINE_INT(osr_to_tierup, 1,
           "number to decrease the invocation budget by when we follow OSR")
DEFINE_INT(minimum_invocations_after_ic_update, 500,
           "How long to minimally wait after IC update before tier up")
DEFINE_INT(minimum_invocations_before_optimization, 2,
           "Minimum number of invocations we need before non-OSR optimization")

// Tiering: JIT fuzzing.
//
// When --jit-fuzzing is enabled, various tiering related thresholds are
// lowered so that the different JIT tiers are reached roughly within a few
// dozen executions of the code instead of a few hundred or thousand. As a rule
// of thumb, aiming for things to happen 5x to 10x sooner in this mode than
// they otherwise would is probably not unreasonable.
DEFINE_BOOL(jit_fuzzing, false,
            "Set JIT tiering thresholds suitable for JIT fuzzing")
// Tier up to Sparkplug should happen after a handful o executions in ignition.
DEFINE_NEG_IMPLICATION(jit_fuzzing, lazy_feedback_allocation)
DEFINE_NEG_IMPLICATION(jit_fuzzing, baseline_batch_compilation)
// Tier up to Maglev should happen soon afterwards.
DEFINE_VALUE_IMPLICATION(jit_fuzzing, invocation_count_for_maglev, 10)
// And tier up to Turbofan should happen after a couple dozen or so executions.
DEFINE_VALUE_IMPLICATION(jit_fuzzing, invocation_count_for_turbofan, 20)
// Additionally, some other JIT-related thresholds should also be lowered.
DEFINE_VALUE_IMPLICATION(jit_fuzzing, invocation_count_for_osr, 5)
DEFINE_VALUE_IMPLICATION(jit_fuzzing, invocation_count_for_maglev_osr, 1)
DEFINE_VALUE_IMPLICATION(jit_fuzzing, minimum_invocations_after_ic_update, 5)

#if V8_ENABLE_WEBASSEMBLY
// Wasm tiering thresholds.
DEFINE_VALUE_IMPLICATION(jit_fuzzing, wasm_wrapper_tiering_budget, 1)
DEFINE_VALUE_IMPLICATION(jit_fuzzing, wasm_tiering_budget, 1)
DEFINE_IMPLICATION(jit_fuzzing, wasm_inlining_ignore_call_counts)
#endif  // V8_ENABLE_WEBASSEMBLY

// Flags for inline caching and feedback vectors.
DEFINE_BOOL(use_ic, true, "use inline caching")
DEFINE_BOOL(lazy_feedback_allocation, true, "Allocate feedback vectors lazily")
DEFINE_BOOL(stress_ic, false, "exercise interesting paths in ICs more often")

// Flags for Ignition.
DEFINE_BOOL(ignition_elide_noneffectful_bytecodes, true,
            "elide bytecodes which won't have any external effect")
DEFINE_BOOL(ignition_reo, true, "use ignition register equivalence optimizer")
DEFINE_BOOL(ignition_filter_expression_positions, true,
            "filter expression positions before the bytecode pipeline")
DEFINE_BOOL(ignition_share_named_property_feedback, true,
            "share feedback slots when loading the same named property from "
            "the same object")
DEFINE_BOOL(ignition_elide_redundant_tdz_checks, true,
            "elide TDZ checks dominated by other TDZ checks")
DEFINE_BOOL(print_bytecode, false,
            "print bytecode generated by ignition interpreter")
DEFINE_BOOL(enable_lazy_source_positions, V8_LAZY_SOURCE_POSITIONS_BOOL,
            "skip generating source positions during initial compile but "
            "regenerate when actually required")
DEFINE_BOOL(stress_lazy_source_positions, false,
            "collect lazy source positions immediately after lazy compile")
DEFINE_STRING(print_bytecode_filter, "*",
              "filter for selecting which functions to print bytecode")
DEFINE_BOOL(omit_default_ctors, true, "omit calling default ctors in bytecode")
#ifdef V8_TRACE_UNOPTIMIZED
DEFINE_BOOL(trace_unoptimized, false,
            "trace the bytecodes executed by all unoptimized execution")
DEFINE_BOOL(trace_ignition, false,
            "trace the bytecodes executed by the ignition interpreter")
DEFINE_BOOL(trace_baseline_exec, false,
            "trace the bytecodes executed by the baseline code")
DEFINE_WEAK_IMPLICATION(trace_unoptimized, trace_ignition)
DEFINE_WEAK_IMPLICATION(trace_unoptimized, trace_baseline_exec)
#endif
#ifdef V8_TRACE_FEEDBACK_UPDATES
DEFINE_BOOL(
    trace_feedback_updates, false,
    "trace updates to feedback vectors during ignition interpreter execution.")
#endif
DEFINE_BOOL(trace_ignition_codegen, false,
            "trace the codegen of ignition interpreter bytecode handlers")
DEFINE_STRING(
    trace_ignition_dispatches_output_file, nullptr,
    "write the bytecode handler dispatch table to the specified file (d8 only) "
    "(requires building with v8_enable_ignition_dispatch_counting)")

DEFINE_BOOL(trace_track_allocation_sites, false,
            "trace the tracking of allocation sites")
DEFINE_BOOL(trace_migration, false, "trace object migration")
DEFINE_BOOL(trace_generalization, false, "trace map generalization")

DEFINE_BOOL(reuse_scope_infos, false,
            "reuse scope infos from previous compiles")

DEFINE_IMPLICATION(fuzzing, reuse_scope_infos)

// Flags for Sparkplug
#undef FLAG
#if V8_ENABLE_SPARKPLUG
#define FLAG FLAG_FULL
#else
#define FLAG FLAG_READONLY
#endif
DEFINE_BOOL(sparkplug, ENABLE_SPARKPLUG_BY_DEFAULT,
            "enable Sparkplug baseline compiler")
DEFINE_BOOL(always_sparkplug, false, "directly tier up to Sparkplug code")
#if V8_ENABLE_SPARKPLUG
DEFINE_IMPLICATION(always_sparkplug, sparkplug)
DEFINE_BOOL(baseline_batch_compilation, true, "batch compile Sparkplug code")
#if defined(V8_OS_DARWIN) && defined(V8_HOST_ARCH_ARM64) && \
    !V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT &&               \
    !V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT
// M1 requires W^X.
DEFINE_BOOL_READONLY(concurrent_sparkplug, false,
                     "compile Sparkplug code in a background thread")
#else
DEFINE_BOOL(concurrent_sparkplug, false,
            "compile Sparkplug code in a background thread")
DEFINE_WEAK_IMPLICATION(future, concurrent_sparkplug)
DEFINE_NEG_IMPLICATION(predictable, concurrent_sparkplug)
DEFINE_NEG_IMPLICATION(single_threaded, concurrent_sparkplug)
DEFINE_NEG_IMPLICATION(jitless, concurrent_sparkplug)
#endif
DEFINE_UINT(
    concurrent_sparkplug_max_threads, 1,
    "max number of threads that concurrent Sparkplug can use (0 for unbounded)")
DEFINE_BOOL(concurrent_sparkplug_high_priority_threads, false,
            "use high priority compiler threads for concurrent Sparkplug")
#else
DEFINE_BOOL(baseline_batch_compilation, false, "batch compile Sparkplug code")
DEFINE_BOOL_READONLY(concurrent_sparkplug, false,
                     "compile Sparkplug code in a background thread")
#endif
DEFINE_STRING(sparkplug_filter, "*", "filter for Sparkplug baseline compiler")
DEFINE_BOOL(sparkplug_needs_short_builtins, false,
            "only enable Sparkplug baseline compiler when "
            "--short-builtin-calls are also enabled")
DEFINE_INT(baseline_batch_compilation_threshold, 4 * KB,
           "the estimated instruction size of a batch to trigger compilation")
DEFINE_BOOL(trace_baseline, false, "trace baseline compilation")
DEFINE_BOOL(trace_baseline_batch_compilation, false,
            "trace baseline batch compilation")
DEFINE_WEAK_IMPLICATION(trace_baseline, trace_baseline_batch_compilation)
#undef FLAG
#define FLAG FLAG_FULL

// Internalize into a shared string table in the shared isolate
DEFINE_BOOL(shared_string_table, false, "internalize strings into shared table")
DEFINE_IMPLICATION(harmony_struct, shared_string_table)
DEFINE_EXPERIMENTAL_FEATURE(
    always_use_string_forwarding_table,
    "use string forwarding table instead of thin strings for all strings")
// With --always-use-string-forwarding-table, we can have young generation
// string entries in the forwarding table, requiring table updates when these
// strings get promoted to old space. Parallel GCs in client isolates
// (enabled by --shared-string-table) are not supported using a single shared
// forwarding table.
DEFINE_NEG_IMPLICATION(shared_string_table, always_use_string_forwarding_table)

DEFINE_BOOL(transition_strings_during_gc_with_stack, false,
            "Transition strings during a full GC with stack")

DEFINE_SIZE_T(initial_shared_heap_size, 0,
              "initial size of the shared heap (in Mbytes); "
              "other heap size flags (e.g. initial_heap_size) take precedence")
DEFINE_SIZE_T(
    max_shared_heap_size, 0,
    "max size of the shared heap (in Mbytes); "
    "other heap size flags (e.g. max_shared_heap_size) take precedence")

// Flags for concurrent recompilation.
DEFINE_BOOL(concurrent_recompilation, true,
            "optimizing hot functions asynchronously on a separate thread")
DEFINE_BOOL(trace_concurrent_recompilation, false,
            "track concurrent recompilation")
DEFINE_INT(concurrent_recompilation_queue_length, 8,
           "the length of the concurrent compilation queue")
DEFINE_INT(concurrent_recompilation_delay, 0,
           "artificial compilation delay in ms")
DEFINE_BOOL(concurrent_recompilation_front_running, true,
            "move compile jobs to the front if recompilation is requested "
            "multiple times")
DEFINE_UINT(
    concurrent_turbofan_max_threads, 4,
    "max number of threads that concurrent Turbofan can use (0 for unbounded)")
DEFINE_BOOL(
    stress_concurrent_inlining, false,
    "create additional concurrent optimization jobs but throw away result")
DEFINE_IMPLICATION(stress_concurrent_inlining, concurrent_recompilation)
DEFINE_IMPLICATION(stress_concurrent_inlining, turbofan)
DEFINE_NEG_IMPLICATION(stress_concurrent_inlining, lazy_feedback_allocation)
DEFINE_WEAK_VALUE_IMPLICATION(stress_concurrent_inlining,
                              invocation_count_for_turbofan, 150)
DEFINE_BOOL(maglev_overwrite_budget, false,
            "whether maglev resets the interrupt budget")
DEFINE_WEAK_IMPLICATION(maglev, maglev_overwrite_budget)
DEFINE_NEG_IMPLICATION(stress_concurrent_inlining, maglev_overwrite_budget)
DEFINE_WEAK_VALUE_IMPLICATION(maglev_overwrite_budget,
                              invocation_count_for_turbofan, 6000)
DEFINE_BOOL(maglev_overwrite_osr_budget, false,
            "whether maglev resets the OSR interrupt budget")
DEFINE_WEAK_IMPLICATION(maglev_osr, maglev_overwrite_osr_budget)
DEFINE_NEG_IMPLICATION(stress_concurrent_inlining, maglev_overwrite_osr_budget)
DEFINE_WEAK_VALUE_IMPLICATION(maglev_overwrite_osr_budget,
                              invocation_count_for_osr, 800)
DEFINE_BOOL(stress_concurrent_inlining_attach_code, false,
            "create additional concurrent optimization jobs")
DEFINE_IMPLICATION(stress_concurrent_inlining_attach_code,
                   stress_concurrent_inlining)
DEFINE_INT(max_serializer_nesting, 25,
           "maximum levels for nesting child serializers")
DEFINE_BOOL(trace_heap_broker_verbose, false,
            "trace the heap broker verbosely (all reports)")
DEFINE_BOOL(trace_heap_broker_memory, false,
            "trace the heap broker memory (refs analysis and zone numbers)")
DEFINE_BOOL(trace_heap_broker, false,
            "trace the heap broker (reports on missing data only)")
DEFINE_IMPLICATION(trace_heap_broker_verbose, trace_heap_broker)
DEFINE_IMPLICATION(trace_heap_broker_memory, trace_heap_broker)
DEFINE_IMPLICATION(trace_heap_broker, trace_pending_allocations)

// Flags for stress-testing the compiler.
DEFINE_INT(stress_runs, 0, "number of stress runs")
DEFINE_INT(deopt_every_n_times, 0,
           "deoptimize every n times a deopt point is passed")
DEFINE_BOOL(print_deopt_stress, false, "print number of possible deopt points")

// Flags for TurboFan.
#ifdef V8_ENABLE_TURBOFAN
#define V8_ENABLE_TURBOFAN_BOOL true
DEFINE_BOOL(turbofan, true, "use the Turbofan optimizing compiler")
// TODO(leszeks): Temporary alias until we make sure all our infra is passing
// --turbofan instead of --opt.
DEFINE_ALIAS_BOOL(opt, turbofan)
#else
#define V8_ENABLE_TURBOFAN_BOOL false
DEFINE_BOOL_READONLY(turbofan, false, "use the Turbofan optimizing compiler")
DEFINE_BOOL_READONLY(opt, false, "use the Turbofan optimizing compiler")
#endif  // V8_ENABLE_TURBOFAN

DEFINE_BOOL(
    stress_turbo_late_spilling, false,
    "optimize placement of all spill instructions, not just loop-top phis")

DEFINE_BOOL(turbo_wasm_address_reassociation, true,
            "refactor address components for immediate indexing")

DEFINE_STRING(turbo_filter, "*", "optimization filter for TurboFan compiler")
DEFINE_BOOL(trace_turbo, false, "trace generated TurboFan IR")
DEFINE_STRING(trace_turbo_path, nullptr,
              "directory to dump generated TurboFan IR to")
DEFINE_STRING(trace_turbo_filter, "*",
              "filter for tracing turbofan compilation")
DEFINE_BOOL(trace_turbo_graph, false, "trace generated TurboFan graphs")
DEFINE_BOOL(trace_turbo_scheduled, false, "trace TurboFan IR with schedule")
DEFINE_IMPLICATION(trace_turbo_scheduled, trace_turbo_graph)
DEFINE_STRING(trace_turbo_file_prefix, "turbo",
              "trace turbo graph to a file with given prefix")
DEFINE_STRING(trace_turbo_cfg_file, nullptr,
              "trace turbo cfg graph (for C1 visualizer) to a given file name")
DEFINE_BOOL(trace_turbo_types, true, "trace TurboFan's types")
DEFINE_BOOL(trace_turbo_scheduler, false, "trace TurboFan's scheduler")
DEFINE_BOOL(trace_turbo_reduction, false, "trace TurboFan's various reducers")
#ifdef V8_ENABLE_SLOW_TRACING
#define DEFINE_SLOW_TRACING_BOOL DEFINE_BOOL
#else
#define DEFINE_SLOW_TRACING_BOOL DEFINE_BOOL_READONLY
#endif  // V8_ENABLE_SLOW_TRACING
DEFINE_SLOW_TRACING_BOOL(trace_turbo_trimming, false,
                         "trace TurboFan's graph trimmer")
DEFINE_SLOW_TRACING_BOOL(trace_turbo_jt, false,
                         "trace TurboFan's jump threading")
DEFINE_SLOW_TRACING_BOOL(trace_turbo_ceq, false,
                         "trace TurboFan's control equivalence")
DEFINE_SLOW_TRACING_BOOL(trace_turbo_loop, false,
                         "trace TurboFan's loop optimizations")
DEFINE_SLOW_TRACING_BOOL(trace_turbo_alloc, false,
                         "trace TurboFan's register allocator")
DEFINE_SLOW_TRACING_BOOL(trace_all_uses, false, "trace all use positions")
DEFINE_SLOW_TRACING_BOOL(trace_representation, false,
                         "trace representation types")
DEFINE_BOOL(
    trace_turbo_stack_accesses, false,
    "trace stack load/store counters for optimized code in run-time (x64 only)")

// When fuzzing and concurrent compilation is enabled, disable Turbofan
// tracing flags since reading/printing heap state is not thread-safe and
// leads to false positives on TSAN bots.
// TODO(chromium:1205289): Teach relevant fuzzers to not pass TF tracing
// flags instead, and remove this section.
DEFINE_BOOL(fuzzing_and_concurrent_recompilation, true,
            "fuzzing && concurrent_recompilation")
DEFINE_NEG_NEG_IMPLICATION(fuzzing, fuzzing_and_concurrent_recompilation)
DEFINE_NEG_NEG_IMPLICATION(concurrent_recompilation,
                           fuzzing_and_concurrent_recompilation)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_graph)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_scheduled)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_reduction)
#ifdef V8_ENABLE_SLOW_TRACING
// If expensive tracing is disabled via a build flag, the following flags
// cannot be disabled (because they are already).
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_trimming)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_jt)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_ceq)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_loop)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_alloc)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_all_uses)
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_representation)
#endif
DEFINE_DISABLE_FLAG_IMPLICATION(fuzzing_and_concurrent_recompilation,
                                trace_turbo_stack_accesses)

DEFINE_BOOL(turbo_verify, DEBUG_BOOL, "verify TurboFan graphs at each phase")
DEFINE_STRING(turbo_verify_machine_graph, nullptr,
              "verify TurboFan machine graph before instruction selection")
#ifdef ENABLE_VERIFY_CSA
DEFINE_BOOL(verify_csa, DEBUG_BOOL,
            "verify TurboFan machine graph of code stubs")
#else
// Define the flag as read-only-false so that code still compiles even in the
// non-ENABLE_VERIFY_CSA configuration.
DEFINE_BOOL_READONLY(verify_csa, false,
                     "verify TurboFan machine graph of code stubs")
#endif  // ENABLE_VERIFY_CSA
DEFINE_BOOL(trace_verify_csa, false, "trace code stubs verification")
DEFINE_STRING(csa_trap_on_node, nullptr,
              "trigger break point when a node with given id is created in "
              "given stub. The format is: StubName,NodeId")
DEFINE_BOOL_READONLY(fixed_array_bounds_checks, true,
                     "enable FixedArray bounds checks")
DEFINE_BOOL(turbo_stats, false, "print TurboFan statistics")
DEFINE_BOOL(turbo_stats_nvp, false,
            "print TurboFan statistics in machine-readable format")
DEFINE_BOOL(turbo_stats_wasm, false,
            "print TurboFan statistics of wasm compilations")
DEFINE_BOOL(turbo_splitting, true, "split nodes during scheduling in TurboFan")
DEFINE_BOOL(turbo_inlining, true, "enable inlining in TurboFan")
DEFINE_INT(max_inlined_bytecode_size, 460,
           "maximum size of bytecode for a single inlining")
DEFINE_INT(max_inlined_bytecode_size_cumulative, 920,
           "maximum cumulative size of bytecode considered for inlining")
DEFINE_INT(max_inlined_bytecode_size_absolute, 4600,
           "maximum absolute size of bytecode considered for inlining")
DEFINE_FLOAT(
    reserve_inline_budget_scale_factor, 1.2,
    "scale factor of bytecode size used to calculate the inlining budget")
DEFINE_INT(max_inlined_bytecode_size_small, 27,
           "maximum size of bytecode considered for small function inlining")
DEFINE_INT(max_optimized_bytecode_size, 60 * KB,
           "maximum bytecode size to "
           "be considered for turbofan optimization; too high values may cause "
           "the compiler to hit (release) assertions")
DEFINE_FLOAT(min_inlining_frequency, 0.15, "minimum frequency for inlining")
DEFINE_BOOL(polymorphic_inlining, true, "polymorphic inlining")
DEFINE_BOOL(stress_inline, false,
            "set high thresholds for inlining to inline as much as possible")
DEFINE_VALUE_IMPLICATION(stress_inline, max_inlined_bytecode_size, 999999)
DEFINE_VALUE_IMPLICATION(stress_inline, max_inlined_bytecode_size_cumulative,
                         999999)
DEFINE_VALUE_IMPLICATION(stress_inline, max_inlined_bytecode_size_absolute,
                         999999)
DEFINE_VALUE_IMPLICATION(stress_inline, min_inlining_frequency, 0.)
DEFINE_IMPLICATION(stress_inline, polymorphic_inlining)
DEFINE_BOOL(trace_turbo_inlining, false, "trace TurboFan inlining")
DEFINE_BOOL(turbo_inline_array_builtins, true,
            "inline array builtins in TurboFan code")
DEFINE_BOOL(use_osr, true, "use on-stack replacement")
DEFINE_BOOL(maglev_osr, true, "use maglev as on-stack replacement target")

// When using maglev as OSR target allow us to tier up further
DEFINE_WEAK_VALUE_IMPLICATION(maglev_osr, osr_from_maglev, true)
DEFINE_NEG_VALUE_IMPLICATION(use_osr, maglev_osr, false)
DEFINE_NEG_VALUE_IMPLICATION(turbofan, osr_from_maglev, false)
DEFINE_BOOL(concurrent_osr, true, "enable concurrent OSR")

DEFINE_BOOL(maglev_escape_analysis, true,
            "avoid inlined allocation of objects that cannot escape")
DEFINE_BOOL(trace_maglev_escape_analysis, false, "trace maglev escape analysis")
DEFINE_EXPERIMENTAL_FEATURE(maglev_object_tracking,
                            "track object changes to avoid escaping them")
DEFINE_WEAK_IMPLICATION(maglev_future, maglev_object_tracking)
DEFINE_BOOL(trace_maglev_object_tracking, false,
            "trace load/stores from maglev virtual objects")
DEFINE_WEAK_IMPLICATION(trace_maglev_graph_building,
                        trace_maglev_object_tracking)

// TODO(dmercadier): fix and re-enable string builder.
DEFINE_BOOL_READONLY(turbo_string_builder, false,
                     "use TurboFan fast string builder")

DEFINE_BOOL(trace_osr, false, "trace on-stack replacement")
DEFINE_BOOL(log_or_trace_osr, false,
            "internal helper flag, please use --trace-osr instead.")
DEFINE_IMPLICATION(trace_osr, log_or_trace_osr)
DEFINE_IMPLICATION(log_function_events, log_or_trace_osr)

DEFINE_BOOL(analyze_environment_liveness, true,
            "analyze liveness of environment slots and zap dead values")
DEFINE_BOOL(trace_environment_liveness, false,
            "trace liveness of local variable slots")
DEFINE_BOOL(turbo_load_elimination, true, "enable load elimination in TurboFan")
DEFINE_BOOL(trace_turbo_load_elimination, false,
            "trace TurboFan load elimination")
DEFINE_BOOL(turbo_profiling, false, "enable basic block profiling in TurboFan")
DEFINE_BOOL(turbo_profiling_verbose, false,
            "enable basic block profiling in TurboFan, and include each "
            "function's schedule and disassembly in the output")
DEFINE_IMPLICATION(turbo_profiling_verbose, turbo_profiling)
DEFINE_STRING(
    turbo_profiling_output, nullptr,
    "emit data about basic block usage in builtins to this file "
    "(requires that V8 was built with v8_enable_builtins_profiling=true)")
DEFINE_BOOL(reorder_builtins, false,
            "enable builtin reordering when run mksnapshot.")

DEFINE_BOOL(abort_on_bad_builtin_profile_data, false,
            "flag for mksnapshot, abort if builtins profile can't be applied")
DEFINE_BOOL(
    warn_about_builtin_profile_data, false,
    "flag for mksnapshot, emit warnings when applying builtin profile data")
DEFINE_STRING(
    dump_builtins_hashes_to_file, nullptr,
    "flag for mksnapshot, dump CSA builtins graph hashes to this file")

DEFINE_BOOL(turbo_verify_allocation, DEBUG_BOOL,
            "verify register allocation in TurboFan")
DEFINE_BOOL(turbo_move_optimization, true, "optimize gap moves in TurboFan")
DEFINE_BOOL(turbo_jt, true, "enable jump threading in TurboFan")
DEFINE_BOOL(turbo_loop_peeling, true, "TurboFan loop peeling")
DEFINE_BOOL(turbo_loop_variable, true, "TurboFan loop variable optimization")
DEFINE_BOOL(turbo_loop_rotation, true, "TurboFan loop rotation")
DEFINE_BOOL(turbo_cf_optimization, true, "optimize control flow in TurboFan")
DEFINE_BOOL(turbo_escape, true, "enable escape analysis")
DEFINE_BOOL(turbo_allocation_folding, true, "TurboFan allocation folding")
DEFINE_BOOL(turbo_instruction_scheduling, false,
            "enable instruction scheduling in TurboFan")
DEFINE_BOOL(turbo_stress_instruction_scheduling, false,
            "randomly schedule instructions to stress dependency tracking")
DEFINE_IMPLICATION(turbo_stress_instruction_scheduling,
                   turbo_instruction_scheduling)
DEFINE_BOOL(turbo_store_elimination, true,
            "enable store-store elimination in TurboFan")
DEFINE_BOOL(trace_store_elimination, false, "trace store elimination")
DEFINE_BOOL_READONLY(turbo_typer_hardening, true,
                     "extra bounds checks to protect against some known typer "
                     "mismatch exploit techniques (best effort)")

#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_IA32)
DEFINE_BOOL(turbo_rewrite_far_jumps, true,
            "rewrite far to near jumps (ia32,x64)")
#else
DEFINE_BOOL_READONLY(turbo_rewrite_far_jumps, false,
                     "rewrite far to near jumps (ia32,x64)")
#endif

DEFINE_BOOL(
    stress_gc_during_compilation, false,
    "simulate GC/compiler thread race related to https://crbug.com/v8/8520")
DEFINE_BOOL(turbo_fast_api_calls, true, "enable fast API calls from TurboFan")

DEFINE_BOOL(fast_api_allow_float_in_sim, false,
            "allow float parameters to be passed in simulator mode")

#ifdef V8_USE_ZLIB
DEFINE_BOOL(turbo_compress_frame_translations, false,
            "compress deoptimization frame translations (experimental)")
#else
DEFINE_BOOL_READONLY(
    turbo_compress_frame_translations, false,
    "compress deoptimization frame translations (experimental)")
#endif  // V8_USE_ZLIB
DEFINE_BOOL(turbo_inline_js_wasm_calls, true, "inline JS->Wasm calls")

DEFINE_BOOL(turbo_optimize_apply, true, "optimize Function.prototype.apply")
DEFINE_BOOL(turbo_optimize_math_minmax, true,
            "optimize call math.min/max with double array")

DEFINE_BOOL(turbo_collect_feedback_in_generic_lowering, false,
            "enable experimental feedback collection in generic lowering.")

DEFINE_BOOL(turboshaft, true, "enable TurboFan's Turboshaft phases for JS")

// Can't use Turbofan without Turboshaft.
DEFINE_NEG_NEG_IMPLICATION(turboshaft, turbofan)

#ifdef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS
DEFINE_BOOL(turboshaft_enable_debug_features, DEBUG_BOOL,
            "enables Turboshaft's DebugPrint, StaticAssert and "
            "CheckTurboshaftTypeOf operations")
#else
DEFINE_BOOL(turboshaft_enable_debug_features, false,
            "enables Turboshaft's DebugPrint, StaticAssert and "
            "CheckTurboshaftTypeOf operations")
#endif
DEFINE_BOOL(turboshaft_wasm, false,
            "enable TurboFan's Turboshaft phases for wasm")
DEFINE_WEAK_IMPLICATION(future, turboshaft_wasm)
DEFINE_BOOL(turboshaft_wasm_load_elimination, false,
            "enable Turboshaft's WasmLoadElimination")
DEFINE_WEAK_IMPLICATION(turboshaft_wasm, turboshaft_wasm_load_elimination)

DEFINE_BOOL(turboshaft_instruction_selection, true,
            "run instruction selection on Turboshaft IR directly")

DEFINE_BOOL(turboshaft_load_elimination, true,
            "enable Turboshaft's low-level load elimination for JS")
DEFINE_BOOL(turboshaft_loop_peeling, false, "enable Turboshaft's loop peeling")
DEFINE_BOOL(turboshaft_loop_unrolling, true,
            "enable Turboshaft's loop unrolling")

DEFINE_EXPERIMENTAL_FEATURE(turboshaft_typed_optimizations,
                            "enable an additional Turboshaft phase that "
                            "performs optimizations based on type information")
DEFINE_EXPERIMENTAL_FEATURE(
    turboshaft_wasm_instruction_selection_experimental,
    "run instruction selection on Turboshaft IR directly for wasm, on "
    "architectures where the feature is experimental")
DEFINE_BOOL(turboshaft_wasm_instruction_selection_staged, false,
            "run instruction selection on Turboshaft IR directly for wasm, on "
            "architectures where we are staging the feature")
DEFINE_EXPERIMENTAL_FEATURE(turboshaft_from_maglev,
                            "build the Turboshaft graph from Maglev")
// inline_api_calls are not supported by the Turboshaft->Maglev translation.
DEFINE_NEG_IMPLICATION(turboshaft_from_maglev, maglev_inline_api_calls)

DEFINE_BOOL(turboshaft_csa, false, "run the CSA pipeline with turboshaft")
DEFINE_IMPLICATION(turboshaft_csa, turboshaft_load_elimination)
DEFINE_EXPERIMENTAL_FEATURE(turboshaft_frontend,
                            "run (parts of) the frontend in Turboshaft")
DEFINE_EXPERIMENTAL_FEATURE(
    turboshaft_future,
    "enable Turboshaft features that we want to ship in the not-too-far future")
DEFINE_IMPLICATION(turboshaft_future, turboshaft)
DEFINE_WEAK_IMPLICATION(turboshaft_future, turboshaft_wasm)
#if V8_TARGET_ARCH_X64 or V8_TARGET_ARCH_ARM64 or V8_TARGET_ARCH_ARM or \
    V8_TARGET_ARCH_IA32
DEFINE_WEAK_IMPLICATION(turboshaft_future,
                        turboshaft_wasm_instruction_selection_experimental)
#endif
DEFINE_WEAK_IMPLICATION(turboshaft_future,
                        turboshaft_wasm_instruction_selection_staged)

#if V8_ENABLE_WEBASSEMBLY
// Shared-everything is implemented on turboshaft only for now.
DEFINE_IMPLICATION(experimental_wasm_shared, turboshaft_wasm)
DEFINE_NEG_IMPLICATION(experimental_wasm_shared, liftoff)

// FP16 is implemented on liftoff and turboshaft only for now.
DEFINE_IMPLICATION(experimental_wasm_fp16, turboshaft_wasm)
#endif

#ifdef DEBUG

DEFINE_UINT64(turboshaft_opt_bisect_limit, std::numeric_limits<uint64_t>::max(),
              "stop applying optional optimizations after a specified number "
              "of steps, useful for bisecting optimization bugs")
DEFINE_UINT64(turboshaft_opt_bisect_break, std::numeric_limits<uint64_t>::max(),
              "abort after a specified number of steps, useful for bisecting "
              "optimization bugs")
DEFINE_BOOL(turboshaft_verify_reductions, false,
            "check that turboshaft reductions are correct with respect to "
            "inferred types")
DEFINE_BOOL(turboshaft_trace_typing, false,
            "print typing steps of turboshaft type inference")
DEFINE_BOOL(turboshaft_trace_reduction, false,
            "trace individual Turboshaft reduction steps")
DEFINE_BOOL(turboshaft_trace_intermediate_reductions, false,
            "trace intermediate Turboshaft reduction steps")
DEFINE_BOOL(turboshaft_trace_emitted, false,
            "trace emitted Turboshaft instructions")
DEFINE_WEAK_IMPLICATION(turboshaft_trace_intermediate_reductions,
                        turboshaft_trace_reduction)
#else
DEFINE_BOOL_READONLY(turboshaft_trace_reduction, false,
                     "trace individual Turboshaft reduction steps")
DEFINE_BOOL_READONLY(turboshaft_trace_emitted, false,
                     "trace emitted Turboshaft instructions")
DEFINE_BOOL_READONLY(turboshaft_trace_intermediate_reductions, false,
                     "trace intermediate Turboshaft reduction steps")
#endif  // DEBUG

DEFINE_BOOL(profile_guided_optimization, true, "profile guided optimization")
DEFINE_BOOL(profile_guided_optimization_for_empty_feedback_vector, true,
            "profile guided optimization for empty feedback vector")
DEFINE_INT(invocation_count_for_early_optimization, 30,
           "invocation count threshold for early optimization")

// Favor memory over execution speed.
DEFINE_BOOL(optimize_for_size, false,
            "Enables optimizations which favor memory size over execution "
            "speed")
DEFINE_VALUE_IMPLICATION(optimize_for_size, max_semi_space_size, size_t{1})

// Flags for WebAssembly.
#if V8_ENABLE_WEBASSEMBLY

DEFINE_BOOL(wasm_generic_wrapper, true,
            "allow use of the generic js-to-wasm wrapper instead of "
            "per-signature wrappers")
DEFINE_BOOL(wasm_to_js_generic_wrapper, true,
            "allow use of the generic wasm-to-js wrapper instead of "
            "per-signature wrappers")
DEFINE_BOOL(expose_wasm, true, "expose wasm interface to JavaScript")
// Do not expose wasm in jitless mode.
//
// Even in interpreter-only mode, wasm currently still creates executable
// memory at runtime. Unexpose wasm until this changes.
// The correctness fuzzers are a special case: many of their test cases are
// built by fetching a random property from the the global object, and thus
// the global object layout must not change between configs. That is why we
// continue exposing wasm on correctness fuzzers even in jitless mode.
// TODO(jgruber): Remove this once / if wasm can run without executable
// memory.
DEFINE_BOOL(jitless_and_not_correctness_fuzzer_suppressions, true,
            "jitless && !correctness_fuzzer_suppressions")
DEFINE_NEG_NEG_IMPLICATION(jitless,
                           jitless_and_not_correctness_fuzzer_suppressions)
DEFINE_NEG_IMPLICATION(correctness_fuzzer_suppressions,
                       jitless_and_not_correctness_fuzzer_suppressions)
#ifdef V8_ENABLE_DRUMBRAKE
DEFINE_NEG_IMPLICATION(wasm_jitless,
                       jitless_and_not_correctness_fuzzer_suppressions)
DEFINE_NEG_IMPLICATION(wasm_jitless_if_available_for_testing,
                       jitless_and_not_correctness_fuzzer_suppressions)
#endif  // V8_ENABLE_DRUMBRAKE
DEFINE_DISABLE_FLAG_IMPLICATION(jitless_and_not_correctness_fuzzer_suppressions,
                                expose_wasm)
DEFINE_INT(wasm_num_compilation_tasks, 128,
           "maximum number of parallel compilation tasks for wasm")
DEFINE_VALUE_IMPLICATION(single_threaded, wasm_num_compilation_tasks, 0)
DEFINE_DEBUG_BOOL(trace_wasm_native_heap, false,
                  "trace wasm native heap events")
DEFINE_BOOL(trace_wasm_offheap_memory, false,
            "print details of wasm off-heap memory when the memory measurement "
            "API is used")
DEFINE_DEBUG_BOOL(trace_wasm_serialization, false,
                  "trace serialization/deserialization")
DEFINE_BOOL(wasm_async_compilation, true,
            "enable actual asynchronous compilation for WebAssembly.compile")
DEFINE_NEG_IMPLICATION(single_threaded, wasm_async_compilation)
DEFINE_BOOL(wasm_test_streaming, false,
            "use streaming compilation instead of async compilation for tests")
DEFINE_BOOL(wasm_native_module_cache_enabled, true,
            "enable the native module cache")
DEFINE_BOOL(turboshaft_wasm_wrappers, false,
            "compile the wasm wrappers with Turboshaft (instead of TurboFan)")
DEFINE_IMPLICATION(turboshaft_wasm, turboshaft_wasm_wrappers)
// The actual value used at runtime is clamped to kV8MaxWasmMemory{32,64}Pages.
DEFINE_UINT(wasm_max_mem_pages, kMaxUInt32,
            "maximum number of 64KiB memory pages per wasm memory")
DEFINE_UINT(wasm_max_table_size, wasm::kV8MaxWasmTableSize,
            "maximum table size of a wasm instance")
DEFINE_UINT(wasm_max_committed_code_mb, kMaxCommittedWasmCodeMB,
            "maximum committed code space for wasm (in MB)")
DEFINE_UINT(wasm_max_code_space_size_mb, kDefaultMaxWasmCodeSpaceSizeMb,
            "maximum size of a single wasm code space")
DEFINE_BOOL(wasm_tier_up, true,
            "enable tier up to the optimizing compiler (requires --liftoff to "
            "have an effect)")
DEFINE_BOOL(wasm_dynamic_tiering, true,
            "enable dynamic tier up to the optimizing compiler")
DEFINE_NEG_NEG_IMPLICATION(liftoff, wasm_dynamic_tiering)
DEFINE_BOOL(wasm_sync_tier_up, false,
            "run tier up jobs synchronously for testing")
DEFINE_INT(wasm_tiering_budget, 13'000'000,
           "budget for dynamic tiering (rough approximation of bytes executed")
DEFINE_INT(wasm_wrapper_tiering_budget, wasm::kGenericWrapperBudget,
           "budget for wrapper tierup (number of calls until tier-up)")
DEFINE_INT(max_wasm_functions, wasm::kV8MaxWasmDefinedFunctions,
           "maximum number of wasm functions defined in a module")
DEFINE_INT(
    wasm_caching_threshold, 1'000,
    "the amount of wasm top tier code that triggers the next caching event")
// Note: wasm_caching_hard_threshold should always be larger than
// wasm_caching_threshold. If wasm_caching_timeout_ms is 0, the hard threshold
// will be ignored.
DEFINE_INT(wasm_caching_hard_threshold, 1'000'000,
           "the amount of wasm top tier code that triggers caching "
           "immediately, ignoring the --wasm-caching-timeout-ms")
DEFINE_INT(
    wasm_caching_timeout_ms, 2000,
    "only trigger caching if no new code was compiled within this timeout (0 "
    "to disable this logic and only use --wasm-caching-threshold)")
DEFINE_BOOL(trace_wasm_compilation_times, false,
            "print how long it took to compile each wasm function")
DEFINE_INT(wasm_tier_up_filter, -1, "only tier-up function with this index")
DEFINE_INT(wasm_eager_tier_up_function, -1,
           "eagerly tier-up function with this index")
DEFINE_DEBUG_BOOL(trace_wasm_decoder, false, "trace decoding of wasm code")
DEFINE_DEBUG_BOOL(trace_wasm_compiler, false, "trace compiling of wasm code")
DEFINE_DEBUG_BOOL(trace_wasm_streaming, false,
                  "trace streaming compilation of wasm code")
DEFINE_DEBUG_BOOL(trace_wasm_stack_switching, false,
                  "trace wasm stack switching")
DEFINE_INT(wasm_stack_switching_stack_size, V8_DEFAULT_STACK_SIZE_KB,
           "default size of stacks for wasm stack-switching (in kB)")
DEFINE_BOOL(liftoff, true,
            "enable Liftoff, the baseline compiler for WebAssembly")
DEFINE_BOOL(liftoff_only, false,
            "disallow TurboFan compilation for WebAssembly (for testing)")
DEFINE_IMPLICATION(liftoff_only, liftoff)
DEFINE_NEG_IMPLICATION(liftoff_only, wasm_tier_up)
DEFINE_NEG_IMPLICATION(liftoff_only, wasm_dynamic_tiering)
DEFINE_NEG_IMPLICATION(fuzzing, liftoff_only)
DEFINE_DEBUG_BOOL(
    enable_testing_opcode_in_wasm, false,
    "enables a testing opcode in wasm that is only implemented in TurboFan")
// We can't tier up (from Liftoff to TurboFan) in single-threaded mode, hence
// disable tier up in that configuration for now.
DEFINE_NEG_IMPLICATION(single_threaded, wasm_tier_up)
DEFINE_DEBUG_BOOL(trace_liftoff, false,
                  "trace Liftoff, the baseline compiler for WebAssembly")
DEFINE_BOOL(trace_wasm_memory, false,
            "print all memory updates performed in wasm code")
// Fuzzers use {wasm_tier_mask_for_testing}, {wasm_debug_mask_for_testing}, and
// {wasm_turboshaft_mask_for_testing} together with {liftoff} and
// {no_wasm_tier_up} to force some functions to be compiled with TurboFan or for
// debug.
DEFINE_INT(wasm_tier_mask_for_testing, 0,
           "bitmask of declared(!) function indices to compile with TurboFan "
           "instead of Liftoff")
DEFINE_INT(wasm_debug_mask_for_testing, 0,
           "bitmask of declared(!) function indices to compile for debugging, "
           "only applies if the tier is Liftoff")
DEFINE_INT(wasm_turboshaft_mask_for_testing, 0,
           "bitmask of declared(!) function indices to compile with Turboshaft "
           "instead of TurboFan")
// TODO(clemensb): Introduce experimental_wasm_pgo to read from a custom section
// instead of from a local file.
DEFINE_BOOL(
    experimental_wasm_pgo_to_file, false,
    "experimental: dump Wasm PGO information to a local file (for testing)")
DEFINE_NEG_IMPLICATION(experimental_wasm_pgo_to_file, single_threaded)
DEFINE_BOOL(
    experimental_wasm_pgo_from_file, false,
    "experimental: read and use Wasm PGO data from a local file (for testing)")

DEFINE_BOOL(validate_asm, true,
            "validate asm.js modules and translate them to Wasm")
// Directly interpret asm.js code as regular JavaScript code.
// asm.js validation is disabled since it triggers wasm code generation.
DEFINE_NEG_IMPLICATION(jitless, validate_asm)

#if V8_ENABLE_DRUMBRAKE
// Wasm is put into interpreter-only mode. We repeat flag implications down
// here to ensure they're applied correctly by setting the --jitless flag.
DEFINE_NEG_IMPLICATION(jitless, asm_wasm_lazy_compilation)
DEFINE_NEG_IMPLICATION(jitless, wasm_lazy_compilation)
#endif  // V8_ENABLE_DRUMBRAKE

DEFINE_BOOL(suppress_asm_messages, false,
            "don't emit asm.js related messages (for golden file testing)")
DEFINE_BOOL(trace_asm_time, false, "print asm.js timing info to the console")
DEFINE_BOOL(trace_asm_scanner, false,
            "print tokens encountered by asm.js scanner")
DEFINE_BOOL(trace_asm_parser, false, "verbose logging of asm.js parse failures")
DEFINE_BOOL(stress_validate_asm, false, "try to validate everything as asm.js")

DEFINE_DEBUG_BOOL(dump_wasm_module, false, "dump wasm module bytes")
DEFINE_STRING(dump_wasm_module_path, nullptr,
              "directory to dump wasm modules to")
DEFINE_EXPERIMENTAL_FEATURE(
    wasm_fast_api,
    "Enable direct calls from wasm to fast API functions with bound "
    "call function to pass the the receiver as first parameter")

DEFINE_EXPERIMENTAL_FEATURE(wasm_deopt,
                            "enable deopts in optimized wasm functions")
// Deopt only works in combination with feedback.
DEFINE_NEG_NEG_IMPLICATION(liftoff, wasm_deopt)
// Deopt support for wasm is not implemented for Turbofan.
DEFINE_IMPLICATION(wasm_deopt, turboshaft_wasm)

// Declare command-line flags for Wasm features. Warning: avoid using these
// flags directly in the implementation. Instead accept
// wasm::WasmEnabledFeatures for configurability.
#include "src/wasm/wasm-feature-flags.h"

#define DECL_WASM_FLAG(feat, desc, val) \
  DEFINE_BOOL(experimental_wasm_##feat, val, "enable " desc " for Wasm")
#define DECL_EXPERIMENTAL_WASM_FLAG(feat, desc, val)    \
  DEFINE_EXPERIMENTAL_FEATURE(experimental_wasm_##feat, \
                              "enable " desc " for Wasm")
// Experimental wasm features imply --experimental and get the " (experimental)"
// suffix.
FOREACH_WASM_EXPERIMENTAL_FEATURE_FLAG(DECL_EXPERIMENTAL_WASM_FLAG)
// Staging and shipped features do not imply --experimental.
FOREACH_WASM_STAGING_FEATURE_FLAG(DECL_WASM_FLAG)
FOREACH_WASM_SHIPPED_FEATURE_FLAG(DECL_WASM_FLAG)
#undef DECL_WASM_FLAG
#undef DECL_EXPERIMENTAL_WASM_FLAG

DEFINE_IMPLICATION(experimental_wasm_stack_switching, experimental_wasm_jspi)

DEFINE_IMPLICATION(experimental_wasm_jspi, experimental_wasm_type_reflection)

DEFINE_BOOL(wasm_staging, false, "enable staged wasm features")

#define WASM_STAGING_IMPLICATION(feat, desc, val) \
  DEFINE_IMPLICATION(wasm_staging, experimental_wasm_##feat)
FOREACH_WASM_STAGING_FEATURE_FLAG(WASM_STAGING_IMPLICATION)
#undef WASM_STAGING_IMPLICATION

DEFINE_BOOL(wasm_opt, true, "enable wasm optimization")
DEFINE_BOOL(wasm_bounds_checks, true,
            "enable bounds checks (disable for performance testing only)")
DEFINE_BOOL(wasm_stack_checks, true,
            "enable stack checks (disable for performance testing only)")
DEFINE_BOOL(
    wasm_enforce_bounds_checks, false,
    "enforce explicit bounds check even if the trap handler is available")
// "no bounds checks" implies "no enforced bounds checks".
DEFINE_NEG_NEG_IMPLICATION(wasm_bounds_checks, wasm_enforce_bounds_checks)
DEFINE_BOOL(wasm_math_intrinsics, true,
            "intrinsify some Math imports into wasm")

DEFINE_EXPERIMENTAL_FEATURE(
    wasm_inlining_call_indirect,
    "enable speculative inlining of Wasm indirect calls, also enables "
    "--experimental-wasm-inlining and requires --turboshaft-wasm")
// Requires basic inlining machinery, e.g., for allocating feedback vectors.
DEFINE_IMPLICATION(wasm_inlining_call_indirect, experimental_wasm_inlining)
// This is not implemented for Turbofan, so make sure users are aware by
// forcing them to explicitly enable Turboshaft (until it's the default anyway).
DEFINE_NEG_NEG_IMPLICATION(turboshaft_wasm, wasm_inlining_call_indirect)

DEFINE_SIZE_T(wasm_inlining_budget, 5000,
              "maximum graph size (in TF nodes) that allows inlining more")
DEFINE_SIZE_T(wasm_inlining_max_size, 500,
              "maximum function size (in wire bytes) that may be inlined")
DEFINE_SIZE_T(
    wasm_inlining_factor, 3,
    "maximum multiple graph size (in TF nodes) in comparison to initial size")
DEFINE_SIZE_T(wasm_inlining_min_budget, 50,
              "minimum graph size budget (in TF nodes) for which the "
              "wasm_inlinining_factor does not apply")
DEFINE_BOOL(wasm_inlining_ignore_call_counts, false,
            "Ignore call counts when considering inlining candidates. The flag "
            "is supposed to be used for fuzzing")
DEFINE_BOOL(trace_wasm_inlining, false, "trace wasm inlining")
DEFINE_BOOL(trace_wasm_typer, false, "trace wasm typer")

DEFINE_BOOL(wasm_loop_unrolling, true,
            "enable loop unrolling for wasm functions")
DEFINE_BOOL(wasm_loop_peeling, true, "enable loop peeling for wasm functions")
DEFINE_SIZE_T(wasm_loop_peeling_max_size, 1000, "maximum size for peeling")
DEFINE_BOOL(trace_wasm_loop_peeling, false, "trace wasm loop peeling")
DEFINE_BOOL(wasm_fuzzer_gen_test, false,
            "generate a test case when running a wasm fuzzer")
DEFINE_IMPLICATION(wasm_fuzzer_gen_test, single_threaded)
DEFINE_BOOL(print_wasm_code, false, "print WebAssembly code")
DEFINE_INT(print_wasm_code_function_index, -1,
           "print WebAssembly code for function at index")
DEFINE_BOOL(print_wasm_stub_code, false, "print WebAssembly stub code")
DEFINE_BOOL(asm_wasm_lazy_compilation, true,
            "enable lazy compilation for asm.js translated to wasm (see "
            "--validate-asm)")
DEFINE_BOOL(wasm_lazy_compilation, true,
            "enable lazy compilation for all wasm modules")
DEFINE_DEBUG_BOOL(trace_wasm_lazy_compilation, false,
                  "trace lazy compilation of wasm functions")
DEFINE_EXPERIMENTAL_FEATURE(
    wasm_lazy_validation,
    "enable lazy validation for lazily compiled wasm functions")
DEFINE_WEAK_IMPLICATION(wasm_lazy_validation, wasm_lazy_compilation)
DEFINE_BOOL(wasm_simd_ssse3_codegen, false, "allow wasm SIMD SSSE3 codegen")

DEFINE_BOOL(wasm_code_gc, true, "enable garbage collection of wasm code")
DEFINE_BOOL(trace_wasm_code_gc, false, "trace garbage collection of wasm code")
DEFINE_BOOL(stress_wasm_code_gc, false,
            "stress test garbage collection of wasm code")
DEFINE_INT(wasm_max_initial_code_space_reservation, 0,
           "maximum size of the initial wasm code space reservation (in MB)")
DEFINE_BOOL(stress_wasm_memory_moving, false,
            "always move non-shared bounds-checked Wasm memory on grow")
DEFINE_BOOL(flush_liftoff_code, false,
            "enable flushing liftoff code on memory pressure signal")

DEFINE_SIZE_T(wasm_max_module_size, wasm::kV8MaxWasmModuleSize,
              "maximum allowed size of wasm modules")
DEFINE_SIZE_T(wasm_disassembly_max_mb, 1000,
              "maximum size of produced disassembly (in MB, approximate)")

DEFINE_BOOL(trace_wasm, false, "trace wasm function calls")
// Inlining breaks --trace-wasm, hence disable that if --trace-wasm is enabled.
DEFINE_NEG_IMPLICATION(trace_wasm, experimental_wasm_inlining)

// Flags for Wasm GDB remote debugging.
#ifdef V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING
#define DEFAULT_WASM_GDB_REMOTE_PORT 8765
DEFINE_BOOL(wasm_gdb_remote, false,
            "enable GDB-remote for WebAssembly debugging")
DEFINE_NEG_IMPLICATION(wasm_gdb_remote, wasm_tier_up)
DEFINE_INT(wasm_gdb_remote_port, DEFAULT_WASM_GDB_REMOTE_PORT,
           "default port for WebAssembly debugging with LLDB.")
DEFINE_BOOL(wasm_pause_waiting_for_debugger, false,
            "pause at the first Webassembly instruction waiting for a debugger "
            "to attach")
DEFINE_BOOL(trace_wasm_gdb_remote, false, "trace Webassembly GDB-remote server")
#endif  // V8_ENABLE_WASM_GDB_REMOTE_DEBUGGING

// wasm instance management
DEFINE_DEBUG_BOOL(trace_wasm_instances, false,
                  "trace creation and collection of wasm instances")

// Flags for WASM SIMD256 revectorize
#ifdef V8_ENABLE_WASM_SIMD256_REVEC
DEFINE_EXPERIMENTAL_FEATURE(
    experimental_wasm_revectorize,
    "enable 128 to 256 bit revectorization for Webassembly SIMD")
DEFINE_BOOL(trace_wasm_revectorize, false, "trace wasm revectorize")
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_X64
DEFINE_BOOL(wasm_memory64_trap_handling, true,
            "Use trap handling for Wasm memory64 bounds checks")
#else
DEFINE_BOOL_READONLY(wasm_memory64_trap_handling, false,
                     "Use trap handling for Wasm memory64 bounds checks (not "
                     "supported for this architecture)")
#endif  // V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_X64

#ifdef V8_ENABLE_DRUMBRAKE
// DrumBrake flags.
DEFINE_EXPERIMENTAL_FEATURE(wasm_jitless,
                            "Execute all wasm code in the Wasm interpreter")
DEFINE_BOOL(wasm_jitless_if_available_for_testing, false,
            "Enables the Wasm interpreter, for testing, but only if "
            "the 'v8_enable_drumbrake' flag is set.")
DEFINE_IMPLICATION(wasm_jitless_if_available_for_testing, wasm_jitless)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
DEFINE_BOOL(trace_drumbrake_bytecode_generator, false,
            "trace drumbrake generation of interpreter bytecode")
DEFINE_BOOL(trace_drumbrake_execution, false,
            "trace drumbrake execution of wasm code")
DEFINE_BOOL(trace_drumbrake_execution_verbose, false,
            "print more information for the drumbrake execution of wasm code")
DEFINE_IMPLICATION(trace_drumbrake_execution_verbose, trace_drumbrake_execution)
DEFINE_BOOL(redirect_drumbrake_traces, false,
            "write drumbrake traces into file <pid>-<isolate id>.dbt")
DEFINE_STRING(
    trace_drumbrake_filter, "*",
    "filter for selecting which wasm functions to trace in the interpreter")
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
DEFINE_BOOL(drumbrake_super_instructions, true,
            "enable drumbrake merged wasm instructions optimization")
DEFINE_BOOL(drumbrake_register_optimization, true,
            "enable passing the top stack value in a register in drumbrake")

// Directly interpret asm.js code as regular JavaScript code, instead of
// translating it to Wasm bytecode first and then interpreting that with
// DrumBrake. (validate_asm=false turns off asm.js to Wasm compilation.)
DEFINE_NEG_IMPLICATION(wasm_jitless, validate_asm)

// --wasm-jitless resets {asm-,}wasm-lazy-compilation.
DEFINE_NEG_IMPLICATION(wasm_jitless, asm_wasm_lazy_compilation)
DEFINE_NEG_IMPLICATION(wasm_jitless, wasm_lazy_compilation)
DEFINE_NEG_IMPLICATION(wasm_jitless, wasm_lazy_validation)
DEFINE_NEG_IMPLICATION(wasm_jitless, wasm_tier_up)

// --wasm-enable-exec-time-histograms works both in jitted and jitless mode
// and enables histogram V8.Jit[less]WasmExecutionPercentage, which measures
// the percentage of time spent running Wasm code. Note that generating samples
// for this metric causes a small performance degradation, and requires setting
// the additional flag --slow-histograms.
DEFINE_BOOL(wasm_enable_exec_time_histograms, false,
            "enables histograms that track the time spent executing Wasm code")
DEFINE_INT(wasm_exec_time_histogram_sample_duration, 1000,
           "sample duration for V8.Jit[less]WasmExecutionPercentage, in msec")
DEFINE_INT(wasm_exec_time_histogram_sample_period, 4000,
           "sample period for V8.Jit[less]WasmExecutionPercentage, in msec")
DEFINE_INT(wasm_exec_time_histogram_slow_threshold, 10000,
           "V8.Jit[less]WasmExecutionPercentage threshold used to detect "
           "Wasm-intensive workloads (0-100000)")
DEFINE_INT(wasm_exec_time_slow_threshold_samples_count, 1,
           "number of V8.Jit[less]WasmExecutionPercentage samples used to "
           "calculate the threshold for the V8.Jit[less]WasmExecutionTooSlow "
           "histogram")
DEFINE_IMPLICATION(wasm_enable_exec_time_histograms, slow_histograms)
DEFINE_NEG_IMPLICATION(wasm_enable_exec_time_histograms,
                       turbo_inline_js_wasm_calls)
DEFINE_NEG_IMPLICATION(wasm_enable_exec_time_histograms, wasm_generic_wrapper)
#else   // V8_ENABLE_DRUMBRAKE
DEFINE_BOOL_READONLY(wasm_jitless, false,
                     "execute all Wasm code in the Wasm interpreter")
DEFINE_BOOL(wasm_jitless_if_available_for_testing, false, "")
#endif  // V8_ENABLE_DRUMBRAKE

#endif  // V8_ENABLE_WEBASSEMBLY

DEFINE_INT(stress_sampling_allocation_profiler, 0,
           "Enables sampling allocation profiler with X as a sample interval")

// Garbage collections flags.
DEFINE_BOOL(lazy_new_space_shrinking, false,
            "Enables the lazy new space shrinking strategy")
DEFINE_SIZE_T(min_semi_space_size, 0,
              "min size of a semi-space (in MBytes), the new space consists of "
              "two semi-spaces")
DEFINE_SIZE_T(max_semi_space_size, 0,
              "max size of a semi-space (in MBytes), the new space consists of "
              "two semi-spaces")
DEFINE_INT(semi_space_growth_factor, 2, "factor by which to grow the new space")
// Set minimum semi space growth factor
DEFINE_MIN_VALUE_IMPLICATION(semi_space_growth_factor, 2)
DEFINE_SIZE_T(max_old_space_size, 0, "max size of the old space (in Mbytes)")
DEFINE_SIZE_T(
    max_heap_size, 0,
    "max size of the heap (in Mbytes) "
    "both max_semi_space_size and max_old_space_size take precedence. "
    "All three flags cannot be specified at the same time.")
DEFINE_SIZE_T(initial_heap_size, 0, "initial size of the heap (in Mbytes)")
DEFINE_BOOL(huge_max_old_generation_size, true,
            "Increase max size of the old space to 4 GB for x64 systems with"
            "the physical memory bigger than 16 GB")
DEFINE_SIZE_T(initial_old_space_size, 0, "initial old space size (in Mbytes)")
DEFINE_BOOL(separate_gc_phases, false,
            "young and full garbage collection phases are not overlapping")
DEFINE_BOOL(gc_global, false, "always perform global GCs")

// TODO(12950): The next three flags only have an effect if
// V8_ENABLE_ALLOCATION_TIMEOUT is set, so we should only define them in that
// config. That currently breaks Node's parallel/test-domain-error-types test
// though.
DEFINE_INT(random_gc_interval, 0,
           "Collect garbage after random(0, X) V8 allocations. It overrides "
           "gc_interval.")
DEFINE_INT(gc_interval, -1, "garbage collect after <n> allocations")
DEFINE_INT(cppgc_random_gc_interval, 0,
           "Collect garbage after random(0, X) cppgc allocations.")

DEFINE_INT(retain_maps_for_n_gc, 2,
           "keeps maps alive for <n> old space garbage collections")
DEFINE_BOOL(trace_gc, false,
            "print one trace line following each garbage collection")
DEFINE_BOOL(trace_gc_nvp, false,
            "print one detailed trace line in name=value format "
            "after each garbage collection")
DEFINE_BOOL(trace_gc_ignore_scavenger, false,
            "do not print trace line after scavenger collection")
DEFINE_BOOL(trace_memory_reducer, false, "print memory reducer behavior")
DEFINE_BOOL(trace_gc_verbose, false,
            "print more details following each garbage collection")
DEFINE_IMPLICATION(trace_gc_verbose, trace_gc)
DEFINE_BOOL(trace_gc_freelists, false,
            "prints details of each freelist before and after "
            "each major garbage collection")
DEFINE_BOOL(trace_gc_freelists_verbose, false,
            "prints details of freelists of each page before and after "
            "each major garbage collection")
DEFINE_IMPLICATION(trace_gc_freelists_verbose, trace_gc_freelists)
DEFINE_BOOL(trace_gc_heap_layout, false,
            "print layout of pages in heap before and after gc")
DEFINE_BOOL(trace_gc_heap_layout_ignore_minor_gc, true,
            "do not print trace line before and after minor-gc")
DEFINE_BOOL(trace_evacuation_candidates, false,
            "Show statistics about the pages evacuation by the compaction")

DEFINE_BOOL(trace_pending_allocations, false,
            "trace calls to Heap::IsAllocationPending that return true")

DEFINE_INT(trace_allocation_stack_interval, -1,
           "print stack trace after <n> free-list allocations")
DEFINE_INT(trace_duplicate_threshold_kb, 0,
           "print duplicate objects in the heap if their size is more than "
           "given threshold")
DEFINE_BOOL(trace_fragmentation, false, "report fragmentation for old space")
DEFINE_BOOL(trace_fragmentation_verbose, false,
            "report fragmentation for old space (detailed)")
DEFINE_BOOL(minor_ms_trace_fragmentation, false,
            "trace fragmentation after marking")
DEFINE_BOOL(trace_evacuation, false, "report evacuation statistics")
DEFINE_BOOL(trace_mutator_utilization, false,
            "print mutator utilization, allocation speed, gc speed")
DEFINE_BOOL(incremental_marking, true, "use incremental marking")
DEFINE_BOOL(incremental_marking_bailout_when_ahead_of_schedule, true,
            "bails out of incremental marking when ahead of schedule")
DEFINE_BOOL(incremental_marking_task, true, "use tasks for incremental marking")
DEFINE_INT(incremental_marking_task_delay_ms, 0,
           "incremental marking task delay. 0 means not using delayed tasks.")
DEFINE_INT(incremental_marking_soft_trigger, 0,
           "threshold for starting incremental marking via a task in percent "
           "of available space: limit - size")
DEFINE_INT(incremental_marking_hard_trigger, 0,
           "threshold for starting incremental marking immediately in percent "
           "of available space: limit - size")
DEFINE_BOOL(trace_unmapper, false, "Trace the unmapping")
DEFINE_BOOL(parallel_scavenge, true, "parallel scavenge")
DEFINE_BOOL(minor_gc_task, true, "schedule scavenge tasks")
DEFINE_UINT(minor_gc_task_trigger, 80,
            "minor GC task trigger in percent of the current heap limit")
DEFINE_BOOL(scavenge_separate_stack_scanning, false,
            "use a separate phase for stack scanning in scavenge")
DEFINE_BOOL(trace_parallel_scavenge, false, "trace parallel scavenge")
DEFINE_EXPERIMENTAL_FEATURE(
    cppgc_young_generation,
    "run young generation garbage collections in Oilpan")
// CppGC young generation (enables unified young heap) is based on Minor MS.
DEFINE_IMPLICATION(cppgc_young_generation, minor_ms)
// Unified young generation disables the unmodified wrapper reclamation
// optimization.
DEFINE_NEG_IMPLICATION(cppgc_young_generation, reclaim_unmodified_wrappers)
DEFINE_BOOL(optimize_gc_for_battery, false, "optimize GC for battery")
#if defined(V8_ATOMIC_OBJECT_FIELD_WRITES)
DEFINE_BOOL(concurrent_marking, true, "use concurrent marking")
#else
// Concurrent marking cannot be used without atomic object field loads and
// stores.
DEFINE_BOOL(concurrent_marking, false, "use concurrent marking")
#endif
DEFINE_INT(
    concurrent_marking_max_worker_num, 7,
    "max worker number of concurrent marking, 0 for NumberOfWorkerThreads")
DEFINE_BOOL(concurrent_array_buffer_sweeping, true,
            "concurrently sweep array buffers")
DEFINE_BOOL(stress_concurrent_allocation, false,
            "start background threads that allocate memory")
DEFINE_BOOL(parallel_marking, true, "use parallel marking in atomic pause")
DEFINE_INT(ephemeron_fixpoint_iterations, 10,
           "number of fixpoint iterations it takes to switch to linear "
           "ephemeron algorithm")
DEFINE_BOOL(trace_concurrent_marking, false, "trace concurrent marking")
DEFINE_BOOL(concurrent_sweeping, true, "use concurrent sweeping")
DEFINE_NEG_NEG_IMPLICATION(concurrent_sweeping,
                           concurrent_array_buffer_sweeping)
DEFINE_BOOL(parallel_compaction, true, "use parallel compaction")
DEFINE_BOOL(parallel_pointer_update, true,
            "use parallel pointer update during compaction")
DEFINE_BOOL(parallel_weak_ref_clearing, true,
            "use parallel threads to clear weak refs in the atomic pause.")
DEFINE_BOOL(detect_ineffective_gcs_near_heap_limit, true,
            "trigger out-of-memory failure to avoid GC storm near heap limit")
DEFINE_BOOL(trace_incremental_marking, false,
            "trace progress of the incremental marking")
DEFINE_BOOL(trace_stress_marking, false, "trace stress marking progress")
DEFINE_BOOL(trace_stress_scavenge, false, "trace stress scavenge progress")
DEFINE_BOOL(track_gc_object_stats, false,
            "track object counts and memory usage")
DEFINE_BOOL(trace_gc_object_stats, false,
            "trace object counts and memory usage")
DEFINE_BOOL(trace_zone_stats, false, "trace zone memory usage")
DEFINE_GENERIC_IMPLICATION(
    trace_zone_stats,
    TracingFlags::zone_stats.store(
        v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_SIZE_T(
    zone_stats_tolerance, 1 * MB,
    "report a tick only when allocated zone memory changes by this amount")
DEFINE_BOOL(trace_zone_type_stats, false, "trace per-type zone memory usage")
DEFINE_GENERIC_IMPLICATION(
    trace_zone_type_stats,
    TracingFlags::zone_stats.store(
        v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_DEBUG_BOOL(trace_backing_store, false, "trace backing store events")
DEFINE_INT(gc_stats, 0, "Used by tracing internally to enable gc statistics")
DEFINE_IMPLICATION(trace_gc_object_stats, track_gc_object_stats)
DEFINE_GENERIC_IMPLICATION(
    track_gc_object_stats,
    TracingFlags::gc_stats.store(
        v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_GENERIC_IMPLICATION(
    trace_gc_object_stats,
    TracingFlags::gc_stats.store(
        v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_NEG_IMPLICATION(trace_gc_object_stats, incremental_marking)
DEFINE_NEG_NEG_IMPLICATION(incremental_marking, concurrent_marking)
DEFINE_NEG_NEG_IMPLICATION(parallel_marking, concurrent_marking)
DEFINE_IMPLICATION(concurrent_marking, incremental_marking)
DEFINE_BOOL(track_detached_contexts, true,
            "track native contexts that are expected to be garbage collected")
DEFINE_BOOL(trace_detached_contexts, false,
            "trace native contexts that are expected to be garbage collected")
DEFINE_IMPLICATION(trace_detached_contexts, track_detached_contexts)
#ifdef VERIFY_HEAP
DEFINE_BOOL(verify_heap, false, "verify heap pointers before and after GC")
DEFINE_BOOL(verify_heap_skip_remembered_set, false,
            "disable remembered set verification")
#else
DEFINE_BOOL_READONLY(verify_heap, false,
                     "verify heap pointers before and after GC")
#endif
DEFINE_BOOL(move_object_start, true, "enable moving of object starts")
DEFINE_BOOL(memory_reducer, true, "use memory reducer")
DEFINE_BOOL(memory_reducer_favors_memory, true,
            "memory reducer runs GC with ReduceMemoryFootprint flag")
DEFINE_BOOL(memory_reducer_for_small_heaps, true,
            "use memory reducer for small heaps")
DEFINE_INT(memory_reducer_gc_count, 2,
           "Maximum number of memory reducer GCs scheduled")
DEFINE_INT(heap_growing_percent, 0,
           "specifies heap growing factor as (1 + heap_growing_percent/100)")
DEFINE_INT(v8_os_page_size, 0, "override OS page size (in KBytes)")
DEFINE_BOOL(allocation_buffer_parking, true, "allocation buffer parking")
DEFINE_BOOL(compact, true,
            "Perform compaction on full GCs based on V8's default heuristics")
DEFINE_BOOL(compact_code_space, true,
            "Perform code space compaction on full collections.")
DEFINE_BOOL(compact_on_every_full_gc, false,
            "Perform compaction on every full GC")
DEFINE_BOOL(compact_with_stack, true,
            "Perform compaction when finalizing a full GC with stack")
DEFINE_BOOL(
    compact_code_space_with_stack, true,
    "Perform code space compaction when finalizing a full GC with stack")
// Disabling compaction with stack implies also disabling code space compaction
// with stack.
DEFINE_NEG_NEG_IMPLICATION(compact_with_stack, compact_code_space_with_stack)
DEFINE_BOOL(shortcut_strings_with_stack, true,
            "Shortcut Strings during GC with stack")
DEFINE_BOOL(stress_compaction, false,
            "Stress GC compaction to flush out bugs (implies "
            "--force_marking_deque_overflows)")
DEFINE_BOOL(stress_compaction_random, false,
            "Stress GC compaction by selecting random percent of pages as "
            "evacuation candidates. Overrides stress_compaction.")
DEFINE_IMPLICATION(stress_compaction, force_marking_deque_overflows)
DEFINE_IMPLICATION(stress_compaction, gc_global)
DEFINE_VALUE_IMPLICATION(stress_compaction, max_semi_space_size, (size_t)1)
DEFINE_BOOL(flush_baseline_code, false,
            "flush of baseline code when it has not been executed recently")
DEFINE_BOOL(flush_bytecode, true,
            "flush of bytecode when it has not been executed recently")
DEFINE_INT(bytecode_old_age, 6, "number of gcs before we flush code")
DEFINE_BOOL(flush_code_based_on_time, false,
            "Use time-base code flushing instead of age.")
DEFINE_BOOL(flush_code_based_on_tab_visibility, false,
            "Flush code when tab goes into the background.")
DEFINE_INT(bytecode_old_time, 30, "number of seconds before we flush code")
DEFINE_BOOL(stress_flush_code, false, "stress code flushing")
DEFINE_BOOL(trace_flush_code, false, "trace bytecode flushing")
DEFINE_BOOL(use_marking_progress_bar, true,
            "Use a progress bar to scan large objects in increments when "
            "incremental marking is active.")
DEFINE_BOOL(stress_per_context_marking_worklist, false,
            "Use per-context worklist for marking")
DEFINE_BOOL(force_marking_deque_overflows, false,
            "force overflows of marking deque by reducing it's size "
            "to 64 words")
DEFINE_BOOL(stress_incremental_marking, false,
            "force incremental marking for small heaps and run it more often")

DEFINE_BOOL(fuzzer_gc_analysis, false,
            "prints number of allocations and enables analysis mode for gc "
            "fuzz testing, e.g. --stress-marking, --stress-scavenge")
DEFINE_INT(stress_marking, 0,
           "force marking at random points between 0 and X (inclusive) percent "
           "of the regular marking start limit")
DEFINE_INT(stress_scavenge, 0,
           "force scavenge at random points between 0 and X (inclusive) "
           "percent of the new space capacity")
DEFINE_VALUE_IMPLICATION(fuzzer_gc_analysis, stress_marking, 99)
DEFINE_VALUE_IMPLICATION(fuzzer_gc_analysis, stress_scavenge, 99)
DEFINE_BOOL(
    reclaim_unmodified_wrappers, true,
    "reclaim otherwise unreachable unmodified wrapper objects when possible")

// These flags will be removed after experiments. Do not rely on them.
DEFINE_BOOL(gc_experiment_less_compaction, false,
            "less compaction in non-memory reducing mode")

DEFINE_INT(gc_memory_reducer_start_delay_ms, 8000,
           "Delay before memory reducer start")

DEFINE_BOOL(concurrent_marking_high_priority_threads, false,
            "use high priority threads for concurrent Marking")

DEFINE_BOOL(disable_abortjs, false, "disables AbortJS runtime function")

DEFINE_BOOL(randomize_all_allocations, false,
            "randomize virtual memory reservations by ignoring any hints "
            "passed when allocating pages")

DEFINE_BOOL(manual_evacuation_candidates_selection, false,
            "Test mode only flag. It allows an unit test to select evacuation "
            "candidates pages (requires --stress_compaction).")

DEFINE_BOOL(clear_free_memory, false, "initialize free memory with 0")

DEFINE_BOOL(idle_gc_on_context_disposal, true, "idle gc on context disposal")

DEFINE_BOOL(trace_context_disposal, false, "trace context disposal")

// v8::CppHeap flags that allow fine-grained control of how C++ memory is
// reclaimed in the garbage collector.
DEFINE_BOOL(cppheap_incremental_marking, false,
            "use incremental marking for CppHeap")
DEFINE_NEG_NEG_IMPLICATION(incremental_marking, cppheap_incremental_marking)
DEFINE_NEG_NEG_IMPLICATION(incremental_marking, memory_reducer)
DEFINE_WEAK_IMPLICATION(incremental_marking, cppheap_incremental_marking)
DEFINE_BOOL(cppheap_concurrent_marking, false,
            "use concurrent marking for CppHeap")
DEFINE_NEG_NEG_IMPLICATION(cppheap_incremental_marking,
                           cppheap_concurrent_marking)
DEFINE_NEG_NEG_IMPLICATION(concurrent_marking, cppheap_concurrent_marking)
DEFINE_WEAK_IMPLICATION(concurrent_marking, cppheap_concurrent_marking)
DEFINE_BOOL(cppheap_optimize_sweep_for_mutator, false,
            "optimize sweeping for mutator time (i.e. spend less time in "
            "synchronous sweeping and idle time)")

DEFINE_BOOL(memory_balancer, false,
            "use membalancer, "
            "a new heap limit balancing algorithm")
DEFINE_FLOAT(memory_balancer_c_value, 3e-10,
             "c value for membalancer. "
             "A special constant to balance between memory and space tradeoff. "
             "The smaller the more memory it uses.")
DEFINE_NEG_IMPLICATION(memory_balancer, memory_reducer)
DEFINE_BOOL(trace_memory_balancer, false, "print memory balancer behavior.")

// assembler-ia32.cc / assembler-arm.cc / assembler-arm64.cc / assembler-x64.cc
#ifdef V8_ENABLE_DEBUG_CODE
DEFINE_BOOL(debug_code, DEBUG_BOOL,
            "generate extra code (assertions) for debugging")
#else
DEFINE_BOOL_READONLY(debug_code, false, "")
#endif
#ifdef V8_CODE_COMMENTS
DEFINE_BOOL(code_comments, false,
            "emit comments in code disassembly; for more readable source "
            "positions you should add --no-concurrent_recompilation")
#else
DEFINE_BOOL_READONLY(code_comments, false, "")
#endif
DEFINE_BOOL(enable_sse3, true, "enable use of SSE3 instructions if available")
DEFINE_BOOL(enable_ssse3, true, "enable use of SSSE3 instructions if available")
DEFINE_BOOL(enable_sse4_1, true,
            "enable use of SSE4.1 instructions if available")
DEFINE_BOOL(enable_sse4_2, true,
            "enable use of SSE4.2 instructions if available")
DEFINE_BOOL(enable_sahf, true,
            "enable use of SAHF instruction if available (X64 only)")
DEFINE_BOOL(enable_avx, true, "enable use of AVX instructions if available")
DEFINE_BOOL(enable_avx2, true, "enable use of AVX2 instructions if available")
DEFINE_BOOL(enable_avx_vnni, true,
            "enable use of AVX-VNNI instructions if available")
DEFINE_BOOL(enable_fma3, true, "enable use of FMA3 instructions if available")
DEFINE_BOOL(enable_f16c, true, "enable use of F16C instructions if available")
DEFINE_BOOL(enable_bmi1, true, "enable use of BMI1 instructions if available")
DEFINE_BOOL(enable_bmi2, true, "enable use of BMI2 instructions if available")
DEFINE_BOOL(enable_lzcnt, true, "enable use of LZCNT instruction if available")
DEFINE_BOOL(enable_popcnt, true,
            "enable use of POPCNT instruction if available")
DEFINE_STRING(arm_arch, ARM_ARCH_DEFAULT,
              "generate instructions for the selected ARM architecture if "
              "available: armv6, armv7, armv7+sudiv or armv8")
DEFINE_BOOL(force_long_branches, false,
            "force all emitted branches to be in long mode (MIPS/PPC only)")
DEFINE_STRING(mcpu, "auto", "enable optimization for specific cpu")
DEFINE_BOOL(partial_constant_pool, true,
            "enable use of partial constant pools (X64 only)")
DEFINE_STRING(sim_arm64_optional_features, "none",
              "enable optional features on the simulator for testing: none or "
              "all")
DEFINE_BOOL(intel_jcc_erratum_mitigation, false,
            "enable mitigation for Intel JCC erratum on affected CPUs")
DEFINE_WEAK_IMPLICATION(future, intel_jcc_erratum_mitigation)

#if defined(V8_TARGET_ARCH_RISCV32) || defined(V8_TARGET_ARCH_RISCV64)
DEFINE_BOOL(riscv_trap_to_simulator_debugger, false,
            "enable simulator trap to debugger")
DEFINE_BOOL(riscv_debug, false, "enable debug prints")

DEFINE_BOOL(riscv_constant_pool, true, "enable constant pool (RISCV only)")

DEFINE_BOOL(riscv_c_extension, false,
            "enable compressed extension isa variant (RISCV only)")
DEFINE_BOOL(riscv_b_extension, false,
            "enable B extension isa variant (RISCV only)")
#endif

// Controlling source positions for Torque/CSA code.
DEFINE_BOOL(enable_source_at_csa_bind, false,
            "Include source information in the binary at CSA bind locations.")

// Deprecated ARM flags (replaced by arm_arch).
DEFINE_MAYBE_BOOL(enable_armv7, "deprecated (use --arm_arch instead)")
DEFINE_MAYBE_BOOL(enable_vfp3, "deprecated (use --arm_arch instead)")
DEFINE_MAYBE_BOOL(enable_32dregs, "deprecated (use --arm_arch instead)")
DEFINE_MAYBE_BOOL(enable_neon, "deprecated (use --arm_arch instead)")
DEFINE_MAYBE_BOOL(enable_sudiv, "deprecated (use --arm_arch instead)")
DEFINE_MAYBE_BOOL(enable_armv8, "deprecated (use --arm_arch instead)")

// regexp-macro-assembler-*.cc
DEFINE_BOOL(enable_regexp_unaligned_accesses, true,
            "enable unaligned accesses for the regexp engine")

// api.cc
DEFINE_BOOL(script_streaming, true, "enable parsing on background")
DEFINE_BOOL(stress_background_compile, false,
            "stress test parsing on background")
DEFINE_BOOL(concurrent_cache_deserialization, true,
            "enable deserializing code caches on background")
DEFINE_BOOL(
    merge_background_deserialized_script_with_compilation_cache, true,
    "After deserializing code cache data on a background thread, merge it into "
    "an existing Script if one is found in the Isolate compilation cache")
DEFINE_BOOL(verify_code_merge, false, "Verify scope infos after merge")
DEFINE_BOOL(
    embedder_instance_types, false,
    "enable type checks based on instance types provided by the embedder")

// bootstrapper.cc
DEFINE_BOOL(expose_gc, false, "expose gc extension")
DEFINE_STRING(expose_gc_as, nullptr,
              "expose gc extension under the specified name")
DEFINE_IMPLICATION(expose_gc_as, expose_gc)
DEFINE_BOOL(expose_externalize_string, false,
            "expose externalize string extension")
DEFINE_BOOL(expose_statistics, false, "expose statistics extension")
DEFINE_BOOL(expose_trigger_failure, false, "expose trigger-failure extension")
DEFINE_BOOL(expose_ignition_statistics, false,
            "expose ignition-statistics extension (requires building with "
            "v8_enable_ignition_dispatch_counting)")
DEFINE_INT(stack_trace_limit, 10, "number of stack frames to capture")
DEFINE_BOOL(builtins_in_stack_traces, false,
            "show built-in functions in stack traces")
DEFINE_BOOL(experimental_stack_trace_frames, false,
            "enable experimental frames (API/Builtins) and stack trace layout")
DEFINE_BOOL(disallow_code_generation_from_strings, false,
            "disallow eval and friends")
DEFINE_BOOL(expose_async_hooks, false, "expose async_hooks object")
DEFINE_STRING(expose_cputracemark_as, nullptr,
              "expose cputracemark extension under the specified name")
#ifdef ENABLE_VTUNE_TRACEMARK
DEFINE_BOOL(enable_vtune_domain_support, true, "enable vtune domain support")
#endif  // ENABLE_VTUNE_TRACEMARK

#ifdef ENABLE_VTUNE_JIT_INTERFACE
DEFINE_BOOL(enable_vtunejit, true, "enable vtune jit interface")
DEFINE_NEG_IMPLICATION(enable_vtunejit, compact_code_space)
#endif  // ENABLE_VTUNE_JIT_INTERFACE

DEFINE_BOOL(experimental_report_exceptions_from_callbacks, true,
            "Notify Api callback about exceptions thrown in Api callbacks")

// builtins.cc
DEFINE_BOOL(allow_unsafe_function_constructor, false,
            "allow invoking the function constructor without security checks")
DEFINE_BOOL(force_slow_path, false, "always take the slow path for builtins")
DEFINE_BOOL(test_small_max_function_context_stub_size, false,
            "enable testing the function context size overflow path "
            "by making the maximum size smaller")

DEFINE_BOOL(inline_new, true, "use fast inline allocation")
DEFINE_NEG_NEG_IMPLICATION(inline_new, turbo_allocation_folding)

// bytecode-generator.cc
DEFINE_INT(switch_table_spread_threshold, 3,
           "allow the jump table used for switch statements to span a range "
           "of integers roughly equal to this number times the number of "
           "clauses in the switch")
DEFINE_INT(switch_table_min_cases, 6,
           "the number of Smi integer cases present in the switch statement "
           "before using the jump table optimization")
// Note that enabling this stress mode might result in a failure to compile
// even a top-level code.
DEFINE_INT(stress_lazy_compilation, 0,
           "stress lazy compilation by simulating stack overflow during "
           "unoptimized bytecode generation with 1/n-th probability, "
           "do nothing on 0")

// codegen-ia32.cc / codegen-arm.cc
DEFINE_BOOL(trace, false, "trace javascript function calls")

// codegen.cc
DEFINE_BOOL(lazy, true, "use lazy compilation")
DEFINE_BOOL(lazy_eval, true, "use lazy compilation during eval")
DEFINE_BOOL(lazy_streaming, true,
            "use lazy compilation during streaming compilation")
DEFINE_BOOL(max_lazy, false, "ignore eager compilation hints")
DEFINE_IMPLICATION(max_lazy, lazy)
DEFINE_BOOL(trace_opt, false, "trace optimized compilation")
DEFINE_BOOL(trace_opt_verbose, false,
            "extra verbose optimized compilation tracing")
DEFINE_IMPLICATION(trace_opt_verbose, trace_opt)
DEFINE_BOOL(trace_opt_stats, false, "trace optimized compilation statistics")
DEFINE_BOOL(trace_deopt, false, "trace deoptimization")
DEFINE_BOOL(log_deopt, false, "log deoptimization")
DEFINE_BOOL(trace_deopt_verbose, false, "extra verbose deoptimization tracing")
DEFINE_IMPLICATION(trace_deopt_verbose, trace_deopt)
DEFINE_BOOL(trace_file_names, false,
            "include file names in trace-opt/trace-deopt output")
DEFINE_BOOL(always_turbofan, false, "always try to optimize functions")
DEFINE_IMPLICATION(always_turbofan, turbofan)
DEFINE_BOOL(always_osr, false, "always try to OSR functions")
DEFINE_BOOL(prepare_always_turbofan, false, "prepare for turning on always opt")
// On Arm64, every entry point in a function needs a BTI landing pad
// instruction. Deopting to baseline means every bytecode is a potential entry
// point, which increases codesize significantly.
DEFINE_BOOL(deopt_to_baseline, false,
            "deoptimize to baseline code when available")

DEFINE_BOOL(trace_serializer, false, "print code serializer trace")
#ifdef DEBUG
DEFINE_BOOL(external_reference_stats, false,
            "print statistics on external references used during serialization")
#endif  // DEBUG

// compilation-cache.cc
DEFINE_BOOL(compilation_cache, true, "enable compilation cache")

DEFINE_BOOL(cache_prototype_transitions, true, "cache prototype transitions")

// lazy-compile-dispatcher.cc
DEFINE_BOOL(lazy_compile_dispatcher, false, "enable compiler dispatcher")
DEFINE_UINT(lazy_compile_dispatcher_max_threads, 0,
            "max threads for compiler dispatcher (0 for unbounded)")
DEFINE_BOOL(trace_compiler_dispatcher, false,
            "trace compiler dispatcher activity")
DEFINE_BOOL(
    parallel_compile_tasks_for_eager_toplevel, false,
    "spawn parallel compile tasks for eagerly compiled, top-level functions")
DEFINE_IMPLICATION(parallel_compile_tasks_for_eager_toplevel,
                   lazy_compile_dispatcher)
DEFINE_BOOL(parallel_compile_tasks_for_lazy, false,
            "spawn parallel compile tasks for all lazily compiled functions")
DEFINE_IMPLICATION(parallel_compile_tasks_for_lazy, lazy_compile_dispatcher)

// cpu-profiler.cc
DEFINE_INT(cpu_profiler_sampling_interval, 1000,
           "CPU profiler sampling interval in microseconds")

// debugger
DEFINE_BOOL(
    trace_side_effect_free_debug_evaluate, false,
    "print debug messages for side-effect-free debug-evaluate for testing")
DEFINE_BOOL(hard_abort, true, "abort by crashing")
DEFINE_NEG_IMPLICATION(fuzzing, hard_abort)
DEFINE_NEG_IMPLICATION(hole_fuzzing, hard_abort)
DEFINE_NEG_IMPLICATION(sandbox_fuzzing, hard_abort)

// disassembler
DEFINE_BOOL(log_colour, ENABLE_LOG_COLOUR,
            "When logging, try to use coloured output.")

// inspector
DEFINE_BOOL(expose_inspector_scripts, false,
            "expose injected-script-source.js for debugging")

// execution.cc
DEFINE_INT(stack_size, V8_DEFAULT_STACK_SIZE_KB,
           "default size of stack region v8 is allowed to use (in kBytes)")

// frames.cc
DEFINE_INT(max_stack_trace_source_length, 300,
           "maximum length of function source code printed in a stack trace.")

// execution.cc, messages.cc
DEFINE_BOOL(clear_exceptions_on_js_entry, false,
            "clear exceptions when entering JavaScript")

DEFINE_BOOL(use_original_message_for_stack_trace, true,
            "use the message with which the Error constructor was called "
            "rather than the value of the \"message\" property for Error.stack")

// counters.cc
DEFINE_INT(histogram_interval, 600000,
           "time interval in ms for aggregating memory histograms")

// heap-snapshot-generator.cc
DEFINE_BOOL(heap_profiler_trace_objects, false,
            "Dump heap object allocations/movements/size_updates")
DEFINE_BOOL(heap_profiler_use_embedder_graph, true,
            "Use the new EmbedderGraph API to get embedder nodes")
DEFINE_BOOL(heap_snapshot_on_oom, false,
            "Write a heap snapshot to disk on last-resort GCs")
DEFINE_INT(heap_snapshot_on_gc, -1,
           "Write a heap snapshot to disk on a certain GC invocation")
DEFINE_INT(heap_snapshot_string_limit, 1024,
           "truncate strings to this length in the heap snapshot")
DEFINE_BOOL(heap_profiler_show_hidden_objects, false,
            "use 'native' rather than 'hidden' node type in snapshot")
DEFINE_BOOL(profile_heap_snapshot, false, "dump time spent on heap snapshot")
#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
DEFINE_BOOL(heap_snapshot_verify, false,
            "verify that heap snapshot matches marking visitor behavior")
DEFINE_IMPLICATION(enable_slow_asserts, heap_snapshot_verify)
#endif

// sampling-heap-profiler.cc
DEFINE_BOOL(sampling_heap_profiler_suppress_randomness, false,
            "Use constant sample intervals to eliminate test flakiness")

// ic.cc
DEFINE_BOOL(log_ic, false,
            "Log inline cache state transitions for tools/ic-processor")
DEFINE_IMPLICATION(log_ic, log_code)
DEFINE_GENERIC_IMPLICATION(
    log_ic, TracingFlags::ic_stats.store(
                v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_BOOL_READONLY(fast_map_update, false,
                     "enable fast map update by caching the migration target")
DEFINE_INT(max_valid_polymorphic_map_count, 4,
           "maximum number of valid maps to track in POLYMORPHIC state")
DEFINE_BOOL(
    clone_object_sidestep_transitions, false,
    "support sidestep transitions for dependency tracking object clone maps")
DEFINE_WEAK_IMPLICATION(future, clone_object_sidestep_transitions)

// map-inl.h
DEFINE_INT(fast_properties_soft_limit, 12,
           "limits the number of properties that can be added to an object "
           "using keyed store before transitioning to dictionary mode")
DEFINE_INT(max_fast_properties, 128,
           "limits the number of mutable properties that can be added to an "
           "object before transitioning to dictionary mode")

DEFINE_BOOL(native_code_counters, DEBUG_BOOL,
            "generate extra code for manipulating stats counters")

DEFINE_BOOL(super_ic, true, "use an IC for super property loads")

DEFINE_BOOL(mega_dom_ic, false, "use MegaDOM IC state for API objects")

// objects.cc
DEFINE_BOOL(trace_prototype_users, false,
            "Trace updates to prototype user tracking")
DEFINE_BOOL(trace_for_in_enumerate, false, "Trace for-in enumerate slow-paths")
DEFINE_BOOL(log_maps, false, "Log map creation")
DEFINE_BOOL(log_maps_details, true, "Also log map details")
DEFINE_IMPLICATION(log_maps, log_code)
DEFINE_BOOL(move_prototype_transitions_first, false,
            "Always move prototype transitions to the front of the tree")
DEFINE_WEAK_IMPLICATION(future, move_prototype_transitions_first)

// parser.cc
DEFINE_BOOL(allow_natives_syntax, false, "allow natives syntax")
DEFINE_BOOL(allow_natives_for_differential_fuzzing, false,
            "allow only natives explicitly allowlisted for differential "
            "fuzzers")
DEFINE_IMPLICATION(allow_natives_for_differential_fuzzing, allow_natives_syntax)
DEFINE_IMPLICATION(allow_natives_for_differential_fuzzing, fuzzing)
DEFINE_BOOL(parse_only, false, "only parse the sources")

// simulator-arm.cc and simulator-arm64.cc.
#ifdef USE_SIMULATOR
DEFINE_BOOL(trace_sim, false, "Trace simulator execution")
DEFINE_BOOL(debug_sim, false, "Enable debugging the simulator")
DEFINE_BOOL(check_icache, false,
            "Check icache flushes in ARM and MIPS simulator")
DEFINE_INT(stop_sim_at, 0, "Simulator stop after x number of instructions")
#if defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_MIPS64) ||  \
    defined(V8_TARGET_ARCH_PPC64) || defined(V8_TARGET_ARCH_RISCV64) || \
    defined(V8_TARGET_ARCH_LOONG64)
DEFINE_INT(sim_stack_alignment, 16,
           "Stack alignment in bytes in simulator. This must be a power of two "
           "and it must be at least 16. 16 is default.")
#else
DEFINE_INT(sim_stack_alignment, 8,
           "Stack alingment in bytes in simulator (4 or 8, 8 is default)")
#endif
DEFINE_INT(sim_stack_size, 2 * MB / KB,
           "Stack size of the ARM64, MIPS64 and PPC64 simulator "
           "in kBytes (default is 2 MB)")
DEFINE_BOOL(trace_sim_messages, false,
            "Trace simulator debug messages. Implied by --trace-sim.")
#endif  // USE_SIMULATOR

#if defined V8_TARGET_ARCH_ARM64
// pointer-auth-arm64.cc
DEFINE_BOOL(sim_abort_on_bad_auth, true,
            "Stop execution when a pointer authentication fails in the "
            "ARM64 simulator.")
#endif

// isolate.cc
DEFINE_BOOL(async_stack_traces, true,
            "include async stack traces in Error.stack")
DEFINE_BOOL(stack_trace_on_illegal, false,
            "print stack trace when an illegal exception is thrown")
DEFINE_BOOL(abort_on_uncaught_exception, false,
            "abort program (dump core) when an uncaught exception is thrown")
DEFINE_BOOL(correctness_fuzzer_suppressions, false,
            "Suppress certain unspecified behaviors to ease correctness "
            "fuzzing: Abort program when the stack overflows or a string "
            "exceeds maximum length (as opposed to throwing RangeError). "
            "Use a fixed suppression string for error messages.")
DEFINE_BOOL(rehash_snapshot, false,
            "rehash strings from the snapshot to override the baked-in seed")
DEFINE_UINT64(hash_seed, 0,
              "Fixed seed to use to hash property keys (0 means random)"
              "(with snapshots this option cannot override the baked-in seed)")
DEFINE_INT(random_seed, 0,
           "Default seed for initializing random generator "
           "(0, the default, means to use system random).")
DEFINE_INT(fuzzer_random_seed, 0,
           "Default seed for initializing fuzzer random generator "
           "(0, the default, means to use v8's random number generator seed).")
DEFINE_BOOL(trace_rail, false, "trace RAIL mode")
DEFINE_BOOL(print_all_exceptions, false,
            "print exception object and stack trace on each thrown exception")
DEFINE_BOOL(
    detailed_error_stack_trace, false,
    "includes arguments for each function call in the error stack frames array")
DEFINE_BOOL(adjust_os_scheduling_parameters, true,
            "adjust OS specific scheduling params for the isolate")
DEFINE_BOOL(experimental_flush_embedded_blob_icache, true,
            "Used in an experiment to evaluate icache flushing on certain CPUs")
DEFINE_BOOL(allow_allocation_in_fast_api_call, true,
            "Allow allocations in fast API calls.")

// Flags for short builtin calls feature
#if V8_SHORT_BUILTIN_CALLS
#define V8_SHORT_BUILTIN_CALLS_BOOL true
#else
#define V8_SHORT_BUILTIN_CALLS_BOOL false
#endif

DEFINE_BOOL(short_builtin_calls, V8_SHORT_BUILTIN_CALLS_BOOL,
            "Put embedded builtins code into the code range for shorter "
            "builtin calls/jumps if system has >=4GB memory")
DEFINE_BOOL(trace_code_range_allocation, false,
            "Trace code range allocation process.")

#ifdef V8_TARGET_OS_CHROMEOS
#define V8_TARGET_OS_CHROMEOS_BOOL true
#else
#define V8_TARGET_OS_CHROMEOS_BOOL false
#endif  // V8_TARGET_OS_CHROMEOS

// TODO(1417652): Enable on ChromeOS once the issue is fixed.
DEFINE_BOOL(
    better_code_range_allocation,
    V8_EXTERNAL_CODE_SPACE_BOOL&& COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL &&
        !V8_TARGET_OS_CHROMEOS_BOOL,
    "This mode tries harder to allocate code range near .text section. "
    "Works only for configurations with external code space and "
    "shared pointer compression cage.")
DEFINE_BOOL(abort_on_far_code_range, false,
            "Abort if code range is allocated further away than 4GB from the"
            ".text section")

// runtime.cc
DEFINE_BOOL(runtime_call_stats, false, "report runtime call counts and times")
DEFINE_GENERIC_IMPLICATION(
    runtime_call_stats,
    TracingFlags::runtime_stats.store(
        v8::tracing::TracingCategoryObserver::ENABLED_BY_NATIVE))
DEFINE_BOOL(rcs, false, "report runtime call counts and times")
DEFINE_IMPLICATION(rcs, runtime_call_stats)

DEFINE_BOOL(rcs_cpu_time, false,
            "report runtime times in cpu time (the default is wall time)")
DEFINE_IMPLICATION(rcs_cpu_time, rcs)

// snapshot-common.cc
DEFINE_BOOL(verify_snapshot_checksum, DEBUG_BOOL,
            "Verify snapshot checksums when deserializing snapshots. Enable "
            "checksum creation and verification for code caches. Enabled by "
            "default in debug builds and once per process for Android.")
DEFINE_BOOL(profile_deserialization, false,
            "Print the time it takes to deserialize the snapshot.")
DEFINE_BOOL(serialization_statistics, false,
            "Collect statistics on serialized objects.")
// Regexp
DEFINE_BOOL(regexp_optimization, true, "generate optimized regexp code")
DEFINE_BOOL(regexp_interpret_all, false, "interpret all regexp code")
#ifdef V8_TARGET_BIG_ENDIAN
#define REGEXP_PEEPHOLE_OPTIMIZATION_BOOL false
#else
#define REGEXP_PEEPHOLE_OPTIMIZATION_BOOL true
#endif
DEFINE_BOOL(regexp_tier_up, true,
            "enable regexp interpreter and tier up to the compiler after the "
            "number of executions set by the tier up ticks flag")
DEFINE_NEG_IMPLICATION(regexp_interpret_all, regexp_tier_up)
DEFINE_INT(regexp_tier_up_ticks, 1,
           "set the number of executions for the regexp interpreter before "
           "tiering-up to the compiler")
DEFINE_BOOL(regexp_peephole_optimization, REGEXP_PEEPHOLE_OPTIMIZATION_BOOL,
            "enable peephole optimization for regexp bytecode")
DEFINE_BOOL(trace_regexp_peephole_optimization, false,
            "trace regexp bytecode peephole optimization")
DEFINE_BOOL(trace_regexp_bytecodes, false, "trace regexp bytecode execution")
DEFINE_BOOL(trace_regexp_assembler, false,
            "trace regexp macro assembler calls.")
DEFINE_BOOL(trace_regexp_parser, false, "trace regexp parsing")
DEFINE_BOOL(trace_regexp_tier_up, false, "trace regexp tiering up execution")
DEFINE_BOOL(trace_regexp_graph, false, "trace the regexp graph")

DEFINE_BOOL(enable_experimental_regexp_engine, false,
            "recognize regexps with 'l' flag, run them on experimental engine")
DEFINE_BOOL(default_to_experimental_regexp_engine, false,
            "run regexps with the experimental engine where possible")
DEFINE_IMPLICATION(default_to_experimental_regexp_engine,
                   enable_experimental_regexp_engine)
DEFINE_BOOL(experimental_regexp_engine_capture_group_opt, false,
            "enable time optimizations for the experimental regexp engine")
DEFINE_IMPLICATION(experimental_regexp_engine_capture_group_opt,
                   enable_experimental_regexp_engine)
DEFINE_UINT64(experimental_regexp_engine_capture_group_opt_max_memory_usage,
              1024,
              "maximum memory usage in MB allowed for experimental engine")
DEFINE_BOOL(trace_experimental_regexp_engine, false,
            "trace execution of experimental regexp engine")

DEFINE_BOOL(enable_experimental_regexp_engine_on_excessive_backtracks, false,
            "fall back to a breadth-first regexp engine on excessive "
            "backtracking")
DEFINE_UINT(regexp_backtracks_before_fallback, 50000,
            "number of backtracks during regexp execution before fall back "
            "to experimental engine if "
            "enable_experimental_regexp_engine_on_excessive_backtracks is set")

DEFINE_BOOL(trace_read_only_promotion, false,
            "trace the read-only promotion pass")
DEFINE_BOOL(trace_read_only_promotion_verbose, false,
            "trace the read-only promotion pass")
DEFINE_WEAK_IMPLICATION(trace_read_only_promotion_verbose,
                        trace_read_only_promotion)

// Testing flags test/cctest/test-{flags,api,serialization}.cc
DEFINE_BOOL(testing_bool_flag, true, "testing_bool_flag")
DEFINE_MAYBE_BOOL(testing_maybe_bool_flag, "testing_maybe_bool_flag")
DEFINE_INT(testing_int_flag, 13, "testing_int_flag")
DEFINE_FLOAT(testing_float_flag, 2.5, "float-flag")
DEFINE_STRING(testing_string_flag, "Hello, world!", "string-flag")
DEFINE_INT(testing_prng_seed, 42, "Seed used for threading test randomness")

// Test flag for a check in %OptimizeFunctionOnNextCall
DEFINE_BOOL(
    testing_d8_test_runner, false,
    "test runner turns on this flag to enable a check that the function was "
    "prepared for optimization before marking it for optimization")

DEFINE_EXPERIMENTAL_FEATURE(
    strict_termination_checks,
    "Enable strict terminating DCHECKs to prevent accidentally "
    "keeping on executing JS after terminating V8.")

DEFINE_BOOL(
    fuzzing, false,
    "Fuzzers use this flag to signal that they are ... fuzzing. This causes "
    "intrinsics to fail silently (e.g. return undefined) on invalid usage.")

// When fuzzing, always compile functions twice and ensure that the generated
// bytecode is the same. This can help find bugs such as crbug.com/1394403 as it
// avoids the need for bytecode aging to kick in to trigger the recomplication.
DEFINE_WEAK_NEG_IMPLICATION(fuzzing, lazy)
DEFINE_WEAK_IMPLICATION(fuzzing, stress_lazy_source_positions)

DEFINE_BOOL(
    hole_fuzzing, false,
    "Fuzzers use this flag to turn DCHECKs into NOPs  and CHECK failures into "
    "silent exits. This is useful if we want to find memory corruption "
    "primitives with a leaked hole, where the engine is already in a weird "
    "state")

//
// Sandbox-related flags.
//
#ifdef V8_ENABLE_SANDBOX
DEFINE_BOOL(sandbox_testing, false,
            "Enable sandbox testing mode. Useful for demonstrating and "
            "validating sandbox bypasses. This exposes the memory corruption "
            "API and enables the sandbox crash filter to filter out crashes "
            "that do not represent a sandbox bypass")
#else
DEFINE_BOOL_READONLY(
    sandbox_testing, false,
    "Enable sandbox testing mode. Useful for demonstrating and validating "
    "sandbox bypasses. This exposes the memory corruption API and enables the "
    "sandbox crash filter to filter out crashes that do not represent a "
    "sandbox bypass")
#endif

#ifdef V8_ENABLE_MEMORY_CORRUPTION_API
// Sandbox fuzzing mode requires the memory corruption API.
DEFINE_BOOL(sandbox_fuzzing, false,
            "Enable sandbox fuzzing mode. This exposes the memory corruption "
            "API and enables the sandbox crash filter, causing all crashes "
            "that are not sandbox violations to be ignored.")
#else
DEFINE_BOOL_READONLY(
    sandbox_fuzzing, false,
    "Enable sandbox fuzzing mode. This exposes the memory corruption API and "
    "enables the sandbox crash filter, causing all crashes that are not "
    "sandbox violations to be ignored.")
#endif

// Only one of these can be enabled.
DEFINE_NEG_IMPLICATION(sandbox_fuzzing, sandbox_testing)
DEFINE_NEG_IMPLICATION(sandbox_testing, sandbox_fuzzing)

#if defined(V8_OS_AIX) && defined(COMPONENT_BUILD)
// FreezeFlags relies on mprotect() method, which does not work by default on
// shared mem: https://www.ibm.com/docs/en/aix/7.2?topic=m-mprotect-subroutine
DEFINE_BOOL(freeze_flags_after_init, false,
            "Disallow changes to flag values after initializing V8")
#else
DEFINE_BOOL(freeze_flags_after_init, true,
            "Disallow changes to flag values after initializing V8")
#endif  // defined(V8_OS_AIX) && defined(COMPONENT_BUILD)

// mksnapshot.cc
DEFINE_STRING(embedded_src, nullptr,
              "Path for the generated embedded data file. (mksnapshot only)")
DEFINE_STRING(
    embedded_variant, nullptr,
    "Label to disambiguate symbols in embedded data file. (mksnapshot only)")
#if V8_STATIC_ROOTS_GENERATION_BOOL
DEFINE_STRING(static_roots_src, nullptr,
              "Path for writing a fresh static-roots.h. (mksnapshot only, "
              "build without static roots only)")
#endif
DEFINE_STRING(startup_src, nullptr,
              "Write V8 startup as C++ src. (mksnapshot only)")
DEFINE_STRING(startup_blob, nullptr,
              "Write V8 startup blob file. (mksnapshot only)")
DEFINE_STRING(target_arch, nullptr,
              "The mksnapshot target arch. (mksnapshot only)")
DEFINE_STRING(target_os, nullptr, "The mksnapshot target os. (mksnapshot only)")
DEFINE_BOOL(target_is_simulator, false,
            "Instruct mksnapshot that the target is meant to run in the "
            "simulator and it can generate simulator-specific instructions. "
            "(mksnapshot only)")
DEFINE_STRING(turbo_profiling_input, nullptr,
              "Path of the input file containing basic information for "
              "builtins. (mksnapshot only)")
DEFINE_STRING(turbo_log_builtins_count_input, nullptr,
              "Path of the input file containing basic block counters for "
              "builtins for logging in turbolizer. (mksnapshot only)")

// On some platforms, the .text section only has execute permissions.
DEFINE_BOOL(text_is_readable, true,
            "Whether the .text section of binary can be read")
DEFINE_NEG_NEG_IMPLICATION(text_is_readable, partial_constant_pool)

//
// Minor mark sweep collector flags.
//
DEFINE_BOOL(trace_minor_ms_parallel_marking, false,
            "trace parallel marking for the young generation")
DEFINE_BOOL(minor_ms, false, "perform young generation mark sweep GCs")
DEFINE_IMPLICATION(minor_ms, separate_gc_phases)
DEFINE_IMPLICATION(minor_ms, page_promotion)

DEFINE_BOOL(concurrent_minor_ms_marking, true,
            "perform young generation marking concurrently")
DEFINE_NEG_NEG_IMPLICATION(concurrent_marking, concurrent_minor_ms_marking)

#ifdef V8_ENABLE_STICKY_MARK_BITS
#define V8_ENABLE_STICKY_MARK_BITS_BOOL true
#else
#define V8_ENABLE_STICKY_MARK_BITS_BOOL false
#endif
DEFINE_BOOL_READONLY(sticky_mark_bits, V8_ENABLE_STICKY_MARK_BITS_BOOL,
                     "use sticky mark bits for separation of generations")
DEFINE_IMPLICATION(sticky_mark_bits, minor_ms)
// TODO(333906585): Copy mark bits and live bytes on compaction.
DEFINE_NEG_IMPLICATION(sticky_mark_bits, compact)

#ifndef DEBUG
#define V8_MINOR_MS_CONCURRENT_MARKING_MIN_CAPACITY_DEFAULT 8
#else
#define V8_MINOR_MS_CONCURRENT_MARKING_MIN_CAPACITY_DEFAULT 0
#endif

DEFINE_UINT(minor_ms_min_new_space_capacity_for_concurrent_marking_mb,
            V8_MINOR_MS_CONCURRENT_MARKING_MIN_CAPACITY_DEFAULT,
            "min new space capacity in MBs for using young generation "
            "concurrent marking.")

DEFINE_UINT(minor_ms_concurrent_marking_trigger, 90,
            "minor ms concurrent marking trigger in percent of the current new "
            "space capacity")

DEFINE_SIZE_T(minor_ms_min_lab_size_kb, 0,
              "override for the minimum lab size in KB to be used for new "
              "space allocations with minor ms. ")

//
// Dev shell flags
//

DEFINE_BOOL(help, false, "Print usage message, including flags, on console")
DEFINE_BOOL(print_flag_values, false, "Print all flag values of V8")

// Slow histograms are also enabled via --dump-counters in d8.
DEFINE_BOOL(slow_histograms, false,
            "Enable slow histograms with more overhead.")

DEFINE_BOOL(use_external_strings, false, "Use external strings for source code")
DEFINE_STRING(map_counters, "", "Map counters to a file")
DEFINE_BOOL(mock_arraybuffer_allocator, false,
            "Use a mock ArrayBuffer allocator for testing.")
DEFINE_SIZE_T(mock_arraybuffer_allocator_limit, 0,
              "Memory limit for mock ArrayBuffer allocator used to simulate "
              "OOM for testing.")
#ifdef V8_OS_LINUX
DEFINE_BOOL(multi_mapped_mock_allocator, false,
            "Use a multi-mapped mock ArrayBuffer allocator for testing.")
#endif

//
// GDB JIT integration flags.
//
#undef FLAG
#ifdef ENABLE_GDB_JIT_INTERFACE
#define FLAG FLAG_FULL
#else
#define FLAG FLAG_READONLY
#endif

DEFINE_BOOL(gdbjit, false, "enable GDBJIT interface")
DEFINE_BOOL(gdbjit_full, false, "enable GDBJIT interface for all code objects")
DEFINE_BOOL(gdbjit_dump, false, "dump elf objects with debug info to disk")
DEFINE_STRING(gdbjit_dump_filter, "",
              "dump only objects containing this substring")

#ifdef ENABLE_GDB_JIT_INTERFACE
DEFINE_IMPLICATION(gdbjit_full, gdbjit)
DEFINE_IMPLICATION(gdbjit_dump, gdbjit)
#endif
DEFINE_NEG_IMPLICATION(gdbjit, compact_code_space)

//
// Debug only flags
//
#undef FLAG
#ifdef DEBUG
#define FLAG FLAG_FULL
#else
#define FLAG FLAG_READONLY
#endif

#ifdef ENABLE_SLOW_DCHECKS
DEFINE_BOOL(enable_slow_asserts, true,
            "enable asserts that are slow to execute")
#else
DEFINE_BOOL_READONLY(enable_slow_asserts, false,
                     "enable asserts that are slow to execute")
#endif

// codegen-ia32.cc / codegen-arm.cc / macro-assembler-*.cc
DEFINE_BOOL(print_ast, false, "print source AST")
DEFINE_BOOL(trap_on_abort, false, "replace aborts by breakpoints")

// compiler.cc
DEFINE_BOOL(print_scopes, false, "print scopes")

// contexts.cc
DEFINE_BOOL(trace_contexts, false, "trace contexts operations")

// heap.cc
DEFINE_BOOL(gc_verbose, false, "print stuff during garbage collection")
DEFINE_BOOL(code_stats, false, "report code statistics after GC")
DEFINE_BOOL(print_handles, false, "report handles after GC")
DEFINE_BOOL(check_handle_count, false,
            "Check that there are not too many handles at GC")
DEFINE_BOOL(print_global_handles, false, "report global handles after GC")

// TurboFan debug-only flags.
DEFINE_BOOL(trace_turbo_escape, false, "enable tracing in escape analysis")

// objects.cc
DEFINE_BOOL(trace_module_status, false,
            "Trace status transitions of ECMAScript modules")
DEFINE_BOOL(trace_normalization, false,
            "prints when objects are turned into dictionaries.")

// runtime.cc
DEFINE_BOOL(trace_lazy, false, "trace lazy compilation")

// spaces.cc
DEFINE_BOOL(trace_isolates, false, "trace isolate state changes")

// Regexp
DEFINE_BOOL(regexp_possessive_quantifier, false,
            "enable possessive quantifier syntax for testing")

// Debugger
DEFINE_BOOL(print_break_location, false, "print source location on debug break")

//
// Logging and profiling flags
//
// Logging flag dependencies are also set separately in
// V8::InitializeOncePerProcessImpl. Please add your flag to the log_all_flags
// list in v8.cc to properly set v8_flags.log and automatically enable it with
// --log-all.
#undef FLAG
#define FLAG FLAG_FULL

// log.cc
DEFINE_STRING(logfile, "v8.log",
              "Specify the name of the log file, use '-' for console, '+' for "
              "a temporary file.")
DEFINE_BOOL(logfile_per_isolate, true, "Separate log files for each isolate.")

DEFINE_BOOL(log, false,
            "Minimal logging (no API, code, GC, suspect, or handles samples).")
DEFINE_BOOL(log_all, false, "Log all events to the log file.")

DEFINE_BOOL(log_internal_timer_events, false, "See --log-timer-events")
DEFINE_BOOL(log_timer_events, false,
            "Log timer events (incl. console.time* and Date.now).")

DEFINE_BOOL(log_source_code, false, "Log source code.")
DEFINE_BOOL(log_source_position, false, "Log detailed source information.")
DEFINE_BOOL(log_code, false,
            "Log code events to the log file without profiling.")
DEFINE_WEAK_IMPLICATION(log_code, log_source_code)
DEFINE_WEAK_IMPLICATION(log_code, log_source_position)
DEFINE_BOOL(log_feedback_vector, false, "Log FeedbackVectors on first creation")
DEFINE_BOOL(log_code_disassemble, false,
            "Log all disassembled code to the log file.")
DEFINE_IMPLICATION(log_code_disassemble, log_code)
DEFINE_BOOL(log_function_events, false,
            "Log function events "
            "(parse, compile, execute) separately.")

DEFINE_BOOL(detailed_line_info, false,
            "Always generate detailed line information for CPU profiling.")

DEFINE_BOOL(perfetto_code_logger, false,
            "Enable the Perfetto code data source.")

#if defined(ANDROID)
// Phones and tablets have processors that are much slower than desktop
// and laptop computers for which current heuristics are tuned.
#define DEFAULT_PROF_SAMPLING_INTERVAL 5000
#else
#define DEFAULT_PROF_SAMPLING_INTERVAL 1000
#endif
DEFINE_INT(prof_sampling_interval, DEFAULT_PROF_SAMPLING_INTERVAL,
           "Interval for --prof samples (in microseconds).")
#undef DEFAULT_PROF_SAMPLING_INTERVAL

DEFINE_BOOL(prof_cpp, false, "Like --prof, but ignore generated code.")
DEFINE_BOOL(prof_browser_mode, true,
            "Used with --prof, turns on browser-compatible mode for profiling.")

DEFINE_BOOL(prof, false,
            "Log statistical profiling information (implies --log-code).")
DEFINE_IMPLICATION(prof, prof_cpp)
DEFINE_IMPLICATION(prof, log_code)

DEFINE_BOOL(ll_prof, false, "Enable low-level linux profiler.")

#if V8_OS_LINUX
#define DEFINE_PERF_PROF_BOOL(nam, cmt) DEFINE_BOOL(nam, false, cmt)
#define DEFINE_PERF_PROF_IMPLICATION DEFINE_IMPLICATION
#else
#define DEFINE_PERF_PROF_BOOL(nam, cmt) DEFINE_BOOL_READONLY(nam, false, cmt)
#define DEFINE_PERF_PROF_IMPLICATION(...)
#endif

#if defined(ANDROID)
#define DEFAULT_PERF_BASIC_PROF_PATH "/data/local/tmp"
#define DEFAULT_PERF_PROF_PATH DEFAULT_PERF_BASIC_PROF_PATH
#else
#define DEFAULT_PERF_BASIC_PROF_PATH "/tmp"
#define DEFAULT_PERF_PROF_PATH "."
#endif

DEFINE_PERF_PROF_BOOL(perf_basic_prof,
                      "Enable perf linux profiler (basic support).")
DEFINE_NEG_IMPLICATION(perf_basic_prof, compact_code_space)
DEFINE_STRING(perf_basic_prof_path, DEFAULT_PERF_BASIC_PROF_PATH,
              "directory to write perf-<pid>.map symbol file to")
DEFINE_PERF_PROF_BOOL(
    perf_basic_prof_only_functions,
    "Only report function code ranges to perf (i.e. no stubs).")
DEFINE_PERF_PROF_IMPLICATION(perf_basic_prof_only_functions, perf_basic_prof)

DEFINE_PERF_PROF_BOOL(
    perf_prof, "Enable perf linux profiler (experimental annotate support).")
DEFINE_STRING(perf_prof_path, DEFAULT_PERF_PROF_PATH,
              "directory to write jit-<pid>.dump symbol file to")
DEFINE_PERF_PROF_BOOL(
    perf_prof_annotate_wasm,
    "Used with --perf-prof, load wasm source map and provide annotate "
    "support (experimental).")
DEFINE_PERF_PROF_BOOL(
    perf_prof_delete_file,
    "Remove the perf file right after creating it (for testing only).")
DEFINE_NEG_IMPLICATION(perf_prof, compact_code_space)

// --perf-prof-unwinding-info is available only on selected architectures.
#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_X64 || \
    V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_PPC64
DEFINE_PERF_PROF_BOOL(
    perf_prof_unwinding_info,
    "Enable unwinding info for perf linux profiler (experimental).")
DEFINE_PERF_PROF_IMPLICATION(perf_prof, perf_prof_unwinding_info)
#else
DEFINE_BOOL_READONLY(
    perf_prof_unwinding_info, false,
    "Enable unwinding info for perf linux profiler (experimental).")
#endif

#undef DEFINE_PERF_PROF_BOOL
#undef DEFINE_PERF_PROF_IMPLICATION

DEFINE_STRING(gc_fake_mmap, "/tmp/__v8_gc__",
              "Specify the name of the file for fake gc mmap used in ll_prof")

DEFINE_BOOL(redirect_code_traces, false,
            "output deopt information and disassembly into file "
            "code-<pid>-<isolate id>.asm")
DEFINE_STRING(redirect_code_traces_to, nullptr,
              "output deopt information and disassembly into the given file")

DEFINE_BOOL(print_opt_source, false,
            "print source code of optimized and inlined functions")

DEFINE_BOOL(vtune_prof_annotate_wasm, false,
            "Used when v8_enable_vtunejit is enabled, load wasm source map and "
            "provide annotate support (experimental).")

DEFINE_BOOL(win64_unwinding_info, true, "Enable unwinding info for Windows/x64")

DEFINE_BOOL(interpreted_frames_native_stack, false,
            "Show interpreted frames on the native stack (useful for external "
            "profilers).")

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
DEFINE_BOOL(enable_etw_stack_walking, false,
            "Enable etw stack walking for windows")
DEFINE_WEAK_IMPLICATION(future, enable_etw_stack_walking)
#else
DEFINE_BOOL_READONLY(enable_etw_stack_walking, false,
                     "Enable etw stack walking for windows")
#endif

//
// Disassembler only flags
//
#undef FLAG
#ifdef ENABLE_DISASSEMBLER
#define FLAG FLAG_FULL
#else
#define FLAG FLAG_READONLY
#endif

// elements.cc
DEFINE_BOOL(trace_elements_transitions, false, "trace elements transitions")

DEFINE_BOOL(trace_creation_allocation_sites, false,
            "trace the creation of allocation sites")

DEFINE_BOOL(print_code, false, "print generated code")
DEFINE_BOOL(print_opt_code, false, "print optimized code")
DEFINE_STRING(print_opt_code_filter, "*", "filter for printing optimized code")
DEFINE_BOOL(print_code_verbose, false, "print more information for code")
DEFINE_BOOL(print_builtin_code, false, "print generated code for builtins")
DEFINE_STRING(print_builtin_code_filter, "*",
              "filter for printing builtin code")
DEFINE_BOOL(print_regexp_code, false, "print generated regexp code")
DEFINE_BOOL(print_regexp_bytecode, false, "print generated regexp bytecode")
DEFINE_BOOL(print_builtin_size, false, "print code size for builtins")

#ifdef ENABLE_DISASSEMBLER
DEFINE_BOOL(print_all_code, false, "enable all flags related to printing code")
DEFINE_IMPLICATION(print_all_code, print_code)
DEFINE_IMPLICATION(print_all_code, print_opt_code)
DEFINE_IMPLICATION(print_all_code, print_code_verbose)
DEFINE_IMPLICATION(print_all_code, print_builtin_code)
DEFINE_IMPLICATION(print_all_code, print_regexp_code)
#endif

#undef FLAG
#define FLAG FLAG_FULL

//
// Predictable mode related flags.
//

DEFINE_BOOL(predictable, false, "enable predictable mode")
DEFINE_NEG_IMPLICATION(predictable, memory_reducer)
// TODO(v8:11848): These flags were recursively implied via --single-threaded
// before. Audit them, and remove any unneeded implications.
DEFINE_IMPLICATION(predictable, single_threaded_gc)
DEFINE_NEG_IMPLICATION(predictable, concurrent_recompilation)
DEFINE_NEG_IMPLICATION(predictable, stress_concurrent_inlining)
DEFINE_NEG_IMPLICATION(predictable, lazy_compile_dispatcher)
DEFINE_NEG_IMPLICATION(predictable, parallel_compile_tasks_for_eager_toplevel)
DEFINE_NEG_IMPLICATION(predictable, parallel_compile_tasks_for_lazy)
#ifdef V8_ENABLE_MAGLEV
DEFINE_NEG_IMPLICATION(predictable, maglev_deopt_data_on_background)
DEFINE_NEG_IMPLICATION(predictable, maglev_build_code_on_background)
#endif  // V8_ENABLE_MAGLEV
// Avoid random seeds in predictable mode.
DEFINE_BOOL(predictable_and_random_seed_is_0, true,
            "predictable && (random_seed == 0)")
DEFINE_NEG_NEG_IMPLICATION(predictable, predictable_and_random_seed_is_0)
DEFINE_NEG_VALUE_VALUE_IMPLICATION(random_seed, 0,
                                   predictable_and_random_seed_is_0, false)
DEFINE_VALUE_IMPLICATION(predictable_and_random_seed_is_0, random_seed, 12347)

DEFINE_BOOL(predictable_gc_schedule, false,
            "Predictable garbage collection schedule. Fixes heap growing, "
            "idle, and memory reducing behavior.")
DEFINE_VALUE_IMPLICATION(predictable_gc_schedule, min_semi_space_size,
                         size_t{4})
DEFINE_VALUE_IMPLICATION(predictable_gc_schedule, max_semi_space_size,
                         size_t{4})
DEFINE_VALUE_IMPLICATION(predictable_gc_schedule, heap_growing_percent, 30)
DEFINE_NEG_IMPLICATION(predictable_gc_schedule, memory_reducer)

//
// Threading related flags.
//

DEFINE_BOOL(single_threaded, false, "disable the use of background tasks")
DEFINE_IMPLICATION(single_threaded, single_threaded_gc)
DEFINE_NEG_IMPLICATION(single_threaded, concurrent_recompilation)
DEFINE_NEG_IMPLICATION(single_threaded, stress_concurrent_inlining)
DEFINE_NEG_IMPLICATION(single_threaded, lazy_compile_dispatcher)
DEFINE_NEG_IMPLICATION(single_threaded,
                       parallel_compile_tasks_for_eager_toplevel)
DEFINE_NEG_IMPLICATION(single_threaded, parallel_compile_tasks_for_lazy)
#ifdef V8_ENABLE_MAGLEV
DEFINE_NEG_IMPLICATION(single_threaded, maglev_deopt_data_on_background)
DEFINE_NEG_IMPLICATION(single_threaded, maglev_build_code_on_background)
#endif  // V8_ENABLE_MAGLEV

//
// Parallel and concurrent GC (Orinoco) related flags.
//
DEFINE_BOOL(single_threaded_gc, false, "disable the use of background gc tasks")
DEFINE_NEG_IMPLICATION(single_threaded_gc, concurrent_marking)
DEFINE_NEG_IMPLICATION(single_threaded_gc, concurrent_sweeping)
DEFINE_NEG_IMPLICATION(single_threaded_gc, parallel_compaction)
DEFINE_NEG_IMPLICATION(single_threaded_gc, parallel_marking)
DEFINE_NEG_IMPLICATION(single_threaded_gc, parallel_pointer_update)
DEFINE_NEG_IMPLICATION(single_threaded_gc, parallel_weak_ref_clearing)
DEFINE_NEG_IMPLICATION(single_threaded_gc, parallel_scavenge)
DEFINE_NEG_IMPLICATION(single_threaded_gc, concurrent_array_buffer_sweeping)
DEFINE_NEG_IMPLICATION(single_threaded_gc, stress_concurrent_allocation)
DEFINE_NEG_IMPLICATION(single_threaded_gc, cppheap_concurrent_marking)

DEFINE_BOOL(single_threaded_gc_in_background, false,
            "disable the use of background gc tasks when in background")
DEFINE_BOOL(parallel_pause_for_gc_in_background, false,
            "Use parallel threads in the atomic pause for background GCs")
DEFINE_BOOL(incremental_marking_for_gc_in_background, true,
            "Use parallel threads in the atomic pause for background GCs")

DEFINE_BOOL(update_allocation_limits_after_loading, false,
            "force recomputation of allocation limites when leaving the "
            "loading RAIL mode (either on a RAIL mode change or incremental "
            "marking start).")

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
DEFINE_BOOL(use_libm_trig_functions, true, "use libm trig functions")
#endif

#undef FLAG

#ifdef VERIFY_PREDICTABLE
#define FLAG FLAG_FULL
#else
#define FLAG FLAG_READONLY
#endif

DEFINE_BOOL(verify_predictable, false,
            "this mode is used for checking that V8 behaves predictably")
DEFINE_IMPLICATION(verify_predictable, predictable)
DEFINE_INT(dump_allocations_digest_at_alloc, -1,
           "dump allocations digest each n-th allocation")

#define LOG_FLAGS(V)      \
  V(log_code)             \
  V(log_code_disassemble) \
  V(log_deopt)            \
  V(log_feedback_vector)  \
  V(log_function_events)  \
  V(log_ic)               \
  V(log_maps)             \
  V(log_source_code)      \
  V(log_source_position)  \
  V(log_timer_events)     \
  V(prof)                 \
  V(prof_cpp)

#define SET_IMPLICATIONS(V)      \
  DEFINE_IMPLICATION(log_all, V) \
  DEFINE_IMPLICATION(V, log)

LOG_FLAGS(SET_IMPLICATIONS)

#undef SET_IMPLICATIONS
#undef LOG_FLAGS

DEFINE_IMPLICATION(log_all, log)
DEFINE_IMPLICATION(perf_prof, log)
DEFINE_IMPLICATION(perf_basic_prof, log)
DEFINE_IMPLICATION(ll_prof, log)
DEFINE_IMPLICATION(gdbjit, log)

// Cleanup...
#undef FLAG_FULL
#undef FLAG_READONLY
#undef FLAG
#undef FLAG_ALIAS

#undef DEFINE_BOOL
#undef DEFINE_MAYBE_BOOL
#undef DEFINE_DEBUG_BOOL
#undef DEFINE_INT
#undef DEFINE_STRING
#undef DEFINE_FLOAT
#undef DEFINE_IMPLICATION
#undef DEFINE_WEAK_IMPLICATION
#undef DEFINE_NEG_IMPLICATION
#undef DEFINE_NEG_IMPLICATION_WITH_WARNING
#undef DEFINE_WEAK_NEG_IMPLICATION
#undef DEFINE_NEG_NEG_IMPLICATION
#undef DEFINE_NEG_VALUE_IMPLICATION
#undef DEFINE_NEG_VALUE_VALUE_IMPLICATION
#undef DEFINE_VALUE_IMPLICATION
#undef DEFINE_MIN_VALUE_IMPLICATION
#undef DEFINE_DISABLE_FLAG_IMPLICATION
#undef DEFINE_WEAK_VALUE_IMPLICATION
#undef DEFINE_GENERIC_IMPLICATION
#undef DEFINE_ALIAS_BOOL
#undef DEFINE_ALIAS_INT
#undef DEFINE_ALIAS_STRING
#undef DEFINE_ALIAS_FLOAT

#undef FLAG_MODE_DECLARE
#undef FLAG_MODE_DEFINE_DEFAULTS
#undef FLAG_MODE_META
#undef FLAG_MODE_DEFINE_IMPLICATIONS
#undef FLAG_MODE_APPLY
