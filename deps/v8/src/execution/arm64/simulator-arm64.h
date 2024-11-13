// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ARM64_SIMULATOR_ARM64_H_
#define V8_EXECUTION_ARM64_SIMULATOR_ARM64_H_

// globals.h defines USE_SIMULATOR.
#include "src/common/globals.h"

#if defined(USE_SIMULATOR)

#include <stdarg.h>

#include <vector>

#include "src/base/compiler-specific.h"
#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/arm64/decoder-arm64.h"
#include "src/codegen/assembler.h"
#include "src/diagnostics/arm64/disasm-arm64.h"
#include "src/execution/simulator-base.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

// Assemble the specified IEEE-754 components into the target type and apply
// appropriate rounding.
//  sign:     0 = positive, 1 = negative
//  exponent: Unbiased IEEE-754 exponent.
//  mantissa: The mantissa of the input. The top bit (which is not encoded for
//            normal IEEE-754 values) must not be omitted. This bit has the
//            value 'pow(2, exponent)'.
//
// The input value is assumed to be a normalized value. That is, the input may
// not be infinity or NaN. If the source value is subnormal, it must be
// normalized before calling this function such that the highest set bit in the
// mantissa has the value 'pow(2, exponent)'.
//
// Callers should use FPRoundToFloat or FPRoundToDouble directly, rather than
// calling a templated FPRound.
template <class T, int ebits, int mbits>
T FPRound(int64_t sign, int64_t exponent, uint64_t mantissa,
          FPRounding round_mode) {
  static_assert((sizeof(T) * 8) >= (1 + ebits + mbits),
                "destination type T not large enough");
  static_assert(sizeof(T) <= sizeof(uint64_t),
                "maximum size of destination type T is 64 bits");
  static_assert(std::is_unsigned<T>::value,
                "destination type T must be unsigned");

  DCHECK((sign == 0) || (sign == 1));

  // Only FPTieEven and FPRoundOdd rounding modes are implemented.
  DCHECK((round_mode == FPTieEven) || (round_mode == FPRoundOdd));

  // Rounding can promote subnormals to normals, and normals to infinities. For
  // example, a double with exponent 127 (FLT_MAX_EXP) would appear to be
  // encodable as a float, but rounding based on the low-order mantissa bits
  // could make it overflow. With ties-to-even rounding, this value would become
  // an infinity.

  // ---- Rounding Method ----
  //
  // The exponent is irrelevant in the rounding operation, so we treat the
  // lowest-order bit that will fit into the result ('onebit') as having
  // the value '1'. Similarly, the highest-order bit that won't fit into
  // the result ('halfbit') has the value '0.5'. The 'point' sits between
  // 'onebit' and 'halfbit':
  //
  //            These bits fit into the result.
  //               |---------------------|
  //  mantissa = 0bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  //                                     ||
  //                                    / |
  //                                   /  halfbit
  //                               onebit
  //
  // For subnormal outputs, the range of representable bits is smaller and
  // the position of onebit and halfbit depends on the exponent of the
  // input, but the method is otherwise similar.
  //
  //   onebit(frac)
  //     |
  //     | halfbit(frac)          halfbit(adjusted)
  //     | /                      /
  //     | |                      |
  //  0b00.0 (exact)      -> 0b00.0 (exact)                    -> 0b00
  //  0b00.0...           -> 0b00.0...                         -> 0b00
  //  0b00.1 (exact)      -> 0b00.0111..111                    -> 0b00
  //  0b00.1...           -> 0b00.1...                         -> 0b01
  //  0b01.0 (exact)      -> 0b01.0 (exact)                    -> 0b01
  //  0b01.0...           -> 0b01.0...                         -> 0b01
  //  0b01.1 (exact)      -> 0b01.1 (exact)                    -> 0b10
  //  0b01.1...           -> 0b01.1...                         -> 0b10
  //  0b10.0 (exact)      -> 0b10.0 (exact)                    -> 0b10
  //  0b10.0...           -> 0b10.0...                         -> 0b10
  //  0b10.1 (exact)      -> 0b10.0111..111                    -> 0b10
  //  0b10.1...           -> 0b10.1...                         -> 0b11
  //  0b11.0 (exact)      -> 0b11.0 (exact)                    -> 0b11
  //  ...                   /             |                      /   |
  //                       /              |                     /    |
  //                                                           /     |
  // adjusted = frac - (halfbit(mantissa) & ~onebit(frac));   /      |
  //
  //                   mantissa = (mantissa >> shift) + halfbit(adjusted);

  const int mantissa_offset = 0;
  const int exponent_offset = mantissa_offset + mbits;
  const int sign_offset = exponent_offset + ebits;
  DCHECK_EQ(sign_offset, static_cast<int>(sizeof(T) * 8 - 1));

  // Bail out early for zero inputs.
  if (mantissa == 0) {
    return static_cast<T>(sign << sign_offset);
  }

  // If all bits in the exponent are set, the value is infinite or NaN.
  // This is true for all binary IEEE-754 formats.
  const int infinite_exponent = (1 << ebits) - 1;
  const int max_normal_exponent = infinite_exponent - 1;

  // Apply the exponent bias to encode it for the result. Doing this early makes
  // it easy to detect values that will be infinite or subnormal.
  exponent += max_normal_exponent >> 1;

  if (exponent > max_normal_exponent) {
    // Overflow: the input is too large for the result type to represent.
    if (round_mode == FPTieEven) {
      // FPTieEven rounding mode handles overflows using infinities.
      exponent = infinite_exponent;
      mantissa = 0;
    } else {
      DCHECK_EQ(round_mode, FPRoundOdd);
      // FPRoundOdd rounding mode handles overflows using the largest magnitude
      // normal number.
      exponent = max_normal_exponent;
      mantissa = (UINT64_C(1) << exponent_offset) - 1;
    }
    return static_cast<T>((sign << sign_offset) |
                          (exponent << exponent_offset) |
                          (mantissa << mantissa_offset));
  }

  // Calculate the shift required to move the top mantissa bit to the proper
  // place in the destination type.
  const int highest_significant_bit = 63 - CountLeadingZeros(mantissa, 64);
  int shift = highest_significant_bit - mbits;

  if (exponent <= 0) {
    // The output will be subnormal (before rounding).
    // For subnormal outputs, the shift must be adjusted by the exponent. The +1
    // is necessary because the exponent of a subnormal value (encoded as 0) is
    // the same as the exponent of the smallest normal value (encoded as 1).
    shift += -exponent + 1;

    // Handle inputs that would produce a zero output.
    //
    // Shifts higher than highest_significant_bit+1 will always produce a zero
    // result. A shift of exactly highest_significant_bit+1 might produce a
    // non-zero result after rounding.
    if (shift > (highest_significant_bit + 1)) {
      if (round_mode == FPTieEven) {
        // The result will always be +/-0.0.
        return static_cast<T>(sign << sign_offset);
      } else {
        DCHECK_EQ(round_mode, FPRoundOdd);
        DCHECK_NE(mantissa, 0U);
        // For FPRoundOdd, if the mantissa is too small to represent and
        // non-zero return the next "odd" value.
        return static_cast<T>((sign << sign_offset) | 1);
      }
    }

    // Properly encode the exponent for a subnormal output.
    exponent = 0;
  } else {
    // Clear the topmost mantissa bit, since this is not encoded in IEEE-754
    // normal values.
    mantissa &= ~(UINT64_C(1) << highest_significant_bit);
  }

  if (shift > 0) {
    if (round_mode == FPTieEven) {
      // We have to shift the mantissa to the right. Some precision is lost, so
      // we need to apply rounding.
      uint64_t onebit_mantissa = (mantissa >> (shift)) & 1;
      uint64_t halfbit_mantissa = (mantissa >> (shift - 1)) & 1;
      uint64_t adjustment = (halfbit_mantissa & ~onebit_mantissa);
      uint64_t adjusted = mantissa - adjustment;
      T halfbit_adjusted = (adjusted >> (shift - 1)) & 1;

      T result =
          static_cast<T>((sign << sign_offset) | (exponent << exponent_offset) |
                         ((mantissa >> shift) << mantissa_offset));

      // A very large mantissa can overflow during rounding. If this happens,
      // the exponent should be incremented and the mantissa set to 1.0
      // (encoded as 0). Applying halfbit_adjusted after assembling the float
      // has the nice side-effect that this case is handled for free.
      //
      // This also handles cases where a very large finite value overflows to
      // infinity, or where a very large subnormal value overflows to become
      // normal.
      return result + halfbit_adjusted;
    } else {
      DCHECK_EQ(round_mode, FPRoundOdd);
      // If any bits at position halfbit or below are set, onebit (ie. the
      // bottom bit of the resulting mantissa) must be set.
      uint64_t fractional_bits = mantissa & ((UINT64_C(1) << shift) - 1);
      if (fractional_bits != 0) {
        mantissa |= UINT64_C(1) << shift;
      }

      return static_cast<T>((sign << sign_offset) |
                            (exponent << exponent_offset) |
                            ((mantissa >> shift) << mantissa_offset));
    }
  } else {
    // We have to shift the mantissa to the left (or not at all). The input
    // mantissa is exactly representable in the output mantissa, so apply no
    // rounding correction.
    return static_cast<T>((sign << sign_offset) |
                          (exponent << exponent_offset) |
                          ((mantissa << -shift) << mantissa_offset));
  }
}

class CachePage {
  // TODO(all): Simulate instruction cache.
};

// Representation of memory, with typed getters and setters for access.
class SimMemory {
 public:
  template <typename T>
  static T AddressUntag(T address) {
    // Cast the address using a C-style cast. A reinterpret_cast would be
    // appropriate, but it can't cast one integral type to another.
    uint64_t bits = (uint64_t)address;
    return (T)(bits & ~kAddressTagMask);
  }

  template <typename T, typename A>
  static T Read(A address) {
    T value;
    address = AddressUntag(address);
    DCHECK((sizeof(value) == 1) || (sizeof(value) == 2) ||
           (sizeof(value) == 4) || (sizeof(value) == 8) ||
           (sizeof(value) == 16));
    memcpy(&value, reinterpret_cast<const char*>(address), sizeof(value));
    return value;
  }

  template <typename T, typename A>
  static void Write(A address, T value) {
    address = AddressUntag(address);
    DCHECK((sizeof(value) == 1) || (sizeof(value) == 2) ||
           (sizeof(value) == 4) || (sizeof(value) == 8) ||
           (sizeof(value) == 16));
    memcpy(reinterpret_cast<char*>(address), &value, sizeof(value));
  }
};

// The proper way to initialize a simulated system register (such as NZCV) is as
// follows:
//  SimSystemRegister nzcv = SimSystemRegister::DefaultValueFor(NZCV);
class SimSystemRegister {
 public:
  // The default constructor represents a register which has no writable bits.
  // It is not possible to set its value to anything other than 0.
  SimSystemRegister() : value_(0), write_ignore_mask_(0xffffffff) {}

  uint32_t RawValue() const { return value_; }

  void SetRawValue(uint32_t new_value) {
    value_ = (value_ & write_ignore_mask_) | (new_value & ~write_ignore_mask_);
  }

  uint32_t Bits(int msb, int lsb) const {
    return unsigned_bitextract_32(msb, lsb, value_);
  }

  int32_t SignedBits(int msb, int lsb) const {
    return signed_bitextract_32(msb, lsb, value_);
  }

  void SetBits(int msb, int lsb, uint32_t bits);

  // Default system register values.
  static SimSystemRegister DefaultValueFor(SystemRegister id);

#define DEFINE_GETTER(Name, HighBit, LowBit, Func, Type)                 \
  Type Name() const { return static_cast<Type>(Func(HighBit, LowBit)); } \
  void Set##Name(Type bits) {                                            \
    SetBits(HighBit, LowBit, static_cast<Type>(bits));                   \
  }
#define DEFINE_WRITE_IGNORE_MASK(Name, Mask) \
  static const uint32_t Name##WriteIgnoreMask = ~static_cast<uint32_t>(Mask);
  SYSTEM_REGISTER_FIELDS_LIST(DEFINE_GETTER, DEFINE_WRITE_IGNORE_MASK)
#undef DEFINE_ZERO_BITS
#undef DEFINE_GETTER

 protected:
  // Most system registers only implement a few of the bits in the word. Other
  // bits are "read-as-zero, write-ignored". The write_ignore_mask argument
  // describes the bits which are not modifiable.
  SimSystemRegister(uint32_t value, uint32_t write_ignore_mask)
      : value_(value), write_ignore_mask_(write_ignore_mask) {}

  uint32_t value_;
  uint32_t write_ignore_mask_;
};

// Represent a register (r0-r31, v0-v31).
template <int kSizeInBytes>
class SimRegisterBase {
 public:
  template <typename T>
  void Set(T new_value) {
    static_assert(sizeof(new_value) <= kSizeInBytes,
                  "Size of new_value must be <= size of template type.");
    if (sizeof(new_value) < kSizeInBytes) {
      // All AArch64 registers are zero-extending.
      memset(value_ + sizeof(new_value), 0, kSizeInBytes - sizeof(new_value));
    }
    memcpy(&value_, &new_value, sizeof(T));
    NotifyRegisterWrite();
  }

  // Insert a typed value into a register, leaving the rest of the register
  // unchanged. The lane parameter indicates where in the register the value
  // should be inserted, in the range [ 0, sizeof(value_) / sizeof(T) ), where
  // 0 represents the least significant bits.
  template <typename T>
  void Insert(int lane, T new_value) {
    DCHECK_GE(lane, 0);
    DCHECK_LE(sizeof(new_value) + (lane * sizeof(new_value)),
              static_cast<unsigned>(kSizeInBytes));
    memcpy(&value_[lane * sizeof(new_value)], &new_value, sizeof(new_value));
    NotifyRegisterWrite();
  }

  template <typename T>
  T Get(int lane = 0) const {
    T result;
    DCHECK_GE(lane, 0);
    DCHECK_LE(sizeof(result) + (lane * sizeof(result)),
              static_cast<unsigned>(kSizeInBytes));
    memcpy(&result, &value_[lane * sizeof(result)], sizeof(result));
    return result;
  }

  // TODO(all): Make this return a map of updated bytes, so that we can
  // highlight updated lanes for load-and-insert. (That never happens for scalar
  // code, but NEON has some instructions that can update individual lanes.)
  bool WrittenSinceLastLog() const { return written_since_last_log_; }

  void NotifyRegisterLogged() { written_since_last_log_ = false; }

 protected:
  uint8_t value_[kSizeInBytes];

  // Helpers to aid with register tracing.
  bool written_since_last_log_;

  void NotifyRegisterWrite() { written_since_last_log_ = true; }
};

using SimRegister = SimRegisterBase<kXRegSize>;   // r0-r31
using SimVRegister = SimRegisterBase<kQRegSize>;  // v0-v31

using sim_uint128_t = std::pair<uint64_t, uint64_t>;

// Representation of a vector register, with typed getters and setters for lanes
// and additional information to represent lane state.
class LogicVRegister {
 public:
  inline LogicVRegister(SimVRegister& other)  // NOLINT
      : register_(other) {
    for (unsigned i = 0; i < arraysize(saturated_); i++) {
      saturated_[i] = kNotSaturated;
    }
    for (unsigned i = 0; i < arraysize(round_); i++) {
      round_[i] = false;
    }
  }

  int64_t Int(VectorFormat vform, int index) const {
    int64_t element;
    switch (LaneSizeInBitsFromFormat(vform)) {
      case 8:
        element = register_.Get<int8_t>(index);
        break;
      case 16:
        element = register_.Get<int16_t>(index);
        break;
      case 32:
        element = register_.Get<int32_t>(index);
        break;
      case 64:
        element = register_.Get<int64_t>(index);
        break;
      default:
        UNREACHABLE();
        return 0;
    }
    return element;
  }

  uint64_t Uint(VectorFormat vform, int index) const {
    uint64_t element;
    switch (LaneSizeInBitsFromFormat(vform)) {
      case 8:
        element = register_.Get<uint8_t>(index);
        break;
      case 16:
        element = register_.Get<uint16_t>(index);
        break;
      case 32:
        element = register_.Get<uint32_t>(index);
        break;
      case 64:
        element = register_.Get<uint64_t>(index);
        break;
      default:
        UNREACHABLE();
        return 0;
    }
    return element;
  }

  uint64_t UintLeftJustified(VectorFormat vform, int index) const {
    return Uint(vform, index) << (64 - LaneSizeInBitsFromFormat(vform));
  }

  int64_t IntLeftJustified(VectorFormat vform, int index) const {
    uint64_t value = UintLeftJustified(vform, index);
    int64_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
  }

  void SetInt(VectorFormat vform, int index, int64_t value) const {
    switch (LaneSizeInBitsFromFormat(vform)) {
      case 8:
        register_.Insert(index, static_cast<int8_t>(value));
        break;
      case 16:
        register_.Insert(index, static_cast<int16_t>(value));
        break;
      case 32:
        register_.Insert(index, static_cast<int32_t>(value));
        break;
      case 64:
        register_.Insert(index, static_cast<int64_t>(value));
        break;
      default:
        UNREACHABLE();
        return;
    }
  }

  void SetIntArray(VectorFormat vform, const int64_t* src) const {
    ClearForWrite(vform);
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      SetInt(vform, i, src[i]);
    }
  }

  void SetUint(VectorFormat vform, int index, uint64_t value) const {
    switch (LaneSizeInBitsFromFormat(vform)) {
      case 8:
        register_.Insert(index, static_cast<uint8_t>(value));
        break;
      case 16:
        register_.Insert(index, static_cast<uint16_t>(value));
        break;
      case 32:
        register_.Insert(index, static_cast<uint32_t>(value));
        break;
      case 64:
        register_.Insert(index, static_cast<uint64_t>(value));
        break;
      default:
        UNREACHABLE();
        return;
    }
  }

  void SetUint(VectorFormat vform, int index, sim_uint128_t value) const {
    if (LaneSizeInBitsFromFormat(vform) <= 64) {
      SetUint(vform, index, value.second);
      return;
    }
    DCHECK((vform == kFormat1Q) && (index == 0));
    SetUint(kFormat2D, 0, value.second);
    SetUint(kFormat2D, 1, value.first);
  }

  void SetUintArray(VectorFormat vform, const uint64_t* src) const {
    ClearForWrite(vform);
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      SetUint(vform, i, src[i]);
    }
  }

  void ReadUintFromMem(VectorFormat vform, int index, uint64_t addr) const;

  void WriteUintToMem(VectorFormat vform, int index, uint64_t addr) const;

  template <typename T>
  T Float(int index) const {
    return register_.Get<T>(index);
  }

  template <typename T>
  void SetFloat(int index, T value) const {
    register_.Insert(index, value);
  }

  // When setting a result in a register of size less than Q, the top bits of
  // the Q register must be cleared.
  void ClearForWrite(VectorFormat vform) const {
    unsigned size = RegisterSizeInBytesFromFormat(vform);
    for (unsigned i = size; i < kQRegSize; i++) {
      SetUint(kFormat16B, i, 0);
    }
  }

  // Saturation state for each lane of a vector.
  enum Saturation {
    kNotSaturated = 0,
    kSignedSatPositive = 1 << 0,
    kSignedSatNegative = 1 << 1,
    kSignedSatMask = kSignedSatPositive | kSignedSatNegative,
    kSignedSatUndefined = kSignedSatMask,
    kUnsignedSatPositive = 1 << 2,
    kUnsignedSatNegative = 1 << 3,
    kUnsignedSatMask = kUnsignedSatPositive | kUnsignedSatNegative,
    kUnsignedSatUndefined = kUnsignedSatMask
  };

  // Getters for saturation state.
  Saturation GetSignedSaturation(int index) {
    return static_cast<Saturation>(saturated_[index] & kSignedSatMask);
  }

  Saturation GetUnsignedSaturation(int index) {
    return static_cast<Saturation>(saturated_[index] & kUnsignedSatMask);
  }

  // Setters for saturation state.
  void ClearSat(int index) { saturated_[index] = kNotSaturated; }

  void SetSignedSat(int index, bool positive) {
    SetSatFlag(index, positive ? kSignedSatPositive : kSignedSatNegative);
  }

  void SetUnsignedSat(int index, bool positive) {
    SetSatFlag(index, positive ? kUnsignedSatPositive : kUnsignedSatNegative);
  }

  void SetSatFlag(int index, Saturation sat) {
    saturated_[index] = static_cast<Saturation>(saturated_[index] | sat);
    DCHECK_NE(sat & kUnsignedSatMask, kUnsignedSatUndefined);
    DCHECK_NE(sat & kSignedSatMask, kSignedSatUndefined);
  }

  // Saturate lanes of a vector based on saturation state.
  LogicVRegister& SignedSaturate(VectorFormat vform) {
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      Saturation sat = GetSignedSaturation(i);
      if (sat == kSignedSatPositive) {
        SetInt(vform, i, MaxIntFromFormat(vform));
      } else if (sat == kSignedSatNegative) {
        SetInt(vform, i, MinIntFromFormat(vform));
      }
    }
    return *this;
  }

  LogicVRegister& UnsignedSaturate(VectorFormat vform) {
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      Saturation sat = GetUnsignedSaturation(i);
      if (sat == kUnsignedSatPositive) {
        SetUint(vform, i, MaxUintFromFormat(vform));
      } else if (sat == kUnsignedSatNegative) {
        SetUint(vform, i, 0);
      }
    }
    return *this;
  }

  // Getter for rounding state.
  bool GetRounding(int index) { return round_[index]; }

  // Setter for rounding state.
  void SetRounding(int index, bool round) { round_[index] = round; }

  // Round lanes of a vector based on rounding state.
  LogicVRegister& Round(VectorFormat vform) {
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      SetUint(vform, i, Uint(vform, i) + (GetRounding(i) ? 1 : 0));
    }
    return *this;
  }

  // Unsigned halve lanes of a vector, and use the saturation state to set the
  // top bit.
  LogicVRegister& Uhalve(VectorFormat vform) {
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      uint64_t val = Uint(vform, i);
      SetRounding(i, (val & 1) == 1);
      val >>= 1;
      if (GetUnsignedSaturation(i) != kNotSaturated) {
        // If the operation causes unsigned saturation, the bit shifted into the
        // most significant bit must be set.
        val |= (MaxUintFromFormat(vform) >> 1) + 1;
      }
      SetInt(vform, i, val);
    }
    return *this;
  }

  // Signed halve lanes of a vector, and use the carry state to set the top bit.
  LogicVRegister& Halve(VectorFormat vform) {
    for (int i = 0; i < LaneCountFromFormat(vform); i++) {
      int64_t val = Int(vform, i);
      SetRounding(i, (val & 1) == 1);
      val >>= 1;
      if (GetSignedSaturation(i) != kNotSaturated) {
        // If the operation causes signed saturation, the sign bit must be
        // inverted.
        val ^= (MaxUintFromFormat(vform) >> 1) + 1;
      }
      SetInt(vform, i, val);
    }
    return *this;
  }

  bool Is(const LogicVRegister& r) const { return &register_ == &r.register_; }

 private:
  SimVRegister& register_;

  // Allocate one saturation state entry per lane; largest register is type Q,
  // and lanes can be a minimum of one byte wide.
  Saturation saturated_[kQRegSize];

  // Allocate one rounding state entry per lane.
  bool round_[kQRegSize];
};

// Using multiple inheritance here is permitted because {DecoderVisitor} is a
// pure interface class with only pure virtual methods.
class Simulator : public DecoderVisitor, public SimulatorBase {
 public:
  static void SetRedirectInstruction(Instruction* instruction);
  static bool ICacheMatch(void* one, void* two) { return false; }
  static void FlushICache(base::CustomMatcherHashMap* i_cache, void* start,
                          size_t size) {
    USE(i_cache);
    USE(start);
    USE(size);
  }

  V8_EXPORT_PRIVATE explicit Simulator(
      Decoder<DispatchingDecoderVisitor>* decoder, Isolate* isolate = nullptr,
      FILE* stream = stderr);
  Simulator();
  V8_EXPORT_PRIVATE ~Simulator();

  // System functions.

  V8_EXPORT_PRIVATE static Simulator* current(v8::internal::Isolate* isolate);

  // A wrapper class that stores an argument for one of the above Call
  // functions.
  //
  // Only arguments up to 64 bits in size are supported.
  class CallArgument {
   public:
    template <typename T>
    explicit CallArgument(T argument) {
      bits_ = 0;
      DCHECK(sizeof(argument) <= sizeof(bits_));
      memcpy(&bits_, &argument, sizeof(argument));
      type_ = X_ARG;
    }

    explicit CallArgument(double argument) {
      DCHECK(sizeof(argument) == sizeof(bits_));
      memcpy(&bits_, &argument, sizeof(argument));
      type_ = D_ARG;
    }

    explicit CallArgument(float argument) {
      // TODO(all): CallArgument(float) is untested, remove this check once
      //            tested.
      UNIMPLEMENTED();
      // Make the D register a NaN to try to trap errors if the callee expects a
      // double. If it expects a float, the callee should ignore the top word.
      DCHECK(sizeof(kFP64SignallingNaN) == sizeof(bits_));
      memcpy(&bits_, &kFP64SignallingNaN, sizeof(kFP64SignallingNaN));
      // Write the float payload to the S register.
      DCHECK(sizeof(argument) <= sizeof(bits_));
      memcpy(&bits_, &argument, sizeof(argument));
      type_ = D_ARG;
    }

    // This indicates the end of the arguments list, so that CallArgument
    // objects can be passed into varargs functions.
    static CallArgument End() { return CallArgument(); }

    int64_t bits() const { return bits_; }
    bool IsEnd() const { return type_ == NO_ARG; }
    bool IsX() const { return type_ == X_ARG; }
    bool IsD() const { return type_ == D_ARG; }

   private:
    enum CallArgumentType { X_ARG, D_ARG, NO_ARG };

    // All arguments are aligned to at least 64 bits and we don't support
    // passing bigger arguments, so the payload size can be fixed at 64 bits.
    int64_t bits_;
    CallArgumentType type_;

    CallArgument() { type_ = NO_ARG; }
  };

  // Call an arbitrary function taking an arbitrary number of arguments.
  template <typename Return, typename... Args>
  Return Call(Address entry, Args... args) {
    // Convert all arguments to CallArgument.
    CallArgument call_args[] = {CallArgument(args)..., CallArgument::End()};
    CallImpl(entry, call_args);
    return ReadReturn<Return>();
  }

  // Start the debugging command line.
  void Debug();

  // Executes a single debug command. Takes ownership of the command (so that it
  // can store it for repeat executions), and returns true if the debugger
  // should resume execution after this command completes.
  bool ExecDebugCommand(ArrayUniquePtr<char> command);

  bool GetValue(const char* desc, int64_t* value);

  bool PrintValue(const char* desc);

  // Push an address onto the JS stack.
  uintptr_t PushAddress(uintptr_t address);

  // Pop an address from the JS stack.
  uintptr_t PopAddress();

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows (kAdditionalStackMargin).
  uintptr_t StackLimit(uintptr_t c_limit) const;
  // Return current stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCurrentStackView() const;

  V8_EXPORT_PRIVATE void ResetState();

  void DoRuntimeCall(Instruction* instr);

  // Run the simulator.
  static const Instruction* kEndOfSimAddress;
  void DecodeInstruction();
  void Run();
  V8_EXPORT_PRIVATE void RunFrom(Instruction* start);

  // Simulation helpers.
  template <typename T>
  void set_pc(T new_pc) {
    static_assert(sizeof(T) == sizeof(pc_));
    memcpy(&pc_, &new_pc, sizeof(T));
    pc_modified_ = true;
  }
  Instruction* pc() { return pc_; }

  void increment_pc() {
    if (!pc_modified_) {
      pc_ = pc_->following();
    }

    pc_modified_ = false;
  }

  virtual void Decode(Instruction* instr) { decoder_->Decode(instr); }

  // Branch Target Identification (BTI)
  //
  // Executing an instruction updates PSTATE.BTYPE, as described in the table
  // below. Execution of an instruction on a guarded page is allowed if either:
  // * PSTATE.BTYPE is 00, or
  // * it is a BTI or PACI[AB]SP instruction that accepts the current value of
  //   PSTATE.BTYPE (as described in the table below), or
  // * it is BRK or HLT instruction that causes some higher-priority exception.
  //
  //  --------------------------------------------------------------------------
  //  | Last-executed instruction    | Sets     | Accepted by                  |
  //  |                              | BTYPE to | BTI | BTI j | BTI c | BTI jc |
  //  --------------------------------------------------------------------------
  //  | - BR from an unguarded page. |          |     |       |       |        |
  //  | - BR from guarded page,      |          |     |       |       |        |
  //  |   to x16 or x17.             |    01    |     |   X   |   X   |   X    |
  //  --------------------------------------------------------------------------
  //  | BR from guarded page,        |          |     |       |       |        |
  //  | not to x16 or x17.           |    11    |     |   X   |       |   X    |
  //  --------------------------------------------------------------------------
  //  | BLR                          |    10    |     |       |   X   |   X    |
  //  --------------------------------------------------------------------------
  //  | Any other instruction        |          |     |       |       |        |
  //  |(including RET).              |    00    |  X  |   X   |   X   |   X    |
  //  --------------------------------------------------------------------------
  //
  // PACI[AB]SP is treated either like "BTI c" or "BTI jc", according to the
  // value of SCTLR_EL1.BT0. Details available in ARM DDI 0487E.a, D5-2580.

  enum BType {
    // Set when executing any instruction, except those cases listed below.
    DefaultBType = 0,

    // Set when an indirect branch is taken from an unguarded page, or from a
    // guarded page to ip0 or ip1 (x16 or x17), eg "br ip0".
    BranchFromUnguardedOrToIP = 1,

    // Set when an indirect branch and link (call) is taken, eg. "blr x0".
    BranchAndLink = 2,

    // Set when an indirect branch is taken from a guarded page to a register
    // that is not ip0 or ip1 (x16 or x17), eg, "br x0".
    BranchFromGuardedNotToIP = 3
  };

  BType btype() const { return btype_; }
  void ResetBType() { btype_ = DefaultBType; }
  void set_btype(BType btype) { btype_ = btype; }

  // Helper function to determine BType for branches.
  BType GetBTypeFromInstruction(const Instruction* instr) const;

  bool PcIsInGuardedPage() const { return guard_pages_; }
  void SetGuardedPages(bool guard_pages) { guard_pages_ = guard_pages; }

  void CheckBTypeForPAuth() {
    DCHECK(pc_->IsPAuth());
    Instr instr = pc_->Mask(SystemPAuthMask);
    // Only PACI[AB]SP allowed here, and we only support PACIBSP.
    CHECK(instr == PACIBSP);
    // Check BType allows PACI[AB]SP instructions.
    switch (btype()) {
      case BranchFromGuardedNotToIP:
        // This case depends on the value of SCTLR_EL1.BT0, which we assume
        // here to be set. This makes PACI[AB]SP behave like "BTI c",
        // disallowing its execution when BTYPE is BranchFromGuardedNotToIP
        // (0b11).
        FATAL("Executing PACIBSP with wrong BType.");
      case BranchFromUnguardedOrToIP:
      case BranchAndLink:
        break;
      case DefaultBType:
        UNREACHABLE();
    }
  }

  void CheckBTypeForBti() {
    DCHECK(pc_->IsBti());
    switch (pc_->ImmHint()) {
      case BTI_jc:
        break;
      case BTI: {
        DCHECK(btype() != DefaultBType);
        FATAL("Executing BTI with wrong BType (expected 0, got %d).", btype());
        break;
      }
      case BTI_c:
        if (btype() == BranchFromGuardedNotToIP) {
          FATAL("Executing BTI c with wrong BType (3).");
        }
        break;
      case BTI_j:
        if (btype() == BranchAndLink) {
          FATAL("Executing BTI j with wrong BType (2).");
        }
        break;
      default:
        UNIMPLEMENTED();
    }
  }

  void CheckBType() {
    // On guarded pages, if BType is not zero, take an exception on any
    // instruction other than BTI, PACI[AB]SP, HLT or BRK.
    if (PcIsInGuardedPage() && (btype() != DefaultBType)) {
      if (pc_->IsPAuth()) {
        CheckBTypeForPAuth();
      } else if (pc_->IsBti()) {
        CheckBTypeForBti();
      } else if (!pc_->IsException()) {
        FATAL("Executing non-BTI instruction with wrong BType.");
      }
    }
  }

  void ExecuteInstruction() {
    DCHECK(IsAligned(reinterpret_cast<uintptr_t>(pc_), kInstrSize));
    CheckBType();
    ResetBType();
    CheckBreakNext();
    Decode(pc_);
    increment_pc();
    LogAllWrittenRegisters();
    CheckBreakpoints();
  }

// Declare all Visitor functions.
#define DECLARE(A) void Visit##A(Instruction* instr);
  VISITOR_LIST(DECLARE)
#undef DECLARE
  void VisitNEON3SameFP(NEON3SameOp op, VectorFormat vf, SimVRegister& rd,
                        SimVRegister& rn, SimVRegister& rm);

  bool IsZeroRegister(unsigned code, Reg31Mode r31mode) const {
    return ((code == 31) && (r31mode == Reg31IsZeroRegister));
  }

  // Register accessors.
  // Return 'size' bits of the value of an integer register, as the specified
  // type. The value is zero-extended to fill the result.
  //
  template <typename T>
  T reg(unsigned code, Reg31Mode r31mode = Reg31IsZeroRegister) const {
    DCHECK_LT(code, static_cast<unsigned>(kNumberOfRegisters));
    if (IsZeroRegister(code, r31mode)) {
      return 0;
    }
    return registers_[code].Get<T>();
  }

  // Common specialized accessors for the reg() template.
  int32_t wreg(unsigned code, Reg31Mode r31mode = Reg31IsZeroRegister) const {
    return reg<int32_t>(code, r31mode);
  }

  int64_t xreg(unsigned code, Reg31Mode r31mode = Reg31IsZeroRegister) const {
    return reg<int64_t>(code, r31mode);
  }

  enum RegLogMode { LogRegWrites, NoRegLog };

  // Write 'value' into an integer register. The value is zero-extended. This
  // behaviour matches AArch64 register writes.
  template <typename T>
  void set_reg(unsigned code, T value,
               Reg31Mode r31mode = Reg31IsZeroRegister) {
    set_reg_no_log(code, value, r31mode);
    LogRegister(code, r31mode);
  }

  // Common specialized accessors for the set_reg() template.
  void set_wreg(unsigned code, int32_t value,
                Reg31Mode r31mode = Reg31IsZeroRegister) {
    set_reg(code, value, r31mode);
  }

  void set_xreg(unsigned code, int64_t value,
                Reg31Mode r31mode = Reg31IsZeroRegister) {
    set_reg(code, value, r31mode);
  }

  // As above, but don't automatically log the register update.
  template <typename T>
  void set_reg_no_log(unsigned code, T value,
                      Reg31Mode r31mode = Reg31IsZeroRegister) {
    DCHECK_LT(code, static_cast<unsigned>(kNumberOfRegisters));
    if (!IsZeroRegister(code, r31mode)) {
      registers_[code].Set(value);
    }
  }

  void set_wreg_no_log(unsigned code, int32_t value,
                       Reg31Mode r31mode = Reg31IsZeroRegister) {
    set_reg_no_log(code, value, r31mode);
  }

  void set_xreg_no_log(unsigned code, int64_t value,
                       Reg31Mode r31mode = Reg31IsZeroRegister) {
    set_reg_no_log(code, value, r31mode);
  }

  // Commonly-used special cases.
  template <typename T>
  void set_lr(T value) {
    DCHECK_EQ(sizeof(T), static_cast<unsigned>(kSystemPointerSize));
    set_reg(kLinkRegCode, value);
  }

  template <typename T>
  void set_sp(T value) {
    DCHECK_EQ(sizeof(T), static_cast<unsigned>(kSystemPointerSize));
    set_reg(31, value, Reg31IsStackPointer);
  }

  // Vector register accessors.
  // These are equivalent to the integer register accessors, but for vector
  // registers.

  // A structure for representing a 128-bit Q register.
  struct qreg_t {
    uint8_t val[kQRegSize];
  };

  // Basic accessor: read the register as the specified type.
  template <typename T>
  T vreg(unsigned code) const {
    static_assert((sizeof(T) == kBRegSize) || (sizeof(T) == kHRegSize) ||
                      (sizeof(T) == kSRegSize) || (sizeof(T) == kDRegSize) ||
                      (sizeof(T) == kQRegSize),
                  "Template type must match size of register.");
    DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));

    return vregisters_[code].Get<T>();
  }

  inline SimVRegister& vreg(unsigned code) { return vregisters_[code]; }

  int64_t sp() { return xreg(31, Reg31IsStackPointer); }
  int64_t fp() { return xreg(kFramePointerRegCode, Reg31IsStackPointer); }
  Instruction* lr() { return reg<Instruction*>(kLinkRegCode); }

  Address get_sp() const { return reg<Address>(31, Reg31IsStackPointer); }

  // Common specialized accessors for the vreg() template.
  uint8_t breg(unsigned code) const { return vreg<uint8_t>(code); }

  float hreg(unsigned code) const { return vreg<uint16_t>(code); }

  float sreg(unsigned code) const { return vreg<float>(code); }

  uint32_t sreg_bits(unsigned code) const { return vreg<uint32_t>(code); }

  double dreg(unsigned code) const { return vreg<double>(code); }

  uint64_t dreg_bits(unsigned code) const { return vreg<uint64_t>(code); }

  qreg_t qreg(unsigned code) const { return vreg<qreg_t>(code); }

  // As above, with parameterized size and return type. The value is
  // either zero-extended or truncated to fit, as required.
  template <typename T>
  T vreg(unsigned size, unsigned code) const {
    uint64_t raw = 0;
    T result;

    switch (size) {
      case kSRegSize:
        raw = vreg<uint32_t>(code);
        break;
      case kDRegSize:
        raw = vreg<uint64_t>(code);
        break;
      default:
        UNREACHABLE();
    }

    static_assert(sizeof(result) <= sizeof(raw),
                  "Template type must be <= 64 bits.");
    // Copy the result and truncate to fit. This assumes a little-endian host.
    memcpy(&result, &raw, sizeof(result));
    return result;
  }

  // Write 'value' into a floating-point register. The value is zero-extended.
  // This behaviour matches AArch64 register writes.
  template <typename T>
  void set_vreg(unsigned code, T value, RegLogMode log_mode = LogRegWrites) {
    static_assert(
        (sizeof(value) == kBRegSize) || (sizeof(value) == kHRegSize) ||
            (sizeof(value) == kSRegSize) || (sizeof(value) == kDRegSize) ||
            (sizeof(value) == kQRegSize),
        "Template type must match size of register.");
    DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));
    vregisters_[code].Set(value);

    if (log_mode == LogRegWrites) {
      LogVRegister(code, GetPrintRegisterFormat(value));
    }
  }

  // Common specialized accessors for the set_vreg() template.
  void set_breg(unsigned code, int8_t value,
                RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_hreg(unsigned code, int16_t value,
                RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_sreg(unsigned code, float value,
                RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_sreg_bits(unsigned code, uint32_t value,
                     RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_dreg(unsigned code, double value,
                RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_dreg_bits(unsigned code, uint64_t value,
                     RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  void set_qreg(unsigned code, qreg_t value,
                RegLogMode log_mode = LogRegWrites) {
    set_vreg(code, value, log_mode);
  }

  // As above, but don't automatically log the register update.
  template <typename T>
  void set_vreg_no_log(unsigned code, T value) {
    static_assert((sizeof(value) == kBRegSize) ||
                  (sizeof(value) == kHRegSize) ||
                  (sizeof(value) == kSRegSize) ||
                  (sizeof(value) == kDRegSize) || (sizeof(value) == kQRegSize));
    DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));
    vregisters_[code].Set(value);
  }

  void set_breg_no_log(unsigned code, uint8_t value) {
    set_vreg_no_log(code, value);
  }

  void set_hreg_no_log(unsigned code, uint16_t value) {
    set_vreg_no_log(code, value);
  }

  void set_sreg_no_log(unsigned code, float value) {
    set_vreg_no_log(code, value);
  }

  void set_dreg_no_log(unsigned code, double value) {
    set_vreg_no_log(code, value);
  }

  void set_qreg_no_log(unsigned code, qreg_t value) {
    set_vreg_no_log(code, value);
  }

  SimSystemRegister& nzcv() { return nzcv_; }
  SimSystemRegister& fpcr() { return fpcr_; }
  FPRounding RMode() { return static_cast<FPRounding>(fpcr_.RMode()); }
  bool DN() { return fpcr_.DN() != 0; }

  // Debug helpers

  // Simulator breakpoints.
  struct Breakpoint {
    Instruction* location;
    bool enabled;
  };
  std::vector<Breakpoint> breakpoints_;
  void SetBreakpoint(Instruction* breakpoint);
  void ListBreakpoints();
  void CheckBreakpoints();

  // Helpers for the 'next' command.
  // When this is set, the Simulator will insert a breakpoint after the next BL
  // instruction it meets.
  bool break_on_next_;
  // Check if the Simulator should insert a break after the current instruction
  // for the 'next' command.
  void CheckBreakNext();

  // Disassemble instruction at the given address.
  void PrintInstructionsAt(Instruction* pc, uint64_t count);

  // Print all registers of the specified types.
  void PrintRegisters();
  void PrintVRegisters();
  void PrintSystemRegisters();

  // As above, but only print the registers that have been updated.
  void PrintWrittenRegisters();
  void PrintWrittenVRegisters();

  // As above, but respect LOG_REG and LOG_VREG.
  void LogWrittenRegisters() {
    if (log_parameters() & LOG_REGS) PrintWrittenRegisters();
  }
  void LogWrittenVRegisters() {
    if (log_parameters() & LOG_VREGS) PrintWrittenVRegisters();
  }
  void LogAllWrittenRegisters() {
    LogWrittenRegisters();
    LogWrittenVRegisters();
  }

  // Specify relevant register formats for Print(V)Register and related helpers.
  enum PrintRegisterFormat {
    // The lane size.
    kPrintRegLaneSizeB = 0 << 0,
    kPrintRegLaneSizeH = 1 << 0,
    kPrintRegLaneSizeS = 2 << 0,
    kPrintRegLaneSizeW = kPrintRegLaneSizeS,
    kPrintRegLaneSizeD = 3 << 0,
    kPrintRegLaneSizeX = kPrintRegLaneSizeD,
    kPrintRegLaneSizeQ = 4 << 0,

    kPrintRegLaneSizeOffset = 0,
    kPrintRegLaneSizeMask = 7 << 0,

    // The lane count.
    kPrintRegAsScalar = 0,
    kPrintRegAsDVector = 1 << 3,
    kPrintRegAsQVector = 2 << 3,

    kPrintRegAsVectorMask = 3 << 3,

    // Indicate floating-point format lanes. (This flag is only supported for S-
    // and D-sized lanes.)
    kPrintRegAsFP = 1 << 5,

    // Supported combinations.

    kPrintXReg = kPrintRegLaneSizeX | kPrintRegAsScalar,
    kPrintWReg = kPrintRegLaneSizeW | kPrintRegAsScalar,
    kPrintSReg = kPrintRegLaneSizeS | kPrintRegAsScalar | kPrintRegAsFP,
    kPrintDReg = kPrintRegLaneSizeD | kPrintRegAsScalar | kPrintRegAsFP,

    kPrintReg1B = kPrintRegLaneSizeB | kPrintRegAsScalar,
    kPrintReg8B = kPrintRegLaneSizeB | kPrintRegAsDVector,
    kPrintReg16B = kPrintRegLaneSizeB | kPrintRegAsQVector,
    kPrintReg1H = kPrintRegLaneSizeH | kPrintRegAsScalar,
    kPrintReg4H = kPrintRegLaneSizeH | kPrintRegAsDVector,
    kPrintReg8H = kPrintRegLaneSizeH | kPrintRegAsQVector,
    kPrintReg1S = kPrintRegLaneSizeS | kPrintRegAsScalar,
    kPrintReg2S = kPrintRegLaneSizeS | kPrintRegAsDVector,
    kPrintReg4S = kPrintRegLaneSizeS | kPrintRegAsQVector,
    kPrintReg1SFP = kPrintRegLaneSizeS | kPrintRegAsScalar | kPrintRegAsFP,
    kPrintReg2SFP = kPrintRegLaneSizeS | kPrintRegAsDVector | kPrintRegAsFP,
    kPrintReg4SFP = kPrintRegLaneSizeS | kPrintRegAsQVector | kPrintRegAsFP,
    kPrintReg1D = kPrintRegLaneSizeD | kPrintRegAsScalar,
    kPrintReg2D = kPrintRegLaneSizeD | kPrintRegAsQVector,
    kPrintReg1DFP = kPrintRegLaneSizeD | kPrintRegAsScalar | kPrintRegAsFP,
    kPrintReg2DFP = kPrintRegLaneSizeD | kPrintRegAsQVector | kPrintRegAsFP,
    kPrintReg1Q = kPrintRegLaneSizeQ | kPrintRegAsScalar
  };

  unsigned GetPrintRegLaneSizeInBytesLog2(PrintRegisterFormat format) {
    return (format & kPrintRegLaneSizeMask) >> kPrintRegLaneSizeOffset;
  }

  unsigned GetPrintRegLaneSizeInBytes(PrintRegisterFormat format) {
    return 1 << GetPrintRegLaneSizeInBytesLog2(format);
  }

  unsigned GetPrintRegSizeInBytesLog2(PrintRegisterFormat format) {
    if (format & kPrintRegAsDVector) return kDRegSizeLog2;
    if (format & kPrintRegAsQVector) return kQRegSizeLog2;

    // Scalar types.
    return GetPrintRegLaneSizeInBytesLog2(format);
  }

  unsigned GetPrintRegSizeInBytes(PrintRegisterFormat format) {
    return 1 << GetPrintRegSizeInBytesLog2(format);
  }

  unsigned GetPrintRegLaneCount(PrintRegisterFormat format) {
    unsigned reg_size_log2 = GetPrintRegSizeInBytesLog2(format);
    unsigned lane_size_log2 = GetPrintRegLaneSizeInBytesLog2(format);
    DCHECK_GE(reg_size_log2, lane_size_log2);
    return 1 << (reg_size_log2 - lane_size_log2);
  }

  template <typename T>
  PrintRegisterFormat GetPrintRegisterFormat(T value) {
    return GetPrintRegisterFormatForSize(sizeof(value));
  }

  PrintRegisterFormat GetPrintRegisterFormat(double value) {
    static_assert(sizeof(value) == kDRegSize,
                  "D register must be size of double.");
    return GetPrintRegisterFormatForSizeFP(sizeof(value));
  }

  PrintRegisterFormat GetPrintRegisterFormat(float value) {
    static_assert(sizeof(value) == kSRegSize,
                  "S register must be size of float.");
    return GetPrintRegisterFormatForSizeFP(sizeof(value));
  }

  PrintRegisterFormat GetPrintRegisterFormat(VectorFormat vform);
  PrintRegisterFormat GetPrintRegisterFormatFP(VectorFormat vform);

  PrintRegisterFormat GetPrintRegisterFormatForSize(size_t reg_size,
                                                    size_t lane_size);

  PrintRegisterFormat GetPrintRegisterFormatForSize(size_t size) {
    return GetPrintRegisterFormatForSize(size, size);
  }

  PrintRegisterFormat GetPrintRegisterFormatForSizeFP(size_t size) {
    switch (size) {
      default:
        UNREACHABLE();
      case kDRegSize:
        return kPrintDReg;
      case kSRegSize:
        return kPrintSReg;
    }
  }

  PrintRegisterFormat GetPrintRegisterFormatTryFP(PrintRegisterFormat format) {
    if ((GetPrintRegLaneSizeInBytes(format) == kSRegSize) ||
        (GetPrintRegLaneSizeInBytes(format) == kDRegSize)) {
      return static_cast<PrintRegisterFormat>(format | kPrintRegAsFP);
    }
    return format;
  }

  // Print individual register values (after update).
  void PrintRegister(unsigned code, Reg31Mode r31mode = Reg31IsStackPointer);
  void PrintVRegister(unsigned code, PrintRegisterFormat sizes);
  void PrintSystemRegister(SystemRegister id);

  // Like Print* (above), but respect log_parameters().
  void LogRegister(unsigned code, Reg31Mode r31mode = Reg31IsStackPointer) {
    if (log_parameters() & LOG_REGS) PrintRegister(code, r31mode);
  }
  void LogVRegister(unsigned code, PrintRegisterFormat format) {
    if (log_parameters() & LOG_VREGS) PrintVRegister(code, format);
  }
  void LogSystemRegister(SystemRegister id) {
    if (log_parameters() & LOG_SYS_REGS) PrintSystemRegister(id);
  }

  // Print memory accesses.
  void PrintRead(uintptr_t address, unsigned reg_code,
                 PrintRegisterFormat format);
  void PrintWrite(uintptr_t address, unsigned reg_code,
                  PrintRegisterFormat format);
  void PrintVRead(uintptr_t address, unsigned reg_code,
                  PrintRegisterFormat format, unsigned lane);
  void PrintVWrite(uintptr_t address, unsigned reg_code,
                   PrintRegisterFormat format, unsigned lane);

  // Like Print* (above), but respect log_parameters().
  void LogRead(uintptr_t address, unsigned reg_code,
               PrintRegisterFormat format) {
    if (log_parameters() & LOG_REGS) PrintRead(address, reg_code, format);
  }
  void LogWrite(uintptr_t address, unsigned reg_code,
                PrintRegisterFormat format) {
    if (log_parameters() & LOG_WRITE) PrintWrite(address, reg_code, format);
  }
  void LogVRead(uintptr_t address, unsigned reg_code,
                PrintRegisterFormat format, unsigned lane = 0) {
    if (log_parameters() & LOG_VREGS) {
      PrintVRead(address, reg_code, format, lane);
    }
  }
  void LogVWrite(uintptr_t address, unsigned reg_code,
                 PrintRegisterFormat format, unsigned lane = 0) {
    if (log_parameters() & LOG_WRITE) {
      PrintVWrite(address, reg_code, format, lane);
    }
  }

  int log_parameters() { return log_parameters_; }
  void set_log_parameters(int new_parameters) {
    log_parameters_ = new_parameters;
    if (!decoder_) {
      if (new_parameters & LOG_DISASM) {
        PrintF("Run --debug-sim to dynamically turn on disassembler\n");
      }
      return;
    }
    if (new_parameters & LOG_DISASM) {
      decoder_->InsertVisitorBefore(print_disasm_, this);
    } else {
      decoder_->RemoveVisitor(print_disasm_);
    }
  }

  // Helper functions for register tracing.
  void PrintRegisterRawHelper(unsigned code, Reg31Mode r31mode,
                              int size_in_bytes = kXRegSize);
  void PrintVRegisterRawHelper(unsigned code, int bytes = kQRegSize,
                               int lsb = 0);
  void PrintVRegisterFPHelper(unsigned code, unsigned lane_size_in_bytes,
                              int lane_count = 1, int rightmost_lane = 0);

  static inline const char* WRegNameForCode(
      unsigned code, Reg31Mode mode = Reg31IsZeroRegister);
  static inline const char* XRegNameForCode(
      unsigned code, Reg31Mode mode = Reg31IsZeroRegister);
  static inline const char* SRegNameForCode(unsigned code);
  static inline const char* DRegNameForCode(unsigned code);
  static inline const char* VRegNameForCode(unsigned code);
  static inline int CodeFromName(const char* name);

  enum PointerType { kDataPointer, kInstructionPointer };

  struct PACKey {
    uint64_t high;
    uint64_t low;
    int number;
  };

  static V8_EXPORT_PRIVATE const PACKey kPACKeyIB;

  // Current implementation is that all pointers are tagged.
  static bool HasTBI(uint64_t ptr, PointerType type) {
    USE(ptr, type);
    return true;
  }

  // Current implementation uses 48-bit virtual addresses.
  static int GetBottomPACBit(uint64_t ptr, int ttbr) {
    USE(ptr, ttbr);
    DCHECK((ttbr == 0) || (ttbr == 1));
    return 48;
  }

  // The top PAC bit is 55 for the purposes of relative bit fields with TBI,
  // however bit 55 is the TTBR bit regardless of TBI so isn't part of the PAC
  // codes in pointers.
  static int GetTopPACBit(uint64_t ptr, PointerType type) {
    return HasTBI(ptr, type) ? 55 : 63;
  }

  // Armv8.3 Pointer authentication helpers.
  V8_EXPORT_PRIVATE static uint64_t CalculatePACMask(uint64_t ptr,
                                                     PointerType type,
                                                     int ext_bit);
  V8_EXPORT_PRIVATE static uint64_t ComputePAC(uint64_t data, uint64_t context,
                                               PACKey key);
  V8_EXPORT_PRIVATE static uint64_t AuthPAC(uint64_t ptr, uint64_t context,
                                            PACKey key, PointerType type);
  V8_EXPORT_PRIVATE static uint64_t AddPAC(uint64_t ptr, uint64_t context,
                                           PACKey key, PointerType type);
  V8_EXPORT_PRIVATE static uint64_t StripPAC(uint64_t ptr, PointerType type);

 protected:
  // Simulation helpers ------------------------------------
  bool ConditionPassed(Condition cond) {
    SimSystemRegister& flags = nzcv();
    switch (cond) {
      case eq:
        return flags.Z();
      case ne:
        return !flags.Z();
      case hs:
        return flags.C();
      case lo:
        return !flags.C();
      case mi:
        return flags.N();
      case pl:
        return !flags.N();
      case vs:
        return flags.V();
      case vc:
        return !flags.V();
      case hi:
        return flags.C() && !flags.Z();
      case ls:
        return !(flags.C() && !flags.Z());
      case ge:
        return flags.N() == flags.V();
      case lt:
        return flags.N() != flags.V();
      case gt:
        return !flags.Z() && (flags.N() == flags.V());
      case le:
        return !(!flags.Z() && (flags.N() == flags.V()));
      case nv:  // Fall through.
      case al:
        return true;
      default:
        UNREACHABLE();
    }
  }

  bool ConditionFailed(Condition cond) { return !ConditionPassed(cond); }

  template <typename T>
  void AddSubHelper(Instruction* instr, T op2);
  template <typename T>
  T AddWithCarry(bool set_flags, T left, T right, int carry_in = 0);
  template <typename T>
  void AddSubWithCarry(Instruction* instr);
  template <typename T>
  void LogicalHelper(Instruction* instr, T op2);
  template <typename T>
  void ConditionalCompareHelper(Instruction* instr, T op2);
  void LoadStoreHelper(Instruction* instr, int64_t offset, AddrMode addrmode);
  void LoadStorePairHelper(Instruction* instr, AddrMode addrmode);
  template <typename T>
  void CompareAndSwapHelper(const Instruction* instr);
  template <typename T>
  void CompareAndSwapPairHelper(const Instruction* instr);
  template <typename T>
  void AtomicMemorySimpleHelper(const Instruction* instr);
  template <typename T>
  void AtomicMemorySwapHelper(const Instruction* instr);
  uintptr_t LoadStoreAddress(unsigned addr_reg, int64_t offset,
                             AddrMode addrmode);
  void LoadStoreWriteBack(unsigned addr_reg, int64_t offset, AddrMode addrmode);
  void NEONLoadStoreMultiStructHelper(const Instruction* instr,
                                      AddrMode addr_mode);
  void NEONLoadStoreSingleStructHelper(const Instruction* instr,
                                       AddrMode addr_mode);
  void CheckMemoryAccess(uintptr_t address, uintptr_t stack);

  // "Probe" if an address range can be read. This is currently implemented
  // by doing a 1-byte read of the last accessed byte, since the assumption is
  // that if the last byte is accessible, also all lower bytes are accessible
  // (which holds true for Wasm).
  // Returns true if the access was successful, false if the access raised a
  // signal which was then handled by the trap handler (also see
  // {trap_handler::ProbeMemory}). If the access raises a signal which is not
  // handled by the trap handler (e.g. because the current PC is not registered
  // as a protected instruction), the signal will propagate and make the process
  // crash. If no trap handler is available, this always returns true.
  bool ProbeMemory(uintptr_t address, uintptr_t access_size);

  // Memory read helpers.
  template <typename T, typename A>
  T MemoryRead(A address) {
    T value;
    static_assert((sizeof(value) == 1) || (sizeof(value) == 2) ||
                  (sizeof(value) == 4) || (sizeof(value) == 8) ||
                  (sizeof(value) == 16));
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
  }

  // Memory write helpers.
  template <typename T, typename A>
  void MemoryWrite(A address, T value) {
    static_assert((sizeof(value) == 1) || (sizeof(value) == 2) ||
                  (sizeof(value) == 4) || (sizeof(value) == 8) ||
                  (sizeof(value) == 16));
    memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
  }

  template <typename T>
  T ShiftOperand(T value, Shift shift_type, unsigned amount);
  template <typename T>
  T ExtendValue(T value, Extend extend_type, unsigned left_shift = 0);
  template <typename T>
  void Extract(Instruction* instr);
  template <typename T>
  void DataProcessing2Source(Instruction* instr);
  template <typename T>
  void BitfieldHelper(Instruction* instr);
  uint16_t PolynomialMult(uint8_t op1, uint8_t op2);
  sim_uint128_t PolynomialMult128(uint64_t op1, uint64_t op2,
                                  int lane_size_in_bits) const;
  sim_uint128_t Lsl128(sim_uint128_t x, unsigned shift) const;
  sim_uint128_t Eor128(sim_uint128_t x, sim_uint128_t y) const;

  void ld1(VectorFormat vform, LogicVRegister dst, uint64_t addr);
  void ld1(VectorFormat vform, LogicVRegister dst, int index, uint64_t addr);
  void ld1r(VectorFormat vform, LogicVRegister dst, uint64_t addr);
  void ld2(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           uint64_t addr);
  void ld2(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           int index, uint64_t addr);
  void ld2r(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
            uint64_t addr);
  void ld3(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           LogicVRegister dst3, uint64_t addr);
  void ld3(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           LogicVRegister dst3, int index, uint64_t addr);
  void ld3r(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
            LogicVRegister dst3, uint64_t addr);
  void ld4(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           LogicVRegister dst3, LogicVRegister dst4, uint64_t addr);
  void ld4(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
           LogicVRegister dst3, LogicVRegister dst4, int index, uint64_t addr);
  void ld4r(VectorFormat vform, LogicVRegister dst1, LogicVRegister dst2,
            LogicVRegister dst3, LogicVRegister dst4, uint64_t addr);
  void st1(VectorFormat vform, LogicVRegister src, uint64_t addr);
  void st1(VectorFormat vform, LogicVRegister src, int index, uint64_t addr);
  void st2(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           uint64_t addr);
  void st2(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           int index, uint64_t addr);
  void st3(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           LogicVRegister src3, uint64_t addr);
  void st3(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           LogicVRegister src3, int index, uint64_t addr);
  void st4(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           LogicVRegister src3, LogicVRegister src4, uint64_t addr);
  void st4(VectorFormat vform, LogicVRegister src, LogicVRegister src2,
           LogicVRegister src3, LogicVRegister src4, int index, uint64_t addr);
  LogicVRegister cmp(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     Condition cond);
  LogicVRegister cmp(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, int imm, Condition cond);
  LogicVRegister cmptst(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister add(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister addp(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister mla(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister mls(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister mul(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister mul(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     int index);
  LogicVRegister mla(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     int index);
  LogicVRegister mls(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     int index);
  LogicVRegister pmul(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);

  using ByElementOp = LogicVRegister (Simulator::*)(VectorFormat vform,
                                                    LogicVRegister dst,
                                                    const LogicVRegister& src1,
                                                    const LogicVRegister& src2,
                                                    int index);
  LogicVRegister fmul(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2,
                      int index);
  LogicVRegister fmla(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2,
                      int index);
  LogicVRegister fmls(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2,
                      int index);
  LogicVRegister fmulx(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister smull(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister smull2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister umull(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister umull2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister smlal(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister smlal2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister umlal(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister umlal2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister smlsl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister smlsl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister umlsl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2,
                       int index);
  LogicVRegister umlsl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2,
                        int index);
  LogicVRegister sqdmull(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         int index);
  LogicVRegister sqdmull2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, int index);
  LogicVRegister sqdmlal(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         int index);
  LogicVRegister sqdmlal2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, int index);
  LogicVRegister sqdmlsl(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         int index);
  LogicVRegister sqdmlsl2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, int index);
  LogicVRegister sqdmulh(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         int index);
  LogicVRegister sqrdmulh(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, int index);
  LogicVRegister sub(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister and_(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister orr(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister orn(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister eor(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister bic(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister bic(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, uint64_t imm);
  LogicVRegister bif(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister bit(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister bsl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister cls(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister clz(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister cnt(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister not_(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister rbit(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister rev(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, int revSize);
  LogicVRegister rev16(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister rev32(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister rev64(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister addlp(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, bool is_signed,
                       bool do_accumulate);
  LogicVRegister saddlp(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister uaddlp(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister sadalp(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister uadalp(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister ext(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     int index);
  LogicVRegister ins_element(VectorFormat vform, LogicVRegister dst,
                             int dst_index, const LogicVRegister& src,
                             int src_index);
  LogicVRegister ins_immediate(VectorFormat vform, LogicVRegister dst,
                               int dst_index, uint64_t imm);
  LogicVRegister dup_element(VectorFormat vform, LogicVRegister dst,
                             const LogicVRegister& src, int src_index);
  LogicVRegister dup_immediate(VectorFormat vform, LogicVRegister dst,
                               uint64_t imm);
  LogicVRegister movi(VectorFormat vform, LogicVRegister dst, uint64_t imm);
  LogicVRegister mvni(VectorFormat vform, LogicVRegister dst, uint64_t imm);
  LogicVRegister orr(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, uint64_t imm);
  LogicVRegister sshl(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister ushl(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister SMinMax(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         bool max);
  LogicVRegister smax(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister smin(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister SMinMaxP(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, bool max);
  LogicVRegister smaxp(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister sminp(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister addp(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister addv(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister uaddlv(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister saddlv(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister SMinMaxV(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, bool max);
  LogicVRegister smaxv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister sminv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister uxtl(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister uxtl2(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister sxtl(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister sxtl2(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister Table(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& ind, bool zero_out_of_bounds,
                       const LogicVRegister* tab1,
                       const LogicVRegister* tab2 = nullptr,
                       const LogicVRegister* tab3 = nullptr,
                       const LogicVRegister* tab4 = nullptr);
  LogicVRegister tbl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& ind);
  LogicVRegister tbl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& ind);
  LogicVRegister tbl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& tab3, const LogicVRegister& ind);
  LogicVRegister tbl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& tab3, const LogicVRegister& tab4,
                     const LogicVRegister& ind);
  LogicVRegister tbx(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& ind);
  LogicVRegister tbx(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& ind);
  LogicVRegister tbx(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& tab3, const LogicVRegister& ind);
  LogicVRegister tbx(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& tab, const LogicVRegister& tab2,
                     const LogicVRegister& tab3, const LogicVRegister& tab4,
                     const LogicVRegister& ind);
  LogicVRegister uaddl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uaddl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uaddw(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uaddw2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister saddl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister saddl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister saddw(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister saddw2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister usubl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister usubl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister usubw(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister usubw2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister ssubl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister ssubl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister ssubw(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister ssubw2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister UMinMax(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         bool max);
  LogicVRegister umax(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister umin(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister UMinMaxP(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, bool max);
  LogicVRegister umaxp(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uminp(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister UMinMaxV(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, bool max);
  LogicVRegister umaxv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister uminv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister trn1(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister trn2(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister zip1(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister zip2(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uzp1(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uzp2(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister shl(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, int shift);
  LogicVRegister scvtf(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int fbits,
                       FPRounding rounding_mode);
  LogicVRegister ucvtf(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int fbits,
                       FPRounding rounding_mode);
  LogicVRegister sshll(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister sshll2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister shll(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister shll2(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister ushll(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister ushll2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister sli(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, int shift);
  LogicVRegister sri(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src, int shift);
  LogicVRegister sshr(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src, int shift);
  LogicVRegister ushr(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src, int shift);
  LogicVRegister ssra(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src, int shift);
  LogicVRegister usra(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src, int shift);
  LogicVRegister srsra(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister ursra(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister suqadd(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister usqadd(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister sqshl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister uqshl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister sqshlu(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister abs(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister neg(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister ExtractNarrow(VectorFormat vform, LogicVRegister dst,
                               bool dstIsSigned, const LogicVRegister& src,
                               bool srcIsSigned);
  LogicVRegister xtn(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src);
  LogicVRegister sqxtn(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister uqxtn(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister sqxtun(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister AbsDiff(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         bool issigned);
  LogicVRegister saba(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister uaba(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister shrn(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src, int shift);
  LogicVRegister shrn2(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister rshrn(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, int shift);
  LogicVRegister rshrn2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister uqshrn(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister uqshrn2(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src, int shift);
  LogicVRegister uqrshrn(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src, int shift);
  LogicVRegister uqrshrn2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, int shift);
  LogicVRegister sqshrn(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, int shift);
  LogicVRegister sqshrn2(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src, int shift);
  LogicVRegister sqrshrn(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src, int shift);
  LogicVRegister sqrshrn2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, int shift);
  LogicVRegister sqshrun(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src, int shift);
  LogicVRegister sqshrun2(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, int shift);
  LogicVRegister sqrshrun(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, int shift);
  LogicVRegister sqrshrun2(VectorFormat vform, LogicVRegister dst,
                           const LogicVRegister& src, int shift);
  LogicVRegister sqrdmulh(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src1,
                          const LogicVRegister& src2, bool round = true);
  LogicVRegister dot(VectorFormat vform, LogicVRegister dst,
                     const LogicVRegister& src1, const LogicVRegister& src2,
                     bool is_src1_signed, bool is_src2_signed);
  LogicVRegister sdot(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister sqdmulh(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1,
                         const LogicVRegister& src2);
#define NEON_3VREG_LOGIC_LIST(V) \
  V(addhn)                       \
  V(addhn2)                      \
  V(raddhn)                      \
  V(raddhn2)                     \
  V(subhn)                       \
  V(subhn2)                      \
  V(rsubhn)                      \
  V(rsubhn2)                     \
  V(pmull)                       \
  V(pmull2)                      \
  V(sabal)                       \
  V(sabal2)                      \
  V(uabal)                       \
  V(uabal2)                      \
  V(sabdl)                       \
  V(sabdl2)                      \
  V(uabdl)                       \
  V(uabdl2)                      \
  V(smull)                       \
  V(smull2)                      \
  V(umull)                       \
  V(umull2)                      \
  V(smlal)                       \
  V(smlal2)                      \
  V(umlal)                       \
  V(umlal2)                      \
  V(smlsl)                       \
  V(smlsl2)                      \
  V(umlsl)                       \
  V(umlsl2)                      \
  V(sqdmlal)                     \
  V(sqdmlal2)                    \
  V(sqdmlsl)                     \
  V(sqdmlsl2)                    \
  V(sqdmull)                     \
  V(sqdmull2)

#define DEFINE_LOGIC_FUNC(FXN)                               \
  LogicVRegister FXN(VectorFormat vform, LogicVRegister dst, \
                     const LogicVRegister& src1, const LogicVRegister& src2);
  NEON_3VREG_LOGIC_LIST(DEFINE_LOGIC_FUNC)
#undef DEFINE_LOGIC_FUNC

#define NEON_FP3SAME_LIST(V) \
  V(fadd, FPAdd, false)      \
  V(fsub, FPSub, true)       \
  V(fmul, FPMul, true)       \
  V(fmulx, FPMulx, true)     \
  V(fdiv, FPDiv, true)       \
  V(fmax, FPMax, false)      \
  V(fmin, FPMin, false)      \
  V(fmaxnm, FPMaxNM, false)  \
  V(fminnm, FPMinNM, false)

#define DECLARE_NEON_FP_VECTOR_OP(FN, OP, PROCNAN)                           \
  template <typename T>                                                      \
  LogicVRegister FN(VectorFormat vform, LogicVRegister dst,                  \
                    const LogicVRegister& src1, const LogicVRegister& src2); \
  LogicVRegister FN(VectorFormat vform, LogicVRegister dst,                  \
                    const LogicVRegister& src1, const LogicVRegister& src2);
  NEON_FP3SAME_LIST(DECLARE_NEON_FP_VECTOR_OP)
#undef DECLARE_NEON_FP_VECTOR_OP

#define NEON_FPPAIRWISE_LIST(V) \
  V(faddp, fadd, FPAdd)         \
  V(fmaxp, fmax, FPMax)         \
  V(fmaxnmp, fmaxnm, FPMaxNM)   \
  V(fminp, fmin, FPMin)         \
  V(fminnmp, fminnm, FPMinNM)

#define DECLARE_NEON_FP_PAIR_OP(FNP, FN, OP)                                  \
  LogicVRegister FNP(VectorFormat vform, LogicVRegister dst,                  \
                     const LogicVRegister& src1, const LogicVRegister& src2); \
  LogicVRegister FNP(VectorFormat vform, LogicVRegister dst,                  \
                     const LogicVRegister& src);
  NEON_FPPAIRWISE_LIST(DECLARE_NEON_FP_PAIR_OP)
#undef DECLARE_NEON_FP_PAIR_OP

  template <typename T>
  LogicVRegister frecps(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister frecps(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src1, const LogicVRegister& src2);
  template <typename T>
  LogicVRegister frsqrts(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1,
                         const LogicVRegister& src2);
  LogicVRegister frsqrts(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1,
                         const LogicVRegister& src2);
  template <typename T>
  LogicVRegister fmla(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister fmla(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  template <typename T>
  LogicVRegister fmls(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister fmls(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister fnmul(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src1, const LogicVRegister& src2);

  template <typename T>
  LogicVRegister fcmp(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2,
                      Condition cond);
  LogicVRegister fcmp(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2,
                      Condition cond);
  LogicVRegister fabscmp(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src1, const LogicVRegister& src2,
                         Condition cond);
  LogicVRegister fcmp_zero(VectorFormat vform, LogicVRegister dst,
                           const LogicVRegister& src, Condition cond);

  template <typename T>
  LogicVRegister fneg(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  LogicVRegister fneg(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src);
  template <typename T>
  LogicVRegister frecpx(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister frecpx(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  template <typename T>
  LogicVRegister fabs_(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fabs_(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fabd(VectorFormat vform, LogicVRegister dst,
                      const LogicVRegister& src1, const LogicVRegister& src2);
  LogicVRegister frint(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, FPRounding rounding_mode,
                       bool inexact_exception = false);
  LogicVRegister fcvts(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, FPRounding rounding_mode,
                       int fbits = 0);
  LogicVRegister fcvtu(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src, FPRounding rounding_mode,
                       int fbits = 0);
  LogicVRegister fcvtl(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fcvtl2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister fcvtn(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fcvtn2(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister fcvtxn(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);
  LogicVRegister fcvtxn2(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src);
  LogicVRegister fsqrt(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister frsqrte(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src);
  LogicVRegister frecpe(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src, FPRounding rounding);
  LogicVRegister ursqrte(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src);
  LogicVRegister urecpe(VectorFormat vform, LogicVRegister dst,
                        const LogicVRegister& src);

  using FPMinMaxOp = float (Simulator::*)(float a, float b);

  LogicVRegister FMinMaxV(VectorFormat vform, LogicVRegister dst,
                          const LogicVRegister& src, FPMinMaxOp Op);

  LogicVRegister fminv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fmaxv(VectorFormat vform, LogicVRegister dst,
                       const LogicVRegister& src);
  LogicVRegister fminnmv(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src);
  LogicVRegister fmaxnmv(VectorFormat vform, LogicVRegister dst,
                         const LogicVRegister& src);

  template <typename T>
  T FPRecipSqrtEstimate(T op);
  template <typename T>
  T FPRecipEstimate(T op, FPRounding rounding);
  template <typename T, typename R>
  R FPToFixed(T op, int fbits, bool is_signed, FPRounding rounding);

  void FPCompare(double val0, double val1);
  double FPRoundInt(double value, FPRounding round_mode);
  double FPToDouble(float value);
  float FPToFloat(double value, FPRounding round_mode);
  float FPToFloat(float16 value);
  float16 FPToFloat16(float value, FPRounding round_mode);
  float16 FPToFloat16(double value, FPRounding round_mode);
  double recip_sqrt_estimate(double a);
  double recip_estimate(double a);
  double FPRecipSqrtEstimate(double a);
  double FPRecipEstimate(double a);
  double FixedToDouble(int64_t src, int fbits, FPRounding round_mode);
  double UFixedToDouble(uint64_t src, int fbits, FPRounding round_mode);
  float FixedToFloat(int64_t src, int fbits, FPRounding round_mode);
  float UFixedToFloat(uint64_t src, int fbits, FPRounding round_mode);
  float16 FixedToFloat16(int64_t src, int fbits, FPRounding round_mode);
  float16 UFixedToFloat16(uint64_t src, int fbits, FPRounding round_mode);
  int16_t FPToInt16(double value, FPRounding rmode);
  int32_t FPToInt32(double value, FPRounding rmode);
  int64_t FPToInt64(double value, FPRounding rmode);
  uint16_t FPToUInt16(double value, FPRounding rmode);
  uint32_t FPToUInt32(double value, FPRounding rmode);
  uint64_t FPToUInt64(double value, FPRounding rmode);
  int32_t FPToFixedJS(double value);

  template <typename T>
  T FPAdd(T op1, T op2);

  template <typename T>
  T FPDiv(T op1, T op2);

  template <typename T>
  T FPMax(T a, T b);

  template <typename T>
  T FPMaxNM(T a, T b);

  template <typename T>
  T FPMin(T a, T b);

  template <typename T>
  T FPMinNM(T a, T b);

  template <typename T>
  T FPMul(T op1, T op2);

  template <typename T>
  T FPMulx(T op1, T op2);

  template <typename T>
  T FPMulAdd(T a, T op1, T op2);

  template <typename T>
  T FPSqrt(T op);

  template <typename T>
  T FPSub(T op1, T op2);

  template <typename T>
  T FPRecipStepFused(T op1, T op2);

  template <typename T>
  T FPRSqrtStepFused(T op1, T op2);

  // This doesn't do anything at the moment. We'll need it if we want support
  // for cumulative exception bits or floating-point exceptions.
  void FPProcessException() {}

  // Standard NaN processing.
  bool FPProcessNaNs(Instruction* instr);

  void CheckStackAlignment();

  inline void CheckPCSComplianceAndRun();

#ifdef DEBUG
  // Corruption values should have their least significant byte cleared to
  // allow the code of the register being corrupted to be inserted.
  static const uint64_t kCallerSavedRegisterCorruptionValue =
      0xca11edc0de000000UL;
  // This value is a NaN in both 32-bit and 64-bit FP.
  static const uint64_t kCallerSavedVRegisterCorruptionValue =
      0x7ff000007f801000UL;
  // This value is a mix of 32/64-bits NaN and "verbose" immediate.
  static const uint64_t kDefaultCPURegisterCorruptionValue =
      0x7ffbad007f8bad00UL;

  void CorruptRegisters(CPURegList* list,
                        uint64_t value = kDefaultCPURegisterCorruptionValue);
  void CorruptAllCallerSavedCPURegisters();
#endif

  // Pseudo Printf instruction
  void DoPrintf(Instruction* instr);

  // Pseudo instruction for switching stack limit
  void DoSwitchStackLimit(Instruction* instr);

  // Processor state ---------------------------------------

  // Output stream.
  FILE* stream_;
  PrintDisassembler* print_disasm_;
  void PRINTF_FORMAT(2, 3) TraceSim(const char* format, ...);

  // General purpose registers. Register 31 is the stack pointer.
  SimRegister registers_[kNumberOfRegisters];

  // Floating point registers
  SimVRegister vregisters_[kNumberOfVRegisters];

  // Processor state
  // bits[31, 27]: Condition flags N, Z, C, and V.
  //               (Negative, Zero, Carry, Overflow)
  SimSystemRegister nzcv_;

  // Floating-Point Control Register
  SimSystemRegister fpcr_;

  // Only a subset of FPCR features are supported by the simulator. This helper
  // checks that the FPCR settings are supported.
  //
  // This is checked when floating-point instructions are executed, not when
  // FPCR is set. This allows generated code to modify FPCR for external
  // functions, or to save and restore it when entering and leaving generated
  // code.
  void AssertSupportedFPCR() {
    DCHECK_EQ(fpcr().FZ(), 0);            // No flush-to-zero support.
    DCHECK(fpcr().RMode() == FPTieEven);  // Ties-to-even rounding only.

    // The simulator does not support half-precision operations so fpcr().AHP()
    // is irrelevant, and is not checked here.
  }

  template <typename T>
  static int CalcNFlag(T result) {
    return (result >> (sizeof(T) * 8 - 1)) & 1;
  }

  static int CalcZFlag(uint64_t result) { return result == 0; }

  static const uint32_t kConditionFlagsMask = 0xf0000000;

  // Stack
  uintptr_t stack_;
  static const size_t kStackProtectionSize = KB;
  // This includes a protection margin at each end of the stack area.
  static size_t AllocatedStackSize() {
    return (v8_flags.sim_stack_size * KB) + (2 * kStackProtectionSize);
  }
  static size_t UsableStackSize() { return v8_flags.sim_stack_size * KB; }
  uintptr_t stack_limit_;
  // Added in Simulator::StackLimit()
  static const int kAdditionalStackMargin = 4 * KB;

  Decoder<DispatchingDecoderVisitor>* decoder_;
  Decoder<DispatchingDecoderVisitor>* disassembler_decoder_;

  // Indicates if the pc has been modified by the instruction and should not be
  // automatically incremented.
  bool pc_modified_;
  Instruction* pc_;

  // Branch type register, used for branch target identification.
  BType btype_;

  // Global flag for enabling guarded pages.
  // TODO(arm64): implement guarding at page granularity, rather than globally.
  bool guard_pages_;

  static const char* xreg_names[];
  static const char* wreg_names[];
  static const char* sreg_names[];
  static const char* dreg_names[];
  static const char* vreg_names[];

  // Debugger input.
  void set_last_debugger_input(ArrayUniquePtr<char> input) {
    last_debugger_input_ = std::move(input);
  }
  const char* last_debugger_input() { return last_debugger_input_.get(); }
  ArrayUniquePtr<char> last_debugger_input_;

  // Synchronization primitives. See ARM DDI 0487A.a, B2.10. Pair types not
  // implemented.
  enum class MonitorAccess {
    Open,
    Exclusive,
  };

  enum class TransactionSize {
    None = 0,
    Byte = 1,
    HalfWord = 2,
    Word = 4,
    DoubleWord = 8,
  };

  TransactionSize get_transaction_size(unsigned size);

  // The least-significant bits of the address are ignored. The number of bits
  // is implementation-defined, between 3 and 11. See ARM DDI 0487A.a, B2.10.3.
  static const uintptr_t kExclusiveTaggedAddrMask = ~((1 << 11) - 1);

  class LocalMonitor {
   public:
    LocalMonitor();

    // These functions manage the state machine for the local monitor, but do
    // not actually perform loads and stores. NotifyStoreExcl only returns
    // true if the exclusive store is allowed; the global monitor will still
    // have to be checked to see whether the memory should be updated.
    void NotifyLoad();
    void NotifyLoadExcl(uintptr_t addr, TransactionSize size);
    void NotifyStore();
    bool NotifyStoreExcl(uintptr_t addr, TransactionSize size);

   private:
    void Clear();

    MonitorAccess access_state_;
    uintptr_t tagged_addr_;
    TransactionSize size_;
  };

  class GlobalMonitor {
   public:
    class Processor {
     public:
      Processor();

     private:
      friend class GlobalMonitor;
      // These functions manage the state machine for the global monitor, but do
      // not actually perform loads and stores.
      void Clear_Locked();
      void NotifyLoadExcl_Locked(uintptr_t addr);
      void NotifyStore_Locked(bool is_requesting_processor);
      bool NotifyStoreExcl_Locked(uintptr_t addr, bool is_requesting_processor);

      MonitorAccess access_state_;
      uintptr_t tagged_addr_;
      Processor* next_;
      Processor* prev_;
      // A stxr can fail due to background cache evictions. Rather than
      // simulating this, we'll just occasionally introduce cases where an
      // exclusive store fails. This will happen once after every
      // kMaxFailureCounter exclusive stores.
      static const int kMaxFailureCounter = 5;
      int failure_counter_;
    };

    // Exposed so it can be accessed by Simulator::{Read,Write}Ex*.
    base::Mutex mutex;

    void NotifyLoadExcl_Locked(uintptr_t addr, Processor* processor);
    void NotifyStore_Locked(Processor* processor);
    bool NotifyStoreExcl_Locked(uintptr_t addr, Processor* processor);

    // Called when the simulator is destroyed.
    void RemoveProcessor(Processor* processor);

    static GlobalMonitor* Get();

   private:
    // Private constructor. Call {GlobalMonitor::Get()} to get the singleton.
    GlobalMonitor() = default;
    friend class base::LeakyObject<GlobalMonitor>;

    bool IsProcessorInLinkedList_Locked(Processor* processor) const;
    void PrependProcessor_Locked(Processor* processor);

    Processor* head_ = nullptr;
  };

  LocalMonitor local_monitor_;
  GlobalMonitor::Processor global_monitor_processor_;

 private:
  void Init(FILE* stream);

  V8_EXPORT_PRIVATE void CallImpl(Address entry, CallArgument* args);

  void CallAnyCTypeFunction(Address target_address,
                            const EncodedCSignature& signature);

  // Read floating point return values.
  template <typename T>
  typename std::enable_if<std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return static_cast<T>(dreg(0));
  }
  // Read non-float return values.
  template <typename T>
  typename std::enable_if<!std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return ConvertReturn<T>(xreg(0));
  }

  template <typename T>
  static T FPDefaultNaN();

  template <typename T>
  T FPProcessNaN(T op) {
    DCHECK(std::isnan(op));
    return fpcr().DN() ? FPDefaultNaN<T>() : ToQuietNaN(op);
  }

  template <typename T>
  T FPProcessNaNs(T op1, T op2) {
    if (IsSignallingNaN(op1)) {
      return FPProcessNaN(op1);
    } else if (IsSignallingNaN(op2)) {
      return FPProcessNaN(op2);
    } else if (std::isnan(op1)) {
      DCHECK(IsQuietNaN(op1));
      return FPProcessNaN(op1);
    } else if (std::isnan(op2)) {
      DCHECK(IsQuietNaN(op2));
      return FPProcessNaN(op2);
    } else {
      return 0.0;
    }
  }

  template <typename T>
  T FPProcessNaNs3(T op1, T op2, T op3) {
    if (IsSignallingNaN(op1)) {
      return FPProcessNaN(op1);
    } else if (IsSignallingNaN(op2)) {
      return FPProcessNaN(op2);
    } else if (IsSignallingNaN(op3)) {
      return FPProcessNaN(op3);
    } else if (std::isnan(op1)) {
      DCHECK(IsQuietNaN(op1));
      return FPProcessNaN(op1);
    } else if (std::isnan(op2)) {
      DCHECK(IsQuietNaN(op2));
      return FPProcessNaN(op2);
    } else if (std::isnan(op3)) {
      DCHECK(IsQuietNaN(op3));
      return FPProcessNaN(op3);
    } else {
      return 0.0;
    }
  }

  int log_parameters_;
  // Instruction counter only valid if v8_flags.stop_sim_at isn't 0.
  int icount_for_stop_sim_at_;
  Isolate* isolate_;
};

template <>
inline double Simulator::FPDefaultNaN<double>() {
  return kFP64DefaultNaN;
}

template <>
inline float Simulator::FPDefaultNaN<float>() {
  return kFP32DefaultNaN;
}

template <>
inline float16 Simulator::FPDefaultNaN<float16>() {
  return kFP16DefaultNaN;
}

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_EXECUTION_ARM64_SIMULATOR_ARM64_H_
