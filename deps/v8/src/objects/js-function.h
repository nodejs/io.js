// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_FUNCTION_H_
#define V8_OBJECTS_JS_FUNCTION_H_

#include "src/base/optional.h"
#include "src/objects/code-kind.h"
#include "src/objects/js-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class AbstractCode;
class ClosureFeedbackCellArray;

#include "torque-generated/src/objects/js-function-tq.inc"

// An abstract superclass for classes representing JavaScript function values.
// It doesn't carry any functionality but allows function classes to be
// identified in the type system.
class JSFunctionOrBoundFunctionOrWrappedFunction
    : public TorqueGeneratedJSFunctionOrBoundFunctionOrWrappedFunction<
          JSFunctionOrBoundFunctionOrWrappedFunction, JSObject> {
 public:
  static const int kLengthDescriptorIndex = 0;
  static const int kNameDescriptorIndex = 1;

  // https://tc39.es/proposal-shadowrealm/#sec-copynameandlength
  static Maybe<bool> CopyNameAndLength(
      Isolate* isolate,
      Handle<JSFunctionOrBoundFunctionOrWrappedFunction> function,
      Handle<JSReceiver> target, Handle<String> prefix, int arg_count);

  static_assert(kHeaderSize == JSObject::kHeaderSize);
  TQ_OBJECT_CONSTRUCTORS(JSFunctionOrBoundFunctionOrWrappedFunction)
};

// JSBoundFunction describes a bound function exotic object.
class JSBoundFunction
    : public TorqueGeneratedJSBoundFunction<
          JSBoundFunction, JSFunctionOrBoundFunctionOrWrappedFunction> {
 public:
  static MaybeHandle<String> GetName(Isolate* isolate,
                                     Handle<JSBoundFunction> function);
  static Maybe<int> GetLength(Isolate* isolate,
                              Handle<JSBoundFunction> function);

  // Dispatched behavior.
  DECL_PRINTER(JSBoundFunction)
  DECL_VERIFIER(JSBoundFunction)

  // The bound function's string representation implemented according
  // to ES6 section 19.2.3.5 Function.prototype.toString ( ).
  static Handle<String> ToString(Handle<JSBoundFunction> function);

  TQ_OBJECT_CONSTRUCTORS(JSBoundFunction)
};

// JSWrappedFunction describes a wrapped function exotic object.
class JSWrappedFunction
    : public TorqueGeneratedJSWrappedFunction<
          JSWrappedFunction, JSFunctionOrBoundFunctionOrWrappedFunction> {
 public:
  static MaybeHandle<String> GetName(Isolate* isolate,
                                     Handle<JSWrappedFunction> function);
  static Maybe<int> GetLength(Isolate* isolate,
                              Handle<JSWrappedFunction> function);
  // https://tc39.es/proposal-shadowrealm/#sec-wrappedfunctioncreate
  static MaybeHandle<Object> Create(Isolate* isolate,
                                    Handle<NativeContext> creation_context,
                                    Handle<JSReceiver> value);

  // Dispatched behavior.
  DECL_PRINTER(JSWrappedFunction)
  DECL_VERIFIER(JSWrappedFunction)

  // The wrapped function's string representation implemented according
  // to ES6 section 19.2.3.5 Function.prototype.toString ( ).
  static Handle<String> ToString(Handle<JSWrappedFunction> function);

  TQ_OBJECT_CONSTRUCTORS(JSWrappedFunction)
};

// JSFunction describes JavaScript functions.
class JSFunction : public TorqueGeneratedJSFunction<
                       JSFunction, JSFunctionOrBoundFunctionOrWrappedFunction> {
 public:
  // [prototype_or_initial_map]:
  DECL_RELEASE_ACQUIRE_ACCESSORS(prototype_or_initial_map, Tagged<HeapObject>)

  // [shared]: The information about the function that can be shared by
  // instances.
  DECL_ACCESSORS(shared, Tagged<SharedFunctionInfo>)
  DECL_RELAXED_GETTER(shared, Tagged<SharedFunctionInfo>)

  // Fast binding requires length and name accessors.
  static const int kMinDescriptorsForFastBindAndWrap = 2;

  // [context]: The context for this function.
  inline Tagged<Context> context();
  DECL_RELAXED_GETTER(context, Tagged<Context>)
  inline bool has_context() const;
  using TorqueGeneratedClass::context;
  using TorqueGeneratedClass::set_context;
  DECL_RELEASE_ACQUIRE_ACCESSORS(context, Tagged<Context>)
  inline Tagged<JSGlobalProxy> global_proxy();
  inline Tagged<NativeContext> native_context();
  inline int length();

  static Handle<String> GetName(Isolate* isolate, Handle<JSFunction> function);

  // [code]: The generated code object for this function.  Executed
  // when the function is invoked, e.g. foo() or new foo(). See
  // [[Call]] and [[Construct]] description in ECMA-262, section
  // 8.6.2, page 27.
  // Release/Acquire accessors are used when storing a newly-created
  // optimized code object, or when reading from the background thread.
  // Storing a builtin doesn't require release semantics because these objects
  // are fully initialized.
  DECL_CODE_POINTER_ACCESSORS(code)

  // Returns the raw content of the Code field. When reading from a background
  // thread, the code field may still be uninitialized, in which case the field
  // contains Smi::zero().
  inline Tagged<Object> raw_code(IsolateForSandbox isolate) const;
  inline Tagged<Object> raw_code(IsolateForSandbox isolate,
                                 AcquireLoadTag) const;

  // Returns the address of the function code's instruction start.
  inline Address instruction_start(IsolateForSandbox isolate) const;

  // Get the abstract code associated with the function, which will either be
  // a InstructionStream object or a BytecodeArray.
  template <typename IsolateT>
  inline Tagged<AbstractCode> abstract_code(IsolateT* isolate);

  // The predicates for querying code kinds related to this function have
  // specific terminology:
  //
  // - Attached: all code kinds that are directly attached to this JSFunction
  //   object.
  // - Available: all code kinds that are either attached or available through
  //   indirect means such as the feedback vector's optimized code cache.
  // - Active: the single code kind that would be executed if this function
  //   were called in its current state. Note that there may not be an active
  //   code kind if the function is not compiled. Also, asm/wasm functions are
  //   currently not supported.
  //
  // Note: code objects that are marked_for_deoptimization are not part of the
  // attached/available/active sets. This is because the JSFunction might have
  // been already deoptimized but its code() still needs to be unlinked, which
  // will happen on its next activation.

  bool HasAvailableHigherTierCodeThan(IsolateForSandbox isolate,
                                      CodeKind kind) const;
  // As above but only considers available code kinds passing the filter mask.
  bool HasAvailableHigherTierCodeThanWithFilter(IsolateForSandbox isolate,
                                                CodeKind kind,
                                                CodeKinds filter_mask) const;

  // True, iff any generated code kind is attached/available to this function.
  V8_EXPORT_PRIVATE bool HasAttachedOptimizedCode(
      IsolateForSandbox isolate) const;
  bool HasAvailableOptimizedCode(IsolateForSandbox isolate) const;

  bool HasAttachedCodeKind(IsolateForSandbox isolate, CodeKind kind) const;
  bool HasAvailableCodeKind(IsolateForSandbox isolate, CodeKind kind) const;

  base::Optional<CodeKind> GetActiveTier(IsolateForSandbox isolate) const;
  V8_EXPORT_PRIVATE bool ActiveTierIsIgnition(IsolateForSandbox isolate) const;
  bool ActiveTierIsBaseline(IsolateForSandbox isolate) const;
  bool ActiveTierIsMaglev(IsolateForSandbox isolate) const;
  bool ActiveTierIsTurbofan(IsolateForSandbox isolate) const;

  // Similar to SharedFunctionInfo::CanDiscardCompiled. Returns true, if the
  // attached code can be recreated at a later point by replacing it with
  // CompileLazy.
  bool CanDiscardCompiled(IsolateForSandbox isolate) const;

  // Tells whether function's code object checks its tiering state (some code
  // kinds, e.g. TURBOFAN, ignore the tiering state).
  inline bool ChecksTieringState(IsolateForSandbox isolate);

  inline TieringState tiering_state() const;
  inline void set_tiering_state(IsolateForSandbox isolate, TieringState state);
  inline void reset_tiering_state();

  // Mark this function for lazy recompilation. The function will be recompiled
  // the next time it is executed.
  void MarkForOptimization(Isolate* isolate, CodeKind target_kind,
                           ConcurrencyMode mode);

  inline TieringState osr_tiering_state();
  inline void set_osr_tiering_state(TieringState marker);

  // Sets the interrupt budget based on whether the function has a feedback
  // vector and any optimized code.
  void SetInterruptBudget(Isolate* isolate,
                          base::Optional<CodeKind> override_active_tier = {});

  // If slack tracking is active, it computes instance size of the initial map
  // with minimum permissible object slack.  If it is not active, it simply
  // returns the initial map's instance size.
  int ComputeInstanceSizeWithMinSlack(Isolate* isolate);

  // Completes inobject slack tracking on initial map if it is active.
  inline void CompleteInobjectSlackTrackingIfActive();

  // [raw_feedback_cell]: Gives raw access to the FeedbackCell used to hold the
  /// FeedbackVector eventually. Generally this shouldn't be used to get the
  // feedback_vector, instead use feedback_vector() which correctly deals with
  // the JSFunction's bytecode being flushed.
  DECL_ACCESSORS(raw_feedback_cell, Tagged<FeedbackCell>)

  // [raw_feedback_cell] (synchronized version) When this is initialized from a
  // newly allocated object (instead of a root sentinel), it should
  // be written with release store semantics.
  DECL_RELEASE_ACQUIRE_ACCESSORS(raw_feedback_cell, Tagged<FeedbackCell>)

  // Functions related to feedback vector. feedback_vector() can be used once
  // the function has feedback vectors allocated. feedback vectors may not be
  // available after compile when lazily allocating feedback vectors.
  DECL_GETTER(feedback_vector, Tagged<FeedbackVector>)
  DECL_GETTER(has_feedback_vector, bool)
  V8_EXPORT_PRIVATE static void EnsureFeedbackVector(
      Isolate* isolate, Handle<JSFunction> function,
      IsCompiledScope* compiled_scope);
  static void CreateAndAttachFeedbackVector(Isolate* isolate,
                                            Handle<JSFunction> function,
                                            IsCompiledScope* compiled_scope);

  // Functions related to closure feedback cell array that holds feedback cells
  // used to create closures from this function. We allocate closure feedback
  // cell arrays after compile, when we want to allocate feedback vectors
  // lazily.
  inline bool has_closure_feedback_cell_array() const;
  inline Tagged<ClosureFeedbackCellArray> closure_feedback_cell_array() const;
  static void EnsureClosureFeedbackCellArray(
      Handle<JSFunction> function, bool reset_budget_for_feedback_allocation);

  // Initializes the feedback cell of |function|. In lite mode, this would be
  // initialized to the closure feedback cell array that holds the feedback
  // cells for create closure calls from this function. In the regular mode,
  // this allocates feedback vector.
  static void InitializeFeedbackCell(Handle<JSFunction> function,
                                     IsCompiledScope* compiled_scope,
                                     bool reset_budget_for_feedback_allocation);

  // Unconditionally clear the type feedback vector, even those that we usually
  // keep (e.g.: BinaryOp feedback).
  void ClearAllTypeFeedbackInfoForTesting();

  // Resets function to clear compiled data after bytecode has been flushed.
  inline bool NeedsResetDueToFlushedBytecode(IsolateForSandbox isolate);
  inline void ResetIfCodeFlushed(
      IsolateForSandbox isolate,
      base::Optional<
          std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                             Tagged<HeapObject> target)>>
          gc_notify_updated_slot = base::nullopt);

  // Returns if the closure's code field has to be updated because it has
  // stale baseline code.
  inline bool NeedsResetDueToFlushedBaselineCode(IsolateForSandbox isolate);

  // Returns if baseline code is a candidate for flushing. This method is called
  // from concurrent marking so we should be careful when accessing data fields.
  inline bool ShouldFlushBaselineCode(
      base::EnumSet<CodeFlushMode> code_flush_mode);

  DECL_GETTER(has_prototype_slot, bool)

  // The initial map for an object created by this constructor.
  DECL_GETTER(initial_map, Tagged<Map>)

  static void SetInitialMap(Isolate* isolate, Handle<JSFunction> function,
                            Handle<Map> map, Handle<HeapObject> prototype);
  static void SetInitialMap(Isolate* isolate, Handle<JSFunction> function,
                            Handle<Map> map, Handle<HeapObject> prototype,
                            Handle<JSFunction> constructor);

  DECL_GETTER(has_initial_map, bool)
  V8_EXPORT_PRIVATE static void EnsureHasInitialMap(
      Handle<JSFunction> function);

  // Creates a map that matches the constructor's initial map, but with
  // [[prototype]] being new.target.prototype. Because new.target can be a
  // JSProxy, this can call back into JavaScript.
  V8_EXPORT_PRIVATE static V8_WARN_UNUSED_RESULT MaybeHandle<Map> GetDerivedMap(
      Isolate* isolate, Handle<JSFunction> constructor,
      Handle<JSReceiver> new_target);

  // Like GetDerivedMap, but returns a map with a RAB / GSAB ElementsKind.
  static V8_WARN_UNUSED_RESULT MaybeHandle<Map> GetDerivedRabGsabTypedArrayMap(
      Isolate* isolate, Handle<JSFunction> constructor,
      Handle<JSReceiver> new_target);

  // Like GetDerivedMap, but can be used for DataViews for retrieving / creating
  // a map with a JS_RAB_GSAB_DATA_VIEW instance type.
  static V8_WARN_UNUSED_RESULT MaybeHandle<Map> GetDerivedRabGsabDataViewMap(
      Isolate* isolate, Handle<JSReceiver> new_target);

  // Get and set the prototype property on a JSFunction. If the
  // function has an initial map the prototype is set on the initial
  // map. Otherwise, the prototype is put in the initial map field
  // until an initial map is needed.
  DECL_GETTER(has_prototype, bool)
  DECL_GETTER(has_instance_prototype, bool)
  DECL_GETTER(prototype, Tagged<Object>)
  DECL_GETTER(instance_prototype, Tagged<HeapObject>)
  DECL_GETTER(has_prototype_property, bool)
  DECL_GETTER(PrototypeRequiresRuntimeLookup, bool)
  static void SetPrototype(Handle<JSFunction> function, Handle<Object> value);

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled(IsolateForSandbox isolate) const;

  static int GetHeaderSize(bool function_has_prototype_slot) {
    return function_has_prototype_slot ? JSFunction::kSizeWithPrototype
                                       : JSFunction::kSizeWithoutPrototype;
  }

  std::unique_ptr<char[]> DebugNameCStr();
  void PrintName(FILE* out = stdout);

  // Calculate the instance size and in-object properties count.
  // {CalculateExpectedNofProperties} can trigger compilation.
  static V8_WARN_UNUSED_RESULT int CalculateExpectedNofProperties(
      Isolate* isolate, Handle<JSFunction> function);
  static void CalculateInstanceSizeHelper(InstanceType instance_type,
                                          bool has_prototype_slot,
                                          int requested_embedder_fields,
                                          int requested_in_object_properties,
                                          int* instance_size,
                                          int* in_object_properties);

  // Dispatched behavior.
  DECL_PRINTER(JSFunction)
  DECL_VERIFIER(JSFunction)

  static Handle<String> GetName(Handle<JSFunction> function);

  // ES6 section 9.2.11 SetFunctionName
  // Because of the way this abstract operation is used in the spec,
  // it should never fail, but in practice it will fail if the generated
  // function name's length exceeds String::kMaxLength.
  static V8_WARN_UNUSED_RESULT bool SetName(Handle<JSFunction> function,
                                            Handle<Name> name,
                                            Handle<String> prefix);

  // The function's name if it is configured, otherwise shared function info
  // debug name.
  static Handle<String> GetDebugName(Handle<JSFunction> function);

  // The function's string representation implemented according to
  // ES6 section 19.2.3.5 Function.prototype.toString ( ).
  static Handle<String> ToString(Handle<JSFunction> function);

  class BodyDescriptor;

 private:
  // JSFunction doesn't have a fixed header size:
  // Hide TorqueGeneratedClass::kHeaderSize to avoid confusion.
  static const int kHeaderSize;

  // Hide generated accessors; custom accessors are called "shared".
  DECL_ACCESSORS(shared_function_info, Tagged<SharedFunctionInfo>)

  // Hide generated accessors; custom accessors are called "raw_feedback_cell".
  DECL_ACCESSORS(feedback_cell, Tagged<FeedbackCell>)

  // Returns the set of code kinds of compilation artifacts (bytecode,
  // generated code) attached to this JSFunction.
  // Note that attached code objects that are marked_for_deoptimization are not
  // included in this set.
  // TODO(jgruber): Currently at most one code kind can be attached. Consider
  // adding a NOT_COMPILED kind and changing this function to simply return the
  // kind if this becomes more convenient in the future.
  CodeKinds GetAttachedCodeKinds(IsolateForSandbox isolate) const;

  // As above, but also considers locations outside of this JSFunction. For
  // example the optimized code cache slot in the feedback vector, and the
  // shared function info.
  CodeKinds GetAvailableCodeKinds(IsolateForSandbox isolate) const;

 public:
  static constexpr int kSizeWithoutPrototype = kPrototypeOrInitialMapOffset;
  static constexpr int kSizeWithPrototype = TorqueGeneratedClass::kHeaderSize;

  TQ_OBJECT_CONSTRUCTORS(JSFunction)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_FUNCTION_H_
