// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_
#define V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/abstract-code-inl.h"
#include "src/objects/debug-objects-inl.h"
#include "src/objects/feedback-vector-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/scope-info-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"
#include "src/objects/templates-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/shared-function-info-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(PreparseData)

int PreparseData::inner_start_offset() const {
  return InnerOffset(data_length());
}

ObjectSlot PreparseData::inner_data_start() const {
  return RawField(inner_start_offset());
}

void PreparseData::clear_padding() {
  int data_end_offset = kDataStartOffset + data_length();
  int padding_size = inner_start_offset() - data_end_offset;
  DCHECK_LE(0, padding_size);
  if (padding_size == 0) return;
  memset(reinterpret_cast<void*>(address() + data_end_offset), 0, padding_size);
}

uint8_t PreparseData::get(int index) const {
  DCHECK_LE(0, index);
  DCHECK_LT(index, data_length());
  int offset = kDataStartOffset + index * kByteSize;
  return ReadField<uint8_t>(offset);
}

void PreparseData::set(int index, uint8_t value) {
  DCHECK_LE(0, index);
  DCHECK_LT(index, data_length());
  int offset = kDataStartOffset + index * kByteSize;
  WriteField<uint8_t>(offset, value);
}

void PreparseData::copy_in(int index, const uint8_t* buffer, int length) {
  DCHECK(index >= 0 && length >= 0 && length <= kMaxInt - index &&
         index + length <= this->data_length());
  Address dst_addr = field_address(kDataStartOffset + index * kByteSize);
  memcpy(reinterpret_cast<void*>(dst_addr), buffer, length);
}

Tagged<PreparseData> PreparseData::get_child(int index) const {
  return PreparseData::cast(get_child_raw(index));
}

Tagged<Object> PreparseData::get_child_raw(int index) const {
  DCHECK_LE(0, index);
  DCHECK_LT(index, this->children_length());
  int offset = inner_start_offset() + index * kTaggedSize;
  return RELAXED_READ_FIELD(*this, offset);
}

void PreparseData::set_child(int index, Tagged<PreparseData> value,
                             WriteBarrierMode mode) {
  DCHECK_LE(0, index);
  DCHECK_LT(index, this->children_length());
  int offset = inner_start_offset() + index * kTaggedSize;
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

TQ_OBJECT_CONSTRUCTORS_IMPL(UncompiledData)
TQ_OBJECT_CONSTRUCTORS_IMPL(UncompiledDataWithoutPreparseData)
TQ_OBJECT_CONSTRUCTORS_IMPL(UncompiledDataWithPreparseData)
TQ_OBJECT_CONSTRUCTORS_IMPL(UncompiledDataWithoutPreparseDataWithJob)
TQ_OBJECT_CONSTRUCTORS_IMPL(UncompiledDataWithPreparseDataAndJob)

TQ_OBJECT_CONSTRUCTORS_IMPL(InterpreterData)
PROTECTED_POINTER_ACCESSORS(InterpreterData, bytecode_array, BytecodeArray,
                            kBytecodeArrayOffset)
PROTECTED_POINTER_ACCESSORS(InterpreterData, interpreter_trampoline, Code,
                            kInterpreterTrampolineOffset)

TQ_OBJECT_CONSTRUCTORS_IMPL(SharedFunctionInfo)
DEFINE_DEOPT_ELEMENT_ACCESSORS(SharedFunctionInfo, Tagged<Object>)

RELEASE_ACQUIRE_ACCESSORS(SharedFunctionInfo, name_or_scope_info,
                          Tagged<Object>, kNameOrScopeInfoOffset)
RELEASE_ACQUIRE_ACCESSORS(SharedFunctionInfo, script, Tagged<HeapObject>,
                          kScriptOffset)
RELEASE_ACQUIRE_ACCESSORS(SharedFunctionInfo, raw_script, Tagged<Object>,
                          kScriptOffset)

void SharedFunctionInfo::SetData(Tagged<Object> value, ReleaseStoreTag tag,
                                 DataType type, WriteBarrierMode mode) {
#ifdef V8_ENABLE_SANDBOX
  if (type == DataType::kTrusted) {
    DCHECK(IsExposedTrustedObject(value));
    // Currently we only support BytecodeArray, InterpreterData, and Code.
    // HasBytecodeArray relies on that.
    DCHECK(IsBytecodeArray(value) || IsInterpreterData(value) || IsCode(value));
    // Only one of trusted_function_data and function_data can be in use.
    set_trusted_function_data(ExposedTrustedObject::cast(value), tag, mode);
    clear_function_data(kReleaseStore);
  } else {
    DCHECK_EQ(type, DataType::kRegular);
    // Only one of trusted_function_data and function_data can be in use.
    set_function_data(value, tag, mode);
    clear_trusted_function_data(kReleaseStore);
  }
#else
  set_function_data(value, tag, mode);
#endif  // V8_ENABLE_SANDBOX
}

#ifdef V8_ENABLE_SANDBOX
// TODO(saelo): consider using a unique magic value here instead. However,
// using -1 has some benefits such as a recognizable crashing address if this
// field was ever accidentally treated as a HeapObject.
constexpr int kClearedFunctionDataValue = -1;
void SharedFunctionInfo::clear_function_data(ReleaseStoreTag) {
  TaggedField<Object, kFunctionDataOffset>::Release_Store(
      *this, Smi::FromInt(kClearedFunctionDataValue));
}

void SharedFunctionInfo::clear_trusted_function_data(ReleaseStoreTag) {
  TaggedField<Object, kTrustedFunctionDataOffset>::Release_Store(*this,
                                                                 Smi::zero());
}

bool SharedFunctionInfo::has_trusted_function_data() const {
  return TaggedField<Object, kTrustedFunctionDataOffset>::Relaxed_Load(*this) !=
         Smi::zero();
}

Tagged<ExposedTrustedObject> SharedFunctionInfo::trusted_function_data(
    IsolateForSandbox isolate, AcquireLoadTag) const {
  Tagged<Object> trusted_data =
      ReadIndirectPointerField<kUnknownIndirectPointerTag>(
          kTrustedFunctionDataOffset, isolate);
  return ExposedTrustedObject::cast(trusted_data);
}

void SharedFunctionInfo::set_trusted_function_data(
    Tagged<ExposedTrustedObject> value, ReleaseStoreTag,
    WriteBarrierMode mode) {
  WriteIndirectPointerField<kUnknownIndirectPointerTag>(
      kTrustedFunctionDataOffset, value);
  CONDITIONAL_INDIRECT_POINTER_WRITE_BARRIER(*this, kTrustedFunctionDataOffset,
                                             kUnknownIndirectPointerTag, value,
                                             mode);
}

IndirectPointerHandle SharedFunctionInfo::trusted_function_data_handle(
    AcquireLoadTag) const {
  return RawIndirectPointerField(kTrustedFunctionDataOffset,
                                 kUnknownIndirectPointerTag)
      .Acquire_LoadHandle();
}
#endif  // V8_ENABLE_SANDBOX

DEF_ACQUIRE_GETTER(SharedFunctionInfo, function_data, Tagged<Object>) {
  Tagged<Object> value =
      TaggedField<Object, kFunctionDataOffset>::Acquire_Load(cage_base, *this);
  return value;
}

void SharedFunctionInfo::set_function_data(Tagged<Object> value,
                                           ReleaseStoreTag,
                                           WriteBarrierMode mode) {
  TaggedField<Object, kFunctionDataOffset>::Release_Store(*this, value);
  CONDITIONAL_WRITE_BARRIER(*this, kFunctionDataOffset, value, mode);
}

Tagged<Object> SharedFunctionInfo::GetData(IsolateForSandbox isolate) const {
#ifdef V8_ENABLE_SANDBOX
  auto trusted_data_slot = RawIndirectPointerField(kTrustedFunctionDataOffset,
                                                   kUnknownIndirectPointerTag);
  IndirectPointerHandle trusted_data_handle =
      trusted_data_slot.Acquire_LoadHandle();
  if (trusted_data_handle != kNullIndirectPointerHandle) {
    return trusted_data_slot.ResolveHandle(trusted_data_handle, isolate);
  }
#endif
  return function_data(kAcquireLoad);
}

DEF_GETTER(SharedFunctionInfo, script, Tagged<HeapObject>) {
  return script(cage_base, kAcquireLoad);
}
bool SharedFunctionInfo::has_script(AcquireLoadTag tag) const {
  return IsScript(script(tag));
}

RENAME_TORQUE_ACCESSORS(SharedFunctionInfo,
                        raw_outer_scope_info_or_feedback_metadata,
                        outer_scope_info_or_feedback_metadata,
                        Tagged<HeapObject>)
DEF_ACQUIRE_GETTER(SharedFunctionInfo,
                   raw_outer_scope_info_or_feedback_metadata,
                   Tagged<HeapObject>) {
  Tagged<HeapObject> value =
      TaggedField<HeapObject, kOuterScopeInfoOrFeedbackMetadataOffset>::
          Acquire_Load(cage_base, *this);
  return value;
}

uint16_t SharedFunctionInfo::internal_formal_parameter_count_with_receiver()
    const {
  const uint16_t param_count = TorqueGeneratedClass::formal_parameter_count();
  return param_count;
}

uint16_t SharedFunctionInfo::internal_formal_parameter_count_without_receiver()
    const {
  const uint16_t param_count = TorqueGeneratedClass::formal_parameter_count();
  if (param_count == kDontAdaptArgumentsSentinel) return param_count;
  return param_count - kJSArgcReceiverSlots;
}

void SharedFunctionInfo::set_internal_formal_parameter_count(int value) {
  DCHECK_EQ(value, static_cast<uint16_t>(value));
  DCHECK_GE(value, kJSArgcReceiverSlots);
  TorqueGeneratedClass::set_formal_parameter_count(value);
}

RENAME_PRIMITIVE_TORQUE_ACCESSORS(SharedFunctionInfo, raw_function_token_offset,
                                  function_token_offset, uint16_t)

RELAXED_INT32_ACCESSORS(SharedFunctionInfo, flags, kFlagsOffset)
int32_t SharedFunctionInfo::relaxed_flags() const {
  return flags(kRelaxedLoad);
}
void SharedFunctionInfo::set_relaxed_flags(int32_t flags) {
  return set_flags(flags, kRelaxedStore);
}

UINT8_ACCESSORS(SharedFunctionInfo, flags2, kFlags2Offset)

bool SharedFunctionInfo::HasSharedName() const {
  Tagged<Object> value = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(value)) {
    return ScopeInfo::cast(value)->HasSharedFunctionName();
  }
  return value != kNoSharedNameSentinel;
}

Tagged<String> SharedFunctionInfo::Name() const {
  if (!HasSharedName()) return GetReadOnlyRoots().empty_string();
  Tagged<Object> value = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(value)) {
    if (ScopeInfo::cast(value)->HasFunctionName()) {
      return String::cast(ScopeInfo::cast(value)->FunctionName());
    }
    return GetReadOnlyRoots().empty_string();
  }
  return String::cast(value);
}

void SharedFunctionInfo::SetName(Tagged<String> name) {
  Tagged<Object> maybe_scope_info = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(maybe_scope_info)) {
    ScopeInfo::cast(maybe_scope_info)->SetFunctionName(name);
  } else {
    DCHECK(IsString(maybe_scope_info) ||
           maybe_scope_info == kNoSharedNameSentinel);
    set_name_or_scope_info(name, kReleaseStore);
  }
  UpdateFunctionMapIndex();
}

bool SharedFunctionInfo::is_script() const {
  return scope_info(kAcquireLoad)->is_script_scope() &&
         Script::cast(script())->compilation_type() ==
             Script::CompilationType::kHost;
}

bool SharedFunctionInfo::needs_script_context() const {
  return is_script() && scope_info(kAcquireLoad)->ContextLocalCount() > 0;
}

Tagged<AbstractCode> SharedFunctionInfo::abstract_code(Isolate* isolate) {
  // TODO(v8:11429): Decide if this return bytecode or baseline code, when the
  // latter is present.
  if (HasBytecodeArray(isolate)) {
    return AbstractCode::cast(GetBytecodeArray(isolate));
  } else {
    return AbstractCode::cast(GetCode(isolate));
  }
}

int SharedFunctionInfo::function_token_position() const {
  int offset = raw_function_token_offset();
  if (offset == kFunctionTokenOutOfRange) {
    return kNoSourcePosition;
  } else {
    return StartPosition() - offset;
  }
}

template <typename IsolateT>
bool SharedFunctionInfo::AreSourcePositionsAvailable(IsolateT* isolate) const {
  if (v8_flags.enable_lazy_source_positions) {
    return !HasBytecodeArray() ||
           GetBytecodeArray(isolate)->HasSourcePositionTable();
  }
  return true;
}

template <typename IsolateT>
SharedFunctionInfo::Inlineability SharedFunctionInfo::GetInlineability(
    IsolateT* isolate) const {
  if (!IsScript(script())) return kHasNoScript;

  if (isolate->is_precise_binary_code_coverage() &&
      !has_reported_binary_coverage()) {
    // We may miss invocations if this function is inlined.
    return kNeedsBinaryCoverage;
  }

  // Built-in functions are handled by the JSCallReducer.
  if (HasBuiltinId()) return kIsBuiltin;

  if (!IsUserJavaScript()) return kIsNotUserCode;

  // If there is no bytecode array, it is either not compiled or it is compiled
  // with WebAssembly for the asm.js pipeline. In either case we don't want to
  // inline.
  if (!HasBytecodeArray()) return kHasNoBytecode;

  if (GetBytecodeArray(isolate)->length() >
      v8_flags.max_inlined_bytecode_size) {
    return kExceedsBytecodeLimit;
  }

  {
    SharedMutexGuardIfOffThread<IsolateT, base::kShared> mutex_guard(
        isolate->shared_function_info_access(), isolate);
    if (HasBreakInfo(isolate->GetMainThreadIsolateUnsafe())) {
      return kMayContainBreakPoints;
    }
  }

  if (optimization_disabled()) return kHasOptimizationDisabled;

  return kIsInlineable;
}

BIT_FIELD_ACCESSORS(SharedFunctionInfo, flags2, class_scope_has_private_brand,
                    SharedFunctionInfo::ClassScopeHasPrivateBrandBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, flags2,
                    has_static_private_methods_or_accessors,
                    SharedFunctionInfo::HasStaticPrivateMethodsOrAccessorsBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, flags2, is_sparkplug_compiling,
                    SharedFunctionInfo::IsSparkplugCompilingBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, flags2, maglev_compilation_failed,
                    SharedFunctionInfo::MaglevCompilationFailedBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, flags2, sparkplug_compiled,
                    SharedFunctionInfo::SparkplugCompiledBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, syntax_kind,
                    SharedFunctionInfo::FunctionSyntaxKindBits)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, allows_lazy_compilation,
                    SharedFunctionInfo::AllowLazyCompilationBit)
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, has_duplicate_parameters,
                    SharedFunctionInfo::HasDuplicateParametersBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, native,
                    SharedFunctionInfo::IsNativeBit)
#if V8_ENABLE_WEBASSEMBLY
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, is_asm_wasm_broken,
                    SharedFunctionInfo::IsAsmWasmBrokenBit)
#endif  // V8_ENABLE_WEBASSEMBLY
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags,
                    requires_instance_members_initializer,
                    SharedFunctionInfo::RequiresInstanceMembersInitializerBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags,
                    name_should_print_as_anonymous,
                    SharedFunctionInfo::NameShouldPrintAsAnonymousBit)
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags,
                    has_reported_binary_coverage,
                    SharedFunctionInfo::HasReportedBinaryCoverageBit)

BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, is_toplevel,
                    SharedFunctionInfo::IsTopLevelBit)
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags, properties_are_final,
                    SharedFunctionInfo::PropertiesAreFinalBit)
BIT_FIELD_ACCESSORS(SharedFunctionInfo, relaxed_flags,
                    private_name_lookup_skips_outer_class,
                    SharedFunctionInfo::PrivateNameLookupSkipsOuterClassBit)

bool SharedFunctionInfo::optimization_disabled() const {
  return disabled_optimization_reason() != BailoutReason::kNoReason;
}

BailoutReason SharedFunctionInfo::disabled_optimization_reason() const {
  return DisabledOptimizationReasonBits::decode(flags(kRelaxedLoad));
}

LanguageMode SharedFunctionInfo::language_mode() const {
  static_assert(LanguageModeSize == 2);
  return construct_language_mode(IsStrictBit::decode(flags(kRelaxedLoad)));
}

void SharedFunctionInfo::set_language_mode(LanguageMode language_mode) {
  static_assert(LanguageModeSize == 2);
  // We only allow language mode transitions that set the same language mode
  // again or go up in the chain:
  DCHECK(is_sloppy(this->language_mode()) || is_strict(language_mode));
  int hints = flags(kRelaxedLoad);
  hints = IsStrictBit::update(hints, is_strict(language_mode));
  set_flags(hints, kRelaxedStore);
  UpdateFunctionMapIndex();
}

FunctionKind SharedFunctionInfo::kind() const {
  static_assert(FunctionKindBits::kSize == kFunctionKindBitSize);
  return FunctionKindBits::decode(flags(kRelaxedLoad));
}

void SharedFunctionInfo::set_kind(FunctionKind kind) {
  int hints = flags(kRelaxedLoad);
  hints = FunctionKindBits::update(hints, kind);
  hints = IsClassConstructorBit::update(hints, IsClassConstructor(kind));
  set_flags(hints, kRelaxedStore);
  UpdateFunctionMapIndex();
}

bool SharedFunctionInfo::is_wrapped() const {
  return syntax_kind() == FunctionSyntaxKind::kWrapped;
}

bool SharedFunctionInfo::construct_as_builtin() const {
  return ConstructAsBuiltinBit::decode(flags(kRelaxedLoad));
}

void SharedFunctionInfo::CalculateConstructAsBuiltin() {
  bool uses_builtins_construct_stub = false;
  if (HasBuiltinId()) {
    Builtin id = builtin_id();
    if (id != Builtin::kCompileLazy && id != Builtin::kEmptyFunction) {
      uses_builtins_construct_stub = true;
    }
  } else if (IsApiFunction()) {
    uses_builtins_construct_stub = true;
  }

  int f = flags(kRelaxedLoad);
  f = ConstructAsBuiltinBit::update(f, uses_builtins_construct_stub);
  set_flags(f, kRelaxedStore);
}

uint16_t SharedFunctionInfo::age() const {
  return RELAXED_READ_UINT16_FIELD(*this, kAgeOffset);
}

void SharedFunctionInfo::set_age(uint16_t value) {
  RELAXED_WRITE_UINT16_FIELD(*this, kAgeOffset, value);
}

uint16_t SharedFunctionInfo::CompareExchangeAge(uint16_t expected_age,
                                                uint16_t new_age) {
  Address age_addr = address() + kAgeOffset;
  return base::AsAtomic16::Relaxed_CompareAndSwap(
      reinterpret_cast<base::Atomic16*>(age_addr), expected_age, new_age);
}

int SharedFunctionInfo::function_map_index() const {
  // Note: Must be kept in sync with the FastNewClosure builtin.
  int index = Context::FIRST_FUNCTION_MAP_INDEX +
              FunctionMapIndexBits::decode(flags(kRelaxedLoad));
  DCHECK_LE(index, Context::LAST_FUNCTION_MAP_INDEX);
  return index;
}

void SharedFunctionInfo::set_function_map_index(int index) {
  static_assert(Context::LAST_FUNCTION_MAP_INDEX <=
                Context::FIRST_FUNCTION_MAP_INDEX + FunctionMapIndexBits::kMax);
  DCHECK_LE(Context::FIRST_FUNCTION_MAP_INDEX, index);
  DCHECK_LE(index, Context::LAST_FUNCTION_MAP_INDEX);
  index -= Context::FIRST_FUNCTION_MAP_INDEX;
  set_flags(FunctionMapIndexBits::update(flags(kRelaxedLoad), index),
            kRelaxedStore);
}

void SharedFunctionInfo::clear_padding() { set_padding(0); }

void SharedFunctionInfo::UpdateFunctionMapIndex() {
  int map_index =
      Context::FunctionMapIndex(language_mode(), kind(), HasSharedName());
  set_function_map_index(map_index);
}

void SharedFunctionInfo::DontAdaptArguments() {
#if V8_ENABLE_WEBASSEMBLY
  // TODO(leszeks): Revise this DCHECK now that the code field is gone.
  DCHECK(!HasWasmExportedFunctionData());
#endif  // V8_ENABLE_WEBASSEMBLY
  TorqueGeneratedClass::set_formal_parameter_count(kDontAdaptArgumentsSentinel);
}

bool SharedFunctionInfo::IsDontAdaptArguments() const {
  return TorqueGeneratedClass::formal_parameter_count() ==
         kDontAdaptArgumentsSentinel;
}

DEF_ACQUIRE_GETTER(SharedFunctionInfo, scope_info, Tagged<ScopeInfo>) {
  Tagged<Object> maybe_scope_info = name_or_scope_info(cage_base, kAcquireLoad);
  if (IsScopeInfo(maybe_scope_info, cage_base)) {
    return ScopeInfo::cast(maybe_scope_info);
  }
  return GetReadOnlyRoots().empty_scope_info();
}

DEF_GETTER(SharedFunctionInfo, scope_info, Tagged<ScopeInfo>) {
  return scope_info(cage_base, kAcquireLoad);
}

Tagged<ScopeInfo> SharedFunctionInfo::EarlyScopeInfo(AcquireLoadTag tag) {
  // Keep in sync with the scope_info getter above.
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  Tagged<Object> maybe_scope_info = name_or_scope_info(cage_base, tag);
  if (IsScopeInfo(maybe_scope_info, cage_base)) {
    return ScopeInfo::cast(maybe_scope_info);
  }
  return EarlyGetReadOnlyRoots().empty_scope_info();
}

void SharedFunctionInfo::SetScopeInfo(Tagged<ScopeInfo> scope_info,
                                      WriteBarrierMode mode) {
  // Move the existing name onto the ScopeInfo.
  Tagged<Object> name = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(name)) {
    name = ScopeInfo::cast(name)->FunctionName();
  }
  DCHECK(IsString(name) || name == kNoSharedNameSentinel);
  // Only set the function name for function scopes.
  scope_info->SetFunctionName(name);
  if (HasInferredName() && inferred_name()->length() != 0) {
    scope_info->SetInferredFunctionName(inferred_name());
  }
  set_name_or_scope_info(scope_info, kReleaseStore, mode);
}

void SharedFunctionInfo::set_raw_scope_info(Tagged<ScopeInfo> scope_info,
                                            WriteBarrierMode mode) {
  WRITE_FIELD(*this, kNameOrScopeInfoOffset, scope_info);
  CONDITIONAL_WRITE_BARRIER(*this, kNameOrScopeInfoOffset, scope_info, mode);
}

DEF_GETTER(SharedFunctionInfo, outer_scope_info, Tagged<HeapObject>) {
  DCHECK(!is_compiled());
  DCHECK(!HasFeedbackMetadata());
  return raw_outer_scope_info_or_feedback_metadata(cage_base);
}

bool SharedFunctionInfo::HasOuterScopeInfo() const {
  Tagged<ScopeInfo> outer_info;
  if (!is_compiled()) {
    if (!IsScopeInfo(outer_scope_info())) return false;
    outer_info = ScopeInfo::cast(outer_scope_info());
  } else {
    Tagged<ScopeInfo> info = scope_info(kAcquireLoad);
    if (!info->HasOuterScopeInfo()) return false;
    outer_info = info->OuterScopeInfo();
  }
  return !outer_info->IsEmpty();
}

Tagged<ScopeInfo> SharedFunctionInfo::GetOuterScopeInfo() const {
  DCHECK(HasOuterScopeInfo());
  if (!is_compiled()) return ScopeInfo::cast(outer_scope_info());
  return scope_info(kAcquireLoad)->OuterScopeInfo();
}

void SharedFunctionInfo::set_outer_scope_info(Tagged<HeapObject> value,
                                              WriteBarrierMode mode) {
  DCHECK(!is_compiled());
  DCHECK(IsTheHole(raw_outer_scope_info_or_feedback_metadata()));
  DCHECK(IsScopeInfo(value) || IsTheHole(value));
  set_raw_outer_scope_info_or_feedback_metadata(value, mode);
}

bool SharedFunctionInfo::HasFeedbackMetadata() const {
  return IsFeedbackMetadata(raw_outer_scope_info_or_feedback_metadata());
}

bool SharedFunctionInfo::HasFeedbackMetadata(AcquireLoadTag tag) const {
  return IsFeedbackMetadata(raw_outer_scope_info_or_feedback_metadata(tag));
}

DEF_GETTER(SharedFunctionInfo, feedback_metadata, Tagged<FeedbackMetadata>) {
  DCHECK(HasFeedbackMetadata());
  return FeedbackMetadata::cast(
      raw_outer_scope_info_or_feedback_metadata(cage_base));
}

RELEASE_ACQUIRE_ACCESSORS_CHECKED2(SharedFunctionInfo, feedback_metadata,
                                   Tagged<FeedbackMetadata>,
                                   kOuterScopeInfoOrFeedbackMetadataOffset,
                                   HasFeedbackMetadata(kAcquireLoad),
                                   !HasFeedbackMetadata(kAcquireLoad) &&
                                       IsFeedbackMetadata(value))

bool SharedFunctionInfo::is_compiled() const {
  Tagged<Object> data = function_data(kAcquireLoad);
  return data != Smi::FromEnum(Builtin::kCompileLazy) &&
         !IsUncompiledData(data);
}

template <typename IsolateT>
IsCompiledScope SharedFunctionInfo::is_compiled_scope(IsolateT* isolate) const {
  return IsCompiledScope(*this, isolate);
}

IsCompiledScope::IsCompiledScope(const Tagged<SharedFunctionInfo> shared,
                                 Isolate* isolate)
    : is_compiled_(shared->is_compiled()) {
  if (shared->HasBaselineCode()) {
    retain_code_ = handle(shared->baseline_code(kAcquireLoad), isolate);
  } else if (shared->HasBytecodeArray()) {
    retain_code_ = handle(shared->GetBytecodeArray(isolate), isolate);
  } else {
    retain_code_ = MaybeHandle<HeapObject>();
  }

  DCHECK_IMPLIES(!retain_code_.is_null(), is_compiled());
}

IsCompiledScope::IsCompiledScope(const Tagged<SharedFunctionInfo> shared,
                                 LocalIsolate* isolate)
    : is_compiled_(shared->is_compiled()) {
  if (shared->HasBaselineCode()) {
    retain_code_ = isolate->heap()->NewPersistentHandle(
        shared->baseline_code(kAcquireLoad));
  } else if (shared->HasBytecodeArray()) {
    retain_code_ =
        isolate->heap()->NewPersistentHandle(shared->GetBytecodeArray(isolate));
  } else {
    retain_code_ = MaybeHandle<HeapObject>();
  }

  DCHECK_IMPLIES(!retain_code_.is_null(), is_compiled());
}

bool SharedFunctionInfo::has_simple_parameters() {
  return scope_info(kAcquireLoad)->HasSimpleParameters();
}

bool SharedFunctionInfo::CanCollectSourcePosition(Isolate* isolate) {
  return v8_flags.enable_lazy_source_positions && HasBytecodeArray() &&
         !GetBytecodeArray(isolate)->HasSourcePositionTable();
}

bool SharedFunctionInfo::IsApiFunction() const {
  return IsFunctionTemplateInfo(function_data(kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, api_func_data, Tagged<FunctionTemplateInfo>) {
  DCHECK(IsApiFunction());
  return FunctionTemplateInfo::cast(function_data(kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, HasBytecodeArray, bool) {
#ifdef V8_ENABLE_SANDBOX
  // The trusted_function_data field contains one of BytecodeArray, Code or
  // InterpreterData (there is a DCHECK in SetData that guards this), so if it
  // is set, we know that we have bytecode. If this ever changes, we could
  // consider introducing a new flag to indicate whether bytecode is available.
  return trusted_function_data_handle(kAcquireLoad) !=
         kNullIndirectPointerHandle;
#else
  Tagged<Object> data = function_data(kAcquireLoad);
  if (!IsHeapObject(data)) return false;
  InstanceType instance_type =
      HeapObject::cast(data)->map(cage_base)->instance_type();
  return InstanceTypeChecker::IsBytecodeArray(instance_type) ||
         InstanceTypeChecker::IsInterpreterData(instance_type) ||
         InstanceTypeChecker::IsCode(instance_type);
#endif
}

template <typename IsolateT>
Tagged<BytecodeArray> SharedFunctionInfo::GetBytecodeArray(
    IsolateT* isolate) const {
  SharedMutexGuardIfOffThread<IsolateT, base::kShared> mutex_guard(
      isolate->shared_function_info_access(), isolate);

  DCHECK(HasBytecodeArray());

  Isolate* main_isolate = isolate->GetMainThreadIsolateUnsafe();
  base::Optional<Tagged<DebugInfo>> debug_info = TryGetDebugInfo(main_isolate);
  if (debug_info.has_value() &&
      debug_info.value()->HasInstrumentedBytecodeArray()) {
    return debug_info.value()->OriginalBytecodeArray(main_isolate);
  }

  return GetActiveBytecodeArray(main_isolate);
}

Tagged<BytecodeArray> SharedFunctionInfo::GetActiveBytecodeArray(
    IsolateForSandbox isolate) const {
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, the bytecode array must be loaded via the
  // trusted pointer field. Loading it from the (tagged) function_data field
  // would not be safe. Here we can't use ReadTrustedPointerField but instead
  // need an acquire load of the handle to synchronize with the release store in
  // set_trusted_function_data. Technically, we'd only need memory_order_consume
  // since we're dealing with dependent loads, but that ordering is not
  // currently supported (and that's why ReadTrustedPointerField uses a relaxed
  // load, but TSan doesn't like that).
  auto trusted_data_slot = RawIndirectPointerField(kTrustedFunctionDataOffset,
                                                   kUnknownIndirectPointerTag);
  IndirectPointerHandle trusted_data_handle =
      trusted_data_slot.Acquire_LoadHandle();
  Tagged<Object> data =
      trusted_data_slot.ResolveHandle(trusted_data_handle, isolate);
#else
  Tagged<Object> data = function_data(kAcquireLoad);
#endif  // V8_ENABLE_SANDBOX
  if (IsCode(data)) {
    Tagged<Code> baseline_code = Code::cast(data);
    data = baseline_code->bytecode_or_interpreter_data();
  }
  if (IsBytecodeArray(data)) {
    return BytecodeArray::cast(data);
  } else {
    DCHECK(IsInterpreterData(data));
    return InterpreterData::cast(data)->bytecode_array();
  }
}

void SharedFunctionInfo::SetActiveBytecodeArray(Tagged<BytecodeArray> bytecode,
                                                IsolateForSandbox isolate) {
  // We don't allow setting the active bytecode array on baseline-optimized
  // functions. They should have been flushed earlier.
  DCHECK(!HasBaselineCode());

  if (HasInterpreterData(isolate)) {
    interpreter_data(isolate)->set_bytecode_array(bytecode);
  } else {
    DCHECK(HasBytecodeArray());
    overwrite_bytecode_array(bytecode);
  }
}

void SharedFunctionInfo::set_bytecode_array(Tagged<BytecodeArray> bytecode) {
  DCHECK(function_data(kAcquireLoad) == Smi::FromEnum(Builtin::kCompileLazy) ||
         HasUncompiledData());
  SetData(bytecode, kReleaseStore, DataType::kTrusted);
}

void SharedFunctionInfo::overwrite_bytecode_array(
    Tagged<BytecodeArray> bytecode) {
  DCHECK(HasBytecodeArray());
  SetData(bytecode, kReleaseStore, DataType::kTrusted);
}

Tagged<Code> SharedFunctionInfo::InterpreterTrampoline(
    IsolateForSandbox isolate) const {
  DCHECK(HasInterpreterData(isolate));
  return interpreter_data(isolate)->interpreter_trampoline();
}

bool SharedFunctionInfo::HasInterpreterData(IsolateForSandbox isolate) const {
  Tagged<Object> data = GetData(isolate);
  if (IsCode(data)) {
    Tagged<Code> baseline_code = Code::cast(data);
    DCHECK_EQ(baseline_code->kind(), CodeKind::BASELINE);
    data = baseline_code->bytecode_or_interpreter_data();
  }
  return IsInterpreterData(data);
}

Tagged<InterpreterData> SharedFunctionInfo::interpreter_data(
    IsolateForSandbox isolate) const {
  DCHECK(HasInterpreterData(isolate));
#ifdef V8_ENABLE_SANDBOX
  Tagged<Object> data = trusted_function_data(isolate, kAcquireLoad);
#else
  Tagged<Object> data = function_data(kAcquireLoad);
#endif
  if (IsCode(data)) {
    Tagged<Code> baseline_code = Code::cast(data);
    DCHECK_EQ(baseline_code->kind(), CodeKind::BASELINE);
    data = baseline_code->bytecode_or_interpreter_data();
  }
  return InterpreterData::cast(data);
}

void SharedFunctionInfo::set_interpreter_data(
    Tagged<InterpreterData> interpreter_data, WriteBarrierMode mode) {
  DCHECK(v8_flags.interpreted_frames_native_stack);
  DCHECK(!HasBaselineCode());
  SetData(interpreter_data, kReleaseStore, DataType::kTrusted, mode);
}

DEF_GETTER(SharedFunctionInfo, HasBaselineCode, bool) {
#ifdef V8_ENABLE_SANDBOX
  // Micro-optimization: we just need to look at the indirect pointer handle
  // stored in the trusted_function_data field and check if that is a code
  // pointer handle.
  IndirectPointerHandle handle = trusted_function_data_handle(kAcquireLoad);
  if (handle & kCodePointerHandleMarker) {
    DCHECK_EQ(Code::cast(GetData(GetIsolateForSandbox(*this)))->kind(),
              CodeKind::BASELINE);
    return true;
  }
  return false;
#else
  Tagged<Object> data = function_data(cage_base, kAcquireLoad);
  if (IsCode(data, cage_base)) {
    DCHECK_EQ(Code::cast(data)->kind(), CodeKind::BASELINE);
    return true;
  }
  return false;
#endif
}

DEF_ACQUIRE_GETTER(SharedFunctionInfo, baseline_code, Tagged<Code>) {
  DCHECK(HasBaselineCode(cage_base));
#ifdef V8_ENABLE_SANDBOX
  return Code::cast(
      trusted_function_data(GetIsolateForSandbox(*this), kAcquireLoad));
#else
  return Code::cast(function_data(cage_base, kAcquireLoad));
#endif
}

void SharedFunctionInfo::set_baseline_code(Tagged<Code> baseline_code,
                                           ReleaseStoreTag tag,
                                           WriteBarrierMode mode) {
  DCHECK_EQ(baseline_code->kind(), CodeKind::BASELINE);
  SetData(baseline_code, tag, DataType::kTrusted, mode);
}

void SharedFunctionInfo::FlushBaselineCode() {
  DCHECK(HasBaselineCode());
  SetData(baseline_code(kAcquireLoad)->bytecode_or_interpreter_data(),
          kReleaseStore, DataType::kTrusted);
}

#if V8_ENABLE_WEBASSEMBLY
bool SharedFunctionInfo::HasAsmWasmData() const {
  return IsAsmWasmData(function_data(kAcquireLoad));
}

bool SharedFunctionInfo::HasWasmFunctionData() const {
  return IsWasmFunctionData(function_data(kAcquireLoad));
}

bool SharedFunctionInfo::HasWasmExportedFunctionData() const {
  return IsWasmExportedFunctionData(function_data(kAcquireLoad));
}

bool SharedFunctionInfo::HasWasmJSFunctionData() const {
  return IsWasmJSFunctionData(function_data(kAcquireLoad));
}

bool SharedFunctionInfo::HasWasmCapiFunctionData() const {
  return IsWasmCapiFunctionData(function_data(kAcquireLoad));
}

bool SharedFunctionInfo::HasWasmResumeData() const {
  return IsWasmResumeData(function_data(kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, asm_wasm_data, Tagged<AsmWasmData>) {
  DCHECK(HasAsmWasmData());
  return AsmWasmData::cast(function_data(kAcquireLoad));
}

void SharedFunctionInfo::set_asm_wasm_data(Tagged<AsmWasmData> data,
                                           WriteBarrierMode mode) {
  DCHECK(function_data(kAcquireLoad) == Smi::FromEnum(Builtin::kCompileLazy) ||
         HasUncompiledData() || HasAsmWasmData());
  SetData(data, kReleaseStore, DataType::kRegular, mode);
}

const wasm::WasmModule* SharedFunctionInfo::wasm_module() const {
  if (!HasWasmExportedFunctionData()) return nullptr;
  Tagged<WasmExportedFunctionData> function_data =
      wasm_exported_function_data();
  Tagged<WasmInstanceObject> wasm_instance = function_data->instance();
  Tagged<WasmModuleObject> wasm_module_object = wasm_instance->module_object();
  return wasm_module_object->module();
}

const wasm::FunctionSig* SharedFunctionInfo::wasm_function_signature() const {
  const wasm::WasmModule* module = wasm_module();
  if (!module) return nullptr;
  Tagged<WasmExportedFunctionData> function_data =
      wasm_exported_function_data();
  DCHECK_LT(function_data->function_index(), module->functions.size());
  return module->functions[function_data->function_index()].sig;
}

int SharedFunctionInfo::wasm_function_index() const {
  if (!HasWasmExportedFunctionData()) return -1;
  Tagged<WasmExportedFunctionData> function_data =
      wasm_exported_function_data();
  DCHECK_GE(function_data->function_index(), 0);
  DCHECK_LT(function_data->function_index(), wasm_module()->functions.size());
  return function_data->function_index();
}

DEF_GETTER(SharedFunctionInfo, wasm_function_data, Tagged<WasmFunctionData>) {
  DCHECK(HasWasmFunctionData());
  return WasmFunctionData::cast(function_data(cage_base, kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, wasm_exported_function_data,
           Tagged<WasmExportedFunctionData>) {
  DCHECK(HasWasmExportedFunctionData());
  return WasmExportedFunctionData::cast(function_data(cage_base, kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, wasm_js_function_data,
           Tagged<WasmJSFunctionData>) {
  DCHECK(HasWasmJSFunctionData());
  return WasmJSFunctionData::cast(function_data(cage_base, kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, wasm_capi_function_data,
           Tagged<WasmCapiFunctionData>) {
  DCHECK(HasWasmCapiFunctionData());
  return WasmCapiFunctionData::cast(function_data(cage_base, kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, wasm_resume_data, Tagged<WasmResumeData>) {
  DCHECK(HasWasmResumeData());
  return WasmResumeData::cast(function_data(cage_base, kAcquireLoad));
}

#endif  // V8_ENABLE_WEBASSEMBLY

bool SharedFunctionInfo::HasBuiltinId() const {
#ifdef V8_ENABLE_SANDBOX
  if (trusted_function_data_handle(kAcquireLoad) !=
      kNullIndirectPointerHandle) {
    return false;
  }
  // It can happen that SetData is called on another thread at this point and
  // transitions from function_data to trusted_function_data. In this case,
  // we'll see the kClearedFunctionDataValue Smi value here and so must be able
  // to handle that, for example by checking that the id is a valid builtin id.
  static_assert(!Builtins::IsBuiltinId(kClearedFunctionDataValue));
  Tagged<Object> data = function_data(kAcquireLoad);
  return IsSmi(data) && Builtins::IsBuiltinId(Smi::ToInt(data));
#endif
  return IsSmi(function_data(kAcquireLoad));
}

Builtin SharedFunctionInfo::builtin_id() const {
  DCHECK(HasBuiltinId());
  int id = Smi::ToInt(function_data(kAcquireLoad));
  // The builtin id is read from the heap and so must be assumed to be
  // untrusted in the sandbox attacker model. As it is considered trusted by
  // e.g. `GetCode` (when fetching the code for this SFI), we validate it here.
  SBXCHECK(Builtins::IsBuiltinId(id));
  return Builtins::FromInt(id);
}

void SharedFunctionInfo::set_builtin_id(Builtin builtin) {
  DCHECK(Builtins::IsBuiltinId(builtin));
  SetData(Smi::FromInt(static_cast<int>(builtin)), kReleaseStore,
          DataType::kRegular, SKIP_WRITE_BARRIER);
}

bool SharedFunctionInfo::HasUncompiledData() const {
  return IsUncompiledData(function_data(kAcquireLoad));
}

DEF_GETTER(SharedFunctionInfo, uncompiled_data, Tagged<UncompiledData>) {
  DCHECK(HasUncompiledData());
  return UncompiledData::cast(function_data(cage_base, kAcquireLoad));
}

void SharedFunctionInfo::set_uncompiled_data(
    Tagged<UncompiledData> uncompiled_data, WriteBarrierMode mode) {
  DCHECK(function_data(kAcquireLoad) == Smi::FromEnum(Builtin::kCompileLazy) ||
         HasUncompiledData() || HasBytecodeArray() || HasBaselineCode());
  DCHECK(IsUncompiledData(uncompiled_data));
  SetData(uncompiled_data, kReleaseStore);
}

bool SharedFunctionInfo::HasUncompiledDataWithPreparseData() const {
  return IsUncompiledDataWithPreparseData(function_data(kAcquireLoad));
}

Tagged<UncompiledDataWithPreparseData>
SharedFunctionInfo::uncompiled_data_with_preparse_data() const {
  DCHECK(HasUncompiledDataWithPreparseData());
  return UncompiledDataWithPreparseData::cast(function_data(kAcquireLoad));
}

void SharedFunctionInfo::set_uncompiled_data_with_preparse_data(
    Tagged<UncompiledDataWithPreparseData> uncompiled_data_with_preparse_data,
    WriteBarrierMode mode) {
  DCHECK(function_data(kAcquireLoad) == Smi::FromEnum(Builtin::kCompileLazy));
  DCHECK(IsUncompiledDataWithPreparseData(uncompiled_data_with_preparse_data));
  SetData(uncompiled_data_with_preparse_data, kReleaseStore);
}

bool SharedFunctionInfo::HasUncompiledDataWithoutPreparseData() const {
  return IsUncompiledDataWithoutPreparseData(function_data(kAcquireLoad));
}

void SharedFunctionInfo::ClearUncompiledDataJobPointer() {
  Tagged<UncompiledData> uncompiled_data = this->uncompiled_data();
  if (IsUncompiledDataWithPreparseDataAndJob(uncompiled_data)) {
    UncompiledDataWithPreparseDataAndJob::cast(uncompiled_data)
        ->set_job(kNullAddress);
  } else if (IsUncompiledDataWithoutPreparseDataWithJob(uncompiled_data)) {
    UncompiledDataWithoutPreparseDataWithJob::cast(uncompiled_data)
        ->set_job(kNullAddress);
  }
}

void SharedFunctionInfo::ClearPreparseData() {
  DCHECK(HasUncompiledDataWithPreparseData());
  Tagged<UncompiledDataWithPreparseData> data =
      uncompiled_data_with_preparse_data();

  // Trim off the pre-parsed scope data from the uncompiled data by swapping the
  // map, leaving only an uncompiled data without pre-parsed scope.
  DisallowGarbageCollection no_gc;
  Heap* heap = GetHeapFromWritableObject(data);

  // We are basically trimming that object to its supertype, so recorded slots
  // within the object don't need to be invalidated.
  heap->NotifyObjectLayoutChange(data, no_gc, InvalidateRecordedSlots::kNo,
                                 InvalidateExternalPointerSlots::kNo);
  static_assert(UncompiledDataWithoutPreparseData::kSize <
                UncompiledDataWithPreparseData::kSize);
  static_assert(UncompiledDataWithoutPreparseData::kSize ==
                UncompiledData::kHeaderSize);

  // Fill the remaining space with filler and clear slots in the trimmed area.
  heap->NotifyObjectSizeChange(data, UncompiledDataWithPreparseData::kSize,
                               UncompiledDataWithoutPreparseData::kSize,
                               ClearRecordedSlots::kYes);

  // Swap the map.
  data->set_map(GetReadOnlyRoots().uncompiled_data_without_preparse_data_map(),
                kReleaseStore);

  // Ensure that the clear was successful.
  DCHECK(HasUncompiledDataWithoutPreparseData());
}

void UncompiledData::InitAfterBytecodeFlush(
    Tagged<String> inferred_name, int start_position, int end_position,
    std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                       Tagged<HeapObject> target)>
        gc_notify_updated_slot) {
  set_inferred_name(inferred_name);
  gc_notify_updated_slot(*this, RawField(UncompiledData::kInferredNameOffset),
                         inferred_name);
  set_start_position(start_position);
  set_end_position(end_position);
}

bool SharedFunctionInfo::is_repl_mode() const {
  return IsScript(script()) && Script::cast(script())->is_repl_mode();
}

bool SharedFunctionInfo::HasInferredName() {
  Tagged<Object> scope_info = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(scope_info)) {
    return ScopeInfo::cast(scope_info)->HasInferredFunctionName();
  }
  return HasUncompiledData();
}

DEF_GETTER(SharedFunctionInfo, inferred_name, Tagged<String>) {
  Tagged<Object> maybe_scope_info = name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(maybe_scope_info)) {
    Tagged<ScopeInfo> scope_info = ScopeInfo::cast(maybe_scope_info);
    if (scope_info->HasInferredFunctionName()) {
      Tagged<Object> name = scope_info->InferredFunctionName();
      if (IsString(name)) return String::cast(name);
    }
  } else if (HasUncompiledData()) {
    return uncompiled_data(cage_base)->inferred_name(cage_base);
  }
  return GetReadOnlyRoots().empty_string();
}

bool SharedFunctionInfo::IsUserJavaScript() const {
  Tagged<Object> script_obj = script();
  if (IsUndefined(script_obj)) return false;
  Tagged<Script> script = Script::cast(script_obj);
  return script->IsUserJavaScript();
}

bool SharedFunctionInfo::IsSubjectToDebugging() const {
#if V8_ENABLE_WEBASSEMBLY
  if (HasAsmWasmData()) return false;
  if (HasWasmExportedFunctionData()) return false;
#endif  // V8_ENABLE_WEBASSEMBLY
  return IsUserJavaScript();
}

bool SharedFunctionInfo::CanDiscardCompiled() const {
#if V8_ENABLE_WEBASSEMBLY
  if (HasAsmWasmData()) return true;
#endif  // V8_ENABLE_WEBASSEMBLY
  return HasBytecodeArray() || HasUncompiledDataWithPreparseData() ||
         HasBaselineCode();
}

bool SharedFunctionInfo::is_class_constructor() const {
  return IsClassConstructorBit::decode(flags(kRelaxedLoad));
}

void SharedFunctionInfo::set_are_properties_final(bool value) {
  if (is_class_constructor()) {
    set_properties_are_final(value);
  }
}

bool SharedFunctionInfo::are_properties_final() const {
  bool bit = properties_are_final();
  return bit && is_class_constructor();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_
