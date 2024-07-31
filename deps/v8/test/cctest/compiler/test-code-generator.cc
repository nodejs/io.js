// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/linkage.h"
#include "src/execution/isolate.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/cctest/compiler/function-tester.h"
#include "test/common/code-assembler-tester.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ assembler.

namespace {

enum MoveMode { kParallelMoves, kSequentialMoves };

// Whether the layout before and after the moves must be the same.
enum LayoutMode {
  kPreserveLayout,
  kChangeLayout,
};
enum OperandLifetime { kInput, kOutput };

int GetSlotSizeInBytes(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kTagged:
      // Spill slots for tagged values are always uncompressed.
      return kSystemPointerSize;
    case MachineRepresentation::kFloat32:
      return kSystemPointerSize;
    case MachineRepresentation::kFloat64:
      return kDoubleSize;
    case MachineRepresentation::kSimd128:
      return kSimd128Size;
    default:
      break;
  }
  UNREACHABLE();
}

// Forward declaration.
Handle<Code> BuildTeardownFunction(
    Isolate* isolate, CallDescriptor* call_descriptor,
    const std::vector<AllocatedOperand>& parameters);

// Build the `setup` function. It takes a code object and a FixedArray as
// parameters and calls the former while passing it each element of the array as
// arguments:
// ~~~
// FixedArray setup(CodeObject* test, FixedArray state_in) {
//   FixedArray state_out = AllocateZeroedFixedArray(state_in.length());
//   // `test` will tail-call to its first parameter which will be `teardown`.
//   return test(teardown, state_out, state_in[0], state_in[1],
//               state_in[2], ...);
// }
// ~~~
//
// This function needs to convert each element of the FixedArray to raw unboxed
// values to pass to the `test` function. The array will have been created using
// `GenerateInitialState()` and needs to be converted in the following way:
//
// | Parameter type | FixedArray element  | Conversion                         |
// |----------------+---------------------+------------------------------------|
// | kTagged        | Smi                 | None.                              |
// | kFloat32       | HeapNumber          | Load value and convert to Float32. |
// | kFloat64       | HeapNumber          | Load value.                        |
// | kSimd128       | FixedArray<Smi>[4]  | Untag each Smi and write the       |
// |                |                     | results into lanes of a new        |
// |                |                     | 128-bit vector.                    |
//
Handle<Code> BuildSetupFunction(Isolate* isolate,
                                CallDescriptor* test_call_descriptor,
                                CallDescriptor* teardown_call_descriptor,
                                std::vector<AllocatedOperand> parameters,
                                const std::vector<AllocatedOperand>& results) {
  CodeAssemblerTester tester(isolate, JSParameterCount(2), CodeKind::BUILTIN,
                             "setup");
  CodeStubAssembler assembler(tester.state());
  std::vector<Node*> params;
  // The first parameter is always the callee.
  params.push_back(__ Parameter<Object>(1));
  // The parameters of the teardown function are the results of the test
  // function.
  params.push_back(__ HeapConstantNoHole(
      BuildTeardownFunction(isolate, teardown_call_descriptor, results)));
  // First allocate the FixedArray which will hold the final results. Here we
  // should take care of all allocations, meaning we allocate HeapNumbers and
  // FixedArrays representing Simd128 values.
  TNode<FixedArray> state_out =
      __ AllocateZeroedFixedArray(__ IntPtrConstant(results.size()));
  for (int i = 0; i < static_cast<int>(results.size()); i++) {
    switch (results[i].representation()) {
      case MachineRepresentation::kTagged:
        break;
      case MachineRepresentation::kFloat32:
      case MachineRepresentation::kFloat64:
        __ StoreFixedArrayElement(state_out, i, __ AllocateHeapNumber());
        break;
      case MachineRepresentation::kSimd128: {
        TNode<FixedArray> vector =
            __ AllocateZeroedFixedArray(__ IntPtrConstant(4));
        for (int lane = 0; lane < 4; lane++) {
          __ StoreFixedArrayElement(vector, lane, __ SmiConstant(0));
        }
        __ StoreFixedArrayElement(state_out, i, vector);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  params.push_back(state_out);
  // Then take each element of the initial state and pass them as arguments.
  auto state_in = __ Parameter<FixedArray>(2);
  for (int i = 0; i < static_cast<int>(parameters.size()); i++) {
    Node* element = __ LoadFixedArrayElement(state_in, __ IntPtrConstant(i));
    // Unbox all elements before passing them as arguments.
    switch (parameters[i].representation()) {
      // Tagged parameters are Smis, they do not need unboxing.
      case MachineRepresentation::kTagged:
        break;
      case MachineRepresentation::kFloat32:
        element = __ TruncateFloat64ToFloat32(
            __ LoadHeapNumberValue(__ CAST(element)));
        break;
      case MachineRepresentation::kFloat64:
        element = __ LoadHeapNumberValue(__ CAST(element));
        break;
#if V8_ENABLE_WEBASSEMBLY
      case MachineRepresentation::kSimd128: {
        Node* vector = tester.raw_assembler_for_testing()->AddNode(
            tester.raw_assembler_for_testing()->machine()->I32x4Splat(),
            __ Int32Constant(0));
        for (int lane = 0; lane < 4; lane++) {
          TNode<Int32T> lane_value = __ LoadAndUntagToWord32FixedArrayElement(
              __ CAST(element), __ IntPtrConstant(lane));
          vector = tester.raw_assembler_for_testing()->AddNode(
              tester.raw_assembler_for_testing()->machine()->I32x4ReplaceLane(
                  lane),
              vector, lane_value);
        }
        element = vector;
        break;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      default:
        UNREACHABLE();
    }
    params.push_back(element);
  }
  __ Return(
      __ UncheckedCast<Object>(tester.raw_assembler_for_testing()->AddNode(
          tester.raw_assembler_for_testing()->common()->Call(
              test_call_descriptor),
          static_cast<int>(params.size()), params.data())));
  return tester.GenerateCodeCloseAndEscape();
}

// Build the `teardown` function. It takes a FixedArray as argument, fills it
// with the rest of its parameters and returns it. The parameters need to be
// consistent with `parameters`.
// ~~~
// FixedArray teardown(CodeObject* /* unused  */, FixedArray result,
//                     // Tagged registers.
//                     Object r0, Object r1, ...,
//                     // FP registers.
//                     Float32 s0, Float64 d1, ...,
//                     // Mixed stack slots.
//                     Float64 mem0, Object mem1, Float32 mem2, ...) {
//   result[0] = r0;
//   result[1] = r1;
//   ...
//   result[..] = s0;
//   ...
//   result[..] = mem0;
//   ...
//   return result;
// }
// ~~~
//
// This function needs to convert its parameters into values fit for a
// FixedArray, essentially reverting what the `setup` function did:
//
// | Parameter type | Parameter value   | Conversion                           |
// |----------------+-------------------+--------------------------------------|
// | kTagged        | Smi or HeapNumber | None.                                |
// | kFloat32       | Raw Float32       | Convert to Float64.                  |
// | kFloat64       | Raw Float64       | None.                                |
// | kSimd128       | Raw Simd128       | Split into 4 Word32 values and tag   |
// |                |                   | them.                                |
//
// Note that it is possible for a `kTagged` value to go from a Smi to a
// HeapNumber. This is because `AssembleMove` will allocate a new HeapNumber if
// it is asked to move a FP constant to a tagged register or slot.
//
// Finally, it is important that this function does not call `RecordWrite` which
// is why "setup" is in charge of all allocations and we are using
// UNSAFE_SKIP_WRITE_BARRIER. The reason for this is that `RecordWrite` may
// clobber the top 64 bits of Simd128 registers. This is the case on x64, ia32
// and Arm64 for example.
Handle<Code> BuildTeardownFunction(
    Isolate* isolate, CallDescriptor* call_descriptor,
    const std::vector<AllocatedOperand>& parameters) {
  CodeAssemblerTester tester(isolate, call_descriptor, "teardown");
  CodeStubAssembler assembler(tester.state());
  auto result_array = __ Parameter<FixedArray>(1);
  for (int i = 0; i < static_cast<int>(parameters.size()); i++) {
    // The first argument is not used and the second is "result_array".
    Node* param = __ UntypedParameter(i + 2);
    switch (parameters[i].representation()) {
      case MachineRepresentation::kTagged:
        __ StoreFixedArrayElement(result_array, i, __ Cast(param),
                                  UNSAFE_SKIP_WRITE_BARRIER);
        break;
      // Box FP values into HeapNumbers.
      case MachineRepresentation::kFloat32:
        param =
            tester.raw_assembler_for_testing()->ChangeFloat32ToFloat64(param);
        [[fallthrough]];
      case MachineRepresentation::kFloat64: {
        __ StoreHeapNumberValue(
            __ Cast(__ LoadFixedArrayElement(result_array, i)),
            __ UncheckedCast<Float64T>(param));
      } break;
#if V8_ENABLE_WEBASSEMBLY
      case MachineRepresentation::kSimd128: {
        TNode<FixedArray> vector =
            __ Cast(__ LoadFixedArrayElement(result_array, i));
        for (int lane = 0; lane < 4; lane++) {
          TNode<Smi> lane_value = __ SmiFromInt32(__ UncheckedCast<Int32T>(
              tester.raw_assembler_for_testing()->AddNode(
                  tester.raw_assembler_for_testing()
                      ->machine()
                      ->I32x4ExtractLane(lane),
                  param)));
          __ StoreFixedArrayElement(vector, lane, lane_value,
                                    UNSAFE_SKIP_WRITE_BARRIER);
        }
        break;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      default:
        UNREACHABLE();
    }
  }
  __ Return(result_array);
  return tester.GenerateCodeCloseAndEscape();
}

// Print the content of `value`, representing the register or stack slot
// described by `operand`.
void PrintStateValue(std::ostream& os, Isolate* isolate, Handle<Object> value,
                     AllocatedOperand operand) {
  switch (operand.representation()) {
    case MachineRepresentation::kTagged:
      if (IsSmi(*value)) {
        os << Smi::cast(*value).value();
      } else {
        os << Object::Number(*value);
      }
      break;
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kFloat64:
      os << Object::Number(*value);
      break;
    case MachineRepresentation::kSimd128: {
      Tagged<FixedArray> vector = FixedArray::cast(*value);
      os << "[";
      for (int lane = 0; lane < 4; lane++) {
        os << Smi::cast(vector->get(lane)).value();
        if (lane < 3) {
          os << ", ";
        }
      }
      os << "]";
      break;
    }
    default:
      UNREACHABLE();
  }
  os << " (" << operand.representation() << " ";
  if (operand.location_kind() == AllocatedOperand::REGISTER) {
    os << "register";
  } else {
    DCHECK_EQ(operand.location_kind(), AllocatedOperand::STACK_SLOT);
    os << "stack slot";
  }
  os << ")";
}

bool TestSimd128Moves() { return CpuFeatures::SupportsWasmSimd128(); }

}  // namespace

#undef __

// Representation of a test environment. It describes a set of registers, stack
// slots and constants available to the CodeGeneratorTester to perform moves
// with. It has the ability to randomly generate lists of moves and run the code
// generated by the CodeGeneratorTester.
//
// The following representations are tested:
//   - kTagged
//   - kFloat32
//   - kFloat64
//   - kSimd128 (if supported)
// There is no need to test using Word32 or Word64 as they are the same as
// Tagged as far as the code generator is concerned.
//
// Testing the generated code is achieved by wrapping it around `setup` and
// `teardown` functions, written using the CodeStubAssembler. The key idea here
// is that `teardown` and the generated code share the same custom
// CallDescriptor. This descriptor assigns parameters to either registers or
// stack slot of a given representation and therefore essentially describes the
// environment.
//
// What happens is the following:
//
//   - The `setup` function receives a FixedArray as the initial state. It
//     unpacks it and passes each element as arguments to the generated code
//     `test`. We also pass the `teardown` function as a first argument as well
//     as a newly allocated FixedArray as a second argument which will hold the
//     final results. Thanks to the custom CallDescriptor, registers and stack
//     slots get initialised according to the content of the initial FixedArray.
//
//   - The `test` function performs the list of moves on its parameters and
//     eventually tail-calls to its first parameter, which is the `teardown`
//     function.
//
//   - The `teardown` function receives the final results as a FixedArray, fills
//     it with the rest of its arguments and returns it. Thanks to the
//     tail-call, this is as if the `setup` function called `teardown` directly,
//     except now moves were performed!
//
// .----------------setup--------------------------.
// | Take a FixedArray as parameters with          |
// | all the initial values of registers           |
// | and stack slots.                              | <- CodeStubAssembler
// |                                               |
// | Allocate a new FixedArray `result` with       |
// | initial values.                               |
// |                                               |
// | Call test(teardown, result, state[0],         |
// |           state[1], state[2], ...);           |
// '-----------------------------------------------'
//   |
//   V
// .----------------test-------------------------------.
// | - Move(param3, param42);                          |
// | - Swap(param64, param4);                          |
// | - Move(param2, param6);                           | <- CodeGeneratorTester
// | ...                                               |
// |                                                   |
// | // "teardown" is the first parameter as well as   |
// | // the callee.                                    |
// | TailCall teardown(teardown, result, param2, ...); |
// '---------------------------------------------------'
//   |
//   V
// .----------------teardown---------------------------.
// | Fill in the incoming `result` FixedArray with all |
// | parameters and return it.                         | <- CodeStubAssembler
// '---------------------------------------------------'

class TestEnvironment : public HandleAndZoneScope {
 public:
  // These constants may be tuned to experiment with different environments.

#ifdef V8_TARGET_ARCH_IA32
  static constexpr int kGeneralRegisterCount = 3;
#else
  static constexpr int kGeneralRegisterCount = 4;
#endif
  static constexpr int kDoubleRegisterCount = 6;

  static constexpr int kTaggedSlotCount = 64;
  static constexpr int kFloat32SlotCount = 64;
  static constexpr int kFloat64SlotCount = 64;
  static constexpr int kSimd128SlotCount = 16;

  // TODO(all): Test all types of constants (e.g. ExternalReference and
  // HeapObject).
  static constexpr int kSmiConstantCount = 4;
  static constexpr int kFloatConstantCount = 4;
  static constexpr int kDoubleConstantCount = 4;

  explicit TestEnvironment(LayoutMode layout_mode = kPreserveLayout)
      : blocks_(1, NewBlock(main_zone(), RpoNumber::FromInt(0)), main_zone()),
        instructions_(main_isolate(), main_zone(), &blocks_),
        rng_(CcTest::random_number_generator()),
        layout_mode_(layout_mode),
        supported_reps_({MachineRepresentation::kTagged,
                         MachineRepresentation::kFloat32,
                         MachineRepresentation::kFloat64}) {
    stack_slot_count_ =
        kTaggedSlotCount + kFloat32SlotCount + kFloat64SlotCount;
    if (TestSimd128Moves()) {
      stack_slot_count_ += kSimd128SlotCount;
      supported_reps_.push_back(MachineRepresentation::kSimd128);
    }
    // The "teardown" and "test" functions share the same descriptor with the
    // following signature:
    // ~~~
    // FixedArray f(CodeObject* teardown, FixedArray preallocated_result,
    //              // Tagged registers.
    //              Object, Object, ...,
    //              // FP registers.
    //              Float32, Float64, Simd128, ...,
    //              // Mixed stack slots.
    //              Float64, Object, Float32, Simd128, ...);
    // ~~~
    LocationSignature::Builder test_signature(
        main_zone(), 1,
        2 + kGeneralRegisterCount + kDoubleRegisterCount + stack_slot_count_);
    LocationSignature::Builder teardown_signature(
        main_zone(), 1,
        2 + kGeneralRegisterCount + kDoubleRegisterCount + stack_slot_count_);

    for (auto* sig : {&test_signature, &teardown_signature}) {
      // The first parameter will be the code object of the "teardown"
      // function. This way, the "test" function can tail-call to it.
      sig->AddParam(LinkageLocation::ForRegister(kReturnRegister0.code(),
                                                 MachineType::AnyTagged()));

      // The second parameter will be a pre-allocated FixedArray that the
      // "teardown" function will fill with result and then return. We place
      // this parameter on the first stack argument slot which is always -1. And
      // therefore slots to perform moves on start at -2.
      sig->AddParam(
          LinkageLocation::ForCallerFrameSlot(-1, MachineType::AnyTagged()));
    }

    // Initialise registers.

    // Make sure that the target has enough general purpose registers to
    // generate a call to a CodeObject using this descriptor. We have reserved
    // kReturnRegister0 as the first parameter, and the call will need a
    // register to hold the CodeObject address. So the maximum number of
    // registers left to test with is the number of available registers minus 2.
    DCHECK_LE(kGeneralRegisterCount,
              GetRegConfig()->num_allocatable_general_registers() - 2);

    GenerateLayout(setup_layout_, allocated_slots_in_, &test_signature);
    test_descriptor_ = MakeCallDescriptor(test_signature.Build());

    if (layout_mode_ == kChangeLayout) {
      GenerateLayout(teardown_layout_, allocated_slots_out_,
                     &teardown_signature);
      teardown_descriptor_ = MakeCallDescriptor(teardown_signature.Build());
    }
    // Else, we just reuse the layout and signature of the setup function for
    // the teardown function since they are the same.
  }

  void AddStackSlots(
      std::vector<AllocatedOperand>& layout,
      std::map<MachineRepresentation, std::vector<AllocatedOperand>>& slots,
      LocationSignature::Builder* sig) {
    // The first stack slot is the FixedArray, start at -2.
    int slot_parameter_n = -2;
    std::map<MachineRepresentation, int> slot_count = {
        {MachineRepresentation::kTagged, kTaggedSlotCount},
        {MachineRepresentation::kFloat32, kFloat32SlotCount},
        {MachineRepresentation::kFloat64, kFloat64SlotCount}};
    if (TestSimd128Moves()) {
      slot_count.emplace(MachineRepresentation::kSimd128, kSimd128SlotCount);
    }

    // Allocate new slots until we run out of them.
    while (std::any_of(slot_count.cbegin(), slot_count.cend(),
                       [](const std::pair<MachineRepresentation, int>& entry) {
                         // True if there are slots left to allocate for this
                         // representation.
                         return entry.second > 0;
                       })) {
      // Pick a random MachineRepresentation from supported_reps_.
      MachineRepresentation rep = CreateRandomMachineRepresentation();
      auto entry = slot_count.find(rep);
      DCHECK_NE(entry, slot_count.end());
      // We may have picked a representation for which all slots have already
      // been allocated.
      if (entry->second > 0) {
        // Keep a map of (MachineRepresentation . std::vector<int>) with
        // allocated slots to pick from for each representation.
        int slot = slot_parameter_n;
        slot_parameter_n -= (GetSlotSizeInBytes(rep) / kSystemPointerSize);
        AddStackSlot(layout, slots, sig, rep, slot);
        entry->second--;
      }
    }
  }

  void GenerateLayout(
      std::vector<AllocatedOperand>& layout,
      std::map<MachineRepresentation, std::vector<AllocatedOperand>>& slots,
      LocationSignature::Builder* sig) {
    RegList general_mask =
        RegList::FromBits(GetRegConfig()->allocatable_general_codes_mask());
    // kReturnRegister0 is used to hold the "teardown" code object, do not
    // generate moves using it.
    general_mask.clear(kReturnRegister0);
    std::unique_ptr<const RegisterConfiguration> registers(
        RegisterConfiguration::RestrictGeneralRegisters(general_mask));

    for (int i = 0; i < kGeneralRegisterCount; i++) {
      int code = registers->GetAllocatableGeneralCode(i);
      AddRegister(layout, sig, MachineRepresentation::kTagged, code);
    }
    // We assume that Double, Float and Simd128 registers alias, depending on
    // kSimpleFPAliasing. For this reason, we allocate a Float, Double and
    // Simd128 together, hence the reason why `kDoubleRegisterCount` should be a
    // multiple of 3 and 2 in case Simd128 is not supported.
    static_assert(
        ((kDoubleRegisterCount % 2) == 0) && ((kDoubleRegisterCount % 3) == 0),
        "kDoubleRegisterCount should be a multiple of two and three.");
    for (int i = 0; i < kDoubleRegisterCount; i += 2) {
      if (kFPAliasing != AliasingKind::kCombine) {
        // Allocate three registers at once if kSimd128 is supported, else
        // allocate in pairs.
        AddRegister(layout, sig, MachineRepresentation::kFloat32,
                    registers->GetAllocatableFloatCode(i));
        AddRegister(layout, sig, MachineRepresentation::kFloat64,
                    registers->GetAllocatableDoubleCode(i + 1));
        if (TestSimd128Moves()) {
          AddRegister(layout, sig, MachineRepresentation::kSimd128,
                      registers->GetAllocatableSimd128Code(i + 2));
          i++;
        }
      } else {
        // Make sure we do not allocate FP registers which alias. To do this, we
        // allocate three 128-bit registers and then convert two of them to a
        // float and a double. With this aliasing scheme, a Simd128 register
        // aliases two Double registers and four Float registers, so we need to
        // scale indexes accordingly:
        //
        //   Simd128 register: q0, q1, q2, q3,  q4, q5
        //                      |   |       |    |
        //                      V   V       V    V
        //   Aliases:          s0, d2, q2, s12, d8, q5
        //
        // This isn't space efficient at all but suits our need.
        static_assert(
            kDoubleRegisterCount < 8,
            "Arm has a q8 and a d16 register but no overlapping s32 register.");
        int first_simd128 = registers->GetAllocatableSimd128Code(i);
        int second_simd128 = registers->GetAllocatableSimd128Code(i + 1);
        AddRegister(layout, sig, MachineRepresentation::kFloat32,
                    first_simd128 * 4);
        AddRegister(layout, sig, MachineRepresentation::kFloat64,
                    second_simd128 * 2);
        if (TestSimd128Moves()) {
          int third_simd128 = registers->GetAllocatableSimd128Code(i + 2);
          AddRegister(layout, sig, MachineRepresentation::kSimd128,
                      third_simd128);
          i++;
        }
      }
    }

    // Initialise random constants.

    // While constants do not know about Smis, we need to be able to
    // differentiate between a pointer to a HeapNumber and a integer. For this
    // reason, we make sure all integers are Smis, including constants.
    for (int i = 0; i < kSmiConstantCount; i++) {
      intptr_t smi_value = static_cast<intptr_t>(
          Smi::FromInt(rng_->NextInt(Smi::kMaxValue)).ptr());
      Constant constant = kSystemPointerSize == 8
                              ? Constant(static_cast<int64_t>(smi_value))
                              : Constant(static_cast<int32_t>(smi_value));
      AddConstant(MachineRepresentation::kTagged, AllocateConstant(constant));
    }
    // Float and Double constants can be moved to both Tagged and FP registers
    // or slots. Register them as compatible with both FP and Tagged
    // destinations.
    for (int i = 0; i < kFloatConstantCount; i++) {
      int virtual_register =
          AllocateConstant(Constant(DoubleToFloat32(rng_->NextDouble())));
      AddConstant(MachineRepresentation::kTagged, virtual_register);
      AddConstant(MachineRepresentation::kFloat32, virtual_register);
    }
    for (int i = 0; i < kDoubleConstantCount; i++) {
      int virtual_register = AllocateConstant(Constant(rng_->NextDouble()));
      AddConstant(MachineRepresentation::kTagged, virtual_register);
      AddConstant(MachineRepresentation::kFloat64, virtual_register);
    }

    // The "teardown" function returns a FixedArray with the resulting state.
    sig->AddReturn(LinkageLocation::ForRegister(kReturnRegister0.code(),
                                                MachineType::AnyTagged()));
    AddStackSlots(layout, slots, sig);
  }

  CallDescriptor* MakeCallDescriptor(LocationSignature* sig) {
    const int kTotalStackParameterCount = stack_slot_count_ + 1;
    return main_zone()->New<CallDescriptor>(
        CallDescriptor::kCallCodeObject,  // kind
        kDefaultCodeEntrypointTag,        // tag
        MachineType::AnyTagged(),         // target MachineType
        LinkageLocation::ForAnyRegister(
            MachineType::AnyTagged()),  // target location
        sig,                            // location_sig
        kTotalStackParameterCount,      // stack_parameter_count
        Operator::kNoProperties,        // properties
        kNoCalleeSaved,                 // callee-saved registers
        kNoCalleeSavedFp,               // callee-saved fp
        CallDescriptor::kNoFlags);      // flags
  }

  int AllocateConstant(Constant constant) {
    int virtual_register = instructions_.NextVirtualRegister();
    instructions_.AddConstant(virtual_register, constant);
    return virtual_register;
  }

  // Register a constant referenced by `virtual_register` as compatible with
  // `rep`.
  void AddConstant(MachineRepresentation rep, int virtual_register) {
    auto entry = allocated_constants_.find(rep);
    if (entry == allocated_constants_.end()) {
      allocated_constants_.emplace(
          rep, std::vector<ConstantOperand>{ConstantOperand(virtual_register)});
    } else {
      entry->second.emplace_back(virtual_register);
    }
  }

  // Register a new register or stack slot as compatible with `rep`. As opposed
  // to constants, registers and stack slots are written to on `setup` and read
  // from on `teardown`. Therefore they are part of the environment's layout,
  // and are parameters of the `test` function.

  void AddRegister(std::vector<AllocatedOperand>& layout,
                   LocationSignature::Builder* test_signature,
                   MachineRepresentation rep, int code) {
    AllocatedOperand operand(AllocatedOperand::REGISTER, rep, code);
    layout.push_back(operand);
    test_signature->AddParam(LinkageLocation::ForRegister(
        code, MachineType::TypeForRepresentation(rep)));
    auto entry = allocated_registers_.find(rep);
    if (entry == allocated_registers_.end()) {
      allocated_registers_.emplace(rep, std::vector<AllocatedOperand>{operand});
    } else {
      entry->second.push_back(operand);
    }
  }

  void AddStackSlot(
      std::vector<AllocatedOperand>& layout,
      std::map<MachineRepresentation, std::vector<AllocatedOperand>>& slots,
      LocationSignature::Builder* sig, MachineRepresentation rep, int slot) {
    AllocatedOperand operand(AllocatedOperand::STACK_SLOT, rep, slot);
    layout.push_back(operand);
    sig->AddParam(LinkageLocation::ForCallerFrameSlot(
        slot, MachineType::TypeForRepresentation(rep)));
    auto entry = slots.find(rep);
    if (entry == slots.end()) {
      slots.emplace(rep, std::vector<AllocatedOperand>{operand});
    } else {
      entry->second.push_back(operand);
    }
  }

  // Generate a random initial state to test moves against. A "state" is a
  // packed FixedArray with Smis and HeapNumbers, according to the layout of the
  // environment.
  Handle<FixedArray> GenerateInitialState() {
    Handle<FixedArray> state = main_isolate()->factory()->NewFixedArray(
        static_cast<int>(setup_layout_.size()));
    for (int i = 0; i < state->length(); i++) {
      switch (setup_layout_[i].representation()) {
        case MachineRepresentation::kTagged:
          state->set(i, Smi::FromInt(rng_->NextInt(Smi::kMaxValue)));
          break;
        case MachineRepresentation::kFloat32: {
          // HeapNumbers are Float64 values. However, we will convert it to a
          // Float32 and back inside `setup` and `teardown`. Make sure the value
          // we pick fits in a Float32.
          Handle<HeapNumber> num = main_isolate()->factory()->NewHeapNumber(
              static_cast<double>(DoubleToFloat32(rng_->NextDouble())));
          state->set(i, *num);
          break;
        }
        case MachineRepresentation::kFloat64: {
          Handle<HeapNumber> num =
              main_isolate()->factory()->NewHeapNumber(rng_->NextDouble());
          state->set(i, *num);
          break;
        }
        case MachineRepresentation::kSimd128: {
          Handle<FixedArray> vector =
              main_isolate()->factory()->NewFixedArray(4);
          for (int lane = 0; lane < 4; lane++) {
            vector->set(lane, Smi::FromInt(rng_->NextInt(Smi::kMaxValue)));
          }
          state->set(i, *vector);
          break;
        }
        default:
          UNREACHABLE();
      }
    }
    return state;
  }

  // Run the code generated by a CodeGeneratorTester against `state_in` and
  // return a new resulting state.
  Handle<FixedArray> Run(Handle<Code> test, Handle<FixedArray> state_in) {
    Handle<FixedArray> state_out = main_isolate()->factory()->NewFixedArray(
        static_cast<int>(TeardownLayout().size()));
    {
#ifdef ENABLE_SLOW_DCHECKS
      // The "setup" and "teardown" functions are relatively big, and with
      // runtime assertions enabled they get so big that memory during register
      // allocation becomes a problem. Temporarily disable such assertions.
      bool old_enable_slow_asserts = v8_flags.enable_slow_asserts;
      v8_flags.enable_slow_asserts = false;
#endif
      Handle<Code> setup = BuildSetupFunction(main_isolate(), test_descriptor_,
                                              TeardownCallDescriptor(),
                                              setup_layout_, TeardownLayout());
#ifdef ENABLE_SLOW_DCHECKS
      v8_flags.enable_slow_asserts = old_enable_slow_asserts;
#endif
      // FunctionTester maintains its own HandleScope which means that its
      // return value will be freed along with it. Copy the result into
      // state_out.
      FunctionTester ft(setup, 2);
      Handle<FixedArray> result = ft.CallChecked<FixedArray>(test, state_in);
      CHECK_EQ(result->length(), state_in->length());
      FixedArray::CopyElements(main_isolate(), *state_out, 0, *result, 0,
                               result->length());
    }
    return state_out;
  }

  std::vector<AllocatedOperand>& TeardownLayout() {
    return layout_mode_ == kPreserveLayout ? setup_layout_ : teardown_layout_;
  }

  CallDescriptor* TeardownCallDescriptor() {
    return layout_mode_ == kPreserveLayout ? test_descriptor_
                                           : teardown_descriptor_;
  }

  // For a given operand representing either a register or a stack slot, return
  // what position it should live in inside a FixedArray state.
  int OperandToStatePosition(std::vector<AllocatedOperand>& layout,
                             const AllocatedOperand& operand) const {
    // Search `layout_` for `operand`.
    auto it = std::find_if(layout.cbegin(), layout.cend(),
                           [operand](const AllocatedOperand& this_operand) {
                             return this_operand.Equals(operand);
                           });
    DCHECK_NE(it, layout.cend());
    return static_cast<int>(std::distance(layout.cbegin(), it));
  }

  Tagged<Object> GetMoveSource(Handle<FixedArray> state, MoveOperands* move) {
    InstructionOperand from = move->source();
    if (from.IsConstant()) {
      Constant constant = instructions_.GetConstant(
          ConstantOperand::cast(from).virtual_register());
      Handle<Object> constant_value;
      switch (constant.type()) {
        case Constant::kInt32:
          constant_value =
              Handle<Smi>(Tagged<Smi>(static_cast<Address>(
                              static_cast<intptr_t>(constant.ToInt32()))),
                          main_isolate());
          break;
        case Constant::kInt64:
          constant_value =
              Handle<Smi>(Tagged<Smi>(static_cast<Address>(constant.ToInt64())),
                          main_isolate());
          break;
        case Constant::kFloat32:
          constant_value = main_isolate()->factory()->NewHeapNumber(
              static_cast<double>(constant.ToFloat32()));
          break;
        case Constant::kFloat64:
          constant_value = main_isolate()->factory()->NewHeapNumber(
              constant.ToFloat64().value());
          break;
        default:
          UNREACHABLE();
      }
      return *constant_value;
    } else {
      int from_index =
          OperandToStatePosition(setup_layout_, AllocatedOperand::cast(from));
      return state->get(from_index);
    }
  }

  // Perform the given list of sequential moves on `state_in` and return a newly
  // allocated state with the results.
  Handle<FixedArray> SimulateSequentialMoves(ParallelMove* moves,
                                             Handle<FixedArray> state_in) {
    Handle<FixedArray> state_out = main_isolate()->factory()->NewFixedArray(
        static_cast<int>(setup_layout_.size()));
    // We do not want to modify `state_in` in place so perform the moves on a
    // copy.
    FixedArray::CopyElements(main_isolate(), *state_out, 0, *state_in, 0,
                             state_in->length());
    DCHECK_EQ(kPreserveLayout, layout_mode_);
    for (auto move : *moves) {
      int to_index = OperandToStatePosition(
          TeardownLayout(), AllocatedOperand::cast(move->destination()));
      Tagged<Object> source = GetMoveSource(state_out, move);
      state_out->set(to_index, source);
    }
    return state_out;
  }

  // Perform the given list of parallel moves on `state_in` and return a newly
  // allocated state with the results.
  Handle<FixedArray> SimulateParallelMoves(ParallelMove* moves,
                                           Handle<FixedArray> state_in) {
    Handle<FixedArray> state_out = main_isolate()->factory()->NewFixedArray(
        static_cast<int>(teardown_layout_.size()));
    for (auto move : *moves) {
      int to_index = OperandToStatePosition(
          TeardownLayout(), AllocatedOperand::cast(move->destination()));
      Tagged<Object> source = GetMoveSource(state_in, move);
      state_out->set(to_index, source);
    }
    // If we generated redundant moves, they were eliminated automatically and
    // don't appear in the parallel move. Simulate them now.
    for (auto& operand : teardown_layout_) {
      int to_index = OperandToStatePosition(TeardownLayout(), operand);
      if (IsUndefined(state_out->get(to_index))) {
        int from_index = OperandToStatePosition(setup_layout_, operand);
        state_out->set(to_index, state_in->get(from_index));
      }
    }
    return state_out;
  }

  // Perform the given list of swaps on `state_in` and return a newly allocated
  // state with the results.
  Handle<FixedArray> SimulateSwaps(ParallelMove* swaps,
                                   Handle<FixedArray> state_in) {
    Handle<FixedArray> state_out = main_isolate()->factory()->NewFixedArray(
        static_cast<int>(setup_layout_.size()));
    // We do not want to modify `state_in` in place so perform the swaps on a
    // copy.
    FixedArray::CopyElements(main_isolate(), *state_out, 0, *state_in, 0,
                             state_in->length());
    for (auto swap : *swaps) {
      int lhs_index = OperandToStatePosition(
          setup_layout_, AllocatedOperand::cast(swap->destination()));
      int rhs_index = OperandToStatePosition(
          setup_layout_, AllocatedOperand::cast(swap->source()));
      Handle<Object> lhs{state_out->get(lhs_index), main_isolate()};
      Handle<Object> rhs{state_out->get(rhs_index), main_isolate()};
      state_out->set(lhs_index, *rhs);
      state_out->set(rhs_index, *lhs);
    }
    return state_out;
  }

  // Compare the given state with a reference.
  void CheckState(Handle<FixedArray> actual, Handle<FixedArray> expected) {
    for (int i = 0; i < static_cast<int>(TeardownLayout().size()); i++) {
      Handle<Object> actual_value{actual->get(i), main_isolate()};
      Handle<Object> expected_value{expected->get(i), main_isolate()};
      if (!CompareValues(actual_value, expected_value,
                         TeardownLayout()[i].representation())) {
        std::ostringstream expected_str;
        PrintStateValue(expected_str, main_isolate(), expected_value,
                        TeardownLayout()[i]);
        std::ostringstream actual_str;
        PrintStateValue(actual_str, main_isolate(), actual_value,
                        TeardownLayout()[i]);
        FATAL("Expected: '%s' but got '%s'", expected_str.str().c_str(),
              actual_str.str().c_str());
      }
    }
  }

  bool CompareValues(Handle<Object> actual, Handle<Object> expected,
                     MachineRepresentation rep) {
    switch (rep) {
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kFloat32:
      case MachineRepresentation::kFloat64:
        return Object::StrictEquals(*actual, *expected);
      case MachineRepresentation::kSimd128:
        for (int lane = 0; lane < 4; lane++) {
          int actual_lane =
              Smi::cast(FixedArray::cast(*actual)->get(lane)).value();
          int expected_lane =
              Smi::cast(FixedArray::cast(*expected)->get(lane)).value();
          if (actual_lane != expected_lane) {
            return false;
          }
        }
        return true;
      default:
        UNREACHABLE();
    }
  }

  enum OperandConstraint {
    kNone,
    // Restrict operands to non-constants. This is useful when generating a
    // destination.
    kCannotBeConstant
  };

  // Generate sequential moves at random. Note that they may not be compatible
  // between each other as this doesn't matter to the code generator.
  ParallelMove* GenerateRandomMoves(int size, MoveMode move_mode) {
    ParallelMove* parallel_move = main_zone()->New<ParallelMove>(main_zone());

    for (int i = 0; i < size;) {
      MachineRepresentation rep = CreateRandomMachineRepresentation();
      InstructionOperand source = CreateRandomOperand(kNone, rep, kInput);
      MoveOperands mo(source,
                      CreateRandomOperand(kCannotBeConstant, rep, kOutput));
      // It isn't valid to call `AssembleMove` and `AssembleSwap` with redundant
      // moves.
      if (mo.IsRedundant()) continue;
      parallel_move->AddMove(mo.source(), mo.destination());
      i++;
    }

    return parallel_move;
  }

  // Generate parallel moves at random. Generate exactly one move for each
  // available destination operand. Since the output layout is different from
  // the input layout, this ensures that each destination operand is initialized
  // with one of the values in the input fixed array.
  ParallelMove* GenerateRandomParallelMoves() {
    ParallelMove* parallel_move = main_zone()->New<ParallelMove>(main_zone());
    std::vector<AllocatedOperand> destinations = teardown_layout_;
    std::shuffle(destinations.begin(), destinations.end(), *rng_);

    for (size_t i = 0; i < destinations.size(); ++i) {
      MachineRepresentation rep = destinations[i].representation();
      InstructionOperand source = CreateRandomOperand(kNone, rep, kInput);
      MoveOperands mo(source, destinations[i]);
      parallel_move->AddMove(mo.source(), mo.destination());
    }

    return parallel_move;
  }

  ParallelMove* GenerateRandomSwaps(int size) {
    ParallelMove* parallel_move = main_zone()->New<ParallelMove>(main_zone());

    for (int i = 0; i < size;) {
      MachineRepresentation rep = CreateRandomMachineRepresentation();
      InstructionOperand lhs =
          CreateRandomOperand(kCannotBeConstant, rep, kOutput);
      InstructionOperand rhs =
          CreateRandomOperand(kCannotBeConstant, rep, kInput);
      MoveOperands mo(lhs, rhs);
      // It isn't valid to call `AssembleMove` and `AssembleSwap` with redundant
      // moves.
      if (mo.IsRedundant()) continue;
      // Canonicalize the swap: the register operand has to be the left hand
      // side.
      if (lhs.IsStackSlot() || lhs.IsFPStackSlot()) {
        std::swap(lhs, rhs);
      }
      parallel_move->AddMove(lhs, rhs);
      // Iterate only when a swap was created.
      i++;
    }

    return parallel_move;
  }

  MachineRepresentation CreateRandomMachineRepresentation() {
    int index = rng_->NextInt(static_cast<int>(supported_reps_.size()));
    return supported_reps_[index];
  }

  InstructionOperand CreateRandomOperand(OperandConstraint constraint,
                                         MachineRepresentation rep,
                                         OperandLifetime operand_lifetime) {
    // Only generate a Constant if the operand is a source and we have a
    // constant with a compatible representation in stock.
    bool generate_constant =
        (constraint != kCannotBeConstant) &&
        (allocated_constants_.find(rep) != allocated_constants_.end());
    switch (rng_->NextInt(generate_constant ? 3 : 2)) {
      case 0:
        return CreateRandomStackSlotOperand(rep, operand_lifetime);
      case 1:
        return CreateRandomRegisterOperand(rep);
      case 2:
        return CreateRandomConstant(rep);
    }
    UNREACHABLE();
  }

  AllocatedOperand CreateRandomRegisterOperand(MachineRepresentation rep) {
    int index =
        rng_->NextInt(static_cast<int>(allocated_registers_[rep].size()));
    return allocated_registers_[rep][index];
  }

  std::map<MachineRepresentation, std::vector<AllocatedOperand>>&
  AllocatedSlotsIn() {
    return allocated_slots_in_;
  }

  std::map<MachineRepresentation, std::vector<AllocatedOperand>>&
  AllocatedSlotsOut() {
    return layout_mode_ == kPreserveLayout ? allocated_slots_in_
                                           : allocated_slots_out_;
  }

  AllocatedOperand CreateRandomStackSlotOperand(
      MachineRepresentation rep,
      std::map<MachineRepresentation, std::vector<AllocatedOperand>>& slots) {
    int index = rng_->NextInt(static_cast<int>(AllocatedSlotsIn()[rep].size()));
    return slots[rep][index];
  }

  AllocatedOperand CreateRandomStackSlotOperand(
      MachineRepresentation rep, OperandLifetime operand_lifetime) {
    return CreateRandomStackSlotOperand(rep, operand_lifetime == kInput
                                                 ? AllocatedSlotsIn()
                                                 : AllocatedSlotsOut());
  }

  ConstantOperand CreateRandomConstant(MachineRepresentation rep) {
    int index =
        rng_->NextInt(static_cast<int>(allocated_constants_[rep].size()));
    return allocated_constants_[rep][index];
  }

  static InstructionBlock* NewBlock(Zone* zone, RpoNumber rpo) {
    return zone->New<InstructionBlock>(zone, rpo, RpoNumber::Invalid(),
                                       RpoNumber::Invalid(),
                                       RpoNumber::Invalid(), false, false);
  }

  v8::base::RandomNumberGenerator* rng() const { return rng_; }
  InstructionSequence* instructions() { return &instructions_; }
  CallDescriptor* test_descriptor() { return test_descriptor_; }
  int stack_slot_count() const { return stack_slot_count_; }
  LayoutMode layout_mode() const { return layout_mode_; }

 private:
  ZoneVector<InstructionBlock*> blocks_;
  InstructionSequence instructions_;
  v8::base::RandomNumberGenerator* rng_;
  // The layout describes the type of each element in the environment, in order.
  const LayoutMode layout_mode_;
  std::vector<AllocatedOperand> setup_layout_;
  std::vector<AllocatedOperand> teardown_layout_;
  CallDescriptor* test_descriptor_;
  CallDescriptor* teardown_descriptor_;
  // Allocated constants, registers and stack slots that we can generate moves
  // with. Each per compatible representation.
  std::vector<MachineRepresentation> supported_reps_;
  std::map<MachineRepresentation, std::vector<ConstantOperand>>
      allocated_constants_;
  std::map<MachineRepresentation, std::vector<AllocatedOperand>>
      allocated_registers_;
  std::map<MachineRepresentation, std::vector<AllocatedOperand>>
      allocated_slots_in_;
  std::map<MachineRepresentation, std::vector<AllocatedOperand>>
      allocated_slots_out_;
  int stack_slot_count_;
};

// static
constexpr int TestEnvironment::kGeneralRegisterCount;
constexpr int TestEnvironment::kDoubleRegisterCount;
constexpr int TestEnvironment::kTaggedSlotCount;
constexpr int TestEnvironment::kFloat32SlotCount;
constexpr int TestEnvironment::kFloat64SlotCount;
constexpr int TestEnvironment::kSimd128SlotCount;
constexpr int TestEnvironment::kSmiConstantCount;
constexpr int TestEnvironment::kFloatConstantCount;
constexpr int TestEnvironment::kDoubleConstantCount;

// Wrapper around the CodeGenerator. Code generated by this can
// only be called using the given `TestEnvironment`.
class CodeGeneratorTester {
 public:
  explicit CodeGeneratorTester(TestEnvironment* environment,
                               int extra_stack_space = 0)
      : zone_(environment->main_zone()),
        info_(base::ArrayVector("test"), environment->main_zone(),
              CodeKind::FOR_TESTING),
        linkage_(environment->test_descriptor()),
        frame_(environment->test_descriptor()->CalculateFixedFrameSize(
                   CodeKind::FOR_TESTING),
               environment->main_zone()) {
    // Pick half of the stack parameters at random and move them into spill
    // slots, separated by `extra_stack_space` bytes.
    // When testing a move with stack slots using CheckAssembleMove or
    // CheckAssembleSwap, we'll transparently make use of local spill slots
    // instead of stack parameters for those that were picked. This allows us to
    // test negative, positive, far and near ranges.
    if (environment->layout_mode() == kPreserveLayout) {
      for (int i = 0; i < (environment->stack_slot_count() / 2);) {
        MachineRepresentation rep =
            environment->CreateRandomMachineRepresentation();
        LocationOperand old_slot = LocationOperand::cast(
            environment->CreateRandomStackSlotOperand(rep, kInput));
        // Do not pick the same slot twice.
        if (GetSpillSlot(&old_slot) != spill_slots_.end()) {
          continue;
        }
        LocationOperand new_slot =
            AllocatedOperand(LocationOperand::STACK_SLOT, rep,
                             frame_.AllocateSpillSlot(GetSlotSizeInBytes(rep)));
        // Artificially create space on the stack by allocating a new slot.
        if (extra_stack_space > 0) {
          frame_.AllocateSpillSlot(extra_stack_space);
        }
        spill_slots_.emplace_back(old_slot, new_slot);
        i++;
      }
    }

    constexpr size_t kMaxUnoptimizedFrameHeight = 0;
    constexpr size_t kMaxPushedArgumentCount = 0;
    generator_ = new CodeGenerator(
        environment->main_zone(), &frame_, &linkage_,
        environment->instructions(), &info_, environment->main_isolate(),
        base::Optional<OsrHelper>(), kNoSourcePosition, nullptr,
        AssemblerOptions::Default(environment->main_isolate()),
        Builtin::kNoBuiltinId, kMaxUnoptimizedFrameHeight,
        kMaxPushedArgumentCount);

    generator_->masm()->CodeEntry();

    // Force a frame to be created.
    generator_->frame_access_state()->MarkHasFrame(true);
    generator_->AssembleConstructFrame();
    // TODO(all): Generate a stack check here so that we fail gracefully if the
    // frame is too big.

    // Move chosen stack parameters into spill slots.
    for (auto move : spill_slots_) {
      generator_->AssembleMove(&move.first, &move.second);
    }
  }

  ~CodeGeneratorTester() { delete generator_; }

  std::vector<std::pair<LocationOperand, LocationOperand>>::iterator
  GetSpillSlot(InstructionOperand* op) {
    if (op->IsAnyStackSlot()) {
      LocationOperand slot = LocationOperand::cast(*op);
      return std::find_if(
          spill_slots_.begin(), spill_slots_.end(),
          [slot](
              const std::pair<LocationOperand, LocationOperand>& moved_pair) {
            return moved_pair.first.index() == slot.index();
          });
    } else {
      return spill_slots_.end();
    }
  }

  // If the operand corresponds to a spill slot, return it. Else just pass it
  // through.
  InstructionOperand* MaybeTranslateSlot(InstructionOperand* op) {
    auto it = GetSpillSlot(op);
    if (it != spill_slots_.end()) {
      // The second element is the spill slot associated with op.
      return &it->second;
    } else {
      return op;
    }
  }

  Instruction* CreateTailCall(int stack_slot_delta) {
    int optional_padding_slot = stack_slot_delta;
    InstructionOperand callee[] = {
        AllocatedOperand(LocationOperand::REGISTER,
                         MachineRepresentation::kTagged,
                         kReturnRegister0.code()),
        ImmediateOperand(ImmediateOperand::INLINE_INT32, optional_padding_slot),
        ImmediateOperand(ImmediateOperand::INLINE_INT32, stack_slot_delta)};
    Instruction* tail_call =
        Instruction::New(zone_, kArchTailCallCodeObject, 0, nullptr,
                         arraysize(callee), callee, 0, nullptr);
    return tail_call;
  }

  enum PushTypeFlag {
    kRegisterPush = CodeGenerator::kRegisterPush,
    kStackSlotPush = CodeGenerator::kStackSlotPush,
    kScalarPush = CodeGenerator::kScalarPush
  };

  void CheckAssembleTailCallGaps(Instruction* instr,
                                 int first_unused_stack_slot,
                                 CodeGeneratorTester::PushTypeFlag push_type) {
    generator_->AssembleTailCallBeforeGap(instr, first_unused_stack_slot);
#if defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_S390) || \
    defined(V8_TARGET_ARCH_PPC) || defined(V8_TARGET_ARCH_PPC64)
    // Only folding register pushes is supported on ARM.
    bool supported =
        ((int{push_type} & CodeGenerator::kRegisterPush) == push_type);
#elif defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_IA32)
    bool supported =
        ((int{push_type} & CodeGenerator::kScalarPush) == push_type);
#else
    bool supported = false;
#endif
    if (supported) {
      // Architectures supporting folding adjacent pushes should now have
      // resolved all moves.
      for (const auto& move :
           *instr->parallel_moves()[Instruction::FIRST_GAP_POSITION]) {
        CHECK(move->IsEliminated());
      }
    }
    generator_->AssembleGaps(instr);
    generator_->AssembleTailCallAfterGap(instr, first_unused_stack_slot);
  }

  void CheckAssembleMove(InstructionOperand* source,
                         InstructionOperand* destination) {
    int start = generator_->masm()->pc_offset();
    generator_->AssembleMove(MaybeTranslateSlot(source),
                             MaybeTranslateSlot(destination));
    CHECK(generator_->masm()->pc_offset() > start);
  }

  void CheckAssembleMoves(ParallelMove* moves) {
    for (auto m : *moves) {
      m->set_source(*MaybeTranslateSlot(&m->source()));
      m->set_destination(*MaybeTranslateSlot(&m->destination()));
    }
    generator_->resolver()->Resolve(moves);
  }

  void CheckAssembleSwap(InstructionOperand* source,
                         InstructionOperand* destination) {
    int start = generator_->masm()->pc_offset();
    generator_->AssembleSwap(MaybeTranslateSlot(source),
                             MaybeTranslateSlot(destination));
    CHECK(generator_->masm()->pc_offset() > start);
  }

  Handle<Code> Finalize() {
    generator_->FinishCode();
    generator_->safepoints()->Emit(generator_->masm(),
                                   frame_.GetTotalFrameSlotCount());
    generator_->MaybeEmitOutOfLineConstantPool();

    return generator_->FinalizeCode().ToHandleChecked();
  }

  Handle<Code> FinalizeForExecuting() {
    // The test environment expects us to have performed moves on stack
    // parameters. However, some of them are mapped to local spill slots. They
    // should be moved back into stack parameters so their values are passed
    // along to the `teardown` function.
    for (auto move : spill_slots_) {
      generator_->AssembleMove(&move.second, &move.first);
    }

    InstructionSequence* sequence = generator_->instructions();

    sequence->StartBlock(RpoNumber::FromInt(0));
    // The environment expects this code to tail-call to it's first parameter
    // placed in `kReturnRegister0`.
    sequence->AddInstruction(Instruction::New(zone_, kArchPrepareTailCall));

    // We use either zero or one slots.
    static constexpr int first_unused_stack_slot = kReturnAddressStackSlotCount;
    int optional_padding_slot = first_unused_stack_slot;
    InstructionOperand callee[] = {
        AllocatedOperand(LocationOperand::REGISTER,
                         MachineRepresentation::kTagged,
                         kReturnRegister0.code()),
        ImmediateOperand(
            ImmediateOperand::INLINE_INT32,
            (kDefaultCodeEntrypointTag >> kCodeEntrypointTagShift)),
        ImmediateOperand(ImmediateOperand::INLINE_INT32, optional_padding_slot),
        ImmediateOperand(ImmediateOperand::INLINE_INT32,
                         first_unused_stack_slot)};
    Instruction* tail_call =
        Instruction::New(zone_, kArchTailCallCodeObject, 0, nullptr,
                         arraysize(callee), callee, 0, nullptr);
    sequence->AddInstruction(tail_call);
    sequence->EndBlock(RpoNumber::FromInt(0));

    generator_->AssembleBlock(
        sequence->InstructionBlockAt(RpoNumber::FromInt(0)));

    return Finalize();
  }

 private:
  Zone* zone_;
  OptimizedCompilationInfo info_;
  Linkage linkage_;
  Frame frame_;
  CodeGenerator* generator_;
  // List of operands to be moved from stack parameters to spill slots.
  std::vector<std::pair<LocationOperand, LocationOperand>> spill_slots_;
};

// The following fuzz tests will assemble a lot of moves, wrap them in
// executable native code and run them. In order to check that moves were
// performed correctly, we need to setup an environment with an initial state
// and get it back after the list of moves were performed.
//
// We have two components to do this: TestEnvironment and CodeGeneratorTester.
//
// The TestEnvironment is in charge of bringing up an environment consisting of
// a set of registers, stack slots and constants, with initial values in
// them. The CodeGeneratorTester is a wrapper around the CodeGenerator and its
// only purpose is to generate code for a list of moves. The TestEnvironment is
// then able to run this code against the environment and return a resulting
// state.
//
// A "state" here is a packed FixedArray with tagged values which can either be
// Smis or HeapNumbers. When calling TestEnvironment::Run(...), registers and
// stack slots will be initialised according to this FixedArray. A new
// FixedArray is returned containing values that were moved by the generated
// code.
//
// And finally, we are able to compare the resulting FixedArray against a
// reference, computed with a simulation of AssembleMove and AssembleSwap. See
// SimulateSequentialMoves, SimulateParallelMoves and SimulateSwaps.

// Allocate space between slots to increase coverage of moves with larger
// ranges. Note that this affects how much stack is allocated when running the
// generated code. It means we have to be careful not to exceed the stack limit,
// which is lower on Windows.
#ifdef V8_OS_WIN
constexpr int kExtraSpace = 0;
#else
constexpr int kExtraSpace = 1 * KB;
#endif

TEST(FuzzAssembleMove) {
  TestEnvironment env;

  Handle<FixedArray> state_in = env.GenerateInitialState();
  ParallelMove* moves = env.GenerateRandomMoves(1000, kSequentialMoves);

  Handle<FixedArray> expected = env.SimulateSequentialMoves(moves, state_in);

  // Test small and potentially large ranges separately.
  for (int extra_space : {0, kExtraSpace}) {
    CodeGeneratorTester c(&env, extra_space);

    for (auto m : *moves) {
      c.CheckAssembleMove(&m->source(), &m->destination());
    }

    Handle<Code> test = c.FinalizeForExecuting();
    if (v8_flags.print_code) {
      Print(*test);
    }

    Handle<FixedArray> actual = env.Run(test, state_in);
    env.CheckState(actual, expected);
  }
}

// Test integration with the gap resolver by resolving parallel moves first.
TEST(FuzzAssembleParallelMove) {
  TestEnvironment env(kChangeLayout);

  Handle<FixedArray> state_in = env.GenerateInitialState();
  ParallelMove* moves = env.GenerateRandomParallelMoves();
  Handle<FixedArray> state_out = env.SimulateParallelMoves(moves, state_in);

  CodeGeneratorTester c(&env);

  // The gap resolver modifies the parallel move in-place. Copy and restore
  // it after assembling.
  c.CheckAssembleMoves(moves);

  Handle<Code> test = c.FinalizeForExecuting();
  if (v8_flags.print_code) {
    Print(*test);
  }

  Handle<FixedArray> actual = env.Run(test, state_in);
  env.CheckState(actual, state_out);
}

TEST(FuzzAssembleSwap) {
  TestEnvironment env;

  Handle<FixedArray> state_in = env.GenerateInitialState();
  ParallelMove* swaps = env.GenerateRandomSwaps(1000);

  Handle<FixedArray> expected = env.SimulateSwaps(swaps, state_in);

  // Test small and potentially large ranges separately.
  for (int extra_space : {0, kExtraSpace}) {
    CodeGeneratorTester c(&env, extra_space);

    for (auto s : *swaps) {
      c.CheckAssembleSwap(&s->source(), &s->destination());
    }

    Handle<Code> test = c.FinalizeForExecuting();
    if (v8_flags.print_code) {
      Print(*test);
    }

    Handle<FixedArray> actual = env.Run(test, state_in);
    env.CheckState(actual, expected);
  }
}

TEST(FuzzAssembleMoveAndSwap) {
  TestEnvironment env;

  Handle<FixedArray> state_in = env.GenerateInitialState();
  Handle<FixedArray> expected =
      env.main_isolate()->factory()->NewFixedArray(state_in->length());

  // Test small and potentially large ranges separately.
  for (int extra_space : {0, kExtraSpace}) {
    CodeGeneratorTester c(&env, extra_space);

    FixedArray::CopyElements(env.main_isolate(), *expected, 0, *state_in, 0,
                             state_in->length());

    for (int i = 0; i < 1000; i++) {
      // Randomly alternate between swaps and moves.
      if (env.rng()->NextInt(2) == 0) {
        ParallelMove* move = env.GenerateRandomMoves(1, kSequentialMoves);
        expected = env.SimulateSequentialMoves(move, expected);
        c.CheckAssembleMove(&move->at(0)->source(),
                            &move->at(0)->destination());
      } else {
        ParallelMove* swap = env.GenerateRandomSwaps(1);
        expected = env.SimulateSwaps(swap, expected);
        c.CheckAssembleSwap(&swap->at(0)->source(),
                            &swap->at(0)->destination());
      }
    }

    Handle<Code> test = c.FinalizeForExecuting();
    if (v8_flags.print_code) {
      Print(*test);
    }

    Handle<FixedArray> actual = env.Run(test, state_in);
    env.CheckState(actual, expected);
  }
}

TEST(AssembleTailCallGap) {
  const RegisterConfiguration* conf = GetRegConfig();
  TestEnvironment env;

  // This test assumes at least 4 registers are allocatable.
  CHECK_LE(4, conf->num_allocatable_general_registers());

  auto r0 = AllocatedOperand(LocationOperand::REGISTER,
                             MachineRepresentation::kTagged,
                             conf->GetAllocatableGeneralCode(0));
  auto r1 = AllocatedOperand(LocationOperand::REGISTER,
                             MachineRepresentation::kTagged,
                             conf->GetAllocatableGeneralCode(1));
  auto r2 = AllocatedOperand(LocationOperand::REGISTER,
                             MachineRepresentation::kTagged,
                             conf->GetAllocatableGeneralCode(2));
  auto r3 = AllocatedOperand(LocationOperand::REGISTER,
                             MachineRepresentation::kTagged,
                             conf->GetAllocatableGeneralCode(3));

  auto slot_minus_4 = AllocatedOperand(LocationOperand::STACK_SLOT,
                                       MachineRepresentation::kTagged, -4);
  auto slot_minus_3 = AllocatedOperand(LocationOperand::STACK_SLOT,
                                       MachineRepresentation::kTagged, -3);
  auto slot_minus_2 = AllocatedOperand(LocationOperand::STACK_SLOT,
                                       MachineRepresentation::kTagged, -2);
  auto slot_minus_1 = AllocatedOperand(LocationOperand::STACK_SLOT,
                                       MachineRepresentation::kTagged, -1);

  // Avoid slot 0 for architectures which use it store the return address.
  static constexpr int first_slot = kReturnAddressStackSlotCount;
  auto slot_0 = AllocatedOperand(LocationOperand::STACK_SLOT,
                                 MachineRepresentation::kTagged, first_slot);
  auto slot_1 =
      AllocatedOperand(LocationOperand::STACK_SLOT,
                       MachineRepresentation::kTagged, first_slot + 1);
  auto slot_2 =
      AllocatedOperand(LocationOperand::STACK_SLOT,
                       MachineRepresentation::kTagged, first_slot + 2);
  auto slot_3 =
      AllocatedOperand(LocationOperand::STACK_SLOT,
                       MachineRepresentation::kTagged, first_slot + 3);

  // These tests all generate series of moves that the code generator should
  // detect as adjacent pushes. Depending on the architecture, we make sure
  // these moves get eliminated.
  // Also, disassembling with `--print-code` is useful when debugging.

  {
    // Generate a series of register pushes only.
    CodeGeneratorTester c(&env);
    Instruction* instr = c.CreateTailCall(first_slot + 4);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r3, slot_0);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r2, slot_1);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r1, slot_2);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r0, slot_3);

    c.CheckAssembleTailCallGaps(instr, first_slot + 4,
                                CodeGeneratorTester::kRegisterPush);
    Handle<Code> code = c.Finalize();
    if (v8_flags.print_code) {
      Print(*code);
    }
  }

  {
    // Generate a series of stack pushes only.
    CodeGeneratorTester c(&env);
    Instruction* instr = c.CreateTailCall(first_slot + 4);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_4, slot_0);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_3, slot_1);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_2, slot_2);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_1, slot_3);

    c.CheckAssembleTailCallGaps(instr, first_slot + 4,
                                CodeGeneratorTester::kStackSlotPush);
    Handle<Code> code = c.Finalize();
    if (v8_flags.print_code) {
      Print(*code);
    }
  }

  {
    // Generate a mix of stack and register pushes.
    CodeGeneratorTester c(&env);
    Instruction* instr = c.CreateTailCall(first_slot + 4);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_2, slot_0);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r1, slot_1);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(slot_minus_1, slot_2);
    instr
        ->GetOrCreateParallelMove(Instruction::FIRST_GAP_POSITION,
                                  env.main_zone())
        ->AddMove(r0, slot_3);

    c.CheckAssembleTailCallGaps(instr, first_slot + 4,
                                CodeGeneratorTester::kScalarPush);
    Handle<Code> code = c.Finalize();
    if (v8_flags.print_code) {
      Print(*code);
    }
  }
}

#if V8_ENABLE_WEBASSEMBLY
namespace {

std::shared_ptr<wasm::NativeModule> AllocateNativeModule(Isolate* isolate,
                                                         size_t code_size) {
  std::shared_ptr<wasm::WasmModule> module(new wasm::WasmModule());
  module->num_declared_functions = 1;
  // We have to add the code object to a NativeModule, because the
  // WasmCallDescriptor assumes that code is on the native heap and not
  // within a code object.
  auto native_module = wasm::GetWasmEngine()->NewNativeModule(
      isolate, wasm::WasmFeatures::All(), wasm::CompileTimeImports{},
      std::move(module), code_size);
  native_module->SetWireBytes({});
  return native_module;
}

}  // namespace

// Test stack argument pushing with some gaps that require stack pointer
// adjustment.
TEST(Regress_1171759) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  // Create a minimal callee with enlough parameters to exhaust parameter
  // registers and force some stack parameters.
  constexpr int kDoubleParams = 16;
  // These are followed by a single, and another double to create a gap.
  constexpr int kTotalParams = kDoubleParams + 2;

  wasm::FunctionSig::Builder builder(&zone, 1, kTotalParams);
  // Make the first parameter slots double width.
  for (int i = 0; i < kDoubleParams; i++) {
    builder.AddParam(wasm::ValueType::For(MachineType::Float64()));
  }
  // Allocate a single parameter.
  builder.AddParam(wasm::ValueType::For(MachineType::Float32()));
  // Allocate a double parameter which should create a stack gap.
  builder.AddParam(wasm::ValueType::For(MachineType::Float64()));

  builder.AddReturn(wasm::ValueType::For(MachineType::Int32()));

  CallDescriptor* desc =
      compiler::GetWasmCallDescriptor(&zone, builder.Build());

  HandleAndZoneScope handles(kCompressGraphZone);
  RawMachineAssembler m(handles.main_isolate(),
                        handles.main_zone()->New<Graph>(handles.main_zone()),
                        desc, MachineType::PointerRepresentation(),
                        InstructionSelector::SupportedMachineOperatorFlags());

  m.Return(m.Int32Constant(0));

  OptimizedCompilationInfo info(base::ArrayVector("testing"),
                                handles.main_zone(), CodeKind::WASM_FUNCTION);
  Handle<Code> code =
      Pipeline::GenerateCodeForTesting(
          &info, handles.main_isolate(), desc, m.graph(),
          AssemblerOptions::Default(handles.main_isolate()), m.ExportForTest())
          .ToHandleChecked();

  std::shared_ptr<wasm::NativeModule> module =
      AllocateNativeModule(handles.main_isolate(), code->instruction_size());
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  uint8_t* code_start = module->AddCodeForTesting(code)->instructions().begin();

  // Generate a minimal calling function, to push stack arguments.
  RawMachineAssemblerTester<int32_t> mt;
  Node* function = mt.PointerConstant(code_start);
  Node* dummy_context = mt.PointerConstant(nullptr);
  Node* double_slot = mt.Float64Constant(0);
  Node* single_slot_that_creates_gap = mt.Float32Constant(0);
  Node* call_inputs[] = {function,
                         dummy_context,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         double_slot,
                         single_slot_that_creates_gap,
                         double_slot};

  Node* call =
      mt.AddNode(mt.common()->Call(desc), 2 + kTotalParams, call_inputs);

  mt.Return(call);

  CHECK_EQ(0, mt.Call());
}
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace compiler
}  // namespace internal
}  // namespace v8
