// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/instructions.h"

#include <optional>

#include "src/torque/cfg.h"
#include "src/torque/type-oracle.h"

namespace v8::internal::torque {

#define TORQUE_INSTRUCTION_BOILERPLATE_DEFINITIONS(Name)        \
  const InstructionKind Name::kKind = InstructionKind::k##Name; \
  std::unique_ptr<InstructionBase> Name::Clone() const {        \
    return std::unique_ptr<InstructionBase>(new Name(*this));   \
  }                                                             \
  void Name::Assign(const InstructionBase& other) {             \
    *this = static_cast<const Name&>(other);                    \
  }
TORQUE_INSTRUCTION_LIST(TORQUE_INSTRUCTION_BOILERPLATE_DEFINITIONS)
#undef TORQUE_INSTRUCTION_BOILERPLATE_DEFINITIONS

namespace {
void ExpectType(const Type* expected, const Type* actual) {
  if (expected != actual) {
    ReportError("expected type ", *expected, " but found ", *actual);
  }
}
void ExpectSubtype(const Type* subtype, const Type* supertype) {
  if (!subtype->IsSubtypeOf(supertype)) {
    ReportError("type ", *subtype, " is not a subtype of ", *supertype);
  }
}
}  // namespace

void PeekInstruction::TypeInstruction(Stack<const Type*>* stack,
                                      ControlFlowGraph* cfg) const {
  const Type* type = stack->Peek(slot);
  if (widened_type) {
    if (type->IsTopType()) {
      const TopType* top_type = TopType::cast(type);
      ReportError("use of " + top_type->reason());
    }
    ExpectSubtype(type, *widened_type);
    type = *widened_type;
  }
  stack->Push(type);
}

void PeekInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Push(locations->Peek(slot));
}

void PokeInstruction::TypeInstruction(Stack<const Type*>* stack,
                                      ControlFlowGraph* cfg) const {
  const Type* type = stack->Top();
  if (widened_type) {
    ExpectSubtype(type, *widened_type);
    type = *widened_type;
  }
  stack->Poke(slot, type);
  stack->Pop();
}

void PokeInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Poke(slot, locations->Pop());
}

void DeleteRangeInstruction::TypeInstruction(Stack<const Type*>* stack,
                                             ControlFlowGraph* cfg) const {
  stack->DeleteRange(range);
}

void DeleteRangeInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->DeleteRange(range);
}

void PushUninitializedInstruction::TypeInstruction(
    Stack<const Type*>* stack, ControlFlowGraph* cfg) const {
  stack->Push(type);
}

void PushUninitializedInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Push(GetValueDefinition());
}

DefinitionLocation PushUninitializedInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void PushBuiltinPointerInstruction::TypeInstruction(
    Stack<const Type*>* stack, ControlFlowGraph* cfg) const {
  stack->Push(type);
}

void PushBuiltinPointerInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Push(GetValueDefinition());
}

DefinitionLocation PushBuiltinPointerInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void NamespaceConstantInstruction::TypeInstruction(
    Stack<const Type*>* stack, ControlFlowGraph* cfg) const {
  stack->PushMany(LowerType(constant->type()));
}

void NamespaceConstantInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
    locations->Push(GetValueDefinition(i));
  }
}

std::size_t NamespaceConstantInstruction::GetValueDefinitionCount() const {
  return LowerType(constant->type()).size();
}

DefinitionLocation NamespaceConstantInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::ostream& operator<<(std::ostream& os,
                         const NamespaceConstantInstruction& instruction) {
  return os << "NamespaceConstant " << instruction.constant->external_name();
}

void InstructionBase::InvalidateTransientTypes(
    Stack<const Type*>* stack) const {
  auto current = stack->begin();
  while (current != stack->end()) {
    if ((*current)->IsTransient()) {
      std::stringstream stream;
      stream << "type " << **current
             << " is made invalid by transitioning callable invocation at "
             << PositionAsString(pos);
      *current = TypeOracle::GetTopType(stream.str(), *current);
    }
    ++current;
  }
}

void CallIntrinsicInstruction::TypeInstruction(Stack<const Type*>* stack,
                                               ControlFlowGraph* cfg) const {
  std::vector<const Type*> parameter_types =
      LowerParameterTypes(intrinsic->signature().parameter_types);
  for (intptr_t i = parameter_types.size() - 1; i >= 0; --i) {
    const Type* arg_type = stack->Pop();
    const Type* parameter_type = parameter_types.back();
    parameter_types.pop_back();
    if (arg_type != parameter_type) {
      ReportError("parameter ", i, ": expected type ", *parameter_type,
                  " but found type ", *arg_type);
    }
  }
  if (intrinsic->IsTransitioning()) {
    InvalidateTransientTypes(stack);
  }
  stack->PushMany(LowerType(intrinsic->signature().return_type));
}

void CallIntrinsicInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  auto parameter_types =
      LowerParameterTypes(intrinsic->signature().parameter_types);
  locations->PopMany(parameter_types.size());
  for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
    locations->Push(DefinitionLocation::Instruction(this, i));
  }
}

std::size_t CallIntrinsicInstruction::GetValueDefinitionCount() const {
  return LowerType(intrinsic->signature().return_type).size();
}

DefinitionLocation CallIntrinsicInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::ostream& operator<<(std::ostream& os,
                         const CallIntrinsicInstruction& instruction) {
  os << "CallIntrinsic " << instruction.intrinsic->ReadableName();
  if (!instruction.specialization_types.empty()) {
    os << "<";
    PrintCommaSeparatedList(
        os, instruction.specialization_types,
        [](const Type* type) -> const Type& { return *type; });
    os << ">";
  }
  os << "(";
  PrintCommaSeparatedList(os, instruction.constexpr_arguments);
  os << ")";
  return os;
}

void CallCsaMacroInstruction::TypeInstruction(Stack<const Type*>* stack,
                                              ControlFlowGraph* cfg) const {
  std::vector<const Type*> parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  for (intptr_t i = parameter_types.size() - 1; i >= 0; --i) {
    const Type* arg_type = stack->Pop();
    const Type* parameter_type = parameter_types.back();
    parameter_types.pop_back();
    if (arg_type != parameter_type) {
      ReportError("parameter ", i, ": expected type ", *parameter_type,
                  " but found type ", *arg_type);
    }
  }

  if (macro->IsTransitioning()) {
    InvalidateTransientTypes(stack);
  }

  if (catch_block) {
    Stack<const Type*> catch_stack = *stack;
    catch_stack.Push(TypeOracle::GetJSAnyType());
    (*catch_block)->SetInputTypes(catch_stack);
  }

  stack->PushMany(LowerType(macro->signature().return_type));
}

void CallCsaMacroInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  auto parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  locations->PopMany(parameter_types.size());

  if (catch_block) {
    locations->Push(*GetExceptionObjectDefinition());
    (*catch_block)->MergeInputDefinitions(*locations, worklist);
    locations->Pop();
  }

  for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
    locations->Push(GetValueDefinition(i));
  }
}

std::optional<DefinitionLocation>
CallCsaMacroInstruction::GetExceptionObjectDefinition() const {
  if (!catch_block) return std::nullopt;
  return DefinitionLocation::Instruction(this, GetValueDefinitionCount());
}

std::size_t CallCsaMacroInstruction::GetValueDefinitionCount() const {
  return LowerType(macro->signature().return_type).size();
}

DefinitionLocation CallCsaMacroInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::ostream& operator<<(std::ostream& os,
                         const CallCsaMacroInstruction& instruction) {
  os << "CallCsaMacro " << instruction.macro->ReadableName();
  os << "(";
  PrintCommaSeparatedList(os, instruction.constexpr_arguments);
  os << ")";
  if (instruction.catch_block) {
    os << ", catch block " << (*instruction.catch_block)->id();
  }
  return os;
}

void CallCsaMacroAndBranchInstruction::TypeInstruction(
    Stack<const Type*>* stack, ControlFlowGraph* cfg) const {
  std::vector<const Type*> parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  for (intptr_t i = parameter_types.size() - 1; i >= 0; --i) {
    const Type* arg_type = stack->Pop();
    const Type* parameter_type = parameter_types.back();
    parameter_types.pop_back();
    if (arg_type != parameter_type) {
      ReportError("parameter ", i, ": expected type ", *parameter_type,
                  " but found type ", *arg_type);
    }
  }

  if (label_blocks.size() != macro->signature().labels.size()) {
    ReportError("wrong number of labels");
  }
  for (size_t i = 0; i < label_blocks.size(); ++i) {
    Stack<const Type*> continuation_stack = *stack;
    continuation_stack.PushMany(
        LowerParameterTypes(macro->signature().labels[i].types));
    label_blocks[i]->SetInputTypes(std::move(continuation_stack));
  }

  if (macro->IsTransitioning()) {
    InvalidateTransientTypes(stack);
  }

  if (catch_block) {
    Stack<const Type*> catch_stack = *stack;
    catch_stack.Push(TypeOracle::GetJSAnyType());
    (*catch_block)->SetInputTypes(catch_stack);
  }

  if (macro->signature().return_type != TypeOracle::GetNeverType()) {
    Stack<const Type*> return_stack = *stack;
    return_stack.PushMany(LowerType(macro->signature().return_type));
    if (return_continuation == std::nullopt) {
      ReportError("missing return continuation.");
    }
    (*return_continuation)->SetInputTypes(return_stack);
  } else {
    if (return_continuation != std::nullopt) {
      ReportError("unreachable return continuation.");
    }
  }
}

void CallCsaMacroAndBranchInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  auto parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  locations->PopMany(parameter_types.size());

  for (std::size_t label_index = 0; label_index < label_blocks.size();
       ++label_index) {
    const std::size_t count = GetLabelValueDefinitionCount(label_index);
    for (std::size_t i = 0; i < count; ++i) {
      locations->Push(GetLabelValueDefinition(label_index, i));
    }
    label_blocks[label_index]->MergeInputDefinitions(*locations, worklist);
    locations->PopMany(count);
  }

  if (catch_block) {
    locations->Push(*GetExceptionObjectDefinition());
    (*catch_block)->MergeInputDefinitions(*locations, worklist);
    locations->Pop();
  }

  if (macro->signature().return_type != TypeOracle::GetNeverType()) {
    if (return_continuation) {
      const std::size_t count = GetValueDefinitionCount();
      for (std::size_t i = 0; i < count; ++i) {
        locations->Push(GetValueDefinition(i));
      }
      (*return_continuation)->MergeInputDefinitions(*locations, worklist);
      locations->PopMany(count);
    }
  }
}

std::size_t CallCsaMacroAndBranchInstruction::GetLabelCount() const {
  return label_blocks.size();
}

std::size_t CallCsaMacroAndBranchInstruction::GetLabelValueDefinitionCount(
    std::size_t label) const {
  DCHECK_LT(label, GetLabelCount());
  return LowerParameterTypes(macro->signature().labels[label].types).size();
}

DefinitionLocation CallCsaMacroAndBranchInstruction::GetLabelValueDefinition(
    std::size_t label, std::size_t index) const {
  DCHECK_LT(index, GetLabelValueDefinitionCount(label));
  std::size_t offset = GetValueDefinitionCount() + (catch_block ? 1 : 0);
  for (std::size_t label_index = 0; label_index < label; ++label_index) {
    offset += GetLabelValueDefinitionCount(label_index);
  }
  return DefinitionLocation::Instruction(this, offset + index);
}

std::size_t CallCsaMacroAndBranchInstruction::GetValueDefinitionCount() const {
  if (macro->signature().return_type == TypeOracle::GetNeverType()) return 0;
  if (!return_continuation) return 0;
  return LowerType(macro->signature().return_type).size();
}

DefinitionLocation CallCsaMacroAndBranchInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::optional<DefinitionLocation>
CallCsaMacroAndBranchInstruction::GetExceptionObjectDefinition() const {
  if (!catch_block) return std::nullopt;
  return DefinitionLocation::Instruction(this, GetValueDefinitionCount());
}

std::ostream& operator<<(std::ostream& os,
                         const CallCsaMacroAndBranchInstruction& instruction) {
  os << "CallCsaMacroAndBranch " << instruction.macro->ReadableName();
  os << "(";
  PrintCommaSeparatedList(os, instruction.constexpr_arguments);
  os << ")";
  if (instruction.return_continuation) {
    os << ", return continuation " << (*instruction.return_continuation)->id();
  }
  if (!instruction.label_blocks.empty()) {
    os << ", label blocks ";
    PrintCommaSeparatedList(os, instruction.label_blocks,
                            [](Block* block) { return block->id(); });
  }
  if (instruction.catch_block) {
    os << ", catch block " << (*instruction.catch_block)->id();
  }
  return os;
}

void CallBuiltinInstruction::TypeInstruction(Stack<const Type*>* stack,
                                             ControlFlowGraph* cfg) const {
  std::vector<const Type*> argument_types = stack->PopMany(argc);
  if (argument_types !=
      LowerParameterTypes(builtin->signature().parameter_types)) {
    ReportError("wrong argument types");
  }
  if (builtin->IsTransitioning()) {
    InvalidateTransientTypes(stack);
  }

  if (catch_block) {
    Stack<const Type*> catch_stack = *stack;
    catch_stack.Push(TypeOracle::GetJSAnyType());
    (*catch_block)->SetInputTypes(catch_stack);
  }

  stack->PushMany(LowerType(builtin->signature().return_type));
}

void CallBuiltinInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->PopMany(argc);

  if (catch_block) {
    locations->Push(*GetExceptionObjectDefinition());
    (*catch_block)->MergeInputDefinitions(*locations, worklist);
    locations->Pop();
  }

  for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
    locations->Push(GetValueDefinition(i));
  }
}

std::size_t CallBuiltinInstruction::GetValueDefinitionCount() const {
  return LowerType(builtin->signature().return_type).size();
}

DefinitionLocation CallBuiltinInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::optional<DefinitionLocation>
CallBuiltinInstruction::GetExceptionObjectDefinition() const {
  if (!catch_block) return std::nullopt;
  return DefinitionLocation::Instruction(this, GetValueDefinitionCount());
}

void CallBuiltinPointerInstruction::TypeInstruction(
    Stack<const Type*>* stack, ControlFlowGraph* cfg) const {
  std::vector<const Type*> argument_types = stack->PopMany(argc);
  const BuiltinPointerType* f = BuiltinPointerType::DynamicCast(stack->Pop());
  if (!f) ReportError("expected function pointer type");
  if (argument_types != LowerParameterTypes(f->parameter_types())) {
    ReportError("wrong argument types");
  }
  DCHECK_EQ(type, f);
  // TODO(turbofan): Only invalidate transient types if the function pointer
  // type is transitioning.
  InvalidateTransientTypes(stack);
  stack->PushMany(LowerType(f->return_type()));
}

void CallBuiltinPointerInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->PopMany(argc + 1);
  for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
    locations->Push(GetValueDefinition(i));
  }
}

std::size_t CallBuiltinPointerInstruction::GetValueDefinitionCount() const {
  return LowerType(type->return_type()).size();
}

DefinitionLocation CallBuiltinPointerInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::ostream& operator<<(std::ostream& os,
                         const CallBuiltinInstruction& instruction) {
  os << "CallBuiltin " << instruction.builtin->ReadableName()
     << ", argc: " << instruction.argc;
  if (instruction.is_tailcall) {
    os << ", is_tailcall";
  }
  if (instruction.catch_block) {
    os << ", catch block " << (*instruction.catch_block)->id();
  }
  return os;
}

void CallRuntimeInstruction::TypeInstruction(Stack<const Type*>* stack,
                                             ControlFlowGraph* cfg) const {
  std::vector<const Type*> argument_types = stack->PopMany(argc);
  if (argument_types !=
      LowerParameterTypes(runtime_function->signature().parameter_types,
                          argc)) {
    ReportError("wrong argument types");
  }
  if (runtime_function->IsTransitioning()) {
    InvalidateTransientTypes(stack);
  }

  if (catch_block) {
    Stack<const Type*> catch_stack = *stack;
    catch_stack.Push(TypeOracle::GetJSAnyType());
    (*catch_block)->SetInputTypes(catch_stack);
  }

  const Type* return_type = runtime_function->signature().return_type;
  if (return_type != TypeOracle::GetNeverType()) {
    stack->PushMany(LowerType(return_type));
  }
}

void CallRuntimeInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->PopMany(argc);

  if (catch_block) {
    locations->Push(*GetExceptionObjectDefinition());
    (*catch_block)->MergeInputDefinitions(*locations, worklist);
    locations->Pop();
  }

  const Type* return_type = runtime_function->signature().return_type;
  if (return_type != TypeOracle::GetNeverType()) {
    for (std::size_t i = 0; i < GetValueDefinitionCount(); ++i) {
      locations->Push(GetValueDefinition(i));
    }
  }
}

std::size_t CallRuntimeInstruction::GetValueDefinitionCount() const {
  const Type* return_type = runtime_function->signature().return_type;
  if (return_type == TypeOracle::GetNeverType()) return 0;
  return LowerType(return_type).size();
}

DefinitionLocation CallRuntimeInstruction::GetValueDefinition(
    std::size_t index) const {
  DCHECK_LT(index, GetValueDefinitionCount());
  return DefinitionLocation::Instruction(this, index);
}

std::optional<DefinitionLocation>
CallRuntimeInstruction::GetExceptionObjectDefinition() const {
  if (!catch_block) return std::nullopt;
  return DefinitionLocation::Instruction(this, GetValueDefinitionCount());
}

std::ostream& operator<<(std::ostream& os,
                         const CallRuntimeInstruction& instruction) {
  os << "CallRuntime " << instruction.runtime_function->ReadableName()
     << ", argc: " << instruction.argc;
  if (instruction.is_tailcall) {
    os << ", is_tailcall";
  }
  if (instruction.catch_block) {
    os << ", catch block " << (*instruction.catch_block)->id();
  }
  return os;
}

void BranchInstruction::TypeInstruction(Stack<const Type*>* stack,
                                        ControlFlowGraph* cfg) const {
  const Type* condition_type = stack->Pop();
  if (condition_type != TypeOracle::GetBoolType()) {
    ReportError("condition has to have type bool");
  }
  if_true->SetInputTypes(*stack);
  if_false->SetInputTypes(*stack);
}

void BranchInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Pop();
  if_true->MergeInputDefinitions(*locations, worklist);
  if_false->MergeInputDefinitions(*locations, worklist);
}

std::ostream& operator<<(std::ostream& os,
                         const BranchInstruction& instruction) {
  return os << "Branch true: " << instruction.if_true->id()
            << ", false: " << instruction.if_false->id();
}

void ConstexprBranchInstruction::TypeInstruction(Stack<const Type*>* stack,
                                                 ControlFlowGraph* cfg) const {
  if_true->SetInputTypes(*stack);
  if_false->SetInputTypes(*stack);
}

void ConstexprBranchInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  if_true->MergeInputDefinitions(*locations, worklist);
  if_false->MergeInputDefinitions(*locations, worklist);
}

std::ostream& operator<<(std::ostream& os,
                         const ConstexprBranchInstruction& instruction) {
  return os << "ConstexprBranch " << instruction.condition
            << ", true: " << instruction.if_true->id()
            << ", false: " << instruction.if_false->id();
}

void GotoInstruction::TypeInstruction(Stack<const Type*>* stack,
                                      ControlFlowGraph* cfg) const {
  destination->SetInputTypes(*stack);
}

void GotoInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  destination->MergeInputDefinitions(*locations, worklist);
}

std::ostream& operator<<(std::ostream& os, const GotoInstruction& instruction) {
  return os << "Goto " << instruction.destination->id();
}

void GotoExternalInstruction::TypeInstruction(Stack<const Type*>* stack,
                                              ControlFlowGraph* cfg) const {
  if (variable_names.size() != stack->Size()) {
    ReportError("goto external label with wrong parameter count.");
  }
}

void GotoExternalInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {}

void ReturnInstruction::TypeInstruction(Stack<const Type*>* stack,
                                        ControlFlowGraph* cfg) const {
  cfg->SetReturnType(stack->PopMany(count));
}

void ReturnInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->PopMany(count);
}

void PrintErrorInstruction::TypeInstruction(Stack<const Type*>* stack,
                                            ControlFlowGraph* cfg) const {}

void PrintErrorInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {}

void AbortInstruction::TypeInstruction(Stack<const Type*>* stack,
                                       ControlFlowGraph* cfg) const {}

void AbortInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {}

void UnsafeCastInstruction::TypeInstruction(Stack<const Type*>* stack,
                                            ControlFlowGraph* cfg) const {
  stack->Poke(stack->AboveTop() - 1, destination_type);
}

void UnsafeCastInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Poke(locations->AboveTop() - 1, GetValueDefinition());
}

DefinitionLocation UnsafeCastInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void LoadReferenceInstruction::TypeInstruction(Stack<const Type*>* stack,
                                               ControlFlowGraph* cfg) const {
  ExpectType(TypeOracle::GetIntPtrType(), stack->Pop());
  ExpectSubtype(stack->Pop(), TypeOracle::GetUnionType(
                                  TypeOracle::GetHeapObjectType(),
                                  TypeOracle::GetTaggedZeroPatternType()));
  DCHECK_EQ(std::vector<const Type*>{type}, LowerType(type));
  stack->Push(type);
}

void LoadReferenceInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Pop();
  locations->Pop();
  locations->Push(GetValueDefinition());
}

DefinitionLocation LoadReferenceInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void StoreReferenceInstruction::TypeInstruction(Stack<const Type*>* stack,
                                                ControlFlowGraph* cfg) const {
  ExpectSubtype(stack->Pop(), type);
  ExpectType(TypeOracle::GetIntPtrType(), stack->Pop());
  ExpectSubtype(stack->Pop(), TypeOracle::GetUnionType(
                                  TypeOracle::GetHeapObjectType(),
                                  TypeOracle::GetTaggedZeroPatternType()));
}

void StoreReferenceInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Pop();
  locations->Pop();
  locations->Pop();
}

void LoadBitFieldInstruction::TypeInstruction(Stack<const Type*>* stack,
                                              ControlFlowGraph* cfg) const {
  ExpectType(bit_field_struct_type, stack->Pop());
  stack->Push(bit_field.name_and_type.type);
}

void LoadBitFieldInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Pop();
  locations->Push(GetValueDefinition());
}

DefinitionLocation LoadBitFieldInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void StoreBitFieldInstruction::TypeInstruction(Stack<const Type*>* stack,
                                               ControlFlowGraph* cfg) const {
  ExpectSubtype(bit_field.name_and_type.type, stack->Pop());
  ExpectType(bit_field_struct_type, stack->Pop());
  stack->Push(bit_field_struct_type);
}

void StoreBitFieldInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  locations->Pop();
  locations->Pop();
  locations->Push(GetValueDefinition());
}

DefinitionLocation StoreBitFieldInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

void MakeLazyNodeInstruction::TypeInstruction(Stack<const Type*>* stack,
                                              ControlFlowGraph* cfg) const {
  std::vector<const Type*> parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  for (intptr_t i = parameter_types.size() - 1; i >= 0; --i) {
    const Type* arg_type = stack->Pop();
    const Type* parameter_type = parameter_types.back();
    parameter_types.pop_back();
    if (arg_type != parameter_type) {
      ReportError("parameter ", i, ": expected type ", *parameter_type,
                  " but found type ", *arg_type);
    }
  }

  stack->Push(result_type);
}

void MakeLazyNodeInstruction::RecomputeDefinitionLocations(
    Stack<DefinitionLocation>* locations, Worklist<Block*>* worklist) const {
  auto parameter_types =
      LowerParameterTypes(macro->signature().parameter_types);
  locations->PopMany(parameter_types.size());

  locations->Push(GetValueDefinition());
}

DefinitionLocation MakeLazyNodeInstruction::GetValueDefinition() const {
  return DefinitionLocation::Instruction(this, 0);
}

std::ostream& operator<<(std::ostream& os,
                         const MakeLazyNodeInstruction& instruction) {
  os << "MakeLazyNode " << instruction.macro->ReadableName() << ", "
     << *instruction.result_type;
  for (const std::string& arg : instruction.constexpr_arguments) {
    os << ", " << arg;
  }
  return os;
}

bool CallRuntimeInstruction::IsBlockTerminator() const {
  return is_tailcall || runtime_function->signature().return_type ==
                            TypeOracle::GetNeverType();
}

}  // namespace v8::internal::torque
