// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FEEDBACK_VECTOR_INL_H_
#define V8_OBJECTS_FEEDBACK_VECTOR_INL_H_

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/maybe-object-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"
#include "src/objects/tagged.h"
#include "src/roots/roots-inl.h"
#include "src/torque/runtime-macro-shims.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/feedback-vector-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(FeedbackVector)
OBJECT_CONSTRUCTORS_IMPL(FeedbackMetadata, HeapObject)
OBJECT_CONSTRUCTORS_IMPL(ClosureFeedbackCellArray,
                         ClosureFeedbackCellArray::Super)

NEVER_READ_ONLY_SPACE_IMPL(FeedbackVector)
NEVER_READ_ONLY_SPACE_IMPL(ClosureFeedbackCellArray)

CAST_ACCESSOR(FeedbackMetadata)
CAST_ACCESSOR(ClosureFeedbackCellArray)

INT32_ACCESSORS(FeedbackMetadata, slot_count, kSlotCountOffset)

INT32_ACCESSORS(FeedbackMetadata, create_closure_slot_count,
                kCreateClosureSlotCountOffset)

int32_t FeedbackMetadata::slot_count(AcquireLoadTag) const {
  return ACQUIRE_READ_INT32_FIELD(*this, kSlotCountOffset);
}

int32_t FeedbackMetadata::get(int index) const {
  CHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  int offset = kHeaderSize + index * kInt32Size;
  return ReadField<int32_t>(offset);
}

void FeedbackMetadata::set(int index, int32_t value) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  int offset = kHeaderSize + index * kInt32Size;
  WriteField<int32_t>(offset, value);
}

bool FeedbackMetadata::is_empty() const { return slot_count() == 0; }

int FeedbackMetadata::length() const {
  return FeedbackMetadata::length(slot_count());
}

int FeedbackMetadata::GetSlotSize(FeedbackSlotKind kind) {
  switch (kind) {
    case FeedbackSlotKind::kForIn:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kCompareOp:
    case FeedbackSlotKind::kBinaryOp:
    case FeedbackSlotKind::kLiteral:
    case FeedbackSlotKind::kJumpLoop:
      return 1;

    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kCloneObject:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kSetNamedSloppy:
    case FeedbackSlotKind::kSetNamedStrict:
    case FeedbackSlotKind::kDefineNamedOwn:
    case FeedbackSlotKind::kDefineKeyedOwn:
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kSetKeyedSloppy:
    case FeedbackSlotKind::kSetKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kDefineKeyedOwnPropertyInLiteral:
      return 2;

    case FeedbackSlotKind::kInvalid:
      UNREACHABLE();
  }
  UNREACHABLE();
}

bool FeedbackVector::is_empty() const { return length() == 0; }

DEF_GETTER(FeedbackVector, metadata, Tagged<FeedbackMetadata>) {
  return shared_function_info(cage_base)->feedback_metadata(cage_base);
}

DEF_ACQUIRE_GETTER(FeedbackVector, metadata, Tagged<FeedbackMetadata>) {
  return shared_function_info(cage_base)->feedback_metadata(cage_base,
                                                            kAcquireLoad);
}

RELAXED_INT32_ACCESSORS(FeedbackVector, invocation_count,
                        kInvocationCountOffset)

void FeedbackVector::clear_invocation_count(RelaxedStoreTag tag) {
  set_invocation_count(0, tag);
}

UINT8_ACCESSORS(FeedbackVector, invocation_count_before_stable,
                kInvocationCountBeforeStableOffset)

int FeedbackVector::osr_urgency() const {
  return OsrUrgencyBits::decode(osr_state());
}

void FeedbackVector::set_osr_urgency(int urgency) {
  DCHECK(0 <= urgency && urgency <= FeedbackVector::kMaxOsrUrgency);
  static_assert(FeedbackVector::kMaxOsrUrgency <= OsrUrgencyBits::kMax);
  set_osr_state(OsrUrgencyBits::update(osr_state(), urgency));
}

void FeedbackVector::reset_osr_urgency() { set_osr_urgency(0); }

void FeedbackVector::RequestOsrAtNextOpportunity() {
  set_osr_urgency(kMaxOsrUrgency);
}

void FeedbackVector::reset_osr_state() { set_osr_state(0); }

bool FeedbackVector::maybe_has_optimized_osr_code() const {
  return maybe_has_maglev_osr_code() || maybe_has_turbofan_osr_code();
}

bool FeedbackVector::maybe_has_maglev_osr_code() const {
  return MaybeHasMaglevOsrCodeBit::decode(osr_state());
}

bool FeedbackVector::maybe_has_turbofan_osr_code() const {
  return MaybeHasTurbofanOsrCodeBit::decode(osr_state());
}

void FeedbackVector::set_maybe_has_optimized_osr_code(bool value,
                                                      CodeKind code_kind) {
  if (code_kind == CodeKind::MAGLEV) {
    CHECK(v8_flags.maglev_osr);
    set_osr_state(MaybeHasMaglevOsrCodeBit::update(osr_state(), value));
  } else {
    CHECK_EQ(code_kind, CodeKind::TURBOFAN);
    set_osr_state(MaybeHasTurbofanOsrCodeBit::update(osr_state(), value));
  }
}

Tagged<Code> FeedbackVector::optimized_code(IsolateForSandbox isolate) const {
  Tagged<MaybeObject> slot = maybe_optimized_code();
  DCHECK(slot.IsWeakOrCleared());
  Tagged<HeapObject> heap_object;
  Tagged<Code> code;
  if (slot.GetHeapObject(&heap_object)) {
    code = CodeWrapper::cast(heap_object)->code(isolate);
  }
  // It is possible that the maybe_optimized_code slot is cleared but the flags
  // haven't been updated yet. We update them when we execute the function next
  // time / when we create new closure.
  DCHECK_IMPLIES(!code.is_null(),
                 maybe_has_maglev_code() || maybe_has_turbofan_code());
  DCHECK_IMPLIES(!code.is_null() && code->is_maglevved(),
                 maybe_has_maglev_code());
  DCHECK_IMPLIES(!code.is_null() && code->is_turbofanned(),
                 maybe_has_turbofan_code());
  return code;
}

TieringState FeedbackVector::tiering_state() const {
  return TieringStateBits::decode(flags());
}

bool FeedbackVector::has_optimized_code() const {
  bool is_cleared = maybe_optimized_code().IsCleared();
  DCHECK_IMPLIES(!is_cleared,
                 maybe_has_maglev_code() || maybe_has_turbofan_code());
  return !is_cleared;
}

bool FeedbackVector::maybe_has_maglev_code() const {
  return MaybeHasMaglevCodeBit::decode(flags());
}

void FeedbackVector::set_maybe_has_maglev_code(bool value) {
  set_flags(MaybeHasMaglevCodeBit::update(flags(), value));
}

bool FeedbackVector::maybe_has_turbofan_code() const {
  return MaybeHasTurbofanCodeBit::decode(flags());
}

void FeedbackVector::set_maybe_has_turbofan_code(bool value) {
  set_flags(MaybeHasTurbofanCodeBit::update(flags(), value));
}

bool FeedbackVector::log_next_execution() const {
  return LogNextExecutionBit::decode(flags());
}

void FeedbackVector::set_log_next_execution(bool value) {
  set_flags(LogNextExecutionBit::update(flags(), value));
}

bool FeedbackVector::interrupt_budget_reset_by_ic_change() const {
  return InterruptBudgetResetByIcChangeBit::decode(flags());
}

void FeedbackVector::set_interrupt_budget_reset_by_ic_change(bool value) {
  set_flags(InterruptBudgetResetByIcChangeBit::update(flags(), value));
}

base::Optional<Tagged<Code>> FeedbackVector::GetOptimizedOsrCode(
    Isolate* isolate, FeedbackSlot slot) {
  Tagged<MaybeObject> maybe_code = Get(isolate, slot);
  if (maybe_code.IsCleared()) return {};

  Tagged<Code> code =
      CodeWrapper::cast(maybe_code.GetHeapObject())->code(isolate);
  if (code->marked_for_deoptimization()) {
    // Clear the cached Code object if deoptimized.
    // TODO(jgruber): Add tracing.
    Set(slot, ClearedValue(isolate));
    return {};
  }

  return code;
}

// Conversion from an integer index to either a slot or an ic slot.
// static
FeedbackSlot FeedbackVector::ToSlot(intptr_t index) {
  if (index == static_cast<intptr_t>(FeedbackSlot::Invalid().ToInt())) {
    return FeedbackSlot();
  }
  DCHECK_LE(static_cast<uintptr_t>(index),
            static_cast<uintptr_t>(std::numeric_limits<int>::max()));
  return FeedbackSlot(static_cast<int>(index));
}

#ifdef DEBUG
// Instead of FixedArray, the Feedback and the Extra should contain
// WeakFixedArrays. The only allowed FixedArray subtype is HashTable.
bool FeedbackVector::IsOfLegacyType(Tagged<MaybeObject> value) {
  Tagged<HeapObject> heap_object;
  if (value.GetHeapObject(&heap_object)) {
    return IsFixedArray(heap_object) && !IsHashTable(heap_object);
  }
  return false;
}
#endif  // DEBUG

Tagged<MaybeObject> FeedbackVector::Get(FeedbackSlot slot) const {
  Tagged<MaybeObject> value = raw_feedback_slots(GetIndex(slot), kRelaxedLoad);
  DCHECK(!IsOfLegacyType(value));
  return value;
}

Tagged<MaybeObject> FeedbackVector::Get(PtrComprCageBase cage_base,
                                        FeedbackSlot slot) const {
  Tagged<MaybeObject> value =
      raw_feedback_slots(cage_base, GetIndex(slot), kRelaxedLoad);
  DCHECK(!IsOfLegacyType(value));
  return value;
}

Handle<FeedbackCell> FeedbackVector::GetClosureFeedbackCell(Isolate* isolate,
                                                            int index) const {
  DCHECK_GE(index, 0);
  return handle(closure_feedback_cell_array()->get(index), isolate);
}

Tagged<FeedbackCell> FeedbackVector::closure_feedback_cell(int index) const {
  DCHECK_GE(index, 0);
  return closure_feedback_cell_array()->get(index);
}

Tagged<MaybeObject> FeedbackVector::SynchronizedGet(FeedbackSlot slot) const {
  const int i = slot.ToInt();
  DCHECK_LT(static_cast<unsigned>(i), static_cast<unsigned>(this->length()));
  const int offset = kRawFeedbackSlotsOffset + i * kTaggedSize;
  Tagged<MaybeObject> value =
      TaggedField<MaybeObject>::Acquire_Load(*this, offset);
  DCHECK(!IsOfLegacyType(value));
  return value;
}

void FeedbackVector::SynchronizedSet(FeedbackSlot slot,
                                     Tagged<MaybeObject> value,
                                     WriteBarrierMode mode) {
  DCHECK(!IsOfLegacyType(value));
  const int i = slot.ToInt();
  DCHECK_LT(static_cast<unsigned>(i), static_cast<unsigned>(this->length()));
  const int offset = kRawFeedbackSlotsOffset + i * kTaggedSize;
  TaggedField<MaybeObject>::Release_Store(*this, offset, value);
  CONDITIONAL_WEAK_WRITE_BARRIER(*this, offset, value, mode);
}

void FeedbackVector::Set(FeedbackSlot slot, Tagged<MaybeObject> value,
                         WriteBarrierMode mode) {
  DCHECK(!IsOfLegacyType(value));
  set_raw_feedback_slots(GetIndex(slot), value, mode);
}

inline MaybeObjectSlot FeedbackVector::slots_start() {
  return RawMaybeWeakField(OffsetOfElementAt(0));
}

// Helper function to transform the feedback to BinaryOperationHint.
BinaryOperationHint BinaryOperationHintFromFeedback(int type_feedback) {
  switch (type_feedback) {
    case BinaryOperationFeedback::kNone:
      return BinaryOperationHint::kNone;
    case BinaryOperationFeedback::kSignedSmall:
      return BinaryOperationHint::kSignedSmall;
    case BinaryOperationFeedback::kSignedSmallInputs:
      return BinaryOperationHint::kSignedSmallInputs;
    case BinaryOperationFeedback::kNumber:
      return BinaryOperationHint::kNumber;
    case BinaryOperationFeedback::kNumberOrOddball:
      return BinaryOperationHint::kNumberOrOddball;
    case BinaryOperationFeedback::kString:
      return BinaryOperationHint::kString;
    case BinaryOperationFeedback::kStringOrStringWrapper:
      return BinaryOperationHint::kStringOrStringWrapper;
    case BinaryOperationFeedback::kBigInt:
      return BinaryOperationHint::kBigInt;
    case BinaryOperationFeedback::kBigInt64:
      return BinaryOperationHint::kBigInt64;
    default:
      return BinaryOperationHint::kAny;
  }
  UNREACHABLE();
}

// Helper function to transform the feedback to CompareOperationHint.
template <CompareOperationFeedback::Type Feedback>
bool Is(int type_feedback) {
  return !(type_feedback & ~Feedback);
}

CompareOperationHint CompareOperationHintFromFeedback(int type_feedback) {
  if (Is<CompareOperationFeedback::kNone>(type_feedback)) {
    return CompareOperationHint::kNone;
  }

  if (Is<CompareOperationFeedback::kSignedSmall>(type_feedback)) {
    return CompareOperationHint::kSignedSmall;
  } else if (Is<CompareOperationFeedback::kNumber>(type_feedback)) {
    return CompareOperationHint::kNumber;
  } else if (Is<CompareOperationFeedback::kNumberOrBoolean>(type_feedback)) {
    return CompareOperationHint::kNumberOrBoolean;
  }

  if (Is<CompareOperationFeedback::kInternalizedString>(type_feedback)) {
    return CompareOperationHint::kInternalizedString;
  } else if (Is<CompareOperationFeedback::kString>(type_feedback)) {
    return CompareOperationHint::kString;
  }

  if (Is<CompareOperationFeedback::kReceiver>(type_feedback)) {
    return CompareOperationHint::kReceiver;
  } else if (Is<CompareOperationFeedback::kReceiverOrNullOrUndefined>(
                 type_feedback)) {
    return CompareOperationHint::kReceiverOrNullOrUndefined;
  }

  if (Is<CompareOperationFeedback::kBigInt64>(type_feedback)) {
    return CompareOperationHint::kBigInt64;
  } else if (Is<CompareOperationFeedback::kBigInt>(type_feedback)) {
    return CompareOperationHint::kBigInt;
  }

  if (Is<CompareOperationFeedback::kSymbol>(type_feedback)) {
    return CompareOperationHint::kSymbol;
  }

  DCHECK(Is<CompareOperationFeedback::kAny>(type_feedback));
  return CompareOperationHint::kAny;
}

// Helper function to transform the feedback to ForInHint.
ForInHint ForInHintFromFeedback(ForInFeedback type_feedback) {
  switch (type_feedback) {
    case ForInFeedback::kNone:
      return ForInHint::kNone;
    case ForInFeedback::kEnumCacheKeys:
      return ForInHint::kEnumCacheKeys;
    case ForInFeedback::kEnumCacheKeysAndIndices:
      return ForInHint::kEnumCacheKeysAndIndices;
    default:
      return ForInHint::kAny;
  }
  UNREACHABLE();
}

Handle<Symbol> FeedbackVector::UninitializedSentinel(Isolate* isolate) {
  return ReadOnlyRoots(isolate).uninitialized_symbol_handle();
}

Handle<Symbol> FeedbackVector::MegamorphicSentinel(Isolate* isolate) {
  return ReadOnlyRoots(isolate).megamorphic_symbol_handle();
}

Handle<Symbol> FeedbackVector::MegaDOMSentinel(Isolate* isolate) {
  return ReadOnlyRoots(isolate).mega_dom_symbol_handle();
}

Tagged<Symbol> FeedbackVector::RawUninitializedSentinel(Isolate* isolate) {
  return ReadOnlyRoots(isolate).uninitialized_symbol();
}

bool FeedbackMetadataIterator::HasNext() const {
  return next_slot_.ToInt() < metadata()->slot_count();
}

FeedbackSlot FeedbackMetadataIterator::Next() {
  DCHECK(HasNext());
  cur_slot_ = next_slot_;
  slot_kind_ = metadata()->GetKind(cur_slot_);
  next_slot_ = FeedbackSlot(next_slot_.ToInt() + entry_size());
  return cur_slot_;
}

int FeedbackMetadataIterator::entry_size() const {
  return FeedbackMetadata::GetSlotSize(kind());
}

Tagged<MaybeObject> NexusConfig::GetFeedback(Tagged<FeedbackVector> vector,
                                             FeedbackSlot slot) const {
  return vector->SynchronizedGet(slot);
}

void NexusConfig::SetFeedback(Tagged<FeedbackVector> vector, FeedbackSlot slot,
                              Tagged<MaybeObject> feedback,
                              WriteBarrierMode mode) const {
  DCHECK(can_write());
  vector->SynchronizedSet(slot, feedback, mode);
}

Tagged<MaybeObject> FeedbackNexus::UninitializedSentinel() const {
  return *FeedbackVector::UninitializedSentinel(GetIsolate());
}

Tagged<MaybeObject> FeedbackNexus::MegamorphicSentinel() const {
  return *FeedbackVector::MegamorphicSentinel(GetIsolate());
}

Tagged<MaybeObject> FeedbackNexus::MegaDOMSentinel() const {
  return *FeedbackVector::MegaDOMSentinel(GetIsolate());
}

Tagged<MaybeObject> FeedbackNexus::FromHandle(MaybeObjectHandle slot) const {
  return slot.is_null() ? ClearedValue(config()->isolate()) : *slot;
}

MaybeObjectHandle FeedbackNexus::ToHandle(Tagged<MaybeObject> value) const {
  return value.IsCleared() ? MaybeObjectHandle()
                           : MaybeObjectHandle(config()->NewHandle(value));
}

Tagged<MaybeObject> FeedbackNexus::GetFeedback() const {
  auto pair = GetFeedbackPair();
  return pair.first;
}

Tagged<MaybeObject> FeedbackNexus::GetFeedbackExtra() const {
  auto pair = GetFeedbackPair();
  return pair.second;
}

std::pair<Tagged<MaybeObject>, Tagged<MaybeObject>>
FeedbackNexus::GetFeedbackPair() const {
  if (config()->mode() == NexusConfig::BackgroundThread &&
      feedback_cache_.has_value()) {
    return std::make_pair(FromHandle(feedback_cache_->first),
                          FromHandle(feedback_cache_->second));
  }
  auto pair = FeedbackMetadata::GetSlotSize(kind()) == 2
                  ? config()->GetFeedbackPair(vector(), slot())
                  : std::make_pair(config()->GetFeedback(vector(), slot()),
                                   Tagged<MaybeObject>());
  if (config()->mode() == NexusConfig::BackgroundThread &&
      !feedback_cache_.has_value()) {
    feedback_cache_ =
        std::make_pair(ToHandle(pair.first), ToHandle(pair.second));
  }
  return pair;
}

template <typename FeedbackType>
void FeedbackNexus::SetFeedback(Tagged<FeedbackType> feedback,
                                WriteBarrierMode mode) {
  config()->SetFeedback(vector(), slot(), feedback, mode);
}

template <typename FeedbackType, typename FeedbackExtraType>
void FeedbackNexus::SetFeedback(Tagged<FeedbackType> feedback,
                                WriteBarrierMode mode,
                                Tagged<FeedbackExtraType> feedback_extra,
                                WriteBarrierMode mode_extra) {
  config()->SetFeedbackPair(vector(), slot(), feedback, mode, feedback_extra,
                            mode_extra);
}

Isolate* FeedbackNexus::GetIsolate() const { return vector()->GetIsolate(); }
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FEEDBACK_VECTOR_INL_H_
