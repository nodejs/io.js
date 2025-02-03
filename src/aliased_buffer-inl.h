#ifndef SRC_ALIASED_BUFFER_INL_H_
#define SRC_ALIASED_BUFFER_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer.h"
#include "memory_tracker-inl.h"
#include "util-inl.h"

namespace node {

typedef size_t AliasedBufferIndex;

template <typename NativeT, typename V8T>
AliasedBufferBase<NativeT, V8T>::AliasedBufferBase(
    v8::Isolate* isolate, const size_t count, const AliasedBufferIndex* index)
    : isolate_(isolate), count_(count), byte_offset_(0), index_(index) {
  CHECK_GT(count, 0);
  if (index != nullptr) {
    // Will be deserialized later.
    return;
  }
  const v8::HandleScope handle_scope(isolate_);
  const size_t size_in_bytes =
      MultiplyWithOverflowCheck(sizeof(NativeT), count);

  // allocate v8 ArrayBuffer
  v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate_, size_in_bytes);
  buffer_ = static_cast<NativeT*>(ab->Data());

  // allocate v8 TypedArray
  v8::Local<V8T> js_array = V8T::New(ab, byte_offset_, count);
  js_array_ = v8::Global<V8T>(isolate, js_array);
}

template <typename NativeT, typename V8T>
AliasedBufferBase<NativeT, V8T>::AliasedBufferBase(
    v8::Isolate* isolate,
    const size_t byte_offset,
    const size_t count,
    const AliasedBufferBase<uint8_t, v8::Uint8Array>& backing_buffer,
    const AliasedBufferIndex* index)
    : isolate_(isolate),
      count_(count),
      byte_offset_(byte_offset),
      index_(index) {
  if (index != nullptr) {
    // Will be deserialized later.
    return;
  }
  const v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::ArrayBuffer> ab = backing_buffer.GetArrayBuffer();

  // validate that the byte_offset is aligned with sizeof(NativeT)
  CHECK_EQ(byte_offset & (sizeof(NativeT) - 1), 0);
  // validate this fits inside the backing buffer
  CHECK_LE(MultiplyWithOverflowCheck(sizeof(NativeT), count),
           ab->ByteLength() - byte_offset);

  buffer_ = reinterpret_cast<NativeT*>(
      const_cast<uint8_t*>(backing_buffer.GetNativeBuffer() + byte_offset));

  v8::Local<V8T> js_array = V8T::New(ab, byte_offset, count);
  js_array_ = v8::Global<V8T>(isolate, js_array);
}

template <typename NativeT, typename V8T>
AliasedBufferBase<NativeT, V8T>::AliasedBufferBase(
    const AliasedBufferBase& that)
    : isolate_(that.isolate_),
      count_(that.count_),
      byte_offset_(that.byte_offset_),
      buffer_(that.buffer_) {
  js_array_ = v8::Global<V8T>(that.isolate_, that.GetJSArray());
  DCHECK(is_valid());
}

template <typename NativeT, typename V8T>
AliasedBufferIndex AliasedBufferBase<NativeT, V8T>::Serialize(
    v8::Local<v8::Context> context, v8::SnapshotCreator* creator) {
  DCHECK(is_valid());
  return creator->AddData(context, GetJSArray());
}

template <typename NativeT, typename V8T>
inline void AliasedBufferBase<NativeT, V8T>::Deserialize(
    v8::Local<v8::Context> context) {
  DCHECK_NOT_NULL(index_);
  v8::Local<V8T> arr =
      context->GetDataFromSnapshotOnce<V8T>(*index_).ToLocalChecked();
  // These may not hold true for AliasedBuffers that have grown, so should
  // be removed when we expand the snapshot support.
  DCHECK_EQ(count_, arr->Length());
  DCHECK_EQ(byte_offset_, arr->ByteOffset());
  uint8_t* raw = static_cast<uint8_t*>(arr->Buffer()->Data());
  buffer_ = reinterpret_cast<NativeT*>(raw + byte_offset_);
  js_array_.Reset(isolate_, arr);
  index_ = nullptr;
}

template <typename NativeT, typename V8T>
AliasedBufferBase<NativeT, V8T>& AliasedBufferBase<NativeT, V8T>::operator=(
    AliasedBufferBase<NativeT, V8T>&& that) noexcept {
  DCHECK(is_valid());
  this->~AliasedBufferBase();
  isolate_ = that.isolate_;
  count_ = that.count_;
  byte_offset_ = that.byte_offset_;
  buffer_ = that.buffer_;

  js_array_.Reset(isolate_, that.js_array_.Get(isolate_));

  that.buffer_ = nullptr;
  that.js_array_.Reset();
  return *this;
}

template <typename NativeT, typename V8T>
v8::Local<V8T> AliasedBufferBase<NativeT, V8T>::GetJSArray() const {
  DCHECK(is_valid());
  return js_array_.Get(isolate_);
}

template <typename NativeT, typename V8T>
void AliasedBufferBase<NativeT, V8T>::Release() {
  DCHECK_NULL(index_);
  js_array_.Reset();
}

template <typename NativeT, typename V8T>
inline void AliasedBufferBase<NativeT, V8T>::MakeWeak() {
  DCHECK(is_valid());
  js_array_.SetWeak();
}

template <typename NativeT, typename V8T>
v8::Local<v8::ArrayBuffer> AliasedBufferBase<NativeT, V8T>::GetArrayBuffer()
    const {
  return GetJSArray()->Buffer();
}

template <typename NativeT, typename V8T>
inline const NativeT* AliasedBufferBase<NativeT, V8T>::GetNativeBuffer() const {
  DCHECK(is_valid());
  return buffer_;
}

template <typename NativeT, typename V8T>
inline const NativeT* AliasedBufferBase<NativeT, V8T>::operator*() const {
  return GetNativeBuffer();
}

template <typename NativeT, typename V8T>
inline void AliasedBufferBase<NativeT, V8T>::SetValue(const size_t index,
                                                      NativeT value) {
  DCHECK_LT(index, count_);
  DCHECK(is_valid());
  buffer_[index] = value;
}

template <typename NativeT, typename V8T>
inline const NativeT AliasedBufferBase<NativeT, V8T>::GetValue(
    const size_t index) const {
  DCHECK(is_valid());
  DCHECK_LT(index, count_);
  return buffer_[index];
}

template <typename NativeT, typename V8T>
typename AliasedBufferBase<NativeT, V8T>::Reference
AliasedBufferBase<NativeT, V8T>::operator[](size_t index) {
  DCHECK(is_valid());
  return Reference(this, index);
}

template <typename NativeT, typename V8T>
NativeT AliasedBufferBase<NativeT, V8T>::operator[](size_t index) const {
  return GetValue(index);
}

template <typename NativeT, typename V8T>
size_t AliasedBufferBase<NativeT, V8T>::Length() const {
  return count_;
}

template <typename NativeT, typename V8T>
void AliasedBufferBase<NativeT, V8T>::reserve(size_t new_capacity) {
  DCHECK(is_valid());
  DCHECK_GE(new_capacity, count_);
  DCHECK_EQ(byte_offset_, 0);
  const v8::HandleScope handle_scope(isolate_);

  const size_t old_size_in_bytes = sizeof(NativeT) * count_;
  const size_t new_size_in_bytes =
      MultiplyWithOverflowCheck(sizeof(NativeT), new_capacity);

  // allocate v8 new ArrayBuffer
  v8::Local<v8::ArrayBuffer> ab =
      v8::ArrayBuffer::New(isolate_, new_size_in_bytes);

  // allocate new native buffer
  NativeT* new_buffer = static_cast<NativeT*>(ab->Data());
  // copy old content
  memcpy(new_buffer, buffer_, old_size_in_bytes);

  // allocate v8 TypedArray
  v8::Local<V8T> js_array = V8T::New(ab, byte_offset_, new_capacity);

  // move over old v8 TypedArray
  js_array_ = std::move(v8::Global<V8T>(isolate_, js_array));

  buffer_ = new_buffer;
  count_ = new_capacity;
}

template <typename NativeT, typename V8T>
inline bool AliasedBufferBase<NativeT, V8T>::is_valid() const {
  return index_ == nullptr && !js_array_.IsEmpty();
}

template <typename NativeT, typename V8T>
inline size_t AliasedBufferBase<NativeT, V8T>::SelfSize() const {
  return sizeof(*this);
}

#define VM(NativeT, V8T)                                                       \
  template <>                                                                  \
  inline const char* AliasedBufferBase<NativeT, v8::V8T>::MemoryInfoName()     \
      const {                                                                  \
    return "Aliased" #V8T;                                                     \
  }                                                                            \
  template <>                                                                  \
  inline void AliasedBufferBase<NativeT, v8::V8T>::MemoryInfo(                 \
      node::MemoryTracker* tracker) const {                                    \
    tracker->TrackField("js_array", js_array_);                                \
  }
ALIASED_BUFFER_LIST(VM)
#undef VM

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ALIASED_BUFFER_INL_H_
