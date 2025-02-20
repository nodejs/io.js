// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_ATOMIC_UTILS_H_
#define V8_BASE_ATOMIC_UTILS_H_

#include <limits.h>

#include <atomic>
#include <type_traits>

#include "src/base/atomicops.h"
#include "src/base/macros.h"
#include "src/base/strong-alias.h"

namespace v8 {
namespace base {

// Deprecated. Use std::atomic<T> for new code.
// Flag using T atomically. Also accepts void* as T.
template <typename T>
class AtomicValue {
 public:
  AtomicValue() : value_(0) {}

  explicit AtomicValue(T initial)
      : value_(cast_helper<T>::to_storage_type(initial)) {}

  V8_INLINE T Value() const {
    return cast_helper<T>::to_return_type(base::Acquire_Load(&value_));
  }

  V8_INLINE void SetValue(T new_value) {
    base::Release_Store(&value_, cast_helper<T>::to_storage_type(new_value));
  }

 private:
  static_assert(sizeof(T) <= sizeof(base::AtomicWord));

  template <typename S>
  struct cast_helper {
    static base::AtomicWord to_storage_type(S value) {
      return static_cast<base::AtomicWord>(value);
    }
    static S to_return_type(base::AtomicWord value) {
      return static_cast<S>(value);
    }
  };

  template <typename S>
  struct cast_helper<S*> {
    static base::AtomicWord to_storage_type(S* value) {
      return reinterpret_cast<base::AtomicWord>(value);
    }
    static S* to_return_type(base::AtomicWord value) {
      return reinterpret_cast<S*>(value);
    }
  };

  base::AtomicWord value_;
};

// Provides atomic operations for a values stored at some address.
template <typename TAtomicStorageType>
class AsAtomicImpl {
 public:
  using AtomicStorageType = TAtomicStorageType;

  template <typename T>
  static T SeqCst_Load(T* addr) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(
        base::SeqCst_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static T Acquire_Load(T* addr) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(
        base::Acquire_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static T Relaxed_Load(T* addr) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(
        base::Relaxed_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static void SeqCst_Store(T* addr,
                           typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    base::SeqCst_Store(to_storage_addr(addr),
                       cast_helper<T>::to_storage_type(new_value));
  }

  template <typename T>
  static void Release_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    base::Release_Store(to_storage_addr(addr),
                        cast_helper<T>::to_storage_type(new_value));
  }

  template <typename T>
  static void Relaxed_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    base::Relaxed_Store(to_storage_addr(addr),
                        cast_helper<T>::to_storage_type(new_value));
  }

  template <typename T>
  static T SeqCst_Swap(T* addr,
                       typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return base::SeqCst_AtomicExchange(
        to_storage_addr(addr), cast_helper<T>::to_storage_type(new_value));
  }

  template <typename T>
  static T Release_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(base::Release_CompareAndSwap(
        to_storage_addr(addr), cast_helper<T>::to_storage_type(old_value),
        cast_helper<T>::to_storage_type(new_value)));
  }

  template <typename T>
  static T Relaxed_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(base::Relaxed_CompareAndSwap(
        to_storage_addr(addr), cast_helper<T>::to_storage_type(old_value),
        cast_helper<T>::to_storage_type(new_value)));
  }

  template <typename T>
  static T AcquireRelease_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(base::AcquireRelease_CompareAndSwap(
        to_storage_addr(addr), cast_helper<T>::to_storage_type(old_value),
        cast_helper<T>::to_storage_type(new_value)));
  }

  template <typename T>
  static T SeqCst_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    return cast_helper<T>::to_return_type(base::SeqCst_CompareAndSwap(
        to_storage_addr(addr), cast_helper<T>::to_storage_type(old_value),
        cast_helper<T>::to_storage_type(new_value)));
  }

  // Atomically sets bits selected by the mask to the given value.
  // Returns false if the bits are already set as needed.
  template <typename T>
  static bool Release_SetBits(T* addr, T bits, T mask) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    DCHECK_EQ(bits & ~mask, static_cast<T>(0));
    T old_value = Relaxed_Load(addr);
    T new_value, old_value_before_cas;
    do {
      if ((old_value & mask) == bits) return false;
      new_value = (old_value & ~mask) | bits;
      old_value_before_cas = old_value;
      old_value = Release_CompareAndSwap(addr, old_value, new_value);
    } while (old_value != old_value_before_cas);
    return true;
  }

  // Atomically sets bits selected by the mask to the given value.
  // Returns false if the bits are already set as needed.
  template <typename T>
  static bool Relaxed_SetBits(T* addr, T bits, T mask) {
    static_assert(sizeof(T) <= sizeof(AtomicStorageType));
    DCHECK_EQ(bits & ~mask, static_cast<T>(0));
    T old_value = Relaxed_Load(addr);
    T new_value, old_value_before_cas;
    do {
      if ((old_value & mask) == bits) return false;
      new_value = (old_value & ~mask) | bits;
      old_value_before_cas = old_value;
      old_value = Relaxed_CompareAndSwap(addr, old_value, new_value);
    } while (old_value != old_value_before_cas);
    return true;
  }

 private:
  template <typename U>
  struct cast_helper {
    static AtomicStorageType to_storage_type(U value) {
      return static_cast<AtomicStorageType>(value);
    }
    static U to_return_type(AtomicStorageType value) {
      return static_cast<U>(value);
    }
  };

  template <typename U>
  struct cast_helper<U*> {
    static AtomicStorageType to_storage_type(U* value) {
      return reinterpret_cast<AtomicStorageType>(value);
    }
    static U* to_return_type(AtomicStorageType value) {
      return reinterpret_cast<U*>(value);
    }
  };
  template <typename T, typename U>
  struct cast_helper<base::StrongAlias<T, U>> {
    static AtomicStorageType to_storage_type(base::StrongAlias<T, U> value) {
      return static_cast<AtomicStorageType>(value.value());
    }
    static base::StrongAlias<T, U> to_return_type(AtomicStorageType value) {
      return base::StrongAlias<T, U>(static_cast<U>(value));
    }
  };

  template <typename T>
  static AtomicStorageType* to_storage_addr(T* value) {
    return reinterpret_cast<AtomicStorageType*>(value);
  }
  template <typename T>
  static const AtomicStorageType* to_storage_addr(const T* value) {
    return reinterpret_cast<const AtomicStorageType*>(value);
  }
};

using AsAtomic8 = AsAtomicImpl<base::Atomic8>;
using AsAtomic16 = AsAtomicImpl<base::Atomic16>;
using AsAtomic32 = AsAtomicImpl<base::Atomic32>;
using AsAtomicWord = AsAtomicImpl<base::AtomicWord>;

template <int Width>
struct AtomicTypeFromByteWidth {};
template <>
struct AtomicTypeFromByteWidth<1> {
  using type = base::Atomic8;
};
template <>
struct AtomicTypeFromByteWidth<2> {
  using type = base::Atomic16;
};
template <>
struct AtomicTypeFromByteWidth<4> {
  using type = base::Atomic32;
};
#if V8_HOST_ARCH_64_BIT
template <>
struct AtomicTypeFromByteWidth<8> {
  using type = base::Atomic64;
};
#endif

// This is similar to AsAtomicWord but it explicitly deletes functionality
// provided atomic access to bit representation of stored values.
template <typename TAtomicStorageType>
class AsAtomicPointerImpl : public AsAtomicImpl<TAtomicStorageType> {
 public:
  template <typename T>
  static bool SetBits(T* addr, T bits, T mask) = delete;
};

using AsAtomicPointer = AsAtomicPointerImpl<base::AtomicWord>;

template <typename T>
inline void CheckedIncrement(
    std::atomic<T>* number, T amount,
    std::memory_order order = std::memory_order_seq_cst)
  requires std::is_unsigned<T>::value
{
  const T old = number->fetch_add(amount, order);
  DCHECK_GE(old + amount, old);
  USE(old);
}

template <typename T>
inline void CheckedDecrement(
    std::atomic<T>* number, T amount,
    std::memory_order order = std::memory_order_seq_cst)
  requires std::is_unsigned<T>::value
{
  const T old = number->fetch_sub(amount, order);
  DCHECK_GE(old, amount);
  USE(old);
}

template <typename T>
V8_INLINE std::atomic<T>* AsAtomicPtr(T* t) {
  static_assert(sizeof(T) == sizeof(std::atomic<T>));
  static_assert(alignof(T) >= alignof(std::atomic<T>));
  return reinterpret_cast<std::atomic<T>*>(t);
}

template <typename T>
V8_INLINE const std::atomic<T>* AsAtomicPtr(const T* t) {
  static_assert(sizeof(T) == sizeof(std::atomic<T>));
  static_assert(alignof(T) >= alignof(std::atomic<T>));
  return reinterpret_cast<const std::atomic<T>*>(t);
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ATOMIC_UTILS_H_
