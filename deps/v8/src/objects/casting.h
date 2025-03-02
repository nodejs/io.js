// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CASTING_H_
#define V8_OBJECTS_CASTING_H_

#include <type_traits>

#include "include/v8-source-location.h"
#include "src/base/logging.h"
#include "src/objects/tagged.h"

namespace v8::internal {

// CastTraits<T> is a type trait that defines type checking behaviour for
// tagged object casting. The expected specialization is:
//
//     template<>
//     struct CastTraits<SomeObject> {
//       template<typename From>
//       static bool AllowFrom(Tagged<From> value) {
//         return IsSomeObject(value);
//       }
//     };
//
// or, likely, just specializations of AllowFrom for Object and HeapObject,
// under the assumption that the HeapObject implementation is the same for all
// HeapObjects and the Object implementation has additional overhead in Smi
// checks.
//
//     struct CastTraits<Object> {
//       static bool AllowFrom(Tagged<HeapObject> value) {
//         return IsSomeObject(value);
//       }
//       static bool AllowFrom(Tagged<Object> value) {
//         return IsSomeObject(value);
//       }
//     };
//
template <typename To>
struct CastTraits;

// `Is<T>(value)` checks whether `value` is a tagged object of type `T`.
template <typename T, typename U>
inline bool Is(Tagged<U> value) {
  return CastTraits<T>::AllowFrom(value);
}
template <typename T, typename U>
inline bool Is(IndirectHandle<U> value);
template <typename T, typename U>
inline bool Is(MaybeIndirectHandle<U> value);
template <typename T, typename U>
inline bool Is(DirectHandle<U> value);
template <typename T, typename U>
inline bool Is(MaybeDirectHandle<U> value);

// `UncheckedCast<T>(value)` casts `value` to a tagged object of type `T`,
// without checking the type of value.
template <typename To, typename From>
inline Tagged<To> UncheckedCast(Tagged<From> value) {
  return Tagged<To>(value.ptr());
}
template <typename To, typename From>
inline IndirectHandle<To> UncheckedCast(IndirectHandle<From> value);
template <typename To, typename From>
inline MaybeIndirectHandle<To> UncheckedCast(MaybeIndirectHandle<From> value);
template <typename To, typename From>
inline DirectHandle<To> UncheckedCast(DirectHandle<From> value);
template <typename To, typename From>
inline MaybeDirectHandle<To> UncheckedCast(MaybeDirectHandle<From> value);

// `TryCast<T>(value, &out)` casts `value` to a tagged object of type `T` and
// writes the value to `out`, returning true if the cast succeeded and false if
// it failed.
template <typename To, typename From>
inline bool TryCast(Tagged<From> value, Tagged<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(IndirectHandle<From> value, IndirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(IndirectHandle<From> value, DirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(DirectHandle<From> value, DirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(MaybeIndirectHandle<From> value,
                    MaybeIndirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(MaybeIndirectHandle<From> value,
                    MaybeDirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}
template <typename To, typename From>
inline bool TryCast(MaybeDirectHandle<From> value, MaybeDirectHandle<To>* out) {
  if (!Is<To>(value)) return false;
  *out = UncheckedCast<To>(value);
  return true;
}

// Only initialise the SourceLocation in debug mode.
#ifdef DEBUG
#define INIT_SOURCE_LOCATION_IN_DEBUG v8::SourceLocation::Current()
#else
#define INIT_SOURCE_LOCATION_IN_DEBUG v8::SourceLocation()
#endif

#ifdef DEBUG
template <typename T>
bool GCAwareObjectTypeCheck(Tagged<Object> object, const Heap* heap);
#endif  // DEBUG

// `GCSafeCast<T>(value)` casts `object` to a tagged object of type `T` and
// should be used when the cast can be called from within a GC. The case all
// includes a debug check that `object` is either a tagged object of type `T`,
// or one of few special cases possible during GC (see GCAwareObjectTypeCheck):
// 1) `object` was already evacuated and the forwarding address refers to a
//     tagged object of type `T`.
// 2) During Scavenger, `object` is a large object.
// 3) During a conservative Scavenger, `object` is a pinned object.
template <typename T>
Tagged<T> GCSafeCast(Tagged<Object> object, const Heap* heap) {
  DCHECK(GCAwareObjectTypeCheck<T>(object, heap));
  return UncheckedCast<T>(object);
}

// `Cast<T>(value)` casts `value` to a tagged object of type `T`, with a debug
// check that `value` is a tagged object of type `T`.
template <typename To, typename From>
inline Tagged<To> Cast(Tagged<From> value, const v8::SourceLocation& loc =
                                               INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}
template <typename To, typename From>
inline IndirectHandle<To> Cast(
    IndirectHandle<From> value,
    const v8::SourceLocation& loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}
template <typename To, typename From>
inline DirectHandle<To> Cast(
    DirectHandle<From> value,
    const v8::SourceLocation& loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}
template <typename To, typename From>
inline MaybeIndirectHandle<To> Cast(
    MaybeIndirectHandle<From> value,
    const v8::SourceLocation& loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}
template <typename To, typename From>
inline MaybeDirectHandle<To> Cast(
    MaybeDirectHandle<From> value,
    const v8::SourceLocation& loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  DCHECK_WITH_MSG_AND_LOC(Is<To>(value),
                          V8_PRETTY_FUNCTION_VALUE_OR("Cast type check"), loc);
  return UncheckedCast<To>(value);
}

// TODO(leszeks): Figure out a way to make these cast to actual pointers rather
// than Tagged.
template <typename To, typename From>
inline Tagged<To> UncheckedCast(const From* value) {
  return UncheckedCast<To>(Tagged(value));
}
template <typename To, typename From>
inline Tagged<To> Cast(const From* value, const v8::SourceLocation& loc =
                                              INIT_SOURCE_LOCATION_IN_DEBUG) {
  return Cast<To>(Tagged(value), loc);
}
template <typename To, typename From>
inline Tagged<To> UncheckedCast(From value) {
  return UncheckedCast<To>(Tagged(value));
}
template <typename To, typename From>
inline Tagged<To> Cast(
    From value, const v8::SourceLocation& loc = INIT_SOURCE_LOCATION_IN_DEBUG) {
  return Cast<To>(Tagged(value), loc);
}

// `Is<T>(maybe_weak_value)` specialization for possible weak values and strong
// target `T`, that additionally first checks whether `maybe_weak_value` is
// actually a strong value (or a Smi, which can't be weak).
template <typename T, typename U>
inline bool Is(Tagged<MaybeWeak<U>> value) {
  // Cast from maybe weak to strong needs to be strong or smi.
  if constexpr (!is_maybe_weak_v<T>) {
    if (!value.IsStrongOrSmi()) return false;
    return CastTraits<T>::AllowFrom(Tagged<U>(value.ptr()));
  } else {
    // Dispatches to CastTraits<MaybeWeak<T>> below.
    return CastTraits<T>::AllowFrom(value);
  }
}
template <typename T, typename... U>
inline bool Is(Tagged<Union<U...>> value) {
  using UnionU = Union<U...>;
  if constexpr (is_subtype_v<UnionU, HeapObject>) {
    return Is<T>(Tagged<HeapObject>(value));
  } else if constexpr (is_subtype_v<UnionU, MaybeWeak<HeapObject>>) {
    return Is<T>(Tagged<MaybeWeak<HeapObject>>(value));
  } else if constexpr (is_subtype_v<UnionU, Object>) {
    return Is<T>(Tagged<Object>(value));
  } else {
    static_assert(is_subtype_v<UnionU, MaybeWeak<Object>>);
    return Is<T>(Tagged<MaybeWeak<Object>>(value));
  }
}

// Specialization for maybe weak cast targets, which first converts the incoming
// value to a strong reference and then checks if the cast to the strong T
// is allowed. Cleared weak references always return true.
template <typename T>
struct CastTraits<MaybeWeak<T>> {
  template <typename U>
  static bool AllowFrom(Tagged<U> value) {
    if constexpr (is_maybe_weak_v<U>) {
      // Cleared values are always ok.
      if (value.IsCleared()) return true;
      // TODO(leszeks): Skip Smi check for values that are known to not be Smi.
      if (value.IsSmi()) {
        return CastTraits<T>::AllowFrom(Tagged<Smi>(value.ptr()));
      }
      return CastTraits<T>::AllowFrom(MakeStrong(value));
    } else {
      return CastTraits<T>::AllowFrom(value);
    }
  }
};

template <>
struct CastTraits<Object> {
  static inline bool AllowFrom(Tagged<Object> value) { return true; }
};
template <>
struct CastTraits<Smi> {
  static inline bool AllowFrom(Tagged<Object> value) { return value.IsSmi(); }
  static inline bool AllowFrom(Tagged<HeapObject> value) { return false; }
};
template <>
struct CastTraits<HeapObject> {
  static inline bool AllowFrom(Tagged<Object> value) {
    return value.IsHeapObject();
  }
  static inline bool AllowFrom(Tagged<HeapObject> value) { return true; }
};

}  // namespace v8::internal

#undef INIT_SOURCE_LOCATION_IN_DEBUG

#endif  // V8_OBJECTS_CASTING_H_
