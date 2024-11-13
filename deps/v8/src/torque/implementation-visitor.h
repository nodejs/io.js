// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_IMPLEMENTATION_VISITOR_H_
#define V8_TORQUE_IMPLEMENTATION_VISITOR_H_

#include <memory>
#include <optional>
#include <string>

#include "src/base/macros.h"
#include "src/torque/ast.h"
#include "src/torque/cfg.h"
#include "src/torque/cpp-builder.h"
#include "src/torque/declarations.h"
#include "src/torque/global-context.h"
#include "src/torque/type-oracle.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

template <typename T>
class Binding;
class LocalValue;
class ImplementationVisitor;

// LocationReference is the representation of an l-value, so a value that might
// allow for assignment. For uniformity, this class can also represent
// unassignable temporaries. Assignable values fall in two categories:
//   - stack ranges that represent mutable variables, including structs.
//   - field or element access expressions that generate operator calls.
class LocationReference {
 public:
  // An assignable stack range.
  static LocationReference VariableAccess(
      VisitResult variable,
      std::optional<Binding<LocalValue>*> binding = std::nullopt) {
    DCHECK(variable.IsOnStack());
    LocationReference result;
    result.variable_ = std::move(variable);
    result.binding_ = binding;
    return result;
  }
  // An unassignable value. {description} is only used for error messages.
  static LocationReference Temporary(VisitResult temporary,
                                     std::string description) {
    LocationReference result;
    result.temporary_ = std::move(temporary);
    result.temporary_description_ = std::move(description);
    return result;
  }
  // A heap reference, that is, a tagged value and an offset to encode an inner
  // pointer.
  static LocationReference HeapReference(VisitResult heap_reference) {
    LocationReference result;
    DCHECK(TypeOracle::MatchReferenceGeneric(heap_reference.type()));
    result.heap_reference_ = std::move(heap_reference);
    return result;
  }
  // A reference to an array on the heap. That is, a tagged value, an offset to
  // encode an inner pointer, and the number of elements.
  static LocationReference HeapSlice(VisitResult heap_slice) {
    LocationReference result;
    DCHECK(Type::MatchUnaryGeneric(heap_slice.type(),
                                   TypeOracle::GetConstSliceGeneric()) ||
           Type::MatchUnaryGeneric(heap_slice.type(),
                                   TypeOracle::GetMutableSliceGeneric()));
    result.heap_slice_ = std::move(heap_slice);
    return result;
  }
  static LocationReference ArrayAccess(VisitResult base, VisitResult offset) {
    LocationReference result;
    result.eval_function_ = std::string{"[]"};
    result.assign_function_ = std::string{"[]="};
    result.call_arguments_ = {base, offset};
    return result;
  }
  static LocationReference FieldAccess(VisitResult object,
                                       std::string fieldname) {
    LocationReference result;
    result.eval_function_ = "." + fieldname;
    result.assign_function_ = "." + fieldname + "=";
    result.call_arguments_ = {object};
    return result;
  }
  static LocationReference BitFieldAccess(const LocationReference& object,
                                          BitField field) {
    LocationReference result;
    result.bit_field_struct_ = std::make_shared<LocationReference>(object);
    result.bit_field_ = std::move(field);
    return result;
  }

  bool IsConst() const {
    if (IsHeapReference()) {
      bool is_const;
      bool success =
          TypeOracle::MatchReferenceGeneric(heap_reference().type(), &is_const)
              .has_value();
      CHECK(success);
      return is_const;
    }
    return IsTemporary();
  }

  bool IsVariableAccess() const { return variable_.has_value(); }
  const VisitResult& variable() const {
    DCHECK(IsVariableAccess());
    return *variable_;
  }
  bool IsTemporary() const { return temporary_.has_value(); }
  const VisitResult& temporary() const {
    DCHECK(IsTemporary());
    return *temporary_;
  }
  bool IsHeapReference() const { return heap_reference_.has_value(); }
  const VisitResult& heap_reference() const {
    DCHECK(IsHeapReference());
    return *heap_reference_;
  }
  bool IsHeapSlice() const { return heap_slice_.has_value(); }
  const VisitResult& heap_slice() const {
    DCHECK(IsHeapSlice());
    return *heap_slice_;
  }
  bool IsBitFieldAccess() const {
    bool is_bitfield_access = bit_field_struct_ != nullptr;
    DCHECK_EQ(is_bitfield_access, bit_field_.has_value());
    return is_bitfield_access;
  }
  const LocationReference& bit_field_struct_location() const {
    DCHECK(IsBitFieldAccess());
    return *bit_field_struct_;
  }
  const BitField& bit_field() const {
    DCHECK(IsBitFieldAccess());
    return *bit_field_;
  }

  std::optional<const Type*> ReferencedType() const {
    if (IsHeapReference()) {
      return *TypeOracle::MatchReferenceGeneric(heap_reference().type());
    }
    if (IsHeapSlice()) {
      if (auto type = Type::MatchUnaryGeneric(
              heap_slice().type(), TypeOracle::GetMutableSliceGeneric())) {
        return *type;
      }
      return Type::MatchUnaryGeneric(heap_slice().type(),
                                     TypeOracle::GetConstSliceGeneric());
    }
    if (IsBitFieldAccess()) {
      return bit_field_->name_and_type.type;
    }
    if (IsVariableAccess() || IsHeapSlice() || IsTemporary()) {
      return GetVisitResult().type();
    }
    return std::nullopt;
  }

  const VisitResult& GetVisitResult() const {
    if (IsVariableAccess()) return variable();
    if (IsHeapSlice()) return heap_slice();
    DCHECK(IsTemporary());
    return temporary();
  }

  // For error reporting.
  const std::string& temporary_description() const {
    DCHECK(IsTemporary());
    return *temporary_description_;
  }

  bool IsCallAccess() const {
    bool is_call_access = eval_function_.has_value();
    DCHECK_EQ(is_call_access, assign_function_.has_value());
    return is_call_access;
  }
  const VisitResultVector& call_arguments() const {
    DCHECK(IsCallAccess());
    return call_arguments_;
  }
  const std::string& eval_function() const {
    DCHECK(IsCallAccess());
    return *eval_function_;
  }
  const std::string& assign_function() const {
    DCHECK(IsCallAccess());
    return *assign_function_;
  }
  std::optional<Binding<LocalValue>*> binding() const {
    DCHECK(IsVariableAccess());
    return binding_;
  }

 private:
  std::optional<VisitResult> variable_;
  std::optional<VisitResult> temporary_;
  std::optional<std::string> temporary_description_;
  std::optional<VisitResult> heap_reference_;
  std::optional<VisitResult> heap_slice_;
  std::optional<std::string> eval_function_;
  std::optional<std::string> assign_function_;
  VisitResultVector call_arguments_;
  std::optional<Binding<LocalValue>*> binding_;

  // The location of the bitfield struct that contains this bitfield, if this
  // reference is a bitfield access. Uses a shared_ptr so that LocationReference
  // is copyable, allowing us to set this field equal to a copy of a
  // stack-allocated LocationReference.
  std::shared_ptr<const LocationReference> bit_field_struct_;
  std::optional<BitField> bit_field_;

  LocationReference() = default;
};

struct InitializerResults {
  std::vector<Identifier*> names;
  std::map<std::string, VisitResult> field_value_map;
};

struct LayoutForInitialization {
  std::map<std::string, VisitResult> array_lengths;
  std::map<std::string, VisitResult> offsets;
  VisitResult size;
};

extern uint64_t next_unique_binding_index;

template <class T>
class Binding;

template <class T>
class BindingsManager {
 public:
  std::optional<Binding<T>*> TryLookup(const std::string& name) {
    if (StartsWithSingleUnderscore(name)) {
      Error("Trying to reference '", name, "' which is marked as unused.")
          .Throw();
    }
    auto binding = current_bindings_[name];
    if (binding) {
      (*binding)->SetUsed();
    }
    return binding;
  }

 private:
  friend class Binding<T>;
  std::unordered_map<std::string, std::optional<Binding<T>*>> current_bindings_;
};

template <class T>
class Binding : public T {
 public:
  template <class... Args>
  Binding(BindingsManager<T>* manager, const std::string& name, Args&&... args)
      : T(std::forward<Args>(args)...),
        manager_(manager),
        name_(name),
        previous_binding_(this),
        used_(false),
        written_(false),
        unique_index_(next_unique_binding_index++) {
    std::swap(previous_binding_, manager_->current_bindings_[name]);
  }
  template <class... Args>
  Binding(BindingsManager<T>* manager, const Identifier* name, Args&&... args)
      : Binding(manager, name->value, std::forward<Args>(args)...) {
    declaration_position_ = name->pos;
  }
  ~Binding() {
    if (!used_ && !SkipLintCheck()) {
      Lint(BindingTypeString(), "'", name_,
           "' is never used. Prefix with '_' if this is intentional.")
          .Position(declaration_position_);
    }

    if (CheckWritten() && !written_ && !SkipLintCheck()) {
      Lint(BindingTypeString(), "'", name_,
           "' is never assigned to. Use 'const' instead of 'let'.")
          .Position(declaration_position_);
    }

    manager_->current_bindings_[name_] = previous_binding_;
  }
  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;

  std::string BindingTypeString() const;
  bool CheckWritten() const;

  const std::string& name() const { return name_; }
  SourcePosition declaration_position() const { return declaration_position_; }

  bool Used() const { return used_; }
  void SetUsed() { used_ = true; }

  bool Written() const { return written_; }
  void SetWritten() { written_ = true; }

  uint64_t unique_index() const { return unique_index_; }

 private:
  bool SkipLintCheck() const { return name_.length() > 0 && name_[0] == '_'; }

  BindingsManager<T>* manager_;
  const std::string name_;
  std::optional<Binding*> previous_binding_;
  SourcePosition declaration_position_ = CurrentSourcePosition::Get();
  bool used_;
  bool written_;
  uint64_t unique_index_;
};

template <class T>
class BlockBindings {
 public:
  explicit BlockBindings(BindingsManager<T>* manager) : manager_(manager) {}
  Binding<T>* Add(std::string name, T value, bool mark_as_used = false) {
    ReportErrorIfAlreadyBound(name);
    auto binding =
        std::make_unique<Binding<T>>(manager_, name, std::move(value));
    Binding<T>* result = binding.get();
    if (mark_as_used) binding->SetUsed();
    bindings_.push_back(std::move(binding));
    return result;
  }

  Binding<T>* Add(const Identifier* name, T value, bool mark_as_used = false) {
    ReportErrorIfAlreadyBound(name->value);
    auto binding =
        std::make_unique<Binding<T>>(manager_, name, std::move(value));
    Binding<T>* result = binding.get();
    if (mark_as_used) binding->SetUsed();
    bindings_.push_back(std::move(binding));
    return result;
  }

  std::vector<Binding<T>*> bindings() const {
    std::vector<Binding<T>*> result;
    result.reserve(bindings_.size());
    for (auto& b : bindings_) {
      result.push_back(b.get());
    }
    return result;
  }

 private:
  void ReportErrorIfAlreadyBound(const std::string& name) {
    for (const auto& binding : bindings_) {
      if (binding->name() == name) {
        ReportError(
            "redeclaration of name \"", name,
            "\" in the same block is illegal, previous declaration at: ",
            binding->declaration_position());
      }
    }
  }

  BindingsManager<T>* manager_;
  std::vector<std::unique_ptr<Binding<T>>> bindings_;
};

class LocalValue {
 public:
  explicit LocalValue(LocationReference reference)
      : value(std::move(reference)) {}
  explicit LocalValue(std::string inaccessible_explanation)
      : inaccessible_explanation(std::move(inaccessible_explanation)) {}
  explicit LocalValue(std::function<LocationReference()> lazy)
      : lazy(std::move(lazy)) {}

  LocationReference GetLocationReference(Binding<LocalValue>* binding) {
    if (value) {
      const LocationReference& ref = *value;
      if (ref.IsVariableAccess()) {
        // Attach the binding to enable the never-assigned-to lint check.
        return LocationReference::VariableAccess(ref.GetVisitResult(), binding);
      }
      return ref;
    } else if (lazy) {
      return (*lazy)();
    } else {
      Error("Cannot access ", binding->name(), ": ", inaccessible_explanation)
          .Throw();
    }
  }

  bool IsAccessibleNonLazy() const { return value.has_value(); }

 private:
  std::optional<LocationReference> value;
  std::optional<std::function<LocationReference()>> lazy;
  std::string inaccessible_explanation;
};

struct LocalLabel {
  Block* block;
  std::vector<const Type*> parameter_types;

  explicit LocalLabel(Block* block,
                      std::vector<const Type*> parameter_types = {})
      : block(block), parameter_types(std::move(parameter_types)) {}
};

template <>
inline std::string Binding<LocalValue>::BindingTypeString() const {
  return "Variable ";
}
template <>
inline bool Binding<LocalValue>::CheckWritten() const {
  // Do the check only for non-const variables and non struct types.
  auto binding = *manager_->current_bindings_[name_];
  if (!binding->IsAccessibleNonLazy()) return false;
  const LocationReference& ref = binding->GetLocationReference(binding);
  if (!ref.IsVariableAccess()) return false;
  return !ref.GetVisitResult().type()->StructSupertype();
}
template <>
inline std::string Binding<LocalLabel>::BindingTypeString() const {
  return "Label ";
}
template <>
inline bool Binding<LocalLabel>::CheckWritten() const {
  return false;
}

struct Arguments {
  VisitResultVector parameters;
  std::vector<Binding<LocalLabel>*> labels;
};

// Determine if a callable should be considered as an overload.
bool IsCompatibleSignature(const Signature& sig, const TypeVector& types,
                           size_t label_count);

class ImplementationVisitor {
 public:
  void GenerateBuiltinDefinitionsAndInterfaceDescriptors(
      const std::string& output_directory);
  void GenerateVisitorLists(const std::string& output_directory);
  void GenerateBitFields(const std::string& output_directory);
  void GeneratePrintDefinitions(const std::string& output_directory);
  void GenerateClassDefinitions(const std::string& output_directory);
  void GenerateBodyDescriptors(const std::string& output_directory);
  void GenerateInstanceTypes(const std::string& output_directory);
  void GenerateClassVerifiers(const std::string& output_directory);
  void GenerateEnumVerifiers(const std::string& output_directory);
  void GenerateClassDebugReaders(const std::string& output_directory);
  void GenerateExportedMacrosAssembler(const std::string& output_directory);
  void GenerateCSATypes(const std::string& output_directory);

  VisitResult Visit(Expression* expr);
  const Type* Visit(Statement* stmt);

  template <typename T>
  void CheckInitializersWellformed(
      const std::string& aggregate_name, const std::vector<T>& aggregate_fields,
      const std::vector<NameAndExpression>& initializers,
      bool ignore_first_field = false) {
    size_t fields_offset = ignore_first_field ? 1 : 0;
    size_t fields_size = aggregate_fields.size() - fields_offset;
    for (size_t i = 0; i < std::min(fields_size, initializers.size()); i++) {
      const std::string& field_name =
          aggregate_fields[i + fields_offset].name_and_type.name;
      Identifier* found_name = initializers[i].name;
      if (field_name != found_name->value) {
        Error("Expected field name \"", field_name, "\" instead of \"",
              found_name->value, "\"")
            .Position(found_name->pos)
            .Throw();
      }
    }
    if (fields_size != initializers.size()) {
      ReportError("expected ", fields_size, " initializers for ",
                  aggregate_name, " found ", initializers.size());
    }
  }

  InitializerResults VisitInitializerResults(
      const ClassType* class_type,
      const std::vector<NameAndExpression>& expressions);
  LocationReference GenerateFieldReference(
      VisitResult object, const Field& field, const ClassType* class_type,
      bool treat_optional_as_indexed = false);
  LocationReference GenerateFieldReferenceForInit(
      VisitResult object, const Field& field,
      const LayoutForInitialization& layout);
  VisitResult GenerateArrayLength(
      Expression* array_length, Namespace* nspace,
      const std::map<std::string, LocalValue>& bindings);
  VisitResult GenerateArrayLength(VisitResult object, const Field& field);
  VisitResult GenerateArrayLength(const ClassType* class_type,
                                  const InitializerResults& initializer_results,
                                  const Field& field);
  LayoutForInitialization GenerateLayoutForInitialization(
      const ClassType* class_type,
      const InitializerResults& initializer_results);

  void InitializeClass(const ClassType* class_type, VisitResult allocate_result,
                       const InitializerResults& initializer_results,
                       const LayoutForInitialization& layout);

  VisitResult Visit(StructExpression* decl);

  LocationReference GetLocationReference(Expression* location);
  LocationReference LookupLocalValue(const std::string& name);
  LocationReference GetLocationReference(IdentifierExpression* expr);
  LocationReference GetLocationReference(DereferenceExpression* expr);
  LocationReference GetLocationReference(FieldAccessExpression* expr);
  LocationReference GenerateFieldAccess(
      LocationReference reference, const std::string& fieldname,
      bool ignore_stuct_field_constness = false,
      std::optional<SourcePosition> pos = {});
  LocationReference GetLocationReference(ElementAccessExpression* expr);
  LocationReference GenerateReferenceToItemInHeapSlice(LocationReference slice,
                                                       VisitResult index);

  VisitResult GenerateFetchFromLocation(const LocationReference& reference);

  VisitResult GetBuiltinCode(Builtin* builtin);

  VisitResult Visit(LocationExpression* expr);
  VisitResult Visit(FieldAccessExpression* expr);

  void VisitAllDeclarables();
  void Visit(Declarable* delarable, std::optional<SourceId> file = {});
  void Visit(TypeAlias* decl);
  VisitResult InlineMacro(Macro* macro,
                          std::optional<LocationReference> this_reference,
                          const std::vector<VisitResult>& arguments,
                          const std::vector<Block*> label_blocks);
  void VisitMacroCommon(Macro* macro);
  void Visit(ExternMacro* macro) {}
  void Visit(TorqueMacro* macro);
  void Visit(Method* macro);
  void Visit(Builtin* builtin);
  void Visit(NamespaceConstant* decl);

  VisitResult Visit(CallExpression* expr, bool is_tail = false);
  VisitResult Visit(CallMethodExpression* expr);
  VisitResult Visit(IntrinsicCallExpression* intrinsic);
  const Type* Visit(TailCallStatement* stmt);

  VisitResult Visit(ConditionalExpression* expr);

  VisitResult Visit(LogicalOrExpression* expr);
  VisitResult Visit(LogicalAndExpression* expr);

  VisitResult Visit(IncrementDecrementExpression* expr);
  VisitResult Visit(AssignmentExpression* expr);
  VisitResult Visit(StringLiteralExpression* expr);
  VisitResult Visit(FloatingPointLiteralExpression* expr);
  VisitResult Visit(IntegerLiteralExpression* expr);
  VisitResult Visit(AssumeTypeImpossibleExpression* expr);
  VisitResult Visit(TryLabelExpression* expr);
  VisitResult Visit(StatementExpression* expr);
  VisitResult Visit(NewExpression* expr);
  VisitResult Visit(SpreadExpression* expr);

  const Type* Visit(ReturnStatement* stmt);
  const Type* Visit(GotoStatement* stmt);
  const Type* Visit(IfStatement* stmt);
  const Type* Visit(WhileStatement* stmt);
  const Type* Visit(BreakStatement* stmt);
  const Type* Visit(ContinueStatement* stmt);
  const Type* Visit(ForLoopStatement* stmt);
  const Type* Visit(VarDeclarationStatement* stmt);
  const Type* Visit(VarDeclarationStatement* stmt,
                    BlockBindings<LocalValue>* block_bindings);
  const Type* Visit(BlockStatement* block);
  const Type* Visit(ExpressionStatement* stmt);
  const Type* Visit(DebugStatement* stmt);
  const Type* Visit(AssertStatement* stmt);

  void BeginGeneratedFiles();
  void EndGeneratedFiles();
  void BeginDebugMacrosFile();
  void EndDebugMacrosFile();

  void GenerateImplementation(const std::string& dir);

  DECLARE_CONTEXTUAL_VARIABLE(ValueBindingsManager,
                              BindingsManager<LocalValue>);
  DECLARE_CONTEXTUAL_VARIABLE(LabelBindingsManager,
                              BindingsManager<LocalLabel>);
  DECLARE_CONTEXTUAL_VARIABLE(CurrentCallable, Callable*);
  DECLARE_CONTEXTUAL_VARIABLE(CurrentFileStreams,
                              GlobalContext::PerFileStreams*);
  DECLARE_CONTEXTUAL_VARIABLE(CurrentReturnValue, std::optional<VisitResult>);

  // A BindingsManagersScope has to be active for local bindings to be created.
  // Shadowing an existing BindingsManagersScope by creating a new one hides all
  // existing bindings while the additional BindingsManagersScope is active.
  struct BindingsManagersScope {
    ValueBindingsManager::Scope value_bindings_manager;
    LabelBindingsManager::Scope label_bindings_manager;
  };

  void SetDryRun(bool is_dry_run) { is_dry_run_ = is_dry_run; }

 private:
  std::optional<Block*> GetCatchBlock();
  void GenerateCatchBlock(std::optional<Block*> catch_block);

  // {StackScope} records the stack height at creation time and reconstructs it
  // when being destructed by emitting a {DeleteRangeInstruction}, except for
  // the slots protected by {StackScope::Yield}. Calling {Yield(v)} deletes all
  // slots above the initial stack height except for the slots of {v}, which are
  // moved to form the only slots above the initial height and marks them to
  // survive destruction of the {StackScope}. A typical pattern is the
  // following:
  //
  // VisitResult result;
  // {
  //   StackScope stack_scope(this);
  //   // ... create temporary slots ...
  //   result = stack_scope.Yield(surviving_slots);
  // }
  class V8_NODISCARD StackScope {
   public:
    explicit StackScope(ImplementationVisitor* visitor) : visitor_(visitor) {
      base_ = visitor_->assembler().CurrentStack().AboveTop();
    }
    VisitResult Yield(VisitResult result) {
      DCHECK(!closed_);
      closed_ = true;
      if (!result.IsOnStack()) {
        if (!visitor_->assembler().CurrentBlockIsComplete()) {
          visitor_->assembler().DropTo(base_);
        }
        return result;
      }
      DCHECK_LE(base_, result.stack_range().begin());
      DCHECK_LE(result.stack_range().end(),
                visitor_->assembler().CurrentStack().AboveTop());
      visitor_->assembler().DropTo(result.stack_range().end());
      visitor_->assembler().DeleteRange(
          StackRange{base_, result.stack_range().begin()});
      base_ = visitor_->assembler().CurrentStack().AboveTop();
      return VisitResult(result.type(), visitor_->assembler().TopRange(
                                            result.stack_range().Size()));
    }

    void Close() {
      DCHECK(!closed_);
      closed_ = true;
      if (!visitor_->assembler().CurrentBlockIsComplete()) {
        visitor_->assembler().DropTo(base_);
      }
    }

    ~StackScope() {
      if (closed_) {
        DCHECK_IMPLIES(
            !visitor_->assembler().CurrentBlockIsComplete(),
            base_ == visitor_->assembler().CurrentStack().AboveTop());
      } else {
        Close();
      }
    }

   private:
    ImplementationVisitor* visitor_;
    BottomOffset base_;
    bool closed_ = false;
  };

  class BreakContinueActivator {
   public:
    BreakContinueActivator(Block* break_block, Block* continue_block)
        : break_binding_{&LabelBindingsManager::Get(), kBreakLabelName,
                         LocalLabel{break_block}},
          continue_binding_{&LabelBindingsManager::Get(), kContinueLabelName,
                            LocalLabel{continue_block}} {}

   private:
    Binding<LocalLabel> break_binding_;
    Binding<LocalLabel> continue_binding_;
  };

  std::optional<Binding<LocalValue>*> TryLookupLocalValue(
      const std::string& name);
  std::optional<Binding<LocalLabel>*> TryLookupLabel(const std::string& name);
  Binding<LocalLabel>* LookupLabel(const std::string& name);
  Block* LookupSimpleLabel(const std::string& name);
  template <class Container>
  Callable* LookupCallable(const QualifiedName& name,
                           const Container& declaration_container,
                           const TypeVector& types,
                           const std::vector<Binding<LocalLabel>*>& labels,
                           const TypeVector& specialization_types,
                           bool silence_errors = false);
  bool TestLookupCallable(const QualifiedName& name,
                          const TypeVector& parameter_types);

  template <class Container>
  Callable* LookupCallable(const QualifiedName& name,
                           const Container& declaration_container,
                           const Arguments& arguments,
                           const TypeVector& specialization_types);

  Method* LookupMethod(const std::string& name,
                       const AggregateType* receiver_type,
                       const Arguments& arguments,
                       const TypeVector& specialization_types);

  TypeArgumentInference InferSpecializationTypes(
      GenericCallable* generic, const TypeVector& explicit_specialization_types,
      const TypeVector& explicit_arguments);

  const Type* GetCommonType(const Type* left, const Type* right);

  VisitResult GenerateCopy(const VisitResult& to_copy);

  void GenerateAssignToLocation(const LocationReference& reference,
                                const VisitResult& assignment_value);

  void AddCallParameter(Callable* callable, VisitResult parameter,
                        const Type* parameter_type,
                        std::vector<VisitResult>* converted_arguments,
                        StackRange* argument_range,
                        std::vector<std::string>* constexpr_arguments,
                        bool inline_macro);

  VisitResult GenerateCall(Callable* callable,
                           std::optional<LocationReference> this_parameter,
                           Arguments parameters,
                           const TypeVector& specialization_types = {},
                           bool tail_call = false);
  VisitResult GenerateCall(const QualifiedName& callable_name,
                           Arguments parameters,
                           const TypeVector& specialization_types = {},
                           bool tail_call = false);
  VisitResult GenerateCall(std::string callable_name, Arguments parameters,
                           const TypeVector& specialization_types = {},
                           bool tail_call = false) {
    return GenerateCall(QualifiedName(std::move(callable_name)),
                        std::move(parameters), specialization_types, tail_call);
  }
  VisitResult GeneratePointerCall(Expression* callee,
                                  const Arguments& parameters, bool tail_call);

  void GenerateBranch(const VisitResult& condition, Block* true_block,
                      Block* false_block);

  VisitResult GenerateBoolConstant(bool constant);

  void GenerateExpressionBranch(Expression* expression, Block* true_block,
                                Block* false_block);

  cpp::Function GenerateMacroFunctionDeclaration(Macro* macro);

  cpp::Function GenerateFunction(
      cpp::Class* owner, const std::string& name, const Signature& signature,
      const NameVector& parameter_names, bool pass_code_assembler_state = true,
      std::vector<std::string>* generated_parameter_names = nullptr);

  VisitResult GenerateImplicitConvert(const Type* destination_type,
                                      VisitResult source);

  StackRange GenerateLabelGoto(LocalLabel* label,
                               std::optional<StackRange> arguments = {});

  VisitResult GenerateSetBitField(const Type* bitfield_struct_type,
                                  const BitField& bitfield,
                                  VisitResult bitfield_struct,
                                  VisitResult value,
                                  bool starts_as_zero = false);

  std::vector<Binding<LocalLabel>*> LabelsFromIdentifiers(
      const std::vector<Identifier*>& names);

  StackRange LowerParameter(const Type* type, const std::string& parameter_name,
                            Stack<std::string>* lowered_parameters);

  void LowerLabelParameter(const Type* type, const std::string& parameter_name,
                           std::vector<std::string>* lowered_parameters);

  std::string ExternalLabelName(const std::string& label_name);
  std::string ExternalLabelParameterName(const std::string& label_name,
                                         size_t i);
  std::string ExternalParameterName(const std::string& name);

  std::ostream& csa_ccfile() {
    if (auto* streams = CurrentFileStreams::Get()) {
      switch (output_type_) {
        case OutputType::kCSA:
          return streams->csa_ccfile;
        case OutputType::kCC:
          return streams->class_definition_inline_headerfile_macro_definitions;
        case OutputType::kCCDebug:
          return debug_macros_cc_;
        default:
          UNREACHABLE();
      }
    }
    return null_stream_;
  }
  std::ostream& csa_headerfile() {
    if (auto* streams = CurrentFileStreams::Get()) {
      switch (output_type_) {
        case OutputType::kCSA:
          return streams->csa_headerfile;
        case OutputType::kCC:
          return streams->class_definition_inline_headerfile_macro_declarations;
        case OutputType::kCCDebug:
          return debug_macros_h_;
        default:
          UNREACHABLE();
      }
    }
    return null_stream_;
  }

  CfgAssembler& assembler() { return *assembler_; }

  void SetReturnValue(VisitResult return_value) {
    std::optional<VisitResult>& current_return_value =
        CurrentReturnValue::Get();
    DCHECK_IMPLIES(current_return_value, *current_return_value == return_value);
    current_return_value = std::move(return_value);
  }

  VisitResult GetAndClearReturnValue() {
    VisitResult return_value = *CurrentReturnValue::Get();
    CurrentReturnValue::Get() = std::nullopt;
    return return_value;
  }

  void WriteFile(const std::string& file, const std::string& content) {
    if (is_dry_run_) return;
    ReplaceFileContentsIfDifferent(file, content);
  }

  const Identifier* TryGetSourceForBitfieldExpression(
      const Expression* expr) const {
    auto it = bitfield_expressions_.find(expr);
    if (it == bitfield_expressions_.end()) return nullptr;
    return it->second;
  }

  void PropagateBitfieldMark(const Expression* original,
                             const Expression* derived) {
    if (const Identifier* source =
            TryGetSourceForBitfieldExpression(original)) {
      bitfield_expressions_[derived] = source;
    }
  }

  class MacroInliningScope;

  std::optional<CfgAssembler> assembler_;
  NullOStream null_stream_;
  bool is_dry_run_;

  // Just for allowing us to emit warnings. After visiting an Expression, if
  // that Expression is a bitfield load, plus an optional inversion or an
  // equality check with a constant, then that Expression will be present in
  // this map. The Identifier associated is the bitfield struct that contains
  // the value to load.
  std::unordered_map<const Expression*, const Identifier*>
      bitfield_expressions_;

  // For emitting warnings. Contains the current set of macros being inlined in
  // calls to InlineMacro.
  std::unordered_set<const Macro*> inlining_macros_;

  // The contents of the debug macros output files. These contain all Torque
  // macros that have been generated using the C++ backend with debug purpose.
  std::stringstream debug_macros_cc_;
  std::stringstream debug_macros_h_;

  OutputType output_type_ = OutputType::kCSA;
};

void ReportAllUnusedMacros();

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_IMPLEMENTATION_VISITOR_H_
