// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_REGISTER_H_
#define V8_WASM_BASELINE_LIFTOFF_REGISTER_H_

#include <iosfwd>
#include <memory>

#include "src/base/bits.h"
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

static constexpr bool kNeedI64RegPair = kSystemPointerSize == 4;
static constexpr bool kNeedS128RegPair = kFPAliasing == AliasingKind::kCombine;

enum RegClass : uint8_t {
  kGpReg,
  kFpReg,
  kGpRegPair = kFpReg + 1 + (kNeedS128RegPair && !kNeedI64RegPair),
  kFpRegPair = kFpReg + 1 + kNeedI64RegPair,
  kNoReg = kFpRegPair + kNeedS128RegPair,
  // +------------------+-------------------------------+
  // |                  |        kNeedI64RegPair        |
  // +------------------+---------------+---------------+
  // | kNeedS128RegPair |     true      |    false      |
  // +------------------+---------------+---------------+
  // |             true | 0,1,2,3,4 (a) | 0,1,3,2,3     |
  // |            false | 0,1,2,3,3 (b) | 0,1,2,2,2 (c) |
  // +------------------+---------------+---------------+
  // (a) arm
  // (b) ia32
  // (c) x64, arm64
};

static_assert(kNeedI64RegPair == (kGpRegPair != kNoReg),
              "kGpRegPair equals kNoReg if unused");
static_assert(kNeedS128RegPair == (kFpRegPair != kNoReg),
              "kFpRegPair equals kNoReg if unused");

enum RegPairHalf : uint8_t { kLowWord = 0, kHighWord = 1 };

static inline constexpr bool needs_gp_reg_pair(ValueKind kind) {
  return kNeedI64RegPair && kind == kI64;
}

static inline constexpr bool needs_fp_reg_pair(ValueKind kind) {
  return kNeedS128RegPair && kind == kS128;
}

static inline constexpr RegClass reg_class_for(ValueKind kind) {
  // Statically generate an array that we use for lookup at runtime.
  constexpr size_t kNumValueKinds = static_cast<size_t>(kBottom);
  constexpr auto kRegClasses =
      base::make_array<kNumValueKinds>([](std::size_t kind) {
        switch (kind) {
          case kF16:
          case kF32:
          case kF64:
            return kFpReg;
          case kI8:
          case kI16:
          case kI32:
            return kGpReg;
          case kI64:
            return kNeedI64RegPair ? kGpRegPair : kGpReg;
          case kS128:
            return kNeedS128RegPair ? kFpRegPair : kFpReg;
          case kRef:
          case kRefNull:
          case kRtt:
            return kGpReg;
          case kVoid:
            return kNoReg;  // unsupported kind
        }
        CONSTEXPR_UNREACHABLE();
      });
  V8_ASSUME(kind < kNumValueKinds);
  RegClass rc = kRegClasses[kind];
  V8_ASSUME(rc != kNoReg);
  return rc;
}

// Description of LiftoffRegister code encoding.
// This example uses the ARM architecture, which as of writing has:
// - 9 GP registers, requiring 4 bits
// - 13 FP registers, requiring 5 bits
// - kNeedI64RegPair is true
// - kNeedS128RegPair is true
// - thus, kBitsPerRegPair is 2 + 2 * 4 = 10
// - storage_t is uint16_t
// The table below illustrates how each RegClass is encoded, with brackets
// surrounding the bits which encode the register number.
//
// +----------------+------------------+
// | RegClass       | Example          |
// +----------------+------------------+
// | kGpReg (1)     | [00 0000   0000] |
// | kFpReg (2)     | [00 0000   1001] |
// | kGpRegPair (3) | 01 [0000] [0001] |
// | kFpRegPair (4) | 10  000[0  0010] |
// +----------------+------------------+
//
// gp and fp registers are encoded in the same index space, which means that
// code has to check for kGpRegPair and kFpRegPair before it can treat the code
// as a register code.
// (1) [0 .. kMaxGpRegCode] encodes gp registers
// (2) [kMaxGpRegCode + 1 .. kMaxGpRegCode + kMaxFpRegCode] encodes fp
// registers, so in this example, 1001 is really fp register 0.
// (3) The second top bit is set for kGpRegPair, and the two gp registers are
// stuffed side by side in code. Note that this is not the second top bit of
// storage_t, since storage_t is larger than the number of meaningful bits we
// need for the encoding.
// (4) The top bit is set for kFpRegPair, and the fp register is stuffed into
// the bottom part of the code. Unlike (2), this is the fp register code itself
// (not sharing index space with gp), so in this example, it is fp register 2.

// Maximum code of a gp cache register.
static constexpr int kMaxGpRegCode = kLiftoffAssemblerGpCacheRegs.last().code();
// Maximum code of an fp cache register.
static constexpr int kMaxFpRegCode = kLiftoffAssemblerFpCacheRegs.last().code();
static constexpr int kAfterMaxLiftoffGpRegCode = kMaxGpRegCode + 1;
static constexpr int kAfterMaxLiftoffFpRegCode =
    kAfterMaxLiftoffGpRegCode + kMaxFpRegCode + 1;
static constexpr int kAfterMaxLiftoffRegCode = kAfterMaxLiftoffFpRegCode;
static constexpr int kBitsPerLiftoffRegCode =
    32 - base::bits::CountLeadingZeros<uint32_t>(kAfterMaxLiftoffRegCode - 1);
static constexpr int kBitsPerGpRegCode =
    32 - base::bits::CountLeadingZeros<uint32_t>(kMaxGpRegCode);
static constexpr int kBitsPerFpRegCode =
    32 - base::bits::CountLeadingZeros<uint32_t>(kMaxFpRegCode);
// GpRegPair requires 1 extra bit, S128RegPair also needs an extra bit.
static constexpr int kBitsPerRegPair =
    (kNeedS128RegPair ? 2 : 1) + 2 * kBitsPerGpRegCode;

static_assert(2 * kBitsPerGpRegCode >= kBitsPerFpRegCode,
              "encoding for gp pair and fp pair collides");

class LiftoffRegister {
  static constexpr int needed_bits =
      std::max(kNeedI64RegPair || kNeedS128RegPair ? kBitsPerRegPair : 0,
               kBitsPerLiftoffRegCode);
  using storage_t = std::conditional<
      needed_bits <= 8, uint8_t,
      std::conditional<needed_bits <= 16, uint16_t, uint32_t>::type>::type;

  static_assert(8 * sizeof(storage_t) >= needed_bits,
                "chosen type is big enough");
  // Check for smallest required data type being chosen.
  // Special case for uint8_t as there are no smaller types.
  static_assert((8 * sizeof(storage_t) < 2 * needed_bits) ||
                    (sizeof(storage_t) == sizeof(uint8_t)),
                "chosen type is small enough");

 public:
  constexpr explicit LiftoffRegister(Register reg)
      : LiftoffRegister(reg.code()) {
    DCHECK(kLiftoffAssemblerGpCacheRegs.has(reg));
    DCHECK_EQ(reg, gp());
  }
  constexpr explicit LiftoffRegister(DoubleRegister reg)
      : LiftoffRegister(kAfterMaxLiftoffGpRegCode + reg.code()) {
    DCHECK(kLiftoffAssemblerFpCacheRegs.has(reg));
    DCHECK_EQ(reg, fp());
  }

#if defined(V8_TARGET_ARCH_IA32)
  // IA32 needs a fixed xmm0 register as a LiftoffRegister, however, xmm0 is not
  // an allocatable double register (see register-ia32.h). This constructor
  // allows bypassing the DCHECK that the LiftoffRegister has to be allocatable.
  static LiftoffRegister from_uncached(DoubleRegister reg) {
    DCHECK(!kLiftoffAssemblerFpCacheRegs.has(reg));
    return LiftoffRegister(kAfterMaxLiftoffGpRegCode + reg.code());
  }
#endif

  static LiftoffRegister from_liftoff_code(int code) {
    LiftoffRegister reg{static_cast<storage_t>(code)};
    // Check that the code is correct by round-tripping through the
    // reg-class-specific constructor.
    DCHECK(
        (reg.is_gp() && code == LiftoffRegister{reg.gp()}.liftoff_code()) ||
        (reg.is_fp() && code == LiftoffRegister{reg.fp()}.liftoff_code()) ||
        (reg.is_gp_pair() &&
         code == ForPair(reg.low_gp(), reg.high_gp()).liftoff_code()) ||
        (reg.is_fp_pair() && code == ForFpPair(reg.low_fp()).liftoff_code()));
    return reg;
  }

  static LiftoffRegister from_code(RegClass rc, int code) {
    switch (rc) {
      case kGpReg:
        return LiftoffRegister(Register::from_code(code));
      case kFpReg:
        return LiftoffRegister(DoubleRegister::from_code(code));
      default:
        UNREACHABLE();
    }
  }

  // Shifts the register code depending on the type before converting to a
  // LiftoffRegister.
  static LiftoffRegister from_external_code(RegClass rc, ValueKind kind,
                                            int code) {
    if (kFPAliasing == AliasingKind::kCombine && kind == kF32) {
      // Liftoff assumes a one-to-one mapping between float registers and
      // double registers, and so does not distinguish between f32 and f64
      // registers. The f32 register code must therefore be halved in order
      // to pass the f64 code to Liftoff.
      DCHECK_EQ(0, code % 2);
      return LiftoffRegister::from_code(rc, code >> 1);
    }
    if (kNeedS128RegPair && kind == kS128) {
      // Similarly for double registers and SIMD registers, the SIMD code
      // needs to be doubled to pass the f64 code to Liftoff.
      return LiftoffRegister::ForFpPair(DoubleRegister::from_code(code << 1));
    }
    return LiftoffRegister::from_code(rc, code);
  }

  static LiftoffRegister ForPair(Register low, Register high) {
    DCHECK(kNeedI64RegPair);
    DCHECK_NE(low, high);
    storage_t combined_code = low.code() | (high.code() << kBitsPerGpRegCode) |
                              (1 << (2 * kBitsPerGpRegCode));
    return LiftoffRegister(combined_code);
  }

  static LiftoffRegister ForFpPair(DoubleRegister low) {
    DCHECK(kNeedS128RegPair);
    DCHECK_EQ(0, low.code() % 2);
    storage_t combined_code = low.code() | 2 << (2 * kBitsPerGpRegCode);
    return LiftoffRegister(combined_code);
  }

  constexpr bool is_pair() const {
    return (kNeedI64RegPair || kNeedS128RegPair) &&
           (code_ & (3 << (2 * kBitsPerGpRegCode)));
  }

  constexpr bool is_gp_pair() const {
    return kNeedI64RegPair && (code_ & (1 << (2 * kBitsPerGpRegCode))) != 0;
  }
  constexpr bool is_fp_pair() const {
    return kNeedS128RegPair && (code_ & (2 << (2 * kBitsPerGpRegCode))) != 0;
  }
  constexpr bool is_gp() const { return code_ < kAfterMaxLiftoffGpRegCode; }
  constexpr bool is_fp() const {
    return code_ >= kAfterMaxLiftoffGpRegCode &&
           code_ < kAfterMaxLiftoffFpRegCode;
  }

  LiftoffRegister low() const {
    // Common case for most archs where only gp pair supported.
    if (!kNeedS128RegPair) return LiftoffRegister(low_gp());
    return is_gp_pair() ? LiftoffRegister(low_gp()) : LiftoffRegister(low_fp());
  }

  LiftoffRegister high() const {
    // Common case for most archs where only gp pair supported.
    if (!kNeedS128RegPair) return LiftoffRegister(high_gp());
    return is_gp_pair() ? LiftoffRegister(high_gp())
                        : LiftoffRegister(high_fp());
  }

  Register low_gp() const {
    DCHECK(is_gp_pair());
    static constexpr storage_t kCodeMask = (1 << kBitsPerGpRegCode) - 1;
    return Register::from_code(code_ & kCodeMask);
  }

  Register high_gp() const {
    DCHECK(is_gp_pair());
    static constexpr storage_t kCodeMask = (1 << kBitsPerGpRegCode) - 1;
    return Register::from_code((code_ >> kBitsPerGpRegCode) & kCodeMask);
  }

  DoubleRegister low_fp() const {
    DCHECK(is_fp_pair());
    static constexpr storage_t kCodeMask = (1 << kBitsPerFpRegCode) - 1;
    return DoubleRegister::from_code(code_ & kCodeMask);
  }

  DoubleRegister high_fp() const {
    DCHECK(is_fp_pair());
    static constexpr storage_t kCodeMask = (1 << kBitsPerFpRegCode) - 1;
    return DoubleRegister::from_code((code_ & kCodeMask) + 1);
  }

  constexpr Register gp() const {
    DCHECK(is_gp());
    return Register::from_code(code_);
  }

  constexpr DoubleRegister fp() const {
    DCHECK(is_fp());
    return DoubleRegister::from_code(code_ - kAfterMaxLiftoffGpRegCode);
  }

  constexpr int liftoff_code() const {
    static_assert(sizeof(int) >= sizeof(storage_t));
    return static_cast<int>(code_);
  }

  constexpr RegClass reg_class() const {
    return is_fp_pair() ? kFpRegPair
                        : is_gp_pair() ? kGpRegPair : is_gp() ? kGpReg : kFpReg;
  }

  bool operator==(const LiftoffRegister other) const {
    DCHECK_EQ(is_gp_pair(), other.is_gp_pair());
    DCHECK_EQ(is_fp_pair(), other.is_fp_pair());
    return code_ == other.code_;
  }
  bool operator!=(const LiftoffRegister other) const {
    DCHECK_EQ(is_gp_pair(), other.is_gp_pair());
    DCHECK_EQ(is_fp_pair(), other.is_fp_pair());
    return code_ != other.code_;
  }
  bool overlaps(const LiftoffRegister other) const {
    if (is_pair()) return low().overlaps(other) || high().overlaps(other);
    if (other.is_pair()) return *this == other.low() || *this == other.high();
    return *this == other;
  }

 private:
  explicit constexpr LiftoffRegister(storage_t code) : code_(code) {}

  storage_t code_;
};
ASSERT_TRIVIALLY_COPYABLE(LiftoffRegister);

inline std::ostream& operator<<(std::ostream& os, LiftoffRegister reg) {
  if (reg.is_gp_pair()) {
    return os << "<" << reg.low_gp() << "+" << reg.high_gp() << ">";
  } else if (reg.is_fp_pair()) {
    return os << "<" << reg.low_fp() << "+" << reg.high_fp() << ">";
  } else if (reg.is_gp()) {
    return os << reg.gp();
  } else {
    return os << reg.fp();
  }
}

class LiftoffRegList {
 public:
  class Iterator;

  static constexpr bool use_u16 = kAfterMaxLiftoffRegCode <= 16;
  static constexpr bool use_u32 = !use_u16 && kAfterMaxLiftoffRegCode <= 32;
  using storage_t = std::conditional<
      use_u16, uint16_t,
      std::conditional<use_u32, uint32_t, uint64_t>::type>::type;

  static constexpr storage_t kGpMask =
      storage_t{kLiftoffAssemblerGpCacheRegs.bits()};
  static constexpr storage_t kFpMask =
      storage_t{kLiftoffAssemblerFpCacheRegs.bits()}
      << kAfterMaxLiftoffGpRegCode;
  // Sets all even numbered fp registers.
  static constexpr uint64_t kEvenFpSetMask = uint64_t{0x5555555555555555}
                                             << kAfterMaxLiftoffGpRegCode;
  static constexpr uint64_t kOddFpSetMask = uint64_t{0xAAAAAAAAAAAAAAAA}
                                            << kAfterMaxLiftoffGpRegCode;

  constexpr LiftoffRegList() = default;

  // Allow to construct LiftoffRegList from a number of
  // {Register|DoubleRegister|LiftoffRegister}.
  template <
      typename... Regs,
      typename = std::enable_if_t<std::conjunction_v<std::disjunction<
          std::is_same<Register, Regs>, std::is_same<DoubleRegister, Regs>,
          std::is_same<LiftoffRegister, Regs>>...>>>
  constexpr explicit LiftoffRegList(Regs... regs) {
    (..., set(regs));
  }

  constexpr Register set(Register reg) {
    return set(LiftoffRegister(reg)).gp();
  }
  constexpr DoubleRegister set(DoubleRegister reg) {
    return set(LiftoffRegister(reg)).fp();
  }

  constexpr LiftoffRegister set(LiftoffRegister reg) {
    if (reg.is_pair()) {
      regs_ |= storage_t{1} << reg.low().liftoff_code();
      regs_ |= storage_t{1} << reg.high().liftoff_code();
    } else {
      regs_ |= storage_t{1} << reg.liftoff_code();
    }
    return reg;
  }

  constexpr LiftoffRegister clear(LiftoffRegister reg) {
    if (reg.is_pair()) {
      regs_ &= ~(storage_t{1} << reg.low().liftoff_code());
      regs_ &= ~(storage_t{1} << reg.high().liftoff_code());
    } else {
      regs_ &= ~(storage_t{1} << reg.liftoff_code());
    }
    return reg;
  }
  constexpr Register clear(Register reg) {
    return clear(LiftoffRegister{reg}).gp();
  }
  constexpr DoubleRegister clear(DoubleRegister reg) {
    return clear(LiftoffRegister{reg}).fp();
  }

  bool has(LiftoffRegister reg) const {
    if (reg.is_pair()) {
      DCHECK_EQ(has(reg.low()), has(reg.high()));
      reg = reg.low();
    }
    return (regs_ & (storage_t{1} << reg.liftoff_code())) != 0;
  }
  bool has(Register reg) const { return has(LiftoffRegister{reg}); }
  bool has(DoubleRegister reg) const { return has(LiftoffRegister{reg}); }

  constexpr bool is_empty() const { return regs_ == 0; }

  constexpr unsigned GetNumRegsSet() const {
    return base::bits::CountPopulation(regs_);
  }

  constexpr LiftoffRegList operator&(const LiftoffRegList other) const {
    return LiftoffRegList(regs_ & other.regs_);
  }

  constexpr LiftoffRegList& operator&=(const LiftoffRegList other) {
    regs_ &= other.regs_;
    return *this;
  }

  constexpr LiftoffRegList operator|(const LiftoffRegList other) const {
    return LiftoffRegList(regs_ | other.regs_);
  }

  constexpr LiftoffRegList& operator|=(const LiftoffRegList other) {
    regs_ |= other.regs_;
    return *this;
  }

  constexpr LiftoffRegList GetAdjacentFpRegsSet() const {
    // And regs_ with a right shifted version of itself, so reg[i] is set only
    // if reg[i+1] is set. We only care about the even fp registers.
    storage_t available = (regs_ >> 1) & regs_ & kEvenFpSetMask;
    return LiftoffRegList(available);
  }

  constexpr bool HasAdjacentFpRegsSet() const {
    return !GetAdjacentFpRegsSet().is_empty();
  }

  // Returns a list where if any part of an adjacent pair of FP regs was set,
  // both are set in the result. For example, [1, 4] is turned into [0, 1, 4, 5]
  // because (0, 1) and (4, 5) are adjacent pairs.
  constexpr LiftoffRegList SpreadSetBitsToAdjacentFpRegs() const {
    storage_t odd_regs = regs_ & kOddFpSetMask;
    storage_t even_regs = regs_ & kEvenFpSetMask;
    return FromBits(regs_ | ((odd_regs >> 1) & kFpMask) |
                    ((even_regs << 1) & kFpMask));
  }

  constexpr bool operator==(const LiftoffRegList other) const {
    return regs_ == other.regs_;
  }
  constexpr bool operator!=(const LiftoffRegList other) const {
    return regs_ != other.regs_;
  }

  LiftoffRegister GetFirstRegSet() const {
    V8_ASSUME(regs_ != 0);
    int first_code = base::bits::CountTrailingZeros(regs_);
    return LiftoffRegister::from_liftoff_code(first_code);
  }

  LiftoffRegister GetLastRegSet() const {
    V8_ASSUME(regs_ != 0);
    int last_code =
        8 * sizeof(regs_) - 1 - base::bits::CountLeadingZeros(regs_);
    return LiftoffRegister::from_liftoff_code(last_code);
  }

  LiftoffRegList MaskOut(const LiftoffRegList mask) const {
    // Masking out is guaranteed to return a correct reg list, hence no checks
    // needed.
    return FromBits(regs_ & ~mask.regs_);
  }

  RegList GetGpList() { return RegList::FromBits(regs_ & kGpMask); }
  DoubleRegList GetFpList() {
    return DoubleRegList::FromBits((regs_ & kFpMask) >>
                                   kAfterMaxLiftoffGpRegCode);
  }

  inline Iterator begin() const;
  inline Iterator end() const;

  static constexpr LiftoffRegList FromBits(storage_t bits) {
    DCHECK_EQ(bits, bits & (kGpMask | kFpMask));
    return LiftoffRegList(bits);
  }

  template <storage_t bits>
  static constexpr LiftoffRegList FromBits() {
    static_assert(bits == (bits & (kGpMask | kFpMask)), "illegal reg list");
    return LiftoffRegList{bits};
  }

#if DEBUG
  void Print() const;
#endif

 private:
  // Unchecked constructor. Only use for valid bits.
  explicit constexpr LiftoffRegList(storage_t bits) : regs_(bits) {}

  storage_t regs_ = 0;
};
ASSERT_TRIVIALLY_COPYABLE(LiftoffRegList);

static constexpr LiftoffRegList kGpCacheRegList =
    LiftoffRegList::FromBits<LiftoffRegList::kGpMask>();
static constexpr LiftoffRegList kFpCacheRegList =
    LiftoffRegList::FromBits<LiftoffRegList::kFpMask>();

class LiftoffRegList::Iterator {
 public:
  LiftoffRegister operator*() { return remaining_.GetFirstRegSet(); }
  Iterator& operator++() {
    remaining_.clear(remaining_.GetFirstRegSet());
    return *this;
  }
  bool operator==(Iterator other) { return remaining_ == other.remaining_; }
  bool operator!=(Iterator other) { return remaining_ != other.remaining_; }

 private:
  explicit Iterator(LiftoffRegList remaining) : remaining_(remaining) {}
  friend class LiftoffRegList;

  LiftoffRegList remaining_;
};

LiftoffRegList::Iterator LiftoffRegList::begin() const {
  return Iterator{*this};
}
LiftoffRegList::Iterator LiftoffRegList::end() const {
  return Iterator{LiftoffRegList{}};
}

static constexpr LiftoffRegList GetCacheRegList(RegClass rc) {
  V8_ASSUME(rc == kFpReg || rc == kGpReg);
  static_assert(kGpReg == 0 && kFpReg == 1);
  constexpr LiftoffRegList kRegLists[2]{kGpCacheRegList, kFpCacheRegList};
  return kRegLists[rc];
}

inline std::ostream& operator<<(std::ostream& os, LiftoffRegList reglist) {
  os << "{";
  for (bool first = true; !reglist.is_empty(); first = false) {
    LiftoffRegister reg = reglist.GetFirstRegSet();
    reglist.clear(reg);
    os << (first ? "" : ", ") << reg;
  }
  return os << "}";
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_REGISTER_H_
