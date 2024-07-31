// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/cc-generator.h"

#include "src/common/globals.h"
#include "src/torque/global-context.h"
#include "src/torque/type-oracle.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

base::Optional<Stack<std::string>> CCGenerator::EmitGraph(
    Stack<std::string> parameters) {
  for (BottomOffset i = {0}; i < parameters.AboveTop(); ++i) {
    SetDefinitionVariable(DefinitionLocation::Parameter(i.offset),
                          parameters.Peek(i));
  }

  // Redirect the output of non-declarations into a buffer and only output
  // declarations right away.
  std::stringstream out_buffer;
  std::ostream* old_out = out_;
  out_ = &out_buffer;

  EmitInstruction(GotoInstruction{cfg_.start()}, &parameters);

  for (Block* block : cfg_.blocks()) {
    if (cfg_.end() && *cfg_.end() == block) continue;
    if (block->IsDead()) continue;
    EmitBlock(block);
  }

  base::Optional<Stack<std::string>> result;
  if (cfg_.end()) {
    result = EmitBlock(*cfg_.end());
  }

  // All declarations have been printed now, so we can append the buffered
  // output and redirect back to the original output stream.
  out_ = old_out;
  out() << out_buffer.str();

  return result;
}

Stack<std::string> CCGenerator::EmitBlock(const Block* block) {
  out() << "\n";
  out() << "  " << BlockName(block) << ":\n";

  Stack<std::string> stack;

  for (BottomOffset i = {0}; i < block->InputTypes().AboveTop(); ++i) {
    const auto& def = block->InputDefinitions().Peek(i);
    stack.Push(DefinitionToVariable(def));
    if (def.IsPhiFromBlock(block)) {
      decls() << "  "
              << (is_cc_debug_ ? block->InputTypes().Peek(i)->GetDebugType()
                               : block->InputTypes().Peek(i)->GetRuntimeType())
              << " " << stack.Top() << "{}; USE(" << stack.Top() << ");\n";
    }
  }

  for (const Instruction& instruction : block->instructions()) {
    TorqueCodeGenerator::EmitInstruction(instruction, &stack);
  }
  return stack;
}

void CCGenerator::EmitSourcePosition(SourcePosition pos, bool always_emit) {
  const std::string& file = SourceFileMap::AbsolutePath(pos.source);
  if (always_emit || !previous_position_.CompareStartIgnoreColumn(pos)) {
    // Lines in Torque SourcePositions are zero-based, while the
    // CodeStubAssembler and downwind systems are one-based.
    out() << "  // " << file << ":" << (pos.start.line + 1) << "\n";
    previous_position_ = pos;
  }
}

void CCGenerator::EmitInstruction(
    const PushUninitializedInstruction& instruction,
    Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: PushUninitialized");
}

void CCGenerator::EmitInstruction(
    const PushBuiltinPointerInstruction& instruction,
    Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: PushBuiltinPointer");
}

void CCGenerator::EmitInstruction(
    const NamespaceConstantInstruction& instruction,
    Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: NamespaceConstantInstruction");
}

std::vector<std::string> CCGenerator::ProcessArgumentsCommon(
    const TypeVector& parameter_types,
    std::vector<std::string> constexpr_arguments, Stack<std::string>* stack) {
  std::vector<std::string> args;
  for (auto it = parameter_types.rbegin(); it != parameter_types.rend(); ++it) {
    const Type* type = *it;
    if (type->IsConstexpr()) {
      args.push_back(std::move(constexpr_arguments.back()));
      constexpr_arguments.pop_back();
    } else {
      std::stringstream s;
      size_t slot_count = LoweredSlotCount(type);
      VisitResult arg = VisitResult(type, stack->TopRange(slot_count));
      EmitCCValue(arg, *stack, s);
      args.push_back(s.str());
      stack->PopMany(slot_count);
    }
  }
  std::reverse(args.begin(), args.end());
  return args;
}

void CCGenerator::EmitInstruction(const CallIntrinsicInstruction& instruction,
                                  Stack<std::string>* stack) {
  TypeVector parameter_types =
      instruction.intrinsic->signature().parameter_types.types;
  std::vector<std::string> args = ProcessArgumentsCommon(
      parameter_types, instruction.constexpr_arguments, stack);

  Stack<std::string> pre_call_stack = *stack;
  const Type* return_type = instruction.intrinsic->signature().return_type;
  std::vector<std::string> results;

  const auto lowered = LowerType(return_type);
  for (std::size_t i = 0; i < lowered.size(); ++i) {
    results.push_back(DefinitionToVariable(instruction.GetValueDefinition(i)));
    stack->Push(results.back());
    decls() << "  "
            << (is_cc_debug_ ? lowered[i]->GetDebugType()
                             : lowered[i]->GetRuntimeType())
            << " " << stack->Top() << "{}; USE(" << stack->Top() << ");\n";
  }

  out() << "  ";
  if (return_type->StructSupertype()) {
    out() << "std::tie(";
    PrintCommaSeparatedList(out(), results);
    out() << ") = ";
  } else {
    if (results.size() == 1) {
      out() << results[0] << " = ";
    }
  }

  if (instruction.intrinsic->ExternalName() == "%RawDownCast") {
    if (parameter_types.size() != 1) {
      ReportError("%RawDownCast must take a single parameter");
    }
    const Type* original_type = parameter_types[0];
    bool is_subtype =
        return_type->IsSubtypeOf(original_type) ||
        (original_type == TypeOracle::GetUninitializedHeapObjectType() &&
         return_type->IsSubtypeOf(TypeOracle::GetHeapObjectType()));
    if (!is_subtype) {
      ReportError("%RawDownCast error: ", *return_type, " is not a subtype of ",
                  *original_type);
    }
    if (!original_type->StructSupertype() &&
        return_type->GetRuntimeType() != original_type->GetRuntimeType()) {
      out() << "static_cast<" << return_type->GetRuntimeType() << ">";
    }
  } else if (instruction.intrinsic->ExternalName() == "%GetClassMapConstant") {
    ReportError("C++ generator doesn't yet support %GetClassMapConstant");
  } else if (instruction.intrinsic->ExternalName() == "%FromConstexpr") {
    if (parameter_types.size() != 1 || !parameter_types[0]->IsConstexpr()) {
      ReportError(
          "%FromConstexpr must take a single parameter with constexpr "
          "type");
    }
    if (return_type->IsConstexpr()) {
      ReportError("%FromConstexpr must return a non-constexpr type");
    }
    if (return_type->IsSubtypeOf(TypeOracle::GetSmiType())) {
      if (is_cc_debug_) {
        out() << "Internals::IntToSmi";
      } else {
        out() << "Smi::FromInt";
      }
    }
    // Wrap the raw constexpr value in a static_cast to ensure that
    // enums get properly casted to their backing integral value.
    out() << "(CastToUnderlyingTypeIfEnum";
  } else {
    ReportError("no built in intrinsic with name " +
                instruction.intrinsic->ExternalName());
  }

  out() << "(";
  PrintCommaSeparatedList(out(), args);
  if (instruction.intrinsic->ExternalName() == "%FromConstexpr") {
    out() << ")";
  }
  out() << ");\n";
}

void CCGenerator::EmitInstruction(const CallCsaMacroInstruction& instruction,
                                  Stack<std::string>* stack) {
  TypeVector parameter_types =
      instruction.macro->signature().parameter_types.types;
  std::vector<std::string> args = ProcessArgumentsCommon(
      parameter_types, instruction.constexpr_arguments, stack);

  Stack<std::string> pre_call_stack = *stack;
  const Type* return_type = instruction.macro->signature().return_type;
  std::vector<std::string> results;

  const auto lowered = LowerType(return_type);
  for (std::size_t i = 0; i < lowered.size(); ++i) {
    results.push_back(DefinitionToVariable(instruction.GetValueDefinition(i)));
    stack->Push(results.back());
    decls() << "  "
            << (is_cc_debug_ ? lowered[i]->GetDebugType()
                             : lowered[i]->GetRuntimeType())
            << " " << stack->Top() << "{}; USE(" << stack->Top() << ");\n";
  }

  // We should have inlined any calls requiring complex control flow.
  CHECK(!instruction.catch_block);
  out() << (is_cc_debug_ ? "  ASSIGN_OR_RETURN(" : "  ");
  if (return_type->StructSupertype().has_value()) {
    out() << "std::tie(";
    PrintCommaSeparatedList(out(), results);
    out() << (is_cc_debug_ ? "), " : ") = ");
  } else {
    if (results.size() == 1) {
      out() << results[0] << (is_cc_debug_ ? ", " : " = ");
    } else {
      DCHECK_EQ(0, results.size());
    }
  }

  if (is_cc_debug_) {
    out() << instruction.macro->CCDebugName() << "(accessor";
    if (!args.empty()) out() << ", ";
  } else {
    out() << instruction.macro->CCName() << "(";
  }
  PrintCommaSeparatedList(out(), args);
  if (is_cc_debug_) {
    out() << "));\n";
  } else {
    out() << ");\n";
  }
}

void CCGenerator::EmitInstruction(
    const CallCsaMacroAndBranchInstruction& instruction,
    Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: CallCsaMacroAndBranch");
}

void CCGenerator::EmitInstruction(const MakeLazyNodeInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: MakeLazyNode");
}

void CCGenerator::EmitInstruction(const CallBuiltinInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: CallBuiltin");
}

void CCGenerator::EmitInstruction(
    const CallBuiltinPointerInstruction& instruction,
    Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: CallBuiltinPointer");
}

void CCGenerator::EmitInstruction(const CallRuntimeInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: CallRuntime");
}

void CCGenerator::EmitInstruction(const BranchInstruction& instruction,
                                  Stack<std::string>* stack) {
  out() << "  if (" << stack->Pop() << ") {\n";
  EmitGoto(instruction.if_true, stack, "    ");
  out() << "  } else {\n";
  EmitGoto(instruction.if_false, stack, "    ");
  out() << "  }\n";
}

void CCGenerator::EmitInstruction(const ConstexprBranchInstruction& instruction,
                                  Stack<std::string>* stack) {
  out() << "  if ((" << instruction.condition << ")) {\n";
  EmitGoto(instruction.if_true, stack, "    ");
  out() << "  } else {\n";
  EmitGoto(instruction.if_false, stack, "    ");
  out() << "  }\n";
}

void CCGenerator::EmitGoto(const Block* destination, Stack<std::string>* stack,
                           std::string indentation) {
  const auto& destination_definitions = destination->InputDefinitions();
  DCHECK_EQ(stack->Size(), destination_definitions.Size());
  for (BottomOffset i = {0}; i < stack->AboveTop(); ++i) {
    DefinitionLocation def = destination_definitions.Peek(i);
    if (def.IsPhiFromBlock(destination)) {
      out() << indentation << DefinitionToVariable(def) << " = "
            << stack->Peek(i) << ";\n";
    }
  }
  out() << indentation << "goto " << BlockName(destination) << ";\n";
}

void CCGenerator::EmitInstruction(const GotoInstruction& instruction,
                                  Stack<std::string>* stack) {
  EmitGoto(instruction.destination, stack, "  ");
}

void CCGenerator::EmitInstruction(const GotoExternalInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: GotoExternal");
}

void CCGenerator::EmitInstruction(const ReturnInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: Return");
}

void CCGenerator::EmitInstruction(const PrintErrorInstruction& instruction,
                                  Stack<std::string>* stack) {
  out() << "  std::cerr << " << StringLiteralQuote(instruction.message)
        << ";\n";
}

void CCGenerator::EmitInstruction(const AbortInstruction& instruction,
                                  Stack<std::string>* stack) {
  switch (instruction.kind) {
    case AbortInstruction::Kind::kUnreachable:
      DCHECK(instruction.message.empty());
      out() << "  UNREACHABLE();\n";
      break;
    case AbortInstruction::Kind::kDebugBreak:
      DCHECK(instruction.message.empty());
      out() << "  base::OS::DebugBreak();\n";
      break;
    case AbortInstruction::Kind::kAssertionFailure: {
      std::string file = StringLiteralQuote(
          SourceFileMap::PathFromV8Root(instruction.pos.source));
      out() << "  CHECK(false, \"Failed Torque assertion: '\""
            << StringLiteralQuote(instruction.message) << "\"' at \"" << file
            << "\":\""
            << StringLiteralQuote(
                   std::to_string(instruction.pos.start.line + 1))
            << ");\n";
      break;
    }
  }
}

void CCGenerator::EmitInstruction(const UnsafeCastInstruction& instruction,
                                  Stack<std::string>* stack) {
  const std::string str = "static_cast<" +
                          instruction.destination_type->GetRuntimeType() +
                          ">(" + stack->Top() + ")";
  stack->Poke(stack->AboveTop() - 1, str);
  SetDefinitionVariable(instruction.GetValueDefinition(), str);
}

void CCGenerator::EmitInstruction(const LoadReferenceInstruction& instruction,
                                  Stack<std::string>* stack) {
  std::string result_name =
      DefinitionToVariable(instruction.GetValueDefinition());

  std::string offset = stack->Pop();
  std::string object = stack->Pop();
  stack->Push(result_name);

  if (!is_cc_debug_) {
    std::string result_type = instruction.type->GetRuntimeType();
    decls() << "  " << result_type << " " << result_name << "{}; USE("
            << result_name << ");\n";
    out() << "  " << result_name << " = ";
    if (instruction.type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
      // Currently, all of the tagged loads we emit are for smi values, so there
      // is no point in providing an PtrComprCageBase. If at some point we start
      // emitting loads for tagged fields which might be HeapObjects, then we
      // should plumb an PtrComprCageBase through the generated functions that
      // need it.
      if (!instruction.type->IsSubtypeOf(TypeOracle::GetSmiType())) {
        Error(
            "Not supported in C++ output: LoadReference on non-smi tagged "
            "value");
      }

      // References and slices can cause some values to have the Torque type
      // HeapObject|TaggedZeroPattern, which is output as "Object". TaggedField
      // requires HeapObject, so we need a cast.
      out() << "TaggedField<" << result_type
            << ">::load(Tagged<HeapObject>::unchecked_cast(" << object
            << "), static_cast<int>(" << offset << "));\n";
    } else {
      out() << "(" << object << ")->ReadField<" << result_type << ">(" << offset
            << ");\n";
    }
  } else {
    std::string result_type = instruction.type->GetDebugType();
    decls() << "  " << result_type << " " << result_name << "{}; USE("
            << result_name << ");\n";
    if (instruction.type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
      out() << "  READ_TAGGED_FIELD_OR_FAIL(" << result_name << ", accessor, "
            << object << ", static_cast<int>(" << offset << "));\n";
    } else {
      out() << "  READ_FIELD_OR_FAIL(" << result_type << ", " << result_name
            << ", accessor, " << object << ", " << offset << ");\n";
    }
  }
}

void CCGenerator::EmitInstruction(const StoreReferenceInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: StoreReference");
}

namespace {
std::string GetBitFieldSpecialization(const Type* container,
                                      const BitField& field) {
  std::stringstream stream;
  stream << "base::BitField<"
         << field.name_and_type.type->GetConstexprGeneratedTypeName() << ", "
         << field.offset << ", " << field.num_bits << ", "
         << container->GetConstexprGeneratedTypeName() << ">";
  return stream.str();
}
}  // namespace

void CCGenerator::EmitInstruction(const LoadBitFieldInstruction& instruction,
                                  Stack<std::string>* stack) {
  std::string result_name =
      DefinitionToVariable(instruction.GetValueDefinition());

  std::string bit_field_struct = stack->Pop();
  stack->Push(result_name);

  const Type* struct_type = instruction.bit_field_struct_type;

  decls() << "  " << instruction.bit_field.name_and_type.type->GetRuntimeType()
          << " " << result_name << "{}; USE(" << result_name << ");\n";

  base::Optional<const Type*> smi_tagged_type =
      Type::MatchUnaryGeneric(struct_type, TypeOracle::GetSmiTaggedGeneric());
  if (smi_tagged_type) {
    // Get the untagged value and its type.
    if (is_cc_debug_) {
      bit_field_struct = "Internals::SmiValue(" + bit_field_struct + ")";
    } else {
      bit_field_struct = bit_field_struct + ".value()";
    }
    struct_type = *smi_tagged_type;
  }

  out() << "  " << result_name << " = CastToUnderlyingTypeIfEnum("
        << GetBitFieldSpecialization(struct_type, instruction.bit_field)
        << "::decode(" << bit_field_struct << "));\n";
}

void CCGenerator::EmitInstruction(const StoreBitFieldInstruction& instruction,
                                  Stack<std::string>* stack) {
  ReportError("Not supported in C++ output: StoreBitField");
}

namespace {

void CollectAllFields(const VisitResult& result,
                      const Stack<std::string>& values,
                      std::vector<std::string>& all_fields) {
  if (!result.IsOnStack()) {
    all_fields.push_back(result.constexpr_value());
  } else if (auto struct_type = result.type()->StructSupertype()) {
    for (const Field& field : (*struct_type)->fields()) {
      CollectAllFields(ProjectStructField(result, field.name_and_type.name),
                       values, all_fields);
    }
  } else {
    DCHECK_EQ(1, result.stack_range().Size());
    all_fields.push_back(values.Peek(result.stack_range().begin()));
  }
}

}  // namespace

// static
void CCGenerator::EmitCCValue(VisitResult result,
                              const Stack<std::string>& values,
                              std::ostream& out) {
  std::vector<std::string> all_fields;
  CollectAllFields(result, values, all_fields);
  if (all_fields.size() == 1) {
    out << all_fields[0];
  } else {
    out << "std::make_tuple(";
    PrintCommaSeparatedList(out, all_fields);
    out << ")";
  }
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
