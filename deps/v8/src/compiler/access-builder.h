// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ACCESS_BUILDER_H_
#define V8_COMPILER_ACCESS_BUILDER_H_

#include "src/base/compiler-specific.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/objects/elements-kind.h"
#include "src/objects/js-objects.h"

namespace v8 {
namespace internal {
namespace compiler {

// This access builder provides a set of static methods constructing commonly
// used FieldAccess and ElementAccess descriptors. These descriptors serve as
// parameters to simplified load/store operators.
class V8_EXPORT_PRIVATE AccessBuilder final
    : public NON_EXPORTED_BASE(AllStatic) {
 public:
  // ===========================================================================
  // Access to external values (based on external references).

  // Provides access to an IntPtr field identified by an external reference.
  static FieldAccess ForExternalIntPtr();

  // ===========================================================================
  // Access to heap object fields and elements (based on tagged pointer).

  // Provides access to HeapObject::map() field.
  static FieldAccess ForMap(WriteBarrierKind write_barrier = kMapWriteBarrier);

  // Provides access to HeapNumber::value() field.
  static FieldAccess ForHeapNumberValue();
  static FieldAccess ForHeapInt32Value();
  static FieldAccess ForHeapInt32UpperValue();

  // Provides access to HeapNumber::value() and Oddball::to_number_raw() fields.
  // This is the same as ForHeapNumberValue, except it documents (and static
  // asserts) that both inputs are valid.
  static FieldAccess ForHeapNumberOrOddballOrHoleValue();

  // Provides access to BigInt's bit field.
  static FieldAccess ForBigIntBitfield();

#ifdef BIGINT_NEEDS_PADDING
  // Provides access to BigInt's 32 bit padding that is placed after the
  // bitfield on 64 bit architectures without pointer compression.
  static FieldAccess ForBigIntOptionalPadding();
#endif

  // Provides access to BigInt's least significant digit on 64 bit
  // architectures. Do not use this on 32 bit architectures.
  static FieldAccess ForBigIntLeastSignificantDigit64();

  // Provides access to JSObject::properties() field.
  static FieldAccess ForJSObjectPropertiesOrHash();

  // Provides access to JSObject::properties() field for known pointers.
  static FieldAccess ForJSObjectPropertiesOrHashKnownPointer();

  // Provides access to JSObject::elements() field.
  static FieldAccess ForJSObjectElements();

  // Provides access to JSObject inobject property fields.
  static FieldAccess ForJSObjectInObjectProperty(
      MapRef map, int index,
      MachineType machine_type = MachineType::AnyTagged());
  static FieldAccess ForJSObjectOffset(
      int offset, WriteBarrierKind write_barrier_kind = kFullWriteBarrier);

  // Provides access to JSCollecton::table() field.
  static FieldAccess ForJSCollectionTable();

  // Provides access to JSCollectionIterator::table() field.
  static FieldAccess ForJSCollectionIteratorTable();

  // Provides access to JSCollectionIterator::index() field.
  static FieldAccess ForJSCollectionIteratorIndex();

  // Provides access to an ExternalPointer through the JSExternalObject::value()
  // field.
  static FieldAccess ForJSExternalObjectValue();

#ifdef V8_ENABLE_SANDBOX
  // Provides access to JSExternalObject::value() field.
  static FieldAccess ForJSExternalObjectPointerHandle();
#endif

  // Provides access to JSFunction::prototype_or_initial_map() field.
  static FieldAccess ForJSFunctionPrototypeOrInitialMap();

  // Provides access to JSFunction::context() field.
  static FieldAccess ForJSFunctionContext();

  // Provides access to JSFunction::shared() field.
  static FieldAccess ForJSFunctionSharedFunctionInfo();

  // Provides access to JSFunction::feedback_cell() field.
  static FieldAccess ForJSFunctionFeedbackCell();

#ifdef V8_ENABLE_LEAPTIERING
  // Provides access to JSFunction::dispatch_handle() field.
  static FieldAccess ForJSFunctionDispatchHandleNoWriteBarrier();
#else
  // Provides access to JSFunction::code() field.
  static FieldAccess ForJSFunctionCode();
#endif  // V8_ENABLE_LEAPTIERING

  // Provides access to JSBoundFunction::bound_target_function() field.
  static FieldAccess ForJSBoundFunctionBoundTargetFunction();

  // Provides access to JSBoundFunction::bound_this() field.
  static FieldAccess ForJSBoundFunctionBoundThis();

  // Provides access to JSBoundFunction::bound_arguments() field.
  static FieldAccess ForJSBoundFunctionBoundArguments();

  // Provides access to JSGeneratorObject::context() field.
  static FieldAccess ForJSGeneratorObjectContext();

  // Provides access to JSGeneratorObject::continuation() field.
  static FieldAccess ForJSGeneratorObjectContinuation();

  // Provides access to JSGeneratorObject::input_or_debug_pos() field.
  static FieldAccess ForJSGeneratorObjectInputOrDebugPos();

  // Provides access to JSGeneratorObject::parameters_and_registers() field.
  static FieldAccess ForJSGeneratorObjectParametersAndRegisters();

  // Provides access to JSGeneratorObject::function() field.
  static FieldAccess ForJSGeneratorObjectFunction();

  // Provides access to JSGeneratorObject::receiver() field.
  static FieldAccess ForJSGeneratorObjectReceiver();

  // Provides access to JSGeneratorObject::resume_mode() field.
  static FieldAccess ForJSGeneratorObjectResumeMode();

  // Provides access to JSAsyncFunctionObject::promise() field.
  static FieldAccess ForJSAsyncFunctionObjectPromise();

  // Provides access to JSAsyncGeneratorObject::queue() field.
  static FieldAccess ForJSAsyncGeneratorObjectQueue();

  // Provides access to JSAsyncGeneratorObject::is_awaiting() field.
  static FieldAccess ForJSAsyncGeneratorObjectIsAwaiting();

  // Provides access to JSArray::length() field.
  static FieldAccess ForJSArrayLength(ElementsKind elements_kind);

  // Provides access to JSArrayBuffer::bit_field() field.
  static FieldAccess ForJSArrayBufferBitField();

  // Provides access to JSArrayBuffer::byteLength() field.
  static FieldAccess ForJSArrayBufferByteLength();

  // Provides access to JSArrayBufferView::buffer() field.
  static FieldAccess ForJSArrayBufferViewBuffer();

  // Provides access to JSArrayBufferView::byteLength() field.
  static FieldAccess ForJSArrayBufferViewByteLength();

  // Provides access to JSArrayBufferView::byteOffset() field.
  static FieldAccess ForJSArrayBufferViewByteOffset();

  // Provides access to JSArrayBufferView::bitfield() field
  static FieldAccess ForJSArrayBufferViewBitField();

  // Provides access to JSTypedArray::length() field.
  static FieldAccess ForJSTypedArrayLength();

  // Provides access to JSTypedArray::byteLength() field.
  static FieldAccess ForJSTypedArrayByteLength() {
    return ForJSArrayBufferViewByteLength();
  }

  // Provides access to JSTypedArray::base_pointer() field.
  static FieldAccess ForJSTypedArrayBasePointer();

  // Provides access to JSTypedArray::external_pointer() field.
  static FieldAccess ForJSTypedArrayExternalPointer();

  // Provides access to JSDataView::data_pointer() field.
  static FieldAccess ForJSDataViewDataPointer();

  static FieldAccess ForJSDataViewByteLength() {
    return ForJSArrayBufferViewByteLength();
  }

  // Provides access to JSDate::value() field.
  static FieldAccess ForJSDateValue();

  // Provides access to JSDate fields.
  static FieldAccess ForJSDateField(JSDate::FieldIndex index);

  // Provides access to JSIteratorResult::done() field.
  static FieldAccess ForJSIteratorResultDone();

  // Provides access to JSIteratorResult::value() field.
  static FieldAccess ForJSIteratorResultValue();

  static FieldAccess ForJSPrimitiveWrapperValue();

  // Provides access to JSRegExp::data() field.
  static FieldAccess ForJSRegExpData();

  // Provides access to JSRegExp::flags() field.
  static FieldAccess ForJSRegExpFlags();

  // Provides access to JSRegExp::last_index() field.
  static FieldAccess ForJSRegExpLastIndex();

  // Provides access to JSRegExp::source() field.
  static FieldAccess ForJSRegExpSource();

  // Provides access to FixedArray::length() field.
  static FieldAccess ForFixedArrayLength();

  // Provides access to WeakFixedArray::length() field.
  static FieldAccess ForWeakFixedArrayLength();

  // Provides access to SloppyArgumentsElements::context() field.
  static FieldAccess ForSloppyArgumentsElementsContext();

  // Provides access to SloppyArgumentsElements::arguments() field.
  static FieldAccess ForSloppyArgumentsElementsArguments();

  // Provides access to PropertyArray::length() field.
  static FieldAccess ForPropertyArrayLengthAndHash();

  // Provides access to DescriptorArray::enum_cache() field.
  static FieldAccess ForDescriptorArrayEnumCache();

  // Provides access to Map::bit_field() byte.
  static FieldAccess ForMapBitField();

  // Provides access to Map::bit_field2() byte.
  static FieldAccess ForMapBitField2();

  // Provides access to Map::bit_field3() field.
  static FieldAccess ForMapBitField3();

  // Provides access to Map::descriptors() field.
  static FieldAccess ForMapDescriptors();

  // Provides access to Map::instance_type() field.
  static FieldAccess ForMapInstanceType();

  // Provides access to Map::prototype() field.
  static FieldAccess ForMapPrototype();

  // Provides access to Map::native_context() field.
  static FieldAccess ForMapNativeContext();

  // Provides access to Module::regular_exports() field.
  static FieldAccess ForModuleRegularExports();

  // Provides access to Module::regular_imports() field.
  static FieldAccess ForModuleRegularImports();

  // Provides access to Name::raw_hash_field() field.
  static FieldAccess ForNameRawHashField();

  // Provides access to FreeSpace::size() field
  static FieldAccess ForFreeSpaceSize();

  // Provides access to String::length() field.
  static FieldAccess ForStringLength();

  // Provides access to ConsString::first() field.
  static FieldAccess ForConsStringFirst();

  // Provides access to ConsString::second() field.
  static FieldAccess ForConsStringSecond();

  // Provides access to ThinString::actual() field.
  static FieldAccess ForThinStringActual();

  // Provides access to SlicedString::offset() field.
  static FieldAccess ForSlicedStringOffset();

  // Provides access to SlicedString::parent() field.
  static FieldAccess ForSlicedStringParent();

  // Provides access to ExternalString::resource_data() field.
  static FieldAccess ForExternalStringResourceData();

  // Provides access to SeqOneByteString characters.
  static ElementAccess ForSeqOneByteStringCharacter();

  // Provides access to SeqTwoByteString characters.
  static ElementAccess ForSeqTwoByteStringCharacter();

  // Provides access to JSArrayIterator::iterated_object() field.
  static FieldAccess ForJSArrayIteratorIteratedObject();

  // Provides access to JSArrayIterator::next_index() field.
  static FieldAccess ForJSArrayIteratorNextIndex();

  // Provides access to JSArrayIterator::kind() field.
  static FieldAccess ForJSArrayIteratorKind();

  // Provides access to JSStringIterator::string() field.
  static FieldAccess ForJSStringIteratorString();

  // Provides access to JSStringIterator::index() field.
  static FieldAccess ForJSStringIteratorIndex();

  // Provides access to Cell::value() field.
  static FieldAccess ForCellValue();

  // Provides access to arguments object fields.
  static FieldAccess ForArgumentsLength();
  static FieldAccess ForArgumentsCallee();

  // Provides access to FixedArray slots.
  static FieldAccess ForFixedArraySlot(
      size_t index, WriteBarrierKind write_barrier_kind = kFullWriteBarrier);

  static FieldAccess ForFeedbackVectorSlot(int index);

  // Provides access to PropertyArray slots.
  static FieldAccess ForPropertyArraySlot(int index);

  // Provides access to ScopeInfo flags.
  static FieldAccess ForScopeInfoFlags();

  // Provides access to Context slots.
  static FieldAccess ForContextSlot(size_t index);

  // Provides access to Context slots that are known to be pointers.
  static FieldAccess ForContextSlotKnownPointer(size_t index);

  // Provides access to Context slots that are known to be Smis.
  static FieldAccess ForContextSlotSmi(size_t index);

  // Provides access to WeakFixedArray elements.
  static ElementAccess ForWeakFixedArrayElement();
  static FieldAccess ForWeakFixedArraySlot(int index);

  // Provides access to FixedArray elements.
  static ElementAccess ForFixedArrayElement();
  static ElementAccess ForFixedArrayElement(ElementsKind kind);

  // Provides access to SloppyArgumentsElements elements.
  static ElementAccess ForSloppyArgumentsElementsMappedEntry();

  // Provides access to FixedDoubleArray elements.
  static ElementAccess ForFixedDoubleArrayElement();

  // Provides access to EnumCache::keys() field.
  static FieldAccess ForEnumCacheKeys();

  // Provides access to EnumCache::indices() field.
  static FieldAccess ForEnumCacheIndices();

  // Provides access to Fixed{type}TypedArray and External{type}Array elements.
  static ElementAccess ForTypedArrayElement(ExternalArrayType type,
                                            bool is_external);

  // Provides access to the for-in cache array.
  static ElementAccess ForJSForInCacheArrayElement(ForInMode mode);

  // Provides access to HashTable fields.
  static FieldAccess ForHashTableBaseNumberOfElements();
  static FieldAccess ForHashTableBaseNumberOfDeletedElement();
  static FieldAccess ForHashTableBaseCapacity();

  // Provides access to OrderedHashMapOrSet fields.
  static FieldAccess ForOrderedHashMapOrSetNextTable();
  static FieldAccess ForOrderedHashMapOrSetNumberOfBuckets();
  static FieldAccess ForOrderedHashMapOrSetNumberOfElements();
  static FieldAccess ForOrderedHashMapOrSetNumberOfDeletedElements();

  // Provides access to OrderedHashMap elements.
  static ElementAccess ForOrderedHashMapEntryValue();

  // Provides access to Dictionary fields.
  static FieldAccess ForDictionaryNextEnumerationIndex();
  static FieldAccess ForDictionaryObjectHashIndex();

  // Provides access to NameDictionary fields.
  static FieldAccess ForNameDictionaryFlagsIndex();

  // Provides access to FeedbackCell fields.
  static FieldAccess ForFeedbackCellInterruptBudget();
#ifdef V8_ENABLE_LEAPTIERING
  static FieldAccess ForFeedbackCellDispatchHandleNoWriteBarrier();
#endif  // V8_ENABLE_LEAPTIERING

  // Provides access to a FeedbackVector fields.
  static FieldAccess ForFeedbackVectorInvocationCount();
  static FieldAccess ForFeedbackVectorFlags();
  static FieldAccess ForFeedbackVectorClosureFeedbackCellArray();

#if V8_ENABLE_WEBASSEMBLY
  static FieldAccess ForWasmArrayLength();
  static FieldAccess ForWasmDispatchTableLength();
#endif

  static FieldAccess ForContextSideProperty();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AccessBuilder);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ACCESS_BUILDER_H_
