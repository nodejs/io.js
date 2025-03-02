// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARABLE_H_
#define V8_TORQUE_DECLARABLE_H_

#include <cassert>
#include <optional>
#include <string>
#include <unordered_map>

#include "src/base/hashing.h"
#include "src/base/logging.h"
#include "src/torque/ast.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

class Scope;
class Namespace;
class TypeArgumentInference;

DECLARE_CONTEXTUAL_VARIABLE(CurrentScope, Scope*);

struct QualifiedName {
  std::vector<std::string> namespace_qualification;
  std::string name;

  QualifiedName(std::vector<std::string> namespace_qualification,
                std::string name)
      : namespace_qualification(std::move(namespace_qualification)),
        name(std::move(name)) {}
  explicit QualifiedName(std::string name)
      : QualifiedName({}, std::move(name)) {}

  static QualifiedName Parse(std::string qualified_name);

  bool HasNamespaceQualification() const {
    return !namespace_qualification.empty();
  }

  QualifiedName DropFirstNamespaceQualification() const {
    return QualifiedName{
        std::vector<std::string>(namespace_qualification.begin() + 1,
                                 namespace_qualification.end()),
        name};
  }

  friend std::ostream& operator<<(std::ostream& os, const QualifiedName& name);
};

class Declarable {
 public:
  virtual ~Declarable() = default;
  enum Kind {
    kNamespace,
    kTorqueMacro,
    kExternMacro,
    kMethod,
    kBuiltin,
    kRuntimeFunction,
    kIntrinsic,
    kGenericCallable,
    kGenericType,
    kTypeAlias,
    kExternConstant,
    kNamespaceConstant
  };
  Kind kind() const { return kind_; }
  bool IsNamespace() const { return kind() == kNamespace; }
  bool IsMacro() const { return IsTorqueMacro() || IsExternMacro(); }
  bool IsTorqueMacro() const { return kind() == kTorqueMacro || IsMethod(); }
  bool IsMethod() const { return kind() == kMethod; }
  bool IsExternMacro() const { return kind() == kExternMacro; }
  bool IsIntrinsic() const { return kind() == kIntrinsic; }
  bool IsBuiltin() const { return kind() == kBuiltin; }
  bool IsRuntimeFunction() const { return kind() == kRuntimeFunction; }
  bool IsGenericCallable() const { return kind() == kGenericCallable; }
  bool IsGenericType() const { return kind() == kGenericType; }
  bool IsTypeAlias() const { return kind() == kTypeAlias; }
  bool IsExternConstant() const { return kind() == kExternConstant; }
  bool IsNamespaceConstant() const { return kind() == kNamespaceConstant; }
  bool IsValue() const { return IsExternConstant() || IsNamespaceConstant(); }
  bool IsScope() const { return IsNamespace() || IsCallable(); }
  bool IsCallable() const {
    return IsMacro() || IsBuiltin() || IsRuntimeFunction() || IsIntrinsic() ||
           IsMethod();
  }
  virtual const char* type_name() const { return "<<unknown>>"; }
  Scope* ParentScope() const { return parent_scope_; }

  // The SourcePosition of the whole declarable. For example, for a macro
  // this will encompass not only the signature, but also the body.
  SourcePosition Position() const { return position_; }
  void SetPosition(const SourcePosition& position) { position_ = position; }

  // The SourcePosition of the identifying name of the declarable. For example,
  // for a macro this will be the SourcePosition of the name.
  // Note that this SourcePosition might not make sense for all kinds of
  // declarables, in that case, the default SourcePosition is returned.
  SourcePosition IdentifierPosition() const {
    return identifier_position_.source.IsValid() ? identifier_position_
                                                 : position_;
  }
  void SetIdentifierPosition(const SourcePosition& position) {
    identifier_position_ = position;
  }

  bool IsUserDefined() const { return is_user_defined_; }
  void SetIsUserDefined(bool is_user_defined) {
    is_user_defined_ = is_user_defined;
  }

 protected:
  explicit Declarable(Kind kind) : kind_(kind) {}

 private:
  const Kind kind_;
  Scope* const parent_scope_ = CurrentScope::Get();
  SourcePosition position_ = CurrentSourcePosition::Get();
  SourcePosition identifier_position_ = SourcePosition::Invalid();
  bool is_user_defined_ = true;
};

#define DECLARE_DECLARABLE_BOILERPLATE(x, y)                  \
  static x* cast(Declarable* declarable) {                    \
    DCHECK(declarable->Is##x());                              \
    return static_cast<x*>(declarable);                       \
  }                                                           \
  static const x* cast(const Declarable* declarable) {        \
    DCHECK(declarable->Is##x());                              \
    return static_cast<const x*>(declarable);                 \
  }                                                           \
  const char* type_name() const override { return #y; }       \
  static x* DynamicCast(Declarable* declarable) {             \
    if (!declarable) return nullptr;                          \
    if (!declarable->Is##x()) return nullptr;                 \
    return static_cast<x*>(declarable);                       \
  }                                                           \
  static const x* DynamicCast(const Declarable* declarable) { \
    if (!declarable) return nullptr;                          \
    if (!declarable->Is##x()) return nullptr;                 \
    return static_cast<const x*>(declarable);                 \
  }

// Information about what code caused a specialization to exist. This is used
// for error reporting.
struct SpecializationRequester {
  // The position of the expression that caused this specialization.
  SourcePosition position;
  // The Scope which contains the expression that caused this specialization.
  // It may in turn also be within a specialization, which allows us to print
  // the stack of requesters when an error occurs.
  Scope* scope;
  // The name of the specialization.
  std::string name;

  static SpecializationRequester None() {
    return {SourcePosition::Invalid(), nullptr, ""};
  }

  bool IsNone() const {
    return position == SourcePosition::Invalid() && scope == nullptr &&
           name == "";
  }
  SpecializationRequester(SourcePosition position, Scope* scope,
                          std::string name);
};

class Scope : public Declarable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Scope, scope)
  explicit Scope(Declarable::Kind kind) : Declarable(kind) {}

  std::vector<Declarable*> LookupShallow(const QualifiedName& name) {
    if (!name.HasNamespaceQualification()) return declarations_[name.name];
    Scope* child = nullptr;
    for (Declarable* declarable :
         declarations_[name.namespace_qualification.front()]) {
      if (Scope* scope = Scope::DynamicCast(declarable)) {
        if (child != nullptr) {
          ReportError("ambiguous reference to scope ",
                      name.namespace_qualification.front());
        }
        child = scope;
      }
    }
    if (child == nullptr) return {};
    return child->LookupShallow(name.DropFirstNamespaceQualification());
  }

  std::vector<Declarable*> Lookup(const QualifiedName& name);
  template <class T>
  T* AddDeclarable(const std::string& name, T* declarable) {
    declarations_[name].push_back(declarable);
    return declarable;
  }

  const SpecializationRequester& GetSpecializationRequester() const {
    return requester_;
  }
  void SetSpecializationRequester(const SpecializationRequester& requester) {
    requester_ = requester;
  }

 private:
  std::unordered_map<std::string, std::vector<Declarable*>> declarations_;

  // If this Scope was created for specializing a generic type or callable,
  // then {requester_} refers to the place that caused the specialization so we
  // can construct useful error messages.
  SpecializationRequester requester_ = SpecializationRequester::None();
};

class Namespace : public Scope {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Namespace, namespace)
  explicit Namespace(const std::string& name)
      : Scope(Declarable::kNamespace), name_(name) {}
  const std::string& name() const { return name_; }
  bool IsDefaultNamespace() const;
  bool IsTestNamespace() const;

 private:
  std::string name_;
};

inline Namespace* CurrentNamespace() {
  Scope* scope = CurrentScope::Get();
  while (true) {
    if (Namespace* n = Namespace::DynamicCast(scope)) {
      return n;
    }
    scope = scope->ParentScope();
  }
}

class Value : public Declarable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Value, value)
  const Identifier* name() const { return name_; }
  virtual bool IsConst() const { return true; }
  VisitResult value() const { return *value_; }
  const Type* type() const { return type_; }

  void set_value(VisitResult value) {
    DCHECK(!value_);
    value_ = value;
  }

 protected:
  Value(Kind kind, const Type* type, Identifier* name)
      : Declarable(kind), type_(type), name_(name) {}

 private:
  const Type* type_;
  Identifier* name_;
  std::optional<VisitResult> value_;
};

class NamespaceConstant : public Value {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(NamespaceConstant, constant)

  const std::string& external_name() const { return external_name_; }
  Expression* body() const { return body_; }

 private:
  friend class Declarations;
  explicit NamespaceConstant(Identifier* constant_name,
                             std::string external_name, const Type* type,
                             Expression* body)
      : Value(Declarable::kNamespaceConstant, type, constant_name),
        external_name_(std::move(external_name)),
        body_(body) {}

  std::string external_name_;
  Expression* body_;
};

class ExternConstant : public Value {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(ExternConstant, constant)

 private:
  friend class Declarations;
  explicit ExternConstant(Identifier* name, const Type* type, std::string value)
      : Value(Declarable::kExternConstant, type, name) {
    set_value(VisitResult(type, std::move(value)));
  }
};

enum class OutputType {
  kCSA,
  kCC,
  kCCDebug,
};

class Callable : public Scope {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Callable, callable)
  const std::string& ExternalName() const { return external_name_; }
  const std::string& ReadableName() const { return readable_name_; }
  const Signature& signature() const { return signature_; }
  bool IsTransitioning() const { return signature().transitioning; }
  const NameVector& parameter_names() const {
    return signature_.parameter_names;
  }
  bool HasReturnValue() const {
    return !signature_.return_type->IsVoidOrNever();
  }
  void IncrementReturns() { ++returns_; }
  bool HasReturns() const { return returns_; }
  std::optional<Statement*> body() const { return body_; }
  bool IsExternal() const { return !body_.has_value(); }
  virtual bool ShouldBeInlined(OutputType output_type) const {
    // C++ output doesn't support exiting to labels, so functions with labels in
    // the signature must be inlined.
    return output_type == OutputType::kCC && !signature().labels.empty();
  }
  bool ShouldGenerateExternalCode(OutputType output_type) const {
    return !ShouldBeInlined(output_type);
  }

  static std::string PrefixNameForCCOutput(const std::string& name) {
    // If a Torque macro requires a C++ runtime function to be generated, then
    // the generated function begins with this prefix to avoid any naming
    // collisions with the generated CSA function for the same macro.
    return "TqRuntime" + name;
  }

  static std::string PrefixNameForCCDebugOutput(const std::string& name) {
    // If a Torque macro requires a C++ runtime function to be generated, then
    // the generated function begins with this prefix to avoid any naming
    // collisions with the generated CSA function for the same macro.
    return "TqDebug" + name;
  }

  // Name to use in runtime C++ code.
  virtual std::string CCName() const {
    return PrefixNameForCCOutput(ExternalName());
  }

  // Name to use in debug C++ code.
  virtual std::string CCDebugName() const {
    return PrefixNameForCCDebugOutput(ExternalName());
  }

 protected:
  Callable(Declarable::Kind kind, std::string external_name,
           std::string readable_name, Signature signature,
           std::optional<Statement*> body)
      : Scope(kind),
        external_name_(std::move(external_name)),

        readable_name_(std::move(readable_name)),
        signature_(std::move(signature)),
        returns_(0),
        body_(body) {
    DCHECK(!body || *body);
  }

 private:
  std::string external_name_;
  std::string readable_name_;
  Signature signature_;
  size_t returns_;
  std::optional<Statement*> body_;
};

class Macro : public Callable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Macro, macro)
  bool ShouldBeInlined(OutputType output_type) const override {
    for (const LabelDeclaration& label : signature().labels) {
      for (const Type* type : label.types) {
        if (type->StructSupertype()) return true;
      }
    }
    // Intrinsics that are used internally in Torque and implemented as torque
    // code should be inlined and not generate C++ definitions.
    if (ReadableName()[0] == '%') return true;
    return Callable::ShouldBeInlined(output_type);
  }

  void SetUsed() { used_ = true; }
  bool IsUsed() const { return used_; }

 protected:
  Macro(Declarable::Kind kind, std::string external_name,
        std::string readable_name, const Signature& signature,
        std::optional<Statement*> body)
      : Callable(kind, std::move(external_name), std::move(readable_name),
                 signature, body),
        used_(false) {
    if (signature.parameter_types.var_args) {
      ReportError("Varargs are not supported for macros.");
    }
  }

 private:
  bool used_;
};

class ExternMacro : public Macro {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(ExternMacro, ExternMacro)

  const std::string& external_assembler_name() const {
    return external_assembler_name_;
  }

  std::string CCName() const override {
    return "TorqueRuntimeMacroShims::" + external_assembler_name() +
           "::" + ExternalName();
  }

  std::string CCDebugName() const override {
    return "TorqueDebugMacroShims::" + external_assembler_name() +
           "::" + ExternalName();
  }

 private:
  friend class Declarations;
  ExternMacro(const std::string& name, std::string external_assembler_name,
              Signature signature)
      : Macro(Declarable::kExternMacro, name, name, std::move(signature),
              std::nullopt),
        external_assembler_name_(std::move(external_assembler_name)) {}

  std::string external_assembler_name_;
};

class TorqueMacro : public Macro {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(TorqueMacro, TorqueMacro)
  bool IsExportedToCSA() const { return exported_to_csa_; }
  std::string CCName() const override {
    // Exported functions must have unique and C++-friendly readable names, so
    // prefer those wherever possible.
    return PrefixNameForCCOutput(IsExportedToCSA() ? ReadableName()
                                                   : ExternalName());
  }
  std::string CCDebugName() const override {
    // Exported functions must have unique and C++-friendly readable names, so
    // prefer those wherever possible.
    return PrefixNameForCCDebugOutput(IsExportedToCSA() ? ReadableName()
                                                        : ExternalName());
  }

 protected:
  TorqueMacro(Declarable::Kind kind, std::string external_name,
              std::string readable_name, const Signature& signature,
              std::optional<Statement*> body, bool is_user_defined,
              bool exported_to_csa)
      : Macro(kind, std::move(external_name), std::move(readable_name),
              signature, body),
        exported_to_csa_(exported_to_csa) {
    SetIsUserDefined(is_user_defined);
  }

 private:
  friend class Declarations;
  TorqueMacro(std::string external_name, std::string readable_name,
              const Signature& signature, std::optional<Statement*> body,
              bool is_user_defined, bool exported_to_csa)
      : TorqueMacro(Declarable::kTorqueMacro, std::move(external_name),
                    std::move(readable_name), signature, body, is_user_defined,
                    exported_to_csa) {}

  bool exported_to_csa_ = false;
};

class Method : public TorqueMacro {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Method, Method)
  bool ShouldBeInlined(OutputType output_type) const override {
    return Macro::ShouldBeInlined(output_type) ||
           signature()
               .parameter_types.types[signature().implicit_count]
               ->IsStructType();
  }
  AggregateType* aggregate_type() const { return aggregate_type_; }

 private:
  friend class Declarations;
  Method(AggregateType* aggregate_type, std::string external_name,
         std::string readable_name, const Signature& signature, Statement* body)
      : TorqueMacro(Declarable::kMethod, std::move(external_name),
                    std::move(readable_name), signature, body, true, false),
        aggregate_type_(aggregate_type) {}
  AggregateType* aggregate_type_;
};

class Builtin : public Callable {
 public:
  enum Kind { kStub, kFixedArgsJavaScript, kVarArgsJavaScript };
  enum class Flag { kNone = 0, kCustomInterfaceDescriptor = 1 << 0 };
  using Flags = base::Flags<Flag>;
  DECLARE_DECLARABLE_BOILERPLATE(Builtin, builtin)
  Kind kind() const { return kind_; }
  Flags flags() const { return flags_; }
  std::optional<std::string> use_counter_name() const {
    return use_counter_name_;
  }
  bool IsStub() const { return kind_ == kStub; }
  bool IsVarArgsJavaScript() const { return kind_ == kVarArgsJavaScript; }
  bool IsFixedArgsJavaScript() const { return kind_ == kFixedArgsJavaScript; }
  bool IsJavaScript() const {
    return IsVarArgsJavaScript() || IsFixedArgsJavaScript();
  }
  bool HasCustomInterfaceDescriptor() const {
    return flags_ & Flag::kCustomInterfaceDescriptor;
  }

 private:
  friend class Declarations;
  Builtin(std::string external_name, std::string readable_name,
          Builtin::Kind kind, Flags flags, const Signature& signature,
          std::optional<std::string> use_counter_name,
          std::optional<Statement*> body)
      : Callable(Declarable::kBuiltin, std::move(external_name),
                 std::move(readable_name), signature, body),
        kind_(kind),
        flags_(flags),
        use_counter_name_(use_counter_name) {}

  Kind kind_;
  Flags flags_;
  std::optional<std::string> use_counter_name_;
};

class RuntimeFunction : public Callable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(RuntimeFunction, runtime)

 private:
  friend class Declarations;
  RuntimeFunction(const std::string& name, const Signature& signature)
      : Callable(Declarable::kRuntimeFunction, name, name, signature,
                 std::nullopt) {}
};

class Intrinsic : public Callable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Intrinsic, intrinsic)

 private:
  friend class Declarations;
  Intrinsic(std::string name, const Signature& signature)
      : Callable(Declarable::kIntrinsic, name, name, signature, std::nullopt) {
    if (signature.parameter_types.var_args) {
      ReportError("Varargs are not supported for intrinsics.");
    }
  }
};

class TypeConstraint {
 public:
  std::optional<std::string> IsViolated(const Type*) const;

  static TypeConstraint Unconstrained() { return {}; }
  static TypeConstraint SubtypeConstraint(const Type* upper_bound) {
    TypeConstraint result;
    result.upper_bound = {upper_bound};
    return result;
  }

 private:
  std::optional<const Type*> upper_bound;
};

std::optional<std::string> FindConstraintViolation(
    const std::vector<const Type*>& types,
    const std::vector<TypeConstraint>& constraints);

std::vector<TypeConstraint> ComputeConstraints(
    Scope* scope, const GenericParameters& parameters);

template <class SpecializationType, class DeclarationType>
class GenericDeclarable : public Declarable {
 private:
  using Map = std::unordered_map<TypeVector, SpecializationType,
                                 base::hash<TypeVector>>;

 public:
  void AddSpecialization(const TypeVector& type_arguments,
                         SpecializationType specialization) {
    DCHECK_EQ(0, specializations_.count(type_arguments));
    if (auto violation =
            FindConstraintViolation(type_arguments, Constraints())) {
      Error(*violation).Throw();
    }
    specializations_[type_arguments] = specialization;
  }
  std::optional<SpecializationType> GetSpecialization(
      const TypeVector& type_arguments) const {
    auto it = specializations_.find(type_arguments);
    if (it != specializations_.end()) return it->second;
    return std::nullopt;
  }

  using iterator = typename Map::const_iterator;
  iterator begin() const { return specializations_.begin(); }
  iterator end() const { return specializations_.end(); }

  const std::string& name() const { return name_; }
  auto declaration() const { return generic_declaration_->declaration; }
  const GenericParameters& generic_parameters() const {
    return generic_declaration_->generic_parameters;
  }

  const std::vector<TypeConstraint>& Constraints() {
    if (!constraints_)
      constraints_ = {ComputeConstraints(ParentScope(), generic_parameters())};
    return *constraints_;
  }

 protected:
  GenericDeclarable(Declarable::Kind kind, const std::string& name,
                    DeclarationType generic_declaration)
      : Declarable(kind),
        name_(name),
        generic_declaration_(generic_declaration) {
    DCHECK(!generic_declaration->generic_parameters.empty());
  }

 private:
  std::string name_;
  DeclarationType generic_declaration_;
  Map specializations_;
  std::optional<std::vector<TypeConstraint>> constraints_;
};

class GenericCallable
    : public GenericDeclarable<Callable*, GenericCallableDeclaration*> {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(GenericCallable, generic_callable)

  std::optional<Statement*> CallableBody();

  TypeArgumentInference InferSpecializationTypes(
      const TypeVector& explicit_specialization_types,
      const std::vector<std::optional<const Type*>>& arguments);

 private:
  friend class Declarations;
  GenericCallable(const std::string& name,
                  GenericCallableDeclaration* generic_declaration)
      : GenericDeclarable<Callable*, GenericCallableDeclaration*>(
            Declarable::kGenericCallable, name, generic_declaration) {}
};

class GenericType
    : public GenericDeclarable<const Type*, GenericTypeDeclaration*> {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(GenericType, generic_type)

 private:
  friend class Declarations;
  GenericType(const std::string& name,
              GenericTypeDeclaration* generic_declaration)
      : GenericDeclarable<const Type*, GenericTypeDeclaration*>(
            Declarable::kGenericType, name, generic_declaration) {}
};

class TypeAlias : public Declarable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(TypeAlias, type_alias)

  const Type* type() const {
    if (type_) return *type_;
    return Resolve();
  }
  const Type* Resolve() const;
  bool IsRedeclaration() const { return redeclaration_; }
  SourcePosition GetDeclarationPosition() const {
    return declaration_position_;
  }

 private:
  friend class Declarations;
  friend class TypeVisitor;

  explicit TypeAlias(
      const Type* type, bool redeclaration,
      SourcePosition declaration_position = SourcePosition::Invalid())
      : Declarable(Declarable::kTypeAlias),
        type_(type),
        redeclaration_(redeclaration),
        declaration_position_(declaration_position) {}
  explicit TypeAlias(
      TypeDeclaration* type, bool redeclaration,
      SourcePosition declaration_position = SourcePosition::Invalid())
      : Declarable(Declarable::kTypeAlias),
        delayed_(type),
        redeclaration_(redeclaration),
        declaration_position_(declaration_position) {}

  mutable bool being_resolved_ = false;
  mutable std::optional<TypeDeclaration*> delayed_;
  mutable std::optional<const Type*> type_;
  bool redeclaration_;
  const SourcePosition declaration_position_;
};

std::ostream& operator<<(std::ostream& os, const Callable& m);
std::ostream& operator<<(std::ostream& os, const Builtin& b);
std::ostream& operator<<(std::ostream& os, const RuntimeFunction& b);
std::ostream& operator<<(std::ostream& os, const GenericCallable& g);

#undef DECLARE_DECLARABLE_BOILERPLATE

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_DECLARABLE_H_
