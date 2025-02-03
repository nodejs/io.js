// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/implementation-visitor.h"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <string>

#include "src/common/globals.h"
#include "src/numbers/integer-literal-inl.h"
#include "src/torque/cc-generator.h"
#include "src/torque/cfg.h"
#include "src/torque/constants.h"
#include "src/torque/cpp-builder.h"
#include "src/torque/csa-generator.h"
#include "src/torque/declaration-visitor.h"
#include "src/torque/global-context.h"
#include "src/torque/kythe-data.h"
#include "src/torque/parameter-difference.h"
#include "src/torque/server-data.h"
#include "src/torque/source-positions.h"
#include "src/torque/type-inference.h"
#include "src/torque/type-visitor.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

uint64_t next_unique_binding_index = 0;

// Sadly, 'using std::string_literals::operator""s;' is bugged in MSVC (see
// https://developercommunity.visualstudio.com/t/Incorrect-warning-when-using-standard-st/673948).
// TODO(nicohartmann@): Change to 'using std::string_literals::operator""s;'
// once this is fixed.
using namespace std::string_literals;  // NOLINT(build/namespaces)

namespace {
const char* BuiltinIncludesMarker = "// __BUILTIN_INCLUDES_MARKER__\n";
}  // namespace

VisitResult ImplementationVisitor::Visit(Expression* expr) {
  CurrentSourcePosition::Scope scope(expr->pos);
  switch (expr->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(expr));
    AST_EXPRESSION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNREACHABLE();
  }
}

const Type* ImplementationVisitor::Visit(Statement* stmt) {
  CurrentSourcePosition::Scope scope(stmt->pos);
  StackScope stack_scope(this);
  const Type* result;
  switch (stmt->kind) {
#define ENUM_ITEM(name)               \
  case AstNode::Kind::k##name:        \
    result = Visit(name::cast(stmt)); \
    break;
    AST_STATEMENT_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNREACHABLE();
  }
  DCHECK_EQ(result == TypeOracle::GetNeverType(),
            assembler().CurrentBlockIsComplete());
  return result;
}

void ImplementationVisitor::BeginGeneratedFiles() {
  std::set<SourceId> contains_class_definitions;
  std::set<SourceId> contains_class_asserts;
  for (const ClassType* type : TypeOracle::GetClasses()) {
    if (type->ShouldGenerateCppClassDefinitions()) {
      contains_class_definitions.insert(type->AttributedToFile());
    }
    if (type->ShouldGenerateCppObjectDefinitionAsserts() ||
        type->ShouldGenerateCppObjectLayoutDefinitionAsserts()) {
      contains_class_asserts.insert(type->AttributedToFile());
    }
  }

  for (SourceId source : SourceFileMap::AllSources()) {
    auto& streams = GlobalContext::GeneratedPerFile(source);
    // Output beginning of CSA .cc file.
    {
      cpp::File& file = streams.csa_cc;

      for (const std::string& include_path : GlobalContext::CppIncludes()) {
        file << "#include " << StringLiteralQuote(include_path) << "\n";
      }
      file << "#include \"src/codegen/code-stub-assembler-inl.h\"\n";

      file << "// Required Builtins:\n";
      file << "#include \"torque-generated/" +
                  SourceFileMap::PathFromV8RootWithoutExtension(source) +
                  "-tq-csa.h\"\n";
      // Now that required include files are collected while generting the file,
      // we only know the full set at the end. Insert a marker here that is
      // replaced with the list of includes at the very end.
      // TODO(nicohartmann@): This is not the most beautiful way to do this,
      // replace once the cpp file builder is available, where this can be
      // handled easily.
      file << BuiltinIncludesMarker;
      file << "\n";

      streams.csa_cc.BeginNamespace("v8", "internal");
      streams.csa_ccfile << "\n";
    }
    // Output beginning of CSA .h file.
    {
      cpp::File& file = streams.csa_header;
      std::string header_define =
          "V8_GEN_TORQUE_GENERATED_" +
          UnderlinifyPath(SourceFileMap::PathFromV8Root(source)) + "_CSA_H_";
      streams.csa_header.BeginIncludeGuard(header_define);
      file << "#include \"src/builtins/torque-csa-header-includes.h\"\n";
      file << "\n";

      streams.csa_header.BeginNamespace("v8", "internal");
      streams.csa_headerfile << "\n";
    }
    // Output beginning of class definition .cc file.
    {
      cpp::File& file = streams.class_definition_cc;
      if (contains_class_definitions.count(source) != 0) {
        file << "#include \""
             << SourceFileMap::PathFromV8RootWithoutExtension(source)
             << "-inl.h\"\n\n";
        file << "#include \"torque-generated/class-verifiers.h\"\n";
        file << "#include \"src/objects/instance-type-inl.h\"\n\n";
      }
      if (contains_class_asserts.count(source) != 0) {
        file << "#include \""
             << SourceFileMap::PathFromV8RootWithoutExtension(source)
             << ".h\"\n\n";
      }

      streams.class_definition_cc.BeginNamespace("v8", "internal");
      streams.class_definition_ccfile << "\n";
    }
  }
}

void ImplementationVisitor::EndGeneratedFiles() {
  for (SourceId file : SourceFileMap::AllSources()) {
    auto& streams = GlobalContext::GeneratedPerFile(file);

    // Output ending of CSA .cc file.
    streams.csa_cc.EndNamespace("v8", "internal");

    // Output ending of CSA .h file.
    {
      std::string header_define =
          "V8_GEN_TORQUE_GENERATED_" +
          UnderlinifyPath(SourceFileMap::PathFromV8Root(file)) + "_CSA_H_";

      streams.csa_header.EndNamespace("v8", "internal");
      streams.csa_headerfile << "\n";
      streams.csa_header.EndIncludeGuard(header_define);
    }

    // Output ending of class definition .cc file.
    streams.class_definition_cc.EndNamespace("v8", "internal");
  }
}

void ImplementationVisitor::BeginDebugMacrosFile() {
  // TODO(torque-builer): Can use builder for debug_macros_*_
  std::ostream& source = debug_macros_cc_;
  std::ostream& header = debug_macros_h_;

  source << "#include \"torque-generated/debug-macros.h\"\n\n";
  source << "#include \"src/objects/swiss-name-dictionary.h\"\n";
  source << "#include \"src/objects/ordered-hash-table.h\"\n";
  source << "#include \"src/torque/runtime-support.h\"\n";
  source << "#include \"tools/debug_helper/debug-macro-shims.h\"\n";
  source << "#include \"include/v8-internal.h\"\n";
  source << "\n";

  source << "namespace v8 {\n"
         << "namespace internal {\n"
         << "namespace debug_helper_internal {\n"
         << "\n";

  const char* kHeaderDefine = "V8_GEN_TORQUE_GENERATED_DEBUG_MACROS_H_";
  header << "#ifndef " << kHeaderDefine << "\n";
  header << "#define " << kHeaderDefine << "\n\n";
  header << "#include \"tools/debug_helper/debug-helper-internal.h\"\n";
  header << "#include \"src/numbers/integer-literal.h\"\n";
  header << "\n";

  header << "namespace v8 {\n"
         << "namespace internal {\n"
         << "namespace debug_helper_internal {\n"
         << "\n";
}

void ImplementationVisitor::EndDebugMacrosFile() {
  // TODO(torque-builder): Can use builder for debug_macros_*_
  std::ostream& source = debug_macros_cc_;
  std::ostream& header = debug_macros_h_;

  source << "}  // namespace internal\n"
         << "}  // namespace v8\n"
         << "}  // namespace debug_helper_internal\n"
         << "\n";

  header << "\n}  // namespace internal\n"
         << "}  // namespace v8\n"
         << "}  // namespace debug_helper_internal\n"
         << "\n";
  header << "#endif  // V8_GEN_TORQUE_GENERATED_DEBUG_MACROS_H_\n";
}

void ImplementationVisitor::Visit(NamespaceConstant* decl) {
  Signature signature{{},           std::nullopt, {{}, false}, 0,
                      decl->type(), {},           false};

  BindingsManagersScope bindings_managers_scope;

  cpp::Function f =
      GenerateFunction(nullptr, decl->external_name(), signature, {});

  f.PrintDeclaration(csa_headerfile());

  f.PrintDefinition(csa_ccfile(), [&](std::ostream& stream) {
    stream << "  compiler::CodeAssembler ca_(state_);\n";

    DCHECK(!signature.return_type->IsVoidOrNever());

    assembler_ = CfgAssembler(Stack<const Type*>{});

    VisitResult expression_result = Visit(decl->body());
    VisitResult return_result =
        GenerateImplicitConvert(signature.return_type, expression_result);

    CSAGenerator csa_generator{assembler().Result(), stream};
    Stack<std::string> values = *csa_generator.EmitGraph(Stack<std::string>{});

    assembler_ = std::nullopt;

    stream << "  return ";
    CSAGenerator::EmitCSAValue(return_result, values, stream);
    stream << ";";
  });
}

void ImplementationVisitor::Visit(TypeAlias* alias) {
  if (alias->IsRedeclaration()) return;
  if (const ClassType* class_type = ClassType::DynamicCast(alias->type())) {
    if (class_type->IsExtern() && !class_type->nspace()->IsDefaultNamespace()) {
      Error(
          "extern classes are currently only supported in the default "
          "namespace");
    }
  }
}

class ImplementationVisitor::MacroInliningScope {
 public:
  MacroInliningScope(ImplementationVisitor* visitor, const Macro* macro)
      : visitor_(visitor), macro_(macro) {
    if (!visitor_->inlining_macros_.insert(macro).second) {
      // Recursive macro expansion would just keep going until stack overflow.
      // To avoid crashes, throw an error immediately.
      ReportError("Recursive macro call to ", *macro);
    }
  }
  ~MacroInliningScope() { visitor_->inlining_macros_.erase(macro_); }

 private:
  ImplementationVisitor* visitor_;
  const Macro* macro_;
};

VisitResult ImplementationVisitor::InlineMacro(
    Macro* macro, std::optional<LocationReference> this_reference,
    const std::vector<VisitResult>& arguments,
    const std::vector<Block*> label_blocks) {
  MacroInliningScope macro_inlining_scope(this, macro);
  CurrentScope::Scope current_scope(macro);
  BindingsManagersScope bindings_managers_scope;
  CurrentCallable::Scope current_callable(macro);
  CurrentReturnValue::Scope current_return_value;
  const Signature& signature = macro->signature();
  const Type* return_type = macro->signature().return_type;
  bool can_return = return_type != TypeOracle::GetNeverType();

  BlockBindings<LocalValue> parameter_bindings(&ValueBindingsManager::Get());
  BlockBindings<LocalLabel> label_bindings(&LabelBindingsManager::Get());
  DCHECK_EQ(macro->signature().parameter_names.size(),
            arguments.size() + (this_reference ? 1 : 0));
  DCHECK_EQ(this_reference.has_value(), macro->IsMethod());

  // Bind the this for methods. Methods that modify a struct-type "this" must
  // only be called if the this is in a variable, in which case the
  // LocalValue is non-const. Otherwise, the LocalValue used for the parameter
  // binding is const, and thus read-only, which will cause errors if
  // modified, e.g. when called by a struct method that sets the structs
  // fields. This prevents using temporary struct values for anything other
  // than read operations.
  if (this_reference) {
    DCHECK(macro->IsMethod());
    parameter_bindings.Add(kThisParameterName, LocalValue{*this_reference},
                           true);
    // TODO(v8:12261): Tracking 'this'-binding for kythe led to a few weird
    // issues. Review to fully support 'this' in methods.
  }

  size_t count = 0;
  for (const auto& arg : arguments) {
    if (this_reference && count == signature.implicit_count) count++;
    const bool mark_as_used = signature.implicit_count > count;
    const Identifier* name = macro->parameter_names()[count++];
    Binding<LocalValue>* binding =
        parameter_bindings.Add(name,
                               LocalValue{LocationReference::Temporary(
                                   arg, "parameter " + name->value)},
                               mark_as_used);
    if (GlobalContext::collect_kythe_data()) {
      KytheData::AddBindingDefinition(binding);
    }
  }

  DCHECK_EQ(label_blocks.size(), signature.labels.size());
  for (size_t i = 0; i < signature.labels.size(); ++i) {
    const LabelDeclaration& label_info = signature.labels[i];
    Binding<LocalLabel>* binding = label_bindings.Add(
        label_info.name, LocalLabel{label_blocks[i], label_info.types});
    if (GlobalContext::collect_kythe_data()) {
      KytheData::AddBindingDefinition(binding);
    }
  }

  Block* macro_end;
  std::optional<Binding<LocalLabel>> macro_end_binding;
  if (can_return) {
    Stack<const Type*> stack = assembler().CurrentStack();
    std::vector<const Type*> lowered_return_types = LowerType(return_type);
    stack.PushMany(lowered_return_types);
    if (!return_type->IsConstexpr()) {
      SetReturnValue(VisitResult(return_type,
                                 stack.TopRange(lowered_return_types.size())));
    }
    // The stack copy used to initialize the _macro_end block is only used
    // as a template for the actual gotos generated by return statements. It
    // doesn't correspond to any real return values, and thus shouldn't contain
    // top types, because these would pollute actual return value types that get
    // unioned with them for return statements, erroneously forcing them to top.
    for (auto i = stack.begin(); i != stack.end(); ++i) {
      if ((*i)->IsTopType()) {
        *i = TopType::cast(*i)->source_type();
      }
    }
    macro_end = assembler().NewBlock(std::move(stack));
    macro_end_binding.emplace(&LabelBindingsManager::Get(), kMacroEndLabelName,
                              LocalLabel{macro_end, {return_type}});
  } else {
    SetReturnValue(VisitResult::NeverResult());
  }

  const Type* result = Visit(*macro->body());

  if (result->IsNever()) {
    if (!return_type->IsNever() && !macro->HasReturns()) {
      std::stringstream s;
      s << "macro " << macro->ReadableName()
        << " that never returns must have return type never";
      ReportError(s.str());
    }
  } else {
    if (return_type->IsNever()) {
      std::stringstream s;
      s << "macro " << macro->ReadableName()
        << " has implicit return at end of its declartion but return type "
           "never";
      ReportError(s.str());
    } else if (!macro->signature().return_type->IsVoid()) {
      std::stringstream s;
      s << "macro " << macro->ReadableName()
        << " expects to return a value but doesn't on all paths";
      ReportError(s.str());
    }
  }
  if (!result->IsNever()) {
    assembler().Goto(macro_end);
  }

  if (macro->HasReturns() || !result->IsNever()) {
    assembler().Bind(macro_end);
  }

  return GetAndClearReturnValue();
}

void ImplementationVisitor::VisitMacroCommon(Macro* macro) {
  CurrentCallable::Scope current_callable(macro);
  const Signature& signature = macro->signature();
  const Type* return_type = macro->signature().return_type;
  bool can_return = return_type != TypeOracle::GetNeverType();
  bool has_return_value =
      can_return && return_type != TypeOracle::GetVoidType();

  cpp::Function f = GenerateMacroFunctionDeclaration(macro);
  f.PrintDeclaration(csa_headerfile());
  csa_headerfile() << "\n";

  cpp::File csa_cc(csa_ccfile());

  // Avoid multiple-definition errors since it is possible for multiple
  // generated -inl.inc files to all contain function definitions for the same
  // Torque macro.
  std::optional<cpp::IncludeGuardScope> include_guard;
  if (output_type_ == OutputType::kCC) {
    include_guard.emplace(&csa_cc, "V8_INTERNAL_DEFINED_"s + macro->CCName());
  } else if (output_type_ == OutputType::kCCDebug) {
    include_guard.emplace(&csa_cc,
                          "V8_INTERNAL_DEFINED_"s + macro->CCDebugName());
  }

  f.PrintBeginDefinition(csa_ccfile());

  if (output_type_ == OutputType::kCC) {
    // For now, generated C++ is only for field offset computations. If we ever
    // generate C++ code that can allocate, then it should be handlified.
    csa_ccfile() << "  DisallowGarbageCollection no_gc;\n";
  } else if (output_type_ == OutputType::kCSA) {
    csa_ccfile() << "  compiler::CodeAssembler ca_(state_);\n";
    csa_ccfile()
        << "  compiler::CodeAssembler::SourcePositionScope pos_scope(&ca_);\n";
  }

  Stack<std::string> lowered_parameters;
  Stack<const Type*> lowered_parameter_types;

  std::vector<VisitResult> arguments;

  std::optional<LocationReference> this_reference;
  if (Method* method = Method::DynamicCast(macro)) {
    const Type* this_type = method->aggregate_type();
    LowerParameter(this_type, ExternalParameterName(kThisParameterName),
                   &lowered_parameters);
    StackRange range = lowered_parameter_types.PushMany(LowerType(this_type));
    VisitResult this_result = VisitResult(this_type, range);
    // For classes, mark 'this' as a temporary to prevent assignment to it.
    // Note that using a VariableAccess for non-class types is technically
    // incorrect because changes to the 'this' variable do not get reflected
    // to the caller. Therefore struct methods should always be inlined and a
    // C++ version should never be generated, since it would be incorrect.
    // However, in order to be able to type- and semantics-check even unused
    // struct methods, set the this_reference to be the local variable copy of
    // the passed-in this, which allows the visitor to at least find and report
    // errors.
    this_reference =
        (this_type->IsClassType())
            ? LocationReference::Temporary(this_result, "this parameter")
            : LocationReference::VariableAccess(this_result);
  }

  for (size_t i = 0; i < macro->signature().parameter_names.size(); ++i) {
    if (this_reference && i == macro->signature().implicit_count) continue;
    const std::string& name = macro->parameter_names()[i]->value;
    std::string external_name = ExternalParameterName(name);
    const Type* type = macro->signature().types()[i];

    if (type->IsConstexpr()) {
      arguments.push_back(VisitResult(type, external_name));
    } else {
      LowerParameter(type, external_name, &lowered_parameters);
      StackRange range = lowered_parameter_types.PushMany(LowerType(type));
      arguments.push_back(VisitResult(type, range));
    }
  }

  DCHECK_EQ(lowered_parameters.Size(), lowered_parameter_types.Size());
  assembler_ = CfgAssembler(lowered_parameter_types);

  std::vector<Block*> label_blocks;
  for (const LabelDeclaration& label_info : signature.labels) {
    Stack<const Type*> label_input_stack;
    for (const Type* type : label_info.types) {
      label_input_stack.PushMany(LowerType(type));
    }
    Block* block = assembler().NewBlock(std::move(label_input_stack));
    label_blocks.push_back(block);
  }

  VisitResult return_value =
      InlineMacro(macro, this_reference, arguments, label_blocks);
  Block* end = assembler().NewBlock();
  if (return_type != TypeOracle::GetNeverType()) {
    assembler().Goto(end);
  }

  for (size_t i = 0; i < label_blocks.size(); ++i) {
    Block* label_block = label_blocks[i];
    const LabelDeclaration& label_info = signature.labels[i];
    assembler().Bind(label_block);
    std::vector<std::string> label_parameter_variables;
    for (size_t j = 0; j < label_info.types.size(); ++j) {
      LowerLabelParameter(label_info.types[j],
                          ExternalLabelParameterName(label_info.name->value, j),
                          &label_parameter_variables);
    }
    assembler().Emit(GotoExternalInstruction{
        ExternalLabelName(label_info.name->value), label_parameter_variables});
  }

  if (return_type != TypeOracle::GetNeverType()) {
    assembler().Bind(end);
  }

  std::optional<Stack<std::string>> values;
  if (output_type_ == OutputType::kCC) {
    CCGenerator cc_generator{assembler().Result(), csa_ccfile()};
    values = cc_generator.EmitGraph(lowered_parameters);
  } else if (output_type_ == OutputType::kCCDebug) {
    CCGenerator cc_generator{assembler().Result(), csa_ccfile(), true};
    values = cc_generator.EmitGraph(lowered_parameters);
  } else {
    CSAGenerator csa_generator{assembler().Result(), csa_ccfile()};
    values = csa_generator.EmitGraph(lowered_parameters);
  }

  assembler_ = std::nullopt;

  if (has_return_value) {
    csa_ccfile() << "  return ";
    if (output_type_ == OutputType::kCCDebug) {
      csa_ccfile() << "{d::MemoryAccessResult::kOk, ";
      CCGenerator::EmitCCValue(return_value, *values, csa_ccfile());
      csa_ccfile() << "}";
    } else if (output_type_ == OutputType::kCC) {
      CCGenerator::EmitCCValue(return_value, *values, csa_ccfile());
    } else {
      CSAGenerator::EmitCSAValue(return_value, *values, csa_ccfile());
    }
    csa_ccfile() << ";\n";
  }
  f.PrintEndDefinition(csa_ccfile());

  include_guard.reset();
}

void ImplementationVisitor::Visit(TorqueMacro* macro) {
  VisitMacroCommon(macro);
}

void ImplementationVisitor::Visit(Method* method) {
  DCHECK(!method->IsExternal());
  VisitMacroCommon(method);
}

namespace {

std::string AddParameter(size_t i, Builtin* builtin,
                         Stack<std::string>* parameters,
                         Stack<const Type*>* parameter_types,
                         BlockBindings<LocalValue>* parameter_bindings,
                         bool mark_as_used) {
  const Identifier* name = builtin->signature().parameter_names[i];
  const Type* type = builtin->signature().types()[i];
  std::string external_name = "parameter" + std::to_string(i);
  parameters->Push(external_name);
  StackRange range = parameter_types->PushMany(LowerType(type));
  Binding<LocalValue>* binding = parameter_bindings->Add(
      name,
      LocalValue{LocationReference::Temporary(VisitResult(type, range),
                                              "parameter " + name->value)},
      mark_as_used);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddBindingDefinition(binding);
  }
  return external_name;
}

}  // namespace

void ImplementationVisitor::Visit(Builtin* builtin) {
  if (builtin->IsExternal()) return;
  CurrentScope::Scope current_scope(builtin);
  CurrentCallable::Scope current_callable(builtin);
  CurrentReturnValue::Scope current_return_value;

  const std::string& name = builtin->ExternalName();
  const Signature& signature = builtin->signature();
  csa_ccfile() << "TF_BUILTIN(" << name << ", CodeStubAssembler) {\n"
               << "  compiler::CodeAssemblerState* state_ = state();"
               << "  compiler::CodeAssembler ca_(state());\n";

  Stack<const Type*> parameter_types;
  Stack<std::string> parameters;

  BindingsManagersScope bindings_managers_scope;

  BlockBindings<LocalValue> parameter_bindings(&ValueBindingsManager::Get());

  if (builtin->IsVarArgsJavaScript() || builtin->IsFixedArgsJavaScript()) {
    if (builtin->IsVarArgsJavaScript()) {
      DCHECK(signature.parameter_types.var_args);
      if (signature.ExplicitCount() > 0) {
        Error("Cannot mix explicit parameters with varargs.")
            .Position(signature.parameter_names[signature.implicit_count]->pos);
      }

      csa_ccfile() << "  TNode<Word32T> argc = UncheckedParameter<Word32T>("
                   << "Descriptor::kJSActualArgumentsCount);\n";
      csa_ccfile() << "  TNode<IntPtrT> "
                      "arguments_length(ChangeInt32ToIntPtr(UncheckedCast<"
                      "Int32T>(argc)));\n";
      csa_ccfile() << "  TNode<RawPtrT> arguments_frame = "
                      "UncheckedCast<RawPtrT>(LoadFramePointer());\n";
      csa_ccfile()
          << "  TorqueStructArguments "
             "torque_arguments(GetFrameArguments(arguments_frame, "
             "arguments_length, FrameArgumentsArgcType::kCountIncludesReceiver"
          << "));\n";
      csa_ccfile()
          << "  CodeStubArguments arguments(this, torque_arguments);\n";

      parameters.Push("torque_arguments.frame");
      parameters.Push("torque_arguments.base");
      parameters.Push("torque_arguments.length");
      parameters.Push("torque_arguments.actual_count");
      const Type* arguments_type = TypeOracle::GetArgumentsType();
      StackRange range = parameter_types.PushMany(LowerType(arguments_type));
      parameter_bindings.Add(*signature.arguments_variable,
                             LocalValue{LocationReference::Temporary(
                                 VisitResult(arguments_type, range),
                                 "parameter " + *signature.arguments_variable)},
                             true);
    }

    for (size_t i = 0; i < signature.implicit_count; ++i) {
      const std::string& param_name = signature.parameter_names[i]->value;
      SourcePosition param_pos = signature.parameter_names[i]->pos;
      std::string generated_name = AddParameter(
          i, builtin, &parameters, &parameter_types, &parameter_bindings, true);
      const Type* actual_type = signature.parameter_types.types[i];
      std::vector<const Type*> expected_types;
      if (param_name == "context") {
        csa_ccfile() << "  TNode<NativeContext> " << generated_name
                     << " = UncheckedParameter<NativeContext>("
                     << "Descriptor::kContext);\n";
        csa_ccfile() << "  USE(" << generated_name << ");\n";
        expected_types = {TypeOracle::GetNativeContextType(),
                          TypeOracle::GetContextType()};
      } else if (param_name == "receiver") {
        csa_ccfile()
            << "  TNode<Object> " << generated_name << " = "
            << (builtin->IsVarArgsJavaScript()
                    ? "arguments.GetReceiver()"
                    : "UncheckedParameter<Object>(Descriptor::kReceiver)")
            << ";\n";
        csa_ccfile() << "  USE(" << generated_name << ");\n";
        expected_types = {TypeOracle::GetJSAnyType()};
      } else if (param_name == "newTarget") {
        csa_ccfile() << "  TNode<Object> " << generated_name
                     << " = UncheckedParameter<Object>("
                     << "Descriptor::kJSNewTarget);\n";
        csa_ccfile() << "USE(" << generated_name << ");\n";
        expected_types = {TypeOracle::GetJSAnyType()};
      } else if (param_name == "target") {
        csa_ccfile() << "  TNode<JSFunction> " << generated_name
                     << " = UncheckedParameter<JSFunction>("
                     << "Descriptor::kJSTarget);\n";
        csa_ccfile() << "USE(" << generated_name << ");\n";
        expected_types = {TypeOracle::GetJSFunctionType()};
      } else {
        Error(
            "Unexpected implicit parameter \"", param_name,
            "\" for JavaScript calling convention, "
            "expected \"context\", \"receiver\", \"target\", or \"newTarget\"")
            .Position(param_pos);
        expected_types = {actual_type};
      }
      if (std::find(expected_types.begin(), expected_types.end(),
                    actual_type) == expected_types.end()) {
        Error("According to JavaScript calling convention, expected parameter ",
              param_name, " to have type ", PrintList(expected_types, " or "),
              " but found type ", *actual_type)
            .Position(param_pos);
      }
    }

    for (size_t i = signature.implicit_count;
         i < signature.parameter_names.size(); ++i) {
      const std::string& parameter_name = signature.parameter_names[i]->value;
      const Type* type = signature.types()[i];
      const bool mark_as_used = signature.implicit_count > i;
      std::string var = AddParameter(i, builtin, &parameters, &parameter_types,
                                     &parameter_bindings, mark_as_used);
      csa_ccfile() << "  " << type->GetGeneratedTypeName() << " " << var
                   << " = "
                   << "UncheckedParameter<" << type->GetGeneratedTNodeTypeName()
                   << ">(Descriptor::k" << CamelifyString(parameter_name)
                   << ");\n";
      csa_ccfile() << "  USE(" << var << ");\n";
    }

  } else {
    DCHECK(builtin->IsStub());

    for (size_t i = 0; i < signature.parameter_names.size(); ++i) {
      const std::string& parameter_name = signature.parameter_names[i]->value;
      const Type* type = signature.types()[i];
      const bool mark_as_used = signature.implicit_count > i;
      std::string var = AddParameter(i, builtin, &parameters, &parameter_types,
                                     &parameter_bindings, mark_as_used);
      csa_ccfile() << "  " << type->GetGeneratedTypeName() << " " << var
                   << " = "
                   << "UncheckedParameter<" << type->GetGeneratedTNodeTypeName()
                   << ">(Descriptor::k" << CamelifyString(parameter_name)
                   << ");\n";
      csa_ccfile() << "  USE(" << var << ");\n";
    }
  }
  assembler_ = CfgAssembler(parameter_types);
  const Type* body_result = Visit(*builtin->body());
  if (body_result != TypeOracle::GetNeverType()) {
    ReportError("control reaches end of builtin, expected return of a value");
  }
  CSAGenerator csa_generator{assembler().Result(), csa_ccfile(),
                             builtin->kind()};
  csa_generator.EmitGraph(parameters);
  assembler_ = std::nullopt;
  csa_ccfile() << "}\n\n";
}

const Type* ImplementationVisitor::Visit(VarDeclarationStatement* stmt) {
  BlockBindings<LocalValue> block_bindings(&ValueBindingsManager::Get());
  return Visit(stmt, &block_bindings);
}

const Type* ImplementationVisitor::Visit(
    VarDeclarationStatement* stmt, BlockBindings<LocalValue>* block_bindings) {
  // const qualified variables are required to be initialized properly.
  if (stmt->const_qualified && !stmt->initializer) {
    ReportError("local constant \"", stmt->name, "\" is not initialized.");
  }

  std::optional<const Type*> type;
  if (stmt->type) {
    type = TypeVisitor::ComputeType(*stmt->type);
  }
  std::optional<VisitResult> init_result;
  if (stmt->initializer) {
    StackScope scope(this);
    init_result = Visit(*stmt->initializer);
    if (type) {
      init_result = GenerateImplicitConvert(*type, *init_result);
    }
    type = init_result->type();
    if ((*type)->IsConstexpr() && !stmt->const_qualified) {
      Error("Use 'const' instead of 'let' for variable '", stmt->name->value,
            "' of constexpr type '", (*type)->ToString(), "'.")
          .Position(stmt->name->pos)
          .Throw();
    }
    init_result = scope.Yield(*init_result);
  } else {
    DCHECK(type.has_value());
    if ((*type)->IsConstexpr()) {
      ReportError("constexpr variables need an initializer");
    }
    TypeVector lowered_types = LowerType(*type);
    for (const Type* t : lowered_types) {
      assembler().Emit(PushUninitializedInstruction{TypeOracle::GetTopType(
          "uninitialized variable '" + stmt->name->value + "' of type " +
              t->ToString() + " originally defined at " +
              PositionAsString(stmt->pos),
          t)});
    }
    init_result =
        VisitResult(*type, assembler().TopRange(lowered_types.size()));
  }
  LocationReference ref = stmt->const_qualified
                              ? LocationReference::Temporary(
                                    *init_result, "const " + stmt->name->value)
                              : LocationReference::VariableAccess(*init_result);
  block_bindings->Add(stmt->name, LocalValue{std::move(ref)});
  return TypeOracle::GetVoidType();
}

const Type* ImplementationVisitor::Visit(TailCallStatement* stmt) {
  return Visit(stmt->call, true).type();
}

VisitResult ImplementationVisitor::Visit(ConditionalExpression* expr) {
  Block* true_block = assembler().NewBlock(assembler().CurrentStack());
  Block* false_block = assembler().NewBlock(assembler().CurrentStack());
  Block* done_block = assembler().NewBlock();
  Block* true_conversion_block = assembler().NewBlock();
  GenerateExpressionBranch(expr->condition, true_block, false_block);

  VisitResult left;
  VisitResult right;

  {
    // The code for both paths of the conditional need to be generated first
    // before evaluating the conditional expression because the common type of
    // the result of both the true and false of the condition needs to be known
    // to convert both branches to a common type.
    assembler().Bind(true_block);
    StackScope left_scope(this);
    left = Visit(expr->if_true);
    assembler().Goto(true_conversion_block);

    const Type* common_type;
    {
      assembler().Bind(false_block);
      StackScope right_scope(this);
      right = Visit(expr->if_false);
      common_type = GetCommonType(left.type(), right.type());
      right = right_scope.Yield(GenerateImplicitConvert(common_type, right));
      assembler().Goto(done_block);
    }

    assembler().Bind(true_conversion_block);
    left = left_scope.Yield(GenerateImplicitConvert(common_type, left));
    assembler().Goto(done_block);
  }

  assembler().Bind(done_block);
  CHECK_EQ(left, right);
  return left;
}

VisitResult ImplementationVisitor::Visit(LogicalOrExpression* expr) {
  StackScope outer_scope(this);
  VisitResult left_result = Visit(expr->left);

  if (left_result.type()->IsConstexprBool()) {
    VisitResult right_result = Visit(expr->right);
    if (!right_result.type()->IsConstexprBool()) {
      ReportError(
          "expected type constexpr bool on right-hand side of operator "
          "||");
    }
    return VisitResult(TypeOracle::GetConstexprBoolType(),
                       std::string("(") + left_result.constexpr_value() +
                           " || " + right_result.constexpr_value() + ")");
  }

  Block* true_block = assembler().NewBlock();
  Block* false_block = assembler().NewBlock();
  Block* done_block = assembler().NewBlock();

  left_result = GenerateImplicitConvert(TypeOracle::GetBoolType(), left_result);
  GenerateBranch(left_result, true_block, false_block);

  assembler().Bind(true_block);
  VisitResult true_result = GenerateBoolConstant(true);
  assembler().Goto(done_block);

  assembler().Bind(false_block);
  VisitResult false_result;
  {
    StackScope false_block_scope(this);
    false_result = false_block_scope.Yield(
        GenerateImplicitConvert(TypeOracle::GetBoolType(), Visit(expr->right)));
  }
  assembler().Goto(done_block);

  assembler().Bind(done_block);
  DCHECK_EQ(true_result, false_result);
  return outer_scope.Yield(true_result);
}

VisitResult ImplementationVisitor::Visit(LogicalAndExpression* expr) {
  StackScope outer_scope(this);
  VisitResult left_result = Visit(expr->left);

  if (left_result.type()->IsConstexprBool()) {
    VisitResult right_result = Visit(expr->right);
    if (!right_result.type()->IsConstexprBool()) {
      ReportError(
          "expected type constexpr bool on right-hand side of operator "
          "&&");
    }
    return VisitResult(TypeOracle::GetConstexprBoolType(),
                       std::string("(") + left_result.constexpr_value() +
                           " && " + right_result.constexpr_value() + ")");
  }

  Block* true_block = assembler().NewBlock();
  Block* false_block = assembler().NewBlock();
  Block* done_block = assembler().NewBlock();

  left_result = GenerateImplicitConvert(TypeOracle::GetBoolType(), left_result);
  GenerateBranch(left_result, true_block, false_block);

  assembler().Bind(true_block);
  VisitResult true_result;
  {
    StackScope true_block_scope(this);
    VisitResult right_result = Visit(expr->right);
    if (TryGetSourceForBitfieldExpression(expr->left) != nullptr &&
        TryGetSourceForBitfieldExpression(expr->right) != nullptr &&
        TryGetSourceForBitfieldExpression(expr->left)->value ==
            TryGetSourceForBitfieldExpression(expr->right)->value) {
      Lint(
          "Please use & rather than && when checking multiple bitfield "
          "values, to avoid complexity in generated code.");
    }
    true_result = true_block_scope.Yield(
        GenerateImplicitConvert(TypeOracle::GetBoolType(), right_result));
  }
  assembler().Goto(done_block);

  assembler().Bind(false_block);
  VisitResult false_result = GenerateBoolConstant(false);
  assembler().Goto(done_block);

  assembler().Bind(done_block);
  DCHECK_EQ(true_result, false_result);
  return outer_scope.Yield(true_result);
}

VisitResult ImplementationVisitor::Visit(IncrementDecrementExpression* expr) {
  StackScope scope(this);
  LocationReference location_ref = GetLocationReference(expr->location);
  VisitResult current_value = GenerateFetchFromLocation(location_ref);
  VisitResult one = {TypeOracle::GetConstInt31Type(), "1"};
  Arguments args;
  args.parameters = {current_value, one};
  VisitResult assignment_value = GenerateCall(
      expr->op == IncrementDecrementOperator::kIncrement ? "+" : "-", args);
  GenerateAssignToLocation(location_ref, assignment_value);
  return scope.Yield(expr->postfix ? current_value : assignment_value);
}

VisitResult ImplementationVisitor::Visit(AssignmentExpression* expr) {
  StackScope scope(this);
  LocationReference location_ref = GetLocationReference(expr->location);
  VisitResult assignment_value;
  if (expr->op) {
    VisitResult location_value = GenerateFetchFromLocation(location_ref);
    assignment_value = Visit(expr->value);
    Arguments args;
    args.parameters = {location_value, assignment_value};
    assignment_value = GenerateCall(*expr->op, args);
    GenerateAssignToLocation(location_ref, assignment_value);
  } else {
    assignment_value = Visit(expr->value);
    GenerateAssignToLocation(location_ref, assignment_value);
  }
  return scope.Yield(assignment_value);
}

VisitResult ImplementationVisitor::Visit(FloatingPointLiteralExpression* expr) {
  const Type* result_type = TypeOracle::GetConstFloat64Type();
  std::stringstream str;
  str << std::setprecision(std::numeric_limits<double>::digits10 + 1)
      << expr->value;
  return VisitResult{result_type, str.str()};
}

VisitResult ImplementationVisitor::Visit(IntegerLiteralExpression* expr) {
  const Type* result_type = TypeOracle::GetIntegerLiteralType();
  std::stringstream str;
  str << "IntegerLiteral("
      << (expr->value.is_negative() ? "true, 0x" : "false, 0x") << std::hex
      << expr->value.absolute_value() << std::dec << "ull)";
  return VisitResult{result_type, str.str()};
}

VisitResult ImplementationVisitor::Visit(AssumeTypeImpossibleExpression* expr) {
  VisitResult result = Visit(expr->expression);
  const Type* result_type = SubtractType(
      result.type(), TypeVisitor::ComputeType(expr->excluded_type));
  if (result_type->IsNever()) {
    ReportError("unreachable code");
  }
  CHECK_EQ(LowerType(result_type), TypeVector{result_type});
  assembler().Emit(UnsafeCastInstruction{result_type});
  result.SetType(result_type);
  return result;
}

VisitResult ImplementationVisitor::Visit(StringLiteralExpression* expr) {
  return VisitResult{
      TypeOracle::GetConstStringType(),
      "\"" + expr->literal.substr(1, expr->literal.size() - 2) + "\""};
}

VisitResult ImplementationVisitor::GetBuiltinCode(Builtin* builtin) {
  if (builtin->IsExternal() || builtin->kind() != Builtin::kStub) {
    ReportError(
        "creating function pointers is only allowed for internal builtins with "
        "stub linkage");
  }
  const Type* type = TypeOracle::GetBuiltinPointerType(
      builtin->signature().parameter_types.types,
      builtin->signature().return_type);
  assembler().Emit(
      PushBuiltinPointerInstruction{builtin->ExternalName(), type});
  return VisitResult(type, assembler().TopRange(1));
}

VisitResult ImplementationVisitor::Visit(LocationExpression* expr) {
  StackScope scope(this);
  return scope.Yield(GenerateFetchFromLocation(GetLocationReference(expr)));
}

VisitResult ImplementationVisitor::Visit(FieldAccessExpression* expr) {
  StackScope scope(this);
  LocationReference location = GetLocationReference(expr);
  if (location.IsBitFieldAccess()) {
    if (auto* identifier = IdentifierExpression::DynamicCast(expr->object)) {
      bitfield_expressions_[expr] = identifier->name;
    }
  }
  return scope.Yield(GenerateFetchFromLocation(location));
}

const Type* ImplementationVisitor::Visit(GotoStatement* stmt) {
  Binding<LocalLabel>* label = LookupLabel(stmt->label->value);
  size_t parameter_count = label->parameter_types.size();
  if (stmt->arguments.size() != parameter_count) {
    ReportError("goto to label has incorrect number of parameters (expected ",
                parameter_count, " found ", stmt->arguments.size(), ")");
  }

  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::AddDefinition(stmt->label->pos,
                                      label->declaration_position());
  }
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddBindingUse(stmt->label->pos, label);
  }

  size_t i = 0;
  StackRange arguments = assembler().TopRange(0);
  for (Expression* e : stmt->arguments) {
    StackScope scope(this);
    VisitResult result = Visit(e);
    const Type* parameter_type = label->parameter_types[i++];
    result = GenerateImplicitConvert(parameter_type, result);
    arguments.Extend(scope.Yield(result).stack_range());
  }

  assembler().Goto(label->block, arguments.Size());
  return TypeOracle::GetNeverType();
}

const Type* ImplementationVisitor::Visit(IfStatement* stmt) {
  bool has_else = stmt->if_false.has_value();

  if (stmt->is_constexpr) {
    VisitResult expression_result = Visit(stmt->condition);

    if (!(expression_result.type() == TypeOracle::GetConstexprBoolType())) {
      std::stringstream stream;
      stream << "expression should return type constexpr bool "
             << "but returns type " << *expression_result.type();
      ReportError(stream.str());
    }

    Block* true_block = assembler().NewBlock();
    Block* false_block = assembler().NewBlock();
    Block* done_block = assembler().NewBlock();

    assembler().Emit(ConstexprBranchInstruction{
        expression_result.constexpr_value(), true_block, false_block});

    assembler().Bind(true_block);
    const Type* left_result = Visit(stmt->if_true);
    if (left_result == TypeOracle::GetVoidType()) {
      assembler().Goto(done_block);
    }

    assembler().Bind(false_block);
    const Type* right_result = TypeOracle::GetVoidType();
    if (has_else) {
      right_result = Visit(*stmt->if_false);
    }
    if (right_result == TypeOracle::GetVoidType()) {
      assembler().Goto(done_block);
    }

    if (left_result->IsNever() != right_result->IsNever()) {
      std::stringstream stream;
      stream << "either both or neither branches in a constexpr if statement "
                "must reach their end at"
             << PositionAsString(stmt->pos);
      ReportError(stream.str());
    }

    if (left_result != TypeOracle::GetNeverType()) {
      assembler().Bind(done_block);
    }
    return left_result;
  } else {
    Block* true_block = assembler().NewBlock(assembler().CurrentStack(),
                                             IsDeferred(stmt->if_true));
    Block* false_block =
        assembler().NewBlock(assembler().CurrentStack(),
                             stmt->if_false && IsDeferred(*stmt->if_false));
    GenerateExpressionBranch(stmt->condition, true_block, false_block);

    Block* done_block;
    bool live = false;
    if (has_else) {
      done_block = assembler().NewBlock();
    } else {
      done_block = false_block;
      live = true;
    }

    assembler().Bind(true_block);
    {
      const Type* result = Visit(stmt->if_true);
      if (result == TypeOracle::GetVoidType()) {
        live = true;
        assembler().Goto(done_block);
      }
    }

    if (has_else) {
      assembler().Bind(false_block);
      const Type* result = Visit(*stmt->if_false);
      if (result == TypeOracle::GetVoidType()) {
        live = true;
        assembler().Goto(done_block);
      }
    }

    if (live) {
      assembler().Bind(done_block);
    }
    return live ? TypeOracle::GetVoidType() : TypeOracle::GetNeverType();
  }
}

const Type* ImplementationVisitor::Visit(WhileStatement* stmt) {
  Block* body_block = assembler().NewBlock(assembler().CurrentStack());
  Block* exit_block = assembler().NewBlock(assembler().CurrentStack());

  Block* header_block = assembler().NewBlock();
  assembler().Goto(header_block);

  assembler().Bind(header_block);
  GenerateExpressionBranch(stmt->condition, body_block, exit_block);

  assembler().Bind(body_block);
  {
    BreakContinueActivator activator{exit_block, header_block};
    const Type* body_result = Visit(stmt->body);
    if (body_result != TypeOracle::GetNeverType()) {
      assembler().Goto(header_block);
    }
  }

  assembler().Bind(exit_block);
  return TypeOracle::GetVoidType();
}

const Type* ImplementationVisitor::Visit(BlockStatement* block) {
  BlockBindings<LocalValue> block_bindings(&ValueBindingsManager::Get());
  const Type* type = TypeOracle::GetVoidType();
  for (Statement* s : block->statements) {
    CurrentSourcePosition::Scope source_position(s->pos);
    if (type->IsNever()) {
      ReportError("statement after non-returning statement");
    }
    if (auto* var_declaration = VarDeclarationStatement::DynamicCast(s)) {
      type = Visit(var_declaration, &block_bindings);
    } else {
      type = Visit(s);
    }
  }
  return type;
}

const Type* ImplementationVisitor::Visit(DebugStatement* stmt) {
  std::string reason;
  const Type* return_type;
  AbortInstruction::Kind kind;
  switch (stmt->kind) {
    case DebugStatement::Kind::kUnreachable:
      // Use the same string as in C++ to simplify fuzzer pattern-matching.
      reason = base::kUnreachableCodeMessage;
      return_type = TypeOracle::GetNeverType();
      kind = AbortInstruction::Kind::kUnreachable;
      break;
    case DebugStatement::Kind::kDebug:
      reason = "debug break";
      return_type = TypeOracle::GetVoidType();
      kind = AbortInstruction::Kind::kDebugBreak;
      break;
  }
#if defined(DEBUG)
  assembler().Emit(PrintErrorInstruction{"halting because of " + reason +
                                         " at " + PositionAsString(stmt->pos)});
#endif
  assembler().Emit(AbortInstruction{kind});
  return return_type;
}

namespace {

std::string FormatAssertSource(const std::string& str) {
  // Replace all whitespace characters with a space character.
  std::string str_no_newlines = str;
  std::replace_if(
      str_no_newlines.begin(), str_no_newlines.end(),
      [](unsigned char c) { return isspace(c); }, ' ');

  // str might include indentation, squash multiple space characters into one.
  std::string result;
  std::unique_copy(str_no_newlines.begin(), str_no_newlines.end(),
                   std::back_inserter(result),
                   [](char a, char b) { return a == ' ' && b == ' '; });
  return result;
}

}  // namespace

const Type* ImplementationVisitor::Visit(AssertStatement* stmt) {
  if (stmt->kind == AssertStatement::AssertKind::kStaticAssert) {
    std::string message =
        "static_assert(" + stmt->source + ") at " + ToString(stmt->pos);
    GenerateCall(QualifiedName({"", TORQUE_INTERNAL_NAMESPACE_STRING},
                               STATIC_ASSERT_MACRO_STRING),
                 Arguments{{Visit(stmt->expression),
                            VisitResult(TypeOracle::GetConstexprStringType(),
                                        StringLiteralQuote(message))},
                           {}});
    return TypeOracle::GetVoidType();
  }
  // When the sandbox is off, sbxchecks become dchecks.
  DCHECK_IMPLIES(stmt->kind == AssertStatement::AssertKind::kSbxCheck,
                 V8_ENABLE_SANDBOX_BOOL);
  bool do_check = stmt->kind != AssertStatement::AssertKind::kDcheck ||
                  GlobalContext::force_assert_statements();
#if defined(DEBUG)
  do_check = true;
#endif
  Block* resume_block;

  if (!do_check) {
    Block* unreachable_block = assembler().NewBlock(assembler().CurrentStack());
    resume_block = assembler().NewBlock(assembler().CurrentStack());
    assembler().Goto(resume_block);
    assembler().Bind(unreachable_block);
  }

  // CSA_DCHECK & co. are not used here on purpose for two reasons. First,
  // Torque allows and handles two types of expressions in the if protocol
  // automagically, ones that return TNode<BoolT> and those that use the
  // BranchIf(..., Label* true, Label* false) idiom. Because the machinery to
  // handle this is embedded in the expression handling and to it's not
  // possible to make the decision to use CSA_DCHECK or CSA_DCHECK_BRANCH
  // isn't trivial up-front. Secondly, on failure, the assert text should be
  // the corresponding Torque code, not the -gen.cc code, which would be the
  // case when using CSA_DCHECK_XXX.
  Block* true_block = assembler().NewBlock(assembler().CurrentStack());
  Block* false_block = assembler().NewBlock(assembler().CurrentStack(), true);
  GenerateExpressionBranch(stmt->expression, true_block, false_block);

  assembler().Bind(false_block);

  assembler().Emit(AbortInstruction{
      AbortInstruction::Kind::kAssertionFailure,
      "Torque assert '" + FormatAssertSource(stmt->source) + "' failed"});

  assembler().Bind(true_block);

  if (!do_check) {
    assembler().Bind(resume_block);
  }

  return TypeOracle::GetVoidType();
}

const Type* ImplementationVisitor::Visit(ExpressionStatement* stmt) {
  const Type* type = Visit(stmt->expression).type();
  return type->IsNever() ? type : TypeOracle::GetVoidType();
}

const Type* ImplementationVisitor::Visit(ReturnStatement* stmt) {
  Callable* current_callable = CurrentCallable::Get();
  if (current_callable->signature().return_type->IsNever()) {
    std::stringstream s;
    s << "cannot return from a function with return type never";
    ReportError(s.str());
  }
  LocalLabel* end =
      current_callable->IsMacro() ? LookupLabel(kMacroEndLabelName) : nullptr;
  if (current_callable->HasReturnValue()) {
    if (!stmt->value) {
      std::stringstream s;
      s << "return expression needs to be specified for a return type of "
        << *current_callable->signature().return_type;
      ReportError(s.str());
    }
    VisitResult expression_result = Visit(*stmt->value);
    VisitResult return_result = GenerateImplicitConvert(
        current_callable->signature().return_type, expression_result);
    if (current_callable->IsMacro()) {
      if (return_result.IsOnStack()) {
        StackRange return_value_range =
            GenerateLabelGoto(end, return_result.stack_range());
        SetReturnValue(VisitResult(return_result.type(), return_value_range));
      } else {
        GenerateLabelGoto(end);
        SetReturnValue(return_result);
      }
    } else if (current_callable->IsBuiltin()) {
      assembler().Emit(ReturnInstruction{
          LoweredSlotCount(current_callable->signature().return_type)});
    } else {
      UNREACHABLE();
    }
  } else {
    if (stmt->value) {
      std::stringstream s;
      s << "return expression can't be specified for a void or never return "
           "type";
      ReportError(s.str());
    }
    GenerateLabelGoto(end);
  }
  current_callable->IncrementReturns();
  return TypeOracle::GetNeverType();
}

VisitResult ImplementationVisitor::Visit(TryLabelExpression* expr) {
  size_t parameter_count = expr->label_block->parameters.names.size();
  std::vector<VisitResult> parameters;

  Block* label_block = nullptr;
  Block* done_block = assembler().NewBlock();
  VisitResult try_result;

  {
    CurrentSourcePosition::Scope source_position(expr->label_block->pos);
    if (expr->label_block->parameters.has_varargs) {
      ReportError("cannot use ... for label parameters");
    }
    Stack<const Type*> label_input_stack = assembler().CurrentStack();
    TypeVector parameter_types;
    for (size_t i = 0; i < parameter_count; ++i) {
      const Type* type =
          TypeVisitor::ComputeType(expr->label_block->parameters.types[i]);
      parameter_types.push_back(type);
      if (type->IsConstexpr()) {
        ReportError("no constexpr type allowed for label arguments");
      }
      StackRange range = label_input_stack.PushMany(LowerType(type));
      parameters.push_back(VisitResult(type, range));
    }
    label_block = assembler().NewBlock(label_input_stack,
                                       IsDeferred(expr->label_block->body));

    Binding<LocalLabel> label_binding{&LabelBindingsManager::Get(),
                                      expr->label_block->label,
                                      LocalLabel{label_block, parameter_types}};

    // Visit try
    StackScope stack_scope(this);
    try_result = Visit(expr->try_expression);
    if (try_result.type() != TypeOracle::GetNeverType()) {
      try_result = stack_scope.Yield(try_result);
      assembler().Goto(done_block);
    }
  }

  // Visit and output the code for the label block. If the label block falls
  // through, then the try must not return a value. Also, if the try doesn't
  // fall through, but the label does, then overall the try-label block
  // returns type void.
  assembler().Bind(label_block);
  const Type* label_result;
  {
    BlockBindings<LocalValue> parameter_bindings(&ValueBindingsManager::Get());
    for (size_t i = 0; i < parameter_count; ++i) {
      Identifier* name = expr->label_block->parameters.names[i];
      parameter_bindings.Add(name,
                             LocalValue{LocationReference::Temporary(
                                 parameters[i], "parameter " + name->value)});
    }

    label_result = Visit(expr->label_block->body);
  }
  if (!try_result.type()->IsVoidOrNever() && label_result->IsVoid()) {
    ReportError(
        "otherwise clauses cannot fall through in a non-void expression");
  }
  if (label_result != TypeOracle::GetNeverType()) {
    assembler().Goto(done_block);
  }
  if (label_result->IsVoid() && try_result.type()->IsNever()) {
    try_result =
        VisitResult(TypeOracle::GetVoidType(), try_result.stack_range());
  }

  if (!try_result.type()->IsNever()) {
    assembler().Bind(done_block);
  }
  return try_result;
}

VisitResult ImplementationVisitor::Visit(StatementExpression* expr) {
  return VisitResult{Visit(expr->statement), assembler().TopRange(0)};
}

InitializerResults ImplementationVisitor::VisitInitializerResults(
    const ClassType* class_type,
    const std::vector<NameAndExpression>& initializers) {
  InitializerResults result;
  for (const NameAndExpression& initializer : initializers) {
    result.names.push_back(initializer.name);
    Expression* e = initializer.expression;
    const Field& field = class_type->LookupField(initializer.name->value);
    bool has_index = field.index.has_value();
    if (SpreadExpression* s = SpreadExpression::DynamicCast(e)) {
      if (!has_index) {
        ReportError(
            "spread expressions can only be used to initialize indexed class "
            "fields ('",
            initializer.name->value, "' is not)");
      }
      e = s->spreadee;
    } else if (has_index) {
      ReportError("the indexed class field '", initializer.name->value,
                  "' must be initialized with a spread operator");
    }
    result.field_value_map[field.name_and_type.name] = Visit(e);
  }
  return result;
}

LocationReference ImplementationVisitor::GenerateFieldReference(
    VisitResult object, const Field& field, const ClassType* class_type,
    bool treat_optional_as_indexed) {
  if (field.index.has_value()) {
    LocationReference slice = LocationReference::HeapSlice(
        GenerateCall(class_type->GetSliceMacroName(field), {{object}, {}}));
    if (field.index->optional && !treat_optional_as_indexed) {
      // This field was declared using optional syntax, so any reference to it
      // is implicitly a reference to the first item.
      return GenerateReferenceToItemInHeapSlice(
          slice, {TypeOracle::GetConstInt31Type(), "0"});
    } else {
      return slice;
    }
  }
  DCHECK(field.offset.has_value());
  StackRange result_range = assembler().TopRange(0);
  result_range.Extend(GenerateCopy(object).stack_range());
  VisitResult offset =
      VisitResult(TypeOracle::GetConstInt31Type(), ToString(*field.offset));
  offset = GenerateImplicitConvert(TypeOracle::GetIntPtrType(), offset);
  result_range.Extend(offset.stack_range());
  const Type* type = TypeOracle::GetReferenceType(field.name_and_type.type,
                                                  field.const_qualified);
  return LocationReference::HeapReference(VisitResult(type, result_range));
}

// This is used to generate field references during initialization, where we can
// re-use the offsets used for computing the allocation size.
LocationReference ImplementationVisitor::GenerateFieldReferenceForInit(
    VisitResult object, const Field& field,
    const LayoutForInitialization& layout) {
  StackRange result_range = assembler().TopRange(0);
  result_range.Extend(GenerateCopy(object).stack_range());
  VisitResult offset = GenerateImplicitConvert(
      TypeOracle::GetIntPtrType(), layout.offsets.at(field.name_and_type.name));
  result_range.Extend(offset.stack_range());
  if (field.index) {
    VisitResult length =
        GenerateCopy(layout.array_lengths.at(field.name_and_type.name));
    result_range.Extend(length.stack_range());
    const Type* slice_type =
        TypeOracle::GetMutableSliceType(field.name_and_type.type);
    return LocationReference::HeapSlice(VisitResult(slice_type, result_range));
  } else {
    // Const fields are writable during initialization.
    VisitResult heap_reference(
        TypeOracle::GetMutableReferenceType(field.name_and_type.type),
        result_range);
    return LocationReference::HeapReference(heap_reference);
  }
}

void ImplementationVisitor::InitializeClass(
    const ClassType* class_type, VisitResult allocate_result,
    const InitializerResults& initializer_results,
    const LayoutForInitialization& layout) {
  if (const ClassType* super = class_type->GetSuperClass()) {
    InitializeClass(super, allocate_result, initializer_results, layout);
  }

  for (const Field& f : class_type->fields()) {
    // Support optional padding fields.
    if (f.name_and_type.type->IsVoid()) continue;
    VisitResult initializer_value =
        initializer_results.field_value_map.at(f.name_and_type.name);
    LocationReference field =
        GenerateFieldReferenceForInit(allocate_result, f, layout);
    if (f.index) {
      DCHECK(field.IsHeapSlice());
      VisitResult slice = field.GetVisitResult();
      GenerateCall(QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                                 "InitializeFieldsFromIterator"),
                   {{slice, initializer_value}, {}});
    } else {
      GenerateAssignToLocation(field, initializer_value);
    }
  }
}

VisitResult ImplementationVisitor::GenerateArrayLength(
    Expression* array_length, Namespace* nspace,
    const std::map<std::string, LocalValue>& bindings) {
  StackScope stack_scope(this);
  CurrentSourcePosition::Scope pos_scope(array_length->pos);
  // Switch to the namespace where the class was declared.
  CurrentScope::Scope current_scope_scope(nspace);
  // Reset local bindings and install local binding for the preceding fields.
  BindingsManagersScope bindings_managers_scope;
  BlockBindings<LocalValue> field_bindings(&ValueBindingsManager::Get());
  for (auto& p : bindings) {
    field_bindings.Add(p.first, LocalValue{p.second}, true);
  }
  VisitResult length = Visit(array_length);
  VisitResult converted_length =
      GenerateCall("Convert", Arguments{{length}, {}},
                   {TypeOracle::GetIntPtrType(), length.type()}, false);
  return stack_scope.Yield(converted_length);
}

VisitResult ImplementationVisitor::GenerateArrayLength(VisitResult object,
                                                       const Field& field) {
  DCHECK(field.index);

  StackScope stack_scope(this);
  const ClassType* class_type = *object.type()->ClassSupertype();
  std::map<std::string, LocalValue> bindings;
  bool before_current = true;
  for (const Field& f : class_type->ComputeAllFields()) {
    if (field.name_and_type.name == f.name_and_type.name) {
      before_current = false;
    }
    // We can't generate field references eagerly here, because some preceding
    // fields might be optional, and attempting to get a reference to an
    // optional field can crash the program if the field isn't present.
    // Instead, we use the lazy form of LocalValue to only generate field
    // references if they are used in the length expression.
    bindings.insert(
        {f.name_and_type.name,
         f.const_qualified
             ? (before_current
                    ? LocalValue{[this, object, f, class_type]() {
                        return GenerateFieldReference(object, f, class_type);
                      }}
                    : LocalValue("Array lengths may only refer to fields "
                                 "defined earlier"))
             : LocalValue(
                   "Non-const fields cannot be used for array lengths.")});
  }
  return stack_scope.Yield(
      GenerateArrayLength(field.index->expr, class_type->nspace(), bindings));
}

VisitResult ImplementationVisitor::GenerateArrayLength(
    const ClassType* class_type, const InitializerResults& initializer_results,
    const Field& field) {
  DCHECK(field.index);

  StackScope stack_scope(this);
  std::map<std::string, LocalValue> bindings;
  for (const Field& f : class_type->ComputeAllFields()) {
    if (f.index) break;
    const std::string& fieldname = f.name_and_type.name;
    VisitResult value = initializer_results.field_value_map.at(fieldname);
    bindings.insert(
        {fieldname,
         f.const_qualified
             ? LocalValue{LocationReference::Temporary(
                   value, "initial field " + fieldname)}
             : LocalValue(
                   "Non-const fields cannot be used for array lengths.")});
  }
  return stack_scope.Yield(
      GenerateArrayLength(field.index->expr, class_type->nspace(), bindings));
}

LayoutForInitialization ImplementationVisitor::GenerateLayoutForInitialization(
    const ClassType* class_type,
    const InitializerResults& initializer_results) {
  LayoutForInitialization layout;
  VisitResult offset;
  for (Field f : class_type->ComputeAllFields()) {
    if (f.offset.has_value()) {
      offset =
          VisitResult(TypeOracle::GetConstInt31Type(), ToString(*f.offset));
    }
    layout.offsets[f.name_and_type.name] = offset;
    if (f.index) {
      size_t element_size;
      std::string element_size_string;
      std::tie(element_size, element_size_string) =
          *SizeOf(f.name_and_type.type);
      VisitResult array_element_size =
          VisitResult(TypeOracle::GetConstInt31Type(), element_size_string);
      VisitResult array_length =
          GenerateArrayLength(class_type, initializer_results, f);
      layout.array_lengths[f.name_and_type.name] = array_length;
      Arguments arguments;
      arguments.parameters = {offset, array_length, array_element_size};
      offset = GenerateCall(QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                                          "AddIndexedFieldSizeToObjectSize"),
                            arguments);
    } else {
      DCHECK(f.offset.has_value());
    }
  }
  if (class_type->size().SingleValue()) {
    layout.size = VisitResult(TypeOracle::GetConstInt31Type(),
                              ToString(*class_type->size().SingleValue()));
  } else {
    layout.size = offset;
  }
  if ((size_t{1} << class_type->size().AlignmentLog2()) <
      TargetArchitecture::TaggedSize()) {
    Arguments arguments;
    arguments.parameters = {layout.size};
    layout.size = GenerateCall(
        QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING}, "AlignTagged"),
        arguments);
  }
  return layout;
}

VisitResult ImplementationVisitor::Visit(NewExpression* expr) {
  StackScope stack_scope(this);
  const Type* type = TypeVisitor::ComputeType(expr->type);
  const ClassType* class_type = ClassType::DynamicCast(type);
  if (class_type == nullptr) {
    ReportError("type for new expression must be a class, \"", *type,
                "\" is not");
  }

  if (!class_type->AllowInstantiation()) {
    // Classes that are only used for testing should never be instantiated.
    ReportError(*class_type,
                " cannot be allocated with new (it's used for testing)");
  }

  InitializerResults initializer_results =
      VisitInitializerResults(class_type, expr->initializers);

  const Field& map_field = class_type->LookupField("map");
  if (*map_field.offset != 0) {
    ReportError("class initializers must have a map as first parameter");
  }
  const std::map<std::string, VisitResult>& initializer_fields =
      initializer_results.field_value_map;
  auto it_object_map = initializer_fields.find(map_field.name_and_type.name);
  VisitResult object_map;
  if (class_type->IsExtern()) {
    if (it_object_map == initializer_fields.end()) {
      ReportError("Constructor for ", class_type->name(),
                  " needs Map argument!");
    }
    object_map = it_object_map->second;
  } else {
    if (it_object_map != initializer_fields.end()) {
      ReportError(
          "Constructor for ", class_type->name(),
          " must not specify Map argument; it is automatically inserted.");
    }
    Arguments get_struct_map_arguments;
    get_struct_map_arguments.parameters.push_back(
        VisitResult(TypeOracle::GetConstexprInstanceTypeType(),
                    CapifyStringWithUnderscores(class_type->name()) + "_TYPE"));
    object_map = GenerateCall(
        QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING}, "GetInstanceTypeMap"),
        get_struct_map_arguments, {}, false);
    CurrentSourcePosition::Scope current_pos(expr->pos);
    initializer_results.names.insert(initializer_results.names.begin(),
                                     MakeNode<Identifier>("map"));
    initializer_results.field_value_map[map_field.name_and_type.name] =
        object_map;
  }

  CheckInitializersWellformed(class_type->name(),
                              class_type->ComputeAllFields(),
                              expr->initializers, !class_type->IsExtern());

  LayoutForInitialization layout =
      GenerateLayoutForInitialization(class_type, initializer_results);

  Arguments allocate_arguments;
  allocate_arguments.parameters.push_back(layout.size);
  allocate_arguments.parameters.push_back(object_map);
  allocate_arguments.parameters.push_back(
      GenerateBoolConstant(expr->pretenured));
  allocate_arguments.parameters.push_back(
      GenerateBoolConstant(expr->clear_padding));
  VisitResult allocate_result = GenerateCall(
      QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING}, "AllocateFromNew"),
      allocate_arguments, {class_type}, false);
  DCHECK(allocate_result.IsOnStack());

  InitializeClass(class_type, allocate_result, initializer_results, layout);

  return stack_scope.Yield(GenerateCall(
      "%RawDownCast", Arguments{{allocate_result}, {}}, {class_type}));
}

const Type* ImplementationVisitor::Visit(BreakStatement* stmt) {
  std::optional<Binding<LocalLabel>*> break_label =
      TryLookupLabel(kBreakLabelName);
  if (!break_label) {
    ReportError("break used outside of loop");
  }
  assembler().Goto((*break_label)->block);
  return TypeOracle::GetNeverType();
}

const Type* ImplementationVisitor::Visit(ContinueStatement* stmt) {
  std::optional<Binding<LocalLabel>*> continue_label =
      TryLookupLabel(kContinueLabelName);
  if (!continue_label) {
    ReportError("continue used outside of loop");
  }
  assembler().Goto((*continue_label)->block);
  return TypeOracle::GetNeverType();
}

const Type* ImplementationVisitor::Visit(ForLoopStatement* stmt) {
  BlockBindings<LocalValue> loop_bindings(&ValueBindingsManager::Get());

  if (stmt->var_declaration) Visit(*stmt->var_declaration, &loop_bindings);

  Block* body_block = assembler().NewBlock(assembler().CurrentStack());
  Block* exit_block = assembler().NewBlock(assembler().CurrentStack());

  Block* header_block = assembler().NewBlock();
  assembler().Goto(header_block);
  assembler().Bind(header_block);

  // The continue label is where "continue" statements jump to. If no action
  // expression is provided, we jump directly to the header.
  Block* continue_block = header_block;

  // The action label is only needed when an action expression was provided.
  Block* action_block = nullptr;
  if (stmt->action) {
    action_block = assembler().NewBlock();

    // The action expression needs to be executed on a continue.
    continue_block = action_block;
  }

  if (stmt->test) {
    GenerateExpressionBranch(*stmt->test, body_block, exit_block);
  } else {
    assembler().Goto(body_block);
  }

  assembler().Bind(body_block);
  {
    BreakContinueActivator activator(exit_block, continue_block);
    const Type* body_result = Visit(stmt->body);
    if (body_result != TypeOracle::GetNeverType()) {
      assembler().Goto(continue_block);
    }
  }

  if (stmt->action) {
    assembler().Bind(action_block);
    const Type* action_result = Visit(*stmt->action);
    if (action_result != TypeOracle::GetNeverType()) {
      assembler().Goto(header_block);
    }
  }

  assembler().Bind(exit_block);
  return TypeOracle::GetVoidType();
}

VisitResult ImplementationVisitor::Visit(SpreadExpression* expr) {
  ReportError(
      "spread operators are only currently supported in indexed class field "
      "initialization expressions");
}

void ImplementationVisitor::GenerateImplementation(const std::string& dir) {
  for (SourceId file : SourceFileMap::AllSources()) {
    std::string base_filename =
        dir + "/" + SourceFileMap::PathFromV8RootWithoutExtension(file);
    GlobalContext::PerFileStreams& streams =
        GlobalContext::GeneratedPerFile(file);

    std::string csa_cc = streams.csa_ccfile.str();
    // Insert missing builtin includes where the marker is.
    {
      auto pos = csa_cc.find(BuiltinIncludesMarker);
      CHECK_NE(pos, std::string::npos);
      std::string includes;
      for (const SourceId& include : streams.required_builtin_includes) {
        std::string include_file =
            SourceFileMap::PathFromV8RootWithoutExtension(include);
        includes += "#include \"torque-generated/";
        includes += include_file;
        includes += "-tq-csa.h\"\n";
      }
      csa_cc.replace(pos, strlen(BuiltinIncludesMarker), std::move(includes));
    }

    // TODO(torque-builder): Pass file directly.
    WriteFile(base_filename + "-tq-csa.cc", std::move(csa_cc));
    WriteFile(base_filename + "-tq-csa.h", streams.csa_headerfile.str());
    WriteFile(base_filename + "-tq.inc",
              streams.class_definition_headerfile.str());
    WriteFile(
        base_filename + "-tq-inl.inc",
        streams.class_definition_inline_headerfile_macro_declarations.str() +
            streams.class_definition_inline_headerfile_macro_definitions.str() +
            streams.class_definition_inline_headerfile.str());
    WriteFile(base_filename + "-tq.cc", streams.class_definition_ccfile.str());
  }

  WriteFile(dir + "/debug-macros.h", debug_macros_h_.str());
  WriteFile(dir + "/debug-macros.cc", debug_macros_cc_.str());
}

cpp::Function ImplementationVisitor::GenerateMacroFunctionDeclaration(
    Macro* macro) {
  return GenerateFunction(nullptr,
                          output_type_ == OutputType::kCC
                              ? macro->CCName()
                              : output_type_ == OutputType::kCCDebug
                                    ? macro->CCDebugName()
                                    : macro->ExternalName(),
                          macro->signature(), macro->parameter_names());
}

cpp::Function ImplementationVisitor::GenerateFunction(
    cpp::Class* owner, const std::string& name, const Signature& signature,
    const NameVector& parameter_names, bool pass_code_assembler_state,
    std::vector<std::string>* generated_parameter_names) {
  cpp::Function f(owner, name);
  f.SetInline(output_type_ == OutputType::kCC);

  // Set return type.
  // TODO(torque-builder): Consider an overload of SetReturnType that handles
  // this.
  if (signature.return_type->IsVoidOrNever()) {
    f.SetReturnType("void");
  } else if (output_type_ == OutputType::kCCDebug) {
    f.SetReturnType(std::string("Value<") +
                    signature.return_type->GetDebugType() + ">");
  } else if (output_type_ == OutputType::kCC) {
    f.SetReturnType(signature.return_type->GetRuntimeType());
  } else {
    DCHECK_EQ(output_type_, OutputType::kCSA);
    f.SetReturnType(signature.return_type->IsConstexpr()
                        ? signature.return_type->TagglifiedCppTypeName()
                        : signature.return_type->GetGeneratedTypeName());
  }

  bool ignore_first_parameter = true;
  if (output_type_ == OutputType::kCCDebug) {
    f.AddParameter("d::MemoryAccessor", "accessor");
  } else if (output_type_ == OutputType::kCSA && pass_code_assembler_state) {
    f.AddParameter("compiler::CodeAssemblerState*", "state_");
  } else {
    ignore_first_parameter = false;
  }

  // TODO(torque-builder): Consider an overload for AddParameter that handles
  // this.
  DCHECK_GE(signature.types().size(), parameter_names.size());
  for (std::size_t i = 0; i < signature.types().size(); ++i) {
    const Type* parameter_type = signature.types()[i];
    std::string type;
    if (output_type_ == OutputType::kCC) {
      type = parameter_type->GetRuntimeType();
    } else if (output_type_ == OutputType::kCCDebug) {
      type = parameter_type->GetDebugType();
    } else {
      DCHECK_EQ(output_type_, OutputType::kCSA);
      if (parameter_type->IsConstexpr()) {
        type = parameter_type->TagglifiedCppTypeName();
      } else {
        type = parameter_type->GetGeneratedTypeName();
      }
    }
    f.AddParameter(std::move(type),
                   ExternalParameterName(i < parameter_names.size()
                                             ? parameter_names[i]->value
                                             : std::to_string(i)));
  }

  for (const LabelDeclaration& label_info : signature.labels) {
    if (output_type_ == OutputType::kCC ||
        output_type_ == OutputType::kCCDebug) {
      ReportError("Macros that generate runtime code can't have label exits");
    }
    f.AddParameter("compiler::CodeAssemblerLabel*",
                   ExternalLabelName(label_info.name->value));
    size_t i = 0;
    for (const Type* type : label_info.types) {
      std::string generated_type_name;
      if (type->StructSupertype()) {
        generated_type_name = "\n#error no structs allowed in labels\n";
      } else {
        generated_type_name = "compiler::TypedCodeAssemblerVariable<";
        generated_type_name += type->GetGeneratedTNodeTypeName();
        generated_type_name += ">*";
      }
      f.AddParameter(generated_type_name,
                     ExternalLabelParameterName(label_info.name->value, i));
      ++i;
    }
  }

  if (generated_parameter_names) {
    *generated_parameter_names = f.GetParameterNames();
    if (ignore_first_parameter) {
      DCHECK(!generated_parameter_names->empty());
      generated_parameter_names->erase(generated_parameter_names->begin());
    }
  }
  return f;
}

namespace {

void FailCallableLookup(
    const std::string& reason, const QualifiedName& name,
    const TypeVector& parameter_types,
    const std::vector<Binding<LocalLabel>*>& labels,
    const std::vector<Signature>& candidates,
    const std::vector<std::pair<GenericCallable*, std::string>>
        inapplicable_generics) {
  std::stringstream stream;
  stream << "\n" << reason << ": \n  " << name << "(" << parameter_types << ")";
  if (!labels.empty()) {
    stream << " labels ";
    for (size_t i = 0; i < labels.size(); ++i) {
      stream << labels[i]->name() << "(" << labels[i]->parameter_types << ")";
    }
  }
  stream << "\ncandidates are:";
  for (const Signature& signature : candidates) {
    stream << "\n  " << name;
    PrintSignature(stream, signature, false);
  }
  if (!inapplicable_generics.empty()) {
    stream << "\nfailed to instantiate all of these generic declarations:";
    for (auto& failure : inapplicable_generics) {
      GenericCallable* generic = failure.first;
      const std::string& fail_reason = failure.second;
      stream << "\n  " << generic->name() << " defined at "
             << PositionAsString(generic->Position()) << ":\n    "
             << fail_reason << "\n";
    }
  }
  ReportError(stream.str());
}

Callable* GetOrCreateSpecialization(
    const SpecializationKey<GenericCallable>& key) {
  if (std::optional<Callable*> specialization =
          key.generic->GetSpecialization(key.specialized_types)) {
    return *specialization;
  }
  return DeclarationVisitor::SpecializeImplicit(key);
}

}  // namespace

std::optional<Binding<LocalValue>*> ImplementationVisitor::TryLookupLocalValue(
    const std::string& name) {
  return ValueBindingsManager::Get().TryLookup(name);
}

std::optional<Binding<LocalLabel>*> ImplementationVisitor::TryLookupLabel(
    const std::string& name) {
  return LabelBindingsManager::Get().TryLookup(name);
}

Binding<LocalLabel>* ImplementationVisitor::LookupLabel(
    const std::string& name) {
  std::optional<Binding<LocalLabel>*> label = TryLookupLabel(name);
  if (!label) ReportError("cannot find label ", name);
  return *label;
}

Block* ImplementationVisitor::LookupSimpleLabel(const std::string& name) {
  LocalLabel* label = LookupLabel(name);
  if (!label->parameter_types.empty()) {
    ReportError("label ", name,
                "was expected to have no parameters, but has parameters (",
                label->parameter_types, ")");
  }
  return label->block;
}

// Try to lookup a callable with the provided argument types. Do not report
// an error if no matching callable was found, but return false instead.
// This is used to test the presence of overloaded field accessors.
bool ImplementationVisitor::TestLookupCallable(
    const QualifiedName& name, const TypeVector& parameter_types) {
  return LookupCallable(name, Declarations::TryLookup(name), parameter_types,
                        {}, {}, true) != nullptr;
}

TypeArgumentInference ImplementationVisitor::InferSpecializationTypes(
    GenericCallable* generic, const TypeVector& explicit_specialization_types,
    const TypeVector& explicit_arguments) {
  std::vector<std::optional<const Type*>> all_arguments;
  const ParameterList& parameters = generic->declaration()->parameters;
  for (size_t i = 0; i < parameters.implicit_count; ++i) {
    std::optional<Binding<LocalValue>*> val =
        TryLookupLocalValue(parameters.names[i]->value);
    all_arguments.push_back(
        val ? (*val)->GetLocationReference(*val).ReferencedType()
            : std::nullopt);
  }
  for (const Type* explicit_argument : explicit_arguments) {
    all_arguments.push_back(explicit_argument);
  }
  return generic->InferSpecializationTypes(explicit_specialization_types,
                                           all_arguments);
}

template <class Container>
Callable* ImplementationVisitor::LookupCallable(
    const QualifiedName& name, const Container& declaration_container,
    const TypeVector& parameter_types,
    const std::vector<Binding<LocalLabel>*>& labels,
    const TypeVector& specialization_types, bool silence_errors) {
  Callable* result = nullptr;

  std::vector<Declarable*> overloads;
  std::vector<Signature> overload_signatures;
  std::vector<std::pair<GenericCallable*, std::string>> inapplicable_generics;
  for (auto* declarable : declaration_container) {
    if (GenericCallable* generic = GenericCallable::DynamicCast(declarable)) {
      TypeArgumentInference inference = InferSpecializationTypes(
          generic, specialization_types, parameter_types);
      if (inference.HasFailed()) {
        inapplicable_generics.push_back(
            std::make_pair(generic, inference.GetFailureReason()));
        continue;
      }
      overloads.push_back(generic);
      overload_signatures.push_back(
          DeclarationVisitor::MakeSpecializedSignature(
              SpecializationKey<GenericCallable>{generic,
                                                 inference.GetResult()}));
    } else if (Callable* callable = Callable::DynamicCast(declarable)) {
      overloads.push_back(callable);
      overload_signatures.push_back(callable->signature());
    }
  }
  // Indices of candidates in overloads/overload_signatures.
  std::vector<size_t> candidates;
  for (size_t i = 0; i < overloads.size(); ++i) {
    const Signature& signature = overload_signatures[i];
    if (IsCompatibleSignature(signature, parameter_types, labels.size())) {
      candidates.push_back(i);
    }
  }

  if (overloads.empty() && inapplicable_generics.empty()) {
    if (silence_errors) return nullptr;
    std::stringstream stream;
    stream << "no matching declaration found for " << name;
    ReportError(stream.str());
  } else if (candidates.empty()) {
    if (silence_errors) return nullptr;
    FailCallableLookup("cannot find suitable callable with name", name,
                       parameter_types, labels, overload_signatures,
                       inapplicable_generics);
  }

  auto is_better_candidate = [&](size_t a, size_t b) {
    return ParameterDifference(overload_signatures[a].GetExplicitTypes(),
                               parameter_types)
        .StrictlyBetterThan(ParameterDifference(
            overload_signatures[b].GetExplicitTypes(), parameter_types));
  };

  size_t best = *std::min_element(candidates.begin(), candidates.end(),
                                  is_better_candidate);
  // This check is contained in libstdc++'s std::min_element.
  DCHECK(!is_better_candidate(best, best));
  for (size_t candidate : candidates) {
    if (candidate != best && !is_better_candidate(best, candidate)) {
      std::vector<Signature> candidate_signatures;
      candidate_signatures.reserve(candidates.size());
      for (size_t i : candidates) {
        candidate_signatures.push_back(overload_signatures[i]);
      }
      FailCallableLookup("ambiguous callable ", name, parameter_types, labels,
                         candidate_signatures, inapplicable_generics);
    }
  }

  if (GenericCallable* generic =
          GenericCallable::DynamicCast(overloads[best])) {
    TypeArgumentInference inference = InferSpecializationTypes(
        generic, specialization_types, parameter_types);
    result = GetOrCreateSpecialization(
        SpecializationKey<GenericCallable>{generic, inference.GetResult()});
  } else {
    result = Callable::cast(overloads[best]);
  }

  size_t caller_size = parameter_types.size();
  size_t callee_size =
      result->signature().types().size() - result->signature().implicit_count;
  if (caller_size != callee_size &&
      !result->signature().parameter_types.var_args) {
    std::stringstream stream;
    stream << "parameter count mismatch calling " << *result << " - expected "
           << std::to_string(callee_size) << ", found "
           << std::to_string(caller_size);
    ReportError(stream.str());
  }

  return result;
}

template <class Container>
Callable* ImplementationVisitor::LookupCallable(
    const QualifiedName& name, const Container& declaration_container,
    const Arguments& arguments, const TypeVector& specialization_types) {
  return LookupCallable(name, declaration_container,
                        arguments.parameters.ComputeTypeVector(),
                        arguments.labels, specialization_types);
}

Method* ImplementationVisitor::LookupMethod(
    const std::string& name, const AggregateType* receiver_type,
    const Arguments& arguments, const TypeVector& specialization_types) {
  TypeVector types(arguments.parameters.ComputeTypeVector());
  types.insert(types.begin(), receiver_type);
  return Method::cast(LookupCallable({{}, name}, receiver_type->Methods(name),
                                     types, arguments.labels,
                                     specialization_types));
}

const Type* ImplementationVisitor::GetCommonType(const Type* left,
                                                 const Type* right) {
  const Type* common_type;
  if (IsAssignableFrom(left, right)) {
    common_type = left;
  } else if (IsAssignableFrom(right, left)) {
    common_type = right;
  } else {
    common_type = TypeOracle::GetUnionType(left, right);
  }
  common_type = common_type->NonConstexprVersion();
  return common_type;
}

VisitResult ImplementationVisitor::GenerateCopy(const VisitResult& to_copy) {
  if (to_copy.IsOnStack()) {
    return VisitResult(to_copy.type(),
                       assembler().Peek(to_copy.stack_range(), to_copy.type()));
  }
  return to_copy;
}

VisitResult ImplementationVisitor::Visit(StructExpression* expr) {
  StackScope stack_scope(this);

  auto& initializers = expr->initializers;
  std::vector<VisitResult> values;
  std::vector<const Type*> term_argument_types;
  values.reserve(initializers.size());
  term_argument_types.reserve(initializers.size());

  // Compute values and types of all initializer arguments
  for (const NameAndExpression& initializer : initializers) {
    VisitResult value = Visit(initializer.expression);
    values.push_back(value);
    term_argument_types.push_back(value.type());
  }

  // Compute and check struct type from given struct name and argument types
  const Type* type = TypeVisitor::ComputeTypeForStructExpression(
      expr->type, term_argument_types);
  if (const auto* struct_type = StructType::DynamicCast(type)) {
    CheckInitializersWellformed(struct_type->name(), struct_type->fields(),
                                initializers);

    // Implicitly convert values and thereby build the struct on the stack
    StackRange struct_range = assembler().TopRange(0);
    auto& fields = struct_type->fields();
    for (size_t i = 0; i < values.size(); i++) {
      values[i] =
          GenerateImplicitConvert(fields[i].name_and_type.type, values[i]);
      struct_range.Extend(values[i].stack_range());
    }

    return stack_scope.Yield(VisitResult(struct_type, struct_range));
  } else {
    const auto* bitfield_struct_type = BitFieldStructType::cast(type);
    CheckInitializersWellformed(bitfield_struct_type->name(),
                                bitfield_struct_type->fields(), initializers);

    // Create a zero and cast it to the desired bitfield struct type.
    VisitResult result{TypeOracle::GetConstInt32Type(), "0"};
    result = GenerateImplicitConvert(TypeOracle::GetInt32Type(), result);
    result = GenerateCall("Unsigned", Arguments{{result}, {}}, {});
    result = GenerateCall("%RawDownCast", Arguments{{result}, {}},
                          {bitfield_struct_type});

    // Set each field in the result. If these fields are constexpr, then all of
    // this initialization will end up reduced to a single value during TurboFan
    // optimization.
    auto& fields = bitfield_struct_type->fields();
    for (size_t i = 0; i < values.size(); i++) {
      values[i] =
          GenerateImplicitConvert(fields[i].name_and_type.type, values[i]);
      result = GenerateSetBitField(bitfield_struct_type, fields[i], result,
                                   values[i], /*starts_as_zero=*/true);
    }

    return stack_scope.Yield(result);
  }
}

VisitResult ImplementationVisitor::GenerateSetBitField(
    const Type* bitfield_struct_type, const BitField& bitfield,
    VisitResult bitfield_struct, VisitResult value, bool starts_as_zero) {
  GenerateCopy(bitfield_struct);
  GenerateCopy(value);
  assembler().Emit(
      StoreBitFieldInstruction{bitfield_struct_type, bitfield, starts_as_zero});
  return VisitResult(bitfield_struct_type, assembler().TopRange(1));
}

LocationReference ImplementationVisitor::GetLocationReference(
    Expression* location) {
  switch (location->kind) {
    case AstNode::Kind::kIdentifierExpression:
      return GetLocationReference(static_cast<IdentifierExpression*>(location));
    case AstNode::Kind::kFieldAccessExpression:
      return GetLocationReference(
          static_cast<FieldAccessExpression*>(location));
    case AstNode::Kind::kElementAccessExpression:
      return GetLocationReference(
          static_cast<ElementAccessExpression*>(location));
    case AstNode::Kind::kDereferenceExpression:
      return GetLocationReference(
          static_cast<DereferenceExpression*>(location));
    default:
      return LocationReference::Temporary(Visit(location), "expression");
  }
}

LocationReference ImplementationVisitor::GetLocationReference(
    FieldAccessExpression* expr) {
  return GenerateFieldAccess(GetLocationReference(expr->object),
                             expr->field->value, false, expr->field->pos);
}

LocationReference ImplementationVisitor::GenerateFieldAccess(
    LocationReference reference, const std::string& fieldname,
    bool ignore_stuct_field_constness, std::optional<SourcePosition> pos) {
  if (reference.IsVariableAccess() &&
      reference.variable().type()->StructSupertype()) {
    const StructType* type = *reference.variable().type()->StructSupertype();
    const Field& field = type->LookupField(fieldname);
    if (GlobalContext::collect_language_server_data() && pos.has_value()) {
      LanguageServerData::AddDefinition(*pos, field.pos);
    }
    if (GlobalContext::collect_kythe_data() && pos.has_value()) {
      KytheData::AddClassFieldUse(*pos, &field);
    }
    if (field.const_qualified) {
      VisitResult t_value = ProjectStructField(reference.variable(), fieldname);
      return LocationReference::Temporary(
          t_value, "for constant field '" + field.name_and_type.name + "'");
    } else {
      return LocationReference::VariableAccess(
          ProjectStructField(reference.variable(), fieldname));
    }
  }
  if (reference.IsTemporary() &&
      reference.temporary().type()->StructSupertype()) {
    if (GlobalContext::collect_language_server_data() && pos.has_value()) {
      const StructType* type = *reference.temporary().type()->StructSupertype();
      const Field& field = type->LookupField(fieldname);
      LanguageServerData::AddDefinition(*pos, field.pos);
    }
    return LocationReference::Temporary(
        ProjectStructField(reference.temporary(), fieldname),
        reference.temporary_description());
  }
  if (std::optional<const Type*> referenced_type = reference.ReferencedType()) {
    if ((*referenced_type)->IsBitFieldStructType()) {
      const BitFieldStructType* bitfield_struct =
          BitFieldStructType::cast(*referenced_type);
      const BitField& field = bitfield_struct->LookupField(fieldname);
      return LocationReference::BitFieldAccess(reference, field);
    }
    if (const auto type_wrapped_in_smi = Type::MatchUnaryGeneric(
            (*referenced_type), TypeOracle::GetSmiTaggedGeneric())) {
      const BitFieldStructType* bitfield_struct =
          BitFieldStructType::DynamicCast(*type_wrapped_in_smi);
      if (bitfield_struct == nullptr) {
        ReportError(
            "When a value of type SmiTagged<T> is used in a field access "
            "expression, T is expected to be a bitfield struct type. Instead, "
            "T "
            "is ",
            **type_wrapped_in_smi);
      }
      const BitField& field = bitfield_struct->LookupField(fieldname);
      return LocationReference::BitFieldAccess(reference, field);
    }
  }
  if (reference.IsHeapReference()) {
    VisitResult ref = reference.heap_reference();
    bool is_const;
    auto generic_type =
        TypeOracle::MatchReferenceGeneric(ref.type(), &is_const);
    if (!generic_type) {
      ReportError(
          "Left-hand side of field access expression is marked as a reference "
          "but is not of type Reference<...>. Found type: ",
          ref.type()->ToString());
    }
    if (auto struct_type = (*generic_type)->StructSupertype()) {
      const Field& field = (*struct_type)->LookupField(fieldname);
      // Update the Reference's type to refer to the field type within the
      // struct.
      ref.SetType(TypeOracle::GetReferenceType(
          field.name_and_type.type,
          is_const ||
              (field.const_qualified && !ignore_stuct_field_constness)));
      if (!field.offset.has_value()) {
        Error("accessing field with unknown offset").Throw();
      }
      if (*field.offset != 0) {
        // Copy the Reference struct up the stack and update the new copy's
        // |offset| value to point to the struct field.
        StackScope scope(this);
        ref = GenerateCopy(ref);
        VisitResult ref_offset = ProjectStructField(ref, "offset");
        VisitResult struct_offset{
            TypeOracle::GetIntPtrType()->ConstexprVersion(),
            std::to_string(*field.offset)};
        VisitResult updated_offset =
            GenerateCall("+", Arguments{{ref_offset, struct_offset}, {}});
        assembler().Poke(ref_offset.stack_range(), updated_offset.stack_range(),
                         ref_offset.type());
        ref = scope.Yield(ref);
      }
      return LocationReference::HeapReference(ref);
    }
  }
  VisitResult object_result = GenerateFetchFromLocation(reference);
  if (std::optional<const ClassType*> class_type =
          object_result.type()->ClassSupertype()) {
    // This is a hack to distinguish the situation where we want to use
    // overloaded field accessors from when we want to create a reference.
    bool has_explicit_overloads = TestLookupCallable(
        QualifiedName{"." + fieldname}, {object_result.type()});
    if ((*class_type)->HasField(fieldname) && !has_explicit_overloads) {
      const Field& field = (*class_type)->LookupField(fieldname);
      if (GlobalContext::collect_language_server_data() && pos.has_value()) {
        LanguageServerData::AddDefinition(*pos, field.pos);
      }
      if (GlobalContext::collect_kythe_data()) {
        KytheData::AddClassFieldUse(*pos, &field);
      }
      return GenerateFieldReference(object_result, field, *class_type);
    }
  }
  return LocationReference::FieldAccess(object_result, fieldname);
}

LocationReference ImplementationVisitor::GetLocationReference(
    ElementAccessExpression* expr) {
  LocationReference reference = GetLocationReference(expr->array);
  VisitResult index = Visit(expr->index);
  if (reference.IsHeapSlice()) {
    return GenerateReferenceToItemInHeapSlice(reference, index);
  } else {
    return LocationReference::ArrayAccess(GenerateFetchFromLocation(reference),
                                          index);
  }
}

LocationReference ImplementationVisitor::GenerateReferenceToItemInHeapSlice(
    LocationReference slice, VisitResult index) {
  DCHECK(slice.IsHeapSlice());
  Arguments arguments{{index}, {}};
  const StructType* slice_type = *slice.heap_slice().type()->StructSupertype();
  Method* method = LookupMethod("AtIndex", slice_type, arguments, {});
  // The reference has to be treated like a normal value when calling methods
  // on the underlying slice implementation.
  LocationReference slice_value =
      LocationReference::Temporary(slice.GetVisitResult(), "slice as value");
  return LocationReference::HeapReference(
      GenerateCall(method, std::move(slice_value), arguments, {}, false));
}

LocationReference ImplementationVisitor::GetLocationReference(
    IdentifierExpression* expr) {
  if (expr->namespace_qualification.empty()) {
    if (std::optional<Binding<LocalValue>*> value =
            TryLookupLocalValue(expr->name->value)) {
      if (GlobalContext::collect_language_server_data()) {
        LanguageServerData::AddDefinition(expr->name->pos,
                                          (*value)->declaration_position());
      }
      if (GlobalContext::collect_kythe_data()) {
        if (!expr->IsThis()) {
          DCHECK_EQ(expr->name->pos.end.column - expr->name->pos.start.column,
                    expr->name->value.length());
          KytheData::AddBindingUse(expr->name->pos, *value);
        }
      }
      if (!expr->generic_arguments.empty()) {
        ReportError("cannot have generic parameters on local name ",
                    expr->name);
      }
      return (*value)->GetLocationReference(*value);
    }
  }

  if (expr->IsThis()) {
    ReportError("\"this\" cannot be qualified");
  }
  QualifiedName name =
      QualifiedName(expr->namespace_qualification, expr->name->value);
  if (std::optional<Builtin*> builtin = Declarations::TryLookupBuiltin(name)) {
    if (GlobalContext::collect_language_server_data()) {
      LanguageServerData::AddDefinition(expr->name->pos,
                                        (*builtin)->Position());
    }
    // TODO(v8:12261): Consider collecting KytheData here.
    return LocationReference::Temporary(GetBuiltinCode(*builtin),
                                        "builtin " + expr->name->value);
  }
  if (!expr->generic_arguments.empty()) {
    GenericCallable* generic = Declarations::LookupUniqueGeneric(name);
    Callable* specialization =
        GetOrCreateSpecialization(SpecializationKey<GenericCallable>{
            generic, TypeVisitor::ComputeTypeVector(expr->generic_arguments)});
    if (Builtin* builtin = Builtin::DynamicCast(specialization)) {
      DCHECK(!builtin->IsExternal());
      return LocationReference::Temporary(GetBuiltinCode(builtin),
                                          "builtin " + expr->name->value);
    } else {
      ReportError("cannot create function pointer for non-builtin ",
                  generic->name());
    }
  }
  Value* value = Declarations::LookupValue(name);
  CHECK(value->Position().source.IsValid());
  if (auto stream = CurrentFileStreams::Get()) {
    stream->required_builtin_includes.insert(value->Position().source);
  }
  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::AddDefinition(expr->name->pos, value->name()->pos);
  }
  if (auto* constant = NamespaceConstant::DynamicCast(value)) {
    if (GlobalContext::collect_kythe_data()) {
      KytheData::AddConstantUse(expr->name->pos, constant);
    }
    if (constant->type()->IsConstexpr()) {
      return LocationReference::Temporary(
          VisitResult(constant->type(), constant->external_name() + "(state_)"),
          "namespace constant " + expr->name->value);
    }
    assembler().Emit(NamespaceConstantInstruction{constant});
    StackRange stack_range =
        assembler().TopRange(LoweredSlotCount(constant->type()));
    return LocationReference::Temporary(
        VisitResult(constant->type(), stack_range),
        "namespace constant " + expr->name->value);
  }
  ExternConstant* constant = ExternConstant::cast(value);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddConstantUse(expr->name->pos, constant);
  }
  return LocationReference::Temporary(constant->value(),
                                      "extern value " + expr->name->value);
}

LocationReference ImplementationVisitor::GetLocationReference(
    DereferenceExpression* expr) {
  VisitResult ref = Visit(expr->reference);
  if (!TypeOracle::MatchReferenceGeneric(ref.type())) {
    Error("Operator * expects a reference type but found a value of type ",
          *ref.type())
        .Throw();
  }
  return LocationReference::HeapReference(ref);
}

VisitResult ImplementationVisitor::GenerateFetchFromLocation(
    const LocationReference& reference) {
  if (reference.IsTemporary()) {
    return GenerateCopy(reference.temporary());
  } else if (reference.IsVariableAccess()) {
    return GenerateCopy(reference.variable());
  } else if (reference.IsHeapReference()) {
    const Type* referenced_type = *reference.ReferencedType();
    if (referenced_type == TypeOracle::GetFloat64OrHoleType()) {
      return GenerateCall(QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                                        "LoadFloat64OrHole"),
                          Arguments{{reference.heap_reference()}, {}});
    } else if (auto struct_type = referenced_type->StructSupertype()) {
      StackRange result_range = assembler().TopRange(0);
      for (const Field& field : (*struct_type)->fields()) {
        StackScope scope(this);
        const std::string& fieldname = field.name_and_type.name;
        VisitResult field_value = scope.Yield(GenerateFetchFromLocation(
            GenerateFieldAccess(reference, fieldname)));
        result_range.Extend(field_value.stack_range());
      }
      return VisitResult(referenced_type, result_range);
    } else {
      GenerateCopy(reference.heap_reference());
      assembler().Emit(LoadReferenceInstruction{referenced_type});
      DCHECK_EQ(1, LoweredSlotCount(referenced_type));
      return VisitResult(referenced_type, assembler().TopRange(1));
    }
  } else if (reference.IsBitFieldAccess()) {
    // First fetch the bitfield struct, then get the bits out of it.
    VisitResult bit_field_struct =
        GenerateFetchFromLocation(reference.bit_field_struct_location());
    assembler().Emit(LoadBitFieldInstruction{bit_field_struct.type(),
                                             reference.bit_field()});
    return VisitResult(*reference.ReferencedType(), assembler().TopRange(1));
  } else {
    if (reference.IsHeapSlice()) {
      ReportError(
          "fetching a value directly from an indexed field isn't allowed");
    }
    DCHECK(reference.IsCallAccess());
    return GenerateCall(reference.eval_function(),
                        Arguments{reference.call_arguments(), {}});
  }
}

void ImplementationVisitor::GenerateAssignToLocation(
    const LocationReference& reference, const VisitResult& assignment_value) {
  if (reference.IsCallAccess()) {
    Arguments arguments{reference.call_arguments(), {}};
    arguments.parameters.push_back(assignment_value);
    GenerateCall(reference.assign_function(), arguments);
  } else if (reference.IsVariableAccess()) {
    VisitResult variable = reference.variable();
    VisitResult converted_value =
        GenerateImplicitConvert(variable.type(), assignment_value);
    assembler().Poke(variable.stack_range(), converted_value.stack_range(),
                     variable.type());

    // Local variables are detected by the existence of a binding. Assignment
    // to local variables is recorded to support lint errors.
    if (reference.binding()) {
      (*reference.binding())->SetWritten();
    }
  } else if (reference.IsHeapSlice()) {
    ReportError("assigning a value directly to an indexed field isn't allowed");
  } else if (reference.IsHeapReference()) {
    const Type* referenced_type = *reference.ReferencedType();
    if (reference.IsConst()) {
      Error("cannot assign to const value of type ", *referenced_type).Throw();
    }
    if (referenced_type == TypeOracle::GetFloat64OrHoleType()) {
      GenerateCall(
          QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                        "StoreFloat64OrHole"),
          Arguments{{reference.heap_reference(), assignment_value}, {}});
    } else if (auto struct_type = referenced_type->StructSupertype()) {
      if (!assignment_value.type()->IsSubtypeOf(referenced_type)) {
        ReportError("Cannot assign to ", *referenced_type,
                    " with value of type ", *assignment_value.type());
      }
      for (const Field& field : (*struct_type)->fields()) {
        const std::string& fieldname = field.name_and_type.name;
        // Allow assignment of structs even if they contain const fields.
        // Const on struct fields just disallows direct writes to them.
        bool ignore_stuct_field_constness = true;
        GenerateAssignToLocation(
            GenerateFieldAccess(reference, fieldname,
                                ignore_stuct_field_constness),
            ProjectStructField(assignment_value, fieldname));
      }
    } else {
      GenerateCopy(reference.heap_reference());
      VisitResult converted_assignment_value =
          GenerateImplicitConvert(referenced_type, assignment_value);
      if (referenced_type == TypeOracle::GetFloat64Type()) {
        VisitResult silenced_float_value = GenerateCall(
            "Float64SilenceNaN", Arguments{{assignment_value}, {}});
        assembler().Poke(converted_assignment_value.stack_range(),
                         silenced_float_value.stack_range(), referenced_type);
      }
      assembler().Emit(StoreReferenceInstruction{referenced_type});
    }
  } else if (reference.IsBitFieldAccess()) {
    // First fetch the bitfield struct, then set the updated bits, then store
    // it back to where we found it.
    VisitResult bit_field_struct =
        GenerateFetchFromLocation(reference.bit_field_struct_location());
    VisitResult converted_value =
        GenerateImplicitConvert(*reference.ReferencedType(), assignment_value);
    VisitResult updated_bit_field_struct =
        GenerateSetBitField(bit_field_struct.type(), reference.bit_field(),
                            bit_field_struct, converted_value);
    GenerateAssignToLocation(reference.bit_field_struct_location(),
                             updated_bit_field_struct);
  } else {
    DCHECK(reference.IsTemporary());
    ReportError("cannot assign to const-bound or temporary ",
                reference.temporary_description());
  }
}

VisitResult ImplementationVisitor::GeneratePointerCall(
    Expression* callee, const Arguments& arguments, bool is_tailcall) {
  StackScope scope(this);
  TypeVector parameter_types(arguments.parameters.ComputeTypeVector());
  VisitResult callee_result = Visit(callee);
  if (!callee_result.type()->IsBuiltinPointerType()) {
    std::stringstream stream;
    stream << "Expected a function pointer type but found "
           << *callee_result.type();
    ReportError(stream.str());
  }
  const BuiltinPointerType* type =
      BuiltinPointerType::cast(callee_result.type());

  if (type->parameter_types().size() != parameter_types.size()) {
    std::stringstream stream;
    stream << "parameter count mismatch calling function pointer with Type: "
           << *type << " - expected "
           << std::to_string(type->parameter_types().size()) << ", found "
           << std::to_string(parameter_types.size());
    ReportError(stream.str());
  }

  ParameterTypes types{type->parameter_types(), false};
  Signature sig;
  sig.parameter_types = types;
  if (!IsCompatibleSignature(sig, parameter_types, 0)) {
    std::stringstream stream;
    stream << "parameters do not match function pointer signature. Expected: ("
           << type->parameter_types() << ") but got: (" << parameter_types
           << ")";
    ReportError(stream.str());
  }

  callee_result = GenerateCopy(callee_result);
  StackRange arg_range = assembler().TopRange(0);
  for (size_t current = 0; current < arguments.parameters.size(); ++current) {
    const Type* to_type = type->parameter_types()[current];
    arg_range.Extend(
        GenerateImplicitConvert(to_type, arguments.parameters[current])
            .stack_range());
  }

  assembler().Emit(
      CallBuiltinPointerInstruction{is_tailcall, type, arg_range.Size()});

  if (is_tailcall) {
    return VisitResult::NeverResult();
  }
  DCHECK_EQ(1, LoweredSlotCount(type->return_type()));
  return scope.Yield(VisitResult(type->return_type(), assembler().TopRange(1)));
}

void ImplementationVisitor::AddCallParameter(
    Callable* callable, VisitResult parameter, const Type* parameter_type,
    std::vector<VisitResult>* converted_arguments, StackRange* argument_range,
    std::vector<std::string>* constexpr_arguments, bool inline_macro) {
  VisitResult converted;
  if ((converted_arguments->size() < callable->signature().implicit_count) &&
      parameter.type()->IsTopType()) {
    converted = GenerateCopy(parameter);
  } else {
    converted = GenerateImplicitConvert(parameter_type, parameter);
  }
  converted_arguments->push_back(converted);
  if (!inline_macro) {
    if (converted.IsOnStack()) {
      argument_range->Extend(converted.stack_range());
    } else {
      constexpr_arguments->push_back(converted.constexpr_value());
    }
  }
}

namespace {
std::pair<std::string, std::string> GetClassInstanceTypeRange(
    const ClassType* class_type) {
  std::pair<std::string, std::string> result;
  if (class_type->InstanceTypeRange()) {
    auto instance_type_range = *class_type->InstanceTypeRange();
    std::string instance_type_string_first =
        "static_cast<InstanceType>(" +
        std::to_string(instance_type_range.first) + ")";
    std::string instance_type_string_second =
        "static_cast<InstanceType>(" +
        std::to_string(instance_type_range.second) + ")";
    result =
        std::make_pair(instance_type_string_first, instance_type_string_second);
  } else {
    ReportError(
        "%Min/MaxInstanceType must take a class type that is either a string "
        "or has a generated instance type range");
  }
  return result;
}
}  // namespace

VisitResult ImplementationVisitor::GenerateCall(
    Callable* callable, std::optional<LocationReference> this_reference,
    Arguments arguments, const TypeVector& specialization_types,
    bool is_tailcall) {
  CHECK(callable->Position().source.IsValid());
  if (auto stream = CurrentFileStreams::Get()) {
    stream->required_builtin_includes.insert(callable->Position().source);
  }

  const Type* return_type = callable->signature().return_type;

  if (is_tailcall) {
    if (Builtin* builtin = Builtin::DynamicCast(CurrentCallable::Get())) {
      const Type* outer_return_type = builtin->signature().return_type;
      if (!return_type->IsSubtypeOf(outer_return_type)) {
        Error("Cannot tailcall, type of result is ", *return_type,
              " but should be a subtype of ", *outer_return_type, ".");
      }
    } else {
      Error("Tail calls are only allowed from builtins");
    }
  }

  bool inline_macro = callable->ShouldBeInlined(output_type_);
  std::vector<VisitResult> implicit_arguments;
  for (size_t i = 0; i < callable->signature().implicit_count; ++i) {
    std::string implicit_name = callable->signature().parameter_names[i]->value;
    std::optional<Binding<LocalValue>*> val =
        TryLookupLocalValue(implicit_name);
    if (val) {
      implicit_arguments.push_back(
          GenerateFetchFromLocation((*val)->GetLocationReference(*val)));
    } else {
      VisitResult unititialized = VisitResult::TopTypeResult(
          "implicit parameter '" + implicit_name +
              "' is not defined when invoking " + callable->ReadableName() +
              " at " + PositionAsString(CurrentSourcePosition::Get()),
          callable->signature().parameter_types.types[i]);
      implicit_arguments.push_back(unititialized);
    }
    const Type* type = implicit_arguments.back().type();
    if (const TopType* top_type = TopType::DynamicCast(type)) {
      if (!callable->IsMacro() || callable->IsExternal()) {
        ReportError(
            "unititialized implicit parameters can only be passed to "
            "Torque-defined macros: the ",
            top_type->reason());
      }
      inline_macro = true;
    }
  }

  std::vector<VisitResult> converted_arguments;
  StackRange argument_range = assembler().TopRange(0);
  std::vector<std::string> constexpr_arguments;

  size_t current = 0;
  for (; current < callable->signature().implicit_count; ++current) {
    AddCallParameter(callable, implicit_arguments[current],
                     callable->signature().parameter_types.types[current],
                     &converted_arguments, &argument_range,
                     &constexpr_arguments, inline_macro);
  }

  if (this_reference) {
    DCHECK(callable->IsMethod());
    Method* method = Method::cast(callable);
    // By now, the this reference should either be a variable, a temporary or
    // a Slice. In either case the fetch of the VisitResult should succeed.
    VisitResult this_value = this_reference->GetVisitResult();
    if (inline_macro) {
      if (!this_value.type()->IsSubtypeOf(method->aggregate_type())) {
        ReportError("this parameter must be a subtype of ",
                    *method->aggregate_type(), " but it is of type ",
                    *this_value.type());
      }
    } else {
      AddCallParameter(callable, this_value, method->aggregate_type(),
                       &converted_arguments, &argument_range,
                       &constexpr_arguments, inline_macro);
    }
    ++current;
  }

  for (const auto& arg : arguments.parameters) {
    const Type* to_type = (current >= callable->signature().types().size())
                              ? TypeOracle::GetObjectType()
                              : callable->signature().types()[current++];
    AddCallParameter(callable, arg, to_type, &converted_arguments,
                     &argument_range, &constexpr_arguments, inline_macro);
  }

  size_t label_count = callable->signature().labels.size();
  if (label_count != arguments.labels.size()) {
    std::stringstream s;
    s << "unexpected number of otherwise labels for "
      << callable->ReadableName() << " (expected "
      << std::to_string(label_count) << " found "
      << std::to_string(arguments.labels.size()) << ")";
    ReportError(s.str());
  }

  if (callable->IsTransitioning()) {
    if (!CurrentCallable::Get()->IsTransitioning()) {
      std::stringstream s;
      s << *CurrentCallable::Get()
        << " isn't marked transitioning but calls the transitioning "
        << *callable;
      ReportError(s.str());
    }
  }

  if (auto* builtin = Builtin::DynamicCast(callable)) {
    std::optional<Block*> catch_block = GetCatchBlock();
    assembler().Emit(CallBuiltinInstruction{
        is_tailcall, builtin, argument_range.Size(), catch_block});
    GenerateCatchBlock(catch_block);
    if (is_tailcall) {
      return VisitResult::NeverResult();
    } else if (return_type->IsNever()) {
      assembler().Emit(AbortInstruction{AbortInstruction::Kind::kUnreachable});
      return VisitResult::NeverResult();
    } else {
      size_t slot_count = LoweredSlotCount(return_type);
      if (builtin->IsStub()) {
        if (slot_count < 1 || slot_count > 2) {
          ReportError(
              "Builtin with stub linkage is expected to return one or two "
              "values but returns ",
              slot_count);
        }
      } else {
        if (slot_count != 1) {
          ReportError(
              "Builtin with JS linkage is expected to return one value but "
              "returns ",
              slot_count);
        }
      }
      return VisitResult(return_type, assembler().TopRange(slot_count));
    }
  } else if (auto* macro = Macro::DynamicCast(callable)) {
    if (is_tailcall) {
      ReportError("can't tail call a macro");
    }

    macro->SetUsed();

    // If we're currently generating a C++ macro and it's calling another macro,
    // then we need to make sure that we also generate C++ code for the called
    // macro within the same -inl.inc file.
    if ((output_type_ == OutputType::kCC ||
         output_type_ == OutputType::kCCDebug) &&
        !inline_macro) {
      if (auto* torque_macro = TorqueMacro::DynamicCast(macro)) {
        auto* streams = CurrentFileStreams::Get();
        SourceId file = streams ? streams->file : SourceId::Invalid();
        GlobalContext::EnsureInCCOutputList(torque_macro, file);
      }
    }

    // TODO(torque-builder): Consider a function builder here.
    if (return_type->IsConstexpr()) {
      DCHECK_EQ(0, arguments.labels.size());
      std::stringstream result;
      result << "(";
      bool first = true;
      switch (output_type_) {
        case OutputType::kCSA: {
          if (auto* extern_macro = ExternMacro::DynamicCast(macro)) {
            result << extern_macro->external_assembler_name() << "(state_)."
                   << extern_macro->ExternalName() << "(";
          } else {
            result << macro->ExternalName() << "(state_";
            first = false;
          }
          break;
        }
        case OutputType::kCC: {
          auto* extern_macro = ExternMacro::DynamicCast(macro);
          CHECK_NOT_NULL(extern_macro);
          result << extern_macro->CCName() << "(";
          break;
        }
        case OutputType::kCCDebug: {
          auto* extern_macro = ExternMacro::DynamicCast(macro);
          CHECK_NOT_NULL(extern_macro);
          result << extern_macro->CCDebugName() << "(accessor";
          first = false;
          break;
        }
      }
      for (const VisitResult& arg : converted_arguments) {
        DCHECK(!arg.IsOnStack());
        if (!first) {
          result << ", ";
        }
        first = false;
        result << arg.constexpr_value();
      }
      result << "))";
      return VisitResult(return_type, result.str());
    } else if (inline_macro) {
      std::vector<Block*> label_blocks;
      label_blocks.reserve(arguments.labels.size());
      for (Binding<LocalLabel>* label : arguments.labels) {
        label_blocks.push_back(label->block);
      }
      return InlineMacro(macro, this_reference, converted_arguments,
                         label_blocks);
    } else if (arguments.labels.empty() &&
               return_type != TypeOracle::GetNeverType()) {
      std::optional<Block*> catch_block = GetCatchBlock();
      assembler().Emit(
          CallCsaMacroInstruction{macro, constexpr_arguments, catch_block});
      GenerateCatchBlock(catch_block);
      size_t return_slot_count = LoweredSlotCount(return_type);
      return VisitResult(return_type, assembler().TopRange(return_slot_count));
    } else {
      std::optional<Block*> return_continuation;
      if (return_type != TypeOracle::GetNeverType()) {
        return_continuation = assembler().NewBlock();
      }

      std::vector<Block*> label_blocks;

      for (size_t i = 0; i < label_count; ++i) {
        label_blocks.push_back(assembler().NewBlock());
      }
      std::optional<Block*> catch_block = GetCatchBlock();
      assembler().Emit(CallCsaMacroAndBranchInstruction{
          macro, constexpr_arguments, return_continuation, label_blocks,
          catch_block});
      GenerateCatchBlock(catch_block);

      for (size_t i = 0; i < label_count; ++i) {
        Binding<LocalLabel>* label = arguments.labels[i];
        size_t callee_label_parameters =
            callable->signature().labels[i].types.size();
        if (label->parameter_types.size() != callee_label_parameters) {
          std::stringstream s;
          s << "label " << label->name()
            << " doesn't have the right number of parameters (found "
            << std::to_string(label->parameter_types.size()) << " expected "
            << std::to_string(callee_label_parameters) << ")";
          ReportError(s.str());
        }
        assembler().Bind(label_blocks[i]);
        assembler().Goto(
            label->block,
            LowerParameterTypes(callable->signature().labels[i].types).size());

        size_t j = 0;
        for (auto t : callable->signature().labels[i].types) {
          const Type* parameter_type = label->parameter_types[j];
          if (!t->IsSubtypeOf(parameter_type)) {
            ReportError("mismatch of label parameters (label expects ",
                        *parameter_type, " but macro produces ", *t,
                        " for parameter ", i + 1, ")");
          }
          j++;
        }
      }

      if (return_continuation) {
        assembler().Bind(*return_continuation);
        size_t return_slot_count = LoweredSlotCount(return_type);
        return VisitResult(return_type,
                           assembler().TopRange(return_slot_count));
      } else {
        return VisitResult::NeverResult();
      }
    }
  } else if (auto* runtime_function = RuntimeFunction::DynamicCast(callable)) {
    std::optional<Block*> catch_block = GetCatchBlock();
    assembler().Emit(CallRuntimeInstruction{
        is_tailcall, runtime_function, argument_range.Size(), catch_block});
    GenerateCatchBlock(catch_block);
    if (is_tailcall || return_type == TypeOracle::GetNeverType()) {
      return VisitResult::NeverResult();
    } else {
      size_t slot_count = LoweredSlotCount(return_type);
      DCHECK_LE(slot_count, 1);
      // TODO(turbofan): Actually, runtime functions have to return a value, so
      // we should assert slot_count == 1 here.
      return VisitResult(return_type, assembler().TopRange(slot_count));
    }
  } else if (auto* intrinsic = Intrinsic::DynamicCast(callable)) {
    if (intrinsic->ExternalName() == "%SizeOf") {
      if (specialization_types.size() != 1) {
        ReportError("%SizeOf must take a single type parameter");
      }
      const Type* type = specialization_types[0];
      std::string size_string;
      if (std::optional<std::tuple<size_t, std::string>> size = SizeOf(type)) {
        size_string = std::get<1>(*size);
      } else {
        Error("size of ", *type, " is not known.");
      }
      return VisitResult(return_type, size_string);
    } else if (intrinsic->ExternalName() == "%ClassHasMapConstant") {
      const Type* type = specialization_types[0];
      const ClassType* class_type = ClassType::DynamicCast(type);
      if (!class_type) {
        ReportError("%ClassHasMapConstant must take a class type parameter");
      }
      // If the class isn't actually used as the parameter to a TNode,
      // then we can't rely on the class existing in C++ or being of the same
      // type (e.g. it could be a template), so don't use the template CSA
      // machinery for accessing the class' map.
      if (class_type->name() != class_type->GetGeneratedTNodeTypeName()) {
        return VisitResult(return_type, std::string("false"));
      } else {
        return VisitResult(
            return_type,
            std::string("CodeStubAssembler(state_).ClassHasMapConstant<") +
                class_type->name() + ">()");
      }
    } else if (intrinsic->ExternalName() == "%MinInstanceType") {
      if (specialization_types.size() != 1) {
        ReportError("%MinInstanceType must take a single type parameter");
      }
      const Type* type = specialization_types[0];
      const ClassType* class_type = ClassType::DynamicCast(type);
      if (!class_type) {
        ReportError("%MinInstanceType must take a class type parameter");
      }
      std::pair<std::string, std::string> instance_types =
          GetClassInstanceTypeRange(class_type);
      return VisitResult(return_type, instance_types.first);
    } else if (intrinsic->ExternalName() == "%MaxInstanceType") {
      if (specialization_types.size() != 1) {
        ReportError("%MaxInstanceType must take a single type parameter");
      }
      const Type* type = specialization_types[0];
      const ClassType* class_type = ClassType::DynamicCast(type);
      if (!class_type) {
        ReportError("%MaxInstanceType must take a class type parameter");
      }
      std::pair<std::string, std::string> instance_types =
          GetClassInstanceTypeRange(class_type);
      return VisitResult(return_type, instance_types.second);
    } else if (intrinsic->ExternalName() == "%RawConstexprCast") {
      if (intrinsic->signature().parameter_types.types.size() != 1 ||
          constexpr_arguments.size() != 1) {
        ReportError(
            "%RawConstexprCast must take a single parameter with constexpr "
            "type");
      }
      if (!return_type->IsConstexpr()) {
        std::stringstream s;
        s << *return_type
          << " return type for %RawConstexprCast is not constexpr";
        ReportError(s.str());
      }
      std::stringstream result;
      result << "static_cast<" << return_type->GetGeneratedTypeName() << ">(";
      result << constexpr_arguments[0];
      result << ")";
      return VisitResult(return_type, result.str());
    } else if (intrinsic->ExternalName() == "%IndexedFieldLength") {
      const Type* type = specialization_types[0];
      const ClassType* class_type = ClassType::DynamicCast(type);
      if (!class_type) {
        ReportError("%IndexedFieldLength must take a class type parameter");
      }
      const Field& field =
          class_type->LookupField(StringLiteralUnquote(constexpr_arguments[0]));
      return GenerateArrayLength(VisitResult(type, argument_range), field);
    } else if (intrinsic->ExternalName() == "%MakeLazy") {
      if (specialization_types[0]->IsStructType()) {
        ReportError("%MakeLazy can't use macros that return structs");
      }
      std::string getter_name = StringLiteralUnquote(constexpr_arguments[0]);

      // Normally the parser would split namespace names for us, but we
      // sidestepped it by putting the macro name in a string literal.
      QualifiedName qualified_getter_name = QualifiedName::Parse(getter_name);

      // converted_arguments contains all of the arguments to %MakeLazy. We're
      // looking for a function that takes all but the first.
      Arguments arguments_to_getter;
      arguments_to_getter.parameters.insert(
          arguments_to_getter.parameters.begin(),
          converted_arguments.begin() + 1, converted_arguments.end());

      Callable* callable_macro = LookupCallable(
          qualified_getter_name, Declarations::Lookup(qualified_getter_name),
          arguments_to_getter, {});
      Macro* getter = Macro::DynamicCast(callable_macro);
      if (!getter || getter->IsMethod()) {
        ReportError(
            "%MakeLazy expects a macro, not builtin or other type of callable");
      }
      if (!getter->signature().labels.empty()) {
        ReportError("%MakeLazy requires a macro with no labels");
      }
      if (!getter->signature().return_type->IsSubtypeOf(
              specialization_types[0])) {
        ReportError("%MakeLazy expected return type ", *specialization_types[0],
                    " but found ", *getter->signature().return_type);
      }
      if (getter->signature().implicit_count > 0) {
        ReportError("Implicit parameters are not yet supported in %MakeLazy");
      }

      getter->SetUsed();  // Prevent warnings about unused macros.

      // Now that we've looked up the getter macro, we have to convert the
      // arguments again, so that, for example, constexpr arguments can be
      // coerced to non-constexpr types and put on the stack.

      std::vector<VisitResult> converted_arguments_for_getter;
      StackRange argument_range_for_getter = assembler().TopRange(0);
      std::vector<std::string> constexpr_arguments_for_getter;

      size_t arg_count = 0;
      for (const auto& arg : arguments_to_getter.parameters) {
        DCHECK_LT(arg_count, getter->signature().types().size());
        const Type* to_type = getter->signature().types()[arg_count++];
        AddCallParameter(getter, arg, to_type, &converted_arguments_for_getter,
                         &argument_range_for_getter,
                         &constexpr_arguments_for_getter,
                         /*inline_macro=*/false);
      }

      // Now that the arguments are prepared, emit the instruction that consumes
      // them.
      assembler().Emit(MakeLazyNodeInstruction{getter, return_type,
                                               constexpr_arguments_for_getter});
      return VisitResult(return_type, assembler().TopRange(1));
    } else if (intrinsic->ExternalName() == "%FieldSlice") {
      const Type* type = specialization_types[0];
      const ClassType* class_type = ClassType::DynamicCast(type);
      if (!class_type) {
        ReportError("The first type parameter to %FieldSlice must be a class");
      }
      const Field& field =
          class_type->LookupField(StringLiteralUnquote(constexpr_arguments[0]));
      const Type* expected_slice_type =
          field.const_qualified
              ? TypeOracle::GetConstSliceType(field.name_and_type.type)
              : TypeOracle::GetMutableSliceType(field.name_and_type.type);
      const Type* declared_slice_type = specialization_types[1];
      if (expected_slice_type != declared_slice_type) {
        Error(
            "The second type parameter to %FieldSlice must be the precise "
            "slice type for the named field");
      }
      LocationReference ref = GenerateFieldReference(
          VisitResult(type, argument_range), field, class_type,
          /*treat_optional_as_indexed=*/true);
      if (!ref.IsHeapSlice()) {
        ReportError("%FieldSlice expected an indexed or optional field");
      }
      return ref.heap_slice();
    } else {
      assembler().Emit(CallIntrinsicInstruction{intrinsic, specialization_types,
                                                constexpr_arguments});
      size_t return_slot_count =
          LoweredSlotCount(intrinsic->signature().return_type);
      return VisitResult(return_type, assembler().TopRange(return_slot_count));
    }
  } else {
    UNREACHABLE();
  }
}

VisitResult ImplementationVisitor::GenerateCall(
    const QualifiedName& callable_name, Arguments arguments,
    const TypeVector& specialization_types, bool is_tailcall) {
  Callable* callable =
      LookupCallable(callable_name, Declarations::Lookup(callable_name),
                     arguments, specialization_types);
  return GenerateCall(callable, std::nullopt, arguments, specialization_types,
                      is_tailcall);
}

VisitResult ImplementationVisitor::Visit(CallExpression* expr,
                                         bool is_tailcall) {
  StackScope scope(this);

  if (expr->callee->name->value == "&" && expr->arguments.size() == 1) {
    if (auto* loc_expr = LocationExpression::DynamicCast(expr->arguments[0])) {
      LocationReference ref = GetLocationReference(loc_expr);
      if (ref.IsHeapReference()) return scope.Yield(ref.heap_reference());
      if (ref.IsHeapSlice()) return scope.Yield(ref.heap_slice());
    }
    ReportError("Unable to create a heap reference.");
  }

  Arguments arguments;
  QualifiedName name = QualifiedName(expr->callee->namespace_qualification,
                                     expr->callee->name->value);
  TypeVector specialization_types =
      TypeVisitor::ComputeTypeVector(expr->callee->generic_arguments);
  bool has_template_arguments = !specialization_types.empty();
  for (Expression* arg : expr->arguments)
    arguments.parameters.push_back(Visit(arg));
  arguments.labels = LabelsFromIdentifiers(expr->labels);
  if (!has_template_arguments && name.namespace_qualification.empty() &&
      TryLookupLocalValue(name.name)) {
    return scope.Yield(
        GeneratePointerCall(expr->callee, arguments, is_tailcall));
  } else {
    if (GlobalContext::collect_language_server_data()) {
      Callable* callable = LookupCallable(name, Declarations::Lookup(name),
                                          arguments, specialization_types);
      LanguageServerData::AddDefinition(expr->callee->name->pos,
                                        callable->IdentifierPosition());
    }
    if (GlobalContext::collect_kythe_data()) {
      Callable* callable = LookupCallable(name, Declarations::Lookup(name),
                                          arguments, specialization_types);
      Callable* caller = CurrentCallable::Get();
      KytheData::AddCall(caller, expr->callee->name->pos, callable);
    }
    if (expr->callee->name->value == "!" && arguments.parameters.size() == 1) {
      PropagateBitfieldMark(expr->arguments[0], expr);
    }
    if (expr->callee->name->value == "==" && arguments.parameters.size() == 2) {
      if (arguments.parameters[0].type()->IsConstexpr()) {
        PropagateBitfieldMark(expr->arguments[1], expr);
      } else if (arguments.parameters[1].type()->IsConstexpr()) {
        PropagateBitfieldMark(expr->arguments[0], expr);
      }
    }
    return scope.Yield(
        GenerateCall(name, arguments, specialization_types, is_tailcall));
  }
}

VisitResult ImplementationVisitor::Visit(CallMethodExpression* expr) {
  StackScope scope(this);
  Arguments arguments;
  std::string method_name = expr->method->name->value;
  TypeVector specialization_types =
      TypeVisitor::ComputeTypeVector(expr->method->generic_arguments);
  LocationReference target = GetLocationReference(expr->target);
  if (!target.IsVariableAccess()) {
    VisitResult result = GenerateFetchFromLocation(target);
    target = LocationReference::Temporary(result, "this parameter");
  }
  const AggregateType* target_type =
      (*target.ReferencedType())->AggregateSupertype().value_or(nullptr);
  if (!target_type) {
    ReportError("target of method call not a struct or class type");
  }
  for (Expression* arg : expr->arguments) {
    arguments.parameters.push_back(Visit(arg));
  }
  arguments.labels = LabelsFromIdentifiers(expr->labels);
  TypeVector argument_types = arguments.parameters.ComputeTypeVector();
  DCHECK_EQ(expr->method->namespace_qualification.size(), 0);
  QualifiedName qualified_name = QualifiedName(method_name);
  Callable* callable = LookupMethod(method_name, target_type, arguments, {});
  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::AddDefinition(expr->method->name->pos,
                                      callable->IdentifierPosition());
  }
  if (GlobalContext::collect_kythe_data()) {
    Callable* caller = CurrentCallable::Get();
    KytheData::AddCall(caller, expr->method->name->pos, callable);
  }
  return scope.Yield(GenerateCall(callable, target, arguments, {}, false));
}

VisitResult ImplementationVisitor::Visit(IntrinsicCallExpression* expr) {
  StackScope scope(this);
  Arguments arguments;
  TypeVector specialization_types =
      TypeVisitor::ComputeTypeVector(expr->generic_arguments);
  for (Expression* arg : expr->arguments)
    arguments.parameters.push_back(Visit(arg));
  return scope.Yield(
      GenerateCall(expr->name->value, arguments, specialization_types, false));
}

void ImplementationVisitor::GenerateBranch(const VisitResult& condition,
                                           Block* true_block,
                                           Block* false_block) {
  DCHECK_EQ(condition,
            VisitResult(TypeOracle::GetBoolType(), assembler().TopRange(1)));
  assembler().Branch(true_block, false_block);
}

VisitResult ImplementationVisitor::GenerateBoolConstant(bool constant) {
  return GenerateImplicitConvert(TypeOracle::GetBoolType(),
                                 VisitResult(TypeOracle::GetConstexprBoolType(),
                                             constant ? "true" : "false"));
}

void ImplementationVisitor::GenerateExpressionBranch(Expression* expression,
                                                     Block* true_block,
                                                     Block* false_block) {
  StackScope stack_scope(this);
  VisitResult expression_result = this->Visit(expression);
  expression_result = stack_scope.Yield(
      GenerateImplicitConvert(TypeOracle::GetBoolType(), expression_result));
  GenerateBranch(expression_result, true_block, false_block);
}

VisitResult ImplementationVisitor::GenerateImplicitConvert(
    const Type* destination_type, VisitResult source) {
  StackScope scope(this);
  if (source.type() == TypeOracle::GetNeverType()) {
    ReportError("it is not allowed to use a value of type never");
  }

  if (destination_type == source.type()) {
    return scope.Yield(GenerateCopy(source));
  }

  if (auto from = TypeOracle::ImplicitlyConvertableFrom(destination_type,
                                                        source.type())) {
    return scope.Yield(GenerateCall(kFromConstexprMacroName,
                                    Arguments{{source}, {}},
                                    {destination_type, *from}, false));
  } else if (IsAssignableFrom(destination_type, source.type())) {
    source.SetType(destination_type);
    return scope.Yield(GenerateCopy(source));
  } else {
    std::stringstream s;
    if (const TopType* top_type = TopType::DynamicCast(source.type())) {
      s << "undefined expression of type " << *destination_type << ": the "
        << top_type->reason();
    } else {
      s << "cannot use expression of type " << *source.type()
        << " as a value of type " << *destination_type;
    }
    ReportError(s.str());
  }
}

StackRange ImplementationVisitor::GenerateLabelGoto(
    LocalLabel* label, std::optional<StackRange> arguments) {
  return assembler().Goto(label->block, arguments ? arguments->Size() : 0);
}

std::vector<Binding<LocalLabel>*> ImplementationVisitor::LabelsFromIdentifiers(
    const std::vector<Identifier*>& names) {
  std::vector<Binding<LocalLabel>*> result;
  result.reserve(names.size());
  for (const auto& name : names) {
    Binding<LocalLabel>* label = LookupLabel(name->value);
    result.push_back(label);

    // Link up labels in "otherwise" part of the call expression with
    // either the label in the signature of the calling macro or the label
    // block ofa surrounding "try".
    if (GlobalContext::collect_language_server_data()) {
      LanguageServerData::AddDefinition(name->pos,
                                        label->declaration_position());
    }
    // TODO(v8:12261): Might have to track KytheData here.
  }
  return result;
}

StackRange ImplementationVisitor::LowerParameter(
    const Type* type, const std::string& parameter_name,
    Stack<std::string>* lowered_parameters) {
  if (std::optional<const StructType*> struct_type = type->StructSupertype()) {
    StackRange range = lowered_parameters->TopRange(0);
    for (auto& field : (*struct_type)->fields()) {
      StackRange parameter_range = LowerParameter(
          field.name_and_type.type,
          parameter_name + "." + field.name_and_type.name, lowered_parameters);
      range.Extend(parameter_range);
    }
    return range;
  } else {
    lowered_parameters->Push(parameter_name);
    return lowered_parameters->TopRange(1);
  }
}

void ImplementationVisitor::LowerLabelParameter(
    const Type* type, const std::string& parameter_name,
    std::vector<std::string>* lowered_parameters) {
  if (std::optional<const StructType*> struct_type = type->StructSupertype()) {
    for (auto& field : (*struct_type)->fields()) {
      LowerLabelParameter(
          field.name_and_type.type,
          "&((*" + parameter_name + ")." + field.name_and_type.name + ")",
          lowered_parameters);
    }
  } else {
    lowered_parameters->push_back(parameter_name);
  }
}

std::string ImplementationVisitor::ExternalLabelName(
    const std::string& label_name) {
  return "label_" + label_name;
}

std::string ImplementationVisitor::ExternalLabelParameterName(
    const std::string& label_name, size_t i) {
  return "label_" + label_name + "_parameter_" + std::to_string(i);
}

std::string ImplementationVisitor::ExternalParameterName(
    const std::string& name) {
  return std::string("p_") + name;
}

bool IsCompatibleSignature(const Signature& sig, const TypeVector& types,
                           size_t label_count) {
  auto i = sig.parameter_types.types.begin() + sig.implicit_count;
  if ((sig.parameter_types.types.size() - sig.implicit_count) > types.size())
    return false;
  if (sig.labels.size() != label_count) return false;
  for (auto current : types) {
    if (i == sig.parameter_types.types.end()) {
      if (!sig.parameter_types.var_args) return false;
      if (!IsAssignableFrom(TypeOracle::GetObjectType(), current)) return false;
    } else {
      if (!IsAssignableFrom(*i++, current)) return false;
    }
  }
  return true;
}

std::optional<Block*> ImplementationVisitor::GetCatchBlock() {
  std::optional<Block*> catch_block;
  if (TryLookupLabel(kCatchLabelName)) {
    catch_block = assembler().NewBlock(std::nullopt, true);
  }
  return catch_block;
}

void ImplementationVisitor::GenerateCatchBlock(
    std::optional<Block*> catch_block) {
  if (catch_block) {
    std::optional<Binding<LocalLabel>*> catch_handler =
        TryLookupLabel(kCatchLabelName);
    // Reset the local scopes to prevent the macro calls below from using the
    // current catch handler.
    BindingsManagersScope bindings_managers_scope;
    if (assembler().CurrentBlockIsComplete()) {
      assembler().Bind(*catch_block);
      GenerateCall(QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                                 "GetAndResetPendingMessage"),
                   Arguments{{}, {}}, {}, false);
      assembler().Goto((*catch_handler)->block, 2);
    } else {
      CfgAssemblerScopedTemporaryBlock temp(&assembler(), *catch_block);
      GenerateCall(QualifiedName({TORQUE_INTERNAL_NAMESPACE_STRING},
                                 "GetAndResetPendingMessage"),
                   Arguments{{}, {}}, {}, false);
      assembler().Goto((*catch_handler)->block, 2);
    }
  }
}
void ImplementationVisitor::VisitAllDeclarables() {
  CurrentCallable::Scope current_callable(nullptr);
  const std::vector<std::unique_ptr<Declarable>>& all_declarables =
      GlobalContext::AllDeclarables();

  // This has to be an index-based loop because all_declarables can be extended
  // during the loop.
  for (size_t i = 0; i < all_declarables.size(); ++i) {
    try {
      Visit(all_declarables[i].get());
    } catch (TorqueAbortCompilation&) {
      // Recover from compile errors here. The error is recorded already.
    }
  }

  // Do the same for macros which generate C++ code.
  output_type_ = OutputType::kCC;
  const std::vector<std::pair<TorqueMacro*, SourceId>>& cc_macros =
      GlobalContext::AllMacrosForCCOutput();
  for (size_t i = 0; i < cc_macros.size(); ++i) {
    try {
      Visit(static_cast<Declarable*>(cc_macros[i].first), cc_macros[i].second);
    } catch (TorqueAbortCompilation&) {
      // Recover from compile errors here. The error is recorded already.
    }
  }

  // Do the same for macros which generate C++ debug code.
  // The set of macros is the same as C++ macros.
  output_type_ = OutputType::kCCDebug;
  const std::vector<std::pair<TorqueMacro*, SourceId>>& cc_debug_macros =
      GlobalContext::AllMacrosForCCDebugOutput();
  for (size_t i = 0; i < cc_debug_macros.size(); ++i) {
    try {
      Visit(static_cast<Declarable*>(cc_debug_macros[i].first),
            cc_debug_macros[i].second);
    } catch (TorqueAbortCompilation&) {
      // Recover from compile errors here. The error is recorded already.
    }
  }

  output_type_ = OutputType::kCSA;
}

void ImplementationVisitor::Visit(Declarable* declarable,
                                  std::optional<SourceId> file) {
  CurrentScope::Scope current_scope(declarable->ParentScope());
  CurrentSourcePosition::Scope current_source_position(declarable->Position());
  CurrentFileStreams::Scope current_file_streams(
      &GlobalContext::GeneratedPerFile(file ? *file
                                            : declarable->Position().source));
  if (Callable* callable = Callable::DynamicCast(declarable)) {
    if (!callable->ShouldGenerateExternalCode(output_type_))
      CurrentFileStreams::Get() = nullptr;
  }
  switch (declarable->kind()) {
    case Declarable::kExternMacro:
      return Visit(ExternMacro::cast(declarable));
    case Declarable::kTorqueMacro:
      return Visit(TorqueMacro::cast(declarable));
    case Declarable::kMethod:
      return Visit(Method::cast(declarable));
    case Declarable::kBuiltin:
      return Visit(Builtin::cast(declarable));
    case Declarable::kTypeAlias:
      return Visit(TypeAlias::cast(declarable));
    case Declarable::kNamespaceConstant:
      return Visit(NamespaceConstant::cast(declarable));
    case Declarable::kRuntimeFunction:
    case Declarable::kIntrinsic:
    case Declarable::kExternConstant:
    case Declarable::kNamespace:
    case Declarable::kGenericCallable:
    case Declarable::kGenericType:
      return;
  }
}

std::string MachineTypeString(const Type* type) {
  if (type->IsSubtypeOf(TypeOracle::GetSmiType())) {
    return "MachineType::TaggedSigned()";
  }
  if (type->IsSubtypeOf(TypeOracle::GetHeapObjectType())) {
    return "MachineType::TaggedPointer()";
  }
  if (type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
    return "MachineType::AnyTagged()";
  }
  return "MachineTypeOf<" + type->GetGeneratedTNodeTypeName() + ">::value";
}

void ImplementationVisitor::GenerateBuiltinDefinitionsAndInterfaceDescriptors(
    const std::string& output_directory) {
  std::stringstream builtin_definitions;
  std::string builtin_definitions_file_name = "builtin-definitions.h";

  // This file contains plain interface descriptor definitions and has to be
  // included in the middle of interface-descriptors.h. Thus it is not a normal
  // header file and uses the .inc suffix instead of the .h suffix.
  std::stringstream interface_descriptors;
  std::string interface_descriptors_file_name = "interface-descriptors.inc";
  {
    IncludeGuardScope builtin_definitions_include_guard(
        builtin_definitions, builtin_definitions_file_name);

    builtin_definitions
        << "\n"
           "#define BUILTIN_LIST_FROM_TORQUE(CPP, TFJ, TFC, TFS, TFH, "
           "ASM) "
           "\\\n";
    for (auto& declarable : GlobalContext::AllDeclarables()) {
      Builtin* builtin = Builtin::DynamicCast(declarable.get());
      if (!builtin || builtin->IsExternal()) continue;
      if (builtin->IsStub()) {
        builtin_definitions << "TFC(" << builtin->ExternalName() << ", "
                            << builtin->ExternalName();
        if (!builtin->HasCustomInterfaceDescriptor()) {
          std::string descriptor_name = builtin->ExternalName() + "Descriptor";
          bool has_context_parameter =
              builtin->signature().HasContextParameter();
          size_t kFirstNonContextParameter = has_context_parameter ? 1 : 0;
          TypeVector return_types = LowerType(builtin->signature().return_type);

          interface_descriptors << "class " << descriptor_name
                                << " : public StaticCallInterfaceDescriptor<"
                                << descriptor_name << "> {\n";

          interface_descriptors << " public:\n";

          // Currently, no torque-defined builtins are directly exposed to
          // objects inside the sandbox via the code pointer table.
          interface_descriptors << "  INTERNAL_DESCRIPTOR()\n";

          if (has_context_parameter) {
            interface_descriptors << "  DEFINE_RESULT_AND_PARAMETERS(";
          } else {
            interface_descriptors
                << "  DEFINE_RESULT_AND_PARAMETERS_NO_CONTEXT(";
          }
          interface_descriptors << return_types.size();
          for (size_t i = kFirstNonContextParameter;
               i < builtin->parameter_names().size(); ++i) {
            Identifier* parameter = builtin->parameter_names()[i];
            interface_descriptors << ", k" << CamelifyString(parameter->value);
          }
          interface_descriptors << ")\n";

          interface_descriptors << "  DEFINE_RESULT_AND_PARAMETER_TYPES(";
          PrintCommaSeparatedList(interface_descriptors, return_types,
                                  MachineTypeString);
          bool is_first = return_types.empty();
          for (size_t i = kFirstNonContextParameter;
               i < builtin->parameter_names().size(); ++i) {
            const Type* type = builtin->signature().parameter_types.types[i];
            interface_descriptors << (is_first ? "" : ", ")
                                  << MachineTypeString(type);
            is_first = false;
          }
          interface_descriptors << ")\n";

          interface_descriptors << "  DECLARE_DEFAULT_DESCRIPTOR("
                                << descriptor_name << ")\n";
          interface_descriptors << "};\n\n";
        }
      } else {
        builtin_definitions << "TFJ(" << builtin->ExternalName();
        if (builtin->IsVarArgsJavaScript()) {
          builtin_definitions << ", kDontAdaptArgumentsSentinel";
        } else {
          DCHECK(builtin->IsFixedArgsJavaScript());
          // FixedArg javascript builtins need to offer the parameter
          // count.
          int parameter_count =
              static_cast<int>(builtin->signature().ExplicitCount());
          builtin_definitions << ", JSParameterCount(" << parameter_count
                              << ")";
          // And the receiver is explicitly declared.
          builtin_definitions << ", kReceiver";
          for (size_t i = builtin->signature().implicit_count;
               i < builtin->parameter_names().size(); ++i) {
            Identifier* parameter = builtin->parameter_names()[i];
            builtin_definitions << ", k" << CamelifyString(parameter->value);
          }
        }
      }
      builtin_definitions << ") \\\n";
    }
    builtin_definitions << "\n";

    builtin_definitions
        << "#define TORQUE_FUNCTION_POINTER_TYPE_TO_BUILTIN_MAP(V) \\\n";
    for (const BuiltinPointerType* type :
         TypeOracle::AllBuiltinPointerTypes()) {
      Builtin* example_builtin =
          Declarations::FindSomeInternalBuiltinWithType(type);
      if (!example_builtin) {
        CurrentSourcePosition::Scope current_source_position(
            SourcePosition{CurrentSourceFile::Get(), LineAndColumn::Invalid(),
                           LineAndColumn::Invalid()});
        ReportError("unable to find any builtin with type \"", *type, "\"");
      }
      builtin_definitions << "  V(" << type->function_pointer_type_id() << ","
                          << example_builtin->ExternalName() << ")\\\n";
    }
    builtin_definitions << "\n";
  }
  WriteFile(output_directory + "/" + builtin_definitions_file_name,
            builtin_definitions.str());
  WriteFile(output_directory + "/" + interface_descriptors_file_name,
            interface_descriptors.str());
}

namespace {

enum class FieldSectionType : uint32_t {
  kNoSection = 0,
  kWeakSection = 1 << 0,
  kStrongSection = 2 << 0,
  kScalarSection = 3 << 0
};

bool IsPointerSection(FieldSectionType type) {
  return type == FieldSectionType::kWeakSection ||
         type == FieldSectionType::kStrongSection;
}

using FieldSections = base::Flags<FieldSectionType>;

std::string ToString(FieldSectionType type) {
  switch (type) {
    case FieldSectionType::kNoSection:
      return "NoSection";
    case FieldSectionType::kWeakSection:
      return "WeakFields";
    case FieldSectionType::kStrongSection:
      return "StrongFields";
    case FieldSectionType::kScalarSection:
      return "ScalarFields";
  }
  UNREACHABLE();
}

class FieldOffsetsGenerator {
 public:
  explicit FieldOffsetsGenerator(const ClassType* type) : type_(type) {}

  virtual void WriteField(const Field& f, const std::string& size_string) = 0;
  virtual void WriteFieldOffsetGetter(const Field& f) = 0;
  virtual void WriteMarker(const std::string& marker) = 0;

  virtual ~FieldOffsetsGenerator() { CHECK(is_finished_); }

  void RecordOffsetFor(const Field& f) {
    CHECK(!is_finished_);
    UpdateSection(f);

    // Emit kHeaderSize before any indexed field.
    if (f.index.has_value() && !header_size_emitted_) {
      WriteMarker("kHeaderSize");
      header_size_emitted_ = true;
    }

    // We don't know statically how much space an indexed field takes, so report
    // it as zero.
    std::string size_string = "0";
    if (!f.index.has_value()) {
      size_t field_size;
      std::tie(field_size, size_string) = f.GetFieldSizeInformation();
    }
    if (f.offset.has_value()) {
      WriteField(f, size_string);
    } else {
      WriteFieldOffsetGetter(f);
    }
  }

  void Finish() {
    End(current_section_);
    if (!(completed_sections_ & FieldSectionType::kWeakSection)) {
      Begin(FieldSectionType::kWeakSection);
      End(FieldSectionType::kWeakSection);
    }
    if (!(completed_sections_ & FieldSectionType::kStrongSection)) {
      Begin(FieldSectionType::kStrongSection);
      End(FieldSectionType::kStrongSection);
    }
    is_finished_ = true;

    // In the presence of indexed fields, we already emitted kHeaderSize before
    // the indexed field.
    if (!type_->IsShape() && !header_size_emitted_) {
      WriteMarker("kHeaderSize");
    }
    if (!type_->IsAbstract() && type_->HasStaticSize()) {
      WriteMarker("kSize");
    }
  }

 protected:
  const ClassType* type_;

 private:
  FieldSectionType GetSectionFor(const Field& f) {
    const Type* field_type = f.name_and_type.type;
    if (field_type == TypeOracle::GetVoidType()) {
      // Allow void type for marker constants of size zero.
      return current_section_;
    }
    StructType::Classification struct_contents =
        StructType::ClassificationFlag::kEmpty;
    if (auto field_as_struct = field_type->StructSupertype()) {
      struct_contents = (*field_as_struct)->ClassifyContents();
    }
    if ((struct_contents & StructType::ClassificationFlag::kStrongTagged) &&
        (struct_contents & StructType::ClassificationFlag::kWeakTagged)) {
      // It's okay for a struct to contain both strong and weak data. We'll just
      // treat the whole thing as weak. This is required for DescriptorEntry.
      struct_contents &= ~StructType::Classification(
          StructType::ClassificationFlag::kStrongTagged);
    }
    bool struct_contains_tagged_fields =
        (struct_contents & StructType::ClassificationFlag::kStrongTagged) ||
        (struct_contents & StructType::ClassificationFlag::kWeakTagged);
    if (struct_contains_tagged_fields &&
        (struct_contents & StructType::ClassificationFlag::kUntagged)) {
      // We can't declare what section a struct goes in if it has multiple
      // categories of data within.
      Error(
          "Classes do not support fields which are structs containing both "
          "tagged and untagged data.")
          .Position(f.pos);
    }
    if ((field_type->IsSubtypeOf(TypeOracle::GetStrongTaggedType()) ||
         struct_contents == StructType::ClassificationFlag::kStrongTagged) &&
        !f.custom_weak_marking) {
      return FieldSectionType::kStrongSection;
    } else if (field_type->IsSubtypeOf(TypeOracle::GetTaggedType()) ||
               struct_contains_tagged_fields) {
      return FieldSectionType::kWeakSection;
    } else {
      return FieldSectionType::kScalarSection;
    }
  }
  void UpdateSection(const Field& f) {
    FieldSectionType type = GetSectionFor(f);
    if (current_section_ == type) return;
    if (IsPointerSection(type)) {
      if (completed_sections_ & type) {
        std::stringstream s;
        s << "cannot declare field " << f.name_and_type.name << " in class "
          << type_->name() << ", because section " << ToString(type)
          << " to which it belongs has already been finished.";
        Error(s.str()).Position(f.pos);
      }
    }
    End(current_section_);
    current_section_ = type;
    Begin(current_section_);
  }
  void Begin(FieldSectionType type) {
    DCHECK(type != FieldSectionType::kNoSection);
    if (!IsPointerSection(type)) return;
    WriteMarker("kStartOf" + ToString(type) + "Offset");
  }
  void End(FieldSectionType type) {
    if (!IsPointerSection(type)) return;
    completed_sections_ |= type;
    WriteMarker("kEndOf" + ToString(type) + "Offset");
  }

  FieldSectionType current_section_ = FieldSectionType::kNoSection;
  FieldSections completed_sections_ = FieldSectionType::kNoSection;
  bool is_finished_ = false;
  bool header_size_emitted_ = false;
};

void GenerateClassExport(const ClassType* type, std::ostream& header,
                         std::ostream& inl_header) {
  const ClassType* super = type->GetSuperClass();
  std::string parent = "TorqueGenerated" + type->name() + "<" + type->name() +
                       ", " + super->name() + ">";
  header << "class " << type->name() << " : public " << parent << " {\n";
  header << " public:\n";
  if (type->ShouldGenerateBodyDescriptor()) {
    header << "  class BodyDescriptor;\n";
  }
  header << "  TQ_OBJECT_CONSTRUCTORS(" << type->name() << ")\n";
  header << "};\n\n";
  inl_header << "TQ_OBJECT_CONSTRUCTORS_IMPL(" << type->name() << ")\n";
}

}  // namespace

void ImplementationVisitor::GenerateVisitorLists(
    const std::string& output_directory) {
  std::stringstream header;
  std::string file_name = "visitor-lists.h";
  {
    IncludeGuardScope include_guard(header, file_name);

    header << "#define TORQUE_INSTANCE_TYPE_TO_BODY_DESCRIPTOR_LIST(V)\\\n";
    for (const ClassType* type : TypeOracle::GetClasses()) {
      if (type->ShouldGenerateBodyDescriptor() && type->OwnInstanceType()) {
        std::string type_name =
            CapifyStringWithUnderscores(type->name()) + "_TYPE";
        header << "V(" << type_name << "," << type->name() << ")\\\n";
      }
    }
    header << "\n";

    header << "#define TORQUE_DATA_ONLY_VISITOR_ID_LIST(V)\\\n";
    for (const ClassType* type : TypeOracle::GetClasses()) {
      if (type->ShouldGenerateBodyDescriptor() &&
          type->HasNoPointerSlotsExceptMap()) {
        header << "V(" << type->name() << ")\\\n";
      }
    }
    header << "\n";

    header << "#define TORQUE_POINTER_VISITOR_ID_LIST(V)\\\n";
    for (const ClassType* type : TypeOracle::GetClasses()) {
      if (type->ShouldGenerateBodyDescriptor() &&
          !type->HasNoPointerSlotsExceptMap()) {
        header << "V(" << type->name() << ")\\\n";
      }
    }
    header << "\n";
  }
  const std::string output_header_path = output_directory + "/" + file_name;
  WriteFile(output_header_path, header.str());
}

void ImplementationVisitor::GenerateBitFields(
    const std::string& output_directory) {
  std::stringstream header;
  std::string file_name = "bit-fields.h";
  {
    IncludeGuardScope include_guard(header, file_name);
    header << "#include \"src/base/bit-field.h\"\n\n";
    NamespaceScope namespaces(header, {"v8", "internal"});

    for (const auto& type : TypeOracle::GetBitFieldStructTypes()) {
      bool all_single_bits = true;  // Track whether every field is one bit.
      header << "// " << type->GetPosition() << "\n";
      header << "#define DEFINE_TORQUE_GENERATED_"
             << CapifyStringWithUnderscores(type->name()) << "() \\\n";
      std::string type_name = type->GetConstexprGeneratedTypeName();
      for (const auto& field : type->fields()) {
        const char* suffix = field.num_bits == 1 ? "Bit" : "Bits";
        all_single_bits = all_single_bits && field.num_bits == 1;
        std::string field_type_name =
            field.name_and_type.type->GetConstexprGeneratedTypeName();
        header << "  using " << CamelifyString(field.name_and_type.name)
               << suffix << " = base::BitField<" << field_type_name << ", "
               << field.offset << ", " << field.num_bits << ", " << type_name
               << ">; \\\n";
      }

      // If every field is one bit, we can also generate a convenient enum.
      if (all_single_bits) {
        header << "  enum Flag: " << type_name << " { \\\n";
        header << "    kNone = 0, \\\n";
        for (const auto& field : type->fields()) {
          header << "    k" << CamelifyString(field.name_and_type.name) << " = "
                 << type_name << "{1} << " << field.offset << ", \\\n";
        }
        header << "  }; \\\n";
        header << "  using Flags = base::Flags<Flag>; \\\n";
        header << "  static constexpr int kFlagCount = "
               << type->fields().size() << "; \\\n";
      }

      header << "\n";
    }
  }
  const std::string output_header_path = output_directory + "/" + file_name;
  WriteFile(output_header_path, header.str());
}

namespace {

class ClassFieldOffsetGenerator : public FieldOffsetsGenerator {
 public:
  ClassFieldOffsetGenerator(std::ostream& header, std::ostream& inline_header,
                            const ClassType* type, std::string gen_name,
                            const ClassType* parent, bool use_templates = true)
      : FieldOffsetsGenerator(type),
        hdr_(header),
        inl_(inline_header),
        previous_field_end_(FirstFieldStart(type, parent, use_templates)),
        gen_name_(gen_name) {}

  void WriteField(const Field& f, const std::string& size_string) override {
    hdr_ << "  // " << f.pos << "\n";
    std::string field = "k" + CamelifyString(f.name_and_type.name) + "Offset";
    std::string field_end = field + "End";
    hdr_ << "  static constexpr int " << field << " = " << previous_field_end_
         << ";\n";
    hdr_ << "  static constexpr int " << field_end << " = " << field << " + "
         << size_string << " - 1;\n";
    previous_field_end_ = field_end + " + 1";
  }

  void WriteFieldOffsetGetter(const Field& f) override {
    // A static constexpr int is more convenient than a getter if the offset is
    // known.
    DCHECK(!f.offset.has_value());

    std::string function_name = CamelifyString(f.name_and_type.name) + "Offset";

    std::vector<cpp::TemplateParameter> params = {cpp::TemplateParameter("D"),
                                                  cpp::TemplateParameter("P")};
    cpp::Class owner(std::move(params), gen_name_);

    auto getter = cpp::Function::DefaultGetter("int", &owner, function_name);
    getter.PrintDeclaration(hdr_);
    getter.PrintDefinition(inl_, [&](std::ostream& stream) {
      // Item 1 in a flattened slice is the offset.
      stream << "  return static_cast<int>(std::get<1>("
             << Callable::PrefixNameForCCOutput(type_->GetSliceMacroName(f))
             << "(*static_cast<const D*>(this))));\n";
    });
  }
  void WriteMarker(const std::string& marker) override {
    hdr_ << "  static constexpr int " << marker << " = " << previous_field_end_
         << ";\n";
  }

 private:
  static std::string FirstFieldStart(const ClassType* type,
                                     const ClassType* parent,
                                     bool use_templates = true) {
    std::string parent_name = use_templates ? "P" : parent->name();

    if (type->IsLayoutDefinedInCpp()) {
      // TODO(leszeks): Hacked in support for some classes (e.g.
      // HeapObject) being mirrored by a *Layout class. Remove once
      // everything is ported to layout classes.
      if (parent_name == "HeapObject") {
        parent_name += "Layout";
      }

      return "sizeof(" + parent_name + ")";
    }

    if (parent && parent->IsShape()) {
      return parent_name + "::kSize";
    }
    return parent_name + "::kHeaderSize";
  }

  std::ostream& hdr_;
  std::ostream& inl_;
  std::string previous_field_end_;
  std::string gen_name_;
};

class CppClassGenerator {
 public:
  CppClassGenerator(const ClassType* type, std::ostream& header,
                    std::ostream& inl_header, std::ostream& impl)
      : type_(type),
        super_(type->GetSuperClass()),
        name_(type->name()),
        gen_name_("TorqueGenerated" + name_),
        gen_name_T_(gen_name_ + "<D, P>"),
        gen_name_I_(gen_name_ + "<" + name_ + ", " + super_->name() + ">"),
        hdr_(header),
        inl_(inl_header),
        impl_(impl) {}
  const std::string template_decl() const {
    return "template <class D, class P>";
  }

  void GenerateClass();
  void GenerateCppObjectDefinitionAsserts();
  void GenerateCppObjectLayoutDefinitionAsserts();

 private:
  SourcePosition Position();

  void GenerateClassConstructors();

  // Generates getter and setter runtime member functions for the given class
  // field. Traverses depth-first through any nested struct fields to generate
  // accessors for them also; struct_fields represents the stack of currently
  // active struct fields.
  void GenerateFieldAccessors(const Field& class_field,
                              std::vector<const Field*>& struct_fields);
  void EmitLoadFieldStatement(std::ostream& stream, const Field& class_field,
                              std::vector<const Field*>& struct_fields);
  void EmitStoreFieldStatement(std::ostream& stream, const Field& class_field,
                               std::vector<const Field*>& struct_fields);

  std::string GetFieldOffsetForAccessor(const Field& f);

  // Gets the C++ type name that should be used in accessors for referring to
  // the value of a class field.
  std::string GetTypeNameForAccessor(const Field& f);

  bool CanContainHeapObjects(const Type* t);

  const ClassType* type_;
  const ClassType* super_;
  const std::string name_;
  const std::string gen_name_;
  const std::string gen_name_T_;
  const std::string gen_name_I_;
  std::ostream& hdr_;
  std::ostream& inl_;
  std::ostream& impl_;
};

std::optional<std::vector<Field>> GetOrderedUniqueIndexFields(
    const ClassType& type) {
  std::vector<Field> result;
  std::set<std::string> index_names;
  for (const Field& field : type.ComputeAllFields()) {
    if (field.index) {
      auto name_and_type = ExtractSimpleFieldArraySize(type, field.index->expr);
      if (!name_and_type) {
        return std::nullopt;
      }
      index_names.insert(name_and_type->name);
    }
  }

  for (const Field& field : type.ComputeAllFields()) {
    if (index_names.count(field.name_and_type.name) != 0) {
      result.push_back(field);
    }
  }

  return result;
}

void CppClassGenerator::GenerateClass() {
  // Is<name>_NonInline(Tagged<HeapObject>)
  if (!type_->IsShape()) {
    cpp::Function f("Is"s + name_ + "_NonInline");
    f.SetDescription("Alias for Is"s + name_ + "() that avoids inlining.");
    f.SetExport(true);
    f.SetReturnType("bool");
    f.AddParameter("Tagged<HeapObject>", "o");

    f.PrintDeclaration(hdr_);
    hdr_ << "\n";
    f.PrintDefinition(impl_, [&](std::ostream& stream) {
      stream << "  return Is" << name_ << "(o);\n";
    });
  }
  hdr_ << "// Definition " << Position() << "\n";
  hdr_ << template_decl() << "\n";
  hdr_ << "class " << gen_name_ << " : public P {\n";
  hdr_ << "  static_assert(\n"
       << "      std::is_same<" << name_ << ", D>::value,\n"
       << "      \"Use this class as direct base for " << name_ << ".\");\n";
  hdr_ << "  static_assert(\n"
       << "      std::is_same<" << super_->name() << ", P>::value,\n"
       << "      \"Pass in " << super_->name()
       << " as second template parameter for " << gen_name_ << ".\");\n\n";
  hdr_ << " public: \n";
  hdr_ << "  using Super = P;\n";
  hdr_ << "  using TorqueGeneratedClass = " << gen_name_ << "<D,P>;\n\n";
  if (!type_->ShouldExport() && !type_->IsExtern()) {
    hdr_ << " protected: // not extern or @export\n";
  }
  for (const Field& f : type_->fields()) {
    CurrentSourcePosition::Scope scope(f.pos);
    std::vector<const Field*> struct_fields;
    GenerateFieldAccessors(f, struct_fields);
  }
  if (!type_->ShouldExport() && !type_->IsExtern()) {
    hdr_ << " public:\n";
  }

  std::vector<cpp::TemplateParameter> templateArgs = {
      cpp::TemplateParameter("D"), cpp::TemplateParameter("P")};
  cpp::Class c(std::move(templateArgs), gen_name_);

  if (type_->ShouldGeneratePrint()) {
    hdr_ << "  DECL_PRINTER(" << name_ << ")\n\n";
  }

  if (type_->ShouldGenerateVerify()) {
    IfDefScope hdr_scope(hdr_, "VERIFY_HEAP");
    // V8_EXPORT_PRIVATE void Verify(Isolate*);
    cpp::Function f(&c, name_ + "Verify");
    f.SetExport();
    f.SetReturnType("void");
    f.AddParameter("Isolate*", "isolate");
    f.PrintDeclaration(hdr_);

    IfDefScope impl_scope(impl_, "VERIFY_HEAP");
    impl_ << "\ntemplate <>\n";
    impl_ << "void " << gen_name_I_ << "::" << name_
          << "Verify(Isolate* isolate) {\n";
    impl_ << "  TorqueGeneratedClassVerifiers::" << name_ << "Verify(Cast<"
          << name_
          << ">(*this), "
             "isolate);\n";
    impl_ << "}\n\n";
    impl_ << "\n";
  }

  hdr_ << "\n";
  ClassFieldOffsetGenerator g(hdr_, inl_, type_, gen_name_,
                              type_->GetSuperClass());
  for (const auto& f : type_->fields()) {
    CurrentSourcePosition::Scope scope(f.pos);
    g.RecordOffsetFor(f);
  }
  g.Finish();
  hdr_ << "\n";

  auto index_fields = GetOrderedUniqueIndexFields(*type_);

  if (!index_fields.has_value()) {
    hdr_ << "  // SizeFor implementations not generated due to complex array "
            "lengths\n\n";

    const Field& last_field = type_->LastField();
    std::string last_field_item_size =
        std::get<1>(*SizeOf(last_field.name_and_type.type));

    // int AllocatedSize() const
    {
      cpp::Function f =
          cpp::Function::DefaultGetter("int", &c, "AllocatedSize");
      f.PrintDeclaration(hdr_);

      f.PrintDefinition(inl_, [&](std::ostream& stream) {
        stream << "  auto slice = "
               << Callable::PrefixNameForCCOutput(
                      type_->GetSliceMacroName(last_field))
               << "(*static_cast<const D*>(this));\n";
        stream << "  return static_cast<int>(std::get<1>(slice)) + "
               << last_field_item_size
               << " * static_cast<int>(std::get<2>(slice));\n";
      });
    }
  } else if (type_->ShouldGenerateBodyDescriptor() ||
             (!type_->IsAbstract() &&
              !type_->IsSubtypeOf(TypeOracle::GetJSObjectType()))) {
    cpp::Function f(&c, "SizeFor");
    f.SetReturnType("int32_t");
    f.SetFlags(cpp::Function::kStatic | cpp::Function::kConstexpr |
               cpp::Function::kV8Inline);
    for (const Field& field : *index_fields) {
      f.AddParameter("int", field.name_and_type.name);
    }
    f.PrintInlineDefinition(hdr_, [&](std::ostream& stream) {
      if (index_fields->empty()) {
        stream << "    DCHECK(kHeaderSize == kSize && kHeaderSize == "
               << *type_->size().SingleValue() << ");\n";
      }
      stream << "    int32_t size = kHeaderSize;\n";
      for (const Field& field : type_->ComputeAllFields()) {
        if (field.index) {
          auto index_name_and_type =
              *ExtractSimpleFieldArraySize(*type_, field.index->expr);
          stream << "    size += " << index_name_and_type.name << " * "
                 << std::get<0>(field.GetFieldSizeInformation()) << ";\n";
        }
      }
      if (type_->size().Alignment() < TargetArchitecture::TaggedSize()) {
        stream << "    size = OBJECT_POINTER_ALIGN(size);\n";
      }
      stream << "    return size;\n";
    });

    // V8_INLINE int32_t AllocatedSize() const
    {
      cpp::Function allocated_size_f =
          cpp::Function::DefaultGetter("int32_t", &c, "AllocatedSize");
      allocated_size_f.SetFlag(cpp::Function::kV8Inline);
      allocated_size_f.PrintInlineDefinition(hdr_, [&](std::ostream& stream) {
        stream << "    return SizeFor(";
        bool first = true;
        for (const auto& field : *index_fields) {
          if (!first) stream << ", ";
          stream << "this->" << field.name_and_type.name << "()";
          first = false;
        }
        stream << ");\n";
      });
    }
  }

  hdr_ << "  friend class Factory;\n\n";

  GenerateClassConstructors();

  hdr_ << "};\n\n";

  if (type_->ShouldGenerateFullClassDefinition()) {
    // If this class extends from another class which is defined in the same tq
    // file, and that other class doesn't generate a full class definition, then
    // the resulting .inc file would be uncompilable due to ordering
    // requirements: the generated file must go before the hand-written
    // definition of the base class, but it must also go after that same
    // hand-written definition.
    std::optional<const ClassType*> parent = type_->parent()->ClassSupertype();
    while (parent) {
      if ((*parent)->ShouldGenerateCppClassDefinitions() &&
          !(*parent)->ShouldGenerateFullClassDefinition() &&
          (*parent)->AttributedToFile() == type_->AttributedToFile()) {
        Error("Exported ", *type_,
              " cannot be in the same file as its parent extern ", **parent);
      }
      parent = (*parent)->parent()->ClassSupertype();
    }

    GenerateClassExport(type_, hdr_, inl_);
  }
}

void CppClassGenerator::GenerateCppObjectDefinitionAsserts() {
  impl_ << "// Definition " << Position() << "\n"
        << "class " << gen_name_ << "Asserts {\n";

  ClassFieldOffsetGenerator g(impl_, impl_, type_, gen_name_,
                              type_->GetSuperClass(), false);
  for (const auto& f : type_->fields()) {
    CurrentSourcePosition::Scope scope(f.pos);
    g.RecordOffsetFor(f);
  }
  g.Finish();
  impl_ << "\n";

  for (const auto& f : type_->fields()) {
    std::string field_offset =
        "k" + CamelifyString(f.name_and_type.name) + "Offset";
    impl_ << "  static_assert(" << field_offset << " == " << name_
          << "::" << field_offset << ",\n"
          << "                \"Values of " << name_ << "::" << field_offset
          << " defined in Torque and C++ do not match\");\n";
  }
  if (!type_->IsAbstract() && type_->HasStaticSize()) {
    impl_ << "  static_assert(kSize == " << name_ << "::kSize);\n";
  }

  impl_ << "};\n\n";
}

void CppClassGenerator::GenerateCppObjectLayoutDefinitionAsserts() {
  impl_ << "// Definition " << Position() << "\n"
        << "class " << gen_name_ << "Asserts {\n";

  ClassFieldOffsetGenerator g(impl_, impl_, type_, gen_name_,
                              type_->GetSuperClass(), false);
  for (const auto& f : type_->fields()) {
    CurrentSourcePosition::Scope scope(f.pos);
    g.RecordOffsetFor(f);
  }
  g.Finish();
  impl_ << "\n";

  for (const auto& f : type_->fields()) {
    std::string field_offset =
        "k" + CamelifyString(f.name_and_type.name) + "Offset";
    std::string cpp_field_offset =
        f.index.has_value()
            ? "OFFSET_OF_DATA_START(" + name_ + ")"
            : "offsetof(" + name_ + ", " + f.name_and_type.name + "_)";
    impl_ << "  static_assert(" << field_offset << " == " << cpp_field_offset
          << ",\n"
          << "                \"Value of " << name_ << "::" << field_offset
          << " defined in Torque and offset of field " << name_
          << "::" << f.name_and_type.name << " in C++ do not match\");\n";
  }
  if (!type_->IsAbstract() && type_->HasStaticSize()) {
    impl_ << "  static_assert(kSize == sizeof(" + name_ + "));\n";
  }

  impl_ << "};\n\n";
}

SourcePosition CppClassGenerator::Position() { return type_->GetPosition(); }

void CppClassGenerator::GenerateClassConstructors() {
  const ClassType* typecheck_type = type_;
  while (typecheck_type->IsShape()) {
    typecheck_type = typecheck_type->GetSuperClass();

    // Shapes have already been checked earlier to inherit from JSObject, so we
    // should have found an appropriate type.
    DCHECK(typecheck_type);
  }

  hdr_ << "  template <class DAlias = D>\n";
  hdr_ << "  constexpr " << gen_name_ << "() : P() {\n";
  hdr_ << "    static_assert(\n";
  hdr_ << "        std::is_base_of<" << gen_name_ << ", DAlias>::value,\n";
  hdr_ << "        \"class " << gen_name_
       << " should be used as direct base for " << name_ << ".\");\n";
  hdr_ << "  }\n\n";

  hdr_ << " protected:\n";
  hdr_ << "  inline explicit constexpr " << gen_name_
       << "(Address ptr, typename P::SkipTypeCheckTag\n)";
  hdr_ << "    : P(ptr, typename P::SkipTypeCheckTag{}) {}\n";
  hdr_ << "  inline explicit " << gen_name_ << "(Address ptr);\n";

  inl_ << "template<class D, class P>\n";
  inl_ << "inline " << gen_name_T_ << "::" << gen_name_ << "(Address ptr)\n";
  inl_ << "    : P(ptr) {\n";
  inl_ << "  SLOW_DCHECK(Is" << typecheck_type->name()
       << "_NonInline(*this));\n";
  inl_ << "}\n";
}

namespace {
std::string GenerateRuntimeTypeCheck(const Type* type,
                                     const std::string& value) {
  bool maybe_object = !type->IsSubtypeOf(TypeOracle::GetStrongTaggedType());
  std::stringstream type_check;
  bool at_start = true;
  // If weak pointers are allowed, then start by checking for a cleared value.
  if (maybe_object) {
    type_check << value << ".IsCleared()";
    at_start = false;
  }
  for (const TypeChecker& runtime_type : type->GetTypeCheckers()) {
    if (!at_start) type_check << " || ";
    at_start = false;
    if (maybe_object) {
      bool strong = runtime_type.weak_ref_to.empty();
      if (strong && runtime_type.type == WEAK_HEAP_OBJECT) {
        // Rather than a generic Weak<T>, this is the basic type WeakHeapObject.
        // We can't validate anything more about the type of the object pointed
        // to, so just check that it's weak.
        type_check << value << ".IsWeak()";
      } else {
        type_check << "(" << (strong ? "!" : "") << value << ".IsWeak() && Is"
                   << (strong ? runtime_type.type : runtime_type.weak_ref_to)
                   << "(" << value << ".GetHeapObjectOrSmi()))";
      }
    } else {
      type_check << "Is" << runtime_type.type << "(" << value << ")";
    }
  }
  return type_check.str();
}

void GenerateBoundsDCheck(std::ostream& os, const std::string& index,
                          const ClassType* type, const Field& f) {
  os << "  DCHECK_GE(" << index << ", 0);\n";
  std::string length_expression;
  if (std::optional<NameAndType> array_length =
          ExtractSimpleFieldArraySize(*type, f.index->expr)) {
    length_expression = "this ->" + array_length->name + "()";
  } else {
    // The length is element 2 in the flattened field slice.
    length_expression =
        "static_cast<int>(std::get<2>(" +
        Callable::PrefixNameForCCOutput(type->GetSliceMacroName(f)) +
        "(*static_cast<const D*>(this))))";
  }
  os << "  DCHECK_LT(" << index << ", " << length_expression << ");\n";
}

bool CanGenerateFieldAccessors(const Type* field_type) {
  // float64_or_hole should be treated like float64. For now, we don't need it.
  // TODO(v8:10391) Generate accessors for external pointers.
  return field_type != TypeOracle::GetVoidType() &&
         field_type != TypeOracle::GetFloat64OrHoleType() &&
         !field_type->IsSubtypeOf(TypeOracle::GetExternalPointerType()) &&
         !field_type->IsSubtypeOf(TypeOracle::GetTrustedPointerType()) &&
         !field_type->IsSubtypeOf(TypeOracle::GetProtectedPointerType());
}
}  // namespace

// TODO(sigurds): Keep in sync with DECL_ACCESSORS and ACCESSORS macro.
void CppClassGenerator::GenerateFieldAccessors(
    const Field& class_field, std::vector<const Field*>& struct_fields) {
  const Field& innermost_field =
      struct_fields.empty() ? class_field : *struct_fields.back();
  const Type* field_type = innermost_field.name_and_type.type;
  if (!CanGenerateFieldAccessors(field_type)) return;

  if (const StructType* struct_type = StructType::DynamicCast(field_type)) {
    struct_fields.resize(struct_fields.size() + 1);
    for (const Field& struct_field : struct_type->fields()) {
      struct_fields[struct_fields.size() - 1] = &struct_field;
      GenerateFieldAccessors(class_field, struct_fields);
    }
    struct_fields.resize(struct_fields.size() - 1);
    return;
  }

  bool indexed = class_field.index && !class_field.index->optional;
  std::string type_name = GetTypeNameForAccessor(innermost_field);
  bool can_contain_heap_objects = CanContainHeapObjects(field_type);

  // Assemble an accessor name by accumulating together all of the nested field
  // names.
  std::string name = class_field.name_and_type.name;
  for (const Field* nested_struct_field : struct_fields) {
    name += "_" + nested_struct_field->name_and_type.name;
  }

  // Generate declarations in header.
  if (can_contain_heap_objects && !field_type->IsClassType() &&
      !field_type->IsStructType() &&
      field_type != TypeOracle::GetObjectType()) {
    hdr_ << "  // Torque type: " << field_type->ToString() << "\n";
  }

  std::vector<cpp::TemplateParameter> templateParameters = {
      cpp::TemplateParameter("D"), cpp::TemplateParameter("P")};
  cpp::Class owner(std::move(templateParameters), gen_name_);

  // getter
  {
    auto getter = cpp::Function::DefaultGetter(type_name, &owner, name);
    if (indexed) {
      getter.AddParameter("int", "i");
    }
    const char* tag_argument;
    switch (class_field.read_synchronization) {
      case FieldSynchronization::kNone:
        tag_argument = "";
        break;
      case FieldSynchronization::kRelaxed:
        getter.AddParameter("RelaxedLoadTag");
        tag_argument = ", kRelaxedLoad";
        break;
      case FieldSynchronization::kAcquireRelease:
        getter.AddParameter("AcquireLoadTag");
        tag_argument = ", kAcquireLoad";
        break;
    }

    getter.PrintDeclaration(hdr_);

    // For tagged data, generate the extra getter that derives an
    // PtrComprCageBase from the current object's pointer.
    if (can_contain_heap_objects) {
      getter.PrintDefinition(inl_, [&](auto& stream) {
        stream
            << "  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);\n";
        stream << "  return " << gen_name_ << "::" << name << "(cage_base"
               << (indexed ? ", i" : "") << tag_argument << ");\n";
      });

      getter.InsertParameter(0, "PtrComprCageBase", "cage_base");
      getter.PrintDeclaration(hdr_);
    }

    getter.PrintDefinition(inl_, [&](auto& stream) {
      EmitLoadFieldStatement(stream, class_field, struct_fields);
      stream << "  return value;\n";
    });
  }

  // setter
  {
    auto setter = cpp::Function::DefaultSetter(
        &owner, std::string("set_") + name, type_name, "value");
    if (indexed) {
      setter.InsertParameter(0, "int", "i");
    }
    switch (class_field.write_synchronization) {
      case FieldSynchronization::kNone:
        break;
      case FieldSynchronization::kRelaxed:
        setter.AddParameter("RelaxedStoreTag");
        break;
      case FieldSynchronization::kAcquireRelease:
        setter.AddParameter("ReleaseStoreTag");
        break;
    }
    if (can_contain_heap_objects) {
      setter.AddParameter("WriteBarrierMode", "mode", "UPDATE_WRITE_BARRIER");
    }
    setter.PrintDeclaration(hdr_);

    setter.PrintDefinition(inl_, [&](auto& stream) {
      EmitStoreFieldStatement(stream, class_field, struct_fields);
    });
  }

  hdr_ << "\n";
}

std::string CppClassGenerator::GetFieldOffsetForAccessor(const Field& f) {
  if (f.offset.has_value()) {
    return "k" + CamelifyString(f.name_and_type.name) + "Offset";
  }
  return CamelifyString(f.name_and_type.name) + "Offset()";
}

std::string CppClassGenerator::GetTypeNameForAccessor(const Field& f) {
  const Type* field_type = f.name_and_type.type;
  if (!field_type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
    const Type* constexpr_version = field_type->ConstexprVersion();
    if (!constexpr_version) {
      Error("Field accessor for ", type_->name(), ":: ", f.name_and_type.name,
            " cannot be generated because its type ", *field_type,
            " is neither a subclass of Object nor does the type have a "
            "constexpr "
            "version.")
          .Position(f.pos)
          .Throw();
    }
    return constexpr_version->GetGeneratedTypeName();
  }
  return field_type->TagglifiedCppTypeName();
}

bool CppClassGenerator::CanContainHeapObjects(const Type* t) {
  return t->IsSubtypeOf(TypeOracle::GetTaggedType()) &&
         !t->IsSubtypeOf(TypeOracle::GetSmiType());
}

void CppClassGenerator::EmitLoadFieldStatement(
    std::ostream& stream, const Field& class_field,
    std::vector<const Field*>& struct_fields) {
  const Field& innermost_field =
      struct_fields.empty() ? class_field : *struct_fields.back();
  const Type* field_type = innermost_field.name_and_type.type;
  std::string type_name = GetTypeNameForAccessor(innermost_field);
  const std::string class_field_size =
      std::get<1>(class_field.GetFieldSizeInformation());

  // field_offset contains both the offset from the beginning of the object to
  // the class field and the combined offsets of any nested struct fields
  // within, but not the index adjustment.
  std::string field_offset = GetFieldOffsetForAccessor(class_field);
  for (const Field* nested_struct_field : struct_fields) {
    field_offset += " + " + std::to_string(*nested_struct_field->offset);
  }

  std::string offset = field_offset;
  if (class_field.index) {
    const char* index = class_field.index->optional ? "0" : "i";
    GenerateBoundsDCheck(stream, index, type_, class_field);
    stream << "  int offset = " << field_offset << " + " << index << " * "
           << class_field_size << ";\n";
    offset = "offset";
  }

  stream << "  " << type_name << " value = ";

  if (!field_type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
    if (class_field.read_synchronization ==
        FieldSynchronization::kAcquireRelease) {
      ReportError("Torque doesn't support @cppAcquireRead on untagged data");
    } else if (class_field.read_synchronization ==
               FieldSynchronization::kRelaxed) {
      ReportError("Torque doesn't support @cppRelaxedRead on untagged data");
    }
    stream << "this->template ReadField<" << type_name << ">(" << offset
           << ");\n";
  } else {
    const char* load;
    switch (class_field.read_synchronization) {
      case FieldSynchronization::kNone:
        load = "load";
        break;
      case FieldSynchronization::kRelaxed:
        load = "Relaxed_Load";
        break;
      case FieldSynchronization::kAcquireRelease:
        load = "Acquire_Load";
        break;
    }
    bool is_smi = field_type->IsSubtypeOf(TypeOracle::GetSmiType());
    const std::string load_type = is_smi ? "Smi" : type_name;
    const char* postfix = is_smi ? ".value()" : "";
    const char* optional_cage_base = is_smi ? "" : "cage_base, ";

    stream << "TaggedField<" << load_type << ">::" << load << "("
           << optional_cage_base << "*this, " << offset << ")" << postfix
           << ";\n";
  }

  if (CanContainHeapObjects(field_type)) {
    stream << "  DCHECK(" << GenerateRuntimeTypeCheck(field_type, "value")
           << ");\n";
  }
}

void CppClassGenerator::EmitStoreFieldStatement(
    std::ostream& stream, const Field& class_field,
    std::vector<const Field*>& struct_fields) {
  const Field& innermost_field =
      struct_fields.empty() ? class_field : *struct_fields.back();
  const Type* field_type = innermost_field.name_and_type.type;
  std::string type_name = GetTypeNameForAccessor(innermost_field);
  const std::string class_field_size =
      std::get<1>(class_field.GetFieldSizeInformation());

  // field_offset contains both the offset from the beginning of the object to
  // the class field and the combined offsets of any nested struct fields
  // within, but not the index adjustment.
  std::string field_offset = GetFieldOffsetForAccessor(class_field);
  for (const Field* nested_struct_field : struct_fields) {
    field_offset += " + " + std::to_string(*nested_struct_field->offset);
  }

  std::string offset = field_offset;
  if (class_field.index) {
    const char* index = class_field.index->optional ? "0" : "i";
    GenerateBoundsDCheck(stream, index, type_, class_field);
    stream << "  int offset = " << field_offset << " + " << index << " * "
           << class_field_size << ";\n";
    offset = "offset";
  }

  if (!field_type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
    stream << "  this->template WriteField<" << type_name << ">(" << offset
           << ", value);\n";
  } else {
    bool strong_pointer = field_type->IsSubtypeOf(TypeOracle::GetObjectType());
    bool is_smi = field_type->IsSubtypeOf(TypeOracle::GetSmiType());
    const char* write_macro;
    if (!strong_pointer) {
      if (class_field.write_synchronization ==
          FieldSynchronization::kAcquireRelease) {
        ReportError("Torque doesn't support @releaseWrite on weak fields");
      }
      write_macro = "RELAXED_WRITE_WEAK_FIELD";
    } else {
      switch (class_field.write_synchronization) {
        case FieldSynchronization::kNone:
          write_macro = "WRITE_FIELD";
          break;
        case FieldSynchronization::kRelaxed:
          write_macro = "RELAXED_WRITE_FIELD";
          break;
        case FieldSynchronization::kAcquireRelease:
          write_macro = "RELEASE_WRITE_FIELD";
          break;
      }
    }
    const std::string value_to_write = is_smi ? "Smi::FromInt(value)" : "value";

    if (!is_smi) {
      stream << "  SLOW_DCHECK("
             << GenerateRuntimeTypeCheck(field_type, "value") << ");\n";
    }
    stream << "  " << write_macro << "(*this, " << offset << ", "
           << value_to_write << ");\n";
    if (!is_smi) {
      const char* write_barrier = strong_pointer
                                      ? "CONDITIONAL_WRITE_BARRIER"
                                      : "CONDITIONAL_WEAK_WRITE_BARRIER";
      stream << "  " << write_barrier << "(*this, " << offset
             << ", value, mode);\n";
    }
  }
}

void GenerateStructLayoutDescription(std::ostream& header,
                                     const StructType* type) {
  header << "struct TorqueGenerated" << CamelifyString(type->name())
         << "Offsets {\n";
  for (const Field& field : type->fields()) {
    header << "  static constexpr int k"
           << CamelifyString(field.name_and_type.name)
           << "Offset = " << *field.offset << ";\n";
  }
  header << "  static constexpr int kSize = " << type->PackedSize() << ";\n";
  header << "};\n\n";
}

}  // namespace

void ImplementationVisitor::GenerateClassDefinitions(
    const std::string& output_directory) {
  std::stringstream factory_header;
  std::stringstream factory_impl;
  std::string factory_basename = "factory";

  std::stringstream forward_declarations;
  std::string forward_declarations_filename = "class-forward-declarations.h";

  {
    factory_impl << "#include \"src/heap/factory-base.h\"\n";
    factory_impl << "#include \"src/heap/factory-base-inl.h\"\n";
    factory_impl << "#include \"src/heap/heap.h\"\n";
    factory_impl << "#include \"src/heap/heap-inl.h\"\n";
    factory_impl << "#include \"src/execution/isolate.h\"\n";
    factory_impl << "#include "
                    "\"src/objects/all-objects-inl.h\"\n\n";
    NamespaceScope factory_impl_namespaces(factory_impl, {"v8", "internal"});
    factory_impl << "\n";

    IncludeGuardScope include_guard(forward_declarations,
                                    forward_declarations_filename);
    NamespaceScope forward_declarations_namespaces(forward_declarations,
                                                   {"v8", "internal"});

    std::set<const StructType*, TypeLess> structs_used_in_classes;

    // Emit forward declarations.
    for (const ClassType* type : TypeOracle::GetClasses()) {
      CurrentSourcePosition::Scope position_activator(type->GetPosition());
      auto& streams = GlobalContext::GeneratedPerFile(type->AttributedToFile());
      std::ostream& header = streams.class_definition_headerfile;
      std::string name = type->ShouldGenerateCppClassDefinitions()
                             ? type->name()
                             : type->GetGeneratedTNodeTypeName();
      if (type->ShouldGenerateCppClassDefinitions()) {
        header << "class " << name << ";\n";
      }
      forward_declarations << "class " << name << ";\n";
    }

    for (const ClassType* type : TypeOracle::GetClasses()) {
      CurrentSourcePosition::Scope position_activator(type->GetPosition());
      auto& streams = GlobalContext::GeneratedPerFile(type->AttributedToFile());
      std::ostream& header = streams.class_definition_headerfile;
      std::ostream& inline_header = streams.class_definition_inline_headerfile;
      std::ostream& implementation = streams.class_definition_ccfile;

      if (type->ShouldGenerateCppClassDefinitions()) {
        CppClassGenerator g(type, header, inline_header, implementation);
        g.GenerateClass();
      } else if (type->ShouldGenerateCppObjectDefinitionAsserts()) {
        CppClassGenerator g(type, header, inline_header, implementation);
        g.GenerateCppObjectDefinitionAsserts();
      } else if (type->ShouldGenerateCppObjectLayoutDefinitionAsserts()) {
        CppClassGenerator g(type, header, inline_header, implementation);
        g.GenerateCppObjectLayoutDefinitionAsserts();
      }
      for (const Field& f : type->fields()) {
        const Type* field_type = f.name_and_type.type;
        if (auto field_as_struct = field_type->StructSupertype()) {
          structs_used_in_classes.insert(*field_as_struct);
        }
      }
      if (type->ShouldGenerateFactoryFunction()) {
        std::string return_type =
            type->HandlifiedCppTypeName(Type::HandleKind::kIndirect);
        std::string function_name = "New" + type->name();
        std::stringstream parameters;
        for (const Field& f : type->ComputeAllFields()) {
          if (f.name_and_type.name == "map") continue;
          if (f.name_and_type.name == "self_indirect_pointer") continue;
          if (!f.index) {
            std::string type_string =
                f.name_and_type.type->HandlifiedCppTypeName(
                    Type::HandleKind::kDirect);
            parameters << type_string << " " << f.name_and_type.name << ", ";
          }
        }
        parameters << "AllocationType allocation_type";

        factory_header << return_type << " " << function_name << "("
                       << parameters.str() << ");\n";
        factory_impl << "template <typename Impl>\n";
        factory_impl << return_type
                     << " TorqueGeneratedFactory<Impl>::" << function_name
                     << "(" << parameters.str() << ") {\n";

        factory_impl << "  int size = ";
        const ClassType* super = type->GetSuperClass();
        std::string gen_name = "TorqueGenerated" + type->name();
        std::string gen_name_T =
            gen_name + "<" + type->name() + ", " + super->name() + ">";
        factory_impl << gen_name_T << "::SizeFor(";

        bool first = true;
        auto index_fields = GetOrderedUniqueIndexFields(*type);
        CHECK(index_fields.has_value());
        for (const auto& index_field : *index_fields) {
          if (!first) {
            factory_impl << ", ";
          }
          factory_impl << index_field.name_and_type.name;
          first = false;
        }

        factory_impl << ");\n";
        factory_impl << "  Tagged<Map> map = factory()->read_only_roots()."
                     << SnakeifyString(type->name()) << "_map();\n";
        factory_impl << "  Tagged<HeapObject> raw_object =\n";
        factory_impl << "    factory()->AllocateRawWithImmortalMap(size, "
                        "allocation_type, map);\n";
        factory_impl << "  " << type->TagglifiedCppTypeName()
                     << " result = Cast<"
                     << type->GetConstexprGeneratedTypeName()
                     << ">(raw_object);\n";
        factory_impl << "  DisallowGarbageCollection no_gc;\n";
        factory_impl << "  WriteBarrierMode write_barrier_mode =\n"
                     << "     allocation_type == AllocationType::kYoung\n"
                     << "     ? SKIP_WRITE_BARRIER : UPDATE_WRITE_BARRIER;\n"
                     << "  USE(write_barrier_mode);\n";

        for (const Field& f : type->ComputeAllFields()) {
          if (f.name_and_type.name == "map") continue;
          if (f.name_and_type.name == "self_indirect_pointer") {
            factory_impl << "  "
                            "result->init_self_indirect_pointer(factory()->"
                            "isolate());\n";
          } else if (!f.index) {
            factory_impl << "  result->TorqueGeneratedClass::set_"
                         << SnakeifyString(f.name_and_type.name) << "(";
            if (f.name_and_type.type->IsSubtypeOf(
                    TypeOracle::GetTaggedType()) &&
                !f.name_and_type.type->IsSubtypeOf(TypeOracle::GetSmiType())) {
              factory_impl << "*" << f.name_and_type.name
                           << ", write_barrier_mode";
            } else {
              factory_impl << f.name_and_type.name;
            }
            factory_impl << ");\n";
          }
        }

        factory_impl << "  return handle(result, factory()->isolate());\n";
        factory_impl << "}\n\n";

        factory_impl << "template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) "
                     << return_type
                     << " TorqueGeneratedFactory<Factory>::" << function_name
                     << "(" << parameters.str() << ");\n";
        factory_impl << "template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) "
                     << return_type << " TorqueGeneratedFactory<LocalFactory>::"
                     << function_name << "(" << parameters.str() << ");\n";

        factory_impl << "\n\n";
      }
    }

    for (const StructType* type : structs_used_in_classes) {
      CurrentSourcePosition::Scope position_activator(type->GetPosition());
      std::ostream& header =
          GlobalContext::GeneratedPerFile(type->GetPosition().source)
              .class_definition_headerfile;
      if (type != TypeOracle::GetFloat64OrHoleType()) {
        GenerateStructLayoutDescription(header, type);
      }
    }
  }
  WriteFile(output_directory + "/" + factory_basename + ".inc",
            factory_header.str());
  WriteFile(output_directory + "/" + factory_basename + ".cc",
            factory_impl.str());
  WriteFile(output_directory + "/" + forward_declarations_filename,
            forward_declarations.str());
}

namespace {
void GeneratePrintDefinitionsForClass(std::ostream& impl, const ClassType* type,
                                      const std::string& gen_name,
                                      const std::string& gen_name_T,
                                      const std::string template_params) {
  impl << template_params << "\n";
  impl << "void " << gen_name_T << "::" << type->name()
       << "Print(std::ostream& os) {\n";
  impl << "  this->PrintHeader(os, \"" << type->name() << "\");\n";
  auto hierarchy = type->GetHierarchy();
  std::map<std::string, const AggregateType*> field_names;
  for (const AggregateType* aggregate_type : hierarchy) {
    for (const Field& f : aggregate_type->fields()) {
      if (f.name_and_type.name == "map" || f.index.has_value() ||
          !CanGenerateFieldAccessors(f.name_and_type.type)) {
        continue;
      }
      std::string getter = f.name_and_type.name;
      if (aggregate_type != type) {
        // We must call getters directly on the class that provided them,
        // because a subclass could have hidden them.
        getter = aggregate_type->name() + "::TorqueGeneratedClass::" + getter;
      }
      if (f.name_and_type.type->IsSubtypeOf(TypeOracle::GetSmiType()) ||
          !f.name_and_type.type->IsSubtypeOf(TypeOracle::GetTaggedType())) {
        impl << "  os << \"\\n - " << f.name_and_type.name << ": \" << ";
        if (f.name_and_type.type->StructSupertype()) {
          // TODO(turbofan): Print struct fields too.
          impl << "\" <struct field printing still unimplemented>\";\n";
        } else {
          impl << "this->" << getter;
          switch (f.read_synchronization) {
            case FieldSynchronization::kNone:
              impl << "();\n";
              break;
            case FieldSynchronization::kRelaxed:
              impl << "(kRelaxedLoad);\n";
              break;
            case FieldSynchronization::kAcquireRelease:
              impl << "(kAcquireLoad);\n";
              break;
          }
        }
      } else {
        impl << "  os << \"\\n - " << f.name_and_type.name << ": \" << "
             << "Brief(this->" << getter;
        switch (f.read_synchronization) {
          case FieldSynchronization::kNone:
            impl << "());\n";
            break;
          case FieldSynchronization::kRelaxed:
            impl << "(kRelaxedLoad));\n";
            break;
          case FieldSynchronization::kAcquireRelease:
            impl << "(kAcquireLoad));\n";
            break;
        }
      }
    }
  }
  impl << "  os << '\\n';\n";
  impl << "}\n\n";
}
}  // namespace

void ImplementationVisitor::GeneratePrintDefinitions(
    const std::string& output_directory) {
  std::stringstream impl;
  std::string file_name = "objects-printer.cc";
  {
    IfDefScope object_print(impl, "OBJECT_PRINT");

    impl << "#include <iosfwd>\n\n";
    impl << "#include \"src/objects/all-objects-inl.h\"\n\n";

    NamespaceScope impl_namespaces(impl, {"v8", "internal"});

    for (const ClassType* type : TypeOracle::GetClasses()) {
      if (!type->ShouldGeneratePrint()) continue;
      DCHECK(type->ShouldGenerateCppClassDefinitions());
      const ClassType* super = type->GetSuperClass();
      std::string gen_name = "TorqueGenerated" + type->name();
      std::string gen_name_T =
          gen_name + "<" + type->name() + ", " + super->name() + ">";
      std::string template_decl = "template <>";
      GeneratePrintDefinitionsForClass(impl, type, gen_name, gen_name_T,
                                       template_decl);
    }
  }

  std::string new_contents(impl.str());
  WriteFile(output_directory + "/" + file_name, new_contents);
}

std::optional<std::string> MatchSimpleBodyDescriptor(const ClassType* type) {
  std::vector<ObjectSlotKind> slots = type->ComputeHeaderSlotKinds();
  if (!type->HasStaticSize()) {
    slots.push_back(*type->ComputeArraySlotKind());
  }

  // Skip the map slot.
  size_t i = 1;
  while (i < slots.size() && slots[i] == ObjectSlotKind::kNoPointer) ++i;
  if (i == slots.size()) return "DataOnlyBodyDescriptor";
  bool has_weak_pointers = false;
  size_t start_index = i;
  for (; i < slots.size(); ++i) {
    if (slots[i] == ObjectSlotKind::kStrongPointer) {
      continue;
    } else if (slots[i] == ObjectSlotKind::kMaybeObjectPointer) {
      has_weak_pointers = true;
    } else if (slots[i] == ObjectSlotKind::kNoPointer) {
      break;
    } else {
      return std::nullopt;
    }
  }
  size_t end_index = i;
  for (; i < slots.size(); ++i) {
    if (slots[i] != ObjectSlotKind::kNoPointer) return std::nullopt;
  }
  size_t start_offset = start_index * TargetArchitecture::TaggedSize();
  size_t end_offset = end_index * TargetArchitecture::TaggedSize();
  // We pick a suffix-range body descriptor even in cases where the object size
  // is fixed, to reduce the amount of code executed for object visitation.
  if (end_index == slots.size()) {
    return ToString("SuffixRange", has_weak_pointers ? "Weak" : "",
                    "BodyDescriptor<", start_offset, ">");
  }
  if (!has_weak_pointers) {
    return ToString("FixedRangeBodyDescriptor<", start_offset, ", ", end_offset,
                    ">");
  }
  return std::nullopt;
}

void ImplementationVisitor::GenerateBodyDescriptors(
    const std::string& output_directory) {
  std::string file_name = "objects-body-descriptors-inl.inc";
  std::stringstream h_contents;

    for (const ClassType* type : TypeOracle::GetClasses()) {
      std::string name = type->name();
      if (!type->ShouldGenerateBodyDescriptor()) continue;

      bool has_array_fields = !type->HasStaticSize();
      std::vector<ObjectSlotKind> header_slot_kinds =
          type->ComputeHeaderSlotKinds();
      std::optional<ObjectSlotKind> array_slot_kind =
          type->ComputeArraySlotKind();
      DCHECK_EQ(has_array_fields, array_slot_kind.has_value());

      h_contents << "class " << name << "::BodyDescriptor final : public ";
      if (auto descriptor_name = MatchSimpleBodyDescriptor(type)) {
        h_contents << *descriptor_name << " {\n";
        h_contents << " public:\n";
      } else {
        h_contents << "BodyDescriptorBase {\n";
        h_contents << " public:\n";

        h_contents << "  template <typename ObjectVisitor>\n";
        h_contents
            << "  static inline void IterateBody(Tagged<Map> map, "
               "Tagged<HeapObject> obj, int object_size, ObjectVisitor* v) {\n";

        std::vector<ObjectSlotKind> slots = std::move(header_slot_kinds);
        if (has_array_fields) slots.push_back(*array_slot_kind);

        // Skip the map slot.
        slots.erase(slots.begin());
        size_t start_offset = TargetArchitecture::TaggedSize();

        size_t end_offset = start_offset;
        ObjectSlotKind section_kind;
        for (size_t i = 0; i <= slots.size(); ++i) {
          std::optional<ObjectSlotKind> next_section_kind;
          bool finished_section = false;
          if (i == 0) {
            next_section_kind = slots[i];
          } else if (i < slots.size()) {
            if (auto combined = Combine(section_kind, slots[i])) {
              next_section_kind = *combined;
            } else {
              next_section_kind = slots[i];
              finished_section = true;
            }
          } else {
            finished_section = true;
          }
          if (finished_section) {
            bool is_array_slot = i == slots.size() && has_array_fields;
            bool multiple_slots =
                is_array_slot ||
                (end_offset - start_offset > TargetArchitecture::TaggedSize());
            std::optional<std::string> iterate_command;
            switch (section_kind) {
              case ObjectSlotKind::kStrongPointer:
                iterate_command = "IteratePointer";
                break;
              case ObjectSlotKind::kMaybeObjectPointer:
                iterate_command = "IterateMaybeWeakPointer";
                break;
              case ObjectSlotKind::kCustomWeakPointer:
                iterate_command = "IterateCustomWeakPointer";
                break;
              case ObjectSlotKind::kNoPointer:
                break;
            }
            if (iterate_command) {
              if (multiple_slots) *iterate_command += "s";
              h_contents << "    " << *iterate_command << "(obj, "
                         << start_offset;
              if (multiple_slots) {
                h_contents << ", "
                           << (i == slots.size() ? "object_size"
                                                 : std::to_string(end_offset));
              }
              h_contents << ", v);\n";
            }
            start_offset = end_offset;
          }
          if (i < slots.size()) section_kind = *next_section_kind;
          end_offset += TargetArchitecture::TaggedSize();
        }

        h_contents << "  }\n\n";
      }

      h_contents << "  static inline int SizeOf(Tagged<Map> map, "
                    "Tagged<HeapObject> raw_object) {\n";
      if (type->size().SingleValue()) {
        h_contents << "    return " << *type->size().SingleValue() << ";\n";
      } else {
        // We use an UncheckedCast here because this is used for concurrent
        // marking, where we shouldn't re-read the map.
        h_contents << "    return UncheckedCast<" << name
                   << ">(raw_object)->AllocatedSize();\n";
      }
      h_contents << "  }\n\n";

      h_contents << "};\n";
    }

    WriteFile(output_directory + "/" + file_name, h_contents.str());
}

namespace {

// Generate verification code for a single piece of class data, which might be
// nested within a struct or might be a single element in an indexed field (or
// both).
void GenerateFieldValueVerifier(const std::string& class_name, bool indexed,
                                std::string offset, const Field& leaf_field,
                                std::string indexed_field_size,
                                std::ostream& cc_contents, bool is_map) {
  const Type* field_type = leaf_field.name_and_type.type;

  bool maybe_object =
      !field_type->IsSubtypeOf(TypeOracle::GetStrongTaggedType());
  const char* object_type = maybe_object ? "MaybeObject" : "Object";
  const char* tagged_object_type =
      maybe_object ? "Tagged<MaybeObject>" : "Tagged<Object>";
  const char* verify_fn =
      maybe_object ? "VerifyMaybeObjectPointer" : "VerifyPointer";
  if (indexed) {
    offset += " + i * " + indexed_field_size;
  }
  // Name the local var based on the field name for nicer CHECK output.
  const std::string value = leaf_field.name_and_type.name + "__value";

  // Read the field.
  if (is_map) {
    cc_contents << "    " << tagged_object_type << " " << value
                << " = o->map();\n";
  } else {
    cc_contents << "    " << tagged_object_type << " " << value
                << " = TaggedField<" << object_type << ">::load(o, " << offset
                << ");\n";
  }

  // Call VerifyPointer or VerifyMaybeObjectPointer on it.
  cc_contents << "    Object::" << verify_fn << "(isolate, " << value << ");\n";

  // Check that the value is of an appropriate type. We can skip this part for
  // the Object type because it would not check anything beyond what we already
  // checked with VerifyPointer.
  if (field_type != TypeOracle::GetObjectType()) {
    cc_contents << "    CHECK(" << GenerateRuntimeTypeCheck(field_type, value)
                << ");\n";
  }
}

void GenerateClassFieldVerifier(const std::string& class_name,
                                const ClassType& class_type, const Field& f,
                                std::ostream& h_contents,
                                std::ostream& cc_contents) {
  const Type* field_type = f.name_and_type.type;

  // We only verify tagged types, not raw numbers or pointers. Structs
  // consisting of tagged types are also included.
  if (!field_type->IsSubtypeOf(TypeOracle::GetTaggedType()) &&
      !field_type->StructSupertype())
    return;
  // Protected pointer fields cannot be read or verified from torque yet.
  if (field_type->IsSubtypeOf(TypeOracle::GetProtectedPointerType())) return;
  if (field_type == TypeOracle::GetFloat64OrHoleType()) return;
  // Do not verify if the field may be uninitialized.
  if (TypeOracle::GetUninitializedType()->IsSubtypeOf(field_type)) return;

  std::string field_start_offset;
  if (f.index) {
    field_start_offset = f.name_and_type.name + "__offset";
    std::string length = f.name_and_type.name + "__length";
    cc_contents << "  intptr_t " << field_start_offset << ", " << length
                << ";\n";
    cc_contents << "  std::tie(std::ignore, " << field_start_offset << ", "
                << length << ") = "
                << Callable::PrefixNameForCCOutput(
                       class_type.GetSliceMacroName(f))
                << "(o);\n";

    // Slices use intptr, but TaggedField<T>.load() uses int, so verify that
    // such a cast is valid.
    cc_contents << "  CHECK_EQ(" << field_start_offset << ", static_cast<int>("
                << field_start_offset << "));\n";
    cc_contents << "  CHECK_EQ(" << length << ", static_cast<int>(" << length
                << "));\n";
    field_start_offset = "static_cast<int>(" + field_start_offset + ")";
    length = "static_cast<int>(" + length + ")";

    cc_contents << "  for (int i = 0; i < " << length << "; ++i) {\n";
  } else {
    // Non-indexed fields have known offsets.
    field_start_offset = std::to_string(*f.offset);
    cc_contents << "  {\n";
  }

  if (auto struct_type = field_type->StructSupertype()) {
    for (const Field& struct_field : (*struct_type)->fields()) {
      if (struct_field.name_and_type.type->IsSubtypeOf(
              TypeOracle::GetTaggedType())) {
        GenerateFieldValueVerifier(
            class_name, f.index.has_value(),
            field_start_offset + " + " + std::to_string(*struct_field.offset),
            struct_field, std::to_string((*struct_type)->PackedSize()),
            cc_contents, f.name_and_type.name == "map");
      }
    }
  } else {
    GenerateFieldValueVerifier(class_name, f.index.has_value(),
                               field_start_offset, f, "kTaggedSize",
                               cc_contents, f.name_and_type.name == "map");
  }

  cc_contents << "  }\n";
}

}  // namespace

void ImplementationVisitor::GenerateClassVerifiers(
    const std::string& output_directory) {
  std::string file_name = "class-verifiers";
  std::stringstream h_contents;
  std::stringstream cc_contents;
  {
    IncludeGuardScope include_guard(h_contents, file_name + ".h");
    IfDefScope verify_heap_h(h_contents, "VERIFY_HEAP");
    IfDefScope verify_heap_cc(cc_contents, "VERIFY_HEAP");

    h_contents << "#include \"src/base/macros.h\"\n\n";

    cc_contents << "#include \"torque-generated/" << file_name << ".h\"\n\n";
    cc_contents << "#include \"src/objects/all-objects-inl.h\"\n";

    IncludeObjectMacrosScope object_macros(cc_contents);

    NamespaceScope h_namespaces(h_contents, {"v8", "internal"});
    NamespaceScope cc_namespaces(cc_contents, {"v8", "internal"});

    cc_contents
        << "#include \"torque-generated/test/torque/test-torque-tq-inl.inc\"\n";

    // Generate forward declarations to avoid including any headers.
    h_contents << "class Isolate;\n";
    h_contents << "template<typename T>\nclass Tagged;\n";
    for (const ClassType* type : TypeOracle::GetClasses()) {
      if (!type->ShouldGenerateVerify()) continue;
      h_contents << "class " << type->name() << ";\n";
    }

    const char* verifier_class = "TorqueGeneratedClassVerifiers";

    h_contents << "class V8_EXPORT_PRIVATE " << verifier_class << "{\n";
    h_contents << " public:\n";

    for (const ClassType* type : TypeOracle::GetClasses()) {
      std::string name = type->name();
      std::string cpp_name = type->TagglifiedCppTypeName();
      if (!type->ShouldGenerateVerify()) continue;

      std::string method_name = name + "Verify";

      h_contents << "  static void " << method_name << "(" << cpp_name
                 << " o, Isolate* isolate);\n";

      cc_contents << "void " << verifier_class << "::" << method_name << "("
                  << cpp_name << " o, Isolate* isolate) {\n";

      // First, do any verification for the super class. Not all classes have
      // verifiers, so skip to the nearest super class that has one.
      const ClassType* super_type = type->GetSuperClass();
      while (super_type && !super_type->ShouldGenerateVerify()) {
        super_type = super_type->GetSuperClass();
      }
      if (super_type) {
        std::string super_name = super_type->name();
        cc_contents << "  o->" << super_name << "Verify(isolate);\n";
      }

      // Second, verify that this object is what it claims to be.
      cc_contents << "  CHECK(Is" << name << "(o, isolate));\n";

      // Third, verify its properties.
      for (const auto& f : type->fields()) {
        GenerateClassFieldVerifier(name, *type, f, h_contents, cc_contents);
      }

      cc_contents << "}\n";
    }

    h_contents << "};\n";
  }
  WriteFile(output_directory + "/" + file_name + ".h", h_contents.str());
  WriteFile(output_directory + "/" + file_name + ".cc", cc_contents.str());
}

void ImplementationVisitor::GenerateEnumVerifiers(
    const std::string& output_directory) {
  std::string file_name = "enum-verifiers";
  std::stringstream cc_contents;
  {
    cc_contents << "#include \"src/compiler/code-assembler.h\"\n";
    for (const std::string& include_path : GlobalContext::CppIncludes()) {
      cc_contents << "#include " << StringLiteralQuote(include_path) << "\n";
    }
    cc_contents << "\n";

    NamespaceScope cc_namespaces(cc_contents, {"v8", "internal", ""});

    cc_contents << "class EnumVerifier {\n";
    for (const auto& desc : GlobalContext::Get().ast()->EnumDescriptions()) {
      std::stringstream alias_checks;
      cc_contents << "  // " << desc.name << " (" << desc.pos << ")\n";
      cc_contents << "  void VerifyEnum_" << desc.name << "("
                  << desc.constexpr_generates
                  << " x) {\n"
                     "    switch(x) {\n";
      for (const auto& entry : desc.entries) {
        if (entry.alias_entry.empty()) {
          cc_contents << "      case " << entry.name << ": break;\n";
        } else {
          // We don't add a case for this, because it aliases another entry, so
          // we would have two cases for the same value.
          alias_checks << "    static_assert(" << entry.name
                       << " == " << entry.alias_entry << ");\n";
        }
      }
      if (desc.is_open) cc_contents << "      default: break;\n";
      cc_contents << "    }\n";
      cc_contents << alias_checks.str();
      cc_contents << "  }\n\n";
    }
    cc_contents << "};\n";
  }

  WriteFile(output_directory + "/" + file_name + ".cc", cc_contents.str());
}

void ImplementationVisitor::GenerateExportedMacrosAssembler(
    const std::string& output_directory) {
  std::string file_name = "exported-macros-assembler";
  std::stringstream h_contents;
  std::stringstream cc_contents;
  {
    IncludeGuardScope include_guard(h_contents, file_name + ".h");

    h_contents << "#include \"src/compiler/code-assembler.h\"\n";
    h_contents << "#include \"src/execution/frames.h\"\n";
    h_contents << "#include \"torque-generated/csa-types.h\"\n";

    for (const std::string& include_path : GlobalContext::CppIncludes()) {
      cc_contents << "#include " << StringLiteralQuote(include_path) << "\n";
    }
    cc_contents << "#include \"torque-generated/" << file_name << ".h\"\n";

    for (SourceId file : SourceFileMap::AllSources()) {
      cc_contents << "#include \"torque-generated/" +
                         SourceFileMap::PathFromV8RootWithoutExtension(file) +
                         "-tq-csa.h\"\n";
    }

    NamespaceScope h_namespaces(h_contents, {"v8", "internal"});
    NamespaceScope cc_namespaces(cc_contents, {"v8", "internal"});

    h_contents << "class V8_EXPORT_PRIVATE "
                  "TorqueGeneratedExportedMacrosAssembler {\n"
               << " public:\n"
               << "  explicit TorqueGeneratedExportedMacrosAssembler"
                  "(compiler::CodeAssemblerState* state) : state_(state) {\n"
               << "    USE(state_);\n"
               << "  }\n";

    for (auto& declarable : GlobalContext::AllDeclarables()) {
      TorqueMacro* macro = TorqueMacro::DynamicCast(declarable.get());
      if (!(macro && macro->IsExportedToCSA())) continue;
      CurrentSourcePosition::Scope position_activator(macro->Position());

      cpp::Class assembler("TorqueGeneratedExportedMacrosAssembler");
      std::vector<std::string> generated_parameter_names;
      cpp::Function f = GenerateFunction(
          &assembler, macro->ReadableName(), macro->signature(),
          macro->parameter_names(), false, &generated_parameter_names);

      f.PrintDeclaration(h_contents);
      f.PrintDefinition(cc_contents, [&](std::ostream& stream) {
        stream << "return " << macro->ExternalName() << "(state_";
        for (const auto& name : generated_parameter_names) {
          stream << ", " << name;
        }
        stream << ");";
      });
    }

    h_contents << " private:\n"
               << "  compiler::CodeAssemblerState* state_;\n"
               << "};\n";
  }
  WriteFile(output_directory + "/" + file_name + ".h", h_contents.str());
  WriteFile(output_directory + "/" + file_name + ".cc", cc_contents.str());
}

namespace {

void CollectAllFields(const std::string& path, const Field& field,
                      std::vector<std::string>& result) {
  if (field.name_and_type.type->StructSupertype()) {
    std::string next_path = path + field.name_and_type.name + ".";
    const StructType* struct_type =
        StructType::DynamicCast(field.name_and_type.type);
    for (const auto& inner_field : struct_type->fields()) {
      CollectAllFields(next_path, inner_field, result);
    }
  } else {
    result.push_back(path + field.name_and_type.name);
  }
}

}  // namespace

void ImplementationVisitor::GenerateCSATypes(
    const std::string& output_directory) {
  std::string file_name = "csa-types";
  std::stringstream h_contents;
  {
    IncludeGuardScope include_guard(h_contents, file_name + ".h");
    h_contents << "#include \"src/compiler/code-assembler.h\"\n\n";

    NamespaceScope h_namespaces(h_contents, {"v8", "internal"});

    // Generates headers for all structs in a topologically-sorted order, since
    // TypeOracle keeps them in the order of their resolution
    for (const auto& type : TypeOracle::GetAggregateTypes()) {
      const StructType* struct_type = StructType::DynamicCast(type.get());
      if (!struct_type) continue;
      h_contents << "struct " << struct_type->GetGeneratedTypeNameImpl()
                 << " {\n";
      for (auto& field : struct_type->fields()) {
        h_contents << "  " << field.name_and_type.type->GetGeneratedTypeName();
        h_contents << " " << field.name_and_type.name << ";\n";
      }
      h_contents << "\n  std::tuple<";
      bool first = true;
      for (const Type* lowered_type : LowerType(struct_type)) {
        if (!first) {
          h_contents << ", ";
        }
        first = false;
        h_contents << lowered_type->GetGeneratedTypeName();
      }
      std::vector<std::string> all_fields;
      for (auto& field : struct_type->fields()) {
        CollectAllFields("", field, all_fields);
      }
      h_contents << "> Flatten() const {\n"
                    "    return std::make_tuple(";
      PrintCommaSeparatedList(h_contents, all_fields);
      h_contents << ");\n";
      h_contents << "  }\n";
      h_contents << "};\n";
    }
  }
  WriteFile(output_directory + "/" + file_name + ".h", h_contents.str());
}

void ReportAllUnusedMacros() {
  for (const auto& declarable : GlobalContext::AllDeclarables()) {
    if (!declarable->IsMacro() || declarable->IsExternMacro()) continue;

    Macro* macro = Macro::cast(declarable.get());
    if (macro->IsUsed()) continue;

    if (macro->IsTorqueMacro() && TorqueMacro::cast(macro)->IsExportedToCSA()) {
      continue;
    }
    // TODO(gsps): Mark methods of generic structs used if they are used in any
    // instantiation
    if (Method* method = Method::DynamicCast(macro)) {
      if (StructType* struct_type =
              StructType::DynamicCast(method->aggregate_type())) {
        if (struct_type->GetSpecializedFrom().has_value()) {
          continue;
        }
      }
    }

    std::vector<std::string> ignored_prefixes = {"Convert<", "Cast<",
                                                 "FromConstexpr<"};
    const std::string name = macro->ReadableName();
    const bool ignore =
        StartsWithSingleUnderscore(name) ||
        std::any_of(ignored_prefixes.begin(), ignored_prefixes.end(),
                    [&name](const std::string& prefix) {
                      return StringStartsWith(name, prefix);
                    });

    if (!ignore) {
      Lint("Macro '", macro->ReadableName(), "' is never used.")
          .Position(macro->IdentifierPosition());
    }
  }
}

}  // namespace v8::internal::torque
