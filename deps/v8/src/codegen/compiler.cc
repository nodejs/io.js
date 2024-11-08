// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/compiler.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "src/api/api-inl.h"
#include "src/asmjs/asm-js.h"
#include "src/ast/prettyprinter.h"
#include "src/ast/scopes.h"
#include "src/base/logging.h"
#include "src/base/platform/time.h"
#include "src/baseline/baseline.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/pending-optimization-table.h"
#include "src/codegen/script-details.h"
#include "src/codegen/unoptimized-compilation-info.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"
#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"
#include "src/compiler/turbofan.h"
#include "src/debug/debug.h"
#include "src/debug/liveedit.h"
#include "src/diagnostics/code-tracer.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-factory-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope-inl.h"
#include "src/init/bootstrapper.h"
#include "src/interpreter/interpreter.h"
#include "src/logging/counters-scopes.h"
#include "src/logging/log-inl.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/map.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/parsing.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/snapshot/code-serializer.h"
#include "src/tracing/traced-value.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-list-inl.h"  // crbug.com/v8/8816

#ifdef V8_ENABLE_MAGLEV
#include "src/maglev/maglev-concurrent-dispatcher.h"
#include "src/maglev/maglev.h"
#endif  // V8_ENABLE_MAGLEV

namespace v8 {
namespace internal {

namespace {

constexpr bool IsOSR(BytecodeOffset osr_offset) { return !osr_offset.IsNone(); }

void SetTieringState(IsolateForSandbox isolate, Tagged<JSFunction> function,
                     BytecodeOffset osr_offset, TieringState value) {
  if (IsOSR(osr_offset)) {
    DCHECK(value == TieringState::kInProgress || value == TieringState::kNone);
    function->set_osr_tiering_in_progress(IsInProgress(value));
  } else {
    function->set_tiering_state(isolate, value);
  }
}

void ResetTieringState(IsolateForSandbox isolate, Tagged<JSFunction> function,
                       BytecodeOffset osr_offset) {
  if (function->has_feedback_vector()) {
    SetTieringState(isolate, function, osr_offset, TieringState::kNone);
  }
}

class CompilerTracer : public AllStatic {
 public:
  static void TraceStartBaselineCompile(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> shared) {
    if (!v8_flags.trace_baseline) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "compiling method", shared, CodeKind::BASELINE);
    PrintTraceSuffix(scope);
  }

  static void TraceStartMaglevCompile(Isolate* isolate,
                                      DirectHandle<JSFunction> function,
                                      bool osr, ConcurrencyMode mode) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "compiling method", function, CodeKind::MAGLEV);
    if (osr) PrintF(scope.file(), " OSR");
    PrintF(scope.file(), ", mode: %s", ToString(mode));
    PrintTraceSuffix(scope);
  }

  static void TracePrepareJob(Isolate* isolate, OptimizedCompilationInfo* info,
                              ConcurrencyMode mode) {
    if (!v8_flags.trace_opt || !info->IsOptimizing()) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "compiling method", info);
    if (info->is_osr()) PrintF(scope.file(), " OSR");
    PrintF(scope.file(), ", mode: %s", ToString(mode));
    PrintTraceSuffix(scope);
  }

  static void TraceOptimizeOSRStarted(Isolate* isolate,
                                      DirectHandle<JSFunction> function,
                                      BytecodeOffset osr_offset,
                                      ConcurrencyMode mode) {
    if (!v8_flags.trace_osr) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(
        scope.file(),
        "[OSR - compilation started. function: %s, osr offset: %d, mode: %s]\n",
        function->DebugNameCStr().get(), osr_offset.ToInt(), ToString(mode));
  }

  static void TraceOptimizeOSRFinished(Isolate* isolate,
                                       DirectHandle<JSFunction> function,
                                       BytecodeOffset osr_offset) {
    if (!v8_flags.trace_osr) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(),
           "[OSR - compilation finished. function: %s, osr offset: %d]\n",
           function->DebugNameCStr().get(), osr_offset.ToInt());
  }

  static void TraceOptimizeOSRAvailable(Isolate* isolate,
                                        DirectHandle<JSFunction> function,
                                        BytecodeOffset osr_offset,
                                        ConcurrencyMode mode) {
    if (!v8_flags.trace_osr) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(),
           "[OSR - available (compilation completed or cache hit). function: "
           "%s, osr offset: %d, mode: %s]\n",
           function->DebugNameCStr().get(), osr_offset.ToInt(), ToString(mode));
  }

  static void TraceOptimizeOSRUnavailable(Isolate* isolate,
                                          DirectHandle<JSFunction> function,
                                          BytecodeOffset osr_offset,
                                          ConcurrencyMode mode) {
    if (!v8_flags.trace_osr) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(),
           "[OSR - unavailable (failed or in progress). function: %s, osr "
           "offset: %d, mode: %s]\n",
           function->DebugNameCStr().get(), osr_offset.ToInt(), ToString(mode));
  }

  static void TraceFinishTurbofanCompile(Isolate* isolate,
                                         OptimizedCompilationInfo* info,
                                         double ms_creategraph,
                                         double ms_optimize,
                                         double ms_codegen) {
    DCHECK(v8_flags.trace_opt);
    DCHECK(info->IsOptimizing());
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "completed compiling", info);
    if (info->is_osr()) PrintF(scope.file(), " OSR");
    PrintF(scope.file(), " - took %0.3f, %0.3f, %0.3f ms", ms_creategraph,
           ms_optimize, ms_codegen);
    PrintTraceSuffix(scope);
  }

  static void TraceFinishBaselineCompile(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> shared,
      double ms_timetaken) {
    if (!v8_flags.trace_baseline) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "completed compiling", shared, CodeKind::BASELINE);
    PrintF(scope.file(), " - took %0.3f ms", ms_timetaken);
    PrintTraceSuffix(scope);
  }

  static void TraceFinishMaglevCompile(Isolate* isolate,
                                       DirectHandle<JSFunction> function,
                                       bool osr, double ms_prepare,
                                       double ms_execute, double ms_finalize) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "completed compiling", function, CodeKind::MAGLEV);
    if (osr) PrintF(scope.file(), " OSR");
    PrintF(scope.file(), " - took %0.3f, %0.3f, %0.3f ms", ms_prepare,
           ms_execute, ms_finalize);
    PrintTraceSuffix(scope);
  }

  static void TraceAbortedMaglevCompile(Isolate* isolate,
                                        DirectHandle<JSFunction> function,
                                        BailoutReason bailout_reason) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "aborted compiling", function, CodeKind::MAGLEV);
    PrintF(scope.file(), " because: %s", GetBailoutReason(bailout_reason));
    PrintTraceSuffix(scope);
  }

  static void TraceCompletedJob(Isolate* isolate,
                                OptimizedCompilationInfo* info) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "completed optimizing", info);
    if (info->is_osr()) PrintF(scope.file(), " OSR");
    PrintTraceSuffix(scope);
  }

  static void TraceAbortedJob(Isolate* isolate, OptimizedCompilationInfo* info,
                              double ms_prepare, double ms_execute,
                              double ms_finalize) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "aborted optimizing", info);
    if (info->is_osr()) PrintF(scope.file(), " OSR");
    PrintF(scope.file(), " because: %s",
           GetBailoutReason(info->bailout_reason()));
    PrintF(scope.file(), " - took %0.3f, %0.3f, %0.3f ms", ms_prepare,
           ms_execute, ms_finalize);
    PrintTraceSuffix(scope);
  }

  static void TraceOptimizedCodeCacheHit(Isolate* isolate,
                                         DirectHandle<JSFunction> function,
                                         BytecodeOffset osr_offset,
                                         CodeKind code_kind) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "found optimized code for", function, code_kind);
    if (IsOSR(osr_offset)) {
      PrintF(scope.file(), " at OSR bytecode offset %d", osr_offset.ToInt());
    }
    PrintTraceSuffix(scope);
  }

  static void TraceOptimizeForAlwaysOpt(Isolate* isolate,
                                        DirectHandle<JSFunction> function,
                                        CodeKind code_kind) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintTracePrefix(scope, "optimizing", function, code_kind);
    PrintF(scope.file(), " because --always-turbofan");
    PrintTraceSuffix(scope);
  }

  static void TraceMarkForAlwaysOpt(Isolate* isolate,
                                    DirectHandle<JSFunction> function) {
    if (!v8_flags.trace_opt) return;
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(), "[marking ");
    ShortPrint(*function, scope.file());
    PrintF(scope.file(),
           " for optimized recompilation because --always-turbofan");
    PrintF(scope.file(), "]\n");
  }

 private:
  static void PrintTracePrefix(const CodeTracer::Scope& scope,
                               const char* header,
                               OptimizedCompilationInfo* info) {
    PrintTracePrefix(scope, header, info->closure(), info->code_kind());
  }

  static void PrintTracePrefix(const CodeTracer::Scope& scope,
                               const char* header,
                               DirectHandle<JSFunction> function,
                               CodeKind code_kind) {
    PrintF(scope.file(), "[%s ", header);
    ShortPrint(*function, scope.file());
    PrintF(scope.file(), " (target %s)", CodeKindToString(code_kind));
  }

  static void PrintTracePrefix(const CodeTracer::Scope& scope,
                               const char* header,
                               DirectHandle<SharedFunctionInfo> shared,
                               CodeKind code_kind) {
    PrintF(scope.file(), "[%s ", header);
    ShortPrint(*shared, scope.file());
    PrintF(scope.file(), " (target %s)", CodeKindToString(code_kind));
  }

  static void PrintTraceSuffix(const CodeTracer::Scope& scope) {
    PrintF(scope.file(), "]\n");
  }
};

}  // namespace

// static
void Compiler::LogFunctionCompilation(Isolate* isolate,
                                      LogEventListener::CodeTag code_type,
                                      DirectHandle<Script> script,
                                      Handle<SharedFunctionInfo> shared,
                                      Handle<FeedbackVector> vector,
                                      Handle<AbstractCode> abstract_code,
                                      CodeKind kind, double time_taken_ms) {
  DCHECK_NE(*abstract_code,
            Cast<AbstractCode>(*BUILTIN_CODE(isolate, CompileLazy)));

  // Log the code generation. If source information is available include
  // script name and line number. Check explicitly whether logging is
  // enabled as finding the line number is not free.
  if (!isolate->IsLoggingCodeCreation()) return;

  Script::PositionInfo info;
  Script::GetPositionInfo(script, shared->StartPosition(), &info);
  int line_num = info.line + 1;
  int column_num = info.column + 1;
  Handle<String> script_name(IsString(script->name())
                                 ? Cast<String>(script->name())
                                 : ReadOnlyRoots(isolate).empty_string(),
                             isolate);
  LogEventListener::CodeTag log_tag =
      V8FileLogger::ToNativeByScript(code_type, *script);
  PROFILE(isolate, CodeCreateEvent(log_tag, abstract_code, shared, script_name,
                                   line_num, column_num));
  if (!vector.is_null()) {
    LOG(isolate, FeedbackVectorEvent(*vector, *abstract_code));
  }
  if (!v8_flags.log_function_events) return;

  std::string name;
  switch (kind) {
    case CodeKind::INTERPRETED_FUNCTION:
      name = "interpreter";
      break;
    case CodeKind::BASELINE:
      name = "baseline";
      break;
    case CodeKind::MAGLEV:
      name = "maglev";
      break;
    case CodeKind::TURBOFAN:
      name = "turbofan";
      break;
    default:
      UNREACHABLE();
  }
  switch (code_type) {
    case LogEventListener::CodeTag::kEval:
      name += "-eval";
      break;
    case LogEventListener::CodeTag::kScript:
    case LogEventListener::CodeTag::kFunction:
      break;
    default:
      UNREACHABLE();
  }

  DirectHandle<String> debug_name =
      SharedFunctionInfo::DebugName(isolate, shared);
  DisallowGarbageCollection no_gc;
  LOG(isolate, FunctionEvent(name.c_str(), script->id(), time_taken_ms,
                             shared->StartPosition(), shared->EndPosition(),
                             *debug_name));
}

namespace {

ScriptOriginOptions OriginOptionsForEval(
    Tagged<Object> script, ParsingWhileDebugging parsing_while_debugging) {
  bool is_shared_cross_origin =
      parsing_while_debugging == ParsingWhileDebugging::kYes;
  bool is_opaque = false;
  if (IsScript(script)) {
    auto script_origin_options = Cast<Script>(script)->origin_options();
    if (script_origin_options.IsSharedCrossOrigin()) {
      is_shared_cross_origin = true;
    }
    if (script_origin_options.IsOpaque()) {
      is_opaque = true;
    }
  }
  return ScriptOriginOptions(is_shared_cross_origin, is_opaque);
}

}  // namespace

// ----------------------------------------------------------------------------
// Implementation of UnoptimizedCompilationJob

CompilationJob::Status UnoptimizedCompilationJob::ExecuteJob() {
  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToExecute);
  base::ScopedTimer t(v8_flags.log_function_events ? &time_taken_to_execute_
                                                   : nullptr);
  return UpdateState(ExecuteJobImpl(), State::kReadyToFinalize);
}

CompilationJob::Status UnoptimizedCompilationJob::FinalizeJob(
    Handle<SharedFunctionInfo> shared_info, Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
  DisallowCodeDependencyChange no_dependency_change;
  DisallowJavascriptExecution no_js(isolate);

  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToFinalize);
  base::ScopedTimer t(v8_flags.log_function_events ? &time_taken_to_finalize_
                                                   : nullptr);
  return UpdateState(FinalizeJobImpl(shared_info, isolate), State::kSucceeded);
}

CompilationJob::Status UnoptimizedCompilationJob::FinalizeJob(
    Handle<SharedFunctionInfo> shared_info, LocalIsolate* isolate) {
  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToFinalize);
  base::ScopedTimer t(v8_flags.log_function_events ? &time_taken_to_finalize_
                                                   : nullptr);
  return UpdateState(FinalizeJobImpl(shared_info, isolate), State::kSucceeded);
}

namespace {
void LogUnoptimizedCompilation(Isolate* isolate,
                               Handle<SharedFunctionInfo> shared,
                               LogEventListener::CodeTag code_type,
                               base::TimeDelta time_taken_to_execute,
                               base::TimeDelta time_taken_to_finalize) {
  Handle<AbstractCode> abstract_code;
  if (shared->HasBytecodeArray()) {
    abstract_code =
        handle(Cast<AbstractCode>(shared->GetBytecodeArray(isolate)), isolate);
  } else {
#if V8_ENABLE_WEBASSEMBLY
    DCHECK(shared->HasAsmWasmData());
    abstract_code = Cast<AbstractCode>(BUILTIN_CODE(isolate, InstantiateAsmJs));
#else
    UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  double time_taken_ms = time_taken_to_execute.InMillisecondsF() +
                         time_taken_to_finalize.InMillisecondsF();

  DirectHandle<Script> script(Cast<Script>(shared->script()), isolate);
  Compiler::LogFunctionCompilation(
      isolate, code_type, script, shared, Handle<FeedbackVector>(),
      abstract_code, CodeKind::INTERPRETED_FUNCTION, time_taken_ms);
}

}  // namespace

// ----------------------------------------------------------------------------
// Implementation of OptimizedCompilationJob

CompilationJob::Status OptimizedCompilationJob::PrepareJob(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
  DisallowJavascriptExecution no_js(isolate);

  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToPrepare);
  base::ScopedTimer t(&time_taken_to_prepare_);
  return UpdateState(PrepareJobImpl(isolate), State::kReadyToExecute);
}

CompilationJob::Status OptimizedCompilationJob::ExecuteJob(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  DCHECK_IMPLIES(local_isolate && !local_isolate->is_main_thread(),
                 local_isolate->heap()->IsParked());
  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToExecute);
  base::ScopedTimer t(&time_taken_to_execute_);
  return UpdateState(ExecuteJobImpl(stats, local_isolate),
                     State::kReadyToFinalize);
}

CompilationJob::Status OptimizedCompilationJob::FinalizeJob(Isolate* isolate) {
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
  DisallowJavascriptExecution no_js(isolate);

  // Delegate to the underlying implementation.
  DCHECK_EQ(state(), State::kReadyToFinalize);
  base::ScopedTimer t(&time_taken_to_finalize_);
  return UpdateState(FinalizeJobImpl(isolate), State::kSucceeded);
}

GlobalHandleVector<Map> OptimizedCompilationJob::CollectRetainedMaps(
    Isolate* isolate, DirectHandle<Code> code) {
  DCHECK(code->is_optimized_code());

  DisallowGarbageCollection no_gc;
  GlobalHandleVector<Map> maps(isolate->heap());
  PtrComprCageBase cage_base(isolate);
  int const mode_mask = RelocInfo::EmbeddedObjectModeMask();
  for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
    DCHECK(RelocInfo::IsEmbeddedObjectMode(it.rinfo()->rmode()));
    Tagged<HeapObject> target_object = it.rinfo()->target_object(cage_base);
    if (code->IsWeakObjectInOptimizedCode(target_object)) {
      if (IsMap(target_object, cage_base)) {
        maps.Push(Cast<Map>(target_object));
      }
    }
  }
  return maps;
}

void OptimizedCompilationJob::RegisterWeakObjectsInOptimizedCode(
    Isolate* isolate, DirectHandle<NativeContext> context,
    DirectHandle<Code> code, GlobalHandleVector<Map> maps) {
  isolate->heap()->AddRetainedMaps(context, std::move(maps));
  code->set_can_have_weak_objects(true);
}

CompilationJob::Status TurbofanCompilationJob::RetryOptimization(
    BailoutReason reason) {
  DCHECK(compilation_info_->IsOptimizing());
  compilation_info_->RetryOptimization(reason);
  return UpdateState(FAILED, State::kFailed);
}

CompilationJob::Status TurbofanCompilationJob::AbortOptimization(
    BailoutReason reason) {
  DCHECK(compilation_info_->IsOptimizing());
  compilation_info_->AbortOptimization(reason);
  return UpdateState(FAILED, State::kFailed);
}

void TurbofanCompilationJob::RecordCompilationStats(ConcurrencyMode mode,
                                                    Isolate* isolate) const {
  DCHECK(compilation_info()->IsOptimizing());
  DirectHandle<SharedFunctionInfo> shared = compilation_info()->shared_info();
  if (v8_flags.trace_opt || v8_flags.trace_opt_stats) {
    double ms_creategraph = time_taken_to_prepare_.InMillisecondsF();
    double ms_optimize = time_taken_to_execute_.InMillisecondsF();
    double ms_codegen = time_taken_to_finalize_.InMillisecondsF();
    if (v8_flags.trace_opt) {
      CompilerTracer::TraceFinishTurbofanCompile(
          isolate, compilation_info(), ms_creategraph, ms_optimize, ms_codegen);
    }
    if (v8_flags.trace_opt_stats) {
      static double compilation_time = 0.0;
      static int compiled_functions = 0;
      static int code_size = 0;

      compilation_time += (ms_creategraph + ms_optimize + ms_codegen);
      compiled_functions++;
      code_size += shared->SourceSize();
      PrintF(
          "[turbofan] Compiled: %d functions with %d byte source size in "
          "%fms.\n",
          compiled_functions, code_size, compilation_time);
    }
  }
  // Don't record samples from machines without high-resolution timers,
  // as that can cause serious reporting issues. See the thread at
  // http://g/chrome-metrics-team/NwwJEyL8odU/discussion for more details.
  if (!base::TimeTicks::IsHighResolution()) return;

  int elapsed_microseconds = static_cast<int>(ElapsedTime().InMicroseconds());
  Counters* const counters = isolate->counters();
  counters->turbofan_ticks()->AddSample(static_cast<int>(
      compilation_info()->tick_counter().CurrentTicks() / 1000));

  if (compilation_info()->is_osr()) {
    counters->turbofan_osr_prepare()->AddSample(
        static_cast<int>(time_taken_to_prepare_.InMicroseconds()));
    counters->turbofan_osr_execute()->AddSample(
        static_cast<int>(time_taken_to_execute_.InMicroseconds()));
    counters->turbofan_osr_finalize()->AddSample(
        static_cast<int>(time_taken_to_finalize_.InMicroseconds()));
    counters->turbofan_osr_total_time()->AddSample(elapsed_microseconds);
    return;
  }

  DCHECK(!compilation_info()->is_osr());
  counters->turbofan_optimize_prepare()->AddSample(
      static_cast<int>(time_taken_to_prepare_.InMicroseconds()));
  counters->turbofan_optimize_execute()->AddSample(
      static_cast<int>(time_taken_to_execute_.InMicroseconds()));
  counters->turbofan_optimize_finalize()->AddSample(
      static_cast<int>(time_taken_to_finalize_.InMicroseconds()));
  counters->turbofan_optimize_total_time()->AddSample(elapsed_microseconds);

  // Compute foreground / background time.
  base::TimeDelta time_background;
  base::TimeDelta time_foreground =
      time_taken_to_prepare_ + time_taken_to_finalize_;
  switch (mode) {
    case ConcurrencyMode::kConcurrent:
      time_background += time_taken_to_execute_;
      counters->turbofan_optimize_concurrent_total_time()->AddSample(
          elapsed_microseconds);
      break;
    case ConcurrencyMode::kSynchronous:
      counters->turbofan_optimize_non_concurrent_total_time()->AddSample(
          elapsed_microseconds);
      time_foreground += time_taken_to_execute_;
      break;
  }
  counters->turbofan_optimize_total_background()->AddSample(
      static_cast<int>(time_background.InMicroseconds()));
  counters->turbofan_optimize_total_foreground()->AddSample(
      static_cast<int>(time_foreground.InMicroseconds()));

  if (v8_flags.profile_guided_optimization &&
      shared->cached_tiering_decision() ==
          CachedTieringDecision::kEarlyMaglev) {
    shared->set_cached_tiering_decision(CachedTieringDecision::kEarlyTurbofan);
  }
}

void TurbofanCompilationJob::RecordFunctionCompilation(
    LogEventListener::CodeTag code_type, Isolate* isolate) const {
  Handle<AbstractCode> abstract_code =
      Cast<AbstractCode>(compilation_info()->code());

  double time_taken_ms = time_taken_to_prepare_.InMillisecondsF() +
                         time_taken_to_execute_.InMillisecondsF() +
                         time_taken_to_finalize_.InMillisecondsF();

  DirectHandle<Script> script(
      Cast<Script>(compilation_info()->shared_info()->script()), isolate);
  Handle<FeedbackVector> feedback_vector(
      compilation_info()->closure()->feedback_vector(), isolate);
  Compiler::LogFunctionCompilation(
      isolate, code_type, script, compilation_info()->shared_info(),
      feedback_vector, abstract_code, compilation_info()->code_kind(),
      time_taken_ms);
}

uint64_t TurbofanCompilationJob::trace_id() const {
  // Xor together the this pointer and the optimization id, to try to make the
  // id more unique on platforms where just the `this` pointer is likely to be
  // reused.
  return reinterpret_cast<uint64_t>(this) ^
         compilation_info_->optimization_id();
}

// ----------------------------------------------------------------------------
// Local helper methods that make up the compilation pipeline.

namespace {

#if V8_ENABLE_WEBASSEMBLY
bool UseAsmWasm(FunctionLiteral* literal, bool asm_wasm_broken) {
  // Check whether asm.js validation is enabled.
  if (!v8_flags.validate_asm) return false;

  // Modules that have validated successfully, but were subsequently broken by
  // invalid module instantiation attempts are off limit forever.
  if (asm_wasm_broken) return false;

  // In stress mode we want to run the validator on everything.
  if (v8_flags.stress_validate_asm) return true;

  // In general, we respect the "use asm" directive.
  return literal->scope()->IsAsmModule();
}
#endif

}  // namespace

void Compiler::InstallInterpreterTrampolineCopy(
    Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
    LogEventListener::CodeTag log_tag) {
  DCHECK(v8_flags.interpreted_frames_native_stack);
  if (!IsBytecodeArray(shared_info->GetTrustedData(isolate))) {
    DCHECK(!shared_info->HasInterpreterData(isolate));
    return;
  }
  DirectHandle<BytecodeArray> bytecode_array(
      shared_info->GetBytecodeArray(isolate), isolate);

  Handle<Code> code =
      Builtins::CreateInterpreterEntryTrampolineForProfiling(isolate);

  DirectHandle<InterpreterData> interpreter_data =
      isolate->factory()->NewInterpreterData(bytecode_array, code);

  if (shared_info->HasBaselineCode()) {
    shared_info->baseline_code(kAcquireLoad)
        ->set_bytecode_or_interpreter_data(*interpreter_data);
  } else {
    // IsBytecodeArray
    shared_info->set_interpreter_data(*interpreter_data);
  }

  DirectHandle<Script> script(Cast<Script>(shared_info->script()), isolate);
  Handle<AbstractCode> abstract_code = Cast<AbstractCode>(code);
  Script::PositionInfo info;
  Script::GetPositionInfo(script, shared_info->StartPosition(), &info);
  int line_num = info.line + 1;
  int column_num = info.column + 1;
  Handle<String> script_name =
      handle(IsString(script->name()) ? Cast<String>(script->name())
                                      : ReadOnlyRoots(isolate).empty_string(),
             isolate);
  PROFILE(isolate, CodeCreateEvent(log_tag, abstract_code, shared_info,
                                   script_name, line_num, column_num));
}

namespace {

template <typename IsolateT>
void InstallUnoptimizedCode(UnoptimizedCompilationInfo* compilation_info,
                            DirectHandle<SharedFunctionInfo> shared_info,
                            IsolateT* isolate) {
  if (compilation_info->has_bytecode_array()) {
    DCHECK(!shared_info->HasBytecodeArray());  // Only compiled once.
    DCHECK(!compilation_info->has_asm_wasm_data());
    DCHECK(!shared_info->HasFeedbackMetadata());

#if V8_ENABLE_WEBASSEMBLY
    // If the function failed asm-wasm compilation, mark asm_wasm as broken
    // to ensure we don't try to compile as asm-wasm.
    if (compilation_info->literal()->scope()->IsAsmModule()) {
      shared_info->set_is_asm_wasm_broken(true);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    DirectHandle<FeedbackMetadata> feedback_metadata = FeedbackMetadata::New(
        isolate, compilation_info->feedback_vector_spec());
    shared_info->set_feedback_metadata(*feedback_metadata, kReleaseStore);

    shared_info->set_age(0);
    shared_info->set_bytecode_array(*compilation_info->bytecode_array());
  } else {
#if V8_ENABLE_WEBASSEMBLY
    DCHECK(compilation_info->has_asm_wasm_data());
    // We should only have asm/wasm data when finalizing on the main thread.
    DCHECK((std::is_same<IsolateT, Isolate>::value));
    shared_info->set_asm_wasm_data(*compilation_info->asm_wasm_data());
    shared_info->set_feedback_metadata(
        ReadOnlyRoots(isolate).empty_feedback_metadata(), kReleaseStore);
#else
    UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
  }
}

template <typename IsolateT>
void EnsureInfosArrayOnScript(DirectHandle<Script> script,
                              ParseInfo* parse_info, IsolateT* isolate) {
  DCHECK(parse_info->flags().is_toplevel());
  if (script->infos()->length() > 0) {
    DCHECK_EQ(script->infos()->length(), parse_info->max_info_id() + 1);
    return;
  }
  DirectHandle<WeakFixedArray> infos(isolate->factory()->NewWeakFixedArray(
      parse_info->max_info_id() + 1, AllocationType::kOld));
  script->set_infos(*infos);
}

void UpdateSharedFunctionFlagsAfterCompilation(FunctionLiteral* literal) {
  Tagged<SharedFunctionInfo> shared_info = *literal->shared_function_info();
  DCHECK_EQ(shared_info->language_mode(), literal->language_mode());

  // These fields are all initialised in ParseInfo from the SharedFunctionInfo,
  // and then set back on the literal after parse. Hence, they should already
  // match.
  DCHECK_EQ(shared_info->requires_instance_members_initializer(),
            literal->requires_instance_members_initializer());
  DCHECK_EQ(shared_info->class_scope_has_private_brand(),
            literal->class_scope_has_private_brand());
  DCHECK_EQ(shared_info->has_static_private_methods_or_accessors(),
            literal->has_static_private_methods_or_accessors());

  shared_info->set_has_duplicate_parameters(
      literal->has_duplicate_parameters());
  shared_info->UpdateAndFinalizeExpectedNofPropertiesFromEstimate(literal);

  shared_info->SetScopeInfo(*literal->scope()->scope_info());
}

// Finalize a single compilation job. This function can return
// RETRY_ON_MAIN_THREAD if the job cannot be finalized off-thread, in which case
// it should be safe to call it again on the main thread with the same job.
template <typename IsolateT>
CompilationJob::Status FinalizeSingleUnoptimizedCompilationJob(
    UnoptimizedCompilationJob* job, Handle<SharedFunctionInfo> shared_info,
    IsolateT* isolate,
    FinalizeUnoptimizedCompilationDataList*
        finalize_unoptimized_compilation_data_list) {
  UnoptimizedCompilationInfo* compilation_info = job->compilation_info();

  CompilationJob::Status status = job->FinalizeJob(shared_info, isolate);
  if (status == CompilationJob::SUCCEEDED) {
    InstallUnoptimizedCode(compilation_info, shared_info, isolate);

    MaybeHandle<CoverageInfo> coverage_info;
    if (compilation_info->has_coverage_info()) {
      SharedMutexGuardIfOffThread<IsolateT, base::kShared> mutex_guard(
          isolate->shared_function_info_access(), isolate);
      if (!shared_info->HasCoverageInfo(
              isolate->GetMainThreadIsolateUnsafe())) {
        coverage_info = compilation_info->coverage_info();
      }
    }

    finalize_unoptimized_compilation_data_list->emplace_back(
        isolate, shared_info, coverage_info, job->time_taken_to_execute(),
        job->time_taken_to_finalize());
  }
  DCHECK_IMPLIES(status == CompilationJob::RETRY_ON_MAIN_THREAD,
                 (std::is_same<IsolateT, LocalIsolate>::value));
  return status;
}

std::unique_ptr<UnoptimizedCompilationJob>
ExecuteSingleUnoptimizedCompilationJob(
    ParseInfo* parse_info, FunctionLiteral* literal, Handle<Script> script,
    AccountingAllocator* allocator,
    std::vector<FunctionLiteral*>* eager_inner_literals,
    LocalIsolate* local_isolate) {
#if V8_ENABLE_WEBASSEMBLY
  if (UseAsmWasm(literal, parse_info->flags().is_asm_wasm_broken())) {
    std::unique_ptr<UnoptimizedCompilationJob> asm_job(
        AsmJs::NewCompilationJob(parse_info, literal, allocator));
    if (asm_job->ExecuteJob() == CompilationJob::SUCCEEDED) {
      return asm_job;
    }
    // asm.js validation failed, fall through to standard unoptimized compile.
    // Note: we rely on the fact that AsmJs jobs have done all validation in the
    // PrepareJob and ExecuteJob phases and can't fail in FinalizeJob with
    // with a validation error or another error that could be solve by falling
    // through to standard unoptimized compile.
  }
#endif
  std::unique_ptr<UnoptimizedCompilationJob> job(
      interpreter::Interpreter::NewCompilationJob(
          parse_info, literal, script, allocator, eager_inner_literals,
          local_isolate));

  if (job->ExecuteJob() != CompilationJob::SUCCEEDED) {
    // Compilation failed, return null.
    return std::unique_ptr<UnoptimizedCompilationJob>();
  }

  return job;
}

template <typename IsolateT>
bool IterativelyExecuteAndFinalizeUnoptimizedCompilationJobs(
    IsolateT* isolate, Handle<Script> script, ParseInfo* parse_info,
    AccountingAllocator* allocator, IsCompiledScope* is_compiled_scope,
    FinalizeUnoptimizedCompilationDataList*
        finalize_unoptimized_compilation_data_list,
    DeferredFinalizationJobDataList*
        jobs_to_retry_finalization_on_main_thread) {
  DeclarationScope::AllocateScopeInfos(parse_info, script, isolate);

  std::vector<FunctionLiteral*> functions_to_compile;
  functions_to_compile.push_back(parse_info->literal());

  bool compilation_succeeded = true;
  while (!functions_to_compile.empty()) {
    FunctionLiteral* literal = functions_to_compile.back();
    functions_to_compile.pop_back();
    Handle<SharedFunctionInfo> shared_info = literal->shared_function_info();
    // It's possible that compilation of an outer function overflowed the stack,
    // so a literal we'd like to compile won't have its SFI yet. Skip compiling
    // the inner function in that case.
    if (shared_info.is_null()) continue;
    if (shared_info->is_compiled()) continue;

    std::unique_ptr<UnoptimizedCompilationJob> job =
        ExecuteSingleUnoptimizedCompilationJob(parse_info, literal, script,
                                               allocator, &functions_to_compile,
                                               isolate->AsLocalIsolate());

    if (!job) {
      // Compilation failed presumably because of stack overflow, make sure
      // the shared function info contains uncompiled data for the next
      // compilation attempts.
      if (!shared_info->HasUncompiledData()) {
        SharedFunctionInfo::CreateAndSetUncompiledData(isolate, literal);
      }
      compilation_succeeded = false;
      // Proceed finalizing other functions in case they don't have uncompiled
      // data.
      continue;
    }

    UpdateSharedFunctionFlagsAfterCompilation(literal);

    auto finalization_status = FinalizeSingleUnoptimizedCompilationJob(
        job.get(), shared_info, isolate,
        finalize_unoptimized_compilation_data_list);

    switch (finalization_status) {
      case CompilationJob::SUCCEEDED:
        if (literal == parse_info->literal()) {
          // Ensure that the top level function is retained.
          *is_compiled_scope = shared_info->is_compiled_scope(isolate);
          DCHECK(is_compiled_scope->is_compiled());
        }
        break;

      case CompilationJob::FAILED:
        compilation_succeeded = false;
        // Proceed finalizing other functions in case they don't have uncompiled
        // data.
        continue;

      case CompilationJob::RETRY_ON_MAIN_THREAD:
        // This should not happen on the main thread.
        DCHECK((!std::is_same<IsolateT, Isolate>::value));
        DCHECK_NOT_NULL(jobs_to_retry_finalization_on_main_thread);

        // Clear the literal and ParseInfo to prevent further attempts to
        // access them.
        job->compilation_info()->ClearLiteral();
        job->ClearParseInfo();
        jobs_to_retry_finalization_on_main_thread->emplace_back(
            isolate, shared_info, std::move(job));
        break;
    }
  }

  // Report any warnings generated during compilation.
  if (parse_info->pending_error_handler()->has_pending_warnings()) {
    parse_info->pending_error_handler()->PrepareWarnings(isolate);
  }

  return compilation_succeeded;
}

bool FinalizeDeferredUnoptimizedCompilationJobs(
    Isolate* isolate, DirectHandle<Script> script,
    DeferredFinalizationJobDataList* deferred_jobs,
    PendingCompilationErrorHandler* pending_error_handler,
    FinalizeUnoptimizedCompilationDataList*
        finalize_unoptimized_compilation_data_list) {
  DCHECK(AllowCompilation::IsAllowed(isolate));

  if (deferred_jobs->empty()) return true;

  // TODO(rmcilroy): Clear native context in debug once AsmJS generates doesn't
  // rely on accessing native context during finalization.

  // Finalize the deferred compilation jobs.
  for (auto&& job : *deferred_jobs) {
    Handle<SharedFunctionInfo> shared_info = job.function_handle();
    if (FinalizeSingleUnoptimizedCompilationJob(
            job.job(), shared_info, isolate,
            finalize_unoptimized_compilation_data_list) !=
        CompilationJob::SUCCEEDED) {
      return false;
    }
  }

  // Report any warnings generated during deferred finalization.
  if (pending_error_handler->has_pending_warnings()) {
    pending_error_handler->PrepareWarnings(isolate);
  }

  return true;
}

// A wrapper to access the optimized code cache slots on the feedback vector.
class OptimizedCodeCache : public AllStatic {
 public:
  static V8_WARN_UNUSED_RESULT MaybeHandle<Code> Get(
      Isolate* isolate, DirectHandle<JSFunction> function,
      BytecodeOffset osr_offset, CodeKind code_kind) {
    if (!CodeKindIsStoredInOptimizedCodeCache(code_kind)) return {};
    if (!function->has_feedback_vector()) return {};

    DisallowGarbageCollection no_gc;
    Tagged<SharedFunctionInfo> shared = function->shared();
    RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileGetFromOptimizedCodeMap);

    Tagged<Code> code;
    Tagged<FeedbackVector> feedback_vector = function->feedback_vector();
    if (IsOSR(osr_offset)) {
      Handle<BytecodeArray> bytecode(shared->GetBytecodeArray(isolate),
                                     isolate);
      interpreter::BytecodeArrayIterator it(bytecode, osr_offset.ToInt());
      DCHECK_EQ(it.current_bytecode(), interpreter::Bytecode::kJumpLoop);
      std::optional<Tagged<Code>> maybe_code =
          feedback_vector->GetOptimizedOsrCode(isolate, it.GetSlotOperand(2));
      if (maybe_code.has_value()) code = maybe_code.value();
    } else {
      feedback_vector->EvictOptimizedCodeMarkedForDeoptimization(
          isolate, shared, "OptimizedCodeCache::Get");
      code = feedback_vector->optimized_code(isolate);
    }

    if (code.is_null() || code->kind() != code_kind) return {};

    DCHECK(!code->marked_for_deoptimization());
    DCHECK(shared->is_compiled());
    DCHECK(CodeKindIsStoredInOptimizedCodeCache(code->kind()));
    DCHECK_IMPLIES(IsOSR(osr_offset), CodeKindCanOSR(code->kind()));

    CompilerTracer::TraceOptimizedCodeCacheHit(isolate, function, osr_offset,
                                               code_kind);
    return handle(code, isolate);
  }

  static void Insert(Isolate* isolate, Tagged<JSFunction> function,
                     BytecodeOffset osr_offset, Tagged<Code> code,
                     bool is_function_context_specializing) {
    const CodeKind kind = code->kind();
    if (!CodeKindIsStoredInOptimizedCodeCache(kind)) return;

    Tagged<FeedbackVector> feedback_vector = function->feedback_vector();

    if (IsOSR(osr_offset)) {
      DCHECK(CodeKindCanOSR(kind));
      DCHECK(!is_function_context_specializing);
      Tagged<SharedFunctionInfo> shared = function->shared();
      Handle<BytecodeArray> bytecode(shared->GetBytecodeArray(isolate),
                                     isolate);
      interpreter::BytecodeArrayIterator it(bytecode, osr_offset.ToInt());
      DCHECK_EQ(it.current_bytecode(), interpreter::Bytecode::kJumpLoop);
      feedback_vector->SetOptimizedOsrCode(isolate, it.GetSlotOperand(2), code);
      return;
    }

    DCHECK(!IsOSR(osr_offset));

    if (is_function_context_specializing) {
      // Function context specialization folds-in the function context, so no
      // sharing can occur. Make sure the optimized code cache is cleared.
      // Only do so if the specialized code's kind matches the cached code kind.
      if (feedback_vector->has_optimized_code() &&
          feedback_vector->optimized_code(isolate)->kind() == code->kind()) {
        feedback_vector->ClearOptimizedCode();
      }
      return;
    }

    function->shared()->set_function_context_independent_compiled(true);
    feedback_vector->SetOptimizedCode(isolate, code);
  }
};

// Runs PrepareJob in the proper compilation scopes. Handles will be allocated
// in a persistent handle scope that is detached and handed off to the
// {compilation_info} after PrepareJob.
bool PrepareJobWithHandleScope(OptimizedCompilationJob* job, Isolate* isolate,
                               OptimizedCompilationInfo* compilation_info,
                               ConcurrencyMode mode) {
  CompilationHandleScope compilation(isolate, compilation_info);
  CompilerTracer::TracePrepareJob(isolate, compilation_info, mode);
  compilation_info->ReopenAndCanonicalizeHandlesInNewScope(isolate);
  return job->PrepareJob(isolate) == CompilationJob::SUCCEEDED;
}

bool CompileTurbofan_NotConcurrent(Isolate* isolate,
                                   TurbofanCompilationJob* job) {
  OptimizedCompilationInfo* const compilation_info = job->compilation_info();
  DCHECK_EQ(compilation_info->code_kind(), CodeKind::TURBOFAN);

  TimerEventScope<TimerEventRecompileSynchronous> timer(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeNonConcurrent);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.OptimizeNonConcurrent");

  if (!PrepareJobWithHandleScope(job, isolate, compilation_info,
                                 ConcurrencyMode::kSynchronous)) {
    CompilerTracer::TraceAbortedJob(isolate, compilation_info,
                                    job->prepare_in_ms(), job->execute_in_ms(),
                                    job->finalize_in_ms());
    return false;
  }

  if (job->ExecuteJob(isolate->counters()->runtime_call_stats(),
                      isolate->main_thread_local_isolate())) {
    CompilerTracer::TraceAbortedJob(isolate, compilation_info,
                                    job->prepare_in_ms(), job->execute_in_ms(),
                                    job->finalize_in_ms());
    return false;
  }

  if (job->FinalizeJob(isolate) != CompilationJob::SUCCEEDED) {
    CompilerTracer::TraceAbortedJob(isolate, compilation_info,
                                    job->prepare_in_ms(), job->execute_in_ms(),
                                    job->finalize_in_ms());
    return false;
  }

  // Success!
  job->RecordCompilationStats(ConcurrencyMode::kSynchronous, isolate);
  DCHECK(!isolate->has_exception());
  OptimizedCodeCache::Insert(isolate, *compilation_info->closure(),
                             compilation_info->osr_offset(),
                             *compilation_info->code(),
                             compilation_info->function_context_specializing());
  job->RecordFunctionCompilation(LogEventListener::CodeTag::kFunction, isolate);
  return true;
}

bool CompileTurbofan_Concurrent(Isolate* isolate,
                                std::unique_ptr<TurbofanCompilationJob> job) {
  OptimizedCompilationInfo* const compilation_info = job->compilation_info();
  DCHECK_EQ(compilation_info->code_kind(), CodeKind::TURBOFAN);
  DirectHandle<JSFunction> function = compilation_info->closure();

  if (!isolate->optimizing_compile_dispatcher()->IsQueueAvailable()) {
    if (v8_flags.trace_concurrent_recompilation) {
      PrintF("  ** Compilation queue full, will retry optimizing ");
      ShortPrint(*function);
      PrintF(" later.\n");
    }
    return false;
  }

  if (isolate->heap()->HighMemoryPressure()) {
    if (v8_flags.trace_concurrent_recompilation) {
      PrintF("  ** High memory pressure, will retry optimizing ");
      ShortPrint(*function);
      PrintF(" later.\n");
    }
    return false;
  }

  TimerEventScope<TimerEventRecompileSynchronous> timer(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeConcurrentPrepare);
  TRACE_EVENT_WITH_FLOW0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                         "V8.OptimizeConcurrentPrepare", job->trace_id(),
                         TRACE_EVENT_FLAG_FLOW_OUT);

  if (!PrepareJobWithHandleScope(job.get(), isolate, compilation_info,
                                 ConcurrencyMode::kConcurrent)) {
    return false;
  }

  // The background recompile will own this job.
  isolate->optimizing_compile_dispatcher()->QueueForOptimization(job.release());

  if (v8_flags.trace_concurrent_recompilation) {
    PrintF("  ** Queued ");
    ShortPrint(*function);
    PrintF(" for concurrent optimization.\n");
  }

  SetTieringState(isolate, *function, compilation_info->osr_offset(),
                  TieringState::kInProgress);

  DCHECK(compilation_info->shared_info()->HasBytecodeArray());
  return true;
}

enum class CompileResultBehavior {
  // Default behavior, i.e. install the result, insert into caches, etc.
  kDefault,
  // Used only for stress testing. The compilation result should be discarded.
  kDiscardForTesting,
};

bool ShouldOptimize(CodeKind code_kind,
                    DirectHandle<SharedFunctionInfo> shared) {
  DCHECK(CodeKindIsOptimizedJSFunction(code_kind));
  switch (code_kind) {
    case CodeKind::TURBOFAN:
      return v8_flags.turbofan && shared->PassesFilter(v8_flags.turbo_filter);
    case CodeKind::MAGLEV:
      return maglev::IsMaglevEnabled() &&
             shared->PassesFilter(v8_flags.maglev_filter);
    default:
      UNREACHABLE();
  }
}

MaybeHandle<Code> CompileTurbofan(Isolate* isolate, Handle<JSFunction> function,
                                  DirectHandle<SharedFunctionInfo> shared,
                                  ConcurrencyMode mode,
                                  BytecodeOffset osr_offset,
                                  CompileResultBehavior result_behavior) {
  VMState<COMPILER> state(isolate);
  TimerEventScope<TimerEventOptimizeCode> optimize_code_timer(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeCode);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.OptimizeCode");

  DCHECK(!isolate->has_exception());
  PostponeInterruptsScope postpone(isolate);
  const compiler::IsScriptAvailable has_script =
      IsScript(shared->script()) ? compiler::IsScriptAvailable::kYes
                                 : compiler::IsScriptAvailable::kNo;
  // BUG(5946): This DCHECK is necessary to make certain that we won't
  // tolerate the lack of a script without bytecode.
  DCHECK_IMPLIES(has_script == compiler::IsScriptAvailable::kNo,
                 shared->HasBytecodeArray());
  std::unique_ptr<TurbofanCompilationJob> job(
      compiler::NewCompilationJob(isolate, function, has_script, osr_offset));

  if (result_behavior == CompileResultBehavior::kDiscardForTesting) {
    job->compilation_info()->set_discard_result_for_testing();
  }

  if (IsOSR(osr_offset)) {
    isolate->CountUsage(v8::Isolate::kTurboFanOsrCompileStarted);
  }

  // Prepare the job and launch concurrent compilation, or compile now.
  if (IsConcurrent(mode)) {
    if (CompileTurbofan_Concurrent(isolate, std::move(job))) return {};
  } else {
    DCHECK(IsSynchronous(mode));
    if (CompileTurbofan_NotConcurrent(isolate, job.get())) {
      return job->compilation_info()->code();
    }
  }

  if (isolate->has_exception()) isolate->clear_exception();
  return {};
}

#ifdef V8_ENABLE_MAGLEV
// TODO(v8:7700): Record maglev compilations better.
void RecordMaglevFunctionCompilation(Isolate* isolate,
                                     DirectHandle<JSFunction> function,
                                     Handle<AbstractCode> code) {
  PtrComprCageBase cage_base(isolate);
  Handle<SharedFunctionInfo> shared(function->shared(cage_base), isolate);
  DirectHandle<Script> script(Cast<Script>(shared->script(cage_base)), isolate);
  Handle<FeedbackVector> feedback_vector(function->feedback_vector(cage_base),
                                         isolate);

  // Optimistic estimate.
  double time_taken_ms = 0;

  Compiler::LogFunctionCompilation(
      isolate, LogEventListener::CodeTag::kFunction, script, shared,
      feedback_vector, code, code->kind(cage_base), time_taken_ms);
}
#endif  // V8_ENABLE_MAGLEV

MaybeHandle<Code> CompileMaglev(Isolate* isolate, Handle<JSFunction> function,
                                ConcurrencyMode mode, BytecodeOffset osr_offset,
                                CompileResultBehavior result_behavior) {
#ifdef V8_ENABLE_MAGLEV
  DCHECK(maglev::IsMaglevEnabled());
  CHECK(result_behavior == CompileResultBehavior::kDefault);

  // TODO(v8:7700): Tracing, see CompileTurbofan.

  DCHECK(!isolate->has_exception());
  PostponeInterruptsScope postpone(isolate);

  // TODO(v8:7700): See everything in CompileTurbofan_Concurrent.
  // - Tracing,
  // - timers,
  // - aborts on memory pressure,
  // ...

  // Prepare the job.
  auto job = maglev::MaglevCompilationJob::New(isolate, function, osr_offset);

  if (IsConcurrent(mode) &&
      !isolate->maglev_concurrent_dispatcher()->is_enabled()) {
    mode = ConcurrencyMode::kSynchronous;
  }

  {
    TRACE_EVENT_WITH_FLOW0(
        TRACE_DISABLED_BY_DEFAULT("v8.compile"),
        IsSynchronous(mode) ? "V8.MaglevPrepare" : "V8.MaglevConcurrentPrepare",
        job->trace_id(), TRACE_EVENT_FLAG_FLOW_OUT);
    CompilerTracer::TraceStartMaglevCompile(isolate, function, job->is_osr(),
                                            mode);
    CompilationJob::Status status = job->PrepareJob(isolate);
    CHECK_EQ(status, CompilationJob::SUCCEEDED);  // TODO(v8:7700): Use status.
  }

  if (IsSynchronous(mode)) {
    CompilationJob::Status status =
        job->ExecuteJob(isolate->counters()->runtime_call_stats(),
                        isolate->main_thread_local_isolate());
    if (status == CompilationJob::FAILED) {
      return {};
    }
    CHECK_EQ(status, CompilationJob::SUCCEEDED);

    Compiler::FinalizeMaglevCompilationJob(job.get(), isolate);

    return job->code();
  }

  DCHECK(IsConcurrent(mode));

  // Enqueue it.
  isolate->maglev_concurrent_dispatcher()->EnqueueJob(std::move(job));

  // Remember that the function is currently being processed.
  SetTieringState(isolate, *function, osr_offset, TieringState::kInProgress);
  function->SetInterruptBudget(isolate, CodeKind::MAGLEV);

  return {};
#else   // V8_ENABLE_MAGLEV
  UNREACHABLE();
#endif  // V8_ENABLE_MAGLEV
}

MaybeHandle<Code> GetOrCompileOptimized(
    Isolate* isolate, Handle<JSFunction> function, ConcurrencyMode mode,
    CodeKind code_kind, BytecodeOffset osr_offset = BytecodeOffset::None(),
    CompileResultBehavior result_behavior = CompileResultBehavior::kDefault) {
  DCHECK(CodeKindIsOptimizedJSFunction(code_kind));

  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate);

  // Clear the optimization marker on the function so that we don't try to
  // re-optimize.
  if (!IsOSR(osr_offset)) {
    ResetTieringState(isolate, *function, osr_offset);
    int invocation_count =
        function->feedback_vector()->invocation_count(kRelaxedLoad);
    if (!(V8_UNLIKELY(v8_flags.testing_d8_test_runner ||
                      v8_flags.allow_natives_syntax) &&
          ManualOptimizationTable::IsMarkedForManualOptimization(isolate,
                                                                 *function)) &&
        invocation_count < v8_flags.minimum_invocations_before_optimization) {
      function->feedback_vector()->set_invocation_count(invocation_count + 1,
                                                        kRelaxedStore);
      return {};
    }
  }

  // TODO(v8:7700): Distinguish between Maglev and Turbofan.
  if (shared->optimization_disabled() &&
      shared->disabled_optimization_reason() == BailoutReason::kNeverOptimize) {
    return {};
  }

  // Do not optimize when debugger needs to hook into every call.
  if (isolate->debug()->needs_check_on_function_call()) {
    // Reset the OSR urgency to avoid triggering this compilation request on
    // every iteration and thereby skipping other interrupts.
    if (IsOSR(osr_offset)) {
      function->feedback_vector()->reset_osr_urgency();
    }
    return {};
  }

  // Do not optimize if we need to be able to set break points.
  if (shared->HasBreakInfo(isolate)) return {};

  // Do not optimize if optimization is disabled or function doesn't pass
  // turbo_filter.
  if (!ShouldOptimize(code_kind, shared)) return {};

  Handle<Code> cached_code;
  if (OptimizedCodeCache::Get(isolate, function, osr_offset, code_kind)
          .ToHandle(&cached_code)) {
    if (IsOSR(osr_offset)) {
      if (!function->osr_tiering_in_progress()) {
        function->feedback_vector()->reset_osr_urgency();
      }
    } else {
      DCHECK_LE(cached_code->kind(), code_kind);
    }
    return cached_code;
  }

  if (IsOSR(osr_offset)) {
    // One OSR job per function at a time.
    if (function->osr_tiering_in_progress()) {
      return {};
    }

    function->feedback_vector()->reset_osr_urgency();
  }

  DCHECK(shared->is_compiled());

  if (code_kind == CodeKind::TURBOFAN) {
    return CompileTurbofan(isolate, function, shared, mode, osr_offset,
                           result_behavior);
  } else {
    DCHECK_EQ(code_kind, CodeKind::MAGLEV);
    return CompileMaglev(isolate, function, mode, osr_offset, result_behavior);
  }
}

// When --stress-concurrent-inlining is enabled, spawn concurrent jobs in
// addition to non-concurrent compiles to increase coverage in mjsunit tests
// (where most interesting compiles are non-concurrent). The result of the
// compilation is thrown out.
void SpawnDuplicateConcurrentJobForStressTesting(Isolate* isolate,
                                                 Handle<JSFunction> function,
                                                 ConcurrencyMode mode,
                                                 CodeKind code_kind) {
  // TODO(v8:7700): Support Maglev.
  if (code_kind == CodeKind::MAGLEV) return;

  DCHECK(v8_flags.stress_concurrent_inlining &&
         isolate->concurrent_recompilation_enabled() && IsSynchronous(mode) &&
         isolate->node_observer() == nullptr);
  CompileResultBehavior result_behavior =
      v8_flags.stress_concurrent_inlining_attach_code
          ? CompileResultBehavior::kDefault
          : CompileResultBehavior::kDiscardForTesting;
  USE(GetOrCompileOptimized(isolate, function, ConcurrencyMode::kConcurrent,
                            code_kind, BytecodeOffset::None(),
                            result_behavior));
}

bool FailAndClearException(Isolate* isolate) {
  isolate->clear_internal_exception();
  return false;
}

template <typename IsolateT>
bool PrepareException(IsolateT* isolate, ParseInfo* parse_info) {
  if (parse_info->pending_error_handler()->has_pending_error()) {
    parse_info->pending_error_handler()->PrepareErrors(
        isolate, parse_info->ast_value_factory());
  }
  return false;
}

bool FailWithPreparedException(
    Isolate* isolate, Handle<Script> script,
    const PendingCompilationErrorHandler* pending_error_handler,
    Compiler::ClearExceptionFlag flag = Compiler::KEEP_EXCEPTION) {
  if (flag == Compiler::CLEAR_EXCEPTION) {
    return FailAndClearException(isolate);
  }

  if (!isolate->has_exception()) {
    if (pending_error_handler->has_pending_error()) {
      pending_error_handler->ReportErrors(isolate, script);
    } else {
      isolate->StackOverflow();
    }
  }
  return false;
}

bool FailWithException(Isolate* isolate, Handle<Script> script,
                       ParseInfo* parse_info,
                       Compiler::ClearExceptionFlag flag) {
  PrepareException(isolate, parse_info);
  return FailWithPreparedException(isolate, script,
                                   parse_info->pending_error_handler(), flag);
}

void FinalizeUnoptimizedCompilation(
    Isolate* isolate, Handle<Script> script,
    const UnoptimizedCompileFlags& flags,
    const UnoptimizedCompileState* compile_state,
    const FinalizeUnoptimizedCompilationDataList&
        finalize_unoptimized_compilation_data_list) {
  if (compile_state->pending_error_handler()->has_pending_warnings()) {
    compile_state->pending_error_handler()->ReportWarnings(isolate, script);
  }

  bool need_source_positions =
      v8_flags.stress_lazy_source_positions ||
      (!flags.collect_source_positions() && isolate->NeedsSourcePositions());

  for (const auto& finalize_data : finalize_unoptimized_compilation_data_list) {
    Handle<SharedFunctionInfo> shared_info = finalize_data.function_handle();
    // It's unlikely, but possible, that the bytecode was flushed between being
    // allocated and now, so guard against that case, and against it being
    // flushed in the middle of this loop.
    IsCompiledScope is_compiled_scope(*shared_info, isolate);
    if (!is_compiled_scope.is_compiled()) continue;

    if (need_source_positions) {
      SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate, shared_info);
    }
    LogEventListener::CodeTag log_tag;
    if (shared_info->is_toplevel()) {
      log_tag = flags.is_eval() ? LogEventListener::CodeTag::kEval
                                : LogEventListener::CodeTag::kScript;
    } else {
      log_tag = LogEventListener::CodeTag::kFunction;
    }
    log_tag = V8FileLogger::ToNativeByScript(log_tag, *script);
    if (v8_flags.interpreted_frames_native_stack &&
        isolate->logger()->is_listening_to_code_events()) {
      Compiler::InstallInterpreterTrampolineCopy(isolate, shared_info, log_tag);
    }
    Handle<CoverageInfo> coverage_info;
    if (finalize_data.coverage_info().ToHandle(&coverage_info)) {
      isolate->debug()->InstallCoverageInfo(shared_info, coverage_info);
    }

    LogUnoptimizedCompilation(isolate, shared_info, log_tag,
                              finalize_data.time_taken_to_execute(),
                              finalize_data.time_taken_to_finalize());
  }
}

void FinalizeUnoptimizedScriptCompilation(
    Isolate* isolate, Handle<Script> script,
    const UnoptimizedCompileFlags& flags,
    const UnoptimizedCompileState* compile_state,
    const FinalizeUnoptimizedCompilationDataList&
        finalize_unoptimized_compilation_data_list) {
  FinalizeUnoptimizedCompilation(isolate, script, flags, compile_state,
                                 finalize_unoptimized_compilation_data_list);

  script->set_compilation_state(Script::CompilationState::kCompiled);
  DCHECK_IMPLIES(isolate->NeedsSourcePositions(), script->has_line_ends());
}

void CompileAllWithBaseline(Isolate* isolate,
                            const FinalizeUnoptimizedCompilationDataList&
                                finalize_unoptimized_compilation_data_list) {
  for (const auto& finalize_data : finalize_unoptimized_compilation_data_list) {
    Handle<SharedFunctionInfo> shared_info = finalize_data.function_handle();
    IsCompiledScope is_compiled_scope(*shared_info, isolate);
    if (!is_compiled_scope.is_compiled()) continue;
    if (!CanCompileWithBaseline(isolate, *shared_info)) continue;
    Compiler::CompileSharedWithBaseline(
        isolate, shared_info, Compiler::CLEAR_EXCEPTION, &is_compiled_scope);
  }
}

// Create shared function info for top level and shared function infos array for
// inner functions.
template <typename IsolateT>
Handle<SharedFunctionInfo> CreateTopLevelSharedFunctionInfo(
    ParseInfo* parse_info, Handle<Script> script, IsolateT* isolate) {
  EnsureInfosArrayOnScript(script, parse_info, isolate);
  DCHECK_EQ(kNoSourcePosition,
            parse_info->literal()->function_token_position());
  return isolate->factory()->NewSharedFunctionInfoForLiteral(
      parse_info->literal(), script, true);
}

Handle<SharedFunctionInfo> GetOrCreateTopLevelSharedFunctionInfo(
    ParseInfo* parse_info, Handle<Script> script, Isolate* isolate,
    IsCompiledScope* is_compiled_scope) {
  EnsureInfosArrayOnScript(script, parse_info, isolate);
  MaybeHandle<SharedFunctionInfo> maybe_shared =
      Script::FindSharedFunctionInfo(script, isolate, parse_info->literal());
  if (Handle<SharedFunctionInfo> shared; maybe_shared.ToHandle(&shared)) {
    DCHECK_EQ(shared->function_literal_id(),
              parse_info->literal()->function_literal_id());
    *is_compiled_scope = shared->is_compiled_scope(isolate);
    return shared;
  }
  return CreateTopLevelSharedFunctionInfo(parse_info, script, isolate);
}

MaybeHandle<SharedFunctionInfo> CompileToplevel(
    ParseInfo* parse_info, Handle<Script> script,
    MaybeHandle<ScopeInfo> maybe_outer_scope_info, Isolate* isolate,
    IsCompiledScope* is_compiled_scope) {
  TimerEventScope<TimerEventCompileCode> top_level_timer(isolate);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.CompileCode");
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  PostponeInterruptsScope postpone(isolate);
  DCHECK(!isolate->native_context().is_null());
  RCS_SCOPE(isolate, parse_info->flags().is_eval()
                         ? RuntimeCallCounterId::kCompileEval
                         : RuntimeCallCounterId::kCompileScript);
  VMState<BYTECODE_COMPILER> state(isolate);
  if (parse_info->literal() == nullptr &&
      !parsing::ParseProgram(parse_info, script, maybe_outer_scope_info,
                             isolate, parsing::ReportStatisticsMode::kYes)) {
    FailWithException(isolate, script, parse_info,
                      Compiler::ClearExceptionFlag::KEEP_EXCEPTION);
    return MaybeHandle<SharedFunctionInfo>();
  }
  // Measure how long it takes to do the compilation; only take the
  // rest of the function into account to avoid overlap with the
  // parsing statistics.
  NestedTimedHistogram* rate = parse_info->flags().is_eval()
                                   ? isolate->counters()->compile_eval()
                                   : isolate->counters()->compile();
  NestedTimedHistogramScope timer(rate);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               parse_info->flags().is_eval() ? "V8.CompileEval" : "V8.Compile");

  // Create the SharedFunctionInfo and add it to the script's list.
  Handle<SharedFunctionInfo> shared_info =
      GetOrCreateTopLevelSharedFunctionInfo(parse_info, script, isolate,
                                            is_compiled_scope);

  FinalizeUnoptimizedCompilationDataList
      finalize_unoptimized_compilation_data_list;

  // Prepare and execute compilation of the outer-most function.
  if (!IterativelyExecuteAndFinalizeUnoptimizedCompilationJobs(
          isolate, script, parse_info, isolate->allocator(), is_compiled_scope,
          &finalize_unoptimized_compilation_data_list, nullptr)) {
    FailWithException(isolate, script, parse_info,
                      Compiler::ClearExceptionFlag::KEEP_EXCEPTION);
    return MaybeHandle<SharedFunctionInfo>();
  }

  // Character stream shouldn't be used again.
  parse_info->ResetCharacterStream();

  FinalizeUnoptimizedScriptCompilation(
      isolate, script, parse_info->flags(), parse_info->state(),
      finalize_unoptimized_compilation_data_list);

  if (v8_flags.always_sparkplug) {
    CompileAllWithBaseline(isolate, finalize_unoptimized_compilation_data_list);
  }

  return shared_info;
}

#ifdef V8_RUNTIME_CALL_STATS
RuntimeCallCounterId RuntimeCallCounterIdForCompile(ParseInfo* parse_info) {
  if (parse_info->flags().is_toplevel()) {
    if (parse_info->flags().is_eval()) {
      return RuntimeCallCounterId::kCompileEval;
    }
    return RuntimeCallCounterId::kCompileScript;
  }
  return RuntimeCallCounterId::kCompileFunction;
}
#endif  // V8_RUNTIME_CALL_STATS

}  // namespace

CompilationHandleScope::~CompilationHandleScope() {
  info_->set_persistent_handles(persistent_.Detach());
}

FinalizeUnoptimizedCompilationData::FinalizeUnoptimizedCompilationData(
    LocalIsolate* isolate, Handle<SharedFunctionInfo> function_handle,
    MaybeHandle<CoverageInfo> coverage_info,
    base::TimeDelta time_taken_to_execute,
    base::TimeDelta time_taken_to_finalize)
    : time_taken_to_execute_(time_taken_to_execute),
      time_taken_to_finalize_(time_taken_to_finalize),
      function_handle_(isolate->heap()->NewPersistentHandle(function_handle)),
      coverage_info_(isolate->heap()->NewPersistentMaybeHandle(coverage_info)) {
}

DeferredFinalizationJobData::DeferredFinalizationJobData(
    LocalIsolate* isolate, Handle<SharedFunctionInfo> function_handle,
    std::unique_ptr<UnoptimizedCompilationJob> job)
    : function_handle_(isolate->heap()->NewPersistentHandle(function_handle)),
      job_(std::move(job)) {}

BackgroundCompileTask::BackgroundCompileTask(
    ScriptStreamingData* streamed_data, Isolate* isolate, ScriptType type,
    ScriptCompiler::CompileOptions options,
    ScriptCompiler::CompilationDetails* compilation_details,
    CompileHintCallback compile_hint_callback, void* compile_hint_callback_data)
    : isolate_for_local_isolate_(isolate),
      flags_(UnoptimizedCompileFlags::ForToplevelCompile(
          isolate, true, construct_language_mode(v8_flags.use_strict),
          REPLMode::kNo, type,
          (options & ScriptCompiler::CompileOptions::kEagerCompile) == 0 &&
              v8_flags.lazy_streaming)),
      character_stream_(ScannerStream::For(streamed_data->source_stream.get(),
                                           streamed_data->encoding)),
      stack_size_(v8_flags.stack_size),
      worker_thread_runtime_call_stats_(
          isolate->counters()->worker_thread_runtime_call_stats()),
      timer_(isolate->counters()->compile_script_on_background()),
      compilation_details_(compilation_details),
      start_position_(0),
      end_position_(0),
      function_literal_id_(kFunctionLiteralIdTopLevel),
      compile_hint_callback_(compile_hint_callback),
      compile_hint_callback_data_(compile_hint_callback_data) {
  if (options & ScriptCompiler::CompileOptions::kProduceCompileHints) {
    flags_.set_produce_compile_hints(true);
  }
  DCHECK(is_streaming_compilation());
  if (options & ScriptCompiler::kConsumeCompileHints) {
    DCHECK_NOT_NULL(compile_hint_callback);
    DCHECK_NOT_NULL(compile_hint_callback_data);
  } else {
    DCHECK_NULL(compile_hint_callback);
    DCHECK_NULL(compile_hint_callback_data);
  }
  flags_.set_compile_hints_magic_enabled(
      options &
      ScriptCompiler::CompileOptions::kFollowCompileHintsMagicComment);
}

BackgroundCompileTask::BackgroundCompileTask(
    Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
    std::unique_ptr<Utf16CharacterStream> character_stream,
    WorkerThreadRuntimeCallStats* worker_thread_runtime_stats,
    TimedHistogram* timer, int max_stack_size)
    : isolate_for_local_isolate_(isolate),
      // TODO(leszeks): Create this from parent compile flags, to avoid
      // accessing the Isolate.
      flags_(
          UnoptimizedCompileFlags::ForFunctionCompile(isolate, *shared_info)),
      character_stream_(std::move(character_stream)),
      stack_size_(max_stack_size),
      worker_thread_runtime_call_stats_(worker_thread_runtime_stats),
      timer_(timer),
      compilation_details_(nullptr),
      input_shared_info_(shared_info),
      start_position_(shared_info->StartPosition()),
      end_position_(shared_info->EndPosition()),
      function_literal_id_(shared_info->function_literal_id()) {
  DCHECK(!shared_info->is_toplevel());
  DCHECK(!is_streaming_compilation());

  character_stream_->Seek(start_position_);

  // Get the script out of the outer ParseInfo and turn it into a persistent
  // handle we can transfer to the background thread.
  persistent_handles_ = std::make_unique<PersistentHandles>(isolate);
  input_shared_info_ = persistent_handles_->NewHandle(shared_info);
}

BackgroundCompileTask::~BackgroundCompileTask() = default;

void SetScriptFieldsFromDetails(Isolate* isolate, Tagged<Script> script,
                                const ScriptDetails& script_details,
                                DisallowGarbageCollection* no_gc) {
  Handle<Object> script_name;
  if (script_details.name_obj.ToHandle(&script_name)) {
    script->set_name(*script_name);
    script->set_line_offset(script_details.line_offset);
    script->set_column_offset(script_details.column_offset);
  }
  // The API can provide a source map URL, but a source map URL could also have
  // been inferred by the parser from a magic comment. The API source map URL
  // takes precedence (as long as it is a non-empty string).
  Handle<Object> source_map_url;
  if (script_details.source_map_url.ToHandle(&source_map_url) &&
      IsString(*source_map_url) &&
      Cast<String>(*source_map_url)->length() > 0) {
    script->set_source_mapping_url(*source_map_url);
  }
  Handle<Object> host_defined_options;
  if (script_details.host_defined_options.ToHandle(&host_defined_options)) {
    // TODO(cbruni, chromium:1244145): Remove once migrated to the context.
    if (IsFixedArray(*host_defined_options)) {
      script->set_host_defined_options(Cast<FixedArray>(*host_defined_options));
    }
  }
}

namespace {

#ifdef ENABLE_SLOW_DCHECKS

// A class which traverses the object graph for a newly compiled Script and
// ensures that it contains pointers to Scripts, ScopeInfos and
// SharedFunctionInfos only at the expected locations. Any failure in this
// visitor indicates a case that is probably not handled correctly in
// BackgroundMergeTask.
class MergeAssumptionChecker final : public ObjectVisitor {
 public:
  explicit MergeAssumptionChecker(PtrComprCageBase cage_base)
      : cage_base_(cage_base) {}

  void IterateObjects(Tagged<HeapObject> start) {
    QueueVisit(start, kNormalObject);
    while (to_visit_.size() > 0) {
      std::pair<Tagged<HeapObject>, ObjectKind> pair = to_visit_.top();
      to_visit_.pop();
      Tagged<HeapObject> current = pair.first;
      // The Script's infos list and the constant pools for all
      // BytecodeArrays are expected to contain pointers to SharedFunctionInfos.
      // However, the type of those objects (FixedArray or WeakFixedArray)
      // doesn't have enough information to indicate their usage, so we enqueue
      // those objects here rather than during VisitPointers.
      if (IsScript(current)) {
        Tagged<HeapObject> infos = Cast<Script>(current)->infos();
        QueueVisit(infos, kScriptInfosList);
      } else if (IsBytecodeArray(current)) {
        Tagged<HeapObject> constants =
            Cast<BytecodeArray>(current)->constant_pool();
        QueueVisit(constants, kConstantPool);
      }
      current_object_kind_ = pair.second;
      current->IterateBody(cage_base_, this);
      QueueVisit(current->map(), kNormalObject);
    }
  }

  // ObjectVisitor implementation:
  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    MaybeObjectSlot maybe_start(start);
    MaybeObjectSlot maybe_end(end);
    VisitPointers(host, maybe_start, maybe_end);
  }
  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    for (MaybeObjectSlot current = start; current != end; ++current) {
      Tagged<MaybeObject> maybe_obj = current.load(cage_base_);
      Tagged<HeapObject> obj;
      bool is_weak = maybe_obj.IsWeak();
      if (maybe_obj.GetHeapObject(&obj)) {
        if (IsSharedFunctionInfo(obj)) {
          CHECK((current_object_kind_ == kConstantPool && !is_weak) ||
                (current_object_kind_ == kScriptInfosList && is_weak));
        } else if (IsScopeInfo(obj)) {
          CHECK((current_object_kind_ == kConstantPool && !is_weak) ||
                (current_object_kind_ == kNormalObject && !is_weak) ||
                (current_object_kind_ == kScriptInfosList && is_weak));
        } else if (IsScript(obj)) {
          CHECK(IsSharedFunctionInfo(host) &&
                current == MaybeObjectSlot(host.address() +
                                           SharedFunctionInfo::kScriptOffset));
        } else if (IsFixedArray(obj) && current_object_kind_ == kConstantPool) {
          // Constant pools can contain nested fixed arrays, which in turn can
          // point to SFIs.
          QueueVisit(obj, kConstantPool);
        }

        QueueVisit(obj, kNormalObject);
      }
    }
  }

  // The object graph for a newly compiled Script shouldn't yet contain any
  // Code. If any of these functions are called, then that would indicate that
  // the graph was not disjoint from the rest of the heap as expected.
  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    UNREACHABLE();
  }
  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    UNREACHABLE();
  }
  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    UNREACHABLE();
  }

 private:
  enum ObjectKind {
    kNormalObject,
    kConstantPool,
    kScriptInfosList,
  };

  // If the object hasn't yet been added to the worklist, add it. Subsequent
  // calls with the same object have no effect, even if kind is different.
  void QueueVisit(Tagged<HeapObject> obj, ObjectKind kind) {
    if (visited_.insert(obj).second) {
      to_visit_.push(std::make_pair(obj, kind));
    }
  }

  DisallowGarbageCollection no_gc_;

  PtrComprCageBase cage_base_;
  std::stack<std::pair<Tagged<HeapObject>, ObjectKind>> to_visit_;

  // Objects that are either in to_visit_ or done being visited. It is safe to
  // use HeapObject directly here because GC is disallowed while running this
  // visitor.
  std::unordered_set<Tagged<HeapObject>, Object::Hasher> visited_;

  ObjectKind current_object_kind_ = kNormalObject;
};

#endif  // ENABLE_SLOW_DCHECKS

}  // namespace

bool BackgroundCompileTask::is_streaming_compilation() const {
  return function_literal_id_ == kFunctionLiteralIdTopLevel;
}

void BackgroundCompileTask::Run() {
  DCHECK_NE(ThreadId::Current(), isolate_for_local_isolate_->thread_id());
  LocalIsolate isolate(isolate_for_local_isolate_, ThreadKind::kBackground);
  UnparkedScope unparked_scope(&isolate);
  LocalHandleScope handle_scope(&isolate);

  ReusableUnoptimizedCompileState reusable_state(&isolate);

  Run(&isolate, &reusable_state);
}

void BackgroundCompileTask::RunOnMainThread(Isolate* isolate) {
  LocalHandleScope handle_scope(isolate->main_thread_local_isolate());
  ReusableUnoptimizedCompileState reusable_state(isolate);
  Run(isolate->main_thread_local_isolate(), &reusable_state);
}

void BackgroundCompileTask::Run(
    LocalIsolate* isolate, ReusableUnoptimizedCompileState* reusable_state) {
  TimedHistogramScope timer(
      timer_, nullptr,
      compilation_details_
          ? &compilation_details_->background_time_in_microseconds
          : nullptr);

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "BackgroundCompileTask::Run");
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileCompileTask,
            RuntimeCallStats::CounterMode::kThreadSpecific);

  bool toplevel_script_compilation = flags_.is_toplevel();

  ParseInfo info(isolate, flags_, &compile_state_, reusable_state,
                 GetCurrentStackPosition() - stack_size_ * KB);
  info.set_character_stream(std::move(character_stream_));
  info.SetCompileHintCallbackAndData(compile_hint_callback_,
                                     compile_hint_callback_data_);
  if (is_streaming_compilation()) info.set_is_streaming_compilation();

  if (toplevel_script_compilation) {
    DCHECK_NULL(persistent_handles_);
    DCHECK(input_shared_info_.is_null());

    // We don't have the script source, origin, or details yet, so use default
    // values for them. These will be fixed up during the main-thread merge.
    Handle<Script> script = info.CreateScript(
        isolate, isolate->factory()->empty_string(), kNullMaybeHandle,
        ScriptOriginOptions(false, false, false, info.flags().is_module()));
    script_ = isolate->heap()->NewPersistentHandle(script);
  } else {
    DCHECK_NOT_NULL(persistent_handles_);
    isolate->heap()->AttachPersistentHandles(std::move(persistent_handles_));
    DirectHandle<SharedFunctionInfo> shared_info =
        input_shared_info_.ToHandleChecked();
    script_ = isolate->heap()->NewPersistentHandle(
        Cast<Script>(shared_info->script()));
    info.CheckFlagsForFunctionFromScript(*script_);

    {
      SharedStringAccessGuardIfNeeded access_guard(isolate);
      info.set_function_name(info.ast_value_factory()->GetString(
          shared_info->Name(), access_guard));
    }

    // Get preparsed scope data from the function literal.
    if (shared_info->HasUncompiledDataWithPreparseData()) {
      info.set_consumed_preparse_data(ConsumedPreparseData::For(
          isolate,
          handle(shared_info->uncompiled_data_with_preparse_data(isolate)
                     ->preparse_data(isolate),
                 isolate)));
    }
  }

  // Update the character stream's runtime call stats.
  info.character_stream()->set_runtime_call_stats(info.runtime_call_stats());

  Parser parser(isolate, &info);
  if (flags().is_toplevel()) {
    parser.InitializeEmptyScopeChain(&info);
  } else {
    // TODO(leszeks): Consider keeping Scope zones alive between compile tasks
    // and passing the Scope for the FunctionLiteral through here directly
    // without copying/deserializing.
    DirectHandle<SharedFunctionInfo> shared_info =
        input_shared_info_.ToHandleChecked();
    MaybeHandle<ScopeInfo> maybe_outer_scope_info;
    if (shared_info->HasOuterScopeInfo()) {
      maybe_outer_scope_info =
          handle(shared_info->GetOuterScopeInfo(), isolate);
    }
    parser.DeserializeScopeChain(
        isolate, &info, maybe_outer_scope_info,
        Scope::DeserializationMode::kIncludingVariables);
  }

  parser.ParseOnBackground(isolate, &info, script_, start_position_,
                           end_position_, function_literal_id_);
  parser.UpdateStatistics(script_, &use_counts_, &total_preparse_skipped_);

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CompileCodeBackground");
  RCS_SCOPE(isolate, RuntimeCallCounterIdForCompile(&info),
            RuntimeCallStats::CounterMode::kThreadSpecific);

  MaybeHandle<SharedFunctionInfo> maybe_result;
  if (info.literal() != nullptr) {
    if (toplevel_script_compilation) {
      CreateTopLevelSharedFunctionInfo(&info, script_, isolate);
    } else {
      // Clone into a placeholder SFI for storing the results.
      info.literal()->set_shared_function_info(
          isolate->factory()->CloneSharedFunctionInfo(
              input_shared_info_.ToHandleChecked()));
    }

    if (IterativelyExecuteAndFinalizeUnoptimizedCompilationJobs(
            isolate, script_, &info, reusable_state->allocator(),
            &is_compiled_scope_, &finalize_unoptimized_compilation_data_,
            &jobs_to_retry_finalization_on_main_thread_)) {
      maybe_result = info.literal()->shared_function_info();
    }
  }

  if (maybe_result.is_null()) {
    PrepareException(isolate, &info);
  } else if (v8_flags.enable_slow_asserts) {
#ifdef ENABLE_SLOW_DCHECKS
    MergeAssumptionChecker checker(isolate);
    checker.IterateObjects(*maybe_result.ToHandleChecked());
#endif
  }

  outer_function_sfi_ = isolate->heap()->NewPersistentMaybeHandle(maybe_result);
  DCHECK(isolate->heap()->ContainsPersistentHandle(script_.location()));
  persistent_handles_ = isolate->heap()->DetachPersistentHandles();
}

// A class which traverses the constant pools of newly compiled
// SharedFunctionInfos and updates any pointers which need updating.
class ConstantPoolPointerForwarder {
 public:
  explicit ConstantPoolPointerForwarder(PtrComprCageBase cage_base,
                                        LocalHeap* local_heap,
                                        DirectHandle<Script> old_script)
      : cage_base_(cage_base),
        local_heap_(local_heap),
        old_script_(old_script) {}

  void AddBytecodeArray(Tagged<BytecodeArray> bytecode_array) {
    CHECK(IsBytecodeArray(bytecode_array));
    bytecode_arrays_to_update_.emplace_back(bytecode_array, local_heap_);
  }

  void RecordScopeInfos(Tagged<MaybeObject> maybe_old_info) {
    RecordScopeInfos(maybe_old_info.GetHeapObjectAssumeWeak());
  }

  // Record all scope infos relevant for a shared function info or scope info
  // (recorded for eval).
  void RecordScopeInfos(Tagged<HeapObject> info) {
    if (!v8_flags.reuse_scope_infos) return;
    Tagged<ScopeInfo> scope_info;
    if (Is<SharedFunctionInfo>(info)) {
      Tagged<SharedFunctionInfo> old_sfi = Cast<SharedFunctionInfo>(info);
      // Also record context-having own scope infos for SFIs.
      if (!old_sfi->scope_info()->IsEmpty() &&
          old_sfi->scope_info()->HasContext()) {
        scope_info = old_sfi->scope_info();
      } else if (old_sfi->HasOuterScopeInfo()) {
        scope_info = old_sfi->GetOuterScopeInfo();
      } else {
        return;
      }
    } else {
      scope_info = Cast<ScopeInfo>(info);
    }

    while (true) {
      auto it = scope_infos_to_update_.find(scope_info->UniqueIdInScript());
      if (it != scope_infos_to_update_.end()) {
        // Once we find an already recorded scope info, it need to match the one
        // on the chain.
        if (V8_UNLIKELY(*it->second != scope_info)) {
          info->Print();
          (*it->second)->Print();
          scope_info->Print();
          UNREACHABLE();
        }
        return;
      }
      scope_infos_to_update_[scope_info->UniqueIdInScript()] =
          handle(scope_info, local_heap_);
      if (!scope_info->HasOuterScopeInfo()) break;
      scope_info = scope_info->OuterScopeInfo();
    }
  }

  // Runs the update after the setup functions above specified the work to do.
  void IterateAndForwardPointers() {
    DCHECK(HasAnythingToForward());
    for (DirectHandle<BytecodeArray> entry : bytecode_arrays_to_update_) {
      local_heap_->Safepoint();
      DisallowGarbageCollection no_gc;
      IterateConstantPool(entry->constant_pool());
    }
  }

  void set_has_shared_function_info_to_forward() {
    has_shared_function_info_to_forward_ = true;
  }

  bool HasAnythingToForward() const {
    return has_shared_function_info_to_forward_ ||
           !scope_infos_to_update_.empty();
  }

  // Find an own scope info for the sfi based on the UniqueIdInScript that the
  // own scope info would have. This works even if the SFI doesn't yet have a
  // scope info attached by computing UniqueIdInScript from the SFI position.
  //
  // This should only directly be used for SFIs that already existed on the
  // script. Their outer scope info will already be correct.
  bool InstallOwnScopeInfo(Tagged<SharedFunctionInfo> sfi) {
    if (!v8_flags.reuse_scope_infos) return false;
    auto it = scope_infos_to_update_.find(sfi->UniqueIdInScript());
    if (it == scope_infos_to_update_.end()) return false;
    sfi->SetScopeInfo(*it->second);
    return true;
  }

  // Either replace the own scope info of the sfi, or the first outer scope info
  // that was recorded.
  //
  // This has to be used for all newly created SFIs since their outer scope info
  // also may need to be reattached.
  void UpdateScopeInfo(Tagged<SharedFunctionInfo> sfi) {
    if (!v8_flags.reuse_scope_infos) return;
    // This should not be called on already existing SFIs. Their scope infos are
    // already correct.
    DCHECK_NE(MakeWeak(sfi),
              old_script_->infos()->get(sfi->function_literal_id()));
    if (InstallOwnScopeInfo(sfi)) return;
    if (!sfi->HasOuterScopeInfo()) return;

    Tagged<ScopeInfo> parent =
        sfi->scope_info()->IsEmpty() ? Tagged<ScopeInfo>() : sfi->scope_info();
    Tagged<ScopeInfo> outer_info = sfi->GetOuterScopeInfo();

    auto it = scope_infos_to_update_.find(outer_info->UniqueIdInScript());
    while (it == scope_infos_to_update_.end()) {
      if (!outer_info->HasOuterScopeInfo()) return;
      parent = outer_info;
      outer_info = outer_info->OuterScopeInfo();
      it = scope_infos_to_update_.find(outer_info->UniqueIdInScript());
    }
    if (outer_info == *it->second) return;

    VerifyScopeInfo(outer_info, *it->second);

    if (parent.is_null()) {
      sfi->set_raw_outer_scope_info_or_feedback_metadata(*it->second);
    } else {
      parent->set_outer_scope_info(*it->second);
    }
  }

 private:
  void VerifyScopeInfo(Tagged<ScopeInfo> scope_info,
                       Tagged<ScopeInfo> replacement) {
    CHECK_EQ(replacement->EndPosition(), scope_info->EndPosition());
    CHECK_EQ(replacement->scope_type(), scope_info->scope_type());
    CHECK_EQ(replacement->ContextLength(), scope_info->ContextLength());
  }
  template <typename TArray>
  void IterateConstantPoolEntry(Tagged<TArray> constant_pool, int i) {
    Tagged<Object> obj = constant_pool->get(i);
    if (IsSmi(obj)) return;
    Tagged<HeapObject> heap_obj = Cast<HeapObject>(obj);
    if (IsFixedArray(heap_obj, cage_base_)) {
      // Constant pools can have nested fixed arrays, but such relationships
      // are acyclic and never more than a few layers deep, so recursion is
      // fine here.
      IterateConstantPoolNestedArray(Cast<FixedArray>(heap_obj));
    } else if (has_shared_function_info_to_forward_ &&
               IsSharedFunctionInfo(heap_obj, cage_base_)) {
      VisitSharedFunctionInfo(constant_pool, i,
                              Cast<SharedFunctionInfo>(heap_obj));
    } else if (!scope_infos_to_update_.empty() &&
               IsScopeInfo(heap_obj, cage_base_)) {
      VisitScopeInfo(constant_pool, i, Cast<ScopeInfo>(heap_obj));
    }
  }

  template <typename TArray>
  void VisitSharedFunctionInfo(Tagged<TArray> constant_pool, int i,
                               Tagged<SharedFunctionInfo> sfi) {
    Tagged<MaybeObject> maybe_old_sfi =
        old_script_->infos()->get(sfi->function_literal_id());
    if (maybe_old_sfi.IsWeak()) {
      constant_pool->set(
          i, Cast<SharedFunctionInfo>(maybe_old_sfi.GetHeapObjectAssumeWeak()));
    }
  }

  template <typename TArray>
  void VisitScopeInfo(Tagged<TArray> constant_pool, int i,
                      Tagged<ScopeInfo> scope_info) {
    if (scope_info->IsHiddenCatchScope()) return;
    auto it = scope_infos_to_update_.find(scope_info->UniqueIdInScript());
    // Try to replace the scope info itself with an already existing version.
    if (it != scope_infos_to_update_.end()) {
      if (scope_info != *it->second) {
        VerifyScopeInfo(scope_info, *it->second);
        constant_pool->set(i, *it->second);
      }
    } else if (scope_info->HasOuterScopeInfo()) {
      // If we didn't find a match, but we have an outer scope info, try to
      // replace the outer scope info with an already existing outer scope
      // info. We only need to look at the direct outer scope info since we'll
      // process all scope infos that are created by this compilation task.
      Tagged<ScopeInfo> outer = scope_info->OuterScopeInfo();
      it = scope_infos_to_update_.find(outer->UniqueIdInScript());
      if (it != scope_infos_to_update_.end() && outer != *it->second) {
        VerifyScopeInfo(outer, *it->second);
        scope_info->set_outer_scope_info(*it->second);
      }
    }
  }

  void IterateConstantPool(Tagged<TrustedFixedArray> constant_pool) {
    for (int i = 0, length = constant_pool->length(); i < length; ++i) {
      IterateConstantPoolEntry(constant_pool, i);
    }
  }

  void IterateConstantPoolNestedArray(Tagged<FixedArray> nested_array) {
    for (int i = 0, length = nested_array->length(); i < length; ++i) {
      IterateConstantPoolEntry(nested_array, i);
    }
  }

  PtrComprCageBase cage_base_;
  LocalHeap* local_heap_;
  DirectHandle<Script> old_script_;
  std::vector<Handle<BytecodeArray>> bytecode_arrays_to_update_;

  // Indicates whether we have any shared function info to forward.
  bool has_shared_function_info_to_forward_ = false;
  std::unordered_map<int, Handle<ScopeInfo>> scope_infos_to_update_;
};

void BackgroundMergeTask::SetUpOnMainThread(Isolate* isolate,
                                            Handle<String> source_text,
                                            const ScriptDetails& script_details,
                                            LanguageMode language_mode) {
  DCHECK_EQ(state_, kNotStarted);

  HandleScope handle_scope(isolate);

  CompilationCacheScript::LookupResult lookup_result =
      isolate->compilation_cache()->LookupScript(source_text, script_details,
                                                 language_mode);
  Handle<Script> script;
  if (!lookup_result.script().ToHandle(&script)) {
    state_ = kDone;
    return;
  }

  if (lookup_result.is_compiled_scope().is_compiled()) {
    // There already exists a compiled top-level SFI, so the main thread will
    // discard the background serialization results and use the top-level SFI
    // from the cache, assuming the top-level SFI is still compiled by then.
    // Thus, there is no need to keep the Script pointer for background merging.
    // Do nothing in this case.
    state_ = kDone;
  } else {
    DCHECK(lookup_result.toplevel_sfi().is_null());
    // A background merge is required.
    SetUpOnMainThread(isolate, script);
  }
}

void BackgroundMergeTask::SetUpOnMainThread(
    Isolate* isolate, DirectHandle<Script> cached_script) {
  // Any data sent to the background thread will need to be a persistent handle.
  persistent_handles_ = std::make_unique<PersistentHandles>(isolate);
  state_ = kPendingBackgroundWork;
  cached_script_ = persistent_handles_->NewHandle(*cached_script);
}

static bool force_gc_during_next_merge_for_testing_ = false;

void BackgroundMergeTask::ForceGCDuringNextMergeForTesting() {
  force_gc_during_next_merge_for_testing_ = true;
}

void BackgroundMergeTask::BeginMergeInBackground(
    LocalIsolate* isolate, DirectHandle<Script> new_script) {
  DCHECK_EQ(state_, kPendingBackgroundWork);

  LocalHeap* local_heap = isolate->heap();
  local_heap->AttachPersistentHandles(std::move(persistent_handles_));
  LocalHandleScope handle_scope(local_heap);
  DirectHandle<Script> old_script = cached_script_.ToHandleChecked();
  ConstantPoolPointerForwarder forwarder(isolate, local_heap, old_script);

  {
    DisallowGarbageCollection no_gc;
    Tagged<MaybeObject> maybe_old_toplevel_sfi =
        old_script->infos()->get(kFunctionLiteralIdTopLevel);
    if (maybe_old_toplevel_sfi.IsWeak()) {
      Tagged<SharedFunctionInfo> old_toplevel_sfi = Cast<SharedFunctionInfo>(
          maybe_old_toplevel_sfi.GetHeapObjectAssumeWeak());
      toplevel_sfi_from_cached_script_ =
          local_heap->NewPersistentHandle(old_toplevel_sfi);
    }
  }

  // Iterate the SFI lists on both Scripts to set up the forwarding table and
  // follow-up worklists for the main thread.
  CHECK_EQ(old_script->infos()->length(), new_script->infos()->length());
  for (int i = 0; i < old_script->infos()->length(); ++i) {
    DisallowGarbageCollection no_gc;
    Tagged<MaybeObject> maybe_new_sfi = new_script->infos()->get(i);
    Tagged<MaybeObject> maybe_old_info = old_script->infos()->get(i);
    // We might have scope infos in the table if it's deserialized from a code
    // cache.
    if (maybe_new_sfi.IsWeak() &&
        Is<SharedFunctionInfo>(maybe_new_sfi.GetHeapObjectAssumeWeak())) {
      Tagged<SharedFunctionInfo> new_sfi =
          Cast<SharedFunctionInfo>(maybe_new_sfi.GetHeapObjectAssumeWeak());
      if (maybe_old_info.IsWeak()) {
        forwarder.set_has_shared_function_info_to_forward();
        // The old script and the new script both have SharedFunctionInfos for
        // this function literal.
        Tagged<SharedFunctionInfo> old_sfi =
            Cast<SharedFunctionInfo>(maybe_old_info.GetHeapObjectAssumeWeak());
        // Make sure to allocate a persistent handle to the old sfi whether or
        // not it or the new sfi have bytecode -- this is necessary to keep the
        // old sfi reference in the old script list alive, so that pointers to
        // the new sfi are redirected to the old sfi.
        Handle<SharedFunctionInfo> old_sfi_handle =
            local_heap->NewPersistentHandle(old_sfi);
        if (old_sfi->HasBytecodeArray()) {
          // Reset the old SFI's bytecode age so that it won't likely get
          // flushed right away. This operation might be racing against
          // concurrent modification by another thread, but such a race is not
          // catastrophic.
          old_sfi->set_age(0);
        } else if (new_sfi->HasBytecodeArray()) {
          // Also push the old_sfi to make sure it stays alive / isn't replaced.
          new_compiled_data_for_cached_sfis_.push_back(
              {old_sfi_handle, local_heap->NewPersistentHandle(new_sfi)});
          // Pick up existing scope infos from the old sfi. The new sfi will be
          // copied over the old sfi later. This will ensure that we'll keep
          // using the old sfis. This will also allow us check later whether new
          // scope infos have appeared that need to be reused.
          if (!old_sfi->scope_info()->IsEmpty()) {
            new_sfi->SetScopeInfo(old_sfi->scope_info());
          } else if (old_sfi->HasOuterScopeInfo()) {
            new_sfi->scope_info()->set_outer_scope_info(
                old_sfi->GetOuterScopeInfo());
          }
          forwarder.AddBytecodeArray(new_sfi->GetBytecodeArray(isolate));
        }
        // TODO(355575275): We shouldn't be using the new sfi, so its script
        // field shouldn't matter -- but there seems to be some cases where we
        // do, so stay robust and set it. Remove this once this bug is fixed.
        new_sfi->set_script(*old_script, kReleaseStore);
      } else {
        // The old script didn't have a SharedFunctionInfo for this function
        // literal, so it can use the new SharedFunctionInfo.
        new_sfi->set_script(*old_script, kReleaseStore);
        used_new_sfis_.push_back(local_heap->NewPersistentHandle(new_sfi));
        if (new_sfi->HasBytecodeArray()) {
          forwarder.AddBytecodeArray(new_sfi->GetBytecodeArray(isolate));
        }
      }
    }

    if (maybe_old_info.IsWeak()) {
      forwarder.RecordScopeInfos(maybe_old_info);
      // If the old script has a SFI, point to it from the new script to
      // indicate we've already seen it and we'll reuse it if necessary (if
      // newly compiled bytecode points to it).
      new_script->infos()->set(i, maybe_old_info);
    }
  }

  // Since we are walking the script infos weak list both when figuring out
  // which SFIs to merge above, and actually merging them below, make sure that
  // a GC here which clears any dead weak refs or flushes any bytecode doesn't
  // break anything.
  if (V8_UNLIKELY(force_gc_during_next_merge_for_testing_)) {
    // This GC is only synchronous on the main thread at the moment.
    DCHECK(isolate->is_main_thread());
    local_heap->AsHeap()->CollectAllAvailableGarbage(
        GarbageCollectionReason::kTesting);
  }

  if (forwarder.HasAnythingToForward()) {
    for (DirectHandle<SharedFunctionInfo> new_sfi : used_new_sfis_) {
      forwarder.UpdateScopeInfo(*new_sfi);
    }
    for (const auto& new_compiled_data : new_compiled_data_for_cached_sfis_) {
      // It's possible that new_compiled_data.cached_sfi had
      // scope_info()->IsEmpty() while an inner function has scope info if the
      // cached_sfi was recreated when an outer function was recompiled. If so,
      // new_compiled_data.new_sfi does not have a reused scope info yet, and
      // we'll have found it when we visited the inner function. Try to pick it
      // up here.
      forwarder.InstallOwnScopeInfo(*new_compiled_data.new_sfi);
    }
    forwarder.IterateAndForwardPointers();
  }
  persistent_handles_ = local_heap->DetachPersistentHandles();
  state_ = kPendingForegroundWork;
}

Handle<SharedFunctionInfo> BackgroundMergeTask::CompleteMergeInForeground(
    Isolate* isolate, DirectHandle<Script> new_script) {
  DCHECK_EQ(state_, kPendingForegroundWork);

  HandleScope handle_scope(isolate);
  DirectHandle<Script> old_script = cached_script_.ToHandleChecked();
  ConstantPoolPointerForwarder forwarder(
      isolate, isolate->main_thread_local_heap(), old_script);

  for (const auto& new_compiled_data : new_compiled_data_for_cached_sfis_) {
    Tagged<SharedFunctionInfo> sfi = *new_compiled_data.cached_sfi;
    if (!sfi->is_compiled() && new_compiled_data.new_sfi->is_compiled()) {
      // Updating existing DebugInfos is not supported, but we don't expect
      // uncompiled SharedFunctionInfos to contain DebugInfos.
      DCHECK(!new_compiled_data.cached_sfi->HasDebugInfo(isolate));
      // The goal here is to copy every field except script from
      // new_sfi to cached_sfi. The safest way to do so (including a DCHECK that
      // no fields were skipped) is to first copy the script from
      // cached_sfi to new_sfi, and then copy every field using CopyFrom.
      new_compiled_data.new_sfi->set_script(
          new_compiled_data.cached_sfi->script(kAcquireLoad), kReleaseStore);
      sfi->CopyFrom(*new_compiled_data.new_sfi, isolate);
    }
  }

  for (int i = 0; i < old_script->infos()->length(); ++i) {
    Tagged<MaybeObject> maybe_old_info = old_script->infos()->get(i);
    Tagged<MaybeObject> maybe_new_info = new_script->infos()->get(i);
    if (maybe_new_info == maybe_old_info) continue;
    DisallowGarbageCollection no_gc;
    if (maybe_old_info.IsWeak()) {
      // The old script's SFI didn't exist during the background work, but does
      // now. This means a re-merge is necessary. Potential references to the
      // new script's SFI need to be updated to point to the cached script's SFI
      // instead. The cached script's SFI's outer scope infos need to be used by
      // the new script's outer SFIs.
      if (Is<SharedFunctionInfo>(maybe_old_info.GetHeapObjectAssumeWeak())) {
        forwarder.set_has_shared_function_info_to_forward();
      }
      forwarder.RecordScopeInfos(maybe_old_info);
    } else {
      old_script->infos()->set(i, maybe_new_info);
    }
  }

  // Most of the time, the background merge was sufficient. However, if there
  // are any new pointers that need forwarding, a new traversal of the constant
  // pools is required.
  if (forwarder.HasAnythingToForward()) {
    for (DirectHandle<SharedFunctionInfo> new_sfi : used_new_sfis_) {
      forwarder.UpdateScopeInfo(*new_sfi);
      if (new_sfi->HasBytecodeArray(isolate)) {
        forwarder.AddBytecodeArray(new_sfi->GetBytecodeArray(isolate));
      }
    }
    for (const auto& new_compiled_data : new_compiled_data_for_cached_sfis_) {
      // It's possible that cached_sfi wasn't compiled, but an inner function
      // existed that didn't exist when be background merged. In that case, pick
      // up the relevant scope infos.
      Tagged<SharedFunctionInfo> sfi = *new_compiled_data.cached_sfi;
      forwarder.InstallOwnScopeInfo(sfi);
      if (new_compiled_data.cached_sfi->HasBytecodeArray(isolate)) {
        forwarder.AddBytecodeArray(
            new_compiled_data.cached_sfi->GetBytecodeArray(isolate));
      }
    }
    forwarder.IterateAndForwardPointers();
  }

  Tagged<MaybeObject> maybe_toplevel_sfi =
      old_script->infos()->get(kFunctionLiteralIdTopLevel);
  CHECK(maybe_toplevel_sfi.IsWeak());
  Handle<SharedFunctionInfo> result = handle(
      Cast<SharedFunctionInfo>(maybe_toplevel_sfi.GetHeapObjectAssumeWeak()),
      isolate);

  state_ = kDone;

  if (isolate->NeedsSourcePositions()) {
    Script::InitLineEnds(isolate, new_script);
    SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate, result);
  }

  {
    // TODO(355575275): Extra validation code to try to find a bug. Remove after
    // fixing.
    for (int i = 0; i < old_script->infos()->length(); ++i) {
      Tagged<MaybeObject> maybe_sfi = old_script->infos()->get(i);
      if (maybe_sfi.IsWeak() &&
          Is<SharedFunctionInfo>(maybe_sfi.GetHeapObjectAssumeWeak())) {
        Tagged<SharedFunctionInfo> sfi =
            Cast<SharedFunctionInfo>(maybe_sfi.GetHeapObjectAssumeWeak());

        // Check that the SFI has the right script.
        if (sfi->script() != *old_script) {
          isolate->PushStackTraceAndContinue(
              reinterpret_cast<void*>(sfi.ptr()),
              reinterpret_cast<void*>(old_script->ptr()),
              reinterpret_cast<void*>(new_script->ptr()),
              reinterpret_cast<void*>(old_script->infos()->ptr() +
                                      WeakFixedArray::OffsetOfElementAt(i)),
              reinterpret_cast<void*>(new_script->infos()->ptr() +
                                      WeakFixedArray::OffsetOfElementAt(i)));
        }

        // Check that all SFIs in the bytecode array's constant pool are from
        // the same script.
        if (sfi->HasBytecodeArray()) {
          Tagged<BytecodeArray> bytecode = sfi->GetBytecodeArray(isolate);
          Tagged<TrustedFixedArray> constant_pool = bytecode->constant_pool();
          for (int i = 0; i < constant_pool->length(); ++i) {
            Tagged<Object> entry = constant_pool->get(i);
            if (Is<SharedFunctionInfo>(entry)) {
              Tagged<SharedFunctionInfo> inner_sfi =
                  Cast<SharedFunctionInfo>(entry);
              int id = inner_sfi->function_literal_id();
              if (MakeWeak(inner_sfi) != old_script->infos()->get(id)) {
                isolate->PushStackTraceAndContinue(
                    reinterpret_cast<void*>(sfi.ptr()),
                    reinterpret_cast<void*>(inner_sfi.ptr()),
                    reinterpret_cast<void*>(old_script->ptr()),
                    reinterpret_cast<void*>(new_script->ptr()),
                    reinterpret_cast<void*>(
                        old_script->infos()->ptr() +
                        WeakFixedArray::OffsetOfElementAt(id)),
                    reinterpret_cast<void*>(
                        new_script->infos()->ptr() +
                        WeakFixedArray::OffsetOfElementAt(id)));
              }

              if (inner_sfi->script() != *old_script) {
                isolate->PushStackTraceAndContinue(
                    reinterpret_cast<void*>(sfi.ptr()),
                    reinterpret_cast<void*>(inner_sfi.ptr()),
                    reinterpret_cast<void*>(old_script->ptr()),
                    reinterpret_cast<void*>(new_script->ptr()),
                    reinterpret_cast<void*>(
                        old_script->infos()->ptr() +
                        WeakFixedArray::OffsetOfElementAt(id)),
                    reinterpret_cast<void*>(
                        new_script->infos()->ptr() +
                        WeakFixedArray::OffsetOfElementAt(id)));
              }
            }
          }
        }
      }
    }
  }

  if (v8_flags.verify_code_merge) {
    // Check that there aren't any duplicate scope infos. Every scope/context
    // should correspond to at most one scope info.
    std::unordered_map<int, Tagged<ScopeInfo>> scope_infos;
    for (int i = 0; i < old_script->infos()->length(); i++) {
      Tagged<ScopeInfo> scope_info;
      if (!old_script->infos()->get(i).IsWeak()) continue;
      Tagged<HeapObject> info =
          old_script->infos()->get(i).GetHeapObjectAssumeWeak();
      if (Is<SharedFunctionInfo>(info)) {
        Tagged<SharedFunctionInfo> old_sfi = Cast<SharedFunctionInfo>(info);
        CHECK_EQ(old_sfi->script(), *old_script);
        if (!old_sfi->scope_info()->IsEmpty()) {
          scope_info = old_sfi->scope_info();
        } else if (old_sfi->HasOuterScopeInfo()) {
          scope_info = old_sfi->GetOuterScopeInfo();
        } else {
          continue;
        }
      } else {
        scope_info = Cast<ScopeInfo>(info);
      }
      while (true) {
        auto it = scope_infos.find(scope_info->UniqueIdInScript());
        if (it != scope_infos.end()) {
          if (*it->second != scope_info) {
            old_script->infos()->get(i).GetHeapObjectAssumeWeak()->Print();
            (*it->second)->Print();
            scope_info->Print();
            UNREACHABLE();
          }
          break;
        }
        scope_infos[scope_info->UniqueIdInScript()] = scope_info;
        if (!scope_info->HasOuterScopeInfo()) break;
        scope_info = scope_info->OuterScopeInfo();
      }
    }
  }

  return handle_scope.CloseAndEscape(result);
}

MaybeHandle<SharedFunctionInfo> BackgroundCompileTask::FinalizeScript(
    Isolate* isolate, DirectHandle<String> source,
    const ScriptDetails& script_details,
    MaybeHandle<Script> maybe_cached_script) {
  ScriptOriginOptions origin_options = script_details.origin_options;

  DCHECK(flags_.is_toplevel());
  DCHECK_EQ(flags_.is_module(), origin_options.IsModule());

  MaybeHandle<SharedFunctionInfo> maybe_result;
  Handle<Script> script = script_;

  // We might not have been able to finalize all jobs on the background
  // thread (e.g. asm.js jobs), so finalize those deferred jobs now.
  if (FinalizeDeferredUnoptimizedCompilationJobs(
          isolate, script, &jobs_to_retry_finalization_on_main_thread_,
          compile_state_.pending_error_handler(),
          &finalize_unoptimized_compilation_data_)) {
    maybe_result = outer_function_sfi_;
  }

  if (Handle<Script> cached_script;
      maybe_cached_script.ToHandle(&cached_script) && !maybe_result.is_null()) {
    BackgroundMergeTask merge;
    merge.SetUpOnMainThread(isolate, cached_script);
    CHECK(merge.HasPendingBackgroundWork());
    merge.BeginMergeInBackground(isolate->AsLocalIsolate(), script);
    CHECK(merge.HasPendingForegroundWork());
    Handle<SharedFunctionInfo> result =
        merge.CompleteMergeInForeground(isolate, script);
    maybe_result = result;

    {
      // TODO(355575275): We shouldn't be using the new script, so its source
      // and origin options shouldn't matter -- but there seems to be some cases
      // where we do, so stay robust and set them. Remove this once this bug is
      // fixed.
      Script::SetSource(isolate, script, source);
      script->set_origin_options(origin_options);
    }

    script = handle(Cast<Script>(result->script()), isolate);
    DCHECK(Object::StrictEquals(script->source(), *source));
    DCHECK(isolate->factory()->script_list()->Contains(MakeWeak(*script)));
  } else {
    Script::SetSource(isolate, script, source);
    script->set_origin_options(origin_options);

    // The one post-hoc fix-up: Add the script to the script list.
    Handle<WeakArrayList> scripts = isolate->factory()->script_list();
    scripts = WeakArrayList::Append(isolate, scripts,
                                    MaybeObjectHandle::Weak(script));
    isolate->heap()->SetRootScriptList(*scripts);

    // Set the script fields after finalization, to keep this path the same
    // between main-thread and off-thread finalization.
    {
      DisallowGarbageCollection no_gc;
      SetScriptFieldsFromDetails(isolate, *script, script_details, &no_gc);
      LOG(isolate, ScriptDetails(*script));
    }
  }

  ReportStatistics(isolate);

  Handle<SharedFunctionInfo> result;
  if (!maybe_result.ToHandle(&result)) {
    FailWithPreparedException(isolate, script,
                              compile_state_.pending_error_handler());
    return kNullMaybeHandle;
  }

  FinalizeUnoptimizedScriptCompilation(isolate, script, flags_, &compile_state_,
                                       finalize_unoptimized_compilation_data_);

  return handle(*result, isolate);
}

bool BackgroundCompileTask::FinalizeFunction(
    Isolate* isolate, Compiler::ClearExceptionFlag flag) {
  DCHECK(!flags_.is_toplevel());

  MaybeHandle<SharedFunctionInfo> maybe_result;
  DirectHandle<SharedFunctionInfo> input_shared_info =
      input_shared_info_.ToHandleChecked();

  // The UncompiledData on the input SharedFunctionInfo will have a pointer to
  // the LazyCompileDispatcher Job that launched this task, which will now be
  // considered complete, so clear that regardless of whether the finalize
  // succeeds or not.
  input_shared_info->ClearUncompiledDataJobPointer(isolate);

  // We might not have been able to finalize all jobs on the background
  // thread (e.g. asm.js jobs), so finalize those deferred jobs now.
  if (FinalizeDeferredUnoptimizedCompilationJobs(
          isolate, script_, &jobs_to_retry_finalization_on_main_thread_,
          compile_state_.pending_error_handler(),
          &finalize_unoptimized_compilation_data_)) {
    maybe_result = outer_function_sfi_;
  }

  ReportStatistics(isolate);

  Handle<SharedFunctionInfo> result;
  if (!maybe_result.ToHandle(&result)) {
    FailWithPreparedException(isolate, script_,
                              compile_state_.pending_error_handler(), flag);
    return false;
  }

  FinalizeUnoptimizedCompilation(isolate, script_, flags_, &compile_state_,
                                 finalize_unoptimized_compilation_data_);

  // Move the compiled data from the placeholder SFI back to the real SFI.
  input_shared_info->CopyFrom(*result, isolate);

  return true;
}

void BackgroundCompileTask::AbortFunction() {
  // The UncompiledData on the input SharedFunctionInfo will have a pointer to
  // the LazyCompileDispatcher Job that launched this task, which is about to be
  // deleted, so clear that to avoid the SharedFunctionInfo from pointing to
  // deallocated memory.
  input_shared_info_.ToHandleChecked()->ClearUncompiledDataJobPointer(
      isolate_for_local_isolate_);
}

void BackgroundCompileTask::ReportStatistics(Isolate* isolate) {
  // Update use-counts.
  for (auto feature : use_counts_) {
    isolate->CountUsage(feature);
  }
}

BackgroundDeserializeTask::BackgroundDeserializeTask(
    Isolate* isolate, std::unique_ptr<ScriptCompiler::CachedData> cached_data)
    : isolate_for_local_isolate_(isolate),
      cached_data_(cached_data->data, cached_data->length),
      timer_(isolate->counters()->deserialize_script_on_background()) {
  // If the passed in cached data has ownership of the buffer, move it to the
  // task.
  if (cached_data->buffer_policy == ScriptCompiler::CachedData::BufferOwned &&
      !cached_data_.HasDataOwnership()) {
    cached_data->buffer_policy = ScriptCompiler::CachedData::BufferNotOwned;
    cached_data_.AcquireDataOwnership();
  }
}

void BackgroundDeserializeTask::Run() {
  TimedHistogramScope timer(timer_, nullptr, &background_time_in_microseconds_);
  LocalIsolate isolate(isolate_for_local_isolate_, ThreadKind::kBackground);
  UnparkedScope unparked_scope(&isolate);
  LocalHandleScope handle_scope(&isolate);

  DirectHandle<SharedFunctionInfo> inner_result;
  off_thread_data_ =
      CodeSerializer::StartDeserializeOffThread(&isolate, &cached_data_);
  if (v8_flags.enable_slow_asserts && off_thread_data_.HasResult()) {
#ifdef ENABLE_SLOW_DCHECKS
    MergeAssumptionChecker checker(&isolate);
    checker.IterateObjects(*off_thread_data_.GetOnlyScript(isolate.heap()));
#endif
  }
}

void BackgroundDeserializeTask::SourceTextAvailable(
    Isolate* isolate, Handle<String> source_text,
    const ScriptDetails& script_details) {
  DCHECK_EQ(isolate, isolate_for_local_isolate_);
  LanguageMode language_mode = construct_language_mode(v8_flags.use_strict);
  background_merge_task_.SetUpOnMainThread(isolate, source_text, script_details,
                                           language_mode);
}

bool BackgroundDeserializeTask::ShouldMergeWithExistingScript() const {
  DCHECK(v8_flags.merge_background_deserialized_script_with_compilation_cache);
  return background_merge_task_.HasPendingBackgroundWork() &&
         off_thread_data_.HasResult();
}

void BackgroundDeserializeTask::MergeWithExistingScript() {
  DCHECK(ShouldMergeWithExistingScript());

  LocalIsolate isolate(isolate_for_local_isolate_, ThreadKind::kBackground);
  UnparkedScope unparked_scope(&isolate);
  LocalHandleScope handle_scope(isolate.heap());

  background_merge_task_.BeginMergeInBackground(
      &isolate, off_thread_data_.GetOnlyScript(isolate.heap()));
}

MaybeHandle<SharedFunctionInfo> BackgroundDeserializeTask::Finish(
    Isolate* isolate, DirectHandle<String> source,
    const ScriptDetails& script_details) {
  return CodeSerializer::FinishOffThreadDeserialize(
      isolate, std::move(off_thread_data_), &cached_data_, source,
      script_details, &background_merge_task_);
}

// ----------------------------------------------------------------------------
// Implementation of Compiler

// static
bool Compiler::CollectSourcePositions(Isolate* isolate,
                                      Handle<SharedFunctionInfo> shared_info) {
  DCHECK(shared_info->is_compiled());
  DCHECK(shared_info->HasBytecodeArray());
  DCHECK(!shared_info->GetBytecodeArray(isolate)->HasSourcePositionTable());

  // Source position collection should be context independent.
  NullContextScope null_context_scope(isolate);

  // Collecting source positions requires allocating a new source position
  // table.
  DCHECK(AllowHeapAllocation::IsAllowed());

  Handle<BytecodeArray> bytecode =
      handle(shared_info->GetBytecodeArray(isolate), isolate);

  // TODO(v8:8510): Push the CLEAR_EXCEPTION flag or something like it down into
  // the parser so it aborts without setting a exception, which then
  // gets thrown. This would avoid the situation where potentially we'd reparse
  // several times (running out of stack each time) before hitting this limit.
  if (GetCurrentStackPosition() < isolate->stack_guard()->real_climit()) {
    // Stack is already exhausted.
    bytecode->SetSourcePositionsFailedToCollect();
    return false;
  }

  // Unfinalized scripts don't yet have the proper source string attached and
  // thus can't be reparsed.
  if (Cast<Script>(shared_info->script())->IsMaybeUnfinalized(isolate)) {
    bytecode->SetSourcePositionsFailedToCollect();
    return false;
  }

  DCHECK(AllowCompilation::IsAllowed(isolate));
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  DCHECK(!isolate->has_exception());
  VMState<BYTECODE_COMPILER> state(isolate);
  PostponeInterruptsScope postpone(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileCollectSourcePositions);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.CollectSourcePositions");
  NestedTimedHistogramScope timer(
      isolate->counters()->collect_source_positions());

  // Set up parse info.
  UnoptimizedCompileFlags flags =
      UnoptimizedCompileFlags::ForFunctionCompile(isolate, *shared_info);
  flags.set_collect_source_positions(true);
  flags.set_is_reparse(true);
  // Prevent parallel tasks from being spawned by this job.
  flags.set_post_parallel_compile_tasks_for_eager_toplevel(false);
  flags.set_post_parallel_compile_tasks_for_lazy(false);

  UnoptimizedCompileState compile_state;
  ReusableUnoptimizedCompileState reusable_state(isolate);
  ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);

  // Parse and update ParseInfo with the results. Don't update parsing
  // statistics since we've already parsed the code before.
  if (!parsing::ParseAny(&parse_info, shared_info, isolate,
                         parsing::ReportStatisticsMode::kNo)) {
    // Parsing failed probably as a result of stack exhaustion.
    bytecode->SetSourcePositionsFailedToCollect();
    return FailAndClearException(isolate);
  }

  // Character stream shouldn't be used again.
  parse_info.ResetCharacterStream();

  // Generate the unoptimized bytecode.
  // TODO(v8:8510): Consider forcing preparsing of inner functions to avoid
  // wasting time fully parsing them when they won't ever be used.
  std::unique_ptr<UnoptimizedCompilationJob> job;
  {
    job = interpreter::Interpreter::NewSourcePositionCollectionJob(
        &parse_info, parse_info.literal(), bytecode, isolate->allocator(),
        isolate->main_thread_local_isolate());

    if (!job || job->ExecuteJob() != CompilationJob::SUCCEEDED ||
        job->FinalizeJob(shared_info, isolate) != CompilationJob::SUCCEEDED) {
      // Recompiling failed probably as a result of stack exhaustion.
      bytecode->SetSourcePositionsFailedToCollect();
      return FailAndClearException(isolate);
    }
  }

  DCHECK(job->compilation_info()->flags().collect_source_positions());

  // If debugging, make sure that instrumented bytecode has the source position
  // table set on it as well.
  if (std::optional<Tagged<DebugInfo>> debug_info =
          shared_info->TryGetDebugInfo(isolate)) {
    if (debug_info.value()->HasInstrumentedBytecodeArray()) {
      Tagged<TrustedByteArray> source_position_table =
          job->compilation_info()->bytecode_array()->SourcePositionTable();
      shared_info->GetActiveBytecodeArray(isolate)->set_source_position_table(
          source_position_table, kReleaseStore);
    }
  }

  DCHECK(!isolate->has_exception());
  DCHECK(shared_info->is_compiled_scope(isolate).is_compiled());
  return true;
}

// static
bool Compiler::Compile(Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
                       ClearExceptionFlag flag,
                       IsCompiledScope* is_compiled_scope,
                       CreateSourcePositions create_source_positions_flag) {
  // We should never reach here if the function is already compiled.
  DCHECK(!shared_info->is_compiled());
  DCHECK(!is_compiled_scope->is_compiled());
  DCHECK(AllowCompilation::IsAllowed(isolate));
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
  DCHECK(!isolate->has_exception());
  DCHECK(!shared_info->HasBytecodeArray());

  VMState<BYTECODE_COMPILER> state(isolate);
  PostponeInterruptsScope postpone(isolate);
  TimerEventScope<TimerEventCompileCode> compile_timer(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileFunction);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.CompileCode");
  AggregatedHistogramTimerScope timer(isolate->counters()->compile_lazy());

  Handle<Script> script(Cast<Script>(shared_info->script()), isolate);

  // Set up parse info.
  UnoptimizedCompileFlags flags =
      UnoptimizedCompileFlags::ForFunctionCompile(isolate, *shared_info);
  if (create_source_positions_flag == CreateSourcePositions::kYes) {
    flags.set_collect_source_positions(true);
  }

  UnoptimizedCompileState compile_state;
  ReusableUnoptimizedCompileState reusable_state(isolate);
  ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);

  // Check if the compiler dispatcher has shared_info enqueued for compile.
  LazyCompileDispatcher* dispatcher = isolate->lazy_compile_dispatcher();
  if (dispatcher && dispatcher->IsEnqueued(shared_info)) {
    if (!dispatcher->FinishNow(shared_info)) {
      return FailWithException(isolate, script, &parse_info, flag);
    }
    *is_compiled_scope = shared_info->is_compiled_scope(isolate);
    DCHECK(is_compiled_scope->is_compiled());
    return true;
  }

  if (shared_info->HasUncompiledDataWithPreparseData()) {
    parse_info.set_consumed_preparse_data(ConsumedPreparseData::For(
        isolate, handle(shared_info->uncompiled_data_with_preparse_data(isolate)
                            ->preparse_data(),
                        isolate)));
  }

  // Parse and update ParseInfo with the results.
  if (!parsing::ParseAny(&parse_info, shared_info, isolate,
                         parsing::ReportStatisticsMode::kYes)) {
    return FailWithException(isolate, script, &parse_info, flag);
  }
  parse_info.literal()->set_shared_function_info(shared_info);

  // Generate the unoptimized bytecode or asm-js data.
  FinalizeUnoptimizedCompilationDataList
      finalize_unoptimized_compilation_data_list;

  if (!IterativelyExecuteAndFinalizeUnoptimizedCompilationJobs(
          isolate, script, &parse_info, isolate->allocator(), is_compiled_scope,
          &finalize_unoptimized_compilation_data_list, nullptr)) {
    return FailWithException(isolate, script, &parse_info, flag);
  }

  FinalizeUnoptimizedCompilation(isolate, script, flags, &compile_state,
                                 finalize_unoptimized_compilation_data_list);

  if (v8_flags.always_sparkplug) {
    CompileAllWithBaseline(isolate, finalize_unoptimized_compilation_data_list);
  }

  if (script->produce_compile_hints()) {
    // Log lazy funtion compilation.
    Handle<ArrayList> list;
    if (IsUndefined(script->compiled_lazy_function_positions())) {
      constexpr int kInitialLazyFunctionPositionListSize = 100;
      list = ArrayList::New(isolate, kInitialLazyFunctionPositionListSize);
    } else {
      list = handle(Cast<ArrayList>(script->compiled_lazy_function_positions()),
                    isolate);
    }
    list = ArrayList::Add(isolate, list,
                          Smi::FromInt(shared_info->StartPosition()));
    script->set_compiled_lazy_function_positions(*list);
  }

  DCHECK(!isolate->has_exception());
  DCHECK(is_compiled_scope->is_compiled());
  return true;
}

// static
bool Compiler::Compile(Isolate* isolate, Handle<JSFunction> function,
                       ClearExceptionFlag flag,
                       IsCompiledScope* is_compiled_scope) {
  // We should never reach here if the function is already compiled or
  // optimized.
  DCHECK(!function->is_compiled(isolate));
  DCHECK_IMPLIES(!IsNone(function->tiering_state()),
                 function->shared()->is_compiled());
  DCHECK_IMPLIES(function->HasAvailableOptimizedCode(isolate),
                 function->shared()->is_compiled());

  // Reset the JSFunction if we are recompiling due to the bytecode having been
  // flushed.
  function->ResetIfCodeFlushed(isolate);

  Handle<SharedFunctionInfo> shared_info(function->shared(), isolate);

  // Ensure shared function info is compiled.
  *is_compiled_scope = shared_info->is_compiled_scope(isolate);
  if (!is_compiled_scope->is_compiled() &&
      !Compile(isolate, shared_info, flag, is_compiled_scope)) {
    return false;
  }

  DCHECK(is_compiled_scope->is_compiled());
  DirectHandle<Code> code(shared_info->GetCode(isolate), isolate);

  // Initialize the feedback cell for this JSFunction and reset the interrupt
  // budget for feedback vector allocation even if there is a closure feedback
  // cell array. We are re-compiling when we have a closure feedback cell array
  // which means we are compiling after a bytecode flush.
  // TODO(verwaest/mythria): Investigate if allocating feedback vector
  // immediately after a flush would be better.
  JSFunction::InitializeFeedbackCell(function, is_compiled_scope, true);

  // Optimize now if --always-turbofan is enabled.
#if V8_ENABLE_WEBASSEMBLY
  if (v8_flags.always_turbofan && !function->shared()->HasAsmWasmData()) {
#else
  if (v8_flags.always_turbofan) {
#endif  // V8_ENABLE_WEBASSEMBLY
    CompilerTracer::TraceOptimizeForAlwaysOpt(isolate, function,
                                              CodeKindForTopTier());

    const CodeKind code_kind = CodeKindForTopTier();
    const ConcurrencyMode concurrency_mode = ConcurrencyMode::kSynchronous;

    if (v8_flags.stress_concurrent_inlining &&
        isolate->concurrent_recompilation_enabled() &&
        isolate->node_observer() == nullptr) {
      SpawnDuplicateConcurrentJobForStressTesting(isolate, function,
                                                  concurrency_mode, code_kind);
    }

    Handle<Code> maybe_code;
    if (GetOrCompileOptimized(isolate, function, concurrency_mode, code_kind)
            .ToHandle(&maybe_code)) {
      code = maybe_code;
    }
  }

  // Install code on closure.
  function->set_code(*code);
  // Install a feedback vector if necessary.
  if (code->kind() == CodeKind::BASELINE) {
    JSFunction::EnsureFeedbackVector(isolate, function, is_compiled_scope);
  }

  // Check postconditions on success.
  DCHECK(!isolate->has_exception());
  DCHECK(function->shared()->is_compiled());
  DCHECK(function->is_compiled(isolate));
  return true;
}

// static
bool Compiler::CompileSharedWithBaseline(Isolate* isolate,
                                         Handle<SharedFunctionInfo> shared,
                                         Compiler::ClearExceptionFlag flag,
                                         IsCompiledScope* is_compiled_scope) {
  // We shouldn't be passing uncompiled functions into this function.
  DCHECK(is_compiled_scope->is_compiled());

  // Early return for already baseline-compiled functions.
  if (shared->HasBaselineCode()) return true;

  // Check if we actually can compile with baseline.
  if (!CanCompileWithBaseline(isolate, *shared)) return false;

  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed(kStackSpaceRequiredForCompilation * KB)) {
    if (flag == Compiler::KEEP_EXCEPTION) {
      isolate->StackOverflow();
    }
    return false;
  }

  CompilerTracer::TraceStartBaselineCompile(isolate, shared);
  Handle<Code> code;
  base::TimeDelta time_taken;
  {
    base::ScopedTimer timer(
        v8_flags.trace_baseline || v8_flags.log_function_events ? &time_taken
                                                                : nullptr);
    if (!GenerateBaselineCode(isolate, shared).ToHandle(&code)) {
      // TODO(leszeks): This can only fail because of an OOM. Do we want to
      // report these somehow, or silently ignore them?
      return false;
    }
    shared->set_baseline_code(*code, kReleaseStore);
    shared->set_age(0);
  }
  double time_taken_ms = time_taken.InMillisecondsF();

  CompilerTracer::TraceFinishBaselineCompile(isolate, shared, time_taken_ms);

  if (IsScript(shared->script())) {
    LogFunctionCompilation(isolate, LogEventListener::CodeTag::kFunction,
                           handle(Cast<Script>(shared->script()), isolate),
                           shared, Handle<FeedbackVector>(),
                           Cast<AbstractCode>(code), CodeKind::BASELINE,
                           time_taken_ms);
  }
  return true;
}

// static
bool Compiler::CompileBaseline(Isolate* isolate,
                               DirectHandle<JSFunction> function,
                               ClearExceptionFlag flag,
                               IsCompiledScope* is_compiled_scope) {
  Handle<SharedFunctionInfo> shared(function->shared(isolate), isolate);
  if (!CompileSharedWithBaseline(isolate, shared, flag, is_compiled_scope)) {
    return false;
  }

  // Baseline code needs a feedback vector.
  JSFunction::EnsureFeedbackVector(isolate, function, is_compiled_scope);

  Tagged<Code> baseline_code = shared->baseline_code(kAcquireLoad);
  DCHECK_EQ(baseline_code->kind(), CodeKind::BASELINE);
  function->set_code(baseline_code);
  return true;
}

// static
MaybeHandle<SharedFunctionInfo> Compiler::CompileToplevel(
    ParseInfo* parse_info, Handle<Script> script, Isolate* isolate,
    IsCompiledScope* is_compiled_scope) {
  return v8::internal::CompileToplevel(parse_info, script, kNullMaybeHandle,
                                       isolate, is_compiled_scope);
}

// static
bool Compiler::FinalizeBackgroundCompileTask(BackgroundCompileTask* task,
                                             Isolate* isolate,
                                             ClearExceptionFlag flag) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.FinalizeBackgroundCompileTask");
  RCS_SCOPE(isolate,
            RuntimeCallCounterId::kCompileFinalizeBackgroundCompileTask);

  HandleScope scope(isolate);

  if (!task->FinalizeFunction(isolate, flag)) return false;

  DCHECK(!isolate->has_exception());
  return true;
}

// static
void Compiler::CompileOptimized(Isolate* isolate, Handle<JSFunction> function,
                                ConcurrencyMode mode, CodeKind code_kind) {
  DCHECK(CodeKindIsOptimizedJSFunction(code_kind));
  DCHECK(AllowCompilation::IsAllowed(isolate));

  if (v8_flags.stress_concurrent_inlining &&
      isolate->concurrent_recompilation_enabled() && IsSynchronous(mode) &&
      isolate->node_observer() == nullptr) {
    SpawnDuplicateConcurrentJobForStressTesting(isolate, function, mode,
                                                code_kind);
  }

  Handle<Code> code;
  if (GetOrCompileOptimized(isolate, function, mode, code_kind)
          .ToHandle(&code)) {
    function->set_code(*code);
  }

#ifdef DEBUG
  DCHECK(!isolate->has_exception());
  DCHECK(function->is_compiled(isolate));
  DCHECK(function->shared()->HasBytecodeArray());
  const TieringState tiering_state = function->tiering_state();
  DCHECK(IsNone(tiering_state) || IsInProgress(tiering_state));
  DCHECK_IMPLIES(IsInProgress(tiering_state),
                 function->ChecksTieringState(isolate));
  DCHECK_IMPLIES(IsInProgress(tiering_state), IsConcurrent(mode));
#endif  // DEBUG
}

// static
MaybeHandle<SharedFunctionInfo> Compiler::CompileForLiveEdit(
    ParseInfo* parse_info, Handle<Script> script,
    MaybeHandle<ScopeInfo> outer_scope_info, Isolate* isolate) {
  IsCompiledScope is_compiled_scope;
  return v8::internal::CompileToplevel(parse_info, script, outer_scope_info,
                                       isolate, &is_compiled_scope);
}

// static
MaybeHandle<JSFunction> Compiler::GetFunctionFromEval(
    Handle<String> source, Handle<SharedFunctionInfo> outer_info,
    Handle<Context> context, LanguageMode language_mode,
    ParseRestriction restriction, int parameters_end_pos, int eval_position,
    ParsingWhileDebugging parsing_while_debugging) {
  Isolate* isolate = context->GetIsolate();

  // The cache lookup key needs to be aware of the separation between the
  // parameters and the body to prevent this valid invocation:
  //   Function("", "function anonymous(\n/**/) {\n}");
  // from adding an entry that falsely approves this invalid invocation:
  //   Function("\n/**/) {\nfunction anonymous(", "}");
  // The actual eval_position for indirect eval and CreateDynamicFunction
  // is unused (just 0), which means it's an available field to use to indicate
  // this separation. But to make sure we're not causing other false hits, we
  // negate the scope position.
  int eval_cache_position = eval_position;
  if (restriction == ONLY_SINGLE_FUNCTION_LITERAL &&
      parameters_end_pos != kNoSourcePosition) {
    // use the parameters_end_pos as the eval_position in the eval cache.
    DCHECK_EQ(eval_position, kNoSourcePosition);
    eval_cache_position = -parameters_end_pos;
  }
  CompilationCache* compilation_cache = isolate->compilation_cache();
  InfoCellPair eval_result = compilation_cache->LookupEval(
      source, outer_info, context, language_mode, eval_cache_position);
  Handle<FeedbackCell> feedback_cell;
  if (eval_result.has_feedback_cell()) {
    feedback_cell = handle(eval_result.feedback_cell(), isolate);
  }

  Handle<SharedFunctionInfo> shared_info;
  Handle<Script> script;
  IsCompiledScope is_compiled_scope;
  bool allow_eval_cache;
  if (eval_result.has_shared()) {
    shared_info = Handle<SharedFunctionInfo>(eval_result.shared(), isolate);
    script = Handle<Script>(Cast<Script>(shared_info->script()), isolate);
    is_compiled_scope = shared_info->is_compiled_scope(isolate);
    allow_eval_cache = true;
  } else {
    UnoptimizedCompileFlags flags = UnoptimizedCompileFlags::ForToplevelCompile(
        isolate, true, language_mode, REPLMode::kNo, ScriptType::kClassic,
        v8_flags.lazy_eval);
    flags.set_is_eval(true);
    flags.set_parsing_while_debugging(parsing_while_debugging);
    DCHECK(!flags.is_module());
    flags.set_parse_restriction(restriction);

    UnoptimizedCompileState compile_state;
    ReusableUnoptimizedCompileState reusable_state(isolate);
    ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);
    parse_info.set_parameters_end_pos(parameters_end_pos);

    MaybeHandle<ScopeInfo> maybe_outer_scope_info;
    if (!IsNativeContext(*context)) {
      maybe_outer_scope_info = handle(context->scope_info(), isolate);
    }
    script = parse_info.CreateScript(
        isolate, source, kNullMaybeHandle,
        OriginOptionsForEval(outer_info->script(), parsing_while_debugging));
    script->set_eval_from_shared(*outer_info);
    if (eval_position == kNoSourcePosition) {
      // If the position is missing, attempt to get the code offset by
      // walking the stack. Do not translate the code offset into source
      // position, but store it as negative value for lazy translation.
      DebuggableStackFrameIterator it(isolate);
      if (!it.done() && it.is_javascript()) {
        FrameSummary summary = it.GetTopValidFrame();
        script->set_eval_from_shared(
            summary.AsJavaScript().function()->shared());
        script->set_origin_options(
            OriginOptionsForEval(*summary.script(), parsing_while_debugging));
        eval_position = -summary.code_offset();
      } else {
        eval_position = 0;
      }
    }
    script->set_eval_from_position(eval_position);

    if (!v8::internal::CompileToplevel(&parse_info, script,
                                       maybe_outer_scope_info, isolate,
                                       &is_compiled_scope)
             .ToHandle(&shared_info)) {
      return MaybeHandle<JSFunction>();
    }
    allow_eval_cache = parse_info.allow_eval_cache();
  }

  // If caller is strict mode, the result must be in strict mode as well.
  DCHECK(is_sloppy(language_mode) || is_strict(shared_info->language_mode()));

  Handle<JSFunction> result;
  if (eval_result.has_shared()) {
    if (eval_result.has_feedback_cell()) {
      result = Factory::JSFunctionBuilder{isolate, shared_info, context}
                   .set_feedback_cell(feedback_cell)
                   .set_allocation_type(AllocationType::kYoung)
                   .Build();
    } else {
      result = Factory::JSFunctionBuilder{isolate, shared_info, context}
                   .set_allocation_type(AllocationType::kYoung)
                   .Build();
      // TODO(mythria): I don't think we need this here. PostInstantiation
      // already initializes feedback cell.
      JSFunction::InitializeFeedbackCell(result, &is_compiled_scope, true);
      if (allow_eval_cache) {
        // Make sure to cache this result.
        DirectHandle<FeedbackCell> new_feedback_cell(
            result->raw_feedback_cell(), isolate);
        compilation_cache->PutEval(source, outer_info, context, shared_info,
                                   new_feedback_cell, eval_cache_position);
      }
    }
  } else {
    result = Factory::JSFunctionBuilder{isolate, shared_info, context}
                 .set_allocation_type(AllocationType::kYoung)
                 .Build();
    // TODO(mythria): I don't think we need this here. PostInstantiation
    // already initializes feedback cell.
    JSFunction::InitializeFeedbackCell(result, &is_compiled_scope, true);
    if (allow_eval_cache) {
      // Add the SharedFunctionInfo and the LiteralsArray to the eval cache if
      // we didn't retrieve from there.
      DirectHandle<FeedbackCell> new_feedback_cell(result->raw_feedback_cell(),
                                                   isolate);
      compilation_cache->PutEval(source, outer_info, context, shared_info,
                                 new_feedback_cell, eval_cache_position);
    }
  }
  DCHECK(is_compiled_scope.is_compiled());

  return result;
}

// Check whether embedder allows code generation in this context.
// (via v8::Isolate::SetAllowCodeGenerationFromStringsCallback)
bool CodeGenerationFromStringsAllowed(Isolate* isolate,
                                      Handle<NativeContext> context,
                                      Handle<String> source) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCodeGenerationFromStringsCallbacks);
  DCHECK(IsFalse(context->allow_code_gen_from_strings(), isolate));
  DCHECK(isolate->allow_code_gen_callback());
  AllowCodeGenerationFromStringsCallback callback =
      isolate->allow_code_gen_callback();
  ExternalCallbackScope external_callback(isolate,
                                          reinterpret_cast<Address>(callback));
  // Callback set. Let it decide if code generation is allowed.
  return callback(v8::Utils::ToLocal(context), v8::Utils::ToLocal(source));
}

// Check whether embedder allows code generation in this context.
// (via v8::Isolate::SetModifyCodeGenerationFromStringsCallback)
bool ModifyCodeGenerationFromStrings(Isolate* isolate,
                                     Handle<NativeContext> context,
                                     Handle<i::Object>* source,
                                     bool is_code_like) {
  DCHECK(isolate->modify_code_gen_callback());
  DCHECK(source);

  // Callback set. Run it, and use the return value as source, or block
  // execution if it's not set.
  VMState<EXTERNAL> state(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kCodeGenerationFromStringsCallbacks);
  ModifyCodeGenerationFromStringsResult result =
      isolate->modify_code_gen_callback()(v8::Utils::ToLocal(context),
                                          v8::Utils::ToLocal(*source),
                                          is_code_like);
  if (result.codegen_allowed && !result.modified_source.IsEmpty()) {
    // Use the new source (which might be the same as the old source).
    *source =
        Utils::OpenHandle(*result.modified_source.ToLocalChecked(), false);
  }
  return result.codegen_allowed;
}

// Run Embedder-mandated checks before generating code from a string.
//
// Returns a string to be used for compilation, or a flag that an object type
// was encountered that is neither a string, nor something the embedder knows
// how to handle.
//
// Returns: (assuming: std::tie(source, unknown_object))
// - !source.is_null(): compilation allowed, source contains the source string.
// - unknown_object is true: compilation allowed, but we don't know how to
//                           deal with source_object.
// - source.is_null() && !unknown_object: compilation should be blocked.
//
// - !source_is_null() and unknown_object can't be true at the same time.

// static
std::pair<MaybeHandle<String>, bool> Compiler::ValidateDynamicCompilationSource(
    Isolate* isolate, Handle<NativeContext> context,
    Handle<i::Object> original_source, bool is_code_like) {
  // Check if the context unconditionally allows code gen from strings.
  // allow_code_gen_from_strings can be many things, so we'll always check
  // against the 'false' literal, so that e.g. undefined and 'true' are treated
  // the same.
  if (!IsFalse(context->allow_code_gen_from_strings(), isolate) &&
      IsString(*original_source)) {
    return {Cast<String>(original_source), false};
  }

  // Check if the context allows code generation for this string.
  // allow_code_gen_callback only allows proper strings.
  // (I.e., let allow_code_gen_callback decide, if it has been set.)
  if (isolate->allow_code_gen_callback()) {
    // If we run into this condition, the embedder has marked some object
    // templates as "code like", but has given us a callback that only accepts
    // strings. That makes no sense.
    DCHECK(!Object::IsCodeLike(*original_source, isolate));

    if (!IsString(*original_source)) {
      return {MaybeHandle<String>(), true};
    }
    Handle<String> string_source = Cast<String>(original_source);
    if (!CodeGenerationFromStringsAllowed(isolate, context, string_source)) {
      return {MaybeHandle<String>(), false};
    }
    return {string_source, false};
  }

  // Check if the context wants to block or modify this source object.
  // Double-check that we really have a string now.
  // (Let modify_code_gen_callback decide, if it's been set.)
  if (isolate->modify_code_gen_callback()) {
    Handle<i::Object> modified_source = original_source;
    if (!ModifyCodeGenerationFromStrings(isolate, context, &modified_source,
                                         is_code_like)) {
      return {MaybeHandle<String>(), false};
    }
    if (!IsString(*modified_source)) {
      return {MaybeHandle<String>(), true};
    }
    return {Cast<String>(modified_source), false};
  }

  if (!IsFalse(context->allow_code_gen_from_strings(), isolate) &&
      Object::IsCodeLike(*original_source, isolate)) {
    // Codegen is unconditionally allowed, and we're been given a CodeLike
    // object. Stringify.
    MaybeHandle<String> stringified_source =
        Object::ToString(isolate, original_source);
    return {stringified_source, stringified_source.is_null()};
  }

  // If unconditional codegen was disabled, and no callback defined, we block
  // strings and allow all other objects.
  return {MaybeHandle<String>(), !IsString(*original_source)};
}

// static
MaybeHandle<JSFunction> Compiler::GetFunctionFromValidatedString(
    Handle<NativeContext> native_context, MaybeHandle<String> source,
    ParseRestriction restriction, int parameters_end_pos) {
  Isolate* const isolate = native_context->GetIsolate();

  // Raise an EvalError if we did not receive a string.
  if (source.is_null()) {
    Handle<Object> error_message =
        native_context->ErrorMessageForCodeGenerationFromStrings();
    THROW_NEW_ERROR(isolate, NewEvalError(MessageTemplate::kCodeGenFromStrings,
                                          error_message));
  }

  // Compile source string in the native context.
  int eval_position = kNoSourcePosition;
  Handle<SharedFunctionInfo> outer_info(
      native_context->empty_function()->shared(), isolate);
  return Compiler::GetFunctionFromEval(
      source.ToHandleChecked(), outer_info, native_context,
      LanguageMode::kSloppy, restriction, parameters_end_pos, eval_position);
}

// static
MaybeHandle<JSFunction> Compiler::GetFunctionFromString(
    Handle<NativeContext> context, Handle<Object> source,
    int parameters_end_pos, bool is_code_like) {
  Isolate* const isolate = context->GetIsolate();
  MaybeHandle<String> validated_source =
      ValidateDynamicCompilationSource(isolate, context, source, is_code_like)
          .first;
  return GetFunctionFromValidatedString(context, validated_source,
                                        ONLY_SINGLE_FUNCTION_LITERAL,
                                        parameters_end_pos);
}

namespace {

struct ScriptCompileTimerScope {
 public:
  // TODO(leszeks): There are too many blink-specific entries in this enum,
  // figure out a way to push produce/hit-isolate-cache/consume/consume-failed
  // back up the API and log them in blink instead.
  enum class CacheBehaviour {
    kProduceCodeCache,
    kHitIsolateCacheWhenNoCache,
    kConsumeCodeCache,
    kConsumeCodeCacheFailed,
    kNoCacheBecauseInlineScript,
    kNoCacheBecauseScriptTooSmall,
    kNoCacheBecauseCacheTooCold,
    kNoCacheNoReason,
    kNoCacheBecauseNoResource,
    kNoCacheBecauseInspector,
    kNoCacheBecauseCachingDisabled,
    kNoCacheBecauseModule,
    kNoCacheBecauseStreamingSource,
    kNoCacheBecauseV8Extension,
    kHitIsolateCacheWhenProduceCodeCache,
    kHitIsolateCacheWhenConsumeCodeCache,
    kNoCacheBecauseExtensionModule,
    kNoCacheBecausePacScript,
    kNoCacheBecauseInDocumentWrite,
    kNoCacheBecauseResourceWithNoCacheHandler,
    kHitIsolateCacheWhenStreamingSource,
    kCount
  };

  ScriptCompileTimerScope(
      Isolate* isolate, ScriptCompiler::NoCacheReason no_cache_reason,
      ScriptCompiler::CompilationDetails* compilation_details)
      : isolate_(isolate),
        histogram_scope_(&compilation_details->foreground_time_in_microseconds),
        all_scripts_histogram_scope_(isolate->counters()->compile_script()),
        no_cache_reason_(no_cache_reason),
        hit_isolate_cache_(false),
        consuming_code_cache_(false),
        consuming_code_cache_failed_(false) {}

  ~ScriptCompileTimerScope() {
    CacheBehaviour cache_behaviour = GetCacheBehaviour();

    Histogram* cache_behaviour_histogram =
        isolate_->counters()->compile_script_cache_behaviour();
    // Sanity check that the histogram has exactly one bin per enum entry.
    DCHECK_EQ(0, cache_behaviour_histogram->min());
    DCHECK_EQ(static_cast<int>(CacheBehaviour::kCount),
              cache_behaviour_histogram->max() + 1);
    DCHECK_EQ(static_cast<int>(CacheBehaviour::kCount),
              cache_behaviour_histogram->num_buckets());
    cache_behaviour_histogram->AddSample(static_cast<int>(cache_behaviour));

    histogram_scope_.set_histogram(
        GetCacheBehaviourTimedHistogram(cache_behaviour));
  }

  void set_hit_isolate_cache() { hit_isolate_cache_ = true; }

  void set_consuming_code_cache() { consuming_code_cache_ = true; }

  void set_consuming_code_cache_failed() {
    consuming_code_cache_failed_ = true;
  }

 private:
  Isolate* isolate_;
  LazyTimedHistogramScope histogram_scope_;
  // TODO(leszeks): This timer is the sum of the other times, consider removing
  // it to save space.
  NestedTimedHistogramScope all_scripts_histogram_scope_;
  ScriptCompiler::NoCacheReason no_cache_reason_;
  bool hit_isolate_cache_;
  bool consuming_code_cache_;
  bool consuming_code_cache_failed_;

  CacheBehaviour GetCacheBehaviour() {
    if (consuming_code_cache_) {
      if (hit_isolate_cache_) {
        return CacheBehaviour::kHitIsolateCacheWhenConsumeCodeCache;
      } else if (consuming_code_cache_failed_) {
        return CacheBehaviour::kConsumeCodeCacheFailed;
      }
      return CacheBehaviour::kConsumeCodeCache;
    }

    if (hit_isolate_cache_) {
      // A roundabout way of knowing the embedder is going to produce a code
      // cache (which is done by a separate API call later) is to check whether
      // no_cache_reason_ is
      // ScriptCompiler::kNoCacheBecauseDeferredProduceCodeCache.
      if (no_cache_reason_ ==
          ScriptCompiler::kNoCacheBecauseDeferredProduceCodeCache) {
        return CacheBehaviour::kHitIsolateCacheWhenProduceCodeCache;
      } else if (no_cache_reason_ ==
                 ScriptCompiler::kNoCacheBecauseStreamingSource) {
        return CacheBehaviour::kHitIsolateCacheWhenStreamingSource;
      }
      return CacheBehaviour::kHitIsolateCacheWhenNoCache;
    }

    switch (no_cache_reason_) {
      case ScriptCompiler::kNoCacheBecauseInlineScript:
        return CacheBehaviour::kNoCacheBecauseInlineScript;
      case ScriptCompiler::kNoCacheBecauseScriptTooSmall:
        return CacheBehaviour::kNoCacheBecauseScriptTooSmall;
      case ScriptCompiler::kNoCacheBecauseCacheTooCold:
        return CacheBehaviour::kNoCacheBecauseCacheTooCold;
      case ScriptCompiler::kNoCacheNoReason:
        return CacheBehaviour::kNoCacheNoReason;
      case ScriptCompiler::kNoCacheBecauseNoResource:
        return CacheBehaviour::kNoCacheBecauseNoResource;
      case ScriptCompiler::kNoCacheBecauseInspector:
        return CacheBehaviour::kNoCacheBecauseInspector;
      case ScriptCompiler::kNoCacheBecauseCachingDisabled:
        return CacheBehaviour::kNoCacheBecauseCachingDisabled;
      case ScriptCompiler::kNoCacheBecauseModule:
        return CacheBehaviour::kNoCacheBecauseModule;
      case ScriptCompiler::kNoCacheBecauseStreamingSource:
        return CacheBehaviour::kNoCacheBecauseStreamingSource;
      case ScriptCompiler::kNoCacheBecauseV8Extension:
        return CacheBehaviour::kNoCacheBecauseV8Extension;
      case ScriptCompiler::kNoCacheBecauseExtensionModule:
        return CacheBehaviour::kNoCacheBecauseExtensionModule;
      case ScriptCompiler::kNoCacheBecausePacScript:
        return CacheBehaviour::kNoCacheBecausePacScript;
      case ScriptCompiler::kNoCacheBecauseInDocumentWrite:
        return CacheBehaviour::kNoCacheBecauseInDocumentWrite;
      case ScriptCompiler::kNoCacheBecauseResourceWithNoCacheHandler:
        return CacheBehaviour::kNoCacheBecauseResourceWithNoCacheHandler;
      case ScriptCompiler::kNoCacheBecauseDeferredProduceCodeCache:
        return CacheBehaviour::kProduceCodeCache;
      }
    UNREACHABLE();
  }

  TimedHistogram* GetCacheBehaviourTimedHistogram(
      CacheBehaviour cache_behaviour) {
    switch (cache_behaviour) {
      case CacheBehaviour::kProduceCodeCache:
      // Even if we hit the isolate's compilation cache, we currently recompile
      // when we want to produce the code cache.
      case CacheBehaviour::kHitIsolateCacheWhenProduceCodeCache:
        return isolate_->counters()->compile_script_with_produce_cache();
      case CacheBehaviour::kHitIsolateCacheWhenNoCache:
      case CacheBehaviour::kHitIsolateCacheWhenConsumeCodeCache:
      case CacheBehaviour::kHitIsolateCacheWhenStreamingSource:
        return isolate_->counters()->compile_script_with_isolate_cache_hit();
      case CacheBehaviour::kConsumeCodeCacheFailed:
        return isolate_->counters()->compile_script_consume_failed();
      case CacheBehaviour::kConsumeCodeCache:
        return isolate_->counters()->compile_script_with_consume_cache();

      // Note that this only counts the finalization part of streaming, the
      // actual streaming compile is counted by BackgroundCompileTask into
      // "compile_script_on_background".
      case CacheBehaviour::kNoCacheBecauseStreamingSource:
        return isolate_->counters()->compile_script_streaming_finalization();

      case CacheBehaviour::kNoCacheBecauseInlineScript:
        return isolate_->counters()
            ->compile_script_no_cache_because_inline_script();
      case CacheBehaviour::kNoCacheBecauseScriptTooSmall:
        return isolate_->counters()
            ->compile_script_no_cache_because_script_too_small();
      case CacheBehaviour::kNoCacheBecauseCacheTooCold:
        return isolate_->counters()
            ->compile_script_no_cache_because_cache_too_cold();

      // Aggregate all the other "no cache" counters into a single histogram, to
      // save space.
      case CacheBehaviour::kNoCacheNoReason:
      case CacheBehaviour::kNoCacheBecauseNoResource:
      case CacheBehaviour::kNoCacheBecauseInspector:
      case CacheBehaviour::kNoCacheBecauseCachingDisabled:
      // TODO(leszeks): Consider counting separately once modules are more
      // common.
      case CacheBehaviour::kNoCacheBecauseModule:
      case CacheBehaviour::kNoCacheBecauseV8Extension:
      case CacheBehaviour::kNoCacheBecauseExtensionModule:
      case CacheBehaviour::kNoCacheBecausePacScript:
      case CacheBehaviour::kNoCacheBecauseInDocumentWrite:
      case CacheBehaviour::kNoCacheBecauseResourceWithNoCacheHandler:
        return isolate_->counters()->compile_script_no_cache_other();

      case CacheBehaviour::kCount:
        UNREACHABLE();
    }
    UNREACHABLE();
  }
};

Handle<Script> NewScript(Isolate* isolate, ParseInfo* parse_info,
                         Handle<String> source, ScriptDetails script_details,
                         NativesFlag natives) {
  // Create a script object describing the script to be compiled.
  Handle<Script> script = parse_info->CreateScript(
      isolate, source, script_details.wrapped_arguments,
      script_details.origin_options, natives);
  DisallowGarbageCollection no_gc;
  SetScriptFieldsFromDetails(isolate, *script, script_details, &no_gc);
  LOG(isolate, ScriptDetails(*script));
  return script;
}

MaybeHandle<SharedFunctionInfo> CompileScriptOnMainThread(
    const UnoptimizedCompileFlags flags, Handle<String> source,
    const ScriptDetails& script_details, NativesFlag natives,
    v8::Extension* extension, Isolate* isolate,
    MaybeHandle<Script> maybe_script, IsCompiledScope* is_compiled_scope,
    CompileHintCallback compile_hint_callback = nullptr,
    void* compile_hint_callback_data = nullptr) {
  UnoptimizedCompileState compile_state;
  ReusableUnoptimizedCompileState reusable_state(isolate);
  ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);
  parse_info.set_extension(extension);
  parse_info.SetCompileHintCallbackAndData(compile_hint_callback,
                                           compile_hint_callback_data);

  Handle<Script> script;
  if (!maybe_script.ToHandle(&script)) {
    script = NewScript(isolate, &parse_info, source, script_details, natives);
  }
  DCHECK_EQ(parse_info.flags().is_repl_mode(), script->is_repl_mode());

  return Compiler::CompileToplevel(&parse_info, script, isolate,
                                   is_compiled_scope);
}

class StressBackgroundCompileThread : public ParkingThread {
 public:
  StressBackgroundCompileThread(Isolate* isolate, Handle<String> source,
                                const ScriptDetails& script_details)
      : ParkingThread(
            base::Thread::Options("StressBackgroundCompileThread", 2 * i::MB)),
        source_(source),
        streamed_source_(std::make_unique<SourceStream>(source, isolate),
                         v8::ScriptCompiler::StreamedSource::UTF8) {
    ScriptType type = script_details.origin_options.IsModule()
                          ? ScriptType::kModule
                          : ScriptType::kClassic;
    data()->task = std::make_unique<i::BackgroundCompileTask>(
        data(), isolate, type,
        ScriptCompiler::CompileOptions::kNoCompileOptions,
        &streamed_source_.compilation_details());
  }

  void Run() override { data()->task->Run(); }

  ScriptStreamingData* data() { return streamed_source_.impl(); }

 private:
  // Dummy external source stream which returns the whole source in one go.
  // TODO(leszeks): Also test chunking the data.
  class SourceStream : public v8::ScriptCompiler::ExternalSourceStream {
   public:
    SourceStream(DirectHandle<String> source, Isolate* isolate) : done_(false) {
      source_buffer_ = source->ToCString(ALLOW_NULLS, FAST_STRING_TRAVERSAL,
                                         &source_length_);
    }

    size_t GetMoreData(const uint8_t** src) override {
      if (done_) {
        return 0;
      }
      *src = reinterpret_cast<uint8_t*>(source_buffer_.release());
      done_ = true;

      return source_length_;
    }

   private:
    int source_length_;
    std::unique_ptr<char[]> source_buffer_;
    bool done_;
  };

  Handle<String> source_;
  v8::ScriptCompiler::StreamedSource streamed_source_;
};

bool CanBackgroundCompile(const ScriptDetails& script_details,
                          v8::Extension* extension,
                          ScriptCompiler::CompileOptions compile_options,
                          NativesFlag natives) {
  // TODO(leszeks): Remove the module check once background compilation of
  // modules is supported.
  return !script_details.origin_options.IsModule() && !extension &&
         script_details.repl_mode == REPLMode::kNo &&
         (compile_options == ScriptCompiler::kNoCompileOptions) &&
         natives == NOT_NATIVES_CODE;
}

bool CompilationExceptionIsRangeError(Isolate* isolate, Handle<Object> obj) {
  if (!IsJSError(*obj, isolate)) return false;
  Handle<JSReceiver> js_obj = Cast<JSReceiver>(obj);
  Handle<JSReceiver> constructor;
  if (!JSReceiver::GetConstructor(isolate, js_obj).ToHandle(&constructor)) {
    return false;
  }
  return *constructor == *isolate->range_error_function();
}

MaybeHandle<SharedFunctionInfo> CompileScriptOnBothBackgroundAndMainThread(
    Handle<String> source, const ScriptDetails& script_details,
    Isolate* isolate, IsCompiledScope* is_compiled_scope) {
  // Start a background thread compiling the script.
  StressBackgroundCompileThread background_compile_thread(isolate, source,
                                                          script_details);

  UnoptimizedCompileFlags flags_copy =
      background_compile_thread.data()->task->flags();

  CHECK(background_compile_thread.Start());
  MaybeHandle<SharedFunctionInfo> main_thread_maybe_result;
  bool main_thread_had_stack_overflow = false;
  // In parallel, compile on the main thread to flush out any data races.
  {
    IsCompiledScope inner_is_compiled_scope;
    // The background thread should also create any relevant exceptions, so we
    // can ignore the main-thread created ones.
    // TODO(leszeks): Maybe verify that any thrown (or unthrown) exceptions are
    // equivalent.
    TryCatch ignore_try_catch(reinterpret_cast<v8::Isolate*>(isolate));
    flags_copy.set_script_id(Script::kTemporaryScriptId);
    main_thread_maybe_result = CompileScriptOnMainThread(
        flags_copy, source, script_details, NOT_NATIVES_CODE, nullptr, isolate,
        MaybeHandle<Script>(), &inner_is_compiled_scope);
    if (main_thread_maybe_result.is_null()) {
      // Assume all range errors are stack overflows.
      main_thread_had_stack_overflow = CompilationExceptionIsRangeError(
          isolate, handle(isolate->exception(), isolate));
      isolate->clear_exception();
    }
  }

  // Join with background thread and finalize compilation.
  background_compile_thread.ParkedJoin(isolate->main_thread_local_isolate());

  ScriptCompiler::CompilationDetails compilation_details;
  MaybeHandle<SharedFunctionInfo> maybe_result =
      Compiler::GetSharedFunctionInfoForStreamedScript(
          isolate, source, script_details, background_compile_thread.data(),
          &compilation_details);

  // Either both compiles should succeed, or both should fail. The one exception
  // to this is that the main-thread compilation might stack overflow while the
  // background compilation doesn't, so relax the check to include this case.
  // TODO(leszeks): Compare the contents of the results of the two compiles.
  if (main_thread_had_stack_overflow) {
    CHECK(main_thread_maybe_result.is_null());
  } else {
    CHECK_EQ(maybe_result.is_null(), main_thread_maybe_result.is_null());
  }

  Handle<SharedFunctionInfo> result;
  if (maybe_result.ToHandle(&result)) {
    // The BackgroundCompileTask's IsCompiledScope will keep the result alive
    // until it dies at the end of this function, after which this new
    // IsCompiledScope can take over.
    *is_compiled_scope = result->is_compiled_scope(isolate);
  }

  return maybe_result;
}

namespace {
ScriptCompiler::InMemoryCacheResult CategorizeLookupResult(
    const CompilationCacheScript::LookupResult& lookup_result) {
  return !lookup_result.toplevel_sfi().is_null()
             ? ScriptCompiler::InMemoryCacheResult::kHit
         : !lookup_result.script().is_null()
             ? ScriptCompiler::InMemoryCacheResult::kPartial
             : ScriptCompiler::InMemoryCacheResult::kMiss;
}
}  // namespace

MaybeHandle<SharedFunctionInfo> GetSharedFunctionInfoForScriptImpl(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details, v8::Extension* extension,
    AlignedCachedData* cached_data, BackgroundDeserializeTask* deserialize_task,
    v8::CompileHintCallback compile_hint_callback,
    void* compile_hint_callback_data,
    ScriptCompiler::CompileOptions compile_options,
    ScriptCompiler::NoCacheReason no_cache_reason, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  ScriptCompileTimerScope compile_timer(isolate, no_cache_reason,
                                        compilation_details);

  if (compile_options & ScriptCompiler::kConsumeCodeCache) {
    // Have to have exactly one of cached_data or deserialize_task.
    DCHECK(cached_data || deserialize_task);
    DCHECK(!(cached_data && deserialize_task));
    DCHECK_NULL(extension);
  } else {
    DCHECK_NULL(cached_data);
    DCHECK_NULL(deserialize_task);
  }

  if (compile_options & ScriptCompiler::kConsumeCompileHints) {
    DCHECK_NOT_NULL(compile_hint_callback);
    DCHECK_NOT_NULL(compile_hint_callback_data);
  } else {
    DCHECK_NULL(compile_hint_callback);
    DCHECK_NULL(compile_hint_callback_data);
  }

  compilation_details->background_time_in_microseconds =
      deserialize_task ? deserialize_task->background_time_in_microseconds()
                       : 0;

  LanguageMode language_mode = construct_language_mode(v8_flags.use_strict);
  CompilationCache* compilation_cache = isolate->compilation_cache();

  // For extensions or REPL mode scripts neither do a compilation cache lookup,
  // nor put the compilation result back into the cache.
  const bool use_compilation_cache =
      extension == nullptr && script_details.repl_mode == REPLMode::kNo;
  MaybeHandle<SharedFunctionInfo> maybe_result;
  MaybeHandle<Script> maybe_script;
  IsCompiledScope is_compiled_scope;
  if (use_compilation_cache) {
    bool can_consume_code_cache =
        compile_options & ScriptCompiler::kConsumeCodeCache;
    if (can_consume_code_cache) {
      compile_timer.set_consuming_code_cache();
    }

    // First check per-isolate compilation cache.
    CompilationCacheScript::LookupResult lookup_result =
        compilation_cache->LookupScript(source, script_details, language_mode);
    compilation_details->in_memory_cache_result =
        CategorizeLookupResult(lookup_result);
    maybe_script = lookup_result.script();
    maybe_result = lookup_result.toplevel_sfi();
    is_compiled_scope = lookup_result.is_compiled_scope();
    if (!maybe_result.is_null()) {
      compile_timer.set_hit_isolate_cache();
    } else if (can_consume_code_cache) {
      compile_timer.set_consuming_code_cache();
      // Then check cached code provided by embedder.
      NestedTimedHistogramScope timer(
          isolate->counters()->compile_deserialize());
      RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileDeserialize);
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.CompileDeserialize");
      if (deserialize_task) {
        // If there's a cache consume task, finish it.
        maybe_result =
            deserialize_task->Finish(isolate, source, script_details);
        // It is possible at this point that there is a Script object for this
        // script in the compilation cache (held in the variable maybe_script),
        // which does not match maybe_result->script(). This could happen any of
        // three ways:
        // 1. The embedder didn't call MergeWithExistingScript.
        // 2. At the time the embedder called SourceTextAvailable, there was not
        //    yet a Script in the compilation cache, but it arrived sometime
        //    later.
        // 3. At the time the embedder called SourceTextAvailable, there was a
        //    Script available, and the new content has been merged into that
        //    Script. However, since then, the Script was replaced in the
        //    compilation cache, such as by another evaluation of the script
        //    hitting case 2, or DevTools clearing the cache.
        // This is okay; the new Script object will replace the current Script
        // held by the compilation cache. Both Scripts may remain in use
        // indefinitely, causing increased memory usage, but these cases are
        // sufficiently unlikely, and ensuring a correct merge in the third case
        // would be non-trivial.
      } else {
        maybe_result = CodeSerializer::Deserialize(
            isolate, cached_data, source, script_details, maybe_script);
      }

      bool consuming_code_cache_succeeded = false;
      Handle<SharedFunctionInfo> result;
      if (maybe_result.ToHandle(&result)) {
        is_compiled_scope = result->is_compiled_scope(isolate);
        if (is_compiled_scope.is_compiled()) {
          consuming_code_cache_succeeded = true;
          // Promote to per-isolate compilation cache.
          compilation_cache->PutScript(source, language_mode, result);
        }
      }
      if (!consuming_code_cache_succeeded) {
        // Deserializer failed. Fall through to compile.
        compile_timer.set_consuming_code_cache_failed();
      }
    }
  }

  if (maybe_result.is_null()) {
    // No cache entry found compile the script.
    if (v8_flags.stress_background_compile &&
        CanBackgroundCompile(script_details, extension, compile_options,
                             natives)) {
      // If the --stress-background-compile flag is set, do the actual
      // compilation on a background thread, and wait for its result.
      maybe_result = CompileScriptOnBothBackgroundAndMainThread(
          source, script_details, isolate, &is_compiled_scope);
    } else {
      UnoptimizedCompileFlags flags =
          UnoptimizedCompileFlags::ForToplevelCompile(
              isolate, natives == NOT_NATIVES_CODE, language_mode,
              script_details.repl_mode,
              script_details.origin_options.IsModule() ? ScriptType::kModule
                                                       : ScriptType::kClassic,
              v8_flags.lazy);

      flags.set_is_eager(compile_options & ScriptCompiler::kEagerCompile);
      flags.set_compile_hints_magic_enabled(
          compile_options & ScriptCompiler::kFollowCompileHintsMagicComment);

      if (Handle<Script> script; maybe_script.ToHandle(&script)) {
        flags.set_script_id(script->id());
      }

      maybe_result = CompileScriptOnMainThread(
          flags, source, script_details, natives, extension, isolate,
          maybe_script, &is_compiled_scope, compile_hint_callback,
          compile_hint_callback_data);
    }

    // Add the result to the isolate cache.
    Handle<SharedFunctionInfo> result;
    if (use_compilation_cache && maybe_result.ToHandle(&result)) {
      DCHECK(is_compiled_scope.is_compiled());
      compilation_cache->PutScript(source, language_mode, result);
    } else if (maybe_result.is_null() && natives != EXTENSION_CODE) {
      isolate->ReportPendingMessages();
    }
  }
  Handle<SharedFunctionInfo> result;
  if (compile_options & ScriptCompiler::CompileOptions::kProduceCompileHints &&
      maybe_result.ToHandle(&result)) {
    Cast<Script>(result->script())->set_produce_compile_hints(true);
  }

  return maybe_result;
}

}  // namespace

MaybeHandle<SharedFunctionInfo> Compiler::GetSharedFunctionInfoForScript(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details,
    ScriptCompiler::CompileOptions compile_options,
    ScriptCompiler::NoCacheReason no_cache_reason, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  return GetSharedFunctionInfoForScriptImpl(
      isolate, source, script_details, nullptr, nullptr, nullptr, nullptr,
      nullptr, compile_options, no_cache_reason, natives, compilation_details);
}

MaybeHandle<SharedFunctionInfo>
Compiler::GetSharedFunctionInfoForScriptWithExtension(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details, v8::Extension* extension,
    ScriptCompiler::CompileOptions compile_options, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  return GetSharedFunctionInfoForScriptImpl(
      isolate, source, script_details, extension, nullptr, nullptr, nullptr,
      nullptr, compile_options, ScriptCompiler::kNoCacheBecauseV8Extension,
      natives, compilation_details);
}

MaybeHandle<SharedFunctionInfo>
Compiler::GetSharedFunctionInfoForScriptWithCachedData(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details, AlignedCachedData* cached_data,
    ScriptCompiler::CompileOptions compile_options,
    ScriptCompiler::NoCacheReason no_cache_reason, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  return GetSharedFunctionInfoForScriptImpl(
      isolate, source, script_details, nullptr, cached_data, nullptr, nullptr,
      nullptr, compile_options, no_cache_reason, natives, compilation_details);
}

MaybeHandle<SharedFunctionInfo>
Compiler::GetSharedFunctionInfoForScriptWithDeserializeTask(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details,
    BackgroundDeserializeTask* deserialize_task,
    ScriptCompiler::CompileOptions compile_options,
    ScriptCompiler::NoCacheReason no_cache_reason, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  return GetSharedFunctionInfoForScriptImpl(
      isolate, source, script_details, nullptr, nullptr, deserialize_task,
      nullptr, nullptr, compile_options, no_cache_reason, natives,
      compilation_details);
}

MaybeHandle<SharedFunctionInfo>
Compiler::GetSharedFunctionInfoForScriptWithCompileHints(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details,
    v8::CompileHintCallback compile_hint_callback,
    void* compile_hint_callback_data,
    ScriptCompiler::CompileOptions compile_options,
    ScriptCompiler::NoCacheReason no_cache_reason, NativesFlag natives,
    ScriptCompiler::CompilationDetails* compilation_details) {
  return GetSharedFunctionInfoForScriptImpl(
      isolate, source, script_details, nullptr, nullptr, nullptr,
      compile_hint_callback, compile_hint_callback_data, compile_options,
      no_cache_reason, natives, compilation_details);
}

// static
MaybeHandle<JSFunction> Compiler::GetWrappedFunction(
    Handle<String> source, Handle<Context> context,
    const ScriptDetails& script_details, AlignedCachedData* cached_data,
    v8::ScriptCompiler::CompileOptions compile_options,
    v8::ScriptCompiler::NoCacheReason no_cache_reason) {
  Isolate* isolate = context->GetIsolate();
  ScriptCompiler::CompilationDetails compilation_details;
  ScriptCompileTimerScope compile_timer(isolate, no_cache_reason,
                                        &compilation_details);

  if (compile_options & ScriptCompiler::kConsumeCodeCache) {
    DCHECK(cached_data);
    DCHECK_EQ(script_details.repl_mode, REPLMode::kNo);
  } else {
    DCHECK_NULL(cached_data);
  }

  LanguageMode language_mode = construct_language_mode(v8_flags.use_strict);
  DCHECK(!script_details.wrapped_arguments.is_null());
  MaybeHandle<SharedFunctionInfo> maybe_result;
  Handle<SharedFunctionInfo> result;
  Handle<Script> script;
  IsCompiledScope is_compiled_scope;
  bool can_consume_code_cache =
      compile_options & ScriptCompiler::kConsumeCodeCache;
  CompilationCache* compilation_cache = isolate->compilation_cache();
  // First check per-isolate compilation cache.
  CompilationCacheScript::LookupResult lookup_result =
      compilation_cache->LookupScript(source, script_details, language_mode);
  maybe_result = lookup_result.toplevel_sfi();
  if (maybe_result.ToHandle(&result)) {
    is_compiled_scope = result->is_compiled_scope(isolate);
    compile_timer.set_hit_isolate_cache();
  } else if (can_consume_code_cache) {
    compile_timer.set_consuming_code_cache();
    // Then check cached code provided by embedder.
    NestedTimedHistogramScope timer(isolate->counters()->compile_deserialize());
    RCS_SCOPE(isolate, RuntimeCallCounterId::kCompileDeserialize);
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.CompileDeserialize");
    maybe_result = CodeSerializer::Deserialize(isolate, cached_data, source,
                                               script_details);
    bool consuming_code_cache_succeeded = false;
    if (maybe_result.ToHandle(&result)) {
      is_compiled_scope = result->is_compiled_scope(isolate);
      if (is_compiled_scope.is_compiled()) {
        consuming_code_cache_succeeded = true;
        // Promote to per-isolate compilation cache.
        compilation_cache->PutScript(source, language_mode, result);
      }
    }
    if (!consuming_code_cache_succeeded) {
      // Deserializer failed. Fall through to compile.
      compile_timer.set_consuming_code_cache_failed();
    }
  }

  if (maybe_result.is_null()) {
    UnoptimizedCompileFlags flags = UnoptimizedCompileFlags::ForToplevelCompile(
        isolate, true, language_mode, script_details.repl_mode,
        ScriptType::kClassic, v8_flags.lazy);
    flags.set_is_eval(true);  // Use an eval scope as declaration scope.
    flags.set_function_syntax_kind(FunctionSyntaxKind::kWrapped);
    // TODO(delphick): Remove this and instead make the wrapped and wrapper
    // functions fully non-lazy instead thus preventing source positions from
    // being omitted.
    flags.set_collect_source_positions(true);
    flags.set_is_eager(compile_options & ScriptCompiler::kEagerCompile);

    UnoptimizedCompileState compile_state;
    ReusableUnoptimizedCompileState reusable_state(isolate);
    ParseInfo parse_info(isolate, flags, &compile_state, &reusable_state);

    MaybeHandle<ScopeInfo> maybe_outer_scope_info;
    if (!IsNativeContext(*context)) {
      maybe_outer_scope_info = handle(context->scope_info(), isolate);
    }
    script = NewScript(isolate, &parse_info, source, script_details,
                       NOT_NATIVES_CODE);

    Handle<SharedFunctionInfo> top_level;
    maybe_result = v8::internal::CompileToplevel(&parse_info, script,
                                                 maybe_outer_scope_info,
                                                 isolate, &is_compiled_scope);
    if (maybe_result.is_null()) isolate->ReportPendingMessages();
    ASSIGN_RETURN_ON_EXCEPTION(isolate, top_level, maybe_result);

    SharedFunctionInfo::ScriptIterator infos(isolate, *script);
    for (Tagged<SharedFunctionInfo> info = infos.Next(); !info.is_null();
         info = infos.Next()) {
      if (info->is_wrapped()) {
        result = Handle<SharedFunctionInfo>(info, isolate);
        break;
      }
    }
    DCHECK(!result.is_null());

    is_compiled_scope = result->is_compiled_scope(isolate);
    script = Handle<Script>(Cast<Script>(result->script()), isolate);
    // Add the result to the isolate cache if there's no context extension.
    if (maybe_outer_scope_info.is_null()) {
      compilation_cache->PutScript(source, language_mode, result);
    }
  }

  DCHECK(is_compiled_scope.is_compiled());

  return Factory::JSFunctionBuilder{isolate, result, context}
      .set_allocation_type(AllocationType::kYoung)
      .Build();
}

// static
MaybeHandle<SharedFunctionInfo>
Compiler::GetSharedFunctionInfoForStreamedScript(
    Isolate* isolate, Handle<String> source,
    const ScriptDetails& script_details, ScriptStreamingData* streaming_data,
    ScriptCompiler::CompilationDetails* compilation_details) {
  DCHECK(!script_details.origin_options.IsWasm());

  ScriptCompileTimerScope compile_timer(
      isolate, ScriptCompiler::kNoCacheBecauseStreamingSource,
      compilation_details);
  PostponeInterruptsScope postpone(isolate);

  BackgroundCompileTask* task = streaming_data->task.get();

  MaybeHandle<SharedFunctionInfo> maybe_result;
  MaybeHandle<Script> maybe_cached_script;
  // Check if compile cache already holds the SFI, if so no need to finalize
  // the code compiled on the background thread.
  CompilationCache* compilation_cache = isolate->compilation_cache();
  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.StreamingFinalization.CheckCache");
    CompilationCacheScript::LookupResult lookup_result =
        compilation_cache->LookupScript(source, script_details,
                                        task->flags().outer_language_mode());
    compilation_details->in_memory_cache_result =
        CategorizeLookupResult(lookup_result);

    if (!lookup_result.toplevel_sfi().is_null()) {
      maybe_result = lookup_result.toplevel_sfi();
    }

    if (!maybe_result.is_null()) {
      compile_timer.set_hit_isolate_cache();
    } else {
      maybe_cached_script = lookup_result.script();
    }
  }

  if (maybe_result.is_null()) {
    // No cache entry found, finalize compilation of the script and add it to
    // the isolate cache.
    RCS_SCOPE(isolate,
              RuntimeCallCounterId::kCompilePublishBackgroundFinalization);
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.OffThreadFinalization.Publish");

    maybe_result = task->FinalizeScript(isolate, source, script_details,
                                        maybe_cached_script);

    Handle<SharedFunctionInfo> result;
    if (maybe_result.ToHandle(&result)) {
      if (task->flags().produce_compile_hints()) {
        Cast<Script>(result->script())->set_produce_compile_hints(true);
      }

      // Add compiled code to the isolate cache.
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.StreamingFinalization.AddToCache");
      compilation_cache->PutScript(source, task->flags().outer_language_mode(),
                                   result);
    }
  }

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.StreamingFinalization.Release");
  streaming_data->Release();
  return maybe_result;
}  // namespace internal

// static
template <typename IsolateT>
Handle<SharedFunctionInfo> Compiler::GetSharedFunctionInfo(
    FunctionLiteral* literal, Handle<Script> script, IsolateT* isolate) {
  // If we're parallel compiling functions, we might already have attached a SFI
  // to this literal.
  if (!literal->shared_function_info().is_null()) {
    return literal->shared_function_info();
  }
  // Precondition: code has been parsed and scopes have been analyzed.
  MaybeHandle<SharedFunctionInfo> maybe_existing;

  // Find any previously allocated shared function info for the given literal.
  maybe_existing = Script::FindSharedFunctionInfo(script, isolate, literal);

  // If we found an existing shared function info, return it.
  Handle<SharedFunctionInfo> existing;
  if (maybe_existing.ToHandle(&existing)) {
    // If the function has been uncompiled (bytecode flushed) it will have lost
    // any preparsed data. If we produced preparsed data during this compile for
    // this function, replace the uncompiled data with one that includes it.
    if (literal->produced_preparse_data() != nullptr &&
        existing->HasUncompiledDataWithoutPreparseData()) {
      DirectHandle<UncompiledData> existing_uncompiled_data(
          existing->uncompiled_data(isolate), isolate);
      DCHECK_EQ(literal->start_position(),
                existing_uncompiled_data->start_position());
      DCHECK_EQ(literal->end_position(),
                existing_uncompiled_data->end_position());
      // Use existing uncompiled data's inferred name as it may be more
      // accurate than the literal we preparsed.
      Handle<String> inferred_name =
          handle(existing_uncompiled_data->inferred_name(), isolate);
      Handle<PreparseData> preparse_data =
          literal->produced_preparse_data()->Serialize(isolate);
      DirectHandle<UncompiledData> new_uncompiled_data =
          isolate->factory()->NewUncompiledDataWithPreparseData(
              inferred_name, existing_uncompiled_data->start_position(),
              existing_uncompiled_data->end_position(), preparse_data);
      existing->set_uncompiled_data(*new_uncompiled_data);
    }
    return existing;
  }

  // Allocate a shared function info object which will be compiled lazily.
  Handle<SharedFunctionInfo> result =
      isolate->factory()->NewSharedFunctionInfoForLiteral(literal, script,
                                                          false);
  return result;
}

template Handle<SharedFunctionInfo> Compiler::GetSharedFunctionInfo(
    FunctionLiteral* literal, Handle<Script> script, Isolate* isolate);
template Handle<SharedFunctionInfo> Compiler::GetSharedFunctionInfo(
    FunctionLiteral* literal, Handle<Script> script, LocalIsolate* isolate);

// static
MaybeHandle<Code> Compiler::CompileOptimizedOSR(Isolate* isolate,
                                                Handle<JSFunction> function,
                                                BytecodeOffset osr_offset,
                                                ConcurrencyMode mode,
                                                CodeKind code_kind) {
  DCHECK(IsOSR(osr_offset));

  if (V8_UNLIKELY(isolate->serializer_enabled())) return {};
  if (V8_UNLIKELY(function->shared()->optimization_disabled())) return {};

  // TODO(chromium:1031479): Currently, OSR triggering mechanism is tied to the
  // bytecode array. So, it might be possible to mark closure in one native
  // context and optimize a closure from a different native context. So check if
  // there is a feedback vector before OSRing. We don't expect this to happen
  // often.
  if (V8_UNLIKELY(!function->has_feedback_vector())) return {};

  CompilerTracer::TraceOptimizeOSRStarted(isolate, function, osr_offset, mode);
  MaybeHandle<Code> result =
      GetOrCompileOptimized(isolate, function, mode, code_kind, osr_offset);

  if (result.is_null()) {
    CompilerTracer::TraceOptimizeOSRUnavailable(isolate, function, osr_offset,
                                                mode);
  } else {
    DCHECK_GE(result.ToHandleChecked()->kind(), CodeKind::MAGLEV);
    CompilerTracer::TraceOptimizeOSRAvailable(isolate, function, osr_offset,
                                              mode);
  }

  return result;
}

// static
void Compiler::DisposeTurbofanCompilationJob(Isolate* isolate,
                                             TurbofanCompilationJob* job,
                                             bool restore_function_code) {
  DirectHandle<JSFunction> function = job->compilation_info()->closure();
  ResetTieringState(isolate, *function, job->compilation_info()->osr_offset());
  if (restore_function_code) {
    function->set_code(function->shared()->GetCode(isolate));
  }
}

// static
void Compiler::FinalizeTurbofanCompilationJob(TurbofanCompilationJob* job,
                                              Isolate* isolate) {
  VMState<COMPILER> state(isolate);
  OptimizedCompilationInfo* compilation_info = job->compilation_info();

  TimerEventScope<TimerEventRecompileSynchronous> timer(isolate);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeConcurrentFinalize);
  TRACE_EVENT_WITH_FLOW0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                         "V8.OptimizeConcurrentFinalize", job->trace_id(),
                         TRACE_EVENT_FLAG_FLOW_IN);

  DirectHandle<JSFunction> function = compilation_info->closure();
  DirectHandle<SharedFunctionInfo> shared = compilation_info->shared_info();

  const bool use_result = !compilation_info->discard_result_for_testing();
  const BytecodeOffset osr_offset = compilation_info->osr_offset();

  DCHECK(!shared->HasBreakInfo(isolate));

  // 1) Optimization on the concurrent thread may have failed.
  // 2) The function may have already been optimized by OSR.  Simply continue.
  //    Except when OSR already disabled optimization for some reason.
  // 3) The code may have already been invalidated due to dependency change.
  // 4) InstructionStream generation may have failed.
  if (job->state() == CompilationJob::State::kReadyToFinalize) {
    if (shared->optimization_disabled()) {
      job->RetryOptimization(BailoutReason::kOptimizationDisabled);
    } else if (job->FinalizeJob(isolate) == CompilationJob::SUCCEEDED) {
      job->RecordCompilationStats(ConcurrencyMode::kConcurrent, isolate);
      job->RecordFunctionCompilation(LogEventListener::CodeTag::kFunction,
                                     isolate);
      if (V8_LIKELY(use_result)) {
        ResetTieringState(isolate, *function, osr_offset);
        OptimizedCodeCache::Insert(
            isolate, *compilation_info->closure(),
            compilation_info->osr_offset(), *compilation_info->code(),
            compilation_info->function_context_specializing());
        CompilerTracer::TraceCompletedJob(isolate, compilation_info);
        if (IsOSR(osr_offset)) {
          CompilerTracer::TraceOptimizeOSRFinished(isolate, function,
                                                   osr_offset);
        } else {
          function->set_code(*compilation_info->code());
        }
      }
      return;
    }
  }

  DCHECK_EQ(job->state(), CompilationJob::State::kFailed);
  CompilerTracer::TraceAbortedJob(isolate, compilation_info,
                                  job->prepare_in_ms(), job->execute_in_ms(),
                                  job->finalize_in_ms());
  if (V8_LIKELY(use_result)) {
    ResetTieringState(isolate, *function, osr_offset);
    if (!IsOSR(osr_offset)) {
      function->set_code(shared->GetCode(isolate));
    }
  }
}

// static
void Compiler::FinalizeMaglevCompilationJob(maglev::MaglevCompilationJob* job,
                                            Isolate* isolate) {
#ifdef V8_ENABLE_MAGLEV
  VMState<COMPILER> state(isolate);

  DirectHandle<JSFunction> function = job->function();
  if (function->ActiveTierIsTurbofan(isolate) && !job->is_osr()) {
    CompilerTracer::TraceAbortedMaglevCompile(
        isolate, function, BailoutReason::kHigherTierAvailable);
    return;
  }

  const CompilationJob::Status status = job->FinalizeJob(isolate);

  // TODO(v8:7700): Use the result and check if job succeed
  // when all the bytecodes are implemented.
  USE(status);

  BytecodeOffset osr_offset = job->osr_offset();
  ResetTieringState(isolate, *function, osr_offset);

  if (status == CompilationJob::SUCCEEDED) {
    DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate);
    DCHECK(!shared->HasBreakInfo(isolate));

    // Note the finalized InstructionStream object has already been installed on
    // the function by MaglevCompilationJob::FinalizeJobImpl.

    Handle<Code> code = job->code().ToHandleChecked();
    if (!job->is_osr()) {
      job->function()->set_code(*code);
    }

    DCHECK(code->is_maglevved());
    OptimizedCodeCache::Insert(isolate, *function, osr_offset, *code,
                               job->specialize_to_function_context());

    RecordMaglevFunctionCompilation(isolate, function,
                                    Cast<AbstractCode>(code));
    job->RecordCompilationStats(isolate);
    if (v8_flags.profile_guided_optimization &&
        shared->cached_tiering_decision() == CachedTieringDecision::kPending) {
      shared->set_cached_tiering_decision(CachedTieringDecision::kEarlyMaglev);
    }
    CompilerTracer::TraceFinishMaglevCompile(
        isolate, function, job->is_osr(), job->prepare_in_ms(),
        job->execute_in_ms(), job->finalize_in_ms());
  }
#endif
}

// static
void Compiler::PostInstantiation(Isolate* isolate,
                                 DirectHandle<JSFunction> function,
                                 IsCompiledScope* is_compiled_scope) {
  DirectHandle<SharedFunctionInfo> shared(function->shared(), isolate);

  // If code is compiled to bytecode (i.e., isn't asm.js), then allocate a
  // feedback and check for optimized code.
  if (is_compiled_scope->is_compiled() && shared->HasBytecodeArray()) {
    // Don't reset budget if there is a closure feedback cell array already. We
    // are just creating a new closure that shares the same feedback cell.
    JSFunction::InitializeFeedbackCell(function, is_compiled_scope, false);

    if (function->has_feedback_vector()) {
      // Evict any deoptimized code on feedback vector. We need to do this after
      // creating the closure, since any heap allocations could trigger a GC and
      // deoptimized the code on the feedback vector. So check for any
      // deoptimized code just before installing it on the funciton.
      function->feedback_vector()->EvictOptimizedCodeMarkedForDeoptimization(
          isolate, *shared, "new function from shared function info");
      Tagged<Code> code = function->feedback_vector()->optimized_code(isolate);
      if (!code.is_null()) {
        // Caching of optimized code enabled and optimized code found.
        DCHECK(!code->marked_for_deoptimization());
        DCHECK(function->shared()->is_compiled());

        function->set_code(code);
      }
    }

    if (v8_flags.always_turbofan && shared->allows_lazy_compilation() &&
        !shared->optimization_disabled() &&
        !function->HasAvailableOptimizedCode(isolate)) {
      CompilerTracer::TraceMarkForAlwaysOpt(isolate, function);
      JSFunction::EnsureFeedbackVector(isolate, function, is_compiled_scope);
      function->MarkForOptimization(isolate, CodeKind::TURBOFAN,
                                    ConcurrencyMode::kSynchronous);
    }
  }

  if (shared->is_toplevel() || shared->is_wrapped()) {
    // If it's a top-level script, report compilation to the debugger.
    DirectHandle<Script> script(Cast<Script>(shared->script()), isolate);
    isolate->debug()->OnAfterCompile(script);
    TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown"),
                 "ScriptCompiled", "data",
                 AddScriptCompiledTrace(isolate, shared));
    bool tracing_enabled;
    TRACE_EVENT_CATEGORY_GROUP_ENABLED(
        TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown-sources"),
        &tracing_enabled);
    if (tracing_enabled) {
      EmitScriptSourceTextTrace(isolate, shared);
    }
  }
}

std::unique_ptr<v8::tracing::TracedValue> Compiler::AddScriptCompiledTrace(
    Isolate* isolate, DirectHandle<SharedFunctionInfo> shared) {
  DirectHandle<Script> script(Cast<Script>(shared->script()), isolate);
  i::Tagged<i::Object> context_value =
      isolate->native_context()->debug_context_id();
  int contextId = (IsSmi(context_value)) ? i::Smi::ToInt(context_value) : 0;
  Script::InitLineEnds(isolate, script);
  Script::PositionInfo endInfo;
  Script::GetPositionInfo(
      script, i::Cast<i::String>(script->source())->length(), &endInfo);
  Script::PositionInfo startInfo;
  Script::GetPositionInfo(script, shared->StartPosition(), &startInfo);
  auto value = v8::tracing::TracedValue::Create();
  value->SetString("isolate",
                   std::to_string(reinterpret_cast<size_t>(isolate)));
  value->SetInteger("executionContextId", contextId);
  value->SetInteger("scriptId", script->id());
  value->SetInteger("startLine", startInfo.line);
  value->SetInteger("startColumn", startInfo.column);
  value->SetInteger("endLine", endInfo.line);
  value->SetInteger("endColumn", endInfo.column);
  value->SetBoolean("isModule", script->origin_options().IsModule());
  value->SetBoolean("hasSourceUrl", script->HasValidSource());
  if (script->HasValidSource() && IsString(script->GetNameOrSourceURL())) {
    value->SetString(
        "sourceMapUrl",
        i::Cast<i::String>(script->GetNameOrSourceURL())->ToCString().get());
  }
  if (IsString(script->name())) {
    value->SetString("url",
                     i::Cast<i::String>(script->name())->ToCString().get());
  }
  value->SetString("hash",
                   i::Script::GetScriptHash(isolate, script,
                                            /* forceForInspector: */ false)
                       ->ToCString()
                       .get());
  return value;
}

void Compiler::EmitScriptSourceTextTrace(
    Isolate* isolate, DirectHandle<SharedFunctionInfo> shared) {
  DirectHandle<Script> script(Cast<Script>(shared->script()), isolate);
  if (IsString(script->source())) {
    Tagged<String> source = i::Cast<i::String>(script->source());
    auto script_id = script->id();
    auto isolate_string = std::to_string(reinterpret_cast<size_t>(isolate));
    int32_t source_length = source->length();
    const int32_t kSplitMaxLength = 1000000;
    if (source_length <= kSplitMaxLength) {
      auto value = v8::tracing::TracedValue::Create();
      value->SetString("isolate", isolate_string);
      value->SetInteger("scriptId", script_id);
      value->SetInteger("length", source_length);
      value->SetString("sourceText", source->ToCString().get());
      TRACE_EVENT1(
          TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown-sources"),
          "ScriptCompiled", "data", std::move(value));
    } else {
      Handle<String> handle_source(source, isolate);
      int32_t split_count = source_length / kSplitMaxLength + 1;
      for (int32_t i = 0; i < split_count; i++) {
        int32_t begin = i * kSplitMaxLength;
        int32_t end = std::min(begin + kSplitMaxLength, source_length);
        DirectHandle<String> partial_source =
            isolate->factory()->NewSubString(handle_source, begin, end);
        auto split_trace_value = v8::tracing::TracedValue::Create();
        split_trace_value->SetInteger("splitIndex", i);
        split_trace_value->SetInteger("splitCount", split_count);
        split_trace_value->SetString("isolate", isolate_string);
        split_trace_value->SetInteger("scriptId", script_id);
        split_trace_value->SetString("sourceText",
                                     partial_source->ToCString().get());
        TRACE_EVENT1(
            TRACE_DISABLED_BY_DEFAULT("devtools.v8-source-rundown-sources"),
            "LargeScriptCompiledSplits", "data", std::move(split_trace_value));
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Implementation of ScriptStreamingData

ScriptStreamingData::ScriptStreamingData(
    std::unique_ptr<ScriptCompiler::ExternalSourceStream> source_stream,
    ScriptCompiler::StreamedSource::Encoding encoding)
    : source_stream(std::move(source_stream)), encoding(encoding) {}

ScriptStreamingData::~ScriptStreamingData() = default;

void ScriptStreamingData::Release() { task.reset(); }

}  // namespace internal
}  // namespace v8
