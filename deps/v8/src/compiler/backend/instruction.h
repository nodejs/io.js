// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_H_

#include <iosfwd>
#include <map>
#include <optional>

#include "src/base/compiler-specific.h"
#include "src/base/numbers/double.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/register.h"
#include "src/codegen/source-position.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/frame.h"
#include "src/compiler/opcodes.h"
#include "src/zone/zone-allocator.h"

namespace v8 {
namespace internal {

class RegisterConfiguration;

namespace compiler {

class Schedule;
class SourcePositionTable;

namespace turboshaft {
class Graph;
}

#if defined(V8_CC_MSVC) && defined(V8_TARGET_ARCH_IA32)
// MSVC on x86 has issues with ALIGNAS(8) on InstructionOperand, but does
// align the object to 8 bytes anyway (covered by a static assert below).
// See crbug.com/v8/10796
#define INSTRUCTION_OPERAND_ALIGN
#else
#define INSTRUCTION_OPERAND_ALIGN ALIGNAS(8)
#endif

class V8_EXPORT_PRIVATE INSTRUCTION_OPERAND_ALIGN InstructionOperand {
 public:
  static const int kInvalidVirtualRegister = -1;

  enum Kind {
    INVALID,
    UNALLOCATED,
    CONSTANT,
    IMMEDIATE,
    PENDING,
    // Location operand kinds.
    ALLOCATED,
    FIRST_LOCATION_OPERAND_KIND = ALLOCATED
    // Location operand kinds must be last.
  };

  InstructionOperand() : InstructionOperand(INVALID) {}

  Kind kind() const { return KindField::decode(value_); }

#define INSTRUCTION_OPERAND_PREDICATE(name, type) \
  bool Is##name() const { return kind() == type; }
  INSTRUCTION_OPERAND_PREDICATE(Invalid, INVALID)
  // UnallocatedOperands are place-holder operands created before register
  // allocation. They later are assigned registers and become AllocatedOperands.
  INSTRUCTION_OPERAND_PREDICATE(Unallocated, UNALLOCATED)
  // Constant operands participate in register allocation. They are allocated to
  // registers but have a special "spilling" behavior. When a ConstantOperand
  // value must be rematerialized, it is loaded from an immediate constant
  // rather from an unspilled slot.
  INSTRUCTION_OPERAND_PREDICATE(Constant, CONSTANT)
  // ImmediateOperands do not participate in register allocation and are only
  // embedded directly in instructions, e.g. small integers and on some
  // platforms Objects.
  INSTRUCTION_OPERAND_PREDICATE(Immediate, IMMEDIATE)
  // PendingOperands are pending allocation during register allocation and
  // shouldn't be seen elsewhere. They chain together multiple operators that
  // will be replaced together with the same value when finalized.
  INSTRUCTION_OPERAND_PREDICATE(Pending, PENDING)
  // AllocatedOperands are registers or stack slots that are assigned by the
  // register allocator and are always associated with a virtual register.
  INSTRUCTION_OPERAND_PREDICATE(Allocated, ALLOCATED)
#undef INSTRUCTION_OPERAND_PREDICATE

  inline bool IsAnyLocationOperand() const;
  inline bool IsLocationOperand() const;
  inline bool IsFPLocationOperand() const;
  inline bool IsAnyRegister() const;
  inline bool IsRegister() const;
  inline bool IsFPRegister() const;
  inline bool IsFloatRegister() const;
  inline bool IsDoubleRegister() const;
  inline bool IsSimd128Register() const;
  inline bool IsSimd256Register() const;
  inline bool IsAnyStackSlot() const;
  inline bool IsStackSlot() const;
  inline bool IsFPStackSlot() const;
  inline bool IsFloatStackSlot() const;
  inline bool IsDoubleStackSlot() const;
  inline bool IsSimd128StackSlot() const;
  inline bool IsSimd256StackSlot() const;

  template <typename SubKindOperand>
  static SubKindOperand* New(Zone* zone, const SubKindOperand& op) {
    return zone->New<SubKindOperand>(op);
  }

  static void ReplaceWith(InstructionOperand* dest,
                          const InstructionOperand* src) {
    *dest = *src;
  }

  bool Equals(const InstructionOperand& that) const {
    if (IsPending()) {
      // Pending operands are only equal if they are the same operand.
      return this == &that;
    }
    return this->value_ == that.value_;
  }

  bool Compare(const InstructionOperand& that) const {
    return this->value_ < that.value_;
  }

  bool EqualsCanonicalized(const InstructionOperand& that) const {
    if (IsPending()) {
      // Pending operands can't be canonicalized, so just compare for equality.
      return Equals(that);
    }
    return this->GetCanonicalizedValue() == that.GetCanonicalizedValue();
  }

  bool CompareCanonicalized(const InstructionOperand& that) const {
    DCHECK(!IsPending());
    return this->GetCanonicalizedValue() < that.GetCanonicalizedValue();
  }

  bool InterferesWith(const InstructionOperand& other) const;

  // APIs to aid debugging. For general-stream APIs, use operator<<.
  void Print() const;

  bool operator==(const InstructionOperand& other) const {
    return Equals(other);
  }
  bool operator!=(const InstructionOperand& other) const {
    return !Equals(other);
  }

 protected:
  explicit InstructionOperand(Kind kind) : value_(KindField::encode(kind)) {}

  inline uint64_t GetCanonicalizedValue() const;

  using KindField = base::BitField64<Kind, 0, 3>;

  uint64_t value_;
};

using InstructionOperandVector = ZoneVector<InstructionOperand>;

std::ostream& operator<<(std::ostream&, const InstructionOperand&);

#define INSTRUCTION_OPERAND_CASTS(OperandType, OperandKind)      \
                                                                 \
  static OperandType* cast(InstructionOperand* op) {             \
    DCHECK_EQ(OperandKind, op->kind());                          \
    return static_cast<OperandType*>(op);                        \
  }                                                              \
                                                                 \
  static const OperandType* cast(const InstructionOperand* op) { \
    DCHECK_EQ(OperandKind, op->kind());                          \
    return static_cast<const OperandType*>(op);                  \
  }                                                              \
                                                                 \
  static OperandType cast(const InstructionOperand& op) {        \
    DCHECK_EQ(OperandKind, op.kind());                           \
    return *static_cast<const OperandType*>(&op);                \
  }

class UnallocatedOperand final : public InstructionOperand {
 public:
  enum BasicPolicy { FIXED_SLOT, EXTENDED_POLICY };

  enum ExtendedPolicy {
    NONE,
    REGISTER_OR_SLOT,
    REGISTER_OR_SLOT_OR_CONSTANT,
    FIXED_REGISTER,
    FIXED_FP_REGISTER,
    MUST_HAVE_REGISTER,
    MUST_HAVE_SLOT,
    SAME_AS_INPUT
  };

  // Lifetime of operand inside the instruction.
  enum Lifetime {
    // USED_AT_START operand is guaranteed to be live only at instruction start.
    // The register allocator is free to assign the same register to some other
    // operand used inside instruction (i.e. temporary or output).
    USED_AT_START,

    // USED_AT_END operand is treated as live until the end of instruction.
    // This means that register allocator will not reuse its register for any
    // other operand inside instruction.
    USED_AT_END
  };

  UnallocatedOperand(ExtendedPolicy policy, int virtual_register)
      : UnallocatedOperand(virtual_register) {
    value_ |= BasicPolicyField::encode(EXTENDED_POLICY);
    value_ |= ExtendedPolicyField::encode(policy);
    value_ |= LifetimeField::encode(USED_AT_END);
  }

  UnallocatedOperand(int virtual_register, int input_index)
      : UnallocatedOperand(virtual_register) {
    value_ |= BasicPolicyField::encode(EXTENDED_POLICY);
    value_ |= ExtendedPolicyField::encode(SAME_AS_INPUT);
    value_ |= LifetimeField::encode(USED_AT_END);
    value_ |= InputIndexField::encode(input_index);
  }

  UnallocatedOperand(BasicPolicy policy, int index, int virtual_register)
      : UnallocatedOperand(virtual_register) {
    DCHECK(policy == FIXED_SLOT);
    value_ |= BasicPolicyField::encode(policy);
    value_ |= static_cast<uint64_t>(static_cast<int64_t>(index))
              << FixedSlotIndexField::kShift;
    DCHECK(this->fixed_slot_index() == index);
  }

  UnallocatedOperand(ExtendedPolicy policy, int index, int virtual_register)
      : UnallocatedOperand(virtual_register) {
    DCHECK(policy == FIXED_REGISTER || policy == FIXED_FP_REGISTER);
    value_ |= BasicPolicyField::encode(EXTENDED_POLICY);
    value_ |= ExtendedPolicyField::encode(policy);
    value_ |= LifetimeField::encode(USED_AT_END);
    value_ |= FixedRegisterField::encode(index);
  }

  UnallocatedOperand(ExtendedPolicy policy, Lifetime lifetime,
                     int virtual_register)
      : UnallocatedOperand(virtual_register) {
    value_ |= BasicPolicyField::encode(EXTENDED_POLICY);
    value_ |= ExtendedPolicyField::encode(policy);
    value_ |= LifetimeField::encode(lifetime);
  }

  UnallocatedOperand(int reg_id, int slot_id, int virtual_register)
      : UnallocatedOperand(FIXED_REGISTER, reg_id, virtual_register) {
    value_ |= HasSecondaryStorageField::encode(true);
    value_ |= SecondaryStorageField::encode(slot_id);
  }

  UnallocatedOperand(const UnallocatedOperand& other, int virtual_register) {
    DCHECK_NE(kInvalidVirtualRegister, virtual_register);
    value_ = VirtualRegisterField::update(
        other.value_, static_cast<uint32_t>(virtual_register));
  }

  // Predicates for the operand policy.
  bool HasRegisterOrSlotPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == REGISTER_OR_SLOT;
  }
  bool HasRegisterOrSlotOrConstantPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == REGISTER_OR_SLOT_OR_CONSTANT;
  }
  bool HasFixedPolicy() const {
    return basic_policy() == FIXED_SLOT ||
           extended_policy() == FIXED_REGISTER ||
           extended_policy() == FIXED_FP_REGISTER;
  }
  bool HasRegisterPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == MUST_HAVE_REGISTER;
  }
  bool HasSlotPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == MUST_HAVE_SLOT;
  }
  bool HasSameAsInputPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == SAME_AS_INPUT;
  }
  bool HasFixedSlotPolicy() const { return basic_policy() == FIXED_SLOT; }
  bool HasFixedRegisterPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == FIXED_REGISTER;
  }
  bool HasFixedFPRegisterPolicy() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == FIXED_FP_REGISTER;
  }
  bool HasSecondaryStorage() const {
    return basic_policy() == EXTENDED_POLICY &&
           extended_policy() == FIXED_REGISTER &&
           HasSecondaryStorageField::decode(value_);
  }
  int GetSecondaryStorage() const {
    DCHECK(HasSecondaryStorage());
    return SecondaryStorageField::decode(value_);
  }

  // [basic_policy]: Distinguish between FIXED_SLOT and all other policies.
  BasicPolicy basic_policy() const { return BasicPolicyField::decode(value_); }

  // [extended_policy]: Only for non-FIXED_SLOT. The finer-grained policy.
  ExtendedPolicy extended_policy() const {
    DCHECK(basic_policy() == EXTENDED_POLICY);
    return ExtendedPolicyField::decode(value_);
  }

  int input_index() const {
    DCHECK(HasSameAsInputPolicy());
    return InputIndexField::decode(value_);
  }

  // [fixed_slot_index]: Only for FIXED_SLOT.
  int fixed_slot_index() const {
    DCHECK(HasFixedSlotPolicy());
    return static_cast<int>(static_cast<int64_t>(value_) >>
                            FixedSlotIndexField::kShift);
  }

  // [fixed_register_index]: Only for FIXED_REGISTER or FIXED_FP_REGISTER.
  int fixed_register_index() const {
    DCHECK(HasFixedRegisterPolicy() || HasFixedFPRegisterPolicy());
    return FixedRegisterField::decode(value_);
  }

  // [virtual_register]: The virtual register ID for this operand.
  int32_t virtual_register() const {
    return static_cast<int32_t>(VirtualRegisterField::decode(value_));
  }

  // [lifetime]: Only for non-FIXED_SLOT.
  bool IsUsedAtStart() const {
    return basic_policy() == EXTENDED_POLICY &&
           LifetimeField::decode(value_) == USED_AT_START;
  }

  INSTRUCTION_OPERAND_CASTS(UnallocatedOperand, UNALLOCATED)

  // The encoding used for UnallocatedOperand operands depends on the policy
  // that is
  // stored within the operand. The FIXED_SLOT policy uses a compact encoding
  // because it accommodates a larger pay-load.
  //
  // For FIXED_SLOT policy:
  //     +------------------------------------------------+
  //     |      slot_index   | 0 | virtual_register | 001 |
  //     +------------------------------------------------+
  //
  // For all other (extended) policies:
  //     +-----------------------------------------------------+
  //     |  reg_index  | L | PPP |  1 | virtual_register | 001 |
  //     +-----------------------------------------------------+
  //     L ... Lifetime
  //     P ... Policy
  //
  // The slot index is a signed value which requires us to decode it manually
  // instead of using the base::BitField utility class.

  using VirtualRegisterField = KindField::Next<uint32_t, 32>;

  // base::BitFields for all unallocated operands.
  using BasicPolicyField = VirtualRegisterField::Next<BasicPolicy, 1>;

  // BitFields specific to BasicPolicy::FIXED_SLOT.
  using FixedSlotIndexField = BasicPolicyField::Next<int, 28>;
  static_assert(FixedSlotIndexField::kLastUsedBit == 63);

  // BitFields specific to BasicPolicy::EXTENDED_POLICY.
  using ExtendedPolicyField = BasicPolicyField::Next<ExtendedPolicy, 3>;
  using LifetimeField = ExtendedPolicyField::Next<Lifetime, 1>;
  using HasSecondaryStorageField = LifetimeField::Next<bool, 1>;
  using FixedRegisterField = HasSecondaryStorageField::Next<int, 6>;
  using SecondaryStorageField = FixedRegisterField::Next<int, 3>;
  using InputIndexField = SecondaryStorageField::Next<int, 3>;

 private:
  explicit UnallocatedOperand(int virtual_register)
      : InstructionOperand(UNALLOCATED) {
    value_ |=
        VirtualRegisterField::encode(static_cast<uint32_t>(virtual_register));
  }
};

class ConstantOperand : public InstructionOperand {
 public:
  explicit ConstantOperand(int virtual_register)
      : InstructionOperand(CONSTANT) {
    value_ |=
        VirtualRegisterField::encode(static_cast<uint32_t>(virtual_register));
  }

  int32_t virtual_register() const {
    return static_cast<int32_t>(VirtualRegisterField::decode(value_));
  }

  static ConstantOperand* New(Zone* zone, int virtual_register) {
    return InstructionOperand::New(zone, ConstantOperand(virtual_register));
  }

  INSTRUCTION_OPERAND_CASTS(ConstantOperand, CONSTANT)

  using VirtualRegisterField = KindField::Next<uint32_t, 32>;
};

class ImmediateOperand : public InstructionOperand {
 public:
  enum ImmediateType { INLINE_INT32, INLINE_INT64, INDEXED_RPO, INDEXED_IMM };

  explicit ImmediateOperand(ImmediateType type, int32_t value)
      : InstructionOperand(IMMEDIATE) {
    value_ |= TypeField::encode(type);
    value_ |= static_cast<uint64_t>(static_cast<int64_t>(value))
              << ValueField::kShift;
  }

  ImmediateType type() const { return TypeField::decode(value_); }

  int32_t inline_int32_value() const {
    DCHECK_EQ(INLINE_INT32, type());
    return static_cast<int64_t>(value_) >> ValueField::kShift;
  }

  int64_t inline_int64_value() const {
    DCHECK_EQ(INLINE_INT64, type());
    return static_cast<int64_t>(value_) >> ValueField::kShift;
  }

  int32_t indexed_value() const {
    DCHECK(type() == INDEXED_IMM || type() == INDEXED_RPO);
    return static_cast<int64_t>(value_) >> ValueField::kShift;
  }

  static ImmediateOperand* New(Zone* zone, ImmediateType type, int32_t value) {
    return InstructionOperand::New(zone, ImmediateOperand(type, value));
  }

  INSTRUCTION_OPERAND_CASTS(ImmediateOperand, IMMEDIATE)

  using TypeField = KindField::Next<ImmediateType, 2>;
  static_assert(TypeField::kLastUsedBit < 32);
  using ValueField = base::BitField64<int32_t, 32, 32>;
};

class PendingOperand : public InstructionOperand {
 public:
  PendingOperand() : InstructionOperand(PENDING) {}
  explicit PendingOperand(PendingOperand* next_operand) : PendingOperand() {
    set_next(next_operand);
  }

  void set_next(PendingOperand* next) {
    DCHECK_NULL(this->next());
    uintptr_t shifted_value =
        reinterpret_cast<uintptr_t>(next) >> kPointerShift;
    DCHECK_EQ(reinterpret_cast<uintptr_t>(next),
              shifted_value << kPointerShift);
    value_ |= NextOperandField::encode(static_cast<uint64_t>(shifted_value));
  }

  PendingOperand* next() const {
    uintptr_t shifted_value =
        static_cast<uint64_t>(NextOperandField::decode(value_));
    return reinterpret_cast<PendingOperand*>(shifted_value << kPointerShift);
  }

  static PendingOperand* New(Zone* zone, PendingOperand* previous_operand) {
    return InstructionOperand::New(zone, PendingOperand(previous_operand));
  }

  INSTRUCTION_OPERAND_CASTS(PendingOperand, PENDING)

 private:
  // Operands are uint64_t values and so are aligned to 8 byte boundaries,
  // therefore we can shift off the bottom three zeros without losing data.
  static const uint64_t kPointerShift = 3;
  static_assert(alignof(InstructionOperand) >= (1 << kPointerShift));

  using NextOperandField = KindField::Next<uint64_t, 61>;
  static_assert(NextOperandField::kLastUsedBit == 63);
};

class LocationOperand : public InstructionOperand {
 public:
  enum LocationKind { REGISTER, STACK_SLOT };

  LocationOperand(InstructionOperand::Kind operand_kind,
                  LocationOperand::LocationKind location_kind,
                  MachineRepresentation rep, int index)
      : InstructionOperand(operand_kind) {
    DCHECK_IMPLIES(location_kind == REGISTER, index >= 0);
    DCHECK(IsSupportedRepresentation(rep));
    value_ |= LocationKindField::encode(location_kind);
    value_ |= RepresentationField::encode(rep);
    value_ |= static_cast<uint64_t>(static_cast<int64_t>(index))
              << IndexField::kShift;
  }

  int index() const {
    DCHECK(IsStackSlot() || IsFPStackSlot());
    return static_cast<int64_t>(value_) >> IndexField::kShift;
  }

  int register_code() const {
    DCHECK(IsRegister() || IsFPRegister());
    return static_cast<int64_t>(value_) >> IndexField::kShift;
  }

  Register GetRegister() const {
    DCHECK(IsRegister());
    return Register::from_code(register_code());
  }

  FloatRegister GetFloatRegister() const {
    DCHECK(IsFloatRegister());
    return FloatRegister::from_code(register_code());
  }

  DoubleRegister GetDoubleRegister() const {
    // On platforms where FloatRegister, DoubleRegister, and Simd128Register
    // are all the same type, it's convenient to treat everything as a
    // DoubleRegister, so be lax about type checking here.
    DCHECK(IsFPRegister());
    return DoubleRegister::from_code(register_code());
  }

  Simd128Register GetSimd128Register() const {
    DCHECK(IsSimd128Register());
    return Simd128Register::from_code(register_code());
  }

#if defined(V8_TARGET_ARCH_X64)
  // On x64, Simd256 and Simd128 share the identical register.
  Simd128Register GetSimd256RegisterAsSimd128() const {
    DCHECK(IsSimd256Register());
    return Simd128Register::from_code(register_code());
  }

  Simd256Register GetSimd256Register() const {
    DCHECK(IsSimd256Register());
    return Simd256Register::from_code(register_code());
  }
#endif

  LocationKind location_kind() const {
    return LocationKindField::decode(value_);
  }

  MachineRepresentation representation() const {
    return RepresentationField::decode(value_);
  }

  static bool IsSupportedRepresentation(MachineRepresentation rep) {
    switch (rep) {
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord64:
      case MachineRepresentation::kFloat32:
      case MachineRepresentation::kFloat64:
      case MachineRepresentation::kSimd128:
      case MachineRepresentation::kSimd256:
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
      case MachineRepresentation::kProtectedPointer:
      case MachineRepresentation::kSandboxedPointer:
        return true;
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kFloat16:
      case MachineRepresentation::kNone:
        return false;
      case MachineRepresentation::kMapWord:
      case MachineRepresentation::kIndirectPointer:
        UNREACHABLE();
    }
  }

  // Return true if the locations can be moved to one another.
  bool IsCompatible(LocationOperand* op);

  static LocationOperand* cast(InstructionOperand* op) {
    DCHECK(op->IsAnyLocationOperand());
    return static_cast<LocationOperand*>(op);
  }

  static const LocationOperand* cast(const InstructionOperand* op) {
    DCHECK(op->IsAnyLocationOperand());
    return static_cast<const LocationOperand*>(op);
  }

  static LocationOperand cast(const InstructionOperand& op) {
    DCHECK(op.IsAnyLocationOperand());
    return *static_cast<const LocationOperand*>(&op);
  }

  using LocationKindField = KindField::Next<LocationKind, 1>;
  using RepresentationField = LocationKindField::Next<MachineRepresentation, 8>;
  static_assert(RepresentationField::kLastUsedBit < 32);
  using IndexField = base::BitField64<int32_t, 32, 32>;
};

class AllocatedOperand : public LocationOperand {
 public:
  AllocatedOperand(LocationKind kind, MachineRepresentation rep, int index)
      : LocationOperand(ALLOCATED, kind, rep, index) {}

  static AllocatedOperand* New(Zone* zone, LocationKind kind,
                               MachineRepresentation rep, int index) {
    return InstructionOperand::New(zone, AllocatedOperand(kind, rep, index));
  }

  INSTRUCTION_OPERAND_CASTS(AllocatedOperand, ALLOCATED)
};

#undef INSTRUCTION_OPERAND_CASTS

bool InstructionOperand::IsAnyLocationOperand() const {
  return this->kind() >= FIRST_LOCATION_OPERAND_KIND;
}

bool InstructionOperand::IsLocationOperand() const {
  return IsAnyLocationOperand() &&
         !IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsFPLocationOperand() const {
  return IsAnyLocationOperand() &&
         IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsAnyRegister() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::REGISTER;
}

bool InstructionOperand::IsRegister() const {
  return IsAnyRegister() &&
         !IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsFPRegister() const {
  return IsAnyRegister() &&
         IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsFloatRegister() const {
  return IsAnyRegister() && LocationOperand::cast(this)->representation() ==
                                MachineRepresentation::kFloat32;
}

bool InstructionOperand::IsDoubleRegister() const {
  return IsAnyRegister() && LocationOperand::cast(this)->representation() ==
                                MachineRepresentation::kFloat64;
}

bool InstructionOperand::IsSimd128Register() const {
  return IsAnyRegister() && LocationOperand::cast(this)->representation() ==
                                MachineRepresentation::kSimd128;
}

bool InstructionOperand::IsSimd256Register() const {
  return IsAnyRegister() && LocationOperand::cast(this)->representation() ==
                                MachineRepresentation::kSimd256;
}

bool InstructionOperand::IsAnyStackSlot() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::STACK_SLOT;
}

bool InstructionOperand::IsStackSlot() const {
  return IsAnyStackSlot() &&
         !IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsFPStackSlot() const {
  return IsAnyStackSlot() &&
         IsFloatingPoint(LocationOperand::cast(this)->representation());
}

bool InstructionOperand::IsFloatStackSlot() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::STACK_SLOT &&
         LocationOperand::cast(this)->representation() ==
             MachineRepresentation::kFloat32;
}

bool InstructionOperand::IsDoubleStackSlot() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::STACK_SLOT &&
         LocationOperand::cast(this)->representation() ==
             MachineRepresentation::kFloat64;
}

bool InstructionOperand::IsSimd128StackSlot() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::STACK_SLOT &&
         LocationOperand::cast(this)->representation() ==
             MachineRepresentation::kSimd128;
}

bool InstructionOperand::IsSimd256StackSlot() const {
  return IsAnyLocationOperand() &&
         LocationOperand::cast(this)->location_kind() ==
             LocationOperand::STACK_SLOT &&
         LocationOperand::cast(this)->representation() ==
             MachineRepresentation::kSimd256;
}

uint64_t InstructionOperand::GetCanonicalizedValue() const {
  if (IsAnyLocationOperand()) {
    MachineRepresentation canonical = MachineRepresentation::kNone;
    if (IsFPRegister()) {
      if (kFPAliasing == AliasingKind::kOverlap) {
        // We treat all FP register operands the same for simple aliasing.
        canonical = MachineRepresentation::kFloat64;
      } else if (kFPAliasing == AliasingKind::kIndependent) {
        if (IsSimd128Register()) {
          canonical = MachineRepresentation::kSimd128;
        } else {
          canonical = MachineRepresentation::kFloat64;
        }
      } else {
        // We need to distinguish FP register operands of different reps when
        // aliasing is AliasingKind::kCombine (e.g. ARM).
        DCHECK_EQ(kFPAliasing, AliasingKind::kCombine);
        canonical = LocationOperand::cast(this)->representation();
      }
    }
    return InstructionOperand::KindField::update(
        LocationOperand::RepresentationField::update(this->value_, canonical),
        LocationOperand::ALLOCATED);
  }
  return this->value_;
}

// Required for maps that don't care about machine type.
struct CompareOperandModuloType {
  bool operator()(const InstructionOperand& a,
                  const InstructionOperand& b) const {
    return a.CompareCanonicalized(b);
  }
};

class V8_EXPORT_PRIVATE MoveOperands final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  MoveOperands(const InstructionOperand& source,
               const InstructionOperand& destination)
      : source_(source), destination_(destination) {
    DCHECK(!source.IsInvalid() && !destination.IsInvalid());
    CheckPointerCompressionConsistency();
  }

  MoveOperands(const MoveOperands&) = delete;
  MoveOperands& operator=(const MoveOperands&) = delete;

  void CheckPointerCompressionConsistency() {
#if DEBUG && V8_COMPRESS_POINTERS
    if (!source_.IsLocationOperand()) return;
    if (!destination_.IsLocationOperand()) return;
    using MR = MachineRepresentation;
    MR dest_rep = LocationOperand::cast(&destination_)->representation();
    if (dest_rep == MR::kTagged || dest_rep == MR::kTaggedPointer) {
      MR src_rep = LocationOperand::cast(&source_)->representation();
      DCHECK_NE(src_rep, MR::kCompressedPointer);
      // TODO(dmercadier): it would be nice to insert a DEBUG runtime check here
      // to make sure that if `src_rep` is kCompressed, then the value is a Smi.
    }
#endif
  }

  const InstructionOperand& source() const { return source_; }
  InstructionOperand& source() { return source_; }
  void set_source(const InstructionOperand& operand) {
    source_ = operand;
    CheckPointerCompressionConsistency();
  }

  const InstructionOperand& destination() const { return destination_; }
  InstructionOperand& destination() { return destination_; }
  void set_destination(const InstructionOperand& operand) {
    destination_ = operand;
    CheckPointerCompressionConsistency();
  }

  // The gap resolver marks moves as "in-progress" by clearing the
  // destination (but not the source).
  bool IsPending() const {
    return destination_.IsInvalid() && !source_.IsInvalid();
  }
  void SetPending() { destination_ = InstructionOperand(); }

  // A move is redundant if it's been eliminated or if its source and
  // destination are the same.
  bool IsRedundant() const {
    DCHECK_IMPLIES(!destination_.IsInvalid(), !destination_.IsConstant());
    return IsEliminated() || source_.EqualsCanonicalized(destination_);
  }

  // We clear both operands to indicate move that's been eliminated.
  void Eliminate() { source_ = destination_ = InstructionOperand(); }
  bool IsEliminated() const {
    DCHECK_IMPLIES(source_.IsInvalid(), destination_.IsInvalid());
    return source_.IsInvalid();
  }

  // APIs to aid debugging. For general-stream APIs, use operator<<.
  void Print() const;

  bool Equals(const MoveOperands& that) const {
    if (IsRedundant() && that.IsRedundant()) return true;
    return source_.Equals(that.source_) &&
           destination_.Equals(that.destination_);
  }

 private:
  InstructionOperand source_;
  InstructionOperand destination_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, const MoveOperands&);

class V8_EXPORT_PRIVATE ParallelMove final
    : public NON_EXPORTED_BASE(ZoneVector<MoveOperands*>),
      public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit ParallelMove(Zone* zone) : ZoneVector<MoveOperands*>(zone) {}
  ParallelMove(const ParallelMove&) = delete;
  ParallelMove& operator=(const ParallelMove&) = delete;

  MoveOperands* AddMove(const InstructionOperand& from,
                        const InstructionOperand& to) {
    return AddMove(from, to, zone());
  }

  MoveOperands* AddMove(const InstructionOperand& from,
                        const InstructionOperand& to,
                        Zone* operand_allocation_zone) {
    if (from.EqualsCanonicalized(to)) return nullptr;
    MoveOperands* move = operand_allocation_zone->New<MoveOperands>(from, to);
    if (empty()) reserve(4);
    push_back(move);
    return move;
  }

  bool IsRedundant() const;

  // Prepare this ParallelMove to insert move as if it happened in a subsequent
  // ParallelMove.  move->source() may be changed.  Any MoveOperands added to
  // to_eliminate must be Eliminated.
  void PrepareInsertAfter(MoveOperands* move,
                          ZoneVector<MoveOperands*>* to_eliminate) const;

  bool Equals(const ParallelMove& that) const;

  // Eliminate all the MoveOperands in this ParallelMove.
  void Eliminate();
};

std::ostream& operator<<(std::ostream&, const ParallelMove&);

// TODOC(dmercadier): what is a ReferenceMap exactly, what does it contain,
// when is it created, and what is it used for?
class ReferenceMap final : public ZoneObject {
 public:
  explicit ReferenceMap(Zone* zone)
      : reference_operands_(zone), instruction_position_(-1) {}

  const ZoneVector<InstructionOperand>& reference_operands() const {
    return reference_operands_;
  }
  int instruction_position() const { return instruction_position_; }

  void set_instruction_position(int pos) {
    DCHECK_EQ(-1, instruction_position_);
    instruction_position_ = pos;
  }

  void RecordReference(const AllocatedOperand& op);

 private:
  friend std::ostream& operator<<(std::ostream&, const ReferenceMap&);

  ZoneVector<InstructionOperand> reference_operands_;
  int instruction_position_;
};

std::ostream& operator<<(std::ostream&, const ReferenceMap&);

class InstructionBlock;

class V8_EXPORT_PRIVATE Instruction final {
 public:
  Instruction(const Instruction&) = delete;
  Instruction& operator=(const Instruction&) = delete;

  size_t OutputCount() const { return OutputCountField::decode(bit_field_); }
  const InstructionOperand* OutputAt(size_t i) const {
    DCHECK_LT(i, OutputCount());
    return &operands_[i];
  }
  InstructionOperand* OutputAt(size_t i) {
    DCHECK_LT(i, OutputCount());
    return &operands_[i];
  }

  bool HasOutput() const { return OutputCount() > 0; }
  const InstructionOperand* Output() const { return OutputAt(0); }
  InstructionOperand* Output() { return OutputAt(0); }

  size_t InputCount() const { return InputCountField::decode(bit_field_); }
  const InstructionOperand* InputAt(size_t i) const {
    DCHECK_LT(i, InputCount());
    return &operands_[OutputCount() + i];
  }
  InstructionOperand* InputAt(size_t i) {
    DCHECK_LT(i, InputCount());
    return &operands_[OutputCount() + i];
  }

  size_t TempCount() const { return TempCountField::decode(bit_field_); }
  const InstructionOperand* TempAt(size_t i) const {
    DCHECK_LT(i, TempCount());
    return &operands_[OutputCount() + InputCount() + i];
  }
  InstructionOperand* TempAt(size_t i) {
    DCHECK_LT(i, TempCount());
    return &operands_[OutputCount() + InputCount() + i];
  }

  InstructionCode opcode() const { return opcode_; }
  ArchOpcode arch_opcode() const { return ArchOpcodeField::decode(opcode()); }
  AddressingMode addressing_mode() const {
    return AddressingModeField::decode(opcode());
  }
  FlagsMode flags_mode() const { return FlagsModeField::decode(opcode()); }
  FlagsCondition flags_condition() const {
    return FlagsConditionField::decode(opcode());
  }
  int misc() const { return MiscField::decode(opcode()); }
  bool HasMemoryAccessMode() const {
    return compiler::HasMemoryAccessMode(arch_opcode());
  }
  MemoryAccessMode memory_access_mode() const {
    DCHECK(HasMemoryAccessMode());
    return AccessModeField::decode(opcode());
  }

  static Instruction* New(Zone* zone, InstructionCode opcode) {
    return New(zone, opcode, 0, nullptr, 0, nullptr, 0, nullptr);
  }

  static Instruction* New(Zone* zone, InstructionCode opcode,
                          size_t output_count, InstructionOperand* outputs,
                          size_t input_count, InstructionOperand* inputs,
                          size_t temp_count, InstructionOperand* temps) {
    DCHECK(output_count == 0 || outputs != nullptr);
    DCHECK(input_count == 0 || inputs != nullptr);
    DCHECK(temp_count == 0 || temps != nullptr);
    // TODO(turbofan): Handle this gracefully. See crbug.com/582702.
    CHECK(InputCountField::is_valid(input_count));

    size_t total_extra_ops = output_count + input_count + temp_count;
    if (total_extra_ops != 0) total_extra_ops--;
    int size = static_cast<int>(
        RoundUp(sizeof(Instruction), sizeof(InstructionOperand)) +
        total_extra_ops * sizeof(InstructionOperand));
    return new (zone->Allocate<Instruction>(size)) Instruction(
        opcode, output_count, outputs, input_count, inputs, temp_count, temps);
  }

  Instruction* MarkAsCall() {
    bit_field_ = IsCallField::update(bit_field_, true);
    return this;
  }
  bool IsCall() const { return IsCallField::decode(bit_field_); }
  bool NeedsReferenceMap() const { return IsCall(); }
  bool HasReferenceMap() const { return reference_map_ != nullptr; }

  bool ClobbersRegisters() const { return IsCall(); }
  bool ClobbersTemps() const { return IsCall(); }
  bool ClobbersDoubleRegisters() const { return IsCall(); }
  ReferenceMap* reference_map() const { return reference_map_; }

  void set_reference_map(ReferenceMap* map) {
    DCHECK(NeedsReferenceMap());
    DCHECK(!reference_map_);
    reference_map_ = map;
  }

  void OverwriteWithNop() {
    opcode_ = ArchOpcodeField::encode(kArchNop);
    bit_field_ = 0;
    reference_map_ = nullptr;
  }

  bool IsNop() const { return arch_opcode() == kArchNop; }

  bool IsDeoptimizeCall() const {
    return arch_opcode() == ArchOpcode::kArchDeoptimize ||
           FlagsModeField::decode(opcode()) == kFlags_deoptimize;
  }

  bool IsTrap() const {
    return FlagsModeField::decode(opcode()) == kFlags_trap;
  }

  bool IsJump() const { return arch_opcode() == ArchOpcode::kArchJmp; }
  bool IsRet() const { return arch_opcode() == ArchOpcode::kArchRet; }
  bool IsTailCall() const {
#if V8_ENABLE_WEBASSEMBLY
    return arch_opcode() <= ArchOpcode::kArchTailCallWasm;
#else
    return arch_opcode() <= ArchOpcode::kArchTailCallAddress;
#endif  // V8_ENABLE_WEBASSEMBLY
  }
  bool IsThrow() const {
    return arch_opcode() == ArchOpcode::kArchThrowTerminator;
  }

  static constexpr bool IsCallWithDescriptorFlags(InstructionCode arch_opcode) {
    return arch_opcode <= ArchOpcode::kArchCallBuiltinPointer;
  }
  bool IsCallWithDescriptorFlags() const {
    return IsCallWithDescriptorFlags(arch_opcode());
  }
  bool HasCallDescriptorFlag(CallDescriptor::Flag flag) const {
    DCHECK(IsCallWithDescriptorFlags());
    static_assert(CallDescriptor::kFlagsBitsEncodedInInstructionCode == 10);
#ifdef DEBUG
    static constexpr int kInstructionCodeFlagsMask =
        ((1 << CallDescriptor::kFlagsBitsEncodedInInstructionCode) - 1);
    DCHECK_EQ(static_cast<int>(flag) & kInstructionCodeFlagsMask, flag);
#endif
    return MiscField::decode(opcode()) & flag;
  }

  // For call instructions, computes the index of the CodeEntrypointTag input.
  size_t CodeEnrypointTagInputIndex() const {
    // Keep in sync with instruction-selector.cc where the inputs are assembled.
    switch (arch_opcode()) {
      case kArchCallCodeObject:
        return InputCount() -
               (HasCallDescriptorFlag(CallDescriptor::kHasExceptionHandler)
                    ? 2
                    : 1);
      case kArchTailCallCodeObject:
        return InputCount() - 3;
      default:
        UNREACHABLE();
    }
  }

  enum GapPosition {
    START,
    END,
    FIRST_GAP_POSITION = START,
    LAST_GAP_POSITION = END
  };

  ParallelMove* GetOrCreateParallelMove(GapPosition pos, Zone* zone) {
    if (parallel_moves_[pos] == nullptr) {
      parallel_moves_[pos] = zone->New<ParallelMove>(zone);
    }
    return parallel_moves_[pos];
  }

  ParallelMove* GetParallelMove(GapPosition pos) {
    return parallel_moves_[pos];
  }

  const ParallelMove* GetParallelMove(GapPosition pos) const {
    return parallel_moves_[pos];
  }

  bool AreMovesRedundant() const;

  ParallelMove* const* parallel_moves() const { return &parallel_moves_[0]; }
  ParallelMove** parallel_moves() { return &parallel_moves_[0]; }

  // The block_id may be invalidated in JumpThreading. It is only important for
  // register allocation, to avoid searching for blocks from instruction
  // indexes.
  InstructionBlock* block() const { return block_; }
  void set_block(InstructionBlock* block) {
    DCHECK_NOT_NULL(block);
    block_ = block;
  }

  // APIs to aid debugging. For general-stream APIs, use operator<<.
  void Print() const;

  using OutputCountField = base::BitField<size_t, 0, 8>;
  using InputCountField = base::BitField<size_t, 8, 16>;
  using TempCountField = base::BitField<size_t, 24, 6>;

  static const size_t kMaxOutputCount = OutputCountField::kMax;
  static const size_t kMaxInputCount = InputCountField::kMax;
  static const size_t kMaxTempCount = TempCountField::kMax;

 private:
  explicit Instruction(InstructionCode opcode);

  Instruction(InstructionCode opcode, size_t output_count,
              InstructionOperand* outputs, size_t input_count,
              InstructionOperand* inputs, size_t temp_count,
              InstructionOperand* temps);

  using IsCallField = base::BitField<bool, 30, 1>;

  InstructionCode opcode_;
  uint32_t bit_field_;
  ParallelMove* parallel_moves_[2];
  ReferenceMap* reference_map_;
  InstructionBlock* block_;
  InstructionOperand operands_[1];
};

std::ostream& operator<<(std::ostream&, const Instruction&);

class RpoNumber final {
 public:
  static const int kInvalidRpoNumber = -1;
  RpoNumber() : index_(kInvalidRpoNumber) {}

  int ToInt() const {
    DCHECK(IsValid());
    return index_;
  }
  size_t ToSize() const {
    DCHECK(IsValid());
    return static_cast<size_t>(index_);
  }
  bool IsValid() const { return index_ >= 0; }
  static RpoNumber FromInt(int index) { return RpoNumber(index); }
  static RpoNumber Invalid() { return RpoNumber(kInvalidRpoNumber); }

  bool IsNext(const RpoNumber other) const {
    DCHECK(IsValid());
    return other.index_ == this->index_ + 1;
  }

  RpoNumber Next() const {
    DCHECK(IsValid());
    return RpoNumber(index_ + 1);
  }

  // Comparison operators.
  bool operator==(RpoNumber other) const { return index_ == other.index_; }
  bool operator!=(RpoNumber other) const { return index_ != other.index_; }
  bool operator>(RpoNumber other) const { return index_ > other.index_; }
  bool operator<(RpoNumber other) const { return index_ < other.index_; }
  bool operator<=(RpoNumber other) const { return index_ <= other.index_; }
  bool operator>=(RpoNumber other) const { return index_ >= other.index_; }

 private:
  explicit RpoNumber(int32_t index) : index_(index) {}
  int32_t index_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, const RpoNumber&);

class V8_EXPORT_PRIVATE Constant final {
 public:
  enum Type {
    kInt32,
    kInt64,
    kFloat32,
    kFloat64,
    kExternalReference,
    kCompressedHeapObject,
    kHeapObject,
    kRpoNumber
  };

  explicit Constant(int32_t v);
  explicit Constant(int64_t v) : type_(kInt64), value_(v) {}
  explicit Constant(float v)
      : type_(kFloat32), value_(base::bit_cast<int32_t>(v)) {}
  explicit Constant(Float32 v) : type_(kFloat32), value_(v.get_bits()) {}
  explicit Constant(double v)
      : type_(kFloat64), value_(base::bit_cast<int64_t>(v)) {}
  explicit Constant(Float64 v) : type_(kFloat64), value_(v.get_bits()) {}
  explicit Constant(ExternalReference ref)
      : type_(kExternalReference),
        value_(base::bit_cast<intptr_t>(ref.raw())) {}
  explicit Constant(Handle<HeapObject> obj, bool is_compressed = false)
      : type_(is_compressed ? kCompressedHeapObject : kHeapObject),
        value_(base::bit_cast<intptr_t>(obj)) {}
  explicit Constant(RpoNumber rpo) : type_(kRpoNumber), value_(rpo.ToInt()) {}
  explicit Constant(RelocatablePtrConstantInfo info);

  Type type() const { return type_; }

  RelocInfo::Mode rmode() const { return rmode_; }

  bool FitsInInt32() const {
    if (type() == kInt32) return true;
    DCHECK(type() == kInt64);
    return value_ >= std::numeric_limits<int32_t>::min() &&
           value_ <= std::numeric_limits<int32_t>::max();
  }

  int32_t ToInt32() const {
    DCHECK(FitsInInt32());
    const int32_t value = static_cast<int32_t>(value_);
    DCHECK_EQ(value_, static_cast<int64_t>(value));
    return value;
  }

  int64_t ToInt64() const {
    if (type() == kInt32) return ToInt32();
    DCHECK_EQ(kInt64, type());
    return value_;
  }

  float ToFloat32() const {
    // TODO(ahaas): We should remove this function. If value_ has the bit
    // representation of a signalling NaN, then returning it as float can cause
    // the signalling bit to flip, and value_ is returned as a quiet NaN.
    DCHECK_EQ(kFloat32, type());
    return base::bit_cast<float>(static_cast<int32_t>(value_));
  }

  // TODO(ahaas): All callers of ToFloat32() should call this function instead
  // to preserve signaling NaNs.
  Float32 ToFloat32Safe() const {
    DCHECK_EQ(kFloat32, type());
    return Float32::FromBits(static_cast<uint32_t>(value_));
  }

  uint32_t ToFloat32AsInt() const {
    DCHECK_EQ(kFloat32, type());
    return base::bit_cast<uint32_t>(static_cast<int32_t>(value_));
  }

  base::Double ToFloat64() const {
    DCHECK_EQ(kFloat64, type());
    return base::Double(base::bit_cast<uint64_t>(value_));
  }

  ExternalReference ToExternalReference() const {
    DCHECK_EQ(kExternalReference, type());
    return ExternalReference::FromRawAddress(static_cast<Address>(value_));
  }

  RpoNumber ToRpoNumber() const {
    DCHECK_EQ(kRpoNumber, type());
    return RpoNumber::FromInt(static_cast<int>(value_));
  }

  Handle<HeapObject> ToHeapObject() const;
  Handle<Code> ToCode() const;

 private:
  Type type_;
  RelocInfo::Mode rmode_ = RelocInfo::NO_INFO;
  int64_t value_;
};

std::ostream& operator<<(std::ostream&, const Constant&);

// Forward declarations.
class FrameStateDescriptor;

enum class StateValueKind : uint8_t {
  kArgumentsElements,
  kArgumentsLength,
  kRestLength,
  kPlain,
  kOptimizedOut,
  kNested,
  kDuplicate
};

std::ostream& operator<<(std::ostream& os, StateValueKind kind);

class StateValueDescriptor {
 public:
  StateValueDescriptor()
      : kind_(StateValueKind::kPlain), type_(MachineType::AnyTagged()) {}

  static StateValueDescriptor ArgumentsElements(ArgumentsStateType type) {
    StateValueDescriptor descr(StateValueKind::kArgumentsElements,
                               MachineType::AnyTagged());
    descr.args_type_ = type;
    return descr;
  }
  static StateValueDescriptor ArgumentsLength() {
    return StateValueDescriptor(StateValueKind::kArgumentsLength,
                                MachineType::AnyTagged());
  }
  static StateValueDescriptor RestLength() {
    return StateValueDescriptor(StateValueKind::kRestLength,
                                MachineType::AnyTagged());
  }
  static StateValueDescriptor Plain(MachineType type) {
    return StateValueDescriptor(StateValueKind::kPlain, type);
  }
  static StateValueDescriptor OptimizedOut() {
    return StateValueDescriptor(StateValueKind::kOptimizedOut,
                                MachineType::AnyTagged());
  }
  static StateValueDescriptor Recursive(size_t id) {
    StateValueDescriptor descr(StateValueKind::kNested,
                               MachineType::AnyTagged());
    descr.id_ = id;
    return descr;
  }
  static StateValueDescriptor Duplicate(size_t id) {
    StateValueDescriptor descr(StateValueKind::kDuplicate,
                               MachineType::AnyTagged());
    descr.id_ = id;
    return descr;
  }

  bool IsArgumentsElements() const {
    return kind_ == StateValueKind::kArgumentsElements;
  }
  bool IsArgumentsLength() const {
    return kind_ == StateValueKind::kArgumentsLength;
  }
  bool IsRestLength() const { return kind_ == StateValueKind::kRestLength; }
  bool IsPlain() const { return kind_ == StateValueKind::kPlain; }
  bool IsOptimizedOut() const { return kind_ == StateValueKind::kOptimizedOut; }
  bool IsNested() const { return kind_ == StateValueKind::kNested; }
  bool IsDuplicate() const { return kind_ == StateValueKind::kDuplicate; }
  MachineType type() const { return type_; }
  size_t id() const {
    DCHECK(kind_ == StateValueKind::kDuplicate ||
           kind_ == StateValueKind::kNested);
    return id_;
  }
  ArgumentsStateType arguments_type() const {
    DCHECK(kind_ == StateValueKind::kArgumentsElements);
    return args_type_;
  }

  void Print(std::ostream& os) const;

 private:
  StateValueDescriptor(StateValueKind kind, MachineType type)
      : kind_(kind), type_(type) {}

  StateValueKind kind_;
  MachineType type_;
  union {
    size_t id_;
    ArgumentsStateType args_type_;
  };
};

class StateValueList {
 public:
  explicit StateValueList(Zone* zone) : fields_(zone), nested_(zone) {}

  size_t size() { return fields_.size(); }

  size_t nested_count() { return nested_.size(); }

  struct Value {
    StateValueDescriptor* desc;
    StateValueList* nested;

    Value(StateValueDescriptor* desc, StateValueList* nested)
        : desc(desc), nested(nested) {}
  };

  class iterator {
   public:
    // Bare minimum of operators needed for range iteration.
    bool operator!=(const iterator& other) const {
      return field_iterator != other.field_iterator;
    }
    bool operator==(const iterator& other) const {
      return field_iterator == other.field_iterator;
    }
    iterator& operator++() {
      if (field_iterator->IsNested()) {
        nested_iterator++;
      }
      ++field_iterator;
      return *this;
    }
    Value operator*() {
      StateValueDescriptor* desc = &(*field_iterator);
      StateValueList* nested = desc->IsNested() ? *nested_iterator : nullptr;
      return Value(desc, nested);
    }

   private:
    friend class StateValueList;

    iterator(ZoneVector<StateValueDescriptor>::iterator it,
             ZoneVector<StateValueList*>::iterator nested)
        : field_iterator(it), nested_iterator(nested) {}

    ZoneVector<StateValueDescriptor>::iterator field_iterator;
    ZoneVector<StateValueList*>::iterator nested_iterator;
  };

  struct Slice {
    Slice(ZoneVector<StateValueDescriptor>::iterator start, size_t fields)
        : start_position(start), fields_count(fields) {}

    ZoneVector<StateValueDescriptor>::iterator start_position;
    size_t fields_count;
  };

  void ReserveSize(size_t size) { fields_.reserve(size); }

  StateValueList* PushRecursiveField(Zone* zone, size_t id) {
    fields_.push_back(StateValueDescriptor::Recursive(id));
    StateValueList* nested = zone->New<StateValueList>(zone);
    nested_.push_back(nested);
    return nested;
  }
  void PushArgumentsElements(ArgumentsStateType type) {
    fields_.push_back(StateValueDescriptor::ArgumentsElements(type));
  }
  void PushArgumentsLength() {
    fields_.push_back(StateValueDescriptor::ArgumentsLength());
  }
  void PushRestLength() {
    fields_.push_back(StateValueDescriptor::RestLength());
  }
  void PushDuplicate(size_t id) {
    fields_.push_back(StateValueDescriptor::Duplicate(id));
  }
  void PushPlain(MachineType type) {
    fields_.push_back(StateValueDescriptor::Plain(type));
  }
  void PushOptimizedOut(size_t num = 1) {
    fields_.insert(fields_.end(), num, StateValueDescriptor::OptimizedOut());
  }
  void PushCachedSlice(const Slice& cached) {
    fields_.insert(fields_.end(), cached.start_position,
                   cached.start_position + cached.fields_count);
  }

  // Returns a Slice representing the (non-nested) fields in StateValueList from
  // values_start to  the current end position.
  Slice MakeSlice(size_t values_start) {
    DCHECK(!HasNestedFieldsAfter(values_start));
    size_t fields_count = fields_.size() - values_start;
    return Slice(fields_.begin() + values_start, fields_count);
  }

  iterator begin() { return iterator(fields_.begin(), nested_.begin()); }
  iterator end() { return iterator(fields_.end(), nested_.end()); }

 private:
  bool HasNestedFieldsAfter(size_t values_start) {
    auto it = fields_.begin() + values_start;
    for (; it != fields_.end(); it++) {
      if (it->IsNested()) return true;
    }
    return false;
  }

  ZoneVector<StateValueDescriptor> fields_;
  ZoneVector<StateValueList*> nested_;
};

class FrameStateDescriptor : public ZoneObject {
 public:
  FrameStateDescriptor(
      Zone* zone, FrameStateType type, BytecodeOffset bailout_id,
      OutputFrameStateCombine state_combine, uint16_t parameters_count,
      uint16_t max_arguments, size_t locals_count, size_t stack_count,
      MaybeHandle<SharedFunctionInfo> shared_info,
      FrameStateDescriptor* outer_state = nullptr,
      uint32_t wasm_liftoff_frame_size = std::numeric_limits<uint32_t>::max(),
      uint32_t wasm_function_index = std::numeric_limits<uint32_t>::max());

  FrameStateType type() const { return type_; }
  BytecodeOffset bailout_id() const { return bailout_id_; }
  OutputFrameStateCombine state_combine() const { return frame_state_combine_; }
  uint16_t parameters_count() const { return parameters_count_; }
  uint16_t max_arguments() const { return max_arguments_; }
  size_t locals_count() const { return locals_count_; }
  size_t stack_count() const { return stack_count_; }
  MaybeHandle<SharedFunctionInfo> shared_info() const { return shared_info_; }
  FrameStateDescriptor* outer_state() const { return outer_state_; }
  bool HasClosure() const {
    return
#if V8_ENABLE_WEBASSEMBLY
        type_ != FrameStateType::kLiftoffFunction &&
#endif
        type_ != FrameStateType::kConstructInvokeStub;
  }
  bool HasContext() const {
    return FrameStateFunctionInfo::IsJSFunctionType(type_) ||
           type_ == FrameStateType::kBuiltinContinuation ||
#if V8_ENABLE_WEBASSEMBLY
           type_ == FrameStateType::kJSToWasmBuiltinContinuation ||
           // TODO(mliedtke): Should we skip the context for the FrameState of
           // inlined wasm functions?
           type_ == FrameStateType::kWasmInlinedIntoJS ||
#endif  // V8_ENABLE_WEBASSEMBLY
           type_ == FrameStateType::kConstructCreateStub ||
           type_ == FrameStateType::kConstructInvokeStub;
  }

  // The frame height on the stack, in number of slots, as serialized into a
  // Translation and later used by the deoptimizer. Does *not* include
  // information from the chain of outer states. Unlike |GetSize| this does not
  // always include parameters, locals, and stack slots; instead, the returned
  // slot kinds depend on the frame type.
  size_t GetHeight() const;

  // Returns an overapproximation of the unoptimized stack frame size in bytes,
  // as later produced by the deoptimizer. Considers both this and the chain of
  // outer states.
  size_t total_conservative_frame_size_in_bytes() const {
    return total_conservative_frame_size_in_bytes_;
  }

  size_t GetSize() const;
  size_t GetTotalSize() const;
  size_t GetFrameCount() const;
  size_t GetJSFrameCount() const;

  uint32_t GetWasmFunctionIndex() const {
    DCHECK(wasm_function_index_ != std::numeric_limits<uint32_t>::max());
    return wasm_function_index_;
  }

  StateValueList* GetStateValueDescriptors() { return &values_; }

  static const int kImpossibleValue = 0xdead;

 private:
  FrameStateType type_;
  BytecodeOffset bailout_id_;
  OutputFrameStateCombine frame_state_combine_;
  const uint16_t parameters_count_;
  const uint16_t max_arguments_;
  const size_t locals_count_;
  const size_t stack_count_;
  const size_t total_conservative_frame_size_in_bytes_;
  StateValueList values_;
  MaybeHandle<SharedFunctionInfo> const shared_info_;
  FrameStateDescriptor* const outer_state_;
  uint32_t wasm_function_index_;
};

#if V8_ENABLE_WEBASSEMBLY
class JSToWasmFrameStateDescriptor : public FrameStateDescriptor {
 public:
  JSToWasmFrameStateDescriptor(Zone* zone, FrameStateType type,
                               BytecodeOffset bailout_id,
                               OutputFrameStateCombine state_combine,
                               uint16_t parameters_count, size_t locals_count,
                               size_t stack_count,
                               MaybeHandle<SharedFunctionInfo> shared_info,
                               FrameStateDescriptor* outer_state,
                               const wasm::FunctionSig* wasm_signature);

  std::optional<wasm::ValueKind> return_kind() const { return return_kind_; }

 private:
  std::optional<wasm::ValueKind> return_kind_;
};
#endif  // V8_ENABLE_WEBASSEMBLY

// A deoptimization entry is a pair of the reason why we deoptimize and the
// frame state descriptor that we have to go back to.
class DeoptimizationEntry final {
 public:
  DeoptimizationEntry(FrameStateDescriptor* descriptor, DeoptimizeKind kind,
                      DeoptimizeReason reason, NodeId node_id,
                      FeedbackSource const& feedback)
      : descriptor_(descriptor),
        kind_(kind),
        reason_(reason),
#ifdef DEBUG
        node_id_(node_id),
#endif  // DEBUG
        feedback_(feedback) {
    USE(node_id);
  }

  FrameStateDescriptor* descriptor() const { return descriptor_; }
  DeoptimizeKind kind() const { return kind_; }
  DeoptimizeReason reason() const { return reason_; }
#ifdef DEBUG
  NodeId node_id() const { return node_id_; }
#endif  // DEBUG
  FeedbackSource const& feedback() const { return feedback_; }

 private:
  FrameStateDescriptor* const descriptor_;
  const DeoptimizeKind kind_;
  const DeoptimizeReason reason_;
#ifdef DEBUG
  const NodeId node_id_;
#endif  // DEBUG
  const FeedbackSource feedback_;
};

using DeoptimizationVector = ZoneVector<DeoptimizationEntry>;

class V8_EXPORT_PRIVATE PhiInstruction final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  using Inputs = ZoneVector<InstructionOperand>;

  PhiInstruction(Zone* zone, int virtual_register, size_t input_count);

  void SetInput(size_t offset, int virtual_register);
  void RenameInput(size_t offset, int virtual_register);

  int virtual_register() const { return virtual_register_; }
  const IntVector& operands() const { return operands_; }

  // TODO(dcarney): this has no real business being here, since it's internal to
  // the register allocator, but putting it here was convenient.
  const InstructionOperand& output() const { return output_; }
  InstructionOperand& output() { return output_; }

 private:
  const int virtual_register_;
  InstructionOperand output_;
  IntVector operands_;
};

// Analogue of BasicBlock for Instructions instead of Nodes.
class V8_EXPORT_PRIVATE InstructionBlock final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  InstructionBlock(Zone* zone, RpoNumber rpo_number, RpoNumber loop_header,
                   RpoNumber loop_end, RpoNumber dominator, bool deferred,
                   bool handler);

  // Instruction indexes (used by the register allocator).
  int first_instruction_index() const {
    DCHECK_LE(0, code_start_);
    DCHECK_LT(0, code_end_);
    DCHECK_GE(code_end_, code_start_);
    return code_start_;
  }
  int last_instruction_index() const {
    DCHECK_LE(0, code_start_);
    DCHECK_LT(0, code_end_);
    DCHECK_GE(code_end_, code_start_);
    return code_end_ - 1;
  }

  int32_t code_start() const { return code_start_; }
  void set_code_start(int32_t start) { code_start_ = start; }

  int32_t code_end() const { return code_end_; }
  void set_code_end(int32_t end) { code_end_ = end; }

  bool IsDeferred() const { return deferred_; }
  bool IsHandler() const { return handler_; }
  void MarkHandler() { handler_ = true; }
  void UnmarkHandler() { handler_ = false; }

  RpoNumber ao_number() const { return ao_number_; }
  RpoNumber rpo_number() const { return rpo_number_; }
  RpoNumber loop_header() const { return loop_header_; }
  RpoNumber loop_end() const {
    DCHECK(IsLoopHeader());
    return loop_end_;
  }
  inline bool IsLoopHeader() const { return loop_end_.IsValid(); }
  inline bool IsSwitchTarget() const { return switch_target_; }
  inline bool ShouldAlignCodeTarget() const { return code_target_alignment_; }
  inline bool ShouldAlignLoopHeader() const { return loop_header_alignment_; }
  inline bool IsLoopHeaderInAssemblyOrder() const {
    return loop_header_alignment_;
  }
  bool omitted_by_jump_threading() const { return omitted_by_jump_threading_; }
  void set_omitted_by_jump_threading() { omitted_by_jump_threading_ = true; }

  using Predecessors = ZoneVector<RpoNumber>;
  Predecessors& predecessors() { return predecessors_; }
  const Predecessors& predecessors() const { return predecessors_; }
  size_t PredecessorCount() const { return predecessors_.size(); }
  size_t PredecessorIndexOf(RpoNumber rpo_number) const;

  using Successors = ZoneVector<RpoNumber>;
  Successors& successors() { return successors_; }
  const Successors& successors() const { return successors_; }
  size_t SuccessorCount() const { return successors_.size(); }

  RpoNumber dominator() const { return dominator_; }
  void set_dominator(RpoNumber dominator) { dominator_ = dominator; }

  using PhiInstructions = ZoneVector<PhiInstruction*>;
  const PhiInstructions& phis() const { return phis_; }
  PhiInstruction* PhiAt(size_t i) const { return phis_[i]; }
  void AddPhi(PhiInstruction* phi) { phis_.push_back(phi); }

  void set_ao_number(RpoNumber ao_number) { ao_number_ = ao_number; }

  void set_code_target_alignment(bool val) { code_target_alignment_ = val; }
  void set_loop_header_alignment(bool val) { loop_header_alignment_ = val; }

  void set_switch_target(bool val) { switch_target_ = val; }

  bool needs_frame() const { return needs_frame_; }
  void mark_needs_frame() { needs_frame_ = true; }

  bool must_construct_frame() const { return must_construct_frame_; }
  void mark_must_construct_frame() { must_construct_frame_ = true; }

  bool must_deconstruct_frame() const { return must_deconstruct_frame_; }
  void mark_must_deconstruct_frame() { must_deconstruct_frame_ = true; }
  void clear_must_deconstruct_frame() { must_deconstruct_frame_ = false; }

 private:
  Successors successors_;
  Predecessors predecessors_;
  PhiInstructions phis_;
  RpoNumber ao_number_;  // Assembly order number.
  const RpoNumber rpo_number_;
  const RpoNumber loop_header_;
  const RpoNumber loop_end_;
  RpoNumber dominator_;
  int32_t code_start_;       // start index of arch-specific code.
  int32_t code_end_ = -1;    // end index of arch-specific code.
  const bool deferred_ : 1;  // Block contains deferred code.
  bool handler_ : 1;         // Block is a handler entry point.
  bool switch_target_ : 1;
  bool code_target_alignment_ : 1;  // insert code target alignment before this
                                    // block
  bool loop_header_alignment_ : 1;  // insert loop header alignment before this
                                    // block
  bool needs_frame_ : 1;
  bool must_construct_frame_ : 1;
  bool must_deconstruct_frame_ : 1;
  bool omitted_by_jump_threading_ : 1;  // Just for cleaner code comments.
};

class InstructionSequence;

struct PrintableInstructionBlock {
  const InstructionBlock* block_;
  const InstructionSequence* code_;
};

std::ostream& operator<<(std::ostream&, const PrintableInstructionBlock&);

using ConstantMap = ZoneUnorderedMap</* virtual register */ int, Constant>;
using Instructions = ZoneVector<Instruction*>;
using ReferenceMaps = ZoneVector<ReferenceMap*>;
using InstructionBlocks = ZoneVector<InstructionBlock*>;

// Represents architecture-specific generated code before, during, and after
// register allocation.
class V8_EXPORT_PRIVATE InstructionSequence final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  static InstructionBlocks* InstructionBlocksFor(Zone* zone,
                                                 const Schedule* schedule);
  static InstructionBlocks* InstructionBlocksFor(
      Zone* zone, const turboshaft::Graph& graph);
  InstructionSequence(Isolate* isolate, Zone* zone,
                      InstructionBlocks* instruction_blocks);
  InstructionSequence(const InstructionSequence&) = delete;
  InstructionSequence& operator=(const InstructionSequence&) = delete;

  int NextVirtualRegister();
  int VirtualRegisterCount() const { return next_virtual_register_; }

  const InstructionBlocks& instruction_blocks() const {
    return *instruction_blocks_;
  }

  const InstructionBlocks& ao_blocks() const { return *ao_blocks_; }

  int InstructionBlockCount() const {
    return static_cast<int>(instruction_blocks_->size());
  }

  InstructionBlock* InstructionBlockAt(RpoNumber rpo_number) {
    return instruction_blocks_->at(rpo_number.ToSize());
  }

  int LastLoopInstructionIndex(const InstructionBlock* block) {
    return instruction_blocks_->at(block->loop_end().ToSize() - 1)
        ->last_instruction_index();
  }

  const InstructionBlock* InstructionBlockAt(RpoNumber rpo_number) const {
    return instruction_blocks_->at(rpo_number.ToSize());
  }

  InstructionBlock* GetInstructionBlock(int instruction_index) const {
    return instructions()[instruction_index]->block();
  }

  static MachineRepresentation DefaultRepresentation() {
    return MachineType::PointerRepresentation();
  }
  MachineRepresentation GetRepresentation(int virtual_register) const;
  void MarkAsRepresentation(MachineRepresentation rep, int virtual_register);

  bool IsReference(int virtual_register) const {
    return CanBeTaggedOrCompressedPointer(GetRepresentation(virtual_register));
  }
  bool IsFP(int virtual_register) const {
    return IsFloatingPoint(GetRepresentation(virtual_register));
  }
  int representation_mask() const { return representation_mask_; }
  bool HasFPVirtualRegisters() const {
    constexpr int kFPRepMask =
        RepresentationBit(MachineRepresentation::kFloat16) |
        RepresentationBit(MachineRepresentation::kFloat32) |
        RepresentationBit(MachineRepresentation::kFloat64) |
        RepresentationBit(MachineRepresentation::kSimd128) |
        RepresentationBit(MachineRepresentation::kSimd256);
    return (representation_mask() & kFPRepMask) != 0;
  }

  bool HasSimd128VirtualRegisters() const {
    constexpr int kSimd128RepMask =
        RepresentationBit(MachineRepresentation::kSimd128);
    return (representation_mask() & kSimd128RepMask) != 0;
  }

  Instruction* GetBlockStart(RpoNumber rpo) const;

  using const_iterator = Instructions::const_iterator;
  const_iterator begin() const { return instructions_.begin(); }
  const_iterator end() const { return instructions_.end(); }
  const Instructions& instructions() const { return instructions_; }
  int LastInstructionIndex() const {
    return static_cast<int>(instructions().size()) - 1;
  }

  Instruction* InstructionAt(int index) const {
    DCHECK_LE(0, index);
    DCHECK_GT(instructions_.size(), index);
    return instructions_[index];
  }

  Isolate* isolate() const { return isolate_; }
  const ReferenceMaps* reference_maps() const { return &reference_maps_; }
  Zone* zone() const { return zone_; }

  // Used by the instruction selector while adding instructions.
  int AddInstruction(Instruction* instr);
  void StartBlock(RpoNumber rpo);
  void EndBlock(RpoNumber rpo);

  void AddConstant(int virtual_register, Constant constant) {
    // TODO(titzer): allow RPO numbers as constants?
    DCHECK_NE(Constant::kRpoNumber, constant.type());
    DCHECK(virtual_register >= 0 && virtual_register < next_virtual_register_);
    DCHECK(constants_.find(virtual_register) == constants_.end());
    constants_.emplace(virtual_register, constant);
  }
  Constant GetConstant(int virtual_register) const {
    auto it = constants_.find(virtual_register);
    DCHECK(it != constants_.end());
    DCHECK_EQ(virtual_register, it->first);
    return it->second;
  }

  using Immediates = ZoneVector<Constant>;
  Immediates& immediates() { return immediates_; }

  using RpoImmediates = ZoneVector<RpoNumber>;
  RpoImmediates& rpo_immediates() { return rpo_immediates_; }

  ImmediateOperand AddImmediate(const Constant& constant) {
    if (RelocInfo::IsNoInfo(constant.rmode())) {
      if (constant.type() == Constant::kRpoNumber) {
        // Ideally we would inline RPO numbers into the operand, however jump-
        // threading modifies RPO values and so we indirect through a vector
        // of rpo_immediates to enable rewriting. We keep this seperate from the
        // immediates vector so that we don't repeatedly push the same rpo
        // number.
        RpoNumber rpo_number = constant.ToRpoNumber();
        DCHECK(!rpo_immediates().at(rpo_number.ToSize()).IsValid() ||
               rpo_immediates().at(rpo_number.ToSize()) == rpo_number);
        rpo_immediates()[rpo_number.ToSize()] = rpo_number;
        return ImmediateOperand(ImmediateOperand::INDEXED_RPO,
                                rpo_number.ToInt());
      } else if (constant.type() == Constant::kInt32) {
        return ImmediateOperand(ImmediateOperand::INLINE_INT32,
                                constant.ToInt32());
      } else if (constant.type() == Constant::kInt64 &&
                 constant.FitsInInt32()) {
        return ImmediateOperand(ImmediateOperand::INLINE_INT64,
                                constant.ToInt32());
      }
    }
    int index = static_cast<int>(immediates_.size());
    immediates_.push_back(constant);
    return ImmediateOperand(ImmediateOperand::INDEXED_IMM, index);
  }

  Constant GetImmediate(const ImmediateOperand* op) const {
    switch (op->type()) {
      case ImmediateOperand::INLINE_INT32:
        return Constant(op->inline_int32_value());
      case ImmediateOperand::INLINE_INT64:
        return Constant(op->inline_int64_value());
      case ImmediateOperand::INDEXED_RPO: {
        int index = op->indexed_value();
        DCHECK_LE(0, index);
        DCHECK_GT(rpo_immediates_.size(), index);
        return Constant(rpo_immediates_[index]);
      }
      case ImmediateOperand::INDEXED_IMM: {
        int index = op->indexed_value();
        DCHECK_LE(0, index);
        DCHECK_GT(immediates_.size(), index);
        return immediates_[index];
      }
    }
    UNREACHABLE();
  }

  int AddDeoptimizationEntry(FrameStateDescriptor* descriptor,
                             DeoptimizeKind kind, DeoptimizeReason reason,
                             NodeId node_id, FeedbackSource const& feedback);
  DeoptimizationEntry const& GetDeoptimizationEntry(int deoptimization_id);
  int GetDeoptimizationEntryCount() const {
    return static_cast<int>(deoptimization_entries_.size());
  }

  RpoNumber InputRpo(Instruction* instr, size_t index);

  bool GetSourcePosition(const Instruction* instr,
                         SourcePosition* result) const;
  void SetSourcePosition(const Instruction* instr, SourcePosition value);

  bool ContainsCall() const {
    for (Instruction* instr : instructions_) {
      if (instr->IsCall()) return true;
    }
    return false;
  }

  // APIs to aid debugging. For general-stream APIs, use operator<<.
  void Print() const;

  void PrintBlock(int block_id) const;

  void ValidateEdgeSplitForm() const;
  void ValidateDeferredBlockExitPaths() const;
  void ValidateDeferredBlockEntryPaths() const;
  void ValidateSSA() const;

  static void SetRegisterConfigurationForTesting(
      const RegisterConfiguration* regConfig);
  static void ClearRegisterConfigurationForTesting();

  void RecomputeAssemblyOrderForTesting();

  void IncreaseRpoForTesting(size_t rpo_count) {
    DCHECK_GE(rpo_count, rpo_immediates().size());
    rpo_immediates().resize(rpo_count);
  }

 private:
  friend V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&,
                                                    const InstructionSequence&);

  using SourcePositionMap =
      ZoneAbslFlatHashMap<const Instruction*, SourcePosition>;

  static const RegisterConfiguration* RegisterConfigurationForTesting();
  static const RegisterConfiguration* registerConfigurationForTesting_;

  // Puts the deferred blocks last and may rotate loops.
  void ComputeAssemblyOrder();

  Isolate* isolate_;
  Zone* const zone_;
  InstructionBlocks* const instruction_blocks_;
  InstructionBlocks* ao_blocks_;
  SourcePositionMap source_positions_;
  ConstantMap constants_;
  Immediates immediates_;
  RpoImmediates rpo_immediates_;
  Instructions instructions_;
  int next_virtual_register_;
  ReferenceMaps reference_maps_;
  ZoneVector<MachineRepresentation> representations_;
  int representation_mask_;
  DeoptimizationVector deoptimization_entries_;

  // Used at construction time
  InstructionBlock* current_block_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&,
                                           const InstructionSequence&);
#undef INSTRUCTION_OPERAND_ALIGN

// Constants for accessing ConditionalCompare data, shared between isel and
// codegen.
constexpr size_t kNumCcmpOperands = 5;
constexpr size_t kCcmpOffsetOfOpcode = 0;
constexpr size_t kCcmpOffsetOfLhs = 1;
constexpr size_t kCcmpOffsetOfRhs = 2;
constexpr size_t kCcmpOffsetOfDefaultFlags = 3;
constexpr size_t kCcmpOffsetOfCompareCondition = 4;
constexpr size_t kConditionalSetEndOffsetOfNumCcmps = 1;
constexpr size_t kConditionalSetEndOffsetOfCondition = 2;
constexpr size_t kBranchEndOffsetOfFalseBlock = 1;
constexpr size_t kBranchEndOffsetOfTrueBlock = 2;
constexpr size_t kConditionalBranchEndOffsetOfNumCcmps = 3;
constexpr size_t kConditionalBranchEndOffsetOfCondition = 4;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_H_
