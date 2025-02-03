// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_VALUE_SERIALIZER_H_
#define V8_OBJECTS_VALUE_SERIALIZER_H_

#include <cstdint>

#include "include/v8-value-serializer.h"
#include "src/base/compiler-specific.h"
#include "src/base/macros.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/message-template.h"
#include "src/handles/maybe-handles.h"
#include "src/utils/identity-map.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class BigInt;
class HeapNumber;
class Isolate;
class JSArrayBuffer;
class JSArrayBufferView;
class JSDate;
class JSMap;
class JSPrimitiveWrapper;
class JSRegExp;
class JSSet;
class JSSharedArray;
class JSSharedStruct;
class Object;
class Oddball;
class SharedObjectConveyorHandles;
class Smi;
class WasmMemoryObject;
class WasmModuleObject;

enum class SerializationTag : uint8_t;

/**
 * Writes V8 objects in a binary format that allows the objects to be cloned
 * according to the HTML structured clone algorithm.
 *
 * Format is based on Blink's previous serialization logic.
 */
class ValueSerializer {
 public:
  ValueSerializer(Isolate* isolate, v8::ValueSerializer::Delegate* delegate);
  ~ValueSerializer();
  ValueSerializer(const ValueSerializer&) = delete;
  ValueSerializer& operator=(const ValueSerializer&) = delete;

  /*
   * Writes out a header, which includes the format version.
   */
  void WriteHeader();

  /*
   * Serializes a V8 object into the buffer.
   */
  Maybe<bool> WriteObject(Handle<Object> object) V8_WARN_UNUSED_RESULT;

  /*
   * Returns the buffer, allocated via the delegate, and its size.
   * Caller assumes ownership of the buffer.
   */
  std::pair<uint8_t*, size_t> Release();

  /*
   * Marks an ArrayBuffer as havings its contents transferred out of band.
   * Pass the corresponding JSArrayBuffer in the deserializing context to
   * ValueDeserializer::TransferArrayBuffer.
   */
  void TransferArrayBuffer(uint32_t transfer_id,
                           DirectHandle<JSArrayBuffer> array_buffer);

  /*
   * Publicly exposed wire format writing methods.
   * These are intended for use within the delegate's WriteHostObject method.
   */
  void WriteUint32(uint32_t value);
  void WriteUint64(uint64_t value);
  void WriteRawBytes(const void* source, size_t length);
  void WriteDouble(double value);
  void WriteByte(uint8_t value);

  /*
   * Indicate whether to treat ArrayBufferView objects as host objects,
   * i.e. pass them to Delegate::WriteHostObject. This should not be
   * called when no Delegate was passed.
   *
   * The default is not to treat ArrayBufferViews as host objects.
   */
  void SetTreatArrayBufferViewsAsHostObjects(bool mode);

 private:
  // Managing allocations of the internal buffer.
  Maybe<bool> ExpandBuffer(size_t required_capacity);

  // Writing the wire format.
  void WriteTag(SerializationTag tag);
  template <typename T>
  void WriteVarint(T value);
  template <typename T>
  void WriteZigZag(T value);
  void WriteOneByteString(base::Vector<const uint8_t> chars);
  void WriteTwoByteString(base::Vector<const base::uc16> chars);
  void WriteBigIntContents(Tagged<BigInt> bigint);
  Maybe<uint8_t*> ReserveRawBytes(size_t bytes);

  // Writing V8 objects of various kinds.
  void WriteOddball(Tagged<Oddball> oddball);
  void WriteSmi(Tagged<Smi> smi);
  void WriteHeapNumber(Tagged<HeapNumber> number);
  void WriteBigInt(Tagged<BigInt> bigint);
  void WriteString(Handle<String> string);
  Maybe<bool> WriteJSReceiver(Handle<JSReceiver> receiver)
      V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSObject(Handle<JSObject> object) V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSObjectSlow(Handle<JSObject> object) V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSArray(Handle<JSArray> array) V8_WARN_UNUSED_RESULT;
  void WriteJSDate(Tagged<JSDate> date);
  Maybe<bool> WriteJSPrimitiveWrapper(DirectHandle<JSPrimitiveWrapper> value)
      V8_WARN_UNUSED_RESULT;
  void WriteJSRegExp(DirectHandle<JSRegExp> regexp);
  Maybe<bool> WriteJSMap(DirectHandle<JSMap> map) V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSSet(DirectHandle<JSSet> map) V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSArrayBuffer(Handle<JSArrayBuffer> array_buffer)
      V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSArrayBufferView(Tagged<JSArrayBufferView> array_buffer);
  Maybe<bool> WriteJSError(Handle<JSObject> error) V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSSharedArray(DirectHandle<JSSharedArray> shared_array)
      V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteJSSharedStruct(DirectHandle<JSSharedStruct> shared_struct)
      V8_WARN_UNUSED_RESULT;
#if V8_ENABLE_WEBASSEMBLY
  Maybe<bool> WriteWasmModule(Handle<WasmModuleObject> object)
      V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteWasmMemory(DirectHandle<WasmMemoryObject> object)
      V8_WARN_UNUSED_RESULT;
#endif  // V8_ENABLE_WEBASSEMBLY
  Maybe<bool> WriteSharedObject(DirectHandle<HeapObject> object)
      V8_WARN_UNUSED_RESULT;
  Maybe<bool> WriteHostObject(Handle<JSObject> object) V8_WARN_UNUSED_RESULT;

  /*
   * Reads the specified keys from the object and writes key-value pairs to the
   * buffer. Returns the number of keys actually written, which may be smaller
   * if some keys are not own properties when accessed.
   */
  Maybe<uint32_t> WriteJSObjectPropertiesSlow(Handle<JSObject> object,
                                              DirectHandle<FixedArray> keys)
      V8_WARN_UNUSED_RESULT;

  Maybe<bool> IsHostObject(Handle<JSObject> object);

  /*
   * Asks the delegate to handle an error that occurred during data cloning, by
   * throwing an exception appropriate for the host.
   */
  V8_NOINLINE Maybe<bool> ThrowDataCloneError(MessageTemplate template_index)
      V8_WARN_UNUSED_RESULT;
  V8_NOINLINE Maybe<bool> ThrowDataCloneError(MessageTemplate template_index,
                                              DirectHandle<Object> arg0)
      V8_WARN_UNUSED_RESULT;

  Maybe<bool> ThrowIfOutOfMemory();

  Isolate* const isolate_;
  v8::ValueSerializer::Delegate* const delegate_;
  uint8_t* buffer_ = nullptr;
  size_t buffer_size_ = 0;
  size_t buffer_capacity_ = 0;
  bool has_custom_host_objects_ = false;
  bool treat_array_buffer_views_as_host_objects_ = false;
  bool out_of_memory_ = false;
  Zone zone_;

  // To avoid extra lookups in the identity map, ID+1 is actually stored in the
  // map (checking if the used identity is zero is the fast way of checking if
  // the entry is new).
  IdentityMap<uint32_t, ZoneAllocationPolicy> id_map_;
  uint32_t next_id_ = 0;

  // A similar map, for transferred array buffers.
  IdentityMap<uint32_t, ZoneAllocationPolicy> array_buffer_transfer_map_;

  // The conveyor used to keep shared objects alive.
  SharedObjectConveyorHandles* shared_object_conveyor_ = nullptr;
};

/*
 * Deserializes values from data written with ValueSerializer, or a compatible
 * implementation.
 */
class ValueDeserializer {
 public:
  ValueDeserializer(Isolate* isolate, base::Vector<const uint8_t> data,
                    v8::ValueDeserializer::Delegate* delegate);
  ValueDeserializer(Isolate* isolate, const uint8_t* data, size_t size);
  ~ValueDeserializer();
  ValueDeserializer(const ValueDeserializer&) = delete;
  ValueDeserializer& operator=(const ValueDeserializer&) = delete;

  /*
   * Runs version detection logic, which may fail if the format is invalid.
   */
  Maybe<bool> ReadHeader() V8_WARN_UNUSED_RESULT;

  /*
   * Reads the underlying wire format version. Likely mostly to be useful to
   * legacy code reading old wire format versions. Must be called after
   * ReadHeader.
   */
  uint32_t GetWireFormatVersion() const { return version_; }

  /*
   * Deserializes a V8 object from the buffer.
   */
  MaybeHandle<Object> ReadObjectWrapper() V8_WARN_UNUSED_RESULT;

  /*
   * Reads an object, consuming the entire buffer.
   *
   * This is required for the legacy "version 0" format, which did not allow
   * reference deduplication, and instead relied on a "stack" model for
   * deserializing, with the contents of objects and arrays provided first.
   */
  MaybeHandle<Object> ReadObjectUsingEntireBufferForLegacyFormat()
      V8_WARN_UNUSED_RESULT;

  /*
   * Accepts the array buffer corresponding to the one passed previously to
   * ValueSerializer::TransferArrayBuffer.
   */
  void TransferArrayBuffer(uint32_t transfer_id,
                           Handle<JSArrayBuffer> array_buffer);

  /*
   * Publicly exposed wire format writing methods.
   * These are intended for use within the delegate's WriteHostObject method.
   */
  bool ReadUint32(uint32_t* value) V8_WARN_UNUSED_RESULT;
  bool ReadUint64(uint64_t* value) V8_WARN_UNUSED_RESULT;
  bool ReadDouble(double* value) V8_WARN_UNUSED_RESULT;
  bool ReadRawBytes(size_t length, const void** data) V8_WARN_UNUSED_RESULT;
  bool ReadByte(uint8_t* value) V8_WARN_UNUSED_RESULT;

 private:
  // Reading the wire format.
  Maybe<SerializationTag> PeekTag() const V8_WARN_UNUSED_RESULT;
  void ConsumeTag(SerializationTag peeked_tag);
  Maybe<SerializationTag> ReadTag() V8_WARN_UNUSED_RESULT;
  template <typename T>
  V8_INLINE Maybe<T> ReadVarint() V8_WARN_UNUSED_RESULT;
  template <typename T>
  V8_NOINLINE Maybe<T> ReadVarintLoop() V8_WARN_UNUSED_RESULT;
  template <typename T>
  Maybe<T> ReadZigZag() V8_WARN_UNUSED_RESULT;
  Maybe<double> ReadDouble() V8_WARN_UNUSED_RESULT;
  Maybe<base::Vector<const uint8_t>> ReadRawBytes(size_t size)
      V8_WARN_UNUSED_RESULT;
  Maybe<base::Vector<const base::uc16>> ReadRawTwoBytes(size_t size)
      V8_WARN_UNUSED_RESULT;
  MaybeHandle<Object> ReadObject() V8_WARN_UNUSED_RESULT;

  // Like ReadObject, but skips logic for special cases in simulating the
  // "stack machine".
  MaybeHandle<Object> ReadObjectInternal() V8_WARN_UNUSED_RESULT;

  // Reads a string intended to be part of a more complicated object.
  // Before v12, these are UTF-8 strings. After, they can be any encoding
  // permissible for a string (with the relevant tag).
  MaybeHandle<String> ReadString() V8_WARN_UNUSED_RESULT;

  // Reading V8 objects of specific kinds.
  // The tag is assumed to have already been read.
  MaybeHandle<BigInt> ReadBigInt() V8_WARN_UNUSED_RESULT;
  MaybeHandle<String> ReadUtf8String(
      AllocationType allocation = AllocationType::kYoung) V8_WARN_UNUSED_RESULT;
  MaybeHandle<String> ReadOneByteString(
      AllocationType allocation = AllocationType::kYoung) V8_WARN_UNUSED_RESULT;
  MaybeHandle<String> ReadTwoByteString(
      AllocationType allocation = AllocationType::kYoung) V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSObject> ReadJSObject() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSArray> ReadSparseJSArray() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSArray> ReadDenseJSArray() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSDate> ReadJSDate() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSPrimitiveWrapper> ReadJSPrimitiveWrapper(SerializationTag tag)
      V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSRegExp> ReadJSRegExp() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSMap> ReadJSMap() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSSet> ReadJSSet() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSArrayBuffer> ReadJSArrayBuffer(
      bool is_shared, bool is_resizable) V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSArrayBuffer> ReadTransferredJSArrayBuffer()
      V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSArrayBufferView> ReadJSArrayBufferView(
      DirectHandle<JSArrayBuffer> buffer) V8_WARN_UNUSED_RESULT;
  bool ValidateJSArrayBufferViewFlags(
      Tagged<JSArrayBuffer> buffer, uint32_t serialized_flags,
      bool& is_length_tracking, bool& is_backed_by_rab) V8_WARN_UNUSED_RESULT;
  MaybeHandle<Object> ReadJSError() V8_WARN_UNUSED_RESULT;
#if V8_ENABLE_WEBASSEMBLY
  MaybeHandle<JSObject> ReadWasmModuleTransfer() V8_WARN_UNUSED_RESULT;
  MaybeHandle<WasmMemoryObject> ReadWasmMemory() V8_WARN_UNUSED_RESULT;
#endif  // V8_ENABLE_WEBASSEMBLY
  MaybeHandle<HeapObject> ReadSharedObject() V8_WARN_UNUSED_RESULT;
  MaybeHandle<JSObject> ReadHostObject() V8_WARN_UNUSED_RESULT;

  /*
   * Reads key-value pairs into the object until the specified end tag is
   * encountered. If successful, returns the number of properties read.
   */
  Maybe<uint32_t> ReadJSObjectProperties(Handle<JSObject> object,
                                         SerializationTag end_tag,
                                         bool can_use_transitions);

  // Manipulating the map from IDs to reified objects.
  bool HasObjectWithID(uint32_t id);
  MaybeHandle<JSReceiver> GetObjectWithID(uint32_t id);
  void AddObjectWithID(uint32_t id, DirectHandle<JSReceiver> object);

  Isolate* const isolate_;
  v8::ValueDeserializer::Delegate* const delegate_;
  const uint8_t* position_;
  const uint8_t* const end_;
  uint32_t version_ = 0;
  uint32_t next_id_ = 0;
  bool version_13_broken_data_mode_ = false;
  bool suppress_deserialization_errors_ = false;

  // Always global handles.
  Handle<FixedArray> id_map_;
  MaybeHandle<SimpleNumberDictionary> array_buffer_transfer_map_;

  // The conveyor used to keep shared objects alive.
  const SharedObjectConveyorHandles* shared_object_conveyor_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_VALUE_SERIALIZER_H_
