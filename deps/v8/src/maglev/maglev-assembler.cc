// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-assembler.h"

#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-code-generator.h"
#include "src/numbers/conversions.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::AllocateHeapNumber(RegisterSnapshot register_snapshot,
                                         Register result,
                                         DoubleRegister value) {
  // In the case we need to call the runtime, we should spill the value
  // register. Even if it is not live in the next node, otherwise the
  // allocation call might trash it.
  register_snapshot.live_double_registers.set(value);
  Allocate(register_snapshot, result, sizeof(HeapNumber));
  SetMapAsRoot(result, RootIndex::kHeapNumberMap);
  StoreFloat64(FieldMemOperand(result, offsetof(HeapNumber, value_)), value);
}

void MaglevAssembler::AllocateTwoByteString(RegisterSnapshot register_snapshot,
                                            Register result, int length) {
  int size = SeqTwoByteString::SizeFor(length);
  Allocate(register_snapshot, result, size);
  StoreTaggedSignedField(result, size - kObjectAlignment, Smi::zero());
  SetMapAsRoot(result, RootIndex::kSeqTwoByteStringMap);
  StoreInt32Field(result, offsetof(Name, raw_hash_field_),
                  Name::kEmptyHashField);
  StoreInt32Field(result, offsetof(String, length_), length);
}

Register MaglevAssembler::FromAnyToRegister(const Input& input,
                                            Register scratch) {
  if (input.operand().IsConstant()) {
    input.node()->LoadToRegister(this, scratch);
    return scratch;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return ToRegister(input);
  } else {
    DCHECK(operand.IsStackSlot());
    Move(scratch, ToMemOperand(input));
    return scratch;
  }
}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                int char_code) {
  DCHECK_GE(char_code, 0);
  DCHECK_LT(char_code, String::kMaxOneByteCharCode);
  Register table = result;
  LoadRoot(table, RootIndex::kSingleCharacterStringTable);
  LoadTaggedField(result, table,
                  FixedArray::kHeaderSize + char_code * kTaggedSize);
}

void MaglevAssembler::LoadDataField(const PolymorphicAccessInfo& access_info,
                                    Register result, Register object,
                                    Register scratch) {
  Register load_source = object;
  // Resolve property holder.
  if (access_info.holder().has_value()) {
    load_source = scratch;
    Move(load_source, access_info.holder().value().object());
  }
  FieldIndex field_index = access_info.field_index();
  if (!field_index.is_inobject()) {
    Register load_source_object = load_source;
    if (load_source == object) {
      load_source = scratch;
    }
    // The field is in the property array, first load it from there.
    AssertNotSmi(load_source_object);
    LoadTaggedField(load_source, load_source_object,
                    JSReceiver::kPropertiesOrHashOffset);
  }
  AssertNotSmi(load_source);
  LoadTaggedField(result, load_source, field_index.offset());
}

void MaglevAssembler::JumpIfNotUndetectable(Register object, Register scratch,
                                            CheckType check_type, Label* target,
                                            Label::Distance distance) {
  if (check_type == CheckType::kCheckHeapObject) {
    JumpIfSmi(object, target, distance);
  } else if (v8_flags.debug_code) {
    AssertNotSmi(object);
  }
  // For heap objects, check the map's undetectable bit.
  LoadMap(scratch, object);
  LoadByte(scratch, FieldMemOperand(scratch, Map::kBitFieldOffset));
  TestInt32AndJumpIfAllClear(scratch, Map::Bits1::IsUndetectableBit::kMask,
                             target, distance);
}

void MaglevAssembler::JumpIfUndetectable(Register object, Register scratch,
                                         CheckType check_type, Label* target,
                                         Label::Distance distance) {
  Label detectable;
  if (check_type == CheckType::kCheckHeapObject) {
    JumpIfSmi(object, &detectable, Label::kNear);
  } else if (v8_flags.debug_code) {
    AssertNotSmi(object);
  }
  // For heap objects, check the map's undetectable bit.
  LoadMap(scratch, object);
  LoadByte(scratch, FieldMemOperand(scratch, Map::kBitFieldOffset));
  TestInt32AndJumpIfAnySet(scratch, Map::Bits1::IsUndetectableBit::kMask,
                           target, distance);
  bind(&detectable);
}

void MaglevAssembler::EnsureWritableFastElements(
    RegisterSnapshot register_snapshot, Register elements, Register object,
    Register scratch) {
  ZoneLabelRef done(this);
  CompareMapWithRoot(elements, RootIndex::kFixedArrayMap, scratch);
  JumpToDeferredIf(
      kNotEqual,
      [](MaglevAssembler* masm, ZoneLabelRef done, Register object,
         Register result_reg, RegisterSnapshot snapshot) {
        {
          snapshot.live_registers.clear(result_reg);
          snapshot.live_tagged_registers.clear(result_reg);
          SaveRegisterStateForCall save_register_state(masm, snapshot);
          __ CallBuiltin<Builtin::kCopyFastSmiOrObjectElements>(object);
          save_register_state.DefineSafepoint();
          __ Move(result_reg, kReturnRegister0);
        }
        __ Jump(*done);
      },
      done, object, elements, register_snapshot);
  bind(*done);
}

void MaglevAssembler::ToBoolean(Register value, CheckType check_type,
                                ZoneLabelRef is_true, ZoneLabelRef is_false,
                                bool fallthrough_when_true) {
  ScratchRegisterScope temps(this);
  Register map = temps.GetDefaultScratchRegister();

  if (check_type == CheckType::kCheckHeapObject) {
    // Check if {{value}} is Smi.
    Condition is_smi = CheckSmi(value);
    JumpToDeferredIf(
        is_smi,
        [](MaglevAssembler* masm, Register value, ZoneLabelRef is_true,
           ZoneLabelRef is_false) {
          // Check if {value} is not zero.
          __ CompareSmiAndJumpIf(value, Smi::FromInt(0), kEqual, *is_false);
          __ Jump(*is_true);
        },
        value, is_true, is_false);
  } else if (v8_flags.debug_code) {
    AssertNotSmi(value);
  }

#if V8_STATIC_ROOTS_BOOL
  // Check if {{value}} is a falsey root or the true value.
  // Undefined is the first root, so it's the smallest possible pointer
  // value, which means we don't have to subtract it for the range check.
  ReadOnlyRoots roots(isolate_);
  static_assert(StaticReadOnlyRoot::kUndefinedValue + sizeof(Undefined) ==
                StaticReadOnlyRoot::kNullValue);
  static_assert(StaticReadOnlyRoot::kNullValue + sizeof(Null) ==
                StaticReadOnlyRoot::kempty_string);
  static_assert(StaticReadOnlyRoot::kempty_string +
                    SeqOneByteString::SizeFor(0) ==
                StaticReadOnlyRoot::kFalseValue);
  static_assert(StaticReadOnlyRoot::kFalseValue + sizeof(False) ==
                StaticReadOnlyRoot::kTrueValue);
  CompareInt32AndJumpIf(value, StaticReadOnlyRoot::kTrueValue,
                        kUnsignedLessThan, *is_false);
  // Reuse the condition flags from the above int32 compare to also check for
  // the true value itself.
  JumpIf(kEqual, *is_true);
#else
  // Check if {{value}} is false.
  JumpIfRoot(value, RootIndex::kFalseValue, *is_false);

  // Check if {{value}} is true.
  JumpIfRoot(value, RootIndex::kTrueValue, *is_true);

  // Check if {{value}} is empty string.
  JumpIfRoot(value, RootIndex::kempty_string, *is_false);

  // Only check null and undefined if we're not going to check the
  // undetectable bit.
  if (compilation_info()
          ->broker()
          ->dependencies()
          ->DependOnNoUndetectableObjectsProtector()) {
    // Check if {{value}} is undefined.
    JumpIfRoot(value, RootIndex::kUndefinedValue, *is_false);

    // Check if {{value}} is null.
    JumpIfRoot(value, RootIndex::kNullValue, *is_false);
  }
#endif

  LoadMap(map, value);

  if (!compilation_info()
           ->broker()
           ->dependencies()
           ->DependOnNoUndetectableObjectsProtector()) {
    // Check if {{value}} is undetectable.
    TestInt32AndJumpIfAnySet(FieldMemOperand(map, Map::kBitFieldOffset),
                             Map::Bits1::IsUndetectableBit::kMask, *is_false);
  }

  // Check if {{value}} is a HeapNumber.
  JumpIfRoot(map, RootIndex::kHeapNumberMap,
             MakeDeferredCode(
                 [](MaglevAssembler* masm, Register value, ZoneLabelRef is_true,
                    ZoneLabelRef is_false) {
                   __ CompareDoubleAndJumpIfZeroOrNaN(
                       FieldMemOperand(value, offsetof(HeapNumber, value_)),
                       *is_false);
                   __ Jump(*is_true);
                 },
                 value, is_true, is_false));

  // Check if {{value}} is a BigInt.
  // {{map}} is not needed after this check, we pass to the deferred code, so it
  // can be added to the temporary registers.
  JumpIfRoot(map, RootIndex::kBigIntMap,
             MakeDeferredCode(
                 [](MaglevAssembler* masm, Register value, Register map,
                    ZoneLabelRef is_true, ZoneLabelRef is_false) {
                   ScratchRegisterScope temps(masm);
                   temps.Include(map);
                   __ TestInt32AndJumpIfAllClear(
                       FieldMemOperand(value, offsetof(BigInt, bitfield_)),
                       BigInt::LengthBits::kMask, *is_false);
                   __ Jump(*is_true);
                 },
                 value, map, is_true, is_false));
  // Otherwise true.
  if (!fallthrough_when_true) {
    Jump(*is_true);
  }
}

void MaglevAssembler::MaterialiseValueNode(Register dst, ValueNode* value) {
  switch (value->opcode()) {
    case Opcode::kInt32Constant: {
      int32_t int_value = value->Cast<Int32Constant>()->value();
      if (Smi::IsValid(int_value)) {
        Move(dst, Smi::FromInt(int_value));
      } else {
        MoveHeapNumber(dst, int_value);
      }
      return;
    }
    case Opcode::kFloat64Constant: {
      double double_value =
          value->Cast<Float64Constant>()->value().get_scalar();
      int smi_value;
      if (DoubleToSmiInteger(double_value, &smi_value)) {
        Move(dst, Smi::FromInt(smi_value));
      } else {
        MoveHeapNumber(dst, double_value);
      }
      return;
    }
    default:
      break;
  }
  DCHECK(!value->allocation().IsConstant());
  DCHECK(value->allocation().IsAnyStackSlot());
  using D = NewHeapNumberDescriptor;
  DoubleRegister builtin_input_value = D::GetDoubleRegisterParameter(D::kValue);
  MemOperand src = ToMemOperand(value->allocation());
  switch (value->properties().value_representation()) {
    case ValueRepresentation::kInt32: {
      Label done;
      ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      Move(scratch, src);
      SmiTagInt32AndJumpIfSuccess(dst, scratch, &done, Label::kNear);
      // If smi tagging fails, instead of bailing out (deopting), we change
      // representation to a HeapNumber.
      Int32ToDouble(builtin_input_value, scratch);
      CallBuiltin<Builtin::kNewHeapNumber>(builtin_input_value);
      Move(dst, kReturnRegister0);
      bind(&done);
      break;
    }
    case ValueRepresentation::kUint32: {
      Label done;
      ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      Move(scratch, src);
      SmiTagUint32AndJumpIfSuccess(dst, scratch, &done, Label::kNear);
      // If smi tagging fails, instead of bailing out (deopting), we change
      // representation to a HeapNumber.
      Uint32ToDouble(builtin_input_value, scratch);
      CallBuiltin<Builtin::kNewHeapNumber>(builtin_input_value);
      Move(dst, kReturnRegister0);
      bind(&done);
      break;
    }
    case ValueRepresentation::kFloat64:
      LoadFloat64(builtin_input_value, src);
      CallBuiltin<Builtin::kNewHeapNumber>(builtin_input_value);
      Move(dst, kReturnRegister0);
      break;
    case ValueRepresentation::kHoleyFloat64: {
      Label done, box;
      JumpIfNotHoleNan(src, &box, Label::kNear);
      LoadRoot(dst, RootIndex::kUndefinedValue);
      Jump(&done);
      bind(&box);
      LoadFloat64(builtin_input_value, src);
      CallBuiltin<Builtin::kNewHeapNumber>(builtin_input_value);
      Move(dst, kReturnRegister0);
      bind(&done);
      break;
    }
    case ValueRepresentation::kIntPtr:
    case ValueRepresentation::kTagged:
      UNREACHABLE();
  }
}

void MaglevAssembler::TestTypeOf(
    Register object, interpreter::TestTypeOfFlags::LiteralFlag literal,
    Label* is_true, Label::Distance true_distance, bool fallthrough_when_true,
    Label* is_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
  // If both true and false are fallthroughs, we don't have to do anything.
  if (fallthrough_when_true && fallthrough_when_false) return;

  // IMPORTANT: Note that `object` could be a register that aliases registers in
  // the ScratchRegisterScope. Make sure that all reads of `object` are before
  // any writes to scratch registers
  using LiteralFlag = interpreter::TestTypeOfFlags::LiteralFlag;
  switch (literal) {
    case LiteralFlag::kNumber: {
      MaglevAssembler::ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      JumpIfSmi(object, is_true, true_distance);
      CompareMapWithRoot(object, RootIndex::kHeapNumberMap, scratch);
      Branch(kEqual, is_true, true_distance, fallthrough_when_true, is_false,
             false_distance, fallthrough_when_false);
      return;
    }
    case LiteralFlag::kString: {
      JumpIfSmi(object, is_false, false_distance);
      CheckJSAnyIsStringAndBranch(object, is_true, true_distance,
                                  fallthrough_when_true, is_false,
                                  false_distance, fallthrough_when_false);
      return;
    }
    case LiteralFlag::kSymbol: {
      JumpIfSmi(object, is_false, false_distance);
      CompareObjectTypeAndBranch(object, SYMBOL_TYPE, kEqual, is_true,
                                 true_distance, fallthrough_when_true, is_false,
                                 false_distance, fallthrough_when_false);
      return;
    }
    case LiteralFlag::kBoolean:
      JumpIfRoot(object, RootIndex::kTrueValue, is_true, true_distance);
      CompareRoot(object, RootIndex::kFalseValue);
      Branch(kEqual, is_true, true_distance, fallthrough_when_true, is_false,
             false_distance, fallthrough_when_false);
      return;
    case LiteralFlag::kBigInt: {
      JumpIfSmi(object, is_false, false_distance);
      CompareObjectTypeAndBranch(object, BIGINT_TYPE, kEqual, is_true,
                                 true_distance, fallthrough_when_true, is_false,
                                 false_distance, fallthrough_when_false);
      return;
    }
    case LiteralFlag::kUndefined: {
      MaglevAssembler::ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      // Make sure `object` isn't a valid temp here, since we re-use it.
      DCHECK(!temps.Available().has(object));
      JumpIfSmi(object, is_false, false_distance);
      // Check it has the undetectable bit set and it is not null.
      LoadMap(scratch, object);
      TestInt32AndJumpIfAllClear(FieldMemOperand(scratch, Map::kBitFieldOffset),
                                 Map::Bits1::IsUndetectableBit::kMask, is_false,
                                 false_distance);
      CompareRoot(object, RootIndex::kNullValue);
      Branch(kNotEqual, is_true, true_distance, fallthrough_when_true, is_false,
             false_distance, fallthrough_when_false);
      return;
    }
    case LiteralFlag::kFunction: {
      MaglevAssembler::ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      JumpIfSmi(object, is_false, false_distance);
      // Check if callable bit is set and not undetectable.
      LoadMap(scratch, object);
      Branch(IsCallableAndNotUndetectable(scratch, scratch), is_true,
             true_distance, fallthrough_when_true, is_false, false_distance,
             fallthrough_when_false);
      return;
    }
    case LiteralFlag::kObject: {
      MaglevAssembler::ScratchRegisterScope temps(this);
      Register scratch = temps.GetDefaultScratchRegister();
      JumpIfSmi(object, is_false, false_distance);
      // If the object is null then return true.
      JumpIfRoot(object, RootIndex::kNullValue, is_true, true_distance);
      // Check if the object is a receiver type,
      LoadMap(scratch, object);
      CompareInstanceType(scratch, FIRST_JS_RECEIVER_TYPE);
      JumpIf(kLessThan, is_false, false_distance);
      // ... and is not undefined (undetectable) nor callable.
      Branch(IsNotCallableNorUndetactable(scratch, scratch), is_true,
             true_distance, fallthrough_when_true, is_false, false_distance,
             fallthrough_when_false);
      return;
    }
    case LiteralFlag::kOther:
      if (!fallthrough_when_false) {
        Jump(is_false, false_distance);
      }
      return;
  }
  UNREACHABLE();
}

template <MaglevAssembler::StoreMode store_mode>
void MaglevAssembler::CheckAndEmitDeferredWriteBarrier(
    Register object, OffsetTypeFor<store_mode> offset, Register value,
    RegisterSnapshot register_snapshot, ValueIsCompressed value_is_compressed,
    ValueCanBeSmi value_can_be_smi) {
  ZoneLabelRef done(this);
  Label* deferred_write_barrier = MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef done, Register object,
         OffsetTypeFor<store_mode> offset, Register value,
         RegisterSnapshot register_snapshot, ValueIsCompressed value_type) {
        ASM_CODE_COMMENT_STRING(masm, "Write barrier slow path");
        if (PointerCompressionIsEnabled() && value_type == kValueIsCompressed) {
          __ DecompressTagged(value, value);
        }

        {
          // Use the value as the scratch register if possible, since
          // CheckPageFlag emits slightly better code when value == scratch.
          MaglevAssembler::ScratchRegisterScope temp(masm);
          Register scratch = temp.GetDefaultScratchRegister();
          if (value != object && !register_snapshot.live_registers.has(value)) {
            scratch = value;
          }
          __ CheckPageFlag(value, scratch,
                           MemoryChunk::kPointersToHereAreInterestingMask,
                           kEqual, *done);
        }

        Register stub_object_reg = WriteBarrierDescriptor::ObjectRegister();
        Register slot_reg = WriteBarrierDescriptor::SlotAddressRegister();

        RegList saved;
        if (object != stub_object_reg &&
            register_snapshot.live_registers.has(stub_object_reg)) {
          saved.set(stub_object_reg);
        }
        if (register_snapshot.live_registers.has(slot_reg)) {
          saved.set(slot_reg);
        }

        __ PushAll(saved);

        if (object != stub_object_reg) {
          __ Move(stub_object_reg, object);
          object = stub_object_reg;
        }

        if constexpr (store_mode == kElement) {
          __ SetSlotAddressForFixedArrayElement(slot_reg, object, offset);
        } else {
          static_assert(store_mode == kField);
          __ SetSlotAddressForTaggedField(slot_reg, object, offset);
        }

        SaveFPRegsMode const save_fp_mode =
            !register_snapshot.live_double_registers.is_empty()
                ? SaveFPRegsMode::kSave
                : SaveFPRegsMode::kIgnore;

        __ CallRecordWriteStub(object, slot_reg, save_fp_mode);

        __ PopAll(saved);
        __ Jump(*done);
      },
      done, object, offset, value, register_snapshot, value_is_compressed);

  if (value_can_be_smi) {
    JumpIfSmi(value, *done);
  } else {
    AssertNotSmi(value);
  }

  MaglevAssembler::ScratchRegisterScope temp(this);
  Register scratch = temp.GetDefaultScratchRegister();
  CheckPageFlag(object, scratch,
                MemoryChunk::kPointersFromHereAreInterestingMask, kNotEqual,
                deferred_write_barrier);
  bind(*done);
}

void MaglevAssembler::StoreTaggedFieldWithWriteBarrier(
    Register object, int offset, Register value,
    RegisterSnapshot register_snapshot, ValueIsCompressed value_is_compressed,
    ValueCanBeSmi value_can_be_smi) {
  AssertNotSmi(object);
  StoreTaggedFieldNoWriteBarrier(object, offset, value);
  CheckAndEmitDeferredWriteBarrier<kField>(
      object, offset, value, register_snapshot, value_is_compressed,
      value_can_be_smi);
}

void MaglevAssembler::StoreFixedArrayElementWithWriteBarrier(
    Register array, Register index, Register value,
    RegisterSnapshot register_snapshot) {
  if (v8_flags.debug_code) {
    CompareObjectTypeAndAssert(array, FIXED_ARRAY_TYPE, kEqual,
                               AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  StoreFixedArrayElementNoWriteBarrier(array, index, value);
  CheckAndEmitDeferredWriteBarrier<kElement>(
      array, index, value, register_snapshot, kValueIsDecompressed,
      kValueCanBeSmi);
}

void MaglevAssembler::GenerateCheckConstTrackingLetCellFooter(Register context,
                                                              Register data,
                                                              int index,
                                                              Label* done) {
  // Load the const tracking let side data.
  LoadTaggedField(
      data, context,
      Context::OffsetOfElementAt(Context::CONST_TRACKING_LET_SIDE_DATA_INDEX));

  LoadTaggedField(data, data,
                  FixedArray::OffsetOfElementAt(
                      index - Context::MIN_CONTEXT_EXTENDED_SLOTS));

  // If the field is already marked as "not a constant", storing a
  // different value is fine. But if it's anything else (including the hole,
  // which means no value was stored yet), deopt this code. The lower tier code
  // will update the side data and invalidate DependentCode if needed.
  CompareTaggedAndJumpIf(data, ConstTrackingLetCell::kNonConstMarker, kEqual,
                         done, Label::kNear);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
