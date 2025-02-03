// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_CSA_GENERATOR_H_
#define V8_TORQUE_CSA_GENERATOR_H_

#include <optional>

#include "src/torque/torque-code-generator.h"

namespace v8::internal::torque {

class CSAGenerator : public TorqueCodeGenerator {
 public:
  CSAGenerator(const ControlFlowGraph& cfg, std::ostream& out,
               std::optional<Builtin::Kind> linkage = std::nullopt)
      : TorqueCodeGenerator(cfg, out), linkage_(linkage) {}
  std::optional<Stack<std::string>> EmitGraph(Stack<std::string> parameters);

  static constexpr const char* ARGUMENTS_VARIABLE_STRING = "arguments";

  static void EmitCSAValue(VisitResult result, const Stack<std::string>& values,
                           std::ostream& out);

 private:
  std::optional<Builtin::Kind> linkage_;

  void EmitSourcePosition(SourcePosition pos,
                          bool always_emit = false) override;

  std::string PreCallableExceptionPreparation(
      std::optional<Block*> catch_block);
  void PostCallableExceptionPreparation(
      const std::string& catch_name, const Type* return_type,
      std::optional<Block*> catch_block, Stack<std::string>* stack,
      const std::optional<DefinitionLocation>& exception_object_definition);

  std::vector<std::string> ProcessArgumentsCommon(
      const TypeVector& parameter_types,
      std::vector<std::string> constexpr_arguments, Stack<std::string>* stack);

  Stack<std::string> EmitBlock(const Block* block);
#define EMIT_INSTRUCTION_DECLARATION(T)                                 \
  void EmitInstruction(const T& instruction, Stack<std::string>* stack) \
      override;
  TORQUE_BACKEND_DEPENDENT_INSTRUCTION_LIST(EMIT_INSTRUCTION_DECLARATION)
#undef EMIT_INSTRUCTION_DECLARATION
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_CSA_GENERATOR_H_
