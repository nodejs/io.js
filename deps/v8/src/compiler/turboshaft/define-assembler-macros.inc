// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

// This file defines Turboshaft's assembler macros. Include this file before
// your reducers and don't forget to include 'undef-assembler-macros.inc'
// afterwards.

#ifdef V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros already defined. Did you forget to #include \"undef-assembler-macros.inc\" in a previous file?"
#endif

#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED 1

#define LIKELY(...) ConditionWithHint(__VA_ARGS__, BranchHint::kTrue)
#define UNLIKELY(...) ConditionWithHint(__VA_ARGS__, BranchHint::kFalse)

#define BIND(label, ...)                                                  \
  auto [CONCAT(is_bound_, __LINE__), ##__VA_ARGS__] =                     \
    Asm().ControlFlowHelper_Bind(label);                                  \
    (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning( \
        CONCAT(is_bound_, __LINE__)))
#define BIND_LOOP(loop_label, ...) \
  for(auto [CONCAT(run_loop_, __LINE__), ##__VA_ARGS__] =                      \
    Asm().ControlFlowHelper_BindLoop(loop_label); CONCAT(run_loop_, __LINE__); \
        Asm().ControlFlowHelper_EndLoop(loop_label),                           \
        CONCAT(run_loop_, __LINE__) = false)

#define WHILE(...)                                                            \
    for (auto [CONCAT(run_loop_, __LINE__), loop_header_xx, loop_exit_xx]     \
              = Asm().ControlFlowHelper_While([&]() {                         \
           return __VA_ARGS__;                                                \
         });                                                                  \
         CONCAT(run_loop_, __LINE__);                                         \
         Asm().ControlFlowHelper_EndWhileLoop(loop_header_xx, loop_exit_xx),  \
              CONCAT(run_loop_, __LINE__) = false)

#define BREAK Asm().ControlFlowHelper_Goto(loop_exit_xx, {})
#define CONTINUE Asm().ControlFlowHelper_Goto(loop_header_xx, {})

#define GOTO(label, ...)                             \
  Asm().ControlFlowHelper_Goto(label, {__VA_ARGS__})
#define GOTO_IF(cond, label, ...)                            \
  Asm().ControlFlowHelper_GotoIf(cond, label, {__VA_ARGS__})
#define GOTO_IF_NOT(cond, label, ...)                           \
  Asm().ControlFlowHelper_GotoIfNot(cond, label, {__VA_ARGS__})

// Clang/GCC helpfully warn us about dangling else in nested if statements. This
// dangling is intentional for the way these macros work, so suppress the
// warning with Pragmas. Clang and GCC helpfully disagree on where the warning
// is (on the if or the else), so they need separate macros.
#if defined(__clang__)
#define FORCE_UNROLL_LOOP _Pragma("clang loop unroll(full)")
#define SUPPRESSED_DANGLING_ELSE_WARNING_IF(...) if (__VA_ARGS__)
#define SUPPRESSED_DANGLING_ELSE_WARNING_ELSE                             \
  _Pragma("GCC diagnostic push")                                          \
      _Pragma("GCC diagnostic ignored \"-Wdangling-else\"") else _Pragma( \
          "GCC diagnostic pop")
#elif defined(__GNUC__)
#define FORCE_UNROLL_LOOP
#define SUPPRESSED_DANGLING_ELSE_WARNING_IF(...)                             \
  _Pragma("GCC diagnostic push")                                             \
      _Pragma("GCC diagnostic ignored \"-Wdangling-else\"") if (__VA_ARGS__) \
          _Pragma("GCC diagnostic pop")
#define SUPPRESSED_DANGLING_ELSE_WARNING_ELSE else
#else
#define FORCE_UNROLL_LOOP
#define SUPPRESSED_DANGLING_ELSE_WARNING_IF(...) if (__VA_ARGS__)
#define SUPPRESSED_DANGLING_ELSE_WARNING_ELSE else
#endif

// IF/ELSE macros. These expand to a real C++ if-else, so that we can get
// similar block syntax behaviour (with an optional `ELSE`). Since C++ will only
// evaluate one side of the if-else, wrap it in a for loop that executes the
// if-else twice, once for each side of the branch. Each iteration also emits a
// goto-end if the corresponding branch target was bound. An if around the for
// loop encapsulates the state -- this is outside the for loop to make it easier
// for the compiler to unroll the two loop iterations.
#define IF(...)                                                                \
  SUPPRESSED_DANGLING_ELSE_WARNING_IF(                                         \
      typename std::decay_t<decltype(Asm())>::ControlFlowHelper_IfState state; \
      true)                                                                    \
  FORCE_UNROLL_LOOP                                                            \
  for (int iteration = 0, bound = false;                                       \
       iteration < 2 || (Asm().ControlFlowHelper_EndIf(&state), false);        \
       (bound ? Asm().ControlFlowHelper_FinishIfBlock(&state) : (void)0),      \
           bound = false, iteration++)                                         \
    if (iteration == 0 &&                                                      \
        (bound = Asm().ControlFlowHelper_BindIf(__VA_ARGS__, &state)))

#define IF_NOT(...)                                                            \
  SUPPRESSED_DANGLING_ELSE_WARNING_IF(                                         \
      typename std::decay_t<decltype(Asm())>::ControlFlowHelper_IfState state; \
      true)                                                                    \
  FORCE_UNROLL_LOOP                                                            \
  for (int iteration = 0, bound = false;                                       \
       iteration < 2 || (Asm().ControlFlowHelper_EndIf(&state), false);        \
       (bound ? Asm().ControlFlowHelper_FinishIfBlock(&state) : (void)0),      \
           bound = false, iteration++)                                         \
    if (iteration == 0 &&                                                      \
        (bound = Asm().ControlFlowHelper_BindIfNot(__VA_ARGS__, &state)))

#define ELSE                                 \
  SUPPRESSED_DANGLING_ELSE_WARNING_ELSE if ( \
      iteration == 1 && (bound = Asm().ControlFlowHelper_BindElse(&state)))

#define Assert(condition) AssertImpl(condition, #condition, __FILE__, __LINE__)

#ifdef DEBUG
// In debug builds, `REDUCE(operation)` makes sure that `operation##Op` exists
// by using this name in an expression. This will detect typos in the name which
// would otherwise stay unnoticed potentially.
#define REDUCE(operation)                                                     \
  CONCAT(CHECK_Reduce, operation) =                                           \
      (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning(   \
           std::is_same_v<operation##Op, operation##Op>),                     \
       OpIndex{});                                                            \
  template <class... Args>                                                    \
  decltype(CONCAT(CHECK_Reduce, operation)) Reduce##operation(Args... args) { \
    if (v8_flags.turboshaft_trace_intermediate_reductions) {                  \
      base::SmallVector<OperationStorageSlot, 32> storage;                    \
      operation##Op* op = CreateOperation<operation##Op>(storage, args...);   \
      PrintF("%*s", Asm().intermediate_tracing_depth(), "");                  \
      std::cout << "[" << ReducerName() << "]: reducing " << *op << "\n";       \
    }                                                                         \
    Asm().intermediate_tracing_depth()++;                                     \
    decltype(CONCAT(CHECK_Reduce, operation)) result =                        \
        Reduce##operation##Helper(args...);                                   \
    Asm().intermediate_tracing_depth()--;                                     \
    return result;                                                            \
  }                                                                           \
  decltype(CONCAT(CHECK_Reduce, operation)) Reduce##operation##Helper

#define REDUCE_INPUT_GRAPH(operation)                                          \
  CONCAT(CHECK_ReduceInputGraph, operation) =                                  \
      (::v8::internal::compiler::turboshaft::detail::SuppressUnusedWarning(    \
           std::is_same_v<operation##Op, operation##Op>),                      \
       OpIndex{});                                                             \
  decltype(CONCAT(                                                             \
      CHECK_ReduceInputGraph,                                                  \
      operation)) ReduceInputGraph##operation(OpIndex ig_index,                \
                                              const operation##Op& op) {       \
    if (v8_flags.turboshaft_trace_intermediate_reductions) {                   \
      PrintF("%*s", Asm().intermediate_tracing_depth(), "");                   \
      std::cout << "[" << ReducerName() << "]: @input-reducing " << op << "\n"; \
    }                                                                          \
    Asm().intermediate_tracing_depth()++;                                      \
    decltype(CONCAT(CHECK_ReduceInputGraph, operation)) result =               \
        ReduceInputGraph##operation##Helper(ig_index, op);                     \
    Asm().intermediate_tracing_depth()--;                                      \
    return result;                                                             \
  }                                                                            \
  decltype(CONCAT(CHECK_ReduceInputGraph,                                      \
                  operation)) ReduceInputGraph##operation##Helper
#else
#define REDUCE(operation) Reduce##operation
#define REDUCE_INPUT_GRAPH(operation) ReduceInputGraph##operation
#endif  // DEBUG

#define __ Asm().
