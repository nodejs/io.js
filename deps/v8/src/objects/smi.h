// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SMI_H_
#define V8_OBJECTS_SMI_H_

#include <type_traits>

#include "src/common/globals.h"
#include "src/objects/tagged.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// Smi represents integer Numbers that can be stored in 31 bits.
// Smis are immediate which means they are NOT allocated in the heap.
// The ptr_ value has the following format: [31 bit signed int] 0
// For long smis it has the following format:
//     [32 bit signed int] [31 bits zero padding] 0
// Smi stands for small integer.
class Smi : public AllStatic {
 public:
  static inline constexpr Tagged<Smi> ToUint32Smi(Tagged<Smi> smi) {
    if (smi.value() <= 0) return Smi::FromInt(0);
    return Smi::FromInt(static_cast<uint32_t>(smi.value()));
  }

  // Convert a Smi object to an int.
  static inline constexpr int ToInt(const Tagged<Object> object) {
    return Tagged<Smi>(object.ptr()).value();
  }

  // Convert a value to a Smi object.
  static inline constexpr Tagged<Smi> FromInt(int value) {
    DCHECK(Smi::IsValid(value));
    return Tagged<Smi>(Internals::IntegralToSmi(value));
  }

  static inline constexpr Tagged<Smi> FromIntptr(intptr_t value) {
    DCHECK(Smi::IsValid(value));
    int smi_shift_bits = kSmiTagSize + kSmiShiftSize;
    return Tagged<Smi>((static_cast<Address>(value) << smi_shift_bits) |
                       kSmiTag);
  }

  // Given {value} in [0, 2^31-1], force it into Smi range by changing at most
  // the MSB (leaving the lower 31 bit unchanged).
  static inline constexpr Tagged<Smi> From31BitPattern(int value) {
    return Smi::FromInt((value << (32 - kSmiValueSize)) >>
                        (32 - kSmiValueSize));
  }

  template <typename E>
  static inline constexpr Tagged<Smi> FromEnum(E value)
    requires std::is_enum<E>::value
  {
    static_assert(sizeof(E) <= sizeof(int));
    return FromInt(static_cast<int>(value));
  }

  // Returns whether value can be represented in a Smi.
  template <typename T>
  static inline bool constexpr IsValid(T value)
    requires(std::is_integral_v<T> && std::is_signed_v<T>)
  {
    DCHECK_EQ(Internals::IsValidSmi(value),
              value >= kMinValue && value <= kMaxValue);
    return Internals::IsValidSmi(value);
  }
  template <typename T>
  static inline bool constexpr IsValid(T value)
    requires(std::is_integral_v<T> && std::is_unsigned_v<T>)
  {
    DCHECK_EQ(Internals::IsValidSmi(value), value <= kMaxValue);
    return Internals::IsValidSmi(value);
  }

  // Compare two Smis x, y as if they were converted to strings and then
  // compared lexicographically. Returns:
  // -1 if x < y.
  //  0 if x == y.
  //  1 if x > y.
  // Returns the result (a tagged Smi) as a raw Address for ExternalReference
  // usage.
  V8_EXPORT_PRIVATE static Address LexicographicCompare(Isolate* isolate,
                                                        Tagged<Smi> x,
                                                        Tagged<Smi> y);

  // Dispatched behavior.
  V8_EXPORT_PRIVATE static void SmiPrint(Tagged<Smi> smi, std::ostream& os);
  DECL_STATIC_VERIFIER(Smi)

  // Since this is a constexpr, "calling" it is just as efficient
  // as reading a constant.
  static inline constexpr Tagged<Smi> zero() { return Smi::FromInt(0); }
  static constexpr int kMinValue = kSmiMinValue;
  static constexpr int kMaxValue = kSmiMaxValue;

  // Smi value for filling in not-yet initialized tagged field values with a
  // valid tagged pointer. A field value equal to this doesn't necessarily
  // indicate that a field is uninitialized, but an uninitialized field should
  // definitely equal this value.
  //
  // This _has_ to be kNullAddress, so that an uninitialized field value read as
  // an embedded pointer field is interpreted as nullptr. This is so that
  // uninitialised embedded pointers are not forwarded to the embedder as part
  // of embedder tracing (and similar mechanisms), as nullptrs are skipped for
  // those cases and otherwise the embedder would try to dereference the
  // uninitialized pointer value.
  static constexpr Tagged<Smi> uninitialized_deserialization_value() {
    return Tagged<Smi>(kNullAddress);
  }
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SMI_H_
