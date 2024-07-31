// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <functional>
#include <limits>
#include <memory>

#include "src/base/bits.h"
#include "src/codegen/assembler.h"
#include "src/codegen/compiler.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
#include "src/compiler/linkage.h"
#include "src/compiler/wasm-compiler.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

CallDescriptor* CreateCallDescriptor(Zone* zone, int return_count,
                                     int param_count, MachineType type) {
  wasm::FunctionSig::Builder builder(zone, return_count, param_count);

  for (int i = 0; i < param_count; i++) {
    builder.AddParam(wasm::ValueType::For(type));
  }

  for (int i = 0; i < return_count; i++) {
    builder.AddReturn(wasm::ValueType::For(type));
  }
  return compiler::GetWasmCallDescriptor(zone, builder.Build());
}

Node* MakeConstant(RawMachineAssembler* m, MachineType type, int value) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m->Int32Constant(static_cast<int32_t>(value));
    case MachineRepresentation::kWord64:
      return m->Int64Constant(static_cast<int64_t>(value));
    case MachineRepresentation::kFloat32:
      return m->Float32Constant(static_cast<float>(value));
    case MachineRepresentation::kFloat64:
      return m->Float64Constant(static_cast<double>(value));
    default:
      UNREACHABLE();
  }
}

Node* Add(RawMachineAssembler* m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m->Int32Add(a, b);
    case MachineRepresentation::kWord64:
      return m->Int64Add(a, b);
    case MachineRepresentation::kFloat32:
      return m->Float32Add(a, b);
    case MachineRepresentation::kFloat64:
      return m->Float64Add(a, b);
    default:
      UNREACHABLE();
  }
}

Node* Sub(RawMachineAssembler* m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m->Int32Sub(a, b);
    case MachineRepresentation::kWord64:
      return m->Int64Sub(a, b);
    case MachineRepresentation::kFloat32:
      return m->Float32Sub(a, b);
    case MachineRepresentation::kFloat64:
      return m->Float64Sub(a, b);
    default:
      UNREACHABLE();
  }
}

Node* Mul(RawMachineAssembler* m, MachineType type, Node* a, Node* b) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return m->Int32Mul(a, b);
    case MachineRepresentation::kWord64:
      return m->Int64Mul(a, b);
    case MachineRepresentation::kFloat32:
      return m->Float32Mul(a, b);
    case MachineRepresentation::kFloat64:
      return m->Float64Mul(a, b);
    default:
      UNREACHABLE();
  }
}

Node* ToInt32(RawMachineAssembler* m, MachineType type, Node* a) {
  switch (type.representation()) {
    case MachineRepresentation::kWord32:
      return a;
    case MachineRepresentation::kWord64:
      return m->TruncateInt64ToInt32(a);
    case MachineRepresentation::kFloat32:
      return m->TruncateFloat32ToInt32(a, TruncateKind::kArchitectureDefault);
    case MachineRepresentation::kFloat64:
      return m->RoundFloat64ToInt32(a);
    default:
      UNREACHABLE();
  }
}

std::shared_ptr<wasm::NativeModule> AllocateNativeModule(Isolate* isolate,
                                                         size_t code_size) {
  auto module = std::make_shared<wasm::WasmModule>(wasm::kWasmOrigin);
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

template <int kMinParamCount, int kMaxParamCount>
void TestReturnMultipleValues(MachineType type, int min_count, int max_count) {
  for (int param_count : {kMinParamCount, kMaxParamCount}) {
    for (int count = min_count; count < max_count; ++count) {
      printf("\n==== type = %s, parameter_count = %d, count = %d ====\n\n\n",
             MachineReprToString(type.representation()), param_count, count);
      v8::internal::AccountingAllocator allocator;
      Zone zone(&allocator, ZONE_NAME);
      CallDescriptor* desc =
          CreateCallDescriptor(&zone, count, param_count, type);
      HandleAndZoneScope handles(kCompressGraphZone);
      RawMachineAssembler m(
          handles.main_isolate(),
          handles.main_zone()->New<Graph>(handles.main_zone()), desc,
          MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags());

      // m.Parameter(0) is the WasmContext.
      Node* p0 = m.Parameter(1);
      Node* p1 = m.Parameter(2);
      using Node_ptr = Node*;
      std::unique_ptr<Node_ptr[]> returns(new Node_ptr[count]);
      for (int i = 0; i < count; ++i) {
        if (i % 3 == 0) returns[i] = Add(&m, type, p0, p1);
        if (i % 3 == 1) returns[i] = Sub(&m, type, p0, p1);
        if (i % 3 == 2) returns[i] = Mul(&m, type, p0, p1);
      }
      m.Return(count, returns.get());

      OptimizedCompilationInfo info(base::ArrayVector("testing"),
                                    handles.main_zone(),
                                    CodeKind::WASM_FUNCTION);
      Handle<Code> code = Pipeline::GenerateCodeForTesting(
                              &info, handles.main_isolate(), desc, m.graph(),
                              AssemblerOptions::Default(handles.main_isolate()),
                              m.ExportForTest())
                              .ToHandleChecked();
#ifdef ENABLE_DISASSEMBLER
      if (v8_flags.print_code) {
        StdoutStream os;
        code->Disassemble("multi_value", os, handles.main_isolate());
      }
#endif

      const int a = 47, b = 12;
      int expect = 0;
      for (int i = 0, sign = +1; i < count; ++i) {
        if (i % 3 == 0) expect += sign * (a + b);
        if (i % 3 == 1) expect += sign * (a - b);
        if (i % 3 == 2) expect += sign * (a * b);
        if (i % 4 == 0) sign = -sign;
      }

      std::shared_ptr<wasm::NativeModule> module = AllocateNativeModule(
          handles.main_isolate(), code->instruction_size());
      wasm::WasmCodeRefScope wasm_code_ref_scope;
      uint8_t* code_start =
          module->AddCodeForTesting(code)->instructions().begin();

      RawMachineAssemblerTester<int32_t> mt(CodeKind::JS_TO_WASM_FUNCTION);
      const int input_count = 2 + param_count;
      Node* call_inputs[2 + kMaxParamCount];
      call_inputs[0] = mt.PointerConstant(code_start);
      // WasmContext dummy
      call_inputs[1] = mt.PointerConstant(nullptr);
      // Special inputs for the test.
      call_inputs[2] = MakeConstant(&mt, type, a);
      call_inputs[3] = MakeConstant(&mt, type, b);
      for (int i = 2; i < param_count; i++) {
        call_inputs[2 + i] = MakeConstant(&mt, type, i);
      }

      Node* ret_multi = mt.AddNode(mt.common()->Call(desc),
                                   input_count, call_inputs);
      Node* ret = MakeConstant(&mt, type, 0);
      bool sign = false;
      for (int i = 0; i < count; ++i) {
        Node* x = (count == 1)
                      ? ret_multi
                      : mt.AddNode(mt.common()->Projection(i), ret_multi);
        ret = sign ? Sub(&mt, type, ret, x) : Add(&mt, type, ret, x);
        if (i % 4 == 0) sign = !sign;
      }
      mt.Return(ToInt32(&mt, type, ret));
#ifdef ENABLE_DISASSEMBLER
      if (v8_flags.print_code) {
        StdoutStream os;
        Handle<Code> code2 = mt.GetCode();
        code2->Disassemble("multi_value_call", os, handles.main_isolate());
      }
#endif
      CHECK_EQ(expect, mt.Call());
    }
  }
}

}  // namespace

// Use 9 parameters as a regression test or https://crbug.com/838098.
#define TEST_MULTI(Type, type) \
  TEST(ReturnMultiple##Type) { TestReturnMultipleValues<2, 9>(type, 0, 20); }

// Create a frame larger than UINT16_MAX to force TF to use an extra register
// when popping the frame.
TEST(TestReturnMultipleValuesLargeFrame) {
  TestReturnMultipleValues<20000, 20000>(MachineType::Int32(), 2, 3);
}

TEST_MULTI(Int32, MachineType::Int32())
#if (!V8_TARGET_ARCH_32_BIT)
TEST_MULTI(Int64, MachineType::Int64())
#endif
TEST_MULTI(Float32, MachineType::Float32())
TEST_MULTI(Float64, MachineType::Float64())

#undef TEST_MULTI

void ReturnLastValue(MachineType type) {
  int slot_counts[] = {1, 2, 3, 600};
  for (auto slot_count : slot_counts) {
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    // The wasm-linkage provides 2 return registers at the moment, on all
    // platforms.
    const int return_count = 2 + slot_count;

    CallDescriptor* desc = CreateCallDescriptor(&zone, return_count, 0, type);

    HandleAndZoneScope handles(kCompressGraphZone);
    RawMachineAssembler m(handles.main_isolate(),
                          handles.main_zone()->New<Graph>(handles.main_zone()),
                          desc, MachineType::PointerRepresentation(),
                          InstructionSelector::SupportedMachineOperatorFlags());

    std::unique_ptr<Node* []> returns(new Node*[return_count]);

    for (int i = 0; i < return_count; ++i) {
      returns[i] = MakeConstant(&m, type, i);
    }

    m.Return(return_count, returns.get());

    OptimizedCompilationInfo info(base::ArrayVector("testing"),
                                  handles.main_zone(), CodeKind::WASM_FUNCTION);
    Handle<Code> code = Pipeline::GenerateCodeForTesting(
                            &info, handles.main_isolate(), desc, m.graph(),
                            AssemblerOptions::Default(handles.main_isolate()),
                            m.ExportForTest())
                            .ToHandleChecked();

    std::shared_ptr<wasm::NativeModule> module =
        AllocateNativeModule(handles.main_isolate(), code->instruction_size());
    wasm::WasmCodeRefScope wasm_code_ref_scope;
    uint8_t* code_start =
        module->AddCodeForTesting(code)->instructions().begin();

    // Generate caller.
    int expect = return_count - 1;
    RawMachineAssemblerTester<int32_t> mt;
    Node* inputs[] = {mt.PointerConstant(code_start),
                      // WasmContext dummy
                      mt.PointerConstant(nullptr)};

    Node* call = mt.AddNode(mt.common()->Call(desc), 2, inputs);

    mt.Return(
        ToInt32(&mt, type,
                mt.AddNode(mt.common()->Projection(return_count - 1), call)));

    CHECK_EQ(expect, mt.Call());
  }
}

TEST(ReturnLastValueInt32) { ReturnLastValue(MachineType::Int32()); }
#if (!V8_TARGET_ARCH_32_BIT)
TEST(ReturnLastValueInt64) { ReturnLastValue(MachineType::Int64()); }
#endif
TEST(ReturnLastValueFloat32) { ReturnLastValue(MachineType::Float32()); }
TEST(ReturnLastValueFloat64) { ReturnLastValue(MachineType::Float64()); }

void ReturnSumOfReturns(MachineType type) {
  for (int unused_stack_slots = 0; unused_stack_slots <= 2;
       ++unused_stack_slots) {
    v8::internal::AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    // Let {unused_stack_slots + 1} returns be on the stack.
    // The wasm-linkage provides 2 return registers at the moment, on all
    // platforms.
    const int return_count = 2 + unused_stack_slots + 1;

    CallDescriptor* desc = CreateCallDescriptor(&zone, return_count, 0, type);

    HandleAndZoneScope handles(kCompressGraphZone);
    RawMachineAssembler m(handles.main_isolate(),
                          handles.main_zone()->New<Graph>(handles.main_zone()),
                          desc, MachineType::PointerRepresentation(),
                          InstructionSelector::SupportedMachineOperatorFlags());

    std::unique_ptr<Node* []> returns(new Node*[return_count]);

    for (int i = 0; i < return_count; ++i) {
      returns[i] = MakeConstant(&m, type, i);
    }

    m.Return(return_count, returns.get());

    OptimizedCompilationInfo info(base::ArrayVector("testing"),
                                  handles.main_zone(), CodeKind::WASM_FUNCTION);
    Handle<Code> code = Pipeline::GenerateCodeForTesting(
                            &info, handles.main_isolate(), desc, m.graph(),
                            AssemblerOptions::Default(handles.main_isolate()),
                            m.ExportForTest())
                            .ToHandleChecked();

    std::shared_ptr<wasm::NativeModule> module =
        AllocateNativeModule(handles.main_isolate(), code->instruction_size());
    wasm::WasmCodeRefScope wasm_code_ref_scope;
    uint8_t* code_start =
        module->AddCodeForTesting(code)->instructions().begin();

    // Generate caller.
    RawMachineAssemblerTester<int32_t> mt;
    Node* call_inputs[] = {mt.PointerConstant(code_start),
                           // WasmContext dummy
                           mt.PointerConstant(nullptr)};

    Node* call = mt.AddNode(mt.common()->Call(desc), 2, call_inputs);

    uint32_t expect = 0;
    Node* result = mt.Int32Constant(0);

    for (int i = 0; i < return_count; ++i) {
      expect += i;
      result = mt.Int32Add(
          result,
          ToInt32(&mt, type, mt.AddNode(mt.common()->Projection(i), call)));
    }

    mt.Return(result);

    CHECK_EQ(expect, mt.Call());
  }
}

TEST(ReturnSumOfReturnsInt32) { ReturnSumOfReturns(MachineType::Int32()); }
#if (!V8_TARGET_ARCH_32_BIT)
TEST(ReturnSumOfReturnsInt64) { ReturnSumOfReturns(MachineType::Int64()); }
#endif
TEST(ReturnSumOfReturnsFloat32) { ReturnSumOfReturns(MachineType::Float32()); }
TEST(ReturnSumOfReturnsFloat64) { ReturnSumOfReturns(MachineType::Float64()); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
