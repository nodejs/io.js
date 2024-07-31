// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_BIGINT_H_
#define V8_OBJECTS_BIGINT_H_

#include <atomic>

#include "src/common/globals.h"
#include "src/objects/objects.h"
#include "src/objects/primitive-heap-object.h"
#include "src/utils/utils.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {

namespace bigint {
class Digits;
class FromStringAccumulator;
}  // namespace bigint

namespace internal {

void MutableBigInt_AbsoluteAddAndCanonicalize(Address result_addr,
                                              Address x_addr, Address y_addr);
int32_t MutableBigInt_AbsoluteCompare(Address x_addr, Address y_addr);
void MutableBigInt_AbsoluteSubAndCanonicalize(Address result_addr,
                                              Address x_addr, Address y_addr);
int32_t MutableBigInt_AbsoluteMulAndCanonicalize(Address result_addr,
                                                 Address x_addr,
                                                 Address y_addr);
int32_t MutableBigInt_AbsoluteDivAndCanonicalize(Address result_addr,
                                                 Address x_addr,
                                                 Address y_addr);
int32_t MutableBigInt_AbsoluteModAndCanonicalize(Address result_addr,
                                                 Address x_addr,
                                                 Address y_addr);
void MutableBigInt_BitwiseAndPosPosAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_BitwiseAndNegNegAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_BitwiseAndPosNegAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_BitwiseOrPosPosAndCanonicalize(Address result_addr,
                                                  Address x_addr,
                                                  Address y_addr);
void MutableBigInt_BitwiseOrNegNegAndCanonicalize(Address result_addr,
                                                  Address x_addr,
                                                  Address y_addr);
void MutableBigInt_BitwiseOrPosNegAndCanonicalize(Address result_addr,
                                                  Address x_addr,
                                                  Address y_addr);
void MutableBigInt_BitwiseXorPosPosAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_BitwiseXorNegNegAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_BitwiseXorPosNegAndCanonicalize(Address result_addr,
                                                   Address x_addr,
                                                   Address y_addr);
void MutableBigInt_LeftShiftAndCanonicalize(Address result_addr, Address x_addr,
                                            intptr_t shift);
uint32_t RightShiftResultLength(Address x_addr, uint32_t x_sign,
                                intptr_t shift);
void MutableBigInt_RightShiftAndCanonicalize(Address result_addr,
                                             Address x_addr, intptr_t shift,
                                             uint32_t must_round_down);

class BigInt;
class ValueDeserializer;
class ValueSerializer;

#if V8_HOST_ARCH_64_BIT && !V8_COMPRESS_POINTERS
// On non-pointer-compressed 64-bit builts, we want the digits to be 8-byte
// aligned, which requires padding.
#define BIGINT_NEEDS_PADDING 1
#endif

// BigIntBase is just the raw data object underlying a BigInt. Use with care!
// Most code should be using BigInts instead.
V8_OBJECT class BigIntBase : public PrimitiveHeapObject {
 public:
  inline int length() const {
    return LengthBits::decode(bitfield_.load(std::memory_order_relaxed));
  }

  // For use by the GC.
  inline int length(AcquireLoadTag) const {
    return LengthBits::decode(bitfield_.load(std::memory_order_acquire));
  }

  bigint::Digits digits() const;

  // The maximum kMaxLengthBits that the current implementation supports
  // would be kMaxInt - kSystemPointerSize * kBitsPerByte - 1.
  // Since we want a platform independent limit, choose a nice round number
  // somewhere below that maximum.
  static const int kMaxLengthBits = 1 << 30;  // ~1 billion.
  static const int kMaxLength =
      kMaxLengthBits / (kSystemPointerSize * kBitsPerByte);

  // Sign and length are stored in the same bitfield.  Since the GC needs to be
  // able to read the length concurrently, the getters and setters are atomic.
  static const int kLengthFieldBits = 30;
  static_assert(kMaxLength <= ((1 << kLengthFieldBits) - 1));
  using SignBits = base::BitField<bool, 0, 1>;
  using LengthBits = SignBits::Next<int, kLengthFieldBits>;
  static_assert(LengthBits::kLastUsedBit < 32);

  DECL_CAST(BigIntBase)
  DECL_VERIFIER(BigIntBase)
  DECL_PRINTER(BigIntBase)

 private:
  friend class ::v8::internal::BigInt;  // MSVC wants full namespace.
  friend class MutableBigInt;
  friend class FreshlyAllocatedBigInt;

  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;

  using digit_t = uintptr_t;

  static const int kDigitSize = sizeof(digit_t);
  // kMaxLength definition assumes this:
  static_assert(kDigitSize == kSystemPointerSize);

  static const int kDigitBits = kDigitSize * kBitsPerByte;
  static const int kHalfDigitBits = kDigitBits / 2;
  static const digit_t kHalfDigitMask = (1ull << kHalfDigitBits) - 1;

  // sign() == true means negative.
  inline bool sign() const {
    return SignBits::decode(bitfield_.load(std::memory_order_relaxed));
  }

  inline digit_t digit(int n) const {
    SLOW_DCHECK(0 <= n && n < length());
    return raw_digits()[n].value();
  }

  bool is_zero() const { return length() == 0; }

  std::atomic_uint32_t bitfield_;
#ifdef BIGINT_NEEDS_PADDING
  char padding_[4];
#endif
  FLEXIBLE_ARRAY_MEMBER(UnalignedValueMember<digit_t>, raw_digits);
} V8_OBJECT_END;

V8_OBJECT class FreshlyAllocatedBigInt : public BigIntBase {
  // This class is essentially the publicly accessible abstract version of
  // MutableBigInt (which is a hidden implementation detail). It serves as
  // the return type of Factory::NewBigInt, and makes it possible to enforce
  // casting restrictions:
  // - FreshlyAllocatedBigInt can be cast explicitly to MutableBigInt
  //   (with MutableBigInt::Cast) for initialization.
  // - MutableBigInt can be cast/converted explicitly to BigInt
  //   (with MutableBigInt::MakeImmutable); is afterwards treated as readonly.
  // - No accidental implicit casting is possible from BigInt to MutableBigInt
  //   (and no explicit operator is provided either).

 public:
  inline static Tagged<FreshlyAllocatedBigInt> cast(Tagged<Object> object);
  inline static Tagged<FreshlyAllocatedBigInt> unchecked_cast(
      Tagged<Object> o) {
    return Tagged<FreshlyAllocatedBigInt>::unchecked_cast(o);
  }

  // Clear uninitialized padding space.
  inline void clear_padding() {
#ifdef BIGINT_NEEDS_PADDING
    memset(padding_, 0, arraysize(padding_));
#endif
  }

 private:
  // Only serves to make macros happy; other code should use IsBigInt.
  static bool IsFreshlyAllocatedBigInt(Tagged<FreshlyAllocatedBigInt>) {
    return true;
  }
} V8_OBJECT_END;

// Arbitrary precision integers in JavaScript.
V8_OBJECT class BigInt : public BigIntBase {
 public:
  // Implementation of the Spec methods, see:
  // https://tc39.github.io/proposal-bigint/#sec-numeric-types
  // Sections 1.1.1 through 1.1.19.
  static Handle<BigInt> UnaryMinus(Isolate* isolate, Handle<BigInt> x);
  static MaybeHandle<BigInt> BitwiseNot(Isolate* isolate, Handle<BigInt> x);
  static MaybeHandle<BigInt> Exponentiate(Isolate* isolate, Handle<BigInt> base,
                                          Handle<BigInt> exponent);
  static MaybeHandle<BigInt> Multiply(Isolate* isolate, Handle<BigInt> x,
                                      Handle<BigInt> y);
  static MaybeHandle<BigInt> Divide(Isolate* isolate, Handle<BigInt> x,
                                    Handle<BigInt> y);
  static MaybeHandle<BigInt> Remainder(Isolate* isolate, Handle<BigInt> x,
                                       Handle<BigInt> y);
  static MaybeHandle<BigInt> Add(Isolate* isolate, Handle<BigInt> x,
                                 Handle<BigInt> y);
  static MaybeHandle<BigInt> Subtract(Isolate* isolate, Handle<BigInt> x,
                                      Handle<BigInt> y);
  static MaybeHandle<BigInt> LeftShift(Isolate* isolate, Handle<BigInt> x,
                                       Handle<BigInt> y);
  static MaybeHandle<BigInt> SignedRightShift(Isolate* isolate,
                                              Handle<BigInt> x,
                                              Handle<BigInt> y);
  static MaybeHandle<BigInt> UnsignedRightShift(Isolate* isolate,
                                                Handle<BigInt> x,
                                                Handle<BigInt> y);
  // More convenient version of "bool LessThan(x, y)".
  static ComparisonResult CompareToBigInt(Handle<BigInt> x, Handle<BigInt> y);
  static bool EqualToBigInt(Tagged<BigInt> x, Tagged<BigInt> y);
  static MaybeHandle<BigInt> BitwiseAnd(Isolate* isolate, Handle<BigInt> x,
                                        Handle<BigInt> y);
  static MaybeHandle<BigInt> BitwiseXor(Isolate* isolate, Handle<BigInt> x,
                                        Handle<BigInt> y);
  static MaybeHandle<BigInt> BitwiseOr(Isolate* isolate, Handle<BigInt> x,
                                       Handle<BigInt> y);

  // Other parts of the public interface.
  static MaybeHandle<BigInt> Increment(Isolate* isolate, Handle<BigInt> x);
  static MaybeHandle<BigInt> Decrement(Isolate* isolate, Handle<BigInt> x);

  bool ToBoolean() { return !is_zero(); }
  uint32_t Hash() {
    // TODO(jkummerow): Improve this. At least use length and sign.
    return is_zero() ? 0 : ComputeLongHash(static_cast<uint64_t>(digit(0)));
  }

  bool IsNegative() const { return sign(); }

  static Maybe<bool> EqualToString(Isolate* isolate, Handle<BigInt> x,
                                   Handle<String> y);
  static bool EqualToNumber(Handle<BigInt> x, Handle<Object> y);
  static Maybe<ComparisonResult> CompareToString(Isolate* isolate,
                                                 Handle<BigInt> x,
                                                 Handle<String> y);
  static ComparisonResult CompareToNumber(Handle<BigInt> x, Handle<Object> y);
  // Exposed for tests, do not call directly. Use CompareToNumber() instead.
  V8_EXPORT_PRIVATE static ComparisonResult CompareToDouble(Handle<BigInt> x,
                                                            double y);

  static Handle<BigInt> AsIntN(Isolate* isolate, uint64_t n, Handle<BigInt> x);
  static MaybeHandle<BigInt> AsUintN(Isolate* isolate, uint64_t n,
                                     Handle<BigInt> x);

  V8_EXPORT_PRIVATE static Handle<BigInt> FromInt64(Isolate* isolate,
                                                    int64_t n);
  V8_EXPORT_PRIVATE static Handle<BigInt> FromUint64(Isolate* isolate,
                                                     uint64_t n);
  static MaybeHandle<BigInt> FromWords64(Isolate* isolate, int sign_bit,
                                         int words64_count,
                                         const uint64_t* words);
  V8_EXPORT_PRIVATE int64_t AsInt64(bool* lossless = nullptr);
  uint64_t AsUint64(bool* lossless = nullptr);
  int Words64Count();
  void ToWordsArray64(int* sign_bit, int* words64_count, uint64_t* words);

  DECL_CAST(BigInt)
  void BigIntShortPrint(std::ostream& os);

  inline static int SizeFor(int length) {
    return sizeof(BigInt) + length * kDigitSize;
  }

  static MaybeHandle<String> ToString(Isolate* isolate, Handle<BigInt> bigint,
                                      int radix = 10,
                                      ShouldThrow should_throw = kThrowOnError);
  // Like the above, but adapted for the needs of producing error messages:
  // doesn't care about termination requests, and returns a default string
  // for inputs beyond a relatively low upper bound.
  static Handle<String> NoSideEffectsToString(Isolate* isolate,
                                              Handle<BigInt> bigint);

  // "The Number value for x", see:
  // https://tc39.github.io/ecma262/#sec-ecmascript-language-types-number-type
  // Returns a Smi or HeapNumber.
  static Handle<Object> ToNumber(Isolate* isolate, Handle<BigInt> x);

  // ECMAScript's NumberToBigInt
  V8_EXPORT_PRIVATE static MaybeHandle<BigInt> FromNumber(
      Isolate* isolate, Handle<Object> number);

  // ECMAScript's ToBigInt (throws for Number input)
  V8_EXPORT_PRIVATE static MaybeHandle<BigInt> FromObject(Isolate* isolate,
                                                          Handle<Object> obj);

  class BodyDescriptor;

 private:
  template <typename IsolateT>
  friend class StringToBigIntHelper;
  friend class ValueDeserializer;
  friend class ValueSerializer;

  // Special functions for StringToBigIntHelper:
  template <typename IsolateT>
  static Handle<BigInt> Zero(
      IsolateT* isolate, AllocationType allocation = AllocationType::kYoung);
  template <typename IsolateT>
  static MaybeHandle<BigInt> Allocate(
      IsolateT* isolate, bigint::FromStringAccumulator* accumulator,
      bool negative, AllocationType allocation);

  // Special functions for ValueSerializer/ValueDeserializer:
  uint32_t GetBitfieldForSerialization() const;
  static int DigitsByteLengthForBitfield(uint32_t bitfield);
  // Expects {storage} to have a length of at least
  // {DigitsByteLengthForBitfield(GetBitfieldForSerialization())}.
  void SerializeDigits(uint8_t* storage);
  V8_WARN_UNUSED_RESULT static MaybeHandle<BigInt> FromSerializedDigits(
      Isolate* isolate, uint32_t bitfield,
      base::Vector<const uint8_t> digits_storage);
} V8_OBJECT_END;

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_BIGINT_H_
