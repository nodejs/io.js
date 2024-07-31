// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_REGISTER_ARM64_H_
#define V8_CODEGEN_ARM64_REGISTER_ARM64_H_

#include "src/codegen/arm64/utils-arm64.h"
#include "src/codegen/register-base.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Registers.
// clang-format off
#define GENERAL_REGISTER_CODE_LIST(R)                     \
  R(0)  R(1)  R(2)  R(3)  R(4)  R(5)  R(6)  R(7)          \
  R(8)  R(9)  R(10) R(11) R(12) R(13) R(14) R(15)         \
  R(16) R(17) R(18) R(19) R(20) R(21) R(22) R(23)         \
  R(24) R(25) R(26) R(27) R(28) R(29) R(30) R(31)

#define GENERAL_REGISTERS(R)                              \
  R(x0)  R(x1)  R(x2)  R(x3)  R(x4)  R(x5)  R(x6)  R(x7)  \
  R(x8)  R(x9)  R(x10) R(x11) R(x12) R(x13) R(x14) R(x15) \
  R(x16) R(x17) R(x18) R(x19) R(x20) R(x21) R(x22) R(x23) \
  R(x24) R(x25) R(x26) R(x27) R(x28) R(x29) R(x30) R(x31)

// x18 is the platform register and is reserved for the use of platform ABIs.
// It is known to be reserved by the OS at least on Windows and iOS.
#define ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(R)                  \
  R(x0)  R(x1)  R(x2)  R(x3)  R(x4)  R(x5)  R(x6)  R(x7)  \
  R(x8)  R(x9)  R(x10) R(x11) R(x12) R(x13) R(x14) R(x15) \
         R(x19) R(x20) R(x21) R(x22) R(x23) R(x24) R(x25) \
  R(x27)

#ifdef V8_COMPRESS_POINTERS
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(R)
#else
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(R) R(x28)
#endif

#define ALLOCATABLE_GENERAL_REGISTERS(V)  \
  ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(V) \
  MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V)

#define FLOAT_REGISTERS(V)                                \
  V(s0)  V(s1)  V(s2)  V(s3)  V(s4)  V(s5)  V(s6)  V(s7)  \
  V(s8)  V(s9)  V(s10) V(s11) V(s12) V(s13) V(s14) V(s15) \
  V(s16) V(s17) V(s18) V(s19) V(s20) V(s21) V(s22) V(s23) \
  V(s24) V(s25) V(s26) V(s27) V(s28) V(s29) V(s30) V(s31)

#define DOUBLE_REGISTERS(R)                               \
  R(d0)  R(d1)  R(d2)  R(d3)  R(d4)  R(d5)  R(d6)  R(d7)  \
  R(d8)  R(d9)  R(d10) R(d11) R(d12) R(d13) R(d14) R(d15) \
  R(d16) R(d17) R(d18) R(d19) R(d20) R(d21) R(d22) R(d23) \
  R(d24) R(d25) R(d26) R(d27) R(d28) R(d29) R(d30) R(d31)

#define SIMD128_REGISTERS(V)                              \
  V(q0)  V(q1)  V(q2)  V(q3)  V(q4)  V(q5)  V(q6)  V(q7)  \
  V(q8)  V(q9)  V(q10) V(q11) V(q12) V(q13) V(q14) V(q15) \
  V(q16) V(q17) V(q18) V(q19) V(q20) V(q21) V(q22) V(q23) \
  V(q24) V(q25) V(q26) V(q27) V(q28) V(q29) V(q30) V(q31)

#define VECTOR_REGISTERS(V)                               \
  V(v0)  V(v1)  V(v2)  V(v3)  V(v4)  V(v5)  V(v6)  V(v7)  \
  V(v8)  V(v9)  V(v10) V(v11) V(v12) V(v13) V(v14) V(v15) \
  V(v16) V(v17) V(v18) V(v19) V(v20) V(v21) V(v22) V(v23) \
  V(v24) V(v25) V(v26) V(v27) V(v28) V(v29) V(v30) V(v31)

// Register d29 could be allocated, but we keep an even length list here, in
// order to make stack alignment easier for save and restore.
#define ALLOCATABLE_DOUBLE_REGISTERS(R)                   \
  R(d0)  R(d1)  R(d2)  R(d3)  R(d4)  R(d5)  R(d6)  R(d7)  \
  R(d8)  R(d9)  R(d10) R(d11) R(d12) R(d13) R(d14) R(d16) \
  R(d17) R(d18) R(d19) R(d20) R(d21) R(d22) R(d23) R(d24) \
  R(d25) R(d26) R(d27) R(d28)

#define MAGLEV_SCRATCH_DOUBLE_REGISTERS(R)                \
  R(d30) R(d31)

// clang-format on

// Some CPURegister methods can return Register and VRegister types, so we
// need to declare them in advance.
class Register;
class VRegister;

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class CPURegister : public RegisterBase<CPURegister, kRegAfterLast> {
 public:
  enum RegisterType : int8_t { kRegister, kVRegister, kNoRegister };

  static constexpr CPURegister no_reg() {
    return CPURegister{kCode_no_reg, 0, kNoRegister};
  }

  static constexpr CPURegister Create(int code, int size, RegisterType type) {
    DCHECK(IsValid(code, size, type));
    return CPURegister{code, size, type};
  }

  RegisterType type() const { return reg_type_; }
  int SizeInBits() const {
    DCHECK(is_valid());
    return reg_size_;
  }
  int SizeInBytes() const {
    DCHECK(is_valid());
    DCHECK_EQ(SizeInBits() % 8, 0);
    return reg_size_ / 8;
  }
  bool Is8Bits() const {
    DCHECK(is_valid());
    return reg_size_ == 8;
  }
  bool Is16Bits() const {
    DCHECK(is_valid());
    return reg_size_ == 16;
  }
  bool Is32Bits() const {
    DCHECK(is_valid());
    return reg_size_ == 32;
  }
  bool Is64Bits() const {
    DCHECK(is_valid());
    return reg_size_ == 64;
  }
  bool Is128Bits() const {
    DCHECK(is_valid());
    return reg_size_ == 128;
  }
  bool IsNone() const { return reg_type_ == kNoRegister; }
  constexpr bool Aliases(const CPURegister& other) const {
    return RegisterBase::operator==(other) && reg_type_ == other.reg_type_;
  }

  constexpr bool operator==(const CPURegister& other) const {
    return RegisterBase::operator==(other) && reg_size_ == other.reg_size_ &&
           reg_type_ == other.reg_type_;
  }
  constexpr bool operator!=(const CPURegister& other) const {
    return !operator==(other);
  }

  bool IsZero() const;
  bool IsSP() const;

  bool IsRegister() const { return reg_type_ == kRegister; }
  bool IsVRegister() const { return reg_type_ == kVRegister; }

  bool IsFPRegister() const { return IsS() || IsD(); }

  bool IsW() const { return IsRegister() && Is32Bits(); }
  bool IsX() const { return IsRegister() && Is64Bits(); }

  // These assertions ensure that the size and type of the register are as
  // described. They do not consider the number of lanes that make up a vector.
  // So, for example, Is8B() implies IsD(), and Is1D() implies IsD, but IsD()
  // does not imply Is1D() or Is8B().
  // Check the number of lanes, ie. the format of the vector, using methods such
  // as Is8B(), Is1D(), etc. in the VRegister class.
  bool IsV() const { return IsVRegister(); }
  bool IsB() const { return IsV() && Is8Bits(); }
  bool IsH() const { return IsV() && Is16Bits(); }
  bool IsS() const { return IsV() && Is32Bits(); }
  bool IsD() const { return IsV() && Is64Bits(); }
  bool IsQ() const { return IsV() && Is128Bits(); }

  Register Reg() const;
  VRegister VReg() const;

  Register X() const;
  Register W() const;
  VRegister V() const;
  VRegister B() const;
  VRegister H() const;
  VRegister D() const;
  VRegister S() const;
  VRegister Q() const;

  bool IsSameSizeAndType(const CPURegister& other) const;

  constexpr bool IsEven() const { return (code() % 2) == 0; }

  int MaxCode() const {
    if (IsVRegister()) {
      return kNumberOfVRegisters - 1;
    }
    DCHECK(IsRegister());
    return kNumberOfRegisters - 1;
  }

 protected:
  uint8_t reg_size_;
  RegisterType reg_type_;

#if defined(V8_OS_WIN) && !defined(__clang__)
  // MSVC has problem to parse template base class as friend class.
  friend RegisterBase;
#else
  friend class RegisterBase;
#endif

  constexpr CPURegister(int code, int size, RegisterType type)
      : RegisterBase(code), reg_size_(size), reg_type_(type) {}

  static constexpr bool IsValidRegister(int code, int size) {
    return (size == kWRegSizeInBits || size == kXRegSizeInBits) &&
           (code < kNumberOfRegisters || code == kSPRegInternalCode);
  }

  static constexpr bool IsValidVRegister(int code, int size) {
    return (size == kBRegSizeInBits || size == kHRegSizeInBits ||
            size == kSRegSizeInBits || size == kDRegSizeInBits ||
            size == kQRegSizeInBits) &&
           code < kNumberOfVRegisters;
  }

  static constexpr bool IsValid(int code, int size, RegisterType type) {
    return (type == kRegister && IsValidRegister(code, size)) ||
           (type == kVRegister && IsValidVRegister(code, size));
  }

  static constexpr bool IsNone(int code, int size, RegisterType type) {
    return type == kNoRegister && code == 0 && size == 0;
  }
};

ASSERT_TRIVIALLY_COPYABLE(CPURegister);
static_assert(sizeof(CPURegister) <= sizeof(int),
              "CPURegister can efficiently be passed by value");

class Register : public CPURegister {
 public:
  static constexpr Register no_reg() { return Register(CPURegister::no_reg()); }

  static constexpr Register Create(int code, int size) {
    return Register(CPURegister::Create(code, size, CPURegister::kRegister));
  }

  static Register XRegFromCode(unsigned code);
  static Register WRegFromCode(unsigned code);

  static constexpr Register from_code(int code) {
    // Always return an X register.
    return Register::Create(code, kXRegSizeInBits);
  }

  static const char* GetSpecialRegisterName(int code) {
    return (code == kSPRegInternalCode) ? "sp" : "UNKNOWN";
  }

 private:
  constexpr explicit Register(const CPURegister& r) : CPURegister(r) {}
};

ASSERT_TRIVIALLY_COPYABLE(Register);
static_assert(sizeof(Register) <= sizeof(int),
              "Register can efficiently be passed by value");

// Assign |source| value to |no_reg| and return the |source|'s previous value.
inline Register ReassignRegister(Register& source) {
  Register result = source;
  source = Register::no_reg();
  return result;
}

// Stack frame alignment and padding.
constexpr int ArgumentPaddingSlots(int argument_count) {
  // Stack frames are aligned to 16 bytes.
  constexpr int kStackFrameAlignment = 16;
  constexpr int alignment_mask = kStackFrameAlignment / kSystemPointerSize - 1;
  return argument_count & alignment_mask;
}

constexpr AliasingKind kFPAliasing = AliasingKind::kOverlap;
constexpr bool kSimdMaskRegisters = false;

enum DoubleRegisterCode {
#define REGISTER_CODE(R) kDoubleCode_##R,
  DOUBLE_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kDoubleAfterLast
};

// Functions for handling NEON vector format information.
enum VectorFormat {
  kFormatUndefined = 0xffffffff,
  kFormat8B = NEON_8B,
  kFormat16B = NEON_16B,
  kFormat4H = NEON_4H,
  kFormat8H = NEON_8H,
  kFormat2S = NEON_2S,
  kFormat4S = NEON_4S,
  kFormat1D = NEON_1D,
  kFormat2D = NEON_2D,

  // Scalar formats. We add the scalar bit to distinguish between scalar and
  // vector enumerations; the bit is always set in the encoding of scalar ops
  // and always clear for vector ops. Although kFormatD and kFormat1D appear
  // to be the same, their meaning is subtly different. The first is a scalar
  // operation, the second a vector operation that only affects one lane.
  kFormatB = NEON_B | NEONScalar,
  kFormatH = NEON_H | NEONScalar,
  kFormatS = NEON_S | NEONScalar,
  kFormatD = NEON_D | NEONScalar
};

VectorFormat VectorFormatHalfWidth(VectorFormat vform);
VectorFormat VectorFormatDoubleWidth(VectorFormat vform);
VectorFormat VectorFormatDoubleLanes(VectorFormat vform);
VectorFormat VectorFormatHalfLanes(VectorFormat vform);
VectorFormat ScalarFormatFromLaneSize(int lanesize);
VectorFormat VectorFormatHalfWidthDoubleLanes(VectorFormat vform);
VectorFormat VectorFormatFillQ(int laneSize);
VectorFormat VectorFormatFillQ(VectorFormat vform);
VectorFormat ScalarFormatFromFormat(VectorFormat vform);
V8_EXPORT_PRIVATE unsigned RegisterSizeInBitsFromFormat(VectorFormat vform);
unsigned RegisterSizeInBytesFromFormat(VectorFormat vform);
int LaneSizeInBytesFromFormat(VectorFormat vform);
unsigned LaneSizeInBitsFromFormat(VectorFormat vform);
int LaneSizeInBytesLog2FromFormat(VectorFormat vform);
V8_EXPORT_PRIVATE int LaneCountFromFormat(VectorFormat vform);
int MaxLaneCountFromFormat(VectorFormat vform);
V8_EXPORT_PRIVATE bool IsVectorFormat(VectorFormat vform);
int64_t MaxIntFromFormat(VectorFormat vform);
int64_t MinIntFromFormat(VectorFormat vform);
uint64_t MaxUintFromFormat(VectorFormat vform);

class VRegister : public CPURegister {
 public:
  static constexpr VRegister no_reg() {
    return VRegister(CPURegister::no_reg(), 0);
  }

  static constexpr VRegister Create(int code, int size, int lane_count = 1) {
    DCHECK(IsValidLaneCount(lane_count));
    return VRegister(CPURegister::Create(code, size, CPURegister::kVRegister),
                     lane_count);
  }

  static VRegister Create(int reg_code, VectorFormat format) {
    int reg_size = RegisterSizeInBitsFromFormat(format);
    int reg_count = IsVectorFormat(format) ? LaneCountFromFormat(format) : 1;
    return VRegister::Create(reg_code, reg_size, reg_count);
  }

  static VRegister BRegFromCode(unsigned code);
  static VRegister HRegFromCode(unsigned code);
  static VRegister SRegFromCode(unsigned code);
  static VRegister DRegFromCode(unsigned code);
  static VRegister QRegFromCode(unsigned code);
  static VRegister VRegFromCode(unsigned code);

  VRegister V8B() const {
    return VRegister::Create(code(), kDRegSizeInBits, 8);
  }
  VRegister V16B() const {
    return VRegister::Create(code(), kQRegSizeInBits, 16);
  }
  VRegister V4H() const {
    return VRegister::Create(code(), kDRegSizeInBits, 4);
  }
  VRegister V8H() const {
    return VRegister::Create(code(), kQRegSizeInBits, 8);
  }
  VRegister V2S() const {
    return VRegister::Create(code(), kDRegSizeInBits, 2);
  }
  VRegister V4S() const {
    return VRegister::Create(code(), kQRegSizeInBits, 4);
  }
  VRegister V2D() const {
    return VRegister::Create(code(), kQRegSizeInBits, 2);
  }
  VRegister V1D() const {
    return VRegister::Create(code(), kDRegSizeInBits, 1);
  }

  VRegister Format(VectorFormat f) const {
    return VRegister::Create(code(), f);
  }

  bool Is8B() const { return (Is64Bits() && (lane_count_ == 8)); }
  bool Is16B() const { return (Is128Bits() && (lane_count_ == 16)); }
  bool Is4H() const { return (Is64Bits() && (lane_count_ == 4)); }
  bool Is8H() const { return (Is128Bits() && (lane_count_ == 8)); }
  bool Is2S() const { return (Is64Bits() && (lane_count_ == 2)); }
  bool Is4S() const { return (Is128Bits() && (lane_count_ == 4)); }
  bool Is1D() const { return (Is64Bits() && (lane_count_ == 1)); }
  bool Is2D() const { return (Is128Bits() && (lane_count_ == 2)); }

  // For consistency, we assert the number of lanes of these scalar registers,
  // even though there are no vectors of equivalent total size with which they
  // could alias.
  bool Is1B() const {
    DCHECK(!(Is8Bits() && IsVector()));
    return Is8Bits();
  }
  bool Is1H() const {
    DCHECK(!(Is16Bits() && IsVector()));
    return Is16Bits();
  }
  bool Is1S() const {
    DCHECK(!(Is32Bits() && IsVector()));
    return Is32Bits();
  }

  bool IsLaneSizeB() const { return LaneSizeInBits() == kBRegSizeInBits; }
  bool IsLaneSizeH() const { return LaneSizeInBits() == kHRegSizeInBits; }
  bool IsLaneSizeS() const { return LaneSizeInBits() == kSRegSizeInBits; }
  bool IsLaneSizeD() const { return LaneSizeInBits() == kDRegSizeInBits; }

  bool IsScalar() const { return lane_count_ == 1; }
  bool IsVector() const { return lane_count_ > 1; }

  bool IsSameFormat(const VRegister& other) const {
    return (reg_size_ == other.reg_size_) && (lane_count_ == other.lane_count_);
  }

  int LaneCount() const { return lane_count_; }

  unsigned LaneSizeInBytes() const { return SizeInBytes() / lane_count_; }

  unsigned LaneSizeInBits() const { return LaneSizeInBytes() * 8; }

  static constexpr int kMaxNumRegisters = kNumberOfVRegisters;
  static_assert(kMaxNumRegisters == kDoubleAfterLast);

  static constexpr VRegister from_code(int code) {
    // Always return a D register.
    return VRegister::Create(code, kDRegSizeInBits);
  }

 private:
  int8_t lane_count_;

  constexpr explicit VRegister(const CPURegister& r, int lane_count)
      : CPURegister(r), lane_count_(lane_count) {}

  static constexpr bool IsValidLaneCount(int lane_count) {
    return base::bits::IsPowerOfTwo(lane_count) && lane_count <= 16;
  }
};

ASSERT_TRIVIALLY_COPYABLE(VRegister);
static_assert(sizeof(VRegister) <= sizeof(int),
              "VRegister can efficiently be passed by value");

// No*Reg is used to indicate an unused argument, or an error case. Note that
// these all compare equal. The Register and VRegister variants are provided for
// convenience.
constexpr Register NoReg = Register::no_reg();
constexpr VRegister NoVReg = VRegister::no_reg();
constexpr CPURegister NoCPUReg = CPURegister::no_reg();
constexpr Register no_reg = NoReg;
constexpr VRegister no_dreg = NoVReg;

#define DEFINE_REGISTER(register_class, name, ...) \
  constexpr register_class name = register_class::Create(__VA_ARGS__)
#define ALIAS_REGISTER(register_class, alias, name) \
  constexpr register_class alias = name

#define DEFINE_REGISTERS(N)                            \
  DEFINE_REGISTER(Register, w##N, N, kWRegSizeInBits); \
  DEFINE_REGISTER(Register, x##N, N, kXRegSizeInBits);
GENERAL_REGISTER_CODE_LIST(DEFINE_REGISTERS)
#undef DEFINE_REGISTERS

DEFINE_REGISTER(Register, wsp, kSPRegInternalCode, kWRegSizeInBits);
DEFINE_REGISTER(Register, sp, kSPRegInternalCode, kXRegSizeInBits);

#define DEFINE_VREGISTERS(N)                            \
  DEFINE_REGISTER(VRegister, b##N, N, kBRegSizeInBits); \
  DEFINE_REGISTER(VRegister, h##N, N, kHRegSizeInBits); \
  DEFINE_REGISTER(VRegister, s##N, N, kSRegSizeInBits); \
  DEFINE_REGISTER(VRegister, d##N, N, kDRegSizeInBits); \
  DEFINE_REGISTER(VRegister, q##N, N, kQRegSizeInBits); \
  DEFINE_REGISTER(VRegister, v##N, N, kQRegSizeInBits);
GENERAL_REGISTER_CODE_LIST(DEFINE_VREGISTERS)
#undef DEFINE_VREGISTERS

#undef DEFINE_REGISTER

// Registers aliases.
ALIAS_REGISTER(Register, kStackPointerRegister, sp);
ALIAS_REGISTER(VRegister, v8_, v8);  // Avoid conflicts with namespace v8.
ALIAS_REGISTER(Register, ip0, x16);
ALIAS_REGISTER(Register, ip1, x17);
ALIAS_REGISTER(Register, wip0, w16);
ALIAS_REGISTER(Register, wip1, w17);
// Root register.
ALIAS_REGISTER(Register, kRootRegister, x26);
ALIAS_REGISTER(Register, rr, x26);
// Pointer cage base register.
#ifdef V8_COMPRESS_POINTERS
ALIAS_REGISTER(Register, kPtrComprCageBaseRegister, x28);
#else
ALIAS_REGISTER(Register, kPtrComprCageBaseRegister, no_reg);
#endif
// Context pointer register.
ALIAS_REGISTER(Register, cp, x27);
ALIAS_REGISTER(Register, fp, x29);
ALIAS_REGISTER(Register, lr, x30);
ALIAS_REGISTER(Register, xzr, x31);
ALIAS_REGISTER(Register, wzr, w31);

// Register used for padding stack slots.
ALIAS_REGISTER(Register, padreg, x31);

// Keeps the 0 double value.
ALIAS_REGISTER(VRegister, fp_zero, d15);
// MacroAssembler fixed V Registers.
// d29 is not part of ALLOCATABLE_DOUBLE_REGISTERS, so use 27 and 28.
ALIAS_REGISTER(VRegister, fp_fixed1, d27);
ALIAS_REGISTER(VRegister, fp_fixed2, d28);

// MacroAssembler scratch V registers.
ALIAS_REGISTER(VRegister, fp_scratch, d30);
ALIAS_REGISTER(VRegister, fp_scratch1, d30);
ALIAS_REGISTER(VRegister, fp_scratch2, d31);

#undef ALIAS_REGISTER

// Arm64 calling convention
constexpr Register kCArgRegs[] = {x0, x1, x2, x3, x4, x5, x6, x7};
constexpr int kRegisterPassedArguments = arraysize(kCArgRegs);
constexpr int kFPRegisterPassedArguments = 8;

// AreAliased returns true if any of the named registers overlap. Arguments set
// to NoReg are ignored. The system stack pointer may be specified.
V8_EXPORT_PRIVATE bool AreAliased(
    const CPURegister& reg1, const CPURegister& reg2,
    const CPURegister& reg3 = NoReg, const CPURegister& reg4 = NoReg,
    const CPURegister& reg5 = NoReg, const CPURegister& reg6 = NoReg,
    const CPURegister& reg7 = NoReg, const CPURegister& reg8 = NoReg);

// AreSameSizeAndType returns true if all of the specified registers have the
// same size, and are of the same type. The system stack pointer may be
// specified. Arguments set to NoReg are ignored, as are any subsequent
// arguments. At least one argument (reg1) must be valid (not NoCPUReg).
V8_EXPORT_PRIVATE bool AreSameSizeAndType(
    const CPURegister& reg1, const CPURegister& reg2 = NoCPUReg,
    const CPURegister& reg3 = NoCPUReg, const CPURegister& reg4 = NoCPUReg,
    const CPURegister& reg5 = NoCPUReg, const CPURegister& reg6 = NoCPUReg,
    const CPURegister& reg7 = NoCPUReg, const CPURegister& reg8 = NoCPUReg);

// AreSameFormat returns true if all of the specified VRegisters have the same
// vector format. Arguments set to NoVReg are ignored, as are any subsequent
// arguments. At least one argument (reg1) must be valid (not NoVReg).
bool AreSameFormat(const Register& reg1, const Register& reg2,
                   const Register& reg3 = NoReg, const Register& reg4 = NoReg);
bool AreSameFormat(const VRegister& reg1, const VRegister& reg2,
                   const VRegister& reg3 = NoVReg,
                   const VRegister& reg4 = NoVReg);

// AreConsecutive returns true if all of the specified VRegisters are
// consecutive in the register file. Arguments may be set to NoVReg, and if so,
// subsequent arguments must also be NoVReg. At least one argument (reg1) must
// be valid (not NoVReg).
V8_EXPORT_PRIVATE bool AreConsecutive(const CPURegister& reg1,
                                      const CPURegister& reg2,
                                      const CPURegister& reg3 = NoReg,
                                      const CPURegister& reg4 = NoReg);

bool AreEven(const CPURegister& reg1, const CPURegister& reg2,
             const CPURegister& reg3 = NoReg, const CPURegister& reg4 = NoReg,
             const CPURegister& reg5 = NoReg, const CPURegister& reg6 = NoReg,
             const CPURegister& reg7 = NoReg, const CPURegister& reg8 = NoReg);

using FloatRegister = VRegister;
using DoubleRegister = VRegister;
using Simd128Register = VRegister;

// Define a {RegisterName} method for {Register} and {VRegister}.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(VRegister, VECTOR_REGISTERS)

// Give alias names to registers for calling conventions.
constexpr Register kReturnRegister0 = x0;
constexpr Register kReturnRegister1 = x1;
constexpr Register kReturnRegister2 = x2;
constexpr Register kJSFunctionRegister = x1;
constexpr Register kContextRegister = cp;
constexpr Register kAllocateSizeRegister = x1;

constexpr Register kInterpreterAccumulatorRegister = x0;
constexpr Register kInterpreterBytecodeOffsetRegister = x19;
constexpr Register kInterpreterBytecodeArrayRegister = x20;
constexpr Register kInterpreterDispatchTableRegister = x21;

constexpr Register kJavaScriptCallArgCountRegister = x0;
constexpr Register kJavaScriptCallCodeStartRegister = x2;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = x3;
constexpr Register kJavaScriptCallExtraArg1Register = x2;

constexpr Register kRuntimeCallFunctionRegister = x1;
constexpr Register kRuntimeCallArgCountRegister = x0;
constexpr Register kRuntimeCallArgvRegister = x11;
constexpr Register kWasmInstanceRegister = x7;
constexpr Register kWasmCompileLazyFuncIndexRegister = x8;
constexpr Register kWasmTrapHandlerFaultAddressRegister = x16;
constexpr Register kSimulatorHltArgument = x16;

constexpr DoubleRegister kFPReturnRegister0 = d0;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_REGISTER_ARM64_H_
