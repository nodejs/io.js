// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BUILTIN_COMPILER_H_
#define V8_COMPILER_TURBOSHAFT_BUILTIN_COMPILER_H_

#include "src/builtins/builtins.h"
#include "src/interpreter/bytecodes.h"
#include "src/objects/code-kind.h"

namespace v8::internal {

struct AssemblerOptions;
class Isolate;
class Zone;

namespace compiler {

class CallDescriptor;

namespace turboshaft {

struct CustomPipelineDataComponent;
class Graph;
class PipelineData;

struct BytecodeHandlerData {
  BytecodeHandlerData(interpreter::Bytecode bytecode,
                      interpreter::OperandScale operand_scale)
      : bytecode(bytecode), operand_scale(operand_scale) {}

  interpreter::Bytecode bytecode;
  interpreter::OperandScale operand_scale;
  interpreter::ImplicitRegisterUse implicit_register_use =
      interpreter::ImplicitRegisterUse::kNone;
  bool made_call = false;
  bool reloaded_frame_ptr = false;
  bool bytecode_array_valid = true;
};

using TurboshaftAssemblerGenerator =
    void (*)(compiler::turboshaft::PipelineData*, Isolate*,
             compiler::turboshaft::Graph&, Zone*);
V8_EXPORT_PRIVATE DirectHandle<Code> BuildWithTurboshaftAssemblerImpl(
    Isolate* isolate, Builtin builtin, TurboshaftAssemblerGenerator generator,
    std::function<compiler::CallDescriptor*(Zone*)> call_descriptor_builder,
    const char* name, const AssemblerOptions& options,
    CodeKind code_kind = CodeKind::BUILTIN,
    std::optional<BytecodeHandlerData> bytecode_handler_data = {});

}  // namespace turboshaft
}  // namespace compiler
}  // namespace v8::internal

#endif  // V8_COMPILER_TURBOSHAFT_BUILTIN_COMPILER_H_
