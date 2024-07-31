// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SCOPE_INFO_H_
#define V8_OBJECTS_SCOPE_INFO_H_

#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/function-kind.h"
#include "src/objects/objects.h"
#include "src/utils/utils.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// scope-info-tq.inc uses NameToIndexHashTable.
class NameToIndexHashTable;

#include "torque-generated/src/objects/scope-info-tq.inc"

class SourceTextModuleInfo;
class StringSet;
class Zone;

struct VariableLookupResult {
  int context_index;
  int slot_index;
  // repl_mode flag is needed to disable inlining of 'const' variables in REPL
  // mode.
  bool is_repl_mode;
  IsStaticFlag is_static_flag;
  VariableMode mode;
  InitializationFlag init_flag;
  MaybeAssignedFlag maybe_assigned_flag;
};

// ScopeInfo represents information about different scopes of a source
// program  and the allocation of the scope's variables. Scope information
// is stored in a compressed form in ScopeInfo objects and is used
// at runtime (stack dumps, deoptimization, etc.).

// This object provides quick access to scope info details for runtime
// routines.
class ScopeInfo : public TorqueGeneratedScopeInfo<ScopeInfo, HeapObject> {
 public:
  DEFINE_TORQUE_GENERATED_SCOPE_FLAGS()

  DECL_PRINTER(ScopeInfo)
  class BodyDescriptor;

  // Return the type of this scope.
  ScopeType scope_type() const;

  // Return the language mode of this scope.
  LanguageMode language_mode() const;

  // True if this scope is a (var) declaration scope.
  bool is_declaration_scope() const;

  // Does this scope make a sloppy eval call?
  bool SloppyEvalCanExtendVars() const;

  // Return the number of context slots for code if a context is allocated. This
  // number consists of three parts:
  //  1. Size of header for every context.
  //  2. One context slot per context allocated local.
  //  3. One context slot for the function name if it is context allocated.
  // Parameters allocated in the context count as context allocated locals. If
  // no contexts are allocated for this scope ContextLength returns 0.
  int ContextLength() const;
  int ContextHeaderLength() const;

  bool HasContextExtensionSlot() const;

  // Does this scope declare a "this" binding?
  bool HasReceiver() const;

  // Does this scope declare a "this" binding, and the "this" binding is stack-
  // or context-allocated?
  bool HasAllocatedReceiver() const;

  // Does this scope has class brand (for private methods)? If it's a class
  // scope, this indicates whether the class has a private brand. If it's a
  // constructor scope, this indicates whther it needs to initialize the
  // brand.
  bool ClassScopeHasPrivateBrand() const;

  // Does this scope contain a saved class variable for checking receivers of
  // static private methods?
  bool HasSavedClassVariable() const;

  // Does this scope declare a "new.target" binding?
  bool HasNewTarget() const;

  // Is this scope the scope of a named function expression?
  V8_EXPORT_PRIVATE bool HasFunctionName() const;

  bool HasContextAllocatedFunctionName() const;

  // See SharedFunctionInfo::HasSharedName.
  V8_EXPORT_PRIVATE bool HasSharedFunctionName() const;

  V8_EXPORT_PRIVATE bool HasInferredFunctionName() const;

  void SetFunctionName(Tagged<Object> name);
  void SetInferredFunctionName(Tagged<String> name);

  // Does this scope belong to a function?
  bool HasPositionInfo() const;

  // Return if contexts are allocated for this scope.
  bool HasContext() const;

  // Return if this is a function scope with "use asm".
  inline bool IsAsmModule() const;

  inline bool HasSimpleParameters() const;

  // Return the function_name if present.
  V8_EXPORT_PRIVATE Tagged<Object> FunctionName() const;

  // The function's name if it is non-empty, otherwise the inferred name or an
  // empty string.
  Tagged<String> FunctionDebugName() const;

  // Return the function's inferred name if present.
  // See SharedFunctionInfo::function_identifier.
  V8_EXPORT_PRIVATE Tagged<Object> InferredFunctionName() const;

  // Position information accessors.
  int StartPosition() const;
  int EndPosition() const;
  void SetPositionInfo(int start, int end);

  Tagged<SourceTextModuleInfo> ModuleDescriptorInfo() const;

  // Return true if the local names are inlined in the scope info object.
  inline bool HasInlinedLocalNames() const;

  template <typename ScopeInfoPtr>
  class LocalNamesRange;

  static inline LocalNamesRange<Handle<ScopeInfo>> IterateLocalNames(
      Handle<ScopeInfo> scope_info);

  static inline LocalNamesRange<Tagged<ScopeInfo>> IterateLocalNames(
      Tagged<ScopeInfo> scope_info, const DisallowGarbageCollection& no_gc);

  // Return the name of a given context local.
  // It should only be used if inlined local names.
  Tagged<String> ContextInlinedLocalName(int var) const;
  Tagged<String> ContextInlinedLocalName(PtrComprCageBase cage_base,
                                         int var) const;

  // Return the mode of the given context local.
  VariableMode ContextLocalMode(int var) const;

  // Return whether the given context local variable is static.
  IsStaticFlag ContextLocalIsStaticFlag(int var) const;

  // Return the initialization flag of the given context local.
  InitializationFlag ContextLocalInitFlag(int var) const;

  bool ContextLocalIsParameter(int var) const;
  uint32_t ContextLocalParameterNumber(int var) const;

  // Return the initialization flag of the given context local.
  MaybeAssignedFlag ContextLocalMaybeAssignedFlag(int var) const;

  // Return true if this local was introduced by the compiler, and should not be
  // exposed to the user in a debugger.
  static bool VariableIsSynthetic(Tagged<String> name);

  // Lookup support for serialized scope info. Returns the local context slot
  // index for a given slot name if the slot is present; otherwise
  // returns a value < 0. The name must be an internalized string.
  // If the slot is present and mode != nullptr, sets *mode to the corresponding
  // mode for that variable.
  int ContextSlotIndex(Handle<String> name);
  int ContextSlotIndex(Handle<String> name,
                       VariableLookupResult* lookup_result);

  // Lookup metadata of a MODULE-allocated variable.  Return 0 if there is no
  // module variable with the given name (the index value of a MODULE variable
  // is never 0).
  int ModuleIndex(Tagged<String> name, VariableMode* mode,
                  InitializationFlag* init_flag,
                  MaybeAssignedFlag* maybe_assigned_flag);

  int ModuleVariableCount() const;

  // Lookup support for serialized scope info. Returns the function context
  // slot index if the function name is present and context-allocated (named
  // function expressions, only), otherwise returns a value < 0. The name
  // must be an internalized string.
  int FunctionContextSlotIndex(Tagged<String> name) const;

  // Lookup support for serialized scope info.  Returns the receiver context
  // slot index if scope has a "this" binding, and the binding is
  // context-allocated.  Otherwise returns a value < 0.
  int ReceiverContextSlotIndex() const;

  // Returns the first parameter context slot index.
  int ParametersStartIndex() const;

  // Lookup support for serialized scope info.  Returns the name and index of
  // the saved class variable in context local slots if scope is a class scope
  // and it contains static private methods that may be accessed.
  std::pair<Tagged<String>, int> SavedClassVariable() const;

  FunctionKind function_kind() const;

  // Returns true if this ScopeInfo is linked to a outer ScopeInfo.
  bool HasOuterScopeInfo() const;

  // Returns true if this ScopeInfo was created for a debug-evaluate scope.
  bool IsDebugEvaluateScope() const;

  // Can be used to mark a ScopeInfo that looks like a with-scope as actually
  // being a debug-evaluate scope.
  void SetIsDebugEvaluateScope();

  // Return the outer ScopeInfo if present.
  Tagged<ScopeInfo> OuterScopeInfo() const;

  bool is_script_scope() const;

  // Returns true if this ScopeInfo has a blocklist attached containing stack
  // allocated local variables.
  V8_EXPORT_PRIVATE bool HasLocalsBlockList() const;
  // Returns a list of stack-allocated locals of parent scopes.
  // Used during local debug-evalute to decide whether a context lookup
  // can continue upwards after checking this scope.
  V8_EXPORT_PRIVATE Tagged<StringSet> LocalsBlockList() const;

  // Returns true if this ScopeInfo was created for a scope that skips the
  // closest outer class when resolving private names.
  bool PrivateNameLookupSkipsOuterClass() const;

  // REPL mode scopes allow re-declaraction of let and const variables. They
  // come from debug evaluate but are different to IsDebugEvaluateScope().
  bool IsReplModeScope() const;

#ifdef DEBUG
  // For LiveEdit we ignore:
  //   - position info: "unchanged" functions are allowed to move in a script
  //   - module info: SourceTextModuleInfo::Equals compares exact FixedArray
  //     addresses which will never match for separate instances.
  //   - outer scope info: LiveEdit already analyses outer scopes of unchanged
  //     functions. Also checking it here will break in really subtle cases
  //     e.g. changing a let to a const in an outer function, which is fine.
  bool Equals(Tagged<ScopeInfo> other, bool is_live_edit_compare = false) const;
#endif

  template <typename IsolateT>
  static Handle<ScopeInfo> Create(IsolateT* isolate, Zone* zone, Scope* scope,
                                  MaybeHandle<ScopeInfo> outer_scope);
  V8_EXPORT_PRIVATE static Handle<ScopeInfo> CreateForWithScope(
      Isolate* isolate, MaybeHandle<ScopeInfo> outer_scope);
  V8_EXPORT_PRIVATE static Handle<ScopeInfo> CreateForEmptyFunction(
      Isolate* isolate);
  static Handle<ScopeInfo> CreateForNativeContext(Isolate* isolate);
  static Handle<ScopeInfo> CreateForShadowRealmNativeContext(Isolate* isolate);
  static Handle<ScopeInfo> CreateGlobalThisBinding(Isolate* isolate);

  // Creates a copy of a {ScopeInfo} but with the provided locals blocklist
  // attached. Does nothing if the original {ScopeInfo} already has a field
  // for a blocklist reserved.
  V8_EXPORT_PRIVATE static Handle<ScopeInfo> RecreateWithBlockList(
      Isolate* isolate, Handle<ScopeInfo> original,
      Handle<StringSet> blocklist);

  // Serializes empty scope info.
  V8_EXPORT_PRIVATE static Tagged<ScopeInfo> Empty(Isolate* isolate);

#define FOR_EACH_SCOPE_INFO_NUMERIC_FIELD(V) \
  V(Flags)                                   \
  V(ParameterCount)                          \
  V(ContextLocalCount)

#define FIELD_ACCESSORS(name)       \
  inline int name() const;
  FOR_EACH_SCOPE_INFO_NUMERIC_FIELD(FIELD_ACCESSORS)
#undef FIELD_ACCESSORS

  enum Fields {
#define DECL_INDEX(name) k##name,
    FOR_EACH_SCOPE_INFO_NUMERIC_FIELD(DECL_INDEX)
#undef DECL_INDEX
        kVariablePartIndex
  };

  static_assert(LanguageModeSize == 1 << LanguageModeBit::kSize);
  static_assert(FunctionKind::kLastFunctionKind <= FunctionKindBits::kMax);

  bool IsEmpty() const;

  // Returns the size in bytes for a ScopeInfo with |length| slots.
  static constexpr int SizeFor(int length) { return OffsetOfElementAt(length); }

  // Gives access to raw memory which stores the ScopeInfo's data.
  inline ObjectSlot data_start();

  // Hash based on position info and flags. Falls back to flags + local count.
  V8_EXPORT_PRIVATE uint32_t Hash();

 private:
  int InlinedLocalNamesLookup(Tagged<String> name);

  int ContextLocalNamesIndex() const;
  int ContextLocalInfosIndex() const;
  int SavedClassVariableInfoIndex() const;
  int FunctionVariableInfoIndex() const;
  int InferredFunctionNameIndex() const;
  int PositionInfoIndex() const;
  int OuterScopeInfoIndex() const;
  V8_EXPORT_PRIVATE int LocalsBlockListIndex() const;
  int ModuleInfoIndex() const;
  int ModuleVariableCountIndex() const;
  int ModuleVariablesIndex() const;

  static bool NeedsPositionInfo(ScopeType type);

  // Raw access by slot index. These functions rely on the fact that everything
  // in ScopeInfo is tagged. Each slot is tagged-pointer sized. Slot 0 is
  // 'flags', the first field defined by ScopeInfo after the standard-size
  // HeapObject header.
  V8_EXPORT_PRIVATE Tagged<Object> get(int index) const;
  Tagged<Object> get(PtrComprCageBase cage_base, int index) const;
  // Setter that doesn't need write barrier.
  void set(int index, Tagged<Smi> value);
  // Setter with explicit barrier mode.
  void set(int index, Tagged<Object> value,
           WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  void CopyElements(Isolate* isolate, int dst_index, Tagged<ScopeInfo> src,
                    int src_index, int len, WriteBarrierMode mode);
  ObjectSlot RawFieldOfElementAt(int index);
  // The number of tagged-pointer-sized slots in the ScopeInfo after its
  // standard HeapObject header.
  V8_EXPORT_PRIVATE int length() const;

  // Conversions between offset (bytes from the beginning of the object) and
  // index (number of tagged-pointer-sized slots starting after the standard
  // HeapObject header).
  static constexpr int OffsetOfElementAt(int index) {
    return HeapObject::kHeaderSize + index * kTaggedSize;
  }
  static constexpr int ConvertOffsetToIndex(int offset) {
    int index = (offset - HeapObject::kHeaderSize) / kTaggedSize;
    DCHECK_EQ(OffsetOfElementAt(index), offset);
    return index;
  }

  enum class BootstrappingType { kScript, kFunction, kNative, kShadowRealm };
  static Handle<ScopeInfo> CreateForBootstrapping(Isolate* isolate,
                                                  BootstrappingType type);

  int Lookup(Handle<String> name, int start, int end, VariableMode* mode,
             VariableLocation* location, InitializationFlag* init_flag,
             MaybeAssignedFlag* maybe_assigned_flag);

  // Get metadata of i-th MODULE-allocated variable, where 0 <= i <
  // ModuleVariableCount.  The metadata is returned via out-arguments, which may
  // be nullptr if the corresponding information is not requested
  void ModuleVariable(int i, Tagged<String>* name, int* index,
                      VariableMode* mode = nullptr,
                      InitializationFlag* init_flag = nullptr,
                      MaybeAssignedFlag* maybe_assigned_flag = nullptr);

  static const int kFunctionNameEntries =
      TorqueGeneratedFunctionVariableInfoOffsets::kSize / kTaggedSize;
  static const int kPositionInfoEntries =
      TorqueGeneratedPositionInfoOffsets::kSize / kTaggedSize;
  static const int kModuleVariableEntryLength =
      TorqueGeneratedModuleVariableOffsets::kSize / kTaggedSize;

  // Properties of variables.
  DEFINE_TORQUE_GENERATED_VARIABLE_PROPERTIES()

  friend class ScopeIterator;
  friend std::ostream& operator<<(std::ostream& os, VariableAllocationInfo var);

  TQ_OBJECT_CONSTRUCTORS(ScopeInfo)
  FRIEND_TEST(TestWithNativeContext, RecreateScopeInfoWithLocalsBlocklistWorks);
};

std::ostream& operator<<(std::ostream& os, VariableAllocationInfo var);

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SCOPE_INFO_H_
