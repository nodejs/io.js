// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOGGING_COUNTERS_DEFINITIONS_H_
#define V8_LOGGING_COUNTERS_DEFINITIONS_H_

#include "include/v8-internal.h"

namespace v8 {
namespace internal {

// Generic range histograms.
// HR(name, caption, min, max, num_buckets)
#define HISTOGRAM_RANGE_LIST(HR)                                               \
  HR(code_cache_reject_reason, V8.CodeCacheRejectReason, 1, 9, 9)              \
  HR(errors_thrown_per_context, V8.ErrorsThrownPerContext, 0, 200, 20)         \
  HR(incremental_marking_reason, V8.GCIncrementalMarkingReason, 0,             \
     kGarbageCollectionReasonMaxValue, kGarbageCollectionReasonMaxValue + 1)   \
  HR(incremental_marking_sum, V8.GCIncrementalMarkingSum, 0, 10000, 101)       \
  HR(mark_compact_reason, V8.GCMarkCompactReason, 0,                           \
     kGarbageCollectionReasonMaxValue, kGarbageCollectionReasonMaxValue + 1)   \
  HR(gc_finalize_clear, V8.GCFinalizeMC.Clear, 0, 10000, 101)                  \
  HR(gc_finalize_epilogue, V8.GCFinalizeMC.Epilogue, 0, 10000, 101)            \
  HR(gc_finalize_evacuate, V8.GCFinalizeMC.Evacuate, 0, 10000, 101)            \
  HR(gc_finalize_finish, V8.GCFinalizeMC.Finish, 0, 10000, 101)                \
  HR(gc_finalize_mark, V8.GCFinalizeMC.Mark, 0, 10000, 101)                    \
  HR(gc_finalize_prologue, V8.GCFinalizeMC.Prologue, 0, 10000, 101)            \
  HR(gc_finalize_sweep, V8.GCFinalizeMC.Sweep, 0, 10000, 101)                  \
  HR(gc_scavenger_scavenge_main, V8.GCScavenger.ScavengeMain, 0, 10000, 101)   \
  HR(gc_scavenger_scavenge_roots, V8.GCScavenger.ScavengeRoots, 0, 10000, 101) \
  HR(gc_marking_sum, V8.GCMarkingSum, 0, 10000, 101)                           \
  /* Asm/Wasm. */                                                              \
  HR(wasm_functions_per_asm_module, V8.WasmFunctionsPerModule.asm, 1, 1000000, \
     51)                                                                       \
  HR(wasm_functions_per_wasm_module, V8.WasmFunctionsPerModule.wasm, 1,        \
     1000000, 51)                                                              \
  HR(array_buffer_big_allocations, V8.ArrayBufferLargeAllocations, 0, 4096,    \
     13)                                                                       \
  HR(array_buffer_new_size_failures, V8.ArrayBufferNewSizeFailures, 0, 4096,   \
     13)                                                                       \
  HR(shared_array_allocations, V8.SharedArrayAllocationSizes, 0, 4096, 13)     \
  HR(wasm_asm_huge_function_size_bytes, V8.WasmHugeFunctionSizeBytes.asm,      \
     100 * KB, GB, 51)                                                         \
  HR(wasm_wasm_huge_function_size_bytes, V8.WasmHugeFunctionSizeBytes.wasm,    \
     100 * KB, GB, 51)                                                         \
  HR(wasm_asm_module_size_bytes, V8.WasmModuleSizeBytes.asm, 1, GB, 51)        \
  HR(wasm_wasm_module_size_bytes, V8.WasmModuleSizeBytes.wasm, 1, GB, 51)      \
  HR(wasm_compile_huge_function_peak_memory_bytes,                             \
     V8.WasmCompileHugeFunctionPeakMemoryBytes, 1, GB, 51)                     \
  HR(asm_module_size_bytes, V8.AsmModuleSizeBytes, 1, GB, 51)                  \
  HR(compile_script_cache_behaviour, V8.CompileScript.CacheBehaviour, 0, 20,   \
     21)                                                                       \
  HR(wasm_memory_allocation_result, V8.WasmMemoryAllocationResult, 0, 3, 4)    \
  /* Committed code size per module, collected on GC. */                       \
  /* Older histogram, in MiB (0..1024MB). */                                   \
  HR(wasm_module_code_size_mb, V8.WasmModuleCodeSizeMiB, 0, 1024, 64)          \
  /* Newer histogram, in KiB (0..100MB). */                                    \
  HR(wasm_module_code_size_kb, V8.WasmModuleCodeSizeKiB, 0, 1024 * 100, 101)   \
  /* Percent of freed code size per module, collected on GC. */                \
  HR(wasm_module_freed_code_size_percent, V8.WasmModuleCodeSizePercentFreed,   \
     0, 100, 32)                                                               \
  /* Number of code GCs triggered per native module, collected on code GC. */  \
  HR(wasm_module_num_triggered_code_gcs,                                       \
     V8.WasmModuleNumberOfCodeGCsTriggered, 1, 128, 20)                        \
  /* Number of code spaces reserved per wasm module. */                        \
  HR(wasm_module_num_code_spaces, V8.WasmModuleNumberOfCodeSpaces, 1, 128, 20) \
  /* Number of live modules per isolate. */                                    \
  HR(wasm_modules_per_isolate, V8.WasmModulesPerIsolate, 1, 1024, 30)          \
  /* Number of live modules per engine (i.e. whole process). */                \
  HR(wasm_modules_per_engine, V8.WasmModulesPerEngine, 1, 1024, 30)            \
  /* Bailout reason if Liftoff failed, or {kSuccess} (per function). */        \
  HR(liftoff_bailout_reasons, V8.LiftoffBailoutReasons, 0, 20, 21)             \
  /* Support for PKEYs/PKU by testing result of pkey_alloc(). */               \
  HR(wasm_memory_protection_keys_support, V8.WasmMemoryProtectionKeysSupport,  \
     0, 1, 2)                                                                  \
  /* Ticks observed in a single Turbofan compilation, in 1K. */                \
  HR(turbofan_ticks, V8.TurboFan1KTicks, 0, 100000, 200)                       \
  /* Backtracks observed in a single regexp interpreter execution. */          \
  /* The maximum of 100M backtracks takes roughly 2 seconds on my machine. */  \
  HR(regexp_backtracks, V8.RegExpBacktracks, 1, 100000000, 50)                 \
  /* Number of times a cache event is triggered for a wasm module. */          \
  HR(wasm_cache_count, V8.WasmCacheCount, 0, 100, 101)                         \
  HR(wasm_streaming_until_compilation_finished,                                \
     V8.WasmStreamingUntilCompilationFinishedMilliSeconds, 0, 10000, 50)       \
  HR(wasm_compilation_until_streaming_finished,                                \
     V8.WasmCompilationUntilStreamFinishedMilliSeconds, 0, 10000, 50)          \
  /* Number of in-use external pointers in the external pointer table. */      \
  /* Counted after sweeping the table at the end of mark-compact GC. */        \
  HR(external_pointers_count, V8.SandboxedExternalPointersCount, 0,            \
     kMaxExternalPointers, 101)                                                \
  HR(code_pointers_count, V8.SandboxedCodePointersCount, 0, kMaxCodePointers,  \
     101)                                                                      \
  HR(trusted_pointers_count, V8.SandboxedTrustedPointersCount, 0,              \
     kMaxTrustedPointers, 101)                                                 \
  HR(wasm_num_lazy_compilations_5sec, V8.WasmNumLazyCompilations5Sec, 0,       \
     200000, 50)                                                               \
  HR(wasm_num_lazy_compilations_20sec, V8.WasmNumLazyCompilations20Sec, 0,     \
     200000, 50)                                                               \
  HR(wasm_num_lazy_compilations_60sec, V8.WasmNumLazyCompilations60Sec, 0,     \
     200000, 50)                                                               \
  HR(wasm_num_lazy_compilations_120sec, V8.WasmNumLazyCompilations120Sec, 0,   \
     200000, 50)                                                               \
  /* Outcome of external pointer table compaction: kSuccess, */                \
  /* kPartialSuccessor kAbortedDuringSweeping. See */                          \
  /* ExternalPointerTable::TableCompactionOutcome enum for more details. */    \
  HR(external_pointer_table_compaction_outcome,                                \
     V8.ExternalPointerTableCompactionOutcome, 0, 2, 3)                        \
  HR(wasm_compilation_method, V8.WasmCompilationMethod, 0, 4, 5)               \
  HR(asmjs_instantiate_result, V8.AsmjsInstantiateResult, 0, 1, 2)

// Like TIMED_HISTOGRAM_LIST, but allows the use of NestedTimedHistogramScope.
// HT(name, caption, max, unit)
#define NESTED_TIMED_HISTOGRAM_LIST(HT)                                       \
  /* Garbage collection timers. */                                            \
  HT(gc_idle_notification, V8.GCIdleNotification, 10000, MILLISECOND)         \
  HT(gc_incremental_marking, V8.GCIncrementalMarking, 10000, MILLISECOND)     \
  HT(gc_incremental_marking_start, V8.GCIncrementalMarkingStart, 10000,       \
     MILLISECOND)                                                             \
  HT(gc_minor_incremental_marking_start, V8.GCMinorIncrementalMarkingStart,   \
     10000, MILLISECOND)                                                      \
  HT(gc_low_memory_notification, V8.GCLowMemoryNotification, 10000,           \
     MILLISECOND)                                                             \
  /* Compilation times. */                                                    \
  HT(collect_source_positions, V8.CollectSourcePositions, 1000000,            \
     MICROSECOND)                                                             \
  HT(compile, V8.CompileMicroSeconds, 1000000, MICROSECOND)                   \
  HT(compile_eval, V8.CompileEvalMicroSeconds, 1000000, MICROSECOND)          \
  /* Serialization as part of compilation (code caching). */                  \
  HT(compile_serialize, V8.CompileSerializeMicroSeconds, 100000, MICROSECOND) \
  HT(compile_deserialize, V8.CompileDeserializeMicroSeconds, 1000000,         \
     MICROSECOND)                                                             \
  /* Snapshot. */                                                             \
  HT(snapshot_decompress, V8.SnapshotDecompressMicroSeconds, 1000000,         \
     MICROSECOND)                                                             \
  HT(snapshot_deserialize_rospace, V8.SnapshotDeserializeRoSpaceMicroSeconds, \
     1000000, MICROSECOND)                                                    \
  HT(snapshot_deserialize_isolate, V8.SnapshotDeserializeIsolateMicroSeconds, \
     1000000, MICROSECOND)                                                    \
  HT(snapshot_deserialize_context, V8.SnapshotDeserializeContextMicroSeconds, \
     1000000, MICROSECOND)                                                    \
  /* ... and also see compile_deserialize above. */                           \
  /* Total compilation time incl. caching/parsing. */                         \
  HT(compile_script, V8.CompileScriptMicroSeconds, 1000000, MICROSECOND)

#define NESTED_TIMED_HISTOGRAM_LIST_SLOW(HT)                                \
  /* Total V8 time (including JS and runtime calls, exluding callbacks). */ \
  HT(execute, V8.ExecuteMicroSeconds, 1000000, MICROSECOND)

// Timer histograms, thread safe: HT(name, caption, max, unit)
#define TIMED_HISTOGRAM_LIST(HT)                                               \
  /* Garbage collection timers. */                                             \
  HT(gc_finalize_incremental_regular,                                          \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.Regular, 10000,          \
     MILLISECOND)                                                              \
  HT(gc_finalize_incremental_regular_foreground,                               \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.Regular.Foreground,      \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_incremental_regular_background,                               \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.Regular.Background,      \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_incremental_memory_reducing,                                  \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.ReduceMemory, 10000,     \
     MILLISECOND)                                                              \
  HT(gc_finalize_incremental_memory_reducing_foreground,                       \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.ReduceMemory.Foreground, \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_incremental_memory_reducing_background,                       \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.ReduceMemory.Background, \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_incremental_memory_measure,                                   \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.MeasureMemory, 10000,    \
     MILLISECOND)                                                              \
  HT(gc_finalize_incremental_memory_measure_foreground,                        \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.MeasureMemory            \
         .Foreground,                                                          \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_incremental_memory_measure_background,                        \
     V8.GC.Event.MainThread.Full.Finalize.Incremental.MeasureMemory            \
         .Background,                                                          \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_regular,                                      \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.Regular, 10000,       \
     MILLISECOND)                                                              \
  HT(gc_finalize_non_incremental_regular_foreground,                           \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.Regular.Foreground,   \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_regular_background,                           \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.Regular.Background,   \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_memory_reducing,                              \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.ReduceMemory, 10000,  \
     MILLISECOND)                                                              \
  HT(gc_finalize_non_incremental_memory_reducing_foreground,                   \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.ReduceMemory          \
         .Foreground,                                                          \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_memory_reducing_background,                   \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.ReduceMemory          \
         .Background,                                                          \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_memory_measure,                               \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.MeasureMemory, 10000, \
     MILLISECOND)                                                              \
  HT(gc_finalize_non_incremental_memory_measure_foreground,                    \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.MeasureMemory         \
         .Foreground,                                                          \
     10000, MILLISECOND)                                                       \
  HT(gc_finalize_non_incremental_memory_measure_background,                    \
     V8.GC.Event.MainThread.Full.Finalize.NonIncremental.MeasureMemory         \
         .Background,                                                          \
     10000, MILLISECOND)                                                       \
  HT(measure_memory_delay_ms, V8.MeasureMemoryDelayMilliseconds, 100000,       \
     MILLISECOND)                                                              \
  HT(gc_time_to_global_safepoint, V8.GC.TimeToGlobalSafepoint, 10000000,       \
     MICROSECOND)                                                              \
  HT(gc_time_to_safepoint, V8.GC.TimeToSafepoint, 10000000, MICROSECOND)       \
  HT(gc_time_to_collection_on_background, V8.GC.TimeToCollectionOnBackground,  \
     10000000, MICROSECOND)                                                    \
  /* Maglev timers. */                                                         \
  HT(maglev_optimize_prepare, V8.MaglevOptimizePrepare, 100000, MICROSECOND)   \
  HT(maglev_optimize_execute, V8.MaglevOptimizeExecute, 100000, MICROSECOND)   \
  HT(maglev_optimize_finalize, V8.MaglevOptimizeFinalize, 100000, MICROSECOND) \
  HT(maglev_optimize_total_time, V8.MaglevOptimizeTotalTime, 1000000,          \
     MICROSECOND)                                                              \
  /* TurboFan timers. */                                                       \
  HT(turbofan_optimize_prepare, V8.TurboFanOptimizePrepare, 1000000,           \
     MICROSECOND)                                                              \
  HT(turbofan_optimize_execute, V8.TurboFanOptimizeExecute, 1000000,           \
     MICROSECOND)                                                              \
  HT(turbofan_optimize_finalize, V8.TurboFanOptimizeFinalize, 1000000,         \
     MICROSECOND)                                                              \
  HT(turbofan_optimize_total_foreground, V8.TurboFanOptimizeTotalForeground,   \
     10000000, MICROSECOND)                                                    \
  HT(turbofan_optimize_total_background, V8.TurboFanOptimizeTotalBackground,   \
     10000000, MICROSECOND)                                                    \
  HT(turbofan_optimize_total_time, V8.TurboFanOptimizeTotalTime, 10000000,     \
     MICROSECOND)                                                              \
  HT(turbofan_optimize_non_concurrent_total_time,                              \
     V8.TurboFanOptimizeNonConcurrentTotalTime, 10000000, MICROSECOND)         \
  HT(turbofan_optimize_concurrent_total_time,                                  \
     V8.TurboFanOptimizeConcurrentTotalTime, 10000000, MICROSECOND)            \
  HT(turbofan_osr_prepare, V8.TurboFanOptimizeForOnStackReplacementPrepare,    \
     1000000, MICROSECOND)                                                     \
  HT(turbofan_osr_execute, V8.TurboFanOptimizeForOnStackReplacementExecute,    \
     1000000, MICROSECOND)                                                     \
  HT(turbofan_osr_finalize, V8.TurboFanOptimizeForOnStackReplacementFinalize,  \
     1000000, MICROSECOND)                                                     \
  HT(turbofan_osr_total_time,                                                  \
     V8.TurboFanOptimizeForOnStackReplacementTotalTime, 10000000, MICROSECOND) \
  /* Wasm timers. */                                                           \
  HT(wasm_compile_asm_module_time, V8.WasmCompileModuleMicroSeconds.asm,       \
     10000000, MICROSECOND)                                                    \
  HT(wasm_compile_wasm_module_time, V8.WasmCompileModuleMicroSeconds.wasm,     \
     10000000, MICROSECOND)                                                    \
  HT(wasm_async_compile_wasm_module_time,                                      \
     V8.WasmCompileModuleAsyncMicroSeconds, 100000000, MICROSECOND)            \
  HT(wasm_streaming_compile_wasm_module_time,                                  \
     V8.WasmCompileModuleStreamingMicroSeconds, 100000000, MICROSECOND)        \
  HT(wasm_streaming_finish_wasm_module_time,                                   \
     V8.WasmFinishModuleStreamingMicroSeconds, 100000000, MICROSECOND)         \
  HT(wasm_deserialization_time, V8.WasmDeserializationTimeMilliSeconds, 10000, \
     MILLISECOND)                                                              \
  HT(wasm_compile_asm_function_time, V8.WasmCompileFunctionMicroSeconds.asm,   \
     1000000, MICROSECOND)                                                     \
  HT(wasm_compile_wasm_function_time, V8.WasmCompileFunctionMicroSeconds.wasm, \
     1000000, MICROSECOND)                                                     \
  HT(wasm_compile_huge_function_time, V8.WasmCompileHugeFunctionMilliSeconds,  \
     100000, MILLISECOND)                                                      \
  HT(wasm_instantiate_wasm_module_time,                                        \
     V8.WasmInstantiateModuleMicroSeconds.wasm, 10000000, MICROSECOND)         \
  HT(wasm_instantiate_asm_module_time,                                         \
     V8.WasmInstantiateModuleMicroSeconds.asm, 10000000, MICROSECOND)          \
  HT(wasm_lazy_compile_time, V8.WasmLazyCompileTimeMicroSeconds, 100000000,    \
     MICROSECOND)                                                              \
  HT(wasm_compile_after_deserialize,                                           \
     V8.WasmCompileAfterDeserializeMilliSeconds, 1000000, MILLISECOND)         \
  /* Total compilation time incl. caching/parsing for various cache states. */ \
  HT(compile_script_with_produce_cache,                                        \
     V8.CompileScriptMicroSeconds.ProduceCache, 1000000, MICROSECOND)          \
  HT(compile_script_with_isolate_cache_hit,                                    \
     V8.CompileScriptMicroSeconds.IsolateCacheHit, 1000000, MICROSECOND)       \
  HT(compile_script_with_consume_cache,                                        \
     V8.CompileScriptMicroSeconds.ConsumeCache, 1000000, MICROSECOND)          \
  HT(compile_script_consume_failed,                                            \
     V8.CompileScriptMicroSeconds.ConsumeCache.Failed, 1000000, MICROSECOND)   \
  HT(compile_script_no_cache_other,                                            \
     V8.CompileScriptMicroSeconds.NoCache.Other, 1000000, MICROSECOND)         \
  HT(compile_script_no_cache_because_inline_script,                            \
     V8.CompileScriptMicroSeconds.NoCache.InlineScript, 1000000, MICROSECOND)  \
  HT(compile_script_no_cache_because_script_too_small,                         \
     V8.CompileScriptMicroSeconds.NoCache.ScriptTooSmall, 1000000,             \
     MICROSECOND)                                                              \
  HT(compile_script_no_cache_because_cache_too_cold,                           \
     V8.CompileScriptMicroSeconds.NoCache.CacheTooCold, 1000000, MICROSECOND)  \
  HT(compile_script_streaming_finalization,                                    \
     V8.CompileScriptMicroSeconds.StreamingFinalization, 1000000, MICROSECOND) \
  HT(compile_script_on_background,                                             \
     V8.CompileScriptMicroSeconds.BackgroundThread, 1000000, MICROSECOND)      \
  HT(compile_function_on_background,                                           \
     V8.CompileFunctionMicroSeconds.BackgroundThread, 1000000, MICROSECOND)    \
  HT(deserialize_script_on_background,                                         \
     V8.CompileScriptMicroSeconds.ConsumeCache.BackgroundThread, 1000000,      \
     MICROSECOND)                                                              \
  HT(wasm_max_lazy_compilation_time_5sec,                                      \
     V8.WasmMaxLazyCompilationTime5SecMilliSeconds, 5000, MILLISECOND)         \
  HT(wasm_max_lazy_compilation_time_20sec,                                     \
     V8.WasmMaxLazyCompilationTime20SecMilliSeconds, 5000, MILLISECOND)        \
  HT(wasm_max_lazy_compilation_time_60sec,                                     \
     V8.WasmMaxLazyCompilationTime60SecMilliSeconds, 5000, MILLISECOND)        \
  HT(wasm_max_lazy_compilation_time_120sec,                                    \
     V8.WasmMaxLazyCompilationTime120SecMilliSeconds, 5000, MILLISECOND)       \
  HT(wasm_sum_lazy_compilation_time_5sec,                                      \
     V8.WasmSumLazyCompilationTime5SecMilliSeconds, 20000, MILLISECOND)        \
  HT(wasm_sum_lazy_compilation_time_20sec,                                     \
     V8.WasmSumLazyCompilationTime20SecMilliSeconds, 20000, MILLISECOND)       \
  HT(wasm_sum_lazy_compilation_time_60sec,                                     \
     V8.WasmSumLazyCompilationTime60SecMilliSeconds, 20000, MILLISECOND)       \
  HT(wasm_sum_lazy_compilation_time_120sec,                                    \
     V8.WasmSumLazyCompilationTime120SecMilliSeconds, 20000, MILLISECOND)      \
  /* Debugger timers. */                                                       \
  HT(debug_pause_to_paused_event, V8.DebugPauseToPausedEventMilliSeconds,      \
     1000000, MILLISECOND)

#define AGGREGATABLE_HISTOGRAM_TIMER_LIST(AHT) \
  AHT(compile_lazy, V8.CompileLazyMicroSeconds)

#define HISTOGRAM_PERCENTAGE_LIST(HP)                                          \
  /* Heap fragmentation. */                                                    \
  HP(external_fragmentation_total, V8.MemoryExternalFragmentationTotal)        \
  HP(external_fragmentation_old_space, V8.MemoryExternalFragmentationOldSpace) \
  HP(external_fragmentation_code_space,                                        \
     V8.MemoryExternalFragmentationCodeSpace)                                  \
  HP(external_fragmentation_map_space, V8.MemoryExternalFragmentationMapSpace) \
  HP(external_fragmentation_lo_space, V8.MemoryExternalFragmentationLoSpace)

// Note: These use Histogram with options (min=1000, max=500000, buckets=50).
#define HISTOGRAM_LEGACY_MEMORY_LIST(HM)                                      \
  HM(heap_sample_total_committed, V8.MemoryHeapSampleTotalCommitted)          \
  HM(heap_sample_total_used, V8.MemoryHeapSampleTotalUsed)                    \
  HM(heap_sample_map_space_committed, V8.MemoryHeapSampleMapSpaceCommitted)   \
  HM(heap_sample_code_space_committed, V8.MemoryHeapSampleCodeSpaceCommitted) \
  HM(heap_sample_maximum_committed, V8.MemoryHeapSampleMaximumCommitted)

#define STATS_COUNTER_LIST(SC)                                                 \
  /* Global handle count. */                                                   \
  SC(global_handles, V8.GlobalHandles)                                         \
  SC(alive_after_last_gc, V8.AliveAfterLastGC)                                 \
  SC(compilation_cache_hits, V8.CompilationCacheHits)                          \
  SC(compilation_cache_misses, V8.CompilationCacheMisses)                      \
  /* Number of times the cache contained a reusable Script but not */          \
  /* the root SharedFunctionInfo. */                                           \
  SC(compilation_cache_partial_hits, V8.CompilationCachePartialHits)           \
  SC(objs_since_last_young, V8.ObjsSinceLastYoung)                             \
  SC(objs_since_last_full, V8.ObjsSinceLastFull)                               \
  SC(gc_compactor_caused_by_request, V8.GCCompactorCausedByRequest)            \
  SC(gc_compactor_caused_by_promoted_data, V8.GCCompactorCausedByPromotedData) \
  SC(gc_compactor_caused_by_oldspace_exhaustion,                               \
     V8.GCCompactorCausedByOldspaceExhaustion)                                 \
  SC(enum_cache_hits, V8.EnumCacheHits)                                        \
  SC(enum_cache_misses, V8.EnumCacheMisses)                                    \
  SC(maps_created, V8.MapsCreated)                                             \
  SC(megamorphic_stub_cache_updates, V8.MegamorphicStubCacheUpdates)           \
  SC(regexp_entry_runtime, V8.RegExpEntryRuntime)                              \
  SC(stack_interrupts, V8.StackInterrupts)                                     \
  SC(new_space_bytes_available, V8.MemoryNewSpaceBytesAvailable)               \
  SC(new_space_bytes_committed, V8.MemoryNewSpaceBytesCommitted)               \
  SC(new_space_bytes_used, V8.MemoryNewSpaceBytesUsed)                         \
  SC(old_space_bytes_available, V8.MemoryOldSpaceBytesAvailable)               \
  SC(old_space_bytes_committed, V8.MemoryOldSpaceBytesCommitted)               \
  SC(old_space_bytes_used, V8.MemoryOldSpaceBytesUsed)                         \
  SC(code_space_bytes_available, V8.MemoryCodeSpaceBytesAvailable)             \
  SC(code_space_bytes_committed, V8.MemoryCodeSpaceBytesCommitted)             \
  SC(code_space_bytes_used, V8.MemoryCodeSpaceBytesUsed)                       \
  SC(map_space_bytes_available, V8.MemoryMapSpaceBytesAvailable)               \
  SC(map_space_bytes_committed, V8.MemoryMapSpaceBytesCommitted)               \
  SC(map_space_bytes_used, V8.MemoryMapSpaceBytesUsed)                         \
  SC(lo_space_bytes_available, V8.MemoryLoSpaceBytesAvailable)                 \
  SC(lo_space_bytes_committed, V8.MemoryLoSpaceBytesCommitted)                 \
  SC(lo_space_bytes_used, V8.MemoryLoSpaceBytesUsed)                           \
  SC(wasm_generated_code_size, V8.WasmGeneratedCodeBytes)                      \
  SC(wasm_reloc_size, V8.WasmRelocBytes)                                       \
  SC(wasm_lazily_compiled_functions, V8.WasmLazilyCompiledFunctions)           \
  SC(wasm_compiled_export_wrapper, V8.WasmCompiledExportWrappers)

// List of counters that can be incremented from generated code. We need them in
// a separate list to be able to relocate them.
#define STATS_COUNTER_NATIVE_CODE_LIST(SC)                         \
  /* Number of write barriers executed at runtime. */              \
  SC(write_barriers, V8.WriteBarriers)                             \
  SC(regexp_entry_native, V8.RegExpEntryNative)                    \
  SC(megamorphic_stub_cache_probes, V8.MegamorphicStubCacheProbes) \
  SC(megamorphic_stub_cache_misses, V8.MegamorphicStubCacheMisses)

}  // namespace internal
}  // namespace v8

#endif  // V8_LOGGING_COUNTERS_DEFINITIONS_H_
