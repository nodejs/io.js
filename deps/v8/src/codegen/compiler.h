// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_COMPILER_H_
#define V8_CODEGEN_COMPILER_H_

#include <forward_list>
#include <memory>

#include "src/ast/ast-value-factory.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/small-vector.h"
#include "src/codegen/background-merge-task.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/persistent-handles.h"
#include "src/logging/code-events.h"
#include "src/objects/contexts.h"
#include "src/objects/debug-objects.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "src/snapshot/code-serializer.h"
#include "src/utils/allocation.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

// Forward declarations.
class AlignedCachedData;
class BackgroundCompileTask;
class IsCompiledScope;
class OptimizedCompilationInfo;
class ParseInfo;
class RuntimeCallStats;
class TimedHistogram;
class TurbofanCompilationJob;
class UnoptimizedCompilationInfo;
class UnoptimizedCompilationJob;
class UnoptimizedFrame;
class WorkerThreadRuntimeCallStats;
struct ScriptDetails;
struct ScriptStreamingData;

namespace maglev {
class MaglevCompilationJob;

static inline bool IsMaglevEnabled() { return v8_flags.maglev; }

static inline bool IsMaglevOsrEnabled() {
  return IsMaglevEnabled() && v8_flags.maglev_osr;
}

}  // namespace maglev

// The V8 compiler API.
//
// This is the central hub for dispatching to the various compilers within V8.
// Logic for which compiler to choose and how to wire compilation results into
// the object heap should be kept inside this class.
//
// General strategy: Scripts are translated into anonymous functions w/o
// parameters which then can be executed. If the source code contains other
// functions, they might be compiled and allocated as part of the compilation
// of the source code or deferred for lazy compilation at a later point.
class V8_EXPORT_PRIVATE Compiler : public AllStatic {
 public:
  enum ClearExceptionFlag { KEEP_EXCEPTION, CLEAR_EXCEPTION };

  // ===========================================================================
  // The following family of methods ensures a given function is compiled. The
  // general contract is that failures will be reported by returning {false},
  // whereas successful compilation ensures the {is_compiled} predicate on the
  // given function holds (except for live-edit, which compiles the world).

  static bool Compile(Isolate* isolate, Handle<SharedFunctionInfo> shared,
                      ClearExceptionFlag flag,
                      IsCompiledScope* is_compiled_scope,
                      CreateSourcePositions create_source_positions_flag =
                          CreateSourcePositions::kNo);
  static bool Compile(Isolate* isolate, Handle<JSFunction> function,
                      ClearExceptionFlag flag,
                      IsCompiledScope* is_compiled_scope);
  static MaybeHandle<SharedFunctionInfo> CompileToplevel(
      ParseInfo* parse_info, Handle<Script> script, Isolate* isolate,
      IsCompiledScope* is_compiled_scope);

  static bool CompileSharedWithBaseline(Isolate* isolate,
                                        Handle<SharedFunctionInfo> shared,
                                        ClearExceptionFlag flag,
                                        IsCompiledScope* is_compiled_scope);
  static bool CompileBaseline(Isolate* isolate, Handle<JSFunction> function,
                              ClearExceptionFlag flag,
                              IsCompiledScope* is_compiled_scope);

  static void CompileOptimized(Isolate* isolate, Handle<JSFunction> function,
                               ConcurrencyMode mode, CodeKind code_kind);

  // Generate and return optimized code for OSR. The empty handle is returned
  // either on failure, or after spawning a concurrent OSR task (in which case
  // a future OSR request will pick up the resulting code object).
  V8_WARN_UNUSED_RESULT static MaybeHandle<Code> CompileOptimizedOSR(
      Isolate* isolate, Handle<JSFunction> function, BytecodeOffset osr_offset,
      ConcurrencyMode mode, CodeKind code_kind);

  V8_WARN_UNUSED_RESULT static MaybeHandle<SharedFunctionInfo>
  CompileForLiveEdit(ParseInfo* parse_info, Handle<Script> script,
                     MaybeHandle<ScopeInfo> outer_scope_info, Isolate* isolate);

  // Collect source positions for a function that has already been compiled to
  // bytecode, but for which source positions were not collected (e.g. because
  // they were not immediately needed).
  static bool CollectSourcePositions(Isolate* isolate,
                                     Handle<SharedFunctionInfo> shared);

  // Finalize and install code from previously run background compile task.
  static bool FinalizeBackgroundCompileTask(BackgroundCompileTask* task,
                                            Isolate* isolate,
                                            ClearExceptionFlag flag);

  // Dispose a job without finalization.
  static void DisposeTurbofanCompilationJob(Isolate* isolate,
                                            TurbofanCompilationJob* job,
                                            bool restore_function_code);

  // Finalize and install Turbofan code from a previously run job.
  static void FinalizeTurbofanCompilationJob(TurbofanCompilationJob* job,
                                             Isolate* isolate);

  // Finalize and install Maglev code from a previously run job.
  static void FinalizeMaglevCompilationJob(maglev::MaglevCompilationJob* job,
                                           Isolate* isolate);

  // Give the compiler a chance to perform low-latency initialization tasks of
  // the given {function} on its instantiation. Note that only the runtime will
  // offer this chance, optimized closure instantiation will not call this.
  static void PostInstantiation(Handle<JSFunction> function,
                                IsCompiledScope* is_compiled_scope);

  // ===========================================================================
  // The following family of methods instantiates new functions for scripts or
  // function literals. The decision whether those functions will be compiled,
  // is left to the discretion of the compiler.
  //
  // Please note this interface returns shared function infos.  This means you
  // need to call Factory::NewFunctionFromSharedFunctionInfo before you have a
  // real function with a context.

  // Create a (bound) function for a String source within a context for eval.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> GetFunctionFromEval(
      Handle<String> source, Handle<SharedFunctionInfo> outer_info,
      Handle<Context> context, LanguageMode language_mode,
      ParseRestriction restriction, int parameters_end_pos,
      int eval_scope_position, int eval_position,
      ParsingWhileDebugging parsing_while_debugging =
          ParsingWhileDebugging::kNo);

  // Create a function that results from wrapping |source| in a function,
  // with |arguments| being a list of parameters for that function.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> GetWrappedFunction(
      Handle<String> source, Handle<Context> context,
      const ScriptDetails& script_details, AlignedCachedData* cached_data,
      v8::ScriptCompiler::CompileOptions compile_options,
      v8::ScriptCompiler::NoCacheReason no_cache_reason);

  // Create a (bound) function for a String source within a context for eval.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> GetFunctionFromString(
      Handle<NativeContext> context, Handle<i::Object> source,
      ParseRestriction restriction, int parameters_end_pos, bool is_code_like);

  // Decompose GetFunctionFromString into two functions, to allow callers to
  // deal seperately with a case of object not handled by the embedder.
  V8_WARN_UNUSED_RESULT static std::pair<MaybeHandle<String>, bool>
  ValidateDynamicCompilationSource(Isolate* isolate,
                                   Handle<NativeContext> context,
                                   Handle<i::Object> source_object,
                                   bool is_code_like = false);
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction>
  GetFunctionFromValidatedString(Handle<NativeContext> context,
                                 MaybeHandle<String> source,
                                 ParseRestriction restriction,
                                 int parameters_end_pos);

  // Create a shared function info object for a String source.
  static MaybeHandle<SharedFunctionInfo> GetSharedFunctionInfoForScript(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details,
      ScriptCompiler::CompileOptions compile_options,
      ScriptCompiler::NoCacheReason no_cache_reason,
      NativesFlag is_natives_code,
      ScriptCompiler::CompilationDetails* compilation_details);

  // Create a shared function info object for a String source.
  static MaybeHandle<SharedFunctionInfo>
  GetSharedFunctionInfoForScriptWithExtension(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details, v8::Extension* extension,
      ScriptCompiler::CompileOptions compile_options,
      NativesFlag is_natives_code,
      ScriptCompiler::CompilationDetails* compilation_details);

  // Create a shared function info object for a String source and serialized
  // cached data. The cached data may be rejected, in which case this function
  // will set cached_data->rejected() to true.
  static MaybeHandle<SharedFunctionInfo>
  GetSharedFunctionInfoForScriptWithCachedData(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details, AlignedCachedData* cached_data,
      ScriptCompiler::CompileOptions compile_options,
      ScriptCompiler::NoCacheReason no_cache_reason,
      NativesFlag is_natives_code,
      ScriptCompiler::CompilationDetails* compilation_details);

  // Create a shared function info object for a String source and a task that
  // has deserialized cached data on a background thread. The cached data from
  // the task may be rejected, in which case this function will set
  // deserialize_task->rejected() to true.
  static MaybeHandle<SharedFunctionInfo>
  GetSharedFunctionInfoForScriptWithDeserializeTask(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details,
      BackgroundDeserializeTask* deserialize_task,
      ScriptCompiler::CompileOptions compile_options,
      ScriptCompiler::NoCacheReason no_cache_reason,
      NativesFlag is_natives_code,
      ScriptCompiler::CompilationDetails* compilation_details);

  static MaybeHandle<SharedFunctionInfo>
  GetSharedFunctionInfoForScriptWithCompileHints(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details,
      v8::CompileHintCallback compile_hint_callback,
      void* compile_hint_callback_data,
      ScriptCompiler::CompileOptions compile_options,
      ScriptCompiler::NoCacheReason no_cache_reason,
      NativesFlag is_natives_code,
      ScriptCompiler::CompilationDetails* compilation_details);

  // Create a shared function info object for a Script source that has already
  // been parsed and possibly compiled on a background thread while being loaded
  // from a streamed source. On return, the data held by |streaming_data| will
  // have been released, however the object itself isn't freed and is still
  // owned by the caller.
  static MaybeHandle<SharedFunctionInfo> GetSharedFunctionInfoForStreamedScript(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details, ScriptStreamingData* streaming_data,
      ScriptCompiler::CompilationDetails* compilation_details);

  // Create a shared function info object for the given function literal
  // node (the code may be lazily compiled).
  template <typename IsolateT>
  static Handle<SharedFunctionInfo> GetSharedFunctionInfo(FunctionLiteral* node,
                                                          Handle<Script> script,
                                                          IsolateT* isolate);

  static void LogFunctionCompilation(Isolate* isolate,
                                     LogEventListener::CodeTag code_type,
                                     Handle<Script> script,
                                     Handle<SharedFunctionInfo> shared,
                                     Handle<FeedbackVector> vector,
                                     Handle<AbstractCode> abstract_code,
                                     CodeKind kind, double time_taken_ms);

  static void InstallInterpreterTrampolineCopy(
      Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
      LogEventListener::CodeTag log_tag);
};

// A base class for compilation jobs intended to run concurrent to the main
// thread. The current state of the job can be checked using {state()}.
class V8_EXPORT_PRIVATE CompilationJob {
 public:
  enum Status { SUCCEEDED, FAILED, RETRY_ON_MAIN_THREAD };
  enum class State {
    kReadyToPrepare,
    kReadyToExecute,
    kReadyToFinalize,
    kSucceeded,
    kFailed,
  };

  explicit CompilationJob(State initial_state) : state_(initial_state) {}
  virtual ~CompilationJob() = default;

  State state() const { return state_; }

 protected:
  V8_WARN_UNUSED_RESULT Status UpdateState(Status status, State next_state) {
    switch (status) {
      case SUCCEEDED:
        state_ = next_state;
        break;
      case FAILED:
        state_ = State::kFailed;
        break;
      case RETRY_ON_MAIN_THREAD:
        // Don't change the state, we'll re-try on the main thread.
        break;
    }
    return status;
  }

 private:
  State state_;
};

// A base class for unoptimized compilation jobs.
//
// The job is split into two phases which are called in sequence on
// different threads and with different limitations:
//  1) ExecuteJob:   Runs concurrently. No heap allocation or handle derefs.
//  2) FinalizeJob:  Runs on main thread. No dependency changes.
//
// Either of phases can either fail or succeed.
class UnoptimizedCompilationJob : public CompilationJob {
 public:
  UnoptimizedCompilationJob(uintptr_t stack_limit, ParseInfo* parse_info,
                            UnoptimizedCompilationInfo* compilation_info)
      : CompilationJob(State::kReadyToExecute),
        stack_limit_(stack_limit),
        parse_info_(parse_info),
        compilation_info_(compilation_info) {}

  // Executes the compile job. Can be called on a background thread.
  V8_WARN_UNUSED_RESULT Status ExecuteJob();

  // Finalizes the compile job. Must be called on the main thread.
  V8_WARN_UNUSED_RESULT Status
  FinalizeJob(Handle<SharedFunctionInfo> shared_info, Isolate* isolate);

  // Finalizes the compile job. Can be called on a background thread, and might
  // return RETRY_ON_MAIN_THREAD if the finalization can't be run on the
  // background thread, and should instead be retried on the foreground thread.
  V8_WARN_UNUSED_RESULT Status
  FinalizeJob(Handle<SharedFunctionInfo> shared_info, LocalIsolate* isolate);

  void RecordCompilationStats(Isolate* isolate) const;
  void RecordFunctionCompilation(LogEventListener::CodeTag code_type,
                                 Handle<SharedFunctionInfo> shared,
                                 Isolate* isolate) const;

  ParseInfo* parse_info() const {
    DCHECK_NOT_NULL(parse_info_);
    return parse_info_;
  }
  UnoptimizedCompilationInfo* compilation_info() const {
    return compilation_info_;
  }

  uintptr_t stack_limit() const { return stack_limit_; }

  base::TimeDelta time_taken_to_execute() const {
    return time_taken_to_execute_;
  }
  base::TimeDelta time_taken_to_finalize() const {
    return time_taken_to_finalize_;
  }

  void ClearParseInfo() { parse_info_ = nullptr; }

 protected:
  // Overridden by the actual implementation.
  virtual Status ExecuteJobImpl() = 0;
  virtual Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                                 Isolate* isolate) = 0;
  virtual Status FinalizeJobImpl(Handle<SharedFunctionInfo> shared_info,
                                 LocalIsolate* isolate) = 0;

 private:
  uintptr_t stack_limit_;
  ParseInfo* parse_info_;
  UnoptimizedCompilationInfo* compilation_info_;
  base::TimeDelta time_taken_to_execute_;
  base::TimeDelta time_taken_to_finalize_;
};

// A base class for optimized compilation jobs.
//
// The job is split into three phases which are called in sequence on
// different threads and with different limitations:
//  1) PrepareJob:   Runs on main thread. No major limitations.
//  2) ExecuteJob:   Runs concurrently. No heap allocation or handle derefs.
//  3) FinalizeJob:  Runs on main thread. No dependency changes.
//
// Each of the three phases can either fail or succeed.
class OptimizedCompilationJob : public CompilationJob {
 public:
  OptimizedCompilationJob(const char* compiler_name, State initial_state)
      : CompilationJob(initial_state), compiler_name_(compiler_name) {
    timer_.Start();
  }

  // Prepare the compile job. Must be called on the main thread.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Status PrepareJob(Isolate* isolate);

  // Executes the compile job. Can be called on a background thread.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Status
  ExecuteJob(RuntimeCallStats* stats, LocalIsolate* local_isolate = nullptr);

  // Finalizes the compile job. Must be called on the main thread.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Status FinalizeJob(Isolate* isolate);

  const char* compiler_name() const { return compiler_name_; }

  double prepare_in_ms() const {
    return time_taken_to_prepare_.InMillisecondsF();
  }
  double execute_in_ms() const {
    return time_taken_to_execute_.InMillisecondsF();
  }
  double finalize_in_ms() const {
    return time_taken_to_finalize_.InMillisecondsF();
  }

  V8_WARN_UNUSED_RESULT base::TimeDelta ElapsedTime() const {
    return timer_.Elapsed();
  }

 protected:
  // Overridden by the actual implementation.
  virtual Status PrepareJobImpl(Isolate* isolate) = 0;
  virtual Status ExecuteJobImpl(RuntimeCallStats* stats,
                                LocalIsolate* local_heap) = 0;
  virtual Status FinalizeJobImpl(Isolate* isolate) = 0;

  // Register weak object to optimized code dependencies.
  GlobalHandleVector<Map> CollectRetainedMaps(Isolate* isolate,
                                              Handle<Code> code);
  void RegisterWeakObjectsInOptimizedCode(Isolate* isolate,
                                          Handle<NativeContext> context,
                                          Handle<Code> code,
                                          GlobalHandleVector<Map> maps);

  base::TimeDelta time_taken_to_prepare_;
  base::TimeDelta time_taken_to_execute_;
  base::TimeDelta time_taken_to_finalize_;

  base::ElapsedTimer timer_;

 private:
  const char* const compiler_name_;
};

// Thin wrapper to split off Turbofan-specific parts.
class TurbofanCompilationJob : public OptimizedCompilationJob {
 public:
  TurbofanCompilationJob(OptimizedCompilationInfo* compilation_info,
                         State initial_state)
      : OptimizedCompilationJob("Turbofan", initial_state),
        compilation_info_(compilation_info) {}

  OptimizedCompilationInfo* compilation_info() const {
    return compilation_info_;
  }

  // Report a transient failure, try again next time. Should only be called on
  // optimization compilation jobs.
  Status RetryOptimization(BailoutReason reason);

  // Report a persistent failure, disable future optimization on the function.
  // Should only be called on optimization compilation jobs.
  Status AbortOptimization(BailoutReason reason);

  void RecordCompilationStats(ConcurrencyMode mode, Isolate* isolate) const;
  void RecordFunctionCompilation(LogEventListener::CodeTag code_type,
                                 Isolate* isolate) const;

  // Intended for use as a globally unique id in trace events.
  uint64_t trace_id() const;

 private:
  OptimizedCompilationInfo* const compilation_info_;
};

class FinalizeUnoptimizedCompilationData {
 public:
  FinalizeUnoptimizedCompilationData(Isolate* isolate,
                                     Handle<SharedFunctionInfo> function_handle,
                                     MaybeHandle<CoverageInfo> coverage_info,
                                     base::TimeDelta time_taken_to_execute,
                                     base::TimeDelta time_taken_to_finalize)
      : time_taken_to_execute_(time_taken_to_execute),
        time_taken_to_finalize_(time_taken_to_finalize),
        function_handle_(function_handle),
        coverage_info_(coverage_info) {}

  FinalizeUnoptimizedCompilationData(LocalIsolate* isolate,
                                     Handle<SharedFunctionInfo> function_handle,
                                     MaybeHandle<CoverageInfo> coverage_info,
                                     base::TimeDelta time_taken_to_execute,
                                     base::TimeDelta time_taken_to_finalize);

  Handle<SharedFunctionInfo> function_handle() const {
    return function_handle_;
  }

  MaybeHandle<CoverageInfo> coverage_info() const { return coverage_info_; }

  base::TimeDelta time_taken_to_execute() const {
    return time_taken_to_execute_;
  }
  base::TimeDelta time_taken_to_finalize() const {
    return time_taken_to_finalize_;
  }

 private:
  base::TimeDelta time_taken_to_execute_;
  base::TimeDelta time_taken_to_finalize_;
  Handle<SharedFunctionInfo> function_handle_;
  MaybeHandle<CoverageInfo> coverage_info_;
};

using FinalizeUnoptimizedCompilationDataList =
    std::vector<FinalizeUnoptimizedCompilationData>;

class DeferredFinalizationJobData {
 public:
  DeferredFinalizationJobData(Isolate* isolate,
                              Handle<SharedFunctionInfo> function_handle,
                              std::unique_ptr<UnoptimizedCompilationJob> job) {
    UNREACHABLE();
  }
  DeferredFinalizationJobData(LocalIsolate* isolate,
                              Handle<SharedFunctionInfo> function_handle,
                              std::unique_ptr<UnoptimizedCompilationJob> job);

  Handle<SharedFunctionInfo> function_handle() const {
    return function_handle_;
  }

  UnoptimizedCompilationJob* job() const { return job_.get(); }

 private:
  Handle<SharedFunctionInfo> function_handle_;
  std::unique_ptr<UnoptimizedCompilationJob> job_;
};

// A wrapper around a OptimizedCompilationInfo that detaches the Handles from
// the underlying PersistentHandlesScope and stores them in info_ on
// destruction.
class V8_NODISCARD CompilationHandleScope final {
 public:
  explicit CompilationHandleScope(Isolate* isolate,
                                  OptimizedCompilationInfo* info)
      : persistent_(isolate), info_(info) {}
  V8_EXPORT_PRIVATE ~CompilationHandleScope();

 private:
  PersistentHandlesScope persistent_;
  OptimizedCompilationInfo* info_;
};

using DeferredFinalizationJobDataList =
    std::vector<DeferredFinalizationJobData>;

class V8_EXPORT_PRIVATE BackgroundCompileTask {
 public:
  // Creates a new task that when run will parse and compile the streamed
  // script associated with |data| and can be finalized with FinalizeScript.
  // Note: does not take ownership of |data|.
  BackgroundCompileTask(ScriptStreamingData* data, Isolate* isolate,
                        v8::ScriptType type,
                        ScriptCompiler::CompileOptions options,
                        ScriptCompiler::CompilationDetails* compilation_details,
                        CompileHintCallback compile_hint_callback = nullptr,
                        void* compile_hint_callback_data = nullptr);
  BackgroundCompileTask(const BackgroundCompileTask&) = delete;
  BackgroundCompileTask& operator=(const BackgroundCompileTask&) = delete;
  ~BackgroundCompileTask();

  // Creates a new task that when run will parse and compile the non-top-level
  // |shared_info| and can be finalized with FinalizeFunction in
  // Compiler::FinalizeBackgroundCompileTask.
  BackgroundCompileTask(
      Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
      std::unique_ptr<Utf16CharacterStream> character_stream,
      WorkerThreadRuntimeCallStats* worker_thread_runtime_stats,
      TimedHistogram* timer, int max_stack_size);

  void Run();
  void RunOnMainThread(Isolate* isolate);
  void Run(LocalIsolate* isolate,
           ReusableUnoptimizedCompileState* reusable_state);

  MaybeHandle<SharedFunctionInfo> FinalizeScript(
      Isolate* isolate, Handle<String> source,
      const ScriptDetails& script_details,
      MaybeHandle<Script> maybe_cached_script);

  bool FinalizeFunction(Isolate* isolate, Compiler::ClearExceptionFlag flag);

  void AbortFunction();

  UnoptimizedCompileFlags flags() const { return flags_; }

 private:
  void ReportStatistics(Isolate* isolate);

  void ClearFunctionJobPointer();

  bool is_streaming_compilation() const;

  // Data needed for parsing and compilation. These need to be initialized
  // before the compilation starts.
  Isolate* isolate_for_local_isolate_;
  UnoptimizedCompileFlags flags_;
  UnoptimizedCompileState compile_state_;
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  int stack_size_;
  WorkerThreadRuntimeCallStats* worker_thread_runtime_call_stats_;
  TimedHistogram* timer_;
  ScriptCompiler::CompilationDetails* compilation_details_;

  // Data needed for merging onto the main thread after background finalization.
  std::unique_ptr<PersistentHandles> persistent_handles_;
  MaybeHandle<SharedFunctionInfo> outer_function_sfi_;
  Handle<Script> script_;
  IsCompiledScope is_compiled_scope_;
  FinalizeUnoptimizedCompilationDataList finalize_unoptimized_compilation_data_;
  DeferredFinalizationJobDataList jobs_to_retry_finalization_on_main_thread_;
  base::SmallVector<v8::Isolate::UseCounterFeature, 8> use_counts_;
  int total_preparse_skipped_ = 0;

  // Single function data for top-level function compilation.
  MaybeHandle<SharedFunctionInfo> input_shared_info_;
  int start_position_;
  int end_position_;
  int function_literal_id_;

  CompileHintCallback compile_hint_callback_ = nullptr;
  void* compile_hint_callback_data_ = nullptr;
};

// Contains all data which needs to be transmitted between threads for
// background parsing and compiling and finalizing it on the main thread.
struct ScriptStreamingData {
  ScriptStreamingData(
      std::unique_ptr<ScriptCompiler::ExternalSourceStream> source_stream,
      ScriptCompiler::StreamedSource::Encoding encoding);
  ScriptStreamingData(const ScriptStreamingData&) = delete;
  ScriptStreamingData& operator=(const ScriptStreamingData&) = delete;
  ~ScriptStreamingData();

  void Release();

  // Internal implementation of v8::ScriptCompiler::StreamedSource.
  std::unique_ptr<ScriptCompiler::ExternalSourceStream> source_stream;
  ScriptCompiler::StreamedSource::Encoding encoding;

  // Task that performs background parsing and compilation.
  std::unique_ptr<BackgroundCompileTask> task;
};

class V8_EXPORT_PRIVATE BackgroundDeserializeTask {
 public:
  BackgroundDeserializeTask(Isolate* isolate,
                            std::unique_ptr<ScriptCompiler::CachedData> data);

  void Run();

  // Checks the Isolate compilation cache to see whether it will be necessary to
  // merge the newly deserialized objects into an existing Script. This can
  // change the value of ShouldMergeWithExistingScript, and embedders should
  // check the latter after calling this. May only be called on a thread where
  // the Isolate is currently entered.
  void SourceTextAvailable(Isolate* isolate, Handle<String> source_text,
                           const ScriptDetails& script_details);

  // Returns whether the embedder should call MergeWithExistingScript. This
  // function may be called from any thread, any number of times, but its return
  // value is only meaningful after SourceTextAvailable has completed.
  bool ShouldMergeWithExistingScript() const;

  // Partially merges newly deserialized objects into an existing Script with
  // the same source, as provided by SourceTextAvailable, and generates a list
  // of follow-up work for the main thread. May be called from any thread, only
  // once.
  void MergeWithExistingScript();

  MaybeHandle<SharedFunctionInfo> Finish(Isolate* isolate,
                                         Handle<String> source,
                                         const ScriptDetails& script_details);

  bool rejected() const { return cached_data_.rejected(); }

  int64_t background_time_in_microseconds() const {
    return background_time_in_microseconds_;
  }

 private:
  Isolate* isolate_for_local_isolate_;
  AlignedCachedData cached_data_;
  CodeSerializer::OffThreadDeserializeData off_thread_data_;
  BackgroundMergeTask background_merge_task_;
  TimedHistogram* timer_;
  int64_t background_time_in_microseconds_ = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_COMPILER_H_
