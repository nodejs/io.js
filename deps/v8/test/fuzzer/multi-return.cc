// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include "src/codegen/machine-type.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/compiler/wasm-compiler.h"
#include "src/execution/simulator.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/fuzzer/fuzzer-support.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace fuzzer {

constexpr MachineType kTypes[] = {
    // The first entry is just a placeholder, because '0' is a separator.
    MachineType(),
#if !V8_TARGET_ARCH_32_BIT
    MachineType::Int64(),
#endif
    MachineType::Int32(), MachineType::Float32(), MachineType::Float64()};

static constexpr int kNumTypes = arraysize(kTypes);

class InputProvider {
 public:
  InputProvider(const uint8_t* data, size_t size)
      : current_(data), end_(data + size) {}

  size_t NumNonZeroBytes(size_t offset, int limit) {
    DCHECK_LE(limit, std::numeric_limits<uint8_t>::max());
    DCHECK_GE(current_ + offset, current_);
    const uint8_t* p;
    for (p = current_ + offset; p < end_; ++p) {
      if (*p % limit == 0) break;
    }
    return p - current_ - offset;
  }

  int NextInt8(int limit) {
    DCHECK_LE(limit, std::numeric_limits<uint8_t>::max());
    if (current_ == end_) return 0;
    uint8_t result = *current_;
    current_++;
    return static_cast<int>(result) % limit;
  }

  int NextInt32(int limit) {
    if (current_ + sizeof(uint32_t) > end_) return 0;
    int result =
        base::ReadLittleEndianValue<int>(reinterpret_cast<Address>(current_));
    current_ += sizeof(uint32_t);
    return result % limit;
  }

 private:
  const uint8_t* current_;
  const uint8_t* end_;
};

MachineType RandomType(InputProvider* input) {
  return kTypes[input->NextInt8(kNumTypes)];
}

int index(MachineType type) { return static_cast<int>(type.representation()); }

Node* Constant(RawMachineAssembler* m, MachineType type, int value) {
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

CallDescriptor* CreateRandomCallDescriptor(Zone* zone, size_t return_count,
                                           size_t param_count,
                                           InputProvider* input) {
  wasm::FunctionSig::Builder builder(zone, return_count, param_count);
  for (size_t i = 0; i < param_count; i++) {
    MachineType type = RandomType(input);
    builder.AddParam(wasm::ValueType::For(type));
  }
  // Read the end byte of the parameters.
  input->NextInt8(1);

  for (size_t i = 0; i < return_count; i++) {
    MachineType type = RandomType(input);
    builder.AddReturn(wasm::ValueType::For(type));
  }

  return compiler::GetWasmCallDescriptor(zone, builder.Build());
}

std::shared_ptr<wasm::NativeModule> AllocateNativeModule(i::Isolate* isolate,
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());
  v8::TryCatch try_catch(isolate);
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  InputProvider input(data, size);
  // Create randomized descriptor.
  size_t param_count = input.NumNonZeroBytes(0, kNumTypes);
  if (param_count > wasm::kV8MaxWasmFunctionParams) return 0;

  size_t return_count = input.NumNonZeroBytes(param_count + 1, kNumTypes);
  if (return_count > wasm::kV8MaxWasmFunctionReturns) return 0;

  CallDescriptor* desc =
      CreateRandomCallDescriptor(&zone, return_count, param_count, &input);

  if (v8_flags.wasm_fuzzer_gen_test) {
    // Print some debugging output which describes the produced signature.
    printf("[");
    for (size_t j = 0; j < param_count; ++j) {
      // Parameter 0 is the WasmContext.
      printf(" %s", MachineReprToString(
                        desc->GetParameterType(j + 1).representation()));
    }
    printf(" ] -> [");
    for (size_t j = 0; j < desc->ReturnCount(); ++j) {
      printf(" %s",
             MachineReprToString(desc->GetReturnType(j).representation()));
    }
    printf(" ]\n\n");
  }

  // Count parameters of each type.
  constexpr size_t kNumMachineRepresentations =
      static_cast<size_t>(MachineRepresentation::kLastRepresentation) + 1;

  // Trivial hash table for the number of occurrences of parameter types. The
  // MachineRepresentation of the parameter types is used as hash code.
  int counts[kNumMachineRepresentations] = {0};
  for (size_t i = 0; i < param_count; ++i) {
    // Parameter 0 is the WasmContext.
    ++counts[index(desc->GetParameterType(i + 1))];
  }

  // Generate random inputs.
  std::unique_ptr<int[]> inputs(new int[param_count]);
  std::unique_ptr<int[]> outputs(new int[desc->ReturnCount()]);
  for (size_t i = 0; i < param_count; ++i) {
    inputs[i] = input.NextInt32(10000);
  }

  RawMachineAssembler callee(
      i_isolate, zone.New<Graph>(&zone), desc,
      MachineType::PointerRepresentation(),
      InstructionSelector::SupportedMachineOperatorFlags());

  // Generate callee, returning random picks of its parameters.
  std::unique_ptr<Node* []> params(new Node*[desc->ParameterCount() + 2]);
  // The first input of a return is the number of stack slots that should be
  // popped before returning.
  std::unique_ptr<Node* []> returns(new Node*[desc->ReturnCount() + 1]);
  for (size_t i = 0; i < param_count; ++i) {
    // Parameter(0) is the WasmContext.
    params[i] = callee.Parameter(i + 1);
  }

  for (size_t i = 0; i < desc->ReturnCount(); ++i) {
    MachineType type = desc->GetReturnType(i);
    // Find a random same-type parameter to return. Use a constant if none.
    if (counts[index(type)] == 0) {
      returns[i] = Constant(&callee, type, 42);
      outputs[i] = 42;
    } else {
      int n = input.NextInt32(counts[index(type)]);
      int k = 0;
      while (desc->GetParameterType(k + 1) != desc->GetReturnType(i) ||
             --n > 0) {
        ++k;
      }
      returns[i] = params[k];
      outputs[i] = inputs[k];
    }
  }
  callee.Return(static_cast<int>(desc->ReturnCount()), returns.get());

  OptimizedCompilationInfo info(base::ArrayVector("testing"), &zone,
                                CodeKind::FOR_TESTING);
  Handle<Code> code =
      Pipeline::GenerateCodeForTesting(&info, i_isolate, desc, callee.graph(),
                                       AssemblerOptions::Default(i_isolate),
                                       callee.ExportForTest())
          .ToHandleChecked();

  std::shared_ptr<wasm::NativeModule> module =
      AllocateNativeModule(i_isolate, code->instruction_size());
  wasm::WasmCodeRefScope wasm_code_ref_scope;
  uint8_t* code_start = module->AddCodeForTesting(code)->instructions().begin();
  // Generate wrapper.
  int expect = 0;

  MachineSignature::Builder sig_builder(&zone, 1, 0);
  sig_builder.AddReturn(MachineType::Int32());

  CallDescriptor* wrapper_desc =
      Linkage::GetSimplifiedCDescriptor(&zone, sig_builder.Build());
  RawMachineAssembler caller(
      i_isolate, zone.New<Graph>(&zone), wrapper_desc,
      MachineType::PointerRepresentation(),
      InstructionSelector::SupportedMachineOperatorFlags());

  params[0] = caller.PointerConstant(code_start);
  // WasmContext dummy.
  params[1] = caller.PointerConstant(nullptr);
  for (size_t i = 0; i < param_count; ++i) {
    params[i + 2] = Constant(&caller, desc->GetParameterType(i + 1), inputs[i]);
  }
  Node* call = caller.AddNode(caller.common()->Call(desc),
                              static_cast<int>(param_count + 2), params.get());
  Node* ret = Constant(&caller, MachineType::Int32(), 0);
  for (size_t i = 0; i < desc->ReturnCount(); ++i) {
    // Skip roughly one third of the outputs.
    if (input.NextInt8(3) == 0) continue;
    Node* ret_i = (desc->ReturnCount() == 1)
                      ? call
                      : caller.AddNode(caller.common()->Projection(i), call);
    ret = caller.Int32Add(ret, ToInt32(&caller, desc->GetReturnType(i), ret_i));
    expect += outputs[i];
  }
  caller.Return(ret);

  // Call the wrapper.
  OptimizedCompilationInfo wrapper_info(base::ArrayVector("wrapper"), &zone,
                                        CodeKind::FOR_TESTING);
  Handle<Code> wrapper_code =
      Pipeline::GenerateCodeForTesting(
          &wrapper_info, i_isolate, wrapper_desc, caller.graph(),
          AssemblerOptions::Default(i_isolate), caller.ExportForTest())
          .ToHandleChecked();

  auto fn = GeneratedCode<int32_t>::FromCode(i_isolate, *wrapper_code);
  int result = fn.Call();

  CHECK_EQ(expect, result);
  return 0;
}

}  // namespace fuzzer
}  // namespace compiler
}  // namespace internal
}  // namespace v8
