// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_H_
#define V8_DEBUG_DEBUG_H_

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "src/base/enum-set.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/codegen/source-position-table.h"
#include "src/common/globals.h"
#include "src/debug/debug-interface.h"
#include "src/debug/interface-types.h"
#include "src/execution/interrupts-scope.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/debug-objects.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

// Forward declarations.
class AbstractCode;
class DebugScope;
class InterpretedFrame;
class JavaScriptFrame;
class JSGeneratorObject;
class StackFrame;

// Step actions.
enum StepAction : int8_t {
  StepNone = -1,  // Stepping not prepared.
  StepOut = 0,    // Step out of the current function.
  StepOver = 1,   // Step to the next statement in the current function.
  StepInto = 2,   // Step into new functions invoked or the next statement
                  // in the current function.
  LastStepAction = StepInto
};

// Type of exception break. NOTE: These values are in macros.py as well.
enum ExceptionBreakType {
  BreakCaughtException = 0,
  BreakUncaughtException = 1,
};

// Type of debug break. NOTE: The order matters for the predicates
// below inside BreakLocation, so be careful when adding / removing.
enum DebugBreakType {
  NOT_DEBUG_BREAK,
  DEBUG_BREAK_AT_ENTRY,
  DEBUGGER_STATEMENT,
  DEBUG_BREAK_SLOT,
  DEBUG_BREAK_SLOT_AT_CALL,
  DEBUG_BREAK_SLOT_AT_RETURN,
  DEBUG_BREAK_SLOT_AT_SUSPEND,
};

enum IgnoreBreakMode {
  kIgnoreIfAllFramesBlackboxed,
  kIgnoreIfTopFrameBlackboxed
};

class BreakLocation {
 public:
  static BreakLocation Invalid() { return BreakLocation(-1, NOT_DEBUG_BREAK); }
  static BreakLocation FromFrame(Handle<DebugInfo> debug_info,
                                 JavaScriptFrame* frame);
  static bool IsPausedInJsFunctionEntry(JavaScriptFrame* frame);

  static void AllAtCurrentStatement(Handle<DebugInfo> debug_info,
                                    JavaScriptFrame* frame,
                                    std::vector<BreakLocation>* result_out);

  bool IsSuspend() const { return type_ == DEBUG_BREAK_SLOT_AT_SUSPEND; }
  bool IsReturn() const { return type_ == DEBUG_BREAK_SLOT_AT_RETURN; }
  bool IsReturnOrSuspend() const { return type_ >= DEBUG_BREAK_SLOT_AT_RETURN; }
  bool IsCall() const { return type_ == DEBUG_BREAK_SLOT_AT_CALL; }
  bool IsDebugBreakSlot() const { return type_ >= DEBUG_BREAK_SLOT; }
  bool IsDebuggerStatement() const { return type_ == DEBUGGER_STATEMENT; }
  bool IsDebugBreakAtEntry() const { return type_ == DEBUG_BREAK_AT_ENTRY; }

  bool HasBreakPoint(Isolate* isolate, Handle<DebugInfo> debug_info) const;

  int generator_suspend_id() { return generator_suspend_id_; }
  int position() const { return position_; }
  int code_offset() const { return code_offset_; }

  debug::BreakLocationType type() const;

  Tagged<JSGeneratorObject> GetGeneratorObjectForSuspendedFrame(
      JavaScriptFrame* frame) const;

 private:
  BreakLocation(Handle<AbstractCode> abstract_code, DebugBreakType type,
                int code_offset, int position, int generator_obj_reg_index,
                int generator_suspend_id)
      : abstract_code_(abstract_code),
        code_offset_(code_offset),
        type_(type),
        position_(position),
        generator_obj_reg_index_(generator_obj_reg_index),
        generator_suspend_id_(generator_suspend_id) {
    DCHECK_NE(NOT_DEBUG_BREAK, type_);
  }

  BreakLocation(int position, DebugBreakType type)
      : code_offset_(0),
        type_(type),
        position_(position),
        generator_obj_reg_index_(0),
        generator_suspend_id_(-1) {}

  static int BreakIndexFromCodeOffset(Handle<DebugInfo> debug_info,
                                      DirectHandle<AbstractCode> abstract_code,
                                      int offset);

  void SetDebugBreak();
  void ClearDebugBreak();

  Handle<AbstractCode> abstract_code_;
  int code_offset_;
  DebugBreakType type_;
  int position_;
  int generator_obj_reg_index_;
  int generator_suspend_id_;

  friend class BreakIterator;
};

class V8_EXPORT_PRIVATE BreakIterator {
 public:
  explicit BreakIterator(Handle<DebugInfo> debug_info);
  BreakIterator(const BreakIterator&) = delete;
  BreakIterator& operator=(const BreakIterator&) = delete;

  BreakLocation GetBreakLocation();
  bool Done() const { return source_position_iterator_.done(); }
  void Next();

  void SkipToPosition(int position);
  void SkipTo(int count) {
    while (count-- > 0) Next();
  }

  int code_offset() { return source_position_iterator_.code_offset(); }
  int break_index() const { return break_index_; }
  inline int position() const { return position_; }
  inline int statement_position() const { return statement_position_; }

  void ClearDebugBreak();
  void SetDebugBreak();

  DebugBreakType GetDebugBreakType();

 private:
  int BreakIndexFromPosition(int position);

  Isolate* isolate();

  Handle<DebugInfo> debug_info_;
  int break_index_;
  int position_;
  int statement_position_;
  SourcePositionTableIterator source_position_iterator_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
};

// Holds all active DebugInfo objects. This is a composite data structure
// consisting of
//
// - an unsorted list-like structure for fast iteration and
//   deletion-during-iteration, and
// - a map-like structure for fast SharedFunctionInfo-DebugInfo lookups.
//
// DebugInfos are held strongly through global handles.
//
// TODO(jgruber): Now that we use an unordered_map as the map-like structure,
// which supports deletion-during-iteration, the list-like part of this data
// structure could be removed.
class DebugInfoCollection final {
  using HandleLocation = Address*;
  using SFIUniqueId = uint32_t;  // The type of SFI::unique_id.

 public:
  explicit DebugInfoCollection(Isolate* isolate) : isolate_(isolate) {}

  void Insert(Tagged<SharedFunctionInfo> sfi, Tagged<DebugInfo> debug_info);

  bool Contains(Tagged<SharedFunctionInfo> sfi) const;
  std::optional<Tagged<DebugInfo>> Find(Tagged<SharedFunctionInfo> sfi) const;

  void DeleteSlow(Tagged<SharedFunctionInfo> sfi);

  size_t Size() const { return list_.size(); }

  class Iterator final {
   public:
    explicit Iterator(DebugInfoCollection* collection)
        : collection_(collection) {}

    bool HasNext() const {
      return index_ < static_cast<int>(collection_->list_.size());
    }

    Tagged<DebugInfo> Next() const {
      DCHECK_GE(index_, 0);
      if (!HasNext()) return {};
      return collection_->EntryAsDebugInfo(index_);
    }

    void Advance() {
      DCHECK(HasNext());
      index_++;
    }

    void DeleteNext() {
      DCHECK_GE(index_, 0);
      DCHECK(HasNext());
      collection_->DeleteIndex(index_);
      index_--;  // `Advance` must be called next.
    }

   private:
    using HandleLocation = DebugInfoCollection::HandleLocation;
    DebugInfoCollection* const collection_;
    int index_ = 0;  // `int` because deletion may rewind to -1.
  };

 private:
  V8_EXPORT_PRIVATE Tagged<DebugInfo> EntryAsDebugInfo(size_t index) const;
  void DeleteIndex(size_t index);

  Isolate* const isolate_;
  std::vector<HandleLocation> list_;
  std::unordered_map<SFIUniqueId, HandleLocation> map_;
};

// This class contains the debugger support. The main purpose is to handle
// setting break points in the code.
//
// This class controls the debug info for all functions which currently have
// active breakpoints in them. This debug info is held in the heap root object
// debug_info which is a FixedArray. Each entry in this list is of class
// DebugInfo.
class V8_EXPORT_PRIVATE Debug {
 public:
  Debug(const Debug&) = delete;
  Debug& operator=(const Debug&) = delete;

  // Debug event triggers.
  void OnDebugBreak(Handle<FixedArray> break_points_hit, StepAction stepAction,
                    debug::BreakReasons break_reasons = {});
  debug::DebugDelegate::ActionAfterInstrumentation OnInstrumentationBreak();

  std::optional<Tagged<Object>> OnThrow(Handle<Object> exception)
      V8_WARN_UNUSED_RESULT;
  void OnPromiseReject(Handle<Object> promise, Handle<Object> value);
  void OnCompileError(DirectHandle<Script> script);
  void OnAfterCompile(DirectHandle<Script> script);

  void HandleDebugBreak(IgnoreBreakMode ignore_break_mode,
                        debug::BreakReasons break_reasons);

  // The break target may not be the top-most frame, since we may be
  // breaking before entering a function that cannot contain break points.
  void Break(JavaScriptFrame* frame, DirectHandle<JSFunction> break_target);

  // Scripts handling.
  Handle<FixedArray> GetLoadedScripts();

  // DebugInfo accessors.
  std::optional<Tagged<DebugInfo>> TryGetDebugInfo(
      Tagged<SharedFunctionInfo> sfi);
  bool HasDebugInfo(Tagged<SharedFunctionInfo> sfi);
  bool HasCoverageInfo(Tagged<SharedFunctionInfo> sfi);
  bool HasBreakInfo(Tagged<SharedFunctionInfo> sfi);
  bool BreakAtEntry(Tagged<SharedFunctionInfo> sfi);

  // Break point handling.
  enum BreakPointKind { kRegular, kInstrumentation };
  bool SetBreakpoint(Handle<SharedFunctionInfo> shared,
                     DirectHandle<BreakPoint> break_point,
                     int* source_position);
  void ClearBreakPoint(DirectHandle<BreakPoint> break_point);
  void ChangeBreakOnException(ExceptionBreakType type, bool enable);
  bool IsBreakOnException(ExceptionBreakType type);

  void SetTerminateOnResume();

  bool SetBreakPointForScript(Handle<Script> script,
                              DirectHandle<String> condition,
                              int* source_position, int* id);
  bool SetBreakpointForFunction(Handle<SharedFunctionInfo> shared,
                                DirectHandle<String> condition, int* id,
                                BreakPointKind kind = kRegular);
  void RemoveBreakpoint(int id);
#if V8_ENABLE_WEBASSEMBLY
  void SetInstrumentationBreakpointForWasmScript(Handle<Script> script,
                                                 int* id);
  void RemoveBreakpointForWasmScript(DirectHandle<Script> script, int id);

  void RecordWasmScriptWithBreakpoints(Handle<Script> script);
#endif  // V8_ENABLE_WEBASSEMBLY

  // Find breakpoints from the debug info and the break location and check
  // whether they are hit. Return an empty handle if not, or a FixedArray with
  // hit BreakPoint objects. has_break_points is set to true if position has
  // any non-instrumentation breakpoint.
  MaybeHandle<FixedArray> GetHitBreakPoints(DirectHandle<DebugInfo> debug_info,
                                            int position,
                                            bool* has_break_points);

  // Stepping handling.
  void PrepareStep(StepAction step_action);
  void PrepareStepIn(DirectHandle<JSFunction> function);
  void PrepareStepInSuspendedGenerator();
  void PrepareStepOnThrow();
  void ClearStepping();
  void PrepareRestartFrame(JavaScriptFrame* frame, int inlined_frame_index);

  void SetBreakOnNextFunctionCall();
  void ClearBreakOnNextFunctionCall();

  void DiscardBaselineCode(Tagged<SharedFunctionInfo> shared);
  void DiscardAllBaselineCode();

  void DeoptimizeFunction(DirectHandle<SharedFunctionInfo> shared);
  void PrepareFunctionForDebugExecution(
      DirectHandle<SharedFunctionInfo> shared);
  void InstallDebugBreakTrampoline();
  bool GetPossibleBreakpoints(Handle<Script> script, int start_position,
                              int end_position, bool restrict_to_function,
                              std::vector<BreakLocation>* locations);

  bool IsBlackboxed(DirectHandle<SharedFunctionInfo> shared);
  bool ShouldBeSkipped();

  bool CanBreakAtEntry(DirectHandle<SharedFunctionInfo> shared);

  void SetDebugDelegate(debug::DebugDelegate* delegate);

  // Returns whether the operation succeeded.
  bool EnsureBreakInfo(Handle<SharedFunctionInfo> shared);
  void CreateBreakInfo(Handle<SharedFunctionInfo> shared);
  Handle<DebugInfo> GetOrCreateDebugInfo(
      DirectHandle<SharedFunctionInfo> shared);

  void InstallCoverageInfo(DirectHandle<SharedFunctionInfo> shared,
                           Handle<CoverageInfo> coverage_info);
  void RemoveAllCoverageInfos();

  // This function is used in FunctionNameUsing* tests.
  Handle<Object> FindInnermostContainingFunctionInfo(Handle<Script> script,
                                                     int position);

  Handle<SharedFunctionInfo> FindClosestSharedFunctionInfoFromPosition(
      int position, Handle<Script> script,
      Handle<SharedFunctionInfo> outer_shared);

  bool FindSharedFunctionInfosIntersectingRange(
      Handle<Script> script, int start_position, int end_position,
      std::vector<Handle<SharedFunctionInfo>>* candidates);

  MaybeHandle<SharedFunctionInfo> GetTopLevelWithRecompile(
      Handle<Script> script, bool* did_compile = nullptr);

  static Handle<Object> GetSourceBreakLocations(
      Isolate* isolate, DirectHandle<SharedFunctionInfo> shared);

  // Check whether this frame is just about to return.
  bool IsBreakAtReturn(JavaScriptFrame* frame);

  // Walks the call stack to see if any frames are not ignore listed.
  bool AllFramesOnStackAreBlackboxed();

  // Set new script source, throw an exception if error occurred. When preview
  // is true: try to set source, throw exception if any without actual script
  // change. stack_changed is true if after editing script on pause stack is
  // changed and client should request stack trace again.
  bool SetScriptSource(Handle<Script> script, Handle<String> source,
                       bool preview, bool allow_top_frame_live_editing,
                       debug::LiveEditResult* result);

  int GetFunctionDebuggingId(DirectHandle<JSFunction> function);

  // Threading support.
  char* ArchiveDebug(char* to);
  char* RestoreDebug(char* from);
  static int ArchiveSpacePerThread();
  void FreeThreadResources() {}
  void Iterate(RootVisitor* v);
  void InitThread(const ExecutionAccess& lock) { ThreadInit(); }

  bool CheckExecutionState() { return is_active(); }

  void StartSideEffectCheckMode();
  void StopSideEffectCheckMode();

  void ApplySideEffectChecks(DirectHandle<DebugInfo> debug_info);
  void ClearSideEffectChecks(DirectHandle<DebugInfo> debug_info);

  // Make a one-time exception for a next call to given side-effectful API
  // function.
  void IgnoreSideEffectsOnNextCallTo(Handle<FunctionTemplateInfo> function);

  bool PerformSideEffectCheck(Handle<JSFunction> function,
                              Handle<Object> receiver);

  void PrepareBuiltinForSideEffectCheck(Isolate* isolate, Builtin id);

  bool PerformSideEffectCheckForAccessor(
      DirectHandle<AccessorInfo> accessor_info, Handle<Object> receiver,
      AccessorComponent component);
  bool PerformSideEffectCheckForCallback(Handle<FunctionTemplateInfo> function);
  bool PerformSideEffectCheckForInterceptor(
      Handle<InterceptorInfo> interceptor_info);

  bool PerformSideEffectCheckAtBytecode(InterpretedFrame* frame);
  bool PerformSideEffectCheckForObject(Handle<Object> object);

  // Flags and states.
  inline bool is_active() const { return is_active_; }
  inline bool in_debug_scope() const {
    return !!base::Relaxed_Load(&thread_local_.current_debug_scope_);
  }
  inline bool needs_check_on_function_call() const {
    return hook_on_function_call_;
  }

  void set_break_points_active(bool v) { break_points_active_ = v; }
  bool break_points_active() const { return break_points_active_; }

  StackFrameId break_frame_id() { return thread_local_.break_frame_id_; }

  Handle<Object> return_value_handle();
  Tagged<Object> return_value() { return thread_local_.return_value_; }
  void set_return_value(Tagged<Object> value) {
    thread_local_.return_value_ = value;
  }

  // Support for embedding into generated code.
  Address is_active_address() { return reinterpret_cast<Address>(&is_active_); }

  Address hook_on_function_call_address() {
    return reinterpret_cast<Address>(&hook_on_function_call_);
  }

  Address suspended_generator_address() {
    return reinterpret_cast<Address>(&thread_local_.suspended_generator_);
  }

  StepAction last_step_action() { return thread_local_.last_step_action_; }
  bool break_on_next_function_call() const {
    return thread_local_.break_on_next_function_call_;
  }

  bool scheduled_break_on_function_call() const {
    return thread_local_.scheduled_break_on_next_function_call_;
  }

  bool IsRestartFrameScheduled() const {
    return thread_local_.restart_frame_id_ != StackFrameId::NO_ID;
  }
  bool ShouldRestartFrame(StackFrameId id) const {
    return IsRestartFrameScheduled() && thread_local_.restart_frame_id_ == id;
  }
  void clear_restart_frame() {
    thread_local_.restart_frame_id_ = StackFrameId::NO_ID;
    thread_local_.restart_inline_frame_index_ = -1;
  }
  int restart_inline_frame_index() const {
    return thread_local_.restart_inline_frame_index_;
  }

  inline bool break_disabled() const { return break_disabled_; }

  // For functions in which we cannot set a break point, use a canonical
  // source position for break points.
  static const int kBreakAtEntryPosition = 0;

  // Use -1 to encode instrumentation breakpoints.
  static const int kInstrumentationId = -1;

  void RemoveBreakInfoAndMaybeFree(DirectHandle<DebugInfo> debug_info);

  // Stops the timer for the top-most `DebugScope` and records a UMA event.
  void NotifyDebuggerPausedEventSent();

  static char* Iterate(RootVisitor* v, char* thread_storage);

  // Clear information pertaining to break location muting.
  void ClearMutedLocation();
#if V8_ENABLE_WEBASSEMBLY
  // Mute additional pauses from this Wasm location.
  void SetMutedWasmLocation(DirectHandle<Script> script, int position);
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  explicit Debug(Isolate* isolate);
  ~Debug();

  void UpdateDebugInfosForExecutionMode();
  void UpdateState();
  void UpdateHookOnFunctionCall();
  void Unload();

  // Return the number of virtual frames below debugger entry.
  int CurrentFrameCount();

  inline bool ignore_events() const {
    return is_suppressed_ || !is_active_ ||
           isolate_->debug_execution_mode() == DebugInfo::kSideEffects;
  }

  void clear_suspended_generator() {
    thread_local_.suspended_generator_ = Smi::zero();
  }

  bool has_suspended_generator() const {
    return thread_local_.suspended_generator_ != Smi::zero();
  }

  void OnException(Handle<Object> exception, MaybeHandle<JSPromise> promise,
                   v8::debug::ExceptionType exception_type);

  void ProcessCompileEvent(bool has_compile_error, DirectHandle<Script> script);

  // Find the closest source position for a break point for a given position.
  int FindBreakablePosition(Handle<DebugInfo> debug_info, int source_position);
  // Instrument code to break at break points.
  void ApplyBreakPoints(Handle<DebugInfo> debug_info);
  // Clear code from instrumentation.
  void ClearBreakPoints(Handle<DebugInfo> debug_info);
  // Clear all code from instrumentation.
  void ClearAllBreakPoints();
  // Instrument a function with one-shots.
  void FloodWithOneShot(Handle<SharedFunctionInfo> function,
                        bool returns_only = false);
  // Clear all one-shot instrumentations, but restore break points.
  void ClearOneShot();

  // Mute additional pauses from this JavaScript location.
  void SetMutedLocation(DirectHandle<SharedFunctionInfo> function,
                        const BreakLocation& location);

  bool IsFrameBlackboxed(JavaScriptFrame* frame);

  void ActivateStepOut(StackFrame* frame);
  bool IsBreakOnInstrumentation(Handle<DebugInfo> debug_info,
                                const BreakLocation& location);
  bool IsBreakOnDebuggerStatement(DirectHandle<SharedFunctionInfo> function,
                                  const BreakLocation& location);
  MaybeHandle<FixedArray> CheckBreakPoints(Handle<DebugInfo> debug_info,
                                           BreakLocation* location,
                                           bool* has_break_points);
  MaybeHandle<FixedArray> CheckBreakPointsForLocations(
      Handle<DebugInfo> debug_info, std::vector<BreakLocation>& break_locations,
      bool* has_break_points);

  bool IsMutedAtAnyBreakLocation(DirectHandle<SharedFunctionInfo> function,
                                 const std::vector<BreakLocation>& locations);
#if V8_ENABLE_WEBASSEMBLY
  bool IsMutedAtWasmLocation(Tagged<Script> script, int position);
#endif  // V8_ENABLE_WEBASSEMBLY
  // Check whether a BreakPoint object is hit. Evaluate condition depending
  // on whether this is a regular break location or a break at function entry.
  bool CheckBreakPoint(DirectHandle<BreakPoint> break_point,
                       bool is_break_at_entry);

  inline void AssertDebugContext() { DCHECK(in_debug_scope()); }

  void ThreadInit();

  void PrintBreakLocation();

  void ClearAllDebuggerHints();

  // Wraps logic for clearing and maybe freeing all debug infos.
  using DebugInfoClearFunction = std::function<void(Handle<DebugInfo>)>;
  void ClearAllDebugInfos(const DebugInfoClearFunction& clear_function);

  void SetTemporaryObjectTrackingDisabled(bool disabled);
  bool GetTemporaryObjectTrackingDisabled() const;

  debug::DebugDelegate* debug_delegate_ = nullptr;

  // Debugger is active, i.e. there is a debug event listener attached.
  // This field is atomic because background compilation jobs can read it
  // through Isolate::NeedsDetailedOptimizedCodeLineInfo.
  std::atomic<bool> is_active_;
  // Debugger needs to be notified on every new function call.
  // Used for stepping and read-only checks
  bool hook_on_function_call_;
  // Suppress debug events.
  bool is_suppressed_;
  // Running liveedit.
  bool running_live_edit_ = false;
  // Do not trigger debug break events.
  bool break_disabled_;
  // Do not break on break points.
  bool break_points_active_;
  // Trigger debug break events for caught exceptions.
  bool break_on_caught_exception_;
  // Trigger debug break events for uncaught exceptions.
  bool break_on_uncaught_exception_;
  // Termination exception because side effect check has failed.
  bool side_effect_check_failed_;

  // List of active debug info objects.
  DebugInfoCollection debug_infos_;

  // Used for side effect check to mark temporary objects.
  class TemporaryObjectsTracker;
  std::unique_ptr<TemporaryObjectsTracker> temporary_objects_;

  Handle<RegExpMatchInfo> regexp_match_info_;

  // Per-thread data.
  class ThreadLocal {
   public:
    // Top debugger entry.
    base::AtomicWord current_debug_scope_;

    // Frame id for the frame of the current break.
    StackFrameId break_frame_id_;

    // Step action for last step performed.
    StepAction last_step_action_;

    // If set, next PrepareStepIn will ignore this function until stepped into
    // another function, at which point this will be cleared.
    Tagged<Object> ignore_step_into_function_;

    // If set then we need to repeat StepOut action at return.
    bool fast_forward_to_return_;

    // Source statement position from last step next action.
    int last_statement_position_;

    // Bytecode offset from last step next action.
    int last_bytecode_offset_;

    // Frame pointer from last step next or step frame action.
    int last_frame_count_;

    // Frame pointer of the target frame we want to arrive at.
    int target_frame_count_;

    // Value of the accumulator at the point of entering the debugger.
    Tagged<Object> return_value_;

    // The suspended generator object to track when stepping.
    Tagged<Object> suspended_generator_;

    // Last used inspector breakpoint id.
    int last_breakpoint_id_;

    // This flag is true when SetBreakOnNextFunctionCall is called and it forces
    // debugger to break on next function call.
    bool break_on_next_function_call_;

    // This flag is true when we break via stack check (BreakReason::kScheduled)
    // We don't stay paused there but instead "step in" to the function similar
    // to what "BreakOnNextFunctionCall" does.
    bool scheduled_break_on_next_function_call_;

    // Frame ID for the frame that needs to be restarted. StackFrameId::NO_ID
    // otherwise. The unwinder uses the id to restart execution in this frame
    // instead of any potential catch handler.
    StackFrameId restart_frame_id_;

    // If `restart_frame_id_` is an optimized frame, then this index denotes
    // which of the inlined frames we actually want to restart. The
    // deoptimizer uses the info to materialize and drop execution into the
    // right frame.
    int restart_inline_frame_index_;

    // If the most recent breakpoint did not result in a break because its
    // condition was false, we will mute other break reasons if we are still at
    // the same location. In that case, this points to the SharedFunctionInfo
    // (if JavaScript) or Script (if Wasm) of the location where stopping has
    // been muted; otherwise it is Smi::zero().
    Tagged<Object> muted_function_;

    // The source position at which breaking is muted. Only relevant if
    // muted_function_ is set.
    int muted_position_;
  };

  static void Iterate(RootVisitor* v, ThreadLocal* thread_local_data);

  // Storage location for registers when handling debug break calls
  ThreadLocal thread_local_;

#if V8_ENABLE_WEBASSEMBLY
  // This is a global handle, lazily initialized.
  Handle<WeakArrayList> wasm_scripts_with_break_points_;
#endif  // V8_ENABLE_WEBASSEMBLY

  // This is a part of machinery for allowing to ignore side effects for one
  // call to this API function. See Function::NewInstanceWithSideEffectType().
  // Since the FunctionTemplateInfo is allowlisted right before the call to
  // constructor there must be never more than one such object at a time.
  Handle<FunctionTemplateInfo> ignore_side_effects_for_function_template_info_;

  Isolate* isolate_;

  friend class Isolate;
  friend class DebugScope;
  friend class DisableBreak;
  friend class DisableTemporaryObjectTracking;
  friend class LiveEdit;
  friend class SuppressDebug;

  friend Handle<FixedArray> GetDebuggedFunctions();  // In test-debug.cc
  friend void CheckDebuggerUnloaded();               // In test-debug.cc
};

// This scope is used to load and enter the debug context and create a new
// break state.  Leaving the scope will restore the previous state.
class V8_NODISCARD DebugScope {
 public:
  explicit DebugScope(Debug* debug);
  ~DebugScope();

  void set_terminate_on_resume();

  base::TimeDelta ElapsedTimeSinceCreation();

 private:
  Isolate* isolate() { return debug_->isolate_; }

  Debug* debug_;
  DebugScope* prev_;             // Previous scope if entered recursively.
  StackFrameId break_frame_id_;  // Previous break frame id.
  PostponeInterruptsScope no_interrupts_;
  // This is used as a boolean.
  bool terminate_on_resume_ = false;

  // Measures (for UMA) the duration beginning when we enter this `DebugScope`
  // until we potentially send a "Debugger.paused" response in the inspector.
  base::ElapsedTimer timer_;
};

// This scope is used to handle return values in nested debug break points.
// When there are nested debug breaks, we use this to restore the return
// value to the previous state. This is not merged with DebugScope because
// return_value_ will not be cleared when we use DebugScope.
class V8_NODISCARD ReturnValueScope {
 public:
  explicit ReturnValueScope(Debug* debug);
  ~ReturnValueScope();

 private:
  Debug* debug_;
  Handle<Object> return_value_;  // Previous result.
};

// Stack allocated class for disabling break.
class DisableBreak {
 public:
  explicit DisableBreak(Debug* debug, bool disable = true)
      : debug_(debug), previous_break_disabled_(debug->break_disabled_) {
    debug_->break_disabled_ = disable;
  }
  ~DisableBreak() { debug_->break_disabled_ = previous_break_disabled_; }
  DisableBreak(const DisableBreak&) = delete;
  DisableBreak& operator=(const DisableBreak&) = delete;

 private:
  Debug* debug_;
  bool previous_break_disabled_;
};

// Stack allocated class for disabling temporary object tracking.
class DisableTemporaryObjectTracking {
 public:
  explicit DisableTemporaryObjectTracking(Debug* debug)
      : debug_(debug),
        previous_tracking_disabled_(
            debug->GetTemporaryObjectTrackingDisabled()) {
    debug_->SetTemporaryObjectTrackingDisabled(true);
  }
  ~DisableTemporaryObjectTracking() {
    debug_->SetTemporaryObjectTrackingDisabled(previous_tracking_disabled_);
  }
  DisableTemporaryObjectTracking(const DisableTemporaryObjectTracking&) =
      delete;
  DisableTemporaryObjectTracking& operator=(
      const DisableTemporaryObjectTracking&) = delete;

 private:
  Debug* debug_;
  bool previous_tracking_disabled_;
};

class SuppressDebug {
 public:
  explicit SuppressDebug(Debug* debug)
      : debug_(debug), old_state_(debug->is_suppressed_) {
    debug_->is_suppressed_ = true;
  }
  ~SuppressDebug() { debug_->is_suppressed_ = old_state_; }
  SuppressDebug(const SuppressDebug&) = delete;
  SuppressDebug& operator=(const SuppressDebug&) = delete;

 private:
  Debug* debug_;
  bool old_state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_H_
