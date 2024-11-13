// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_BASE_H_
#define V8_HEAP_FACTORY_BASE_H_

#include "src/base/export-template.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/code-kind.h"
#include "src/objects/function-kind.h"
#include "src/objects/instance-type.h"
#include "src/roots/roots.h"
#include "torque-generated/class-forward-declarations.h"

namespace v8 {
namespace internal {

class ArrayBoilerplateDescription;
class BytecodeArray;
class ClassPositions;
class CoverageInfo;
class DeoptimizationLiteralArray;
class DeoptimizationFrameTranslation;
class FixedArray;
template <typename T, typename Base>
class FixedIntegerArrayBase;
class FreshlyAllocatedBigInt;
class FunctionLiteral;
class HeapObject;
class ObjectBoilerplateDescription;
template <typename T>
class PodArray;
class PreparseData;
class RegExpBoilerplateDescription;
class SeqOneByteString;
class SeqTwoByteString;
class SharedFunctionInfo;
class SourceTextModuleInfo;
class TemplateObjectDescription;
class UncompiledDataWithoutPreparseData;
class UncompiledDataWithPreparseData;
struct SourceRange;
enum class Builtin : int32_t;
template <typename T>
class ZoneVector;

namespace wasm {
class ValueType;
}  // namespace wasm

template <typename Impl>
class FactoryBase;

enum class NumberCacheMode { kIgnore, kSetOnly, kBoth };

using FixedInt32Array = FixedIntegerArrayBase<int32_t, ByteArray>;
using FixedUInt32Array = FixedIntegerArrayBase<uint32_t, ByteArray>;

// Putting Torque-generated definitions in a superclass allows to shadow them
// easily when they shouldn't be used and to reference them when they happen to
// have the same signature.
template <typename Impl>
class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) TorqueGeneratedFactory {
 private:
  FactoryBase<Impl>* factory() { return static_cast<FactoryBase<Impl>*>(this); }

 public:
#include "torque-generated/factory.inc"
};

struct NewCodeOptions {
  CodeKind kind;
  Builtin builtin;
  bool is_turbofanned;
  int stack_slots;
  uint16_t parameter_count;
  int instruction_size;
  int metadata_size;
  unsigned int inlined_bytecode_size;
  BytecodeOffset osr_offset;
  int handler_table_offset;
  int constant_pool_offset;
  int code_comments_offset;
  int32_t unwinding_info_offset;
  MaybeHandle<TrustedObject> bytecode_or_interpreter_data;
  MaybeHandle<DeoptimizationData> deoptimization_data;
  MaybeHandle<TrustedByteArray> bytecode_offset_table;
  MaybeHandle<TrustedByteArray> source_position_table;
  // Either instruction_stream is set and instruction_start is kNullAddress, or
  // instruction_stream is empty and instruction_start a valid target.
  MaybeHandle<InstructionStream> instruction_stream;
  Address instruction_start;
};

template <typename Impl>
class FactoryBase : public TorqueGeneratedFactory<Impl> {
 public:
  Handle<Code> NewCode(const NewCodeOptions& options);

  Handle<CodeWrapper> NewCodeWrapper();

  // Converts the given boolean condition to JavaScript boolean value.
  inline Handle<Boolean> ToBoolean(bool value);

#define ROOT_ACCESSOR(Type, name, CamelName) inline Handle<Type> name();
  READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
  MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  // Numbers (e.g. literals) are pretenured by the parser.
  // The return value may be a smi or a heap number.
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<Number> NewNumber(double value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<Number> NewNumberFromInt(int32_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<Number> NewNumberFromUint(uint32_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<Number> NewNumberFromSize(size_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<Number> NewNumberFromInt64(int64_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<HeapNumber> NewHeapNumber(double value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<HeapNumber> NewHeapNumberFromBits(uint64_t bits);
  template <AllocationType allocation = AllocationType::kYoung>
  inline Handle<HeapNumber> NewHeapNumberWithHoleNaN();

  template <AllocationType allocation>
  Handle<HeapNumber> NewHeapNumber();

  Handle<Struct> NewStruct(InstanceType type,
                           AllocationType allocation = AllocationType::kYoung);

  // Create a pre-tenured empty AccessorPair.
  Handle<AccessorPair> NewAccessorPair();

  // Allocates a fixed array initialized with undefined values.
  Handle<FixedArray> NewFixedArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a trusted fixed array in trusted space, initialized with zeros.
  Handle<TrustedFixedArray> NewTrustedFixedArray(int length);

  // Allocates a protected fixed array in trusted space, initialized with zeros.
  Handle<ProtectedFixedArray> NewProtectedFixedArray(int length);

  // Allocates a fixed array-like object with given map and initialized with
  // undefined values.
  Handle<FixedArray> NewFixedArrayWithMap(
      DirectHandle<Map> map, int length,
      AllocationType allocation = AllocationType::kYoung);

  // Allocate a new fixed array with non-existing entries (the hole).
  Handle<FixedArray> NewFixedArrayWithHoles(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocate a new fixed array with Tagged<Smi>(0) entries.
  Handle<FixedArray> NewFixedArrayWithZeroes(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocate a new uninitialized fixed double array.
  // The function returns a pre-allocated empty fixed array for length = 0,
  // so the return type must be the general fixed array class.
  Handle<FixedArrayBase> NewFixedDoubleArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a weak fixed array-like object with given map and initialized
  // with undefined values. Length must be > 0.
  Handle<WeakFixedArray> NewWeakFixedArrayWithMap(
      Tagged<Map> map, int length,
      AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array which may contain in-place weak references. The
  // array is initialized with undefined values
  // The function returns a pre-allocated empty weak fixed array for length = 0.
  Handle<WeakFixedArray> NewWeakFixedArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a trusted weak fixed array in trusted space, initialized with
  // zeros.
  Handle<TrustedWeakFixedArray> NewTrustedWeakFixedArray(int length);

  // The function returns a pre-allocated empty byte array for length = 0.
  Handle<ByteArray> NewByteArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a trusted byte array in trusted space, initialized with zeros.
  Handle<TrustedByteArray> NewTrustedByteArray(
      int length, AllocationType allocation_type = AllocationType::kTrusted);

  Handle<ExternalPointerArray> NewExternalPointerArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  Handle<DeoptimizationLiteralArray> NewDeoptimizationLiteralArray(int length);
  Handle<DeoptimizationFrameTranslation> NewDeoptimizationFrameTranslation(
      int length);

  Handle<BytecodeArray> NewBytecodeArray(
      int length, const uint8_t* raw_bytecodes, int frame_size,
      uint16_t parameter_count, uint16_t max_arguments,
      DirectHandle<TrustedFixedArray> constant_pool,
      DirectHandle<TrustedByteArray> handler_table);

  Handle<BytecodeWrapper> NewBytecodeWrapper();

  // Allocates a fixed array for name-value pairs of boilerplate properties and
  // calculates the number of properties we need to store in the backing store.
  Handle<ObjectBoilerplateDescription> NewObjectBoilerplateDescription(
      int boilerplate, int all_properties, int index_keys, bool has_seen_proto);

  // Create a new ArrayBoilerplateDescription struct.
  Handle<ArrayBoilerplateDescription> NewArrayBoilerplateDescription(
      ElementsKind elements_kind, DirectHandle<FixedArrayBase> constant_values);

  Handle<RegExpDataWrapper> NewRegExpDataWrapper();

  Handle<RegExpBoilerplateDescription> NewRegExpBoilerplateDescription(
      DirectHandle<RegExpData> data, DirectHandle<String> source,
      Tagged<Smi> flags);

  // Create a new TemplateObjectDescription struct.
  Handle<TemplateObjectDescription> NewTemplateObjectDescription(
      DirectHandle<FixedArray> raw_strings,
      DirectHandle<FixedArray> cooked_strings);

  Handle<Script> NewScript(
      DirectHandle<UnionOf<String, Undefined>> source,
      ScriptEventType event_type = ScriptEventType::kCreate);
  Handle<Script> NewScriptWithId(
      DirectHandle<UnionOf<String, Undefined>> source, int script_id,
      ScriptEventType event_type = ScriptEventType::kCreate);

  Handle<SloppyArgumentsElements> NewSloppyArgumentsElements(
      int length, DirectHandle<Context> context,
      DirectHandle<FixedArray> arguments,
      AllocationType allocation = AllocationType::kYoung);
  Handle<ArrayList> NewArrayList(
      int size, AllocationType allocation = AllocationType::kYoung);

  Handle<SharedFunctionInfo> NewSharedFunctionInfoForLiteral(
      FunctionLiteral* literal, DirectHandle<Script> script, bool is_toplevel);

  // Create a copy of a given SharedFunctionInfo for use as a placeholder in
  // off-thread compilation
  Handle<SharedFunctionInfo> CloneSharedFunctionInfo(
      DirectHandle<SharedFunctionInfo> other);

  Handle<SharedFunctionInfoWrapper> NewSharedFunctionInfoWrapper(
      DirectHandle<SharedFunctionInfo> sfi);

  Handle<PreparseData> NewPreparseData(int data_length, int children_length);

  Handle<UncompiledDataWithoutPreparseData>
  NewUncompiledDataWithoutPreparseData(Handle<String> inferred_name,
                                       int32_t start_position,
                                       int32_t end_position);

  Handle<UncompiledDataWithPreparseData> NewUncompiledDataWithPreparseData(
      Handle<String> inferred_name, int32_t start_position,
      int32_t end_position, Handle<PreparseData>);

  Handle<UncompiledDataWithoutPreparseDataWithJob>
  NewUncompiledDataWithoutPreparseDataWithJob(Handle<String> inferred_name,
                                              int32_t start_position,
                                              int32_t end_position);

  Handle<UncompiledDataWithPreparseDataAndJob>
  NewUncompiledDataWithPreparseDataAndJob(Handle<String> inferred_name,
                                          int32_t start_position,
                                          int32_t end_position,
                                          Handle<PreparseData>);

  // Allocates a FeedbackMetadata object and zeroes the data section.
  Handle<FeedbackMetadata> NewFeedbackMetadata(
      int slot_count, int create_closure_slot_count,
      AllocationType allocation = AllocationType::kOld);

  Handle<CoverageInfo> NewCoverageInfo(const ZoneVector<SourceRange>& slots);

  Handle<String> InternalizeString(base::Vector<const uint8_t> string,
                                   bool convert_encoding = false);
  Handle<String> InternalizeString(base::Vector<const uint16_t> string,
                                   bool convert_encoding = false);

  template <class StringTableKey>
  Handle<String> InternalizeStringWithKey(StringTableKey* key);

  Handle<SeqOneByteString> NewOneByteInternalizedString(
      base::Vector<const uint8_t> str, uint32_t raw_hash_field);
  Handle<SeqTwoByteString> NewTwoByteInternalizedString(
      base::Vector<const base::uc16> str, uint32_t raw_hash_field);
  Handle<SeqOneByteString> NewOneByteInternalizedStringFromTwoByte(
      base::Vector<const base::uc16> str, uint32_t raw_hash_field);

  Handle<SeqOneByteString> AllocateRawOneByteInternalizedString(
      int length, uint32_t raw_hash_field);
  Handle<SeqTwoByteString> AllocateRawTwoByteInternalizedString(
      int length, uint32_t raw_hash_field);

  // Creates a single character string where the character has given code.
  // A cache is used for Latin1 codes.
  Handle<String> LookupSingleCharacterStringFromCode(uint16_t code);

  MaybeHandle<String> NewStringFromOneByte(
      base::Vector<const uint8_t> string,
      AllocationType allocation = AllocationType::kYoung);

  inline Handle<String> NewStringFromAsciiChecked(
      const char* str, AllocationType allocation = AllocationType::kYoung) {
    return NewStringFromOneByte(base::OneByteVector(str), allocation)
        .ToHandleChecked();
  }

  // Allocates and partially initializes an one-byte or two-byte String. The
  // characters of the string are uninitialized. Currently used in regexp code
  // only, where they are pretenured.
  V8_WARN_UNUSED_RESULT MaybeHandle<SeqOneByteString> NewRawOneByteString(
      int length, AllocationType allocation = AllocationType::kYoung);
  V8_WARN_UNUSED_RESULT MaybeHandle<SeqTwoByteString> NewRawTwoByteString(
      int length, AllocationType allocation = AllocationType::kYoung);
  // Create a new cons string object which consists of a pair of strings.
  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewConsString(
      Handle<String> left, Handle<String> right,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT Handle<String> NewConsString(
      DirectHandle<String> left, DirectHandle<String> right, int length,
      bool one_byte, AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT Handle<String> NumberToString(
      DirectHandle<Object> number,
      NumberCacheMode mode = NumberCacheMode::kBoth);
  V8_WARN_UNUSED_RESULT Handle<String> HeapNumberToString(
      DirectHandle<HeapNumber> number, double value,
      NumberCacheMode mode = NumberCacheMode::kBoth);
  V8_WARN_UNUSED_RESULT Handle<String> SmiToString(
      Tagged<Smi> number, NumberCacheMode mode = NumberCacheMode::kBoth);

  V8_WARN_UNUSED_RESULT MaybeHandle<SeqOneByteString> NewRawSharedOneByteString(
      int length);
  V8_WARN_UNUSED_RESULT MaybeHandle<SeqTwoByteString> NewRawSharedTwoByteString(
      int length);

  // Allocates a new BigInt with {length} digits. Only to be used by
  // MutableBigInt::New*.
  Handle<FreshlyAllocatedBigInt> NewBigInt(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Create a serialized scope info.
  Handle<ScopeInfo> NewScopeInfo(int length,
                                 AllocationType type = AllocationType::kOld);

  Handle<SourceTextModuleInfo> NewSourceTextModuleInfo();

  Handle<DescriptorArray> NewDescriptorArray(
      int number_of_descriptors, int slack = 0,
      AllocationType allocation = AllocationType::kYoung);

  Handle<ClassPositions> NewClassPositions(int start, int end);

  Handle<SwissNameDictionary> NewSwissNameDictionary(
      int at_least_space_for = kSwissNameDictionaryInitialCapacity,
      AllocationType allocation = AllocationType::kYoung);

  Handle<SwissNameDictionary> NewSwissNameDictionaryWithCapacity(
      int capacity, AllocationType allocation);

  Handle<FunctionTemplateRareData> NewFunctionTemplateRareData();

  MaybeDirectHandle<Map> GetInPlaceInternalizedStringMap(
      Tagged<Map> from_string_map);

  AllocationType RefineAllocationTypeForInPlaceInternalizableString(
      AllocationType allocation, Tagged<Map> string_map);

 protected:
  // Must be large enough to fit any double, int, or size_t.
  static constexpr int kNumberToStringBufferSize = 32;

  // Allocate memory for an uninitialized array (e.g., a FixedArray or similar).
  Tagged<HeapObject> AllocateRawArray(int size, AllocationType allocation);
  Tagged<HeapObject> AllocateRawFixedArray(int length,
                                           AllocationType allocation);
  Tagged<HeapObject> AllocateRawWeakArrayList(int length,
                                              AllocationType allocation);

  template <typename StructType>
  inline Tagged<StructType> NewStructInternal(InstanceType type,
                                              AllocationType allocation);
  Tagged<Struct> NewStructInternal(ReadOnlyRoots roots, Tagged<Map> map,
                                   int size, AllocationType allocation);

  Tagged<HeapObject> AllocateRawWithImmortalMap(
      int size, AllocationType allocation, Tagged<Map> map,
      AllocationAlignment alignment = kTaggedAligned);
  Tagged<HeapObject> NewWithImmortalMap(Tagged<Map> map,
                                        AllocationType allocation);

  Handle<FixedArray> NewFixedArrayWithFiller(DirectHandle<Map> map, int length,
                                             DirectHandle<HeapObject> filler,
                                             AllocationType allocation);

  Handle<SharedFunctionInfo> NewSharedFunctionInfo(AllocationType allocation);
  Handle<SharedFunctionInfo> NewSharedFunctionInfo(
      MaybeDirectHandle<String> maybe_name,
      MaybeDirectHandle<HeapObject> maybe_function_data, Builtin builtin,
      FunctionKind kind = FunctionKind::kNormalFunction);

  Handle<String> MakeOrFindTwoCharacterString(uint16_t c1, uint16_t c2);

  template <typename SeqStringT>
  MaybeHandle<SeqStringT> NewRawStringWithMap(int length, Tagged<Map> map,
                                              AllocationType allocation);

 private:
  Impl* impl() { return static_cast<Impl*>(this); }
  auto isolate() { return impl()->isolate(); }
  ReadOnlyRoots read_only_roots() { return impl()->read_only_roots(); }

  Tagged<HeapObject> AllocateRaw(
      int size, AllocationType allocation,
      AllocationAlignment alignment = kTaggedAligned);

  friend TorqueGeneratedFactory<Impl>;
  template <class Derived, class Shape, class Super>
  friend class TaggedArrayBase;
  template <class Derived, class Shape, class Super>
  friend class PrimitiveArrayBase;
};

extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
    FactoryBase<Factory>;
extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
    FactoryBase<LocalFactory>;

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_BASE_H_
