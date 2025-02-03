// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_CC_GENERATOR_H_
#define V8_TORQUE_CC_GENERATOR_H_

#include <optional>

#include "src/torque/torque-code-generator.h"

namespace v8::internal::torque {

class CCGenerator : public TorqueCodeGenerator {
 public:
  CCGenerator(const ControlFlowGraph& cfg, std::ostream& out,
              bool is_cc_debug = false)
      : TorqueCodeGenerator(cfg, out), is_cc_debug_(is_cc_debug) {}
  std::optional<Stack<std::string>> EmitGraph(Stack<std::string> parameters);

  static void EmitCCValue(VisitResult result, const Stack<std::string>& values,
                          std::ostream& out);

 private:
  bool is_cc_debug_;

  void EmitSourcePosition(SourcePosition pos,
                          bool always_emit = false) override;

  void EmitGoto(const Block* destination, Stack<std::string>* stack,
                std::string indentation);

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

#endif  // V8_TORQUE_CC_GENERATOR_H_
