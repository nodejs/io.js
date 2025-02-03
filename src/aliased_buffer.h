#ifndef SRC_ALIASED_BUFFER_H_
#define SRC_ALIASED_BUFFER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include "memory_tracker.h"
#include "v8.h"

namespace node {

typedef size_t AliasedBufferIndex;

/**
 * Do not use this class directly when creating instances of it - use the
 * Aliased*Array defined at the end of this file instead.
 *
 * This class encapsulates the technique of having a native buffer mapped to
 * a JS object. Writes to the native buffer can happen efficiently without
 * going through JS, and the data is then available to user's via the exposed
 * JS object.
 *
 * While this technique is computationally efficient, it is effectively a
 * write to JS program state w/out going through the standard
 * (monitored) API. Thus any VM capabilities to detect the modification are
 * circumvented.
 *
 * The encapsulation herein provides a placeholder where such writes can be
 * observed. Any notification APIs will be left as a future exercise.
 */
template <class NativeT, class V8T>
class AliasedBufferBase : public MemoryRetainer {
 public:
  static_assert(std::is_scalar<NativeT>::value);

  AliasedBufferBase(v8::Isolate* isolate,
                    const size_t count,
                    const AliasedBufferIndex* index = nullptr);

  /**
   * Create an AliasedBufferBase over a sub-region of another aliased buffer.
   * The two will share a v8::ArrayBuffer instance &
   * a native buffer, but will each read/write to different sections of the
   * native buffer.
   *
   *  Note that byte_offset must by aligned by sizeof(NativeT).
   */
  // TODO(refack): refactor into a non-owning `AliasedBufferBaseView`
  AliasedBufferBase(
      v8::Isolate* isolate,
      const size_t byte_offset,
      const size_t count,
      const AliasedBufferBase<uint8_t, v8::Uint8Array>& backing_buffer,
      const AliasedBufferIndex* index = nullptr);

  AliasedBufferBase(const AliasedBufferBase& that);

  AliasedBufferIndex Serialize(v8::Local<v8::Context> context,
                               v8::SnapshotCreator* creator);

  inline void Deserialize(v8::Local<v8::Context> context);

  AliasedBufferBase& operator=(AliasedBufferBase&& that) noexcept;

  /**
   * Helper class that is returned from operator[] to support assignment into
   * a specified location.
   */
  class Reference {
   public:
    Reference(AliasedBufferBase<NativeT, V8T>* aliased_buffer, size_t index)
        : aliased_buffer_(aliased_buffer), index_(index) {}

    Reference(const Reference& that)
        : aliased_buffer_(that.aliased_buffer_),
          index_(that.index_) {
    }

    inline Reference& operator=(const NativeT& val) {
      aliased_buffer_->SetValue(index_, val);
      return *this;
    }

    inline Reference& operator=(const Reference& val) {
      return *this = static_cast<NativeT>(val);
    }

    operator NativeT() const {
      return aliased_buffer_->GetValue(index_);
    }

    inline Reference& operator+=(const NativeT& val) {
      const NativeT current = aliased_buffer_->GetValue(index_);
      aliased_buffer_->SetValue(index_, current + val);
      return *this;
    }

    inline Reference& operator+=(const Reference& val) {
      return this->operator+=(static_cast<NativeT>(val));
    }

    inline Reference& operator-=(const NativeT& val) {
      const NativeT current = aliased_buffer_->GetValue(index_);
      aliased_buffer_->SetValue(index_, current - val);
      return *this;
    }

   private:
    AliasedBufferBase<NativeT, V8T>* aliased_buffer_;
    size_t index_;
  };

  /**
   *  Get the underlying v8 TypedArray overlaid on top of the native buffer
   */
  v8::Local<V8T> GetJSArray() const;

  void Release();

  /**
   * Make the global reference to the typed array weak. The caller must make
   * sure that no operation can be done on the AliasedBuffer when the typed
   * array becomes unreachable. Usually this means the caller must maintain
   * a JS reference to the typed array from JS object.
   */
  inline void MakeWeak();

  /**
  *  Get the underlying v8::ArrayBuffer underlying the TypedArray and
  *  overlaying the native buffer
  */
  v8::Local<v8::ArrayBuffer> GetArrayBuffer() const;

  /**
   *  Get the underlying native buffer. Note that all reads/writes should occur
   *  through the GetValue/SetValue/operator[] methods
   */
  inline const NativeT* GetNativeBuffer() const;

  /**
   *  Synonym for GetBuffer()
   */
  inline const NativeT* operator*() const;

  /**
   *  Set position index to given value.
   */
  inline void SetValue(const size_t index, NativeT value);

  /**
   *  Get value at position index
   */
  inline const NativeT GetValue(const size_t index) const;

  /**
   *  Effectively, a synonym for GetValue/SetValue
   */
  Reference operator[](size_t index);

  NativeT operator[](size_t index) const;

  size_t Length() const;

  // Should only be used to extend the array.
  // Should only be used on an owning array, not one created as a sub array of
  // an owning `AliasedBufferBase`.
  void reserve(size_t new_capacity);

  inline size_t SelfSize() const override;

  inline const char* MemoryInfoName() const override;
  inline void MemoryInfo(node::MemoryTracker* tracker) const override;

 private:
  inline bool is_valid() const;
  v8::Isolate* isolate_ = nullptr;
  size_t count_ = 0;
  size_t byte_offset_ = 0;
  NativeT* buffer_ = nullptr;
  v8::Global<V8T> js_array_;

  // Deserialize data
  const AliasedBufferIndex* index_ = nullptr;
};

#define ALIASED_BUFFER_LIST(V)                                                 \
  V(int8_t, Int8Array)                                                         \
  V(uint8_t, Uint8Array)                                                       \
  V(int16_t, Int16Array)                                                       \
  V(uint16_t, Uint16Array)                                                     \
  V(int32_t, Int32Array)                                                       \
  V(uint32_t, Uint32Array)                                                     \
  V(float, Float32Array)                                                       \
  V(double, Float64Array)                                                      \
  V(int64_t, BigInt64Array)

#define V(NativeT, V8T)                                                        \
  typedef AliasedBufferBase<NativeT, v8::V8T> Aliased##V8T;
ALIASED_BUFFER_LIST(V)
#undef V

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ALIASED_BUFFER_H_
