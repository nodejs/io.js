// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/interface-descriptors-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Allocate(RegisterSnapshot register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  DCHECK(allow_allocate());
  // TODO(victorgomes): Call the runtime for large object allocation.
  // TODO(victorgomes): Support double alignment.
  DCHECK_EQ(alignment, kTaggedAligned);
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);
  if (v8_flags.single_generation) {
    alloc_type = AllocationType::kOld;
  }
  bool in_new_space = alloc_type == AllocationType::kYoung;
  ExternalReference top =
      in_new_space
          ? ExternalReference::new_space_allocation_top_address(isolate_)
          : ExternalReference::old_space_allocation_top_address(isolate_);
  ExternalReference limit =
      in_new_space
          ? ExternalReference::new_space_allocation_limit_address(isolate_)
          : ExternalReference::old_space_allocation_limit_address(isolate_);

  ZoneLabelRef done(this);
  ScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  // We are a bit short on registers, so we use the same register for {object}
  // and {new_top}. Once we have defined {new_top}, we don't use {object} until
  // {new_top} is used for the last time. And there (at the end of this
  // function), we recover the original {object} from {new_top} by subtracting
  // {size_in_bytes}.
  Register new_top = object;
  // Check if there is enough space.
  Ldr(object, ExternalReferenceAsOperand(top, scratch));
  Add(new_top, object, size_in_bytes);
  Ldr(scratch, ExternalReferenceAsOperand(limit, scratch));
  Cmp(new_top, scratch);
  // Otherwise call runtime.
  JumpToDeferredIf(
      ge,
      [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
         Register object, Builtin builtin, int size_in_bytes,
         ZoneLabelRef done) {
        // Remove {object} from snapshot, since it is the returned allocated
        // HeapObject.
        register_snapshot.live_registers.clear(object);
        register_snapshot.live_tagged_registers.clear(object);
        {
          SaveRegisterStateForCall save_register_state(masm, register_snapshot);
          using D = AllocateDescriptor;
          __ Move(D::GetRegisterParameter(D::kRequestedSize), size_in_bytes);
          __ CallBuiltin(builtin);
          save_register_state.DefineSafepoint();
          __ Move(object, kReturnRegister0);
        }
        __ B(*done);
      },
      register_snapshot, object,
      in_new_space ? Builtin::kAllocateInYoungGeneration
                   : Builtin::kAllocateInOldGeneration,
      size_in_bytes, done);
  // Store new top and tag object.
  Move(ExternalReferenceAsOperand(top, scratch), new_top);
  Add(object, object, kHeapObjectTag - size_in_bytes);
  bind(*done);
}

void MaglevAssembler::OSRPrologue(Graph* graph) {
  DCHECK(graph->is_osr());
  CHECK(!graph->has_recursive_calls());

  uint32_t source_frame_size =
      graph->min_maglev_stackslots_for_unoptimized_frame_size();

  static_assert(StandardFrameConstants::kFixedSlotCount % 2 == 1);
  if (source_frame_size % 2 == 0) source_frame_size++;

  if (v8_flags.maglev_assert_stack_size && v8_flags.debug_code) {
    ScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Add(scratch, sp,
        source_frame_size * kSystemPointerSize +
            StandardFrameConstants::kFixedFrameSizeFromFp);
    Cmp(scratch, fp);
    Assert(eq, AbortReason::kOsrUnexpectedStackSize);
  }

  uint32_t target_frame_size =
      graph->tagged_stack_slots() + graph->untagged_stack_slots();
  CHECK_EQ(target_frame_size % 2, 1);
  CHECK_LE(source_frame_size, target_frame_size);
  if (source_frame_size < target_frame_size) {
    ASM_CODE_COMMENT_STRING(this, "Growing frame for OSR");
    uint32_t additional_tagged =
        source_frame_size < graph->tagged_stack_slots()
            ? graph->tagged_stack_slots() - source_frame_size
            : 0;
    uint32_t additional_tagged_double =
        additional_tagged / 2 + additional_tagged % 2;
    for (size_t i = 0; i < additional_tagged_double; ++i) {
      Push(xzr, xzr);
    }
    uint32_t size_so_far = source_frame_size + additional_tagged_double * 2;
    CHECK_LE(size_so_far, target_frame_size);
    if (size_so_far < target_frame_size) {
      Sub(sp, sp,
          Immediate((target_frame_size - size_so_far) * kSystemPointerSize));
    }
  }
}

void MaglevAssembler::Prologue(Graph* graph) {
  ScratchRegisterScope temps(this);
  //  We add two extra registers to the scope. Ideally we could add all the
  //  allocatable general registers, except Context, JSFunction, NewTarget and
  //  ArgCount. Unfortunately, OptimizeCodeOrTailCallOptimizedCodeSlot and
  //  LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing pick random registers and
  //  we could alias those.
  // TODO(victorgomes): Fix these builtins to either use the scope or pass the
  // used registers manually.
  temps.Include({x14, x15});

  DCHECK(!graph->is_osr());

  CallTarget();
  BailoutIfDeoptimized();

  if (graph->has_recursive_calls()) {
    BindCallTarget(code_gen_state()->entry_label());
  }

  // Tiering support.
  if (v8_flags.turbofan) {
    using D = MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor;
    Register flags = D::GetRegisterParameter(D::kFlags);
    Register feedback_vector = D::GetRegisterParameter(D::kFeedbackVector);
    DCHECK(!AreAliased(flags, feedback_vector, kJavaScriptCallArgCountRegister,
                       kJSFunctionRegister, kContextRegister,
                       kJavaScriptCallNewTargetRegister));
    DCHECK(!temps.Available().has(flags));
    DCHECK(!temps.Available().has(feedback_vector));
    Move(feedback_vector,
         compilation_info()->toplevel_compilation_unit()->feedback().object());
    Condition needs_processing =
        LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(flags, feedback_vector,
                                                         CodeKind::MAGLEV);
    TailCallBuiltin(Builtin::kMaglevOptimizeCodeOrTailCallOptimizedCodeSlot,
                    needs_processing);
  }

  EnterFrame(StackFrame::MAGLEV);

  // Save arguments in frame.
  // TODO(leszeks): Consider eliding this frame if we don't make any calls
  // that could clobber these registers.
  // Push the context and the JSFunction.
  Push(kContextRegister, kJSFunctionRegister);
  // Push the actual argument count and a _possible_ stack slot.
  Push(kJavaScriptCallArgCountRegister, xzr);
  int remaining_stack_slots = code_gen_state()->stack_slots() - 1;
  DCHECK_GE(remaining_stack_slots, 0);

  // Initialize stack slots.
  if (graph->tagged_stack_slots() > 0) {
    ASM_CODE_COMMENT_STRING(this, "Initializing stack slots");

    // If tagged_stack_slots is divisible by 2, we overshoot and allocate one
    // extra stack slot, otherwise we allocate exactly the right amount, since
    // one stack has already been allocated.
    int tagged_two_slots_count = graph->tagged_stack_slots() / 2;
    remaining_stack_slots -= 2 * tagged_two_slots_count;

    // Magic value. Experimentally, an unroll size of 8 doesn't seem any
    // worse than fully unrolled pushes.
    const int kLoopUnrollSize = 8;
    if (tagged_two_slots_count < kLoopUnrollSize) {
      for (int i = 0; i < tagged_two_slots_count; i++) {
        Push(xzr, xzr);
      }
    } else {
      ScratchRegisterScope temps(this);
      Register count = temps.Acquire();
      // Extract the first few slots to round to the unroll size.
      int first_slots = tagged_two_slots_count % kLoopUnrollSize;
      for (int i = 0; i < first_slots; ++i) {
        Push(xzr, xzr);
      }
      Move(count, tagged_two_slots_count / kLoopUnrollSize);
      // We enter the loop unconditionally, so make sure we need to loop at
      // least once.
      DCHECK_GT(tagged_two_slots_count / kLoopUnrollSize, 0);
      Label loop;
      bind(&loop);
      for (int i = 0; i < kLoopUnrollSize; ++i) {
        Push(xzr, xzr);
      }
      Subs(count, count, Immediate(1));
      B(&loop, gt);
    }
  }
  if (remaining_stack_slots > 0) {
    // Round up.
    remaining_stack_slots += (remaining_stack_slots % 2);
    // Extend sp by the size of the remaining untagged part of the frame,
    // no need to initialise these.
    Sub(sp, sp, Immediate(remaining_stack_slots * kSystemPointerSize));
  }
}

void MaglevAssembler::MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                                 Label* eager_deopt_entry,
                                                 size_t lazy_deopt_count,
                                                 Label* lazy_deopt_entry) {
  ForceConstantPoolEmissionWithoutJump();

  DCHECK_GE(Deoptimizer::kLazyDeoptExitSize, Deoptimizer::kEagerDeoptExitSize);
  size_t deopt_count = eager_deopt_count + lazy_deopt_count;
  CheckVeneerPool(
      false, false,
      static_cast<int>(deopt_count) * Deoptimizer::kLazyDeoptExitSize);

  ScratchRegisterScope scope(this);
  Register scratch = scope.Acquire();
  if (eager_deopt_count > 0) {
    Bind(eager_deopt_entry);
    LoadEntryFromBuiltin(Builtin::kDeoptimizationEntry_Eager, scratch);
    MacroAssembler::Jump(scratch);
  }
  if (lazy_deopt_count > 0) {
    Bind(lazy_deopt_entry);
    LoadEntryFromBuiltin(Builtin::kDeoptimizationEntry_Lazy, scratch);
    MacroAssembler::Jump(scratch);
  }
}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                Register char_code,
                                                Register scratch) {
  DCHECK_NE(char_code, scratch);
  if (v8_flags.debug_code) {
    Cmp(char_code, Immediate(String::kMaxOneByteCharCode));
    Assert(ls, AbortReason::kUnexpectedValue);
  }
  Register table = scratch;
  LoadRoot(table, RootIndex::kSingleCharacterStringTable);
  LoadTaggedFieldByIndex(result, table, char_code, kTaggedSize,
                         FixedArray::kHeaderSize);
}

void MaglevAssembler::StringFromCharCode(RegisterSnapshot register_snapshot,
                                         Label* char_code_fits_one_byte,
                                         Register result, Register char_code,
                                         Register scratch) {
  AssertZeroExtended(char_code);
  DCHECK_NE(char_code, scratch);
  ZoneLabelRef done(this);
  Cmp(char_code, Immediate(String::kMaxOneByteCharCode));
  JumpToDeferredIf(
      hi,
      [](MaglevAssembler* masm, RegisterSnapshot register_snapshot,
         ZoneLabelRef done, Register result, Register char_code,
         Register scratch) {
        ScratchRegisterScope temps(masm);
        // Ensure that {result} never aliases {scratch}, otherwise the store
        // will fail.
        Register string = result;
        bool reallocate_result = scratch.Aliases(result);
        if (reallocate_result) {
          string = temps.Acquire();
        }
        // Be sure to save {char_code}. If it aliases with {result}, use
        // the scratch register.
        if (char_code.Aliases(result)) {
          __ Move(scratch, char_code);
          char_code = scratch;
        }
        DCHECK(!char_code.Aliases(string));
        DCHECK(!scratch.Aliases(string));
        DCHECK(!register_snapshot.live_tagged_registers.has(char_code));
        register_snapshot.live_registers.set(char_code);
        __ AllocateTwoByteString(register_snapshot, string, 1);
        __ And(scratch, char_code, Immediate(0xFFFF));
        __ Strh(
            scratch.W(),
            FieldMemOperand(string, OFFSET_OF_DATA_START(SeqTwoByteString)));
        if (reallocate_result) {
          __ Move(result, string);
        }
        __ B(*done);
      },
      register_snapshot, done, result, char_code, scratch);
  if (char_code_fits_one_byte != nullptr) {
    bind(char_code_fits_one_byte);
  }
  LoadSingleCharacterString(result, char_code, scratch);
  bind(*done);
}

void MaglevAssembler::StringCharCodeOrCodePointAt(
    BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
    RegisterSnapshot& register_snapshot, Register result, Register string,
    Register index, Register instance_type, Label* result_fits_one_byte) {
  ZoneLabelRef done(this);
  Label seq_string;
  Label cons_string;
  Label sliced_string;

  Label* deferred_runtime_call = MakeDeferredCode(
      [](MaglevAssembler* masm,
         BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
         RegisterSnapshot register_snapshot, ZoneLabelRef done, Register result,
         Register string, Register index) {
        DCHECK(!register_snapshot.live_registers.has(result));
        DCHECK(!register_snapshot.live_registers.has(string));
        DCHECK(!register_snapshot.live_registers.has(index));
        {
          SaveRegisterStateForCall save_register_state(masm, register_snapshot);
          __ SmiTag(index);
          __ Push(string, index);
          __ Move(kContextRegister, masm->native_context().object());
          // This call does not throw nor can deopt.
          if (mode ==
              BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt) {
            __ CallRuntime(Runtime::kStringCodePointAt);
          } else {
            DCHECK_EQ(mode,
                      BuiltinStringPrototypeCharCodeOrCodePointAt::kCharCodeAt);
            __ CallRuntime(Runtime::kStringCharCodeAt);
          }
          save_register_state.DefineSafepoint();
          __ SmiUntag(kReturnRegister0);
          __ Move(result, kReturnRegister0);
        }
        __ jmp(*done);
      },
      mode, register_snapshot, done, result, string, index);

  // We might need to try more than one time for ConsString, SlicedString and
  // ThinString.
  Label loop;
  bind(&loop);

  if (v8_flags.debug_code) {
    Register scratch = instance_type;

    // Check if {string} is a string.
    AssertNotSmi(string);
    LoadMap(scratch, string);
    CompareInstanceTypeRange(scratch, scratch, FIRST_STRING_TYPE,
                             LAST_STRING_TYPE);
    Check(ls, AbortReason::kUnexpectedValue);

    Ldr(scratch.W(), FieldMemOperand(string, offsetof(String, length_)));
    Cmp(index.W(), scratch.W());
    Check(lo, AbortReason::kUnexpectedValue);
  }

  // Get instance type.
  LoadInstanceType(instance_type, string);

  {
    ScratchRegisterScope temps(this);
    Register representation = temps.Acquire().W();

    // TODO(victorgomes): Add fast path for external strings.
    And(representation, instance_type.W(),
        Immediate(kStringRepresentationMask));
    CompareAndBranch(representation, Immediate(kSeqStringTag), kEqual,
                     &seq_string);
    CompareAndBranch(representation, Immediate(kConsStringTag), kEqual,
                     &cons_string);
    CompareAndBranch(representation, Immediate(kSlicedStringTag), kEqual,
                     &sliced_string);
    CompareAndBranch(representation, Immediate(kThinStringTag), kNotEqual,
                     deferred_runtime_call);
    // Fallthrough to thin string.
  }

  // Is a thin string.
  {
    LoadTaggedField(string, string, offsetof(ThinString, actual_));
    B(&loop);
  }

  bind(&sliced_string);
  {
    ScratchRegisterScope temps(this);
    Register offset = temps.Acquire();

    LoadAndUntagTaggedSignedField(offset, string,
                                  offsetof(SlicedString, offset_));
    LoadTaggedField(string, string, offsetof(SlicedString, parent_));
    Add(index, index, offset);
    B(&loop);
  }

  bind(&cons_string);
  {
    // Reuse {instance_type} register here, since CompareRoot requires a scratch
    // register as well.
    Register second_string = instance_type;
    LoadTaggedFieldWithoutDecompressing(second_string, string,
                                        offsetof(ConsString, second_));
    CompareRoot(second_string, RootIndex::kempty_string);
    B(deferred_runtime_call, ne);
    LoadTaggedField(string, string, offsetof(ConsString, first_));
    B(&loop);  // Try again with first string.
  }

  bind(&seq_string);
  {
    Label two_byte_string;
    TestAndBranchIfAllClear(instance_type, kOneByteStringTag, &two_byte_string);
    // The result of one-byte string will be the same for both modes
    // (CharCodeAt/CodePointAt), since it cannot be the first half of a
    // surrogate pair.
    Add(index, index, OFFSET_OF_DATA_START(SeqOneByteString) - kHeapObjectTag);
    Ldrb(result, MemOperand(string, index));
    B(result_fits_one_byte);

    bind(&two_byte_string);
    // {instance_type} is unused from this point, so we can use as scratch.
    Register scratch = instance_type;
    Lsl(scratch, index, 1);
    Add(scratch, scratch,
        OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag);
    Ldrh(result, MemOperand(string, scratch));

    if (mode == BuiltinStringPrototypeCharCodeOrCodePointAt::kCodePointAt) {
      Register first_code_point = scratch;
      And(first_code_point.W(), result.W(), Immediate(0xfc00));
      CompareAndBranch(first_code_point, Immediate(0xd800), kNotEqual, *done);

      Register length = scratch;
      Ldr(length.W(), FieldMemOperand(string, offsetof(String, length_)));
      Add(index.W(), index.W(), Immediate(1));
      CompareAndBranch(index, length, kGreaterThanEqual, *done);

      Register second_code_point = scratch;
      Lsl(index, index, 1);
      Add(index, index,
          OFFSET_OF_DATA_START(SeqTwoByteString) - kHeapObjectTag);
      Ldrh(second_code_point, MemOperand(string, index));

      // {index} is not needed at this point.
      Register scratch2 = index;
      And(scratch2.W(), second_code_point.W(), Immediate(0xfc00));
      CompareAndBranch(scratch2, Immediate(0xdc00), kNotEqual, *done);

      int surrogate_offset = 0x10000 - (0xd800 << 10) - 0xdc00;
      Add(second_code_point, second_code_point, Immediate(surrogate_offset));
      Lsl(result, result, 10);
      Add(result, result, second_code_point);
    }

    // Fallthrough.
  }

  bind(*done);

  if (v8_flags.debug_code) {
    // We make sure that the user of this macro is not relying in string and
    // index to not be clobbered.
    if (result != string) {
      Mov(string, Immediate(0xdeadbeef));
    }
    if (result != index) {
      Mov(index, Immediate(0xdeadbeef));
    }
  }
}

void MaglevAssembler::TruncateDoubleToInt32(Register dst, DoubleRegister src) {
  if (CpuFeatures::IsSupported(JSCVT)) {
    Fjcvtzs(dst.W(), src);
    return;
  }

  ZoneLabelRef done(this);
  // Try to convert with an FPU convert instruction. It's trivial to compute
  // the modulo operation on an integer register so we convert to a 64-bit
  // integer.
  //
  // Fcvtzs will saturate to INT64_MIN (0x800...00) or INT64_MAX (0x7FF...FF)
  // when the double is out of range. NaNs and infinities will be converted to 0
  // (as ECMA-262 requires).
  Fcvtzs(dst.X(), src);

  // The values INT64_MIN (0x800...00) or INT64_MAX (0x7FF...FF) are not
  // representable using a double, so if the result is one of those then we know
  // that saturation occurred, and we need to manually handle the conversion.
  //
  // It is easy to detect INT64_MIN and INT64_MAX because adding or subtracting
  // 1 will cause signed overflow.
  Cmp(dst.X(), 1);
  Ccmp(dst.X(), -1, VFlag, vc);

  JumpToDeferredIf(
      vs,
      [](MaglevAssembler* masm, DoubleRegister src, Register dst,
         ZoneLabelRef done) {
        __ MacroAssembler::Push(xzr, src);
        __ CallBuiltin(Builtin::kDoubleToI);
        __ Ldr(dst.W(), MemOperand(sp, 0));
        DCHECK_EQ(xzr.SizeInBytes(), src.SizeInBytes());
        __ Drop(2);
        __ B(*done);
      },
      src, dst, done);

  Bind(*done);
  // Zero extend the converted value to complete the truncation.
  Mov(dst, Operand(dst.W(), UXTW));
}

void MaglevAssembler::TryTruncateDoubleToInt32(Register dst, DoubleRegister src,
                                               Label* fail) {
  ScratchRegisterScope temps(this);
  DoubleRegister converted_back = temps.AcquireDouble();

  // Convert the input float64 value to int32.
  Fcvtzs(dst.W(), src);
  // Convert that int32 value back to float64.
  Scvtf(converted_back, dst.W());
  // Check that the result of the float64->int32->float64 is equal to the input
  // (i.e. that the conversion didn't truncate.
  Fcmp(src, converted_back);
  JumpIf(ne, fail);

  // Check if {input} is -0.
  Label check_done;
  Cbnz(dst, &check_done);

  // In case of 0, we need to check for the IEEE 0 pattern (which is all zeros).
  Register input_bits = temps.Acquire();
  Fmov(input_bits, src);
  Cbnz(input_bits, fail);

  Bind(&check_done);
}

void MaglevAssembler::TryTruncateDoubleToUint32(Register dst,
                                                DoubleRegister src,
                                                Label* fail) {
  ScratchRegisterScope temps(this);
  DoubleRegister converted_back = temps.AcquireDouble();

  // Convert the input float64 value to uint32.
  Fcvtzu(dst.W(), src);
  // Convert that uint32 value back to float64.
  Ucvtf(converted_back, dst);
  // Check that the result of the float64->uint32->float64 is equal to the input
  // (i.e. that the conversion didn't truncate.
  Fcmp(src, converted_back);
  JumpIf(ne, fail);

  // Check if {input} is -0.
  Label check_done;
  Cbnz(dst, &check_done);

  // In case of 0, we need to check for the IEEE 0 pattern (which is all zeros).
  Register input_bits = temps.Acquire();
  Fmov(input_bits, src);
  Cbnz(input_bits, fail);

  Bind(&check_done);
}

void MaglevAssembler::TryChangeFloat64ToIndex(Register result,
                                              DoubleRegister value,
                                              Label* success, Label* fail) {
  ScratchRegisterScope temps(this);
  DoubleRegister converted_back = temps.AcquireDouble();
  // Convert the input float64 value to int32.
  Fcvtzs(result.W(), value);
  // Convert that int32 value back to float64.
  Scvtf(converted_back, result.W());
  // Check that the result of the float64->int32->float64 is equal to
  // the input (i.e. that the conversion didn't truncate).
  Fcmp(value, converted_back);
  JumpIf(kNotEqual, fail);
  Jump(success);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
