// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/objects/code-inl.h"
#include "test/common/code-assembler-tester.h"
#include "test/unittests/compiler/function-tester.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

#define __ assembler.

namespace {

// Function that takes a number of pointer-sized integer arguments, calculates a
// weighted sum of them and returns it.
Handle<Code> BuildCallee(Isolate* isolate, CallDescriptor* call_descriptor) {
  CodeAssemblerTester tester(isolate, call_descriptor, "callee");
  CodeStubAssembler assembler(tester.state());
  int param_slots = static_cast<int>(call_descriptor->ParameterSlotCount());
  TNode<IntPtrT> sum = __ IntPtrConstant(0);
  for (int i = 0; i < param_slots; ++i) {
    TNode<WordT> product = __ IntPtrMul(__ UncheckedParameter<IntPtrT>(i),
                                        __ IntPtrConstant(i + 1));
    sum = __ Signed(__ IntPtrAdd(sum, product));
  }
  __ Return(sum);
  return tester.GenerateCodeCloseAndEscape();
}

// Function that tail-calls another function with a number of pointer-sized
// integer arguments.
Handle<Code> BuildCaller(Isolate* isolate, CallDescriptor* call_descriptor,
                         CallDescriptor* callee_descriptor) {
  CodeAssemblerTester tester(isolate, call_descriptor, "caller");
  CodeStubAssembler assembler(tester.state());
  std::vector<Node*> params;
  // The first parameter is always the callee.
  Handle<Code> code = BuildCallee(isolate, callee_descriptor);
  params.push_back(__ HeapConstantNoHole(code));
  int param_slots = static_cast<int>(callee_descriptor->ParameterSlotCount());
  for (int i = 0; i < param_slots; ++i) {
    params.push_back(__ IntPtrConstant(i));
  }
  DCHECK_EQ(param_slots + 1, params.size());
  tester.raw_assembler_for_testing()->TailCallN(callee_descriptor,
                                                param_slots + 1, params.data());
  return tester.GenerateCodeCloseAndEscape();
}

// Setup function, which calls "caller".
Handle<Code> BuildSetupFunction(Isolate* isolate,
                                CallDescriptor* caller_descriptor,
                                CallDescriptor* callee_descriptor) {
  CodeAssemblerTester tester(isolate, JSParameterCount(0));
  CodeStubAssembler assembler(tester.state());
  std::vector<Node*> params;
  // The first parameter is always the callee.
  Handle<Code> code =
      BuildCaller(isolate, caller_descriptor, callee_descriptor);
  params.push_back(__ HeapConstantNoHole(code));
  // Set up arguments for "Caller".
  int param_slots = static_cast<int>(caller_descriptor->ParameterSlotCount());
  for (int i = 0; i < param_slots; ++i) {
    // Use values that are different from the ones we will pass to this
    // function's callee later.
    params.push_back(__ IntPtrConstant(i + 42));
  }
  DCHECK_EQ(param_slots + 1, params.size());
  TNode<IntPtrT> intptr_result =
      __ UncheckedCast<IntPtrT>(tester.raw_assembler_for_testing()->CallN(
          caller_descriptor, param_slots + 1, params.data()));
  __ Return(__ SmiTag(intptr_result));
  return tester.GenerateCodeCloseAndEscape();
}

CallDescriptor* CreateDescriptorForStackArguments(Zone* zone, int param_slots) {
  LocationSignature::Builder locations(zone, 1,
                                       static_cast<size_t>(param_slots));

  locations.AddReturn(LinkageLocation::ForRegister(kReturnRegister0.code(),
                                                   MachineType::IntPtr()));

  for (int i = 0; i < param_slots; ++i) {
    locations.AddParam(LinkageLocation::ForCallerFrameSlot(
        i - param_slots, MachineType::IntPtr()));
  }

  return zone->New<CallDescriptor>(
      CallDescriptor::kCallCodeObject,  // kind
      kDefaultCodeEntrypointTag,        // tag
      MachineType::AnyTagged(),         // target MachineType
      LinkageLocation::ForAnyRegister(
          MachineType::AnyTagged()),  // target location
      locations.Build(),              // location_sig
      param_slots,                    // stack parameter slots
      Operator::kNoProperties,        // properties
      kNoCalleeSaved,                 // callee-saved registers
      kNoCalleeSavedFp,               // callee-saved fp
      CallDescriptor::kNoFlags);      // flags
}

}  // namespace

class RunTailCallsTest : public TestWithContextAndZone {
 protected:
  // Test a tail call from a caller with n parameters to a callee with m
  // parameters. All parameters are pointer-sized.
  void TestHelper(int n, int m) {
    Isolate* isolate = i_isolate();
    CallDescriptor* caller_descriptor =
        CreateDescriptorForStackArguments(zone(), n);
    CallDescriptor* callee_descriptor =
        CreateDescriptorForStackArguments(zone(), m);
    Handle<Code> setup =
        BuildSetupFunction(isolate, caller_descriptor, callee_descriptor);
    FunctionTester ft(isolate, setup, 0);
    DirectHandle<Object> result = ft.Call().ToHandleChecked();
    int expected = 0;
    for (int i = 0; i < m; ++i) expected += (i + 1) * i;
    CHECK_EQ(expected, Cast<Smi>(*result).value());
  }
};

#undef __

TEST_F(RunTailCallsTest, CallerOddCalleeEven) {
  TestHelper(1, 0);
  TestHelper(1, 2);
  TestHelper(3, 2);
  TestHelper(3, 4);
}

TEST_F(RunTailCallsTest, CallerOddCalleeOdd) {
  TestHelper(1, 1);
  TestHelper(1, 3);
  TestHelper(3, 1);
  TestHelper(3, 3);
}

TEST_F(RunTailCallsTest, CallerEvenCalleeEven) {
  TestHelper(0, 0);
  TestHelper(0, 2);
  TestHelper(2, 0);
  TestHelper(2, 2);
}

TEST_F(RunTailCallsTest, CallerEvenCalleeOdd) {
  TestHelper(0, 1);
  TestHelper(0, 3);
  TestHelper(2, 1);
  TestHelper(2, 3);
}

TEST_F(RunTailCallsTest, FuzzStackParamCount) {
  const int kNumTests = 20;
  const int kMaxSlots = 30;
  base::RandomNumberGenerator* const rng =
      i_isolate()->random_number_generator();
  for (int i = 0; i < kNumTests; ++i) {
    int n = rng->NextInt(kMaxSlots);
    int m = rng->NextInt(kMaxSlots);
    TestHelper(n, m);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
